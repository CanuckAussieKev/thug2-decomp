// NOT ORIGINAL - the volumetric clouds' model on the device (vulkan_rt_clouds.h). The GLSL here is
// engine/gfx/rt_clouds.cpp's functions: change both.
#include "gpu_vulkan/vulkan_rt_clouds.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_clouds.h"
#include "engine/gfx/rt_sky.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportFailed = 0x434C0001U;
constexpr uint64_t kReportMade = 0x434C0002U;
constexpr uint32_t kGroupSide = 4;
constexpr std::size_t kChannels = 4;

// rt_clouds.cpp's noise, then the generating pass (SHAPE, DETAIL or WEATHER).
constexpr const char* kNoise = R"(
uint cloud_hash(uint x, uint y, uint z, uint seed)
{
    uint h = (x * 0x8DA6B343u) ^ (y * 0xD8163841u) ^ (z * 0xCB1AB31Fu) ^ (seed * 0x165667B1u);
    h ^= h >> 16u;
    h *= 0x7FEB352Du;
    h ^= h >> 15u;
    h *= 0x846CA68Bu;
    h ^= h >> 16u;
    return h;
}
float unit_of(uint h) { return float(h >> 8u) * (1.0 / 16777216.0); }
uint wrapped(int v, int period) { return uint((v % period + period) % period); }

float cloud_worley(vec3 p, int period, uint seed)
{
    ivec3 cell = ivec3(floor(p));
    vec3 inside = p - vec3(cell);
    float best = 9.0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                uint wx = wrapped(cell.x + dx, period);
                uint wy = wrapped(cell.y + dy, period);
                uint wz = wrapped(cell.z + dz, period);
                vec3 f = vec3(float(dx) + unit_of(cloud_hash(wx, wy, wz, seed)),
                              float(dy) + unit_of(cloud_hash(wx, wy, wz, seed + 1u)),
                              float(dz) + unit_of(cloud_hash(wx, wy, wz, seed + 2u))) - inside;
                best = min(best, dot(f, f));
            }
        }
    }
    return 1.0 - clamp(sqrt(best), 0.0, 1.0);
}

const ivec3 GRADIENTS[16] = ivec3[16](ivec3(1, 1, 0), ivec3(-1, 1, 0), ivec3(1, -1, 0), ivec3(-1, -1, 0),
                                      ivec3(1, 0, 1), ivec3(-1, 0, 1), ivec3(1, 0, -1), ivec3(-1, 0, -1),
                                      ivec3(0, 1, 1), ivec3(0, -1, 1), ivec3(0, 1, -1), ivec3(0, -1, -1),
                                      ivec3(1, 1, 0), ivec3(-1, 1, 0), ivec3(0, -1, 1), ivec3(0, -1, -1));

float fade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

float cloud_perlin(vec3 p, int period, uint seed)
{
    ivec3 cell = ivec3(floor(p));
    vec3 f = p - vec3(cell);
    float corner[8];
    for (int i = 0; i < 8; ++i) {
        ivec3 o = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        uint h = cloud_hash(wrapped(cell.x + o.x, period), wrapped(cell.y + o.y, period), wrapped(cell.z + o.z, period), seed);
        corner[i] = dot(vec3(GRADIENTS[h & 15u]), f - vec3(o));
    }
    vec3 u = vec3(fade(f.x), fade(f.y), fade(f.z));
    float x00 = mix(corner[0], corner[1], u.x);
    float x10 = mix(corner[2], corner[3], u.x);
    float x01 = mix(corner[4], corner[5], u.x);
    float x11 = mix(corner[6], corner[7], u.x);
    return mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z);
}

float worley_fbm(vec3 p, int cells, uint seed)
{
    return 0.625 * cloud_worley(p * float(cells), cells, seed) +
           0.25 * cloud_worley(p * float(2 * cells), 2 * cells, seed + 1u) +
           0.125 * cloud_worley(p * float(4 * cells), 4 * cells, seed + 2u);
}

float remap(float v, float lo, float hi, float a, float b) { return a + (v - lo) * (b - a) / (hi - lo); }
)";

constexpr const char* kNoiseMain = R"(
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
#ifdef WEATHER
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D o_noise;
#else
layout(set = 0, binding = 0, rgba8) uniform writeonly image3D o_noise;
#endif

void main()
{
    uvec3 q = gl_GlobalInvocationID;
#if defined(SHAPE)
    if (any(greaterThanEqual(q, uvec3(SHAPE_SIZE)))) {
        return;
    }
    vec3 p = (vec3(q) + 0.5) / float(SHAPE_SIZE);
    float perlin = cloud_perlin(p * 4.0, 4, 11u) * 0.5 + cloud_perlin(p * 8.0, 8, 12u) * 0.25 +
                   cloud_perlin(p * 16.0, 16, 13u) * 0.125;
    float perlin01 = clamp(perlin * 0.9 + 0.5, 0.0, 1.0);
    float worley = worley_fbm(p, 4, 21u);
    vec4 value = vec4(clamp(remap(perlin01, worley - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0), worley, worley_fbm(p, 8, 31u),
                      worley_fbm(p, 16, 41u));
    imageStore(o_noise, ivec3(q), value);
#elif defined(DETAIL)
    if (any(greaterThanEqual(q, uvec3(DETAIL_SIZE)))) {
        return;
    }
    vec3 p = (vec3(q) + 0.5) / float(DETAIL_SIZE);
    imageStore(o_noise, ivec3(q), vec4(worley_fbm(p, 4, 51u), worley_fbm(p, 8, 61u), worley_fbm(p, 16, 71u), 1.0));
#else
    if (q.z != 0u || any(greaterThanEqual(q.xy, uvec2(WEATHER_SIZE)))) {
        return;
    }
    vec2 uv = (vec2(q.xy) + 0.5) / float(WEATHER_SIZE);
    float fbm = cloud_perlin(vec3(uv * 4.0, 0.5), 4, 81u) * 0.5 + cloud_perlin(vec3(uv * 8.0, 0.5), 8, 82u) * 0.25 +
                cloud_perlin(vec3(uv * 16.0, 0.5), 16, 83u) * 0.125 + cloud_perlin(vec3(uv * 32.0, 0.5), 32, 84u) * 0.0625;
    imageStore(o_noise, ivec2(q.xy), vec4(clamp(fbm * 1.1 + 0.5, 0.0, 1.0),
                                          clamp(cloud_perlin(vec3(uv * 2.0, 0.5), 2, 91u) * 0.9 + 0.5, 0.0, 1.0),
                                          worley_fbm(vec3(uv, 0.5), 16, 95u), 1.0));
#endif
}
)";

// rt_clouds.cpp's density and march.
constexpr const char* kMarch = R"(
float c_saturate(float v) { return clamp(v, 0.0, 1.0); }
float c_remap(float v, float lo, float hi, float a, float b) { return a + (v - lo) * (b - a) / (hi - lo); }

float cloud_cover(vec4 weather, float coverage)
{
    float patches = c_saturate((weather.r - (1.0 - coverage)) * CLOUD_COVER_EDGE + CLOUD_PATCH_LIFT * coverage);
    float clumps = c_saturate(c_remap(weather.b, CLOUD_CLUMP_LOW, CLOUD_CLUMP_HIGH, 0.0, 1.0));
    float overcast = coverage * coverage * coverage * coverage;
    return patches * mix(clumps, 1.0, overcast);
}
float cloud_threshold_share(float coverage)
{
    return mix(CLOUD_THRESHOLD_LEAST, CLOUD_THRESHOLD_MOST, coverage);
}

float cloud_density(vec3 point, float coverage, bool detail)
{
    float height = (point.y - CLOUD_BASE) / (CLOUD_TOP - CLOUD_BASE);
    if (height <= 0.0 || height >= 1.0) {
        return 0.0;
    }
    vec4 weather = textureLod(u_cloud_weather, point.xz / CLOUD_WEATHER_SCALE, 0.0);
    float cover = cloud_cover(weather, coverage);
    if (cover <= 0.0) {
        return 0.0;
    }
    float top = (0.45 + 0.55 * weather.g) * mix(0.25, 1.0, cover);
    float profile = c_saturate(height / 0.12) * c_saturate((top - height) / (0.4 * top));
    if (profile <= 0.0) {
        return 0.0;
    }
    vec4 shape = textureLod(u_cloud_shape, point / CLOUD_SHAPE_SCALE, 0.0);
    float fbm = shape.g * 0.625 + shape.b * 0.25 + shape.a * 0.125;
    float base = c_saturate(c_remap(shape.r, fbm - 1.0, 1.0, 0.0, 1.0)) * profile;
    float density = c_saturate(c_remap(base, 1.0 - cloud_threshold_share(coverage) * cover, 1.0, 0.0, 1.0)) * profile;
    if (detail && density > 0.0) {
        vec4 fine = textureLod(u_cloud_detail, point / CLOUD_DETAIL_SCALE, 0.0);
        float detail_fbm = fine.r * 0.625 + fine.g * 0.25 + fine.b * 0.125;
        float erode = mix(detail_fbm, 1.0 - detail_fbm, c_saturate(height * 5.0));
        density = c_saturate(c_remap(density, erode * CLOUD_DETAIL_EROSION, 1.0, 0.0, 1.0));
    }
    return density;
}

float c_hg(float c, float g)
{
    float k = max(1.0 + g * g - 2.0 * g * c, 1e-4);
    return (1.0 - g * g) / (4.0 * 3.14159265358979 * k * sqrt(k));
}
float cloud_phase(float c, float scale)
{
    return CLOUD_FORWARD_SHARE * c_hg(c, CLOUD_FORWARD_G * scale) + (1.0 - CLOUD_FORWARD_SHARE) * c_hg(c, CLOUD_BACK_G * scale);
}

struct CloudRay {
    float camera_altitude;
    vec2 offset;
    vec3 direction;
    vec3 to_sun;
    float coverage;
    float density_scale;
    int steps;
    float jitter;
    vec3 sun_light;
    vec3 ambient_bottom;
    vec3 ambient_top;
};
struct CloudResult {
    vec3 light;
    float transmittance;
    float distance;
    bool marched;
};

// where a ray from the camera (r0 from the planet's centre, straight above it) with a vertical component dy leaves the
// sphere of `radius` (-1: never ahead) - the products arranged so floats keep metres
float c_leave(float r0, float dy, float radius)
{
    float b = r0 * dy;
    float c = (r0 - radius) * (r0 + radius);
    float k = b * b - c;
    if (k < 0.0) {
        return -1.0;
    }
    float far = -b + sqrt(k);
    return far > 0.0 ? far : -1.0;
}
// the altitude above the ground t along the ray
float c_altitude(float r0, float dy, float t)
{
    float above = (r0 - CLOUD_GROUND) * (r0 + CLOUD_GROUND) + t * (2.0 * r0 * dy + t);
    return above / (sqrt(max(CLOUD_GROUND * CLOUD_GROUND + above, 0.0)) + CLOUD_GROUND);
}

CloudResult march_clouds(CloudRay ray)
{
    CloudResult result;
    result.light = vec3(0.0);
    result.transmittance = 1.0;
    result.distance = -1.0;
    result.marched = false;
    // (relative to the camera: the planet's centre below it - floats keep metres there)
    float r0 = CLOUD_GROUND + ray.camera_altitude;
    vec3 d = ray.direction;
    // the ground ahead
    float b = d.y * r0;
    if (b < 0.0 && b * b - (r0 - CLOUD_GROUND) * (r0 + CLOUD_GROUND) >= 0.0) {
        return result;
    }
    float start = c_leave(r0, d.y, CLOUD_GROUND + CLOUD_BASE);
    float end = c_leave(r0, d.y, CLOUD_GROUND + CLOUD_TOP);
    if (start < 0.0 || end <= start) {
        return result;
    }
    result.marched = true;
    end = min(end, start + CLOUD_FAR);
    int steps = max(ray.steps, 1);
    float span = (end - start) / float(steps);
    float cosine = dot(d, ray.to_sun);
    float phases[CLOUD_OCTAVES];
    float asymmetry = 1.0;
    for (int k = 0; k < CLOUD_OCTAVES; ++k) {
        phases[k] = cloud_phase(cosine, asymmetry);
        asymmetry *= OCTAVE_ASYMMETRY;
    }
    float scale = CLOUD_DENSITY * ray.density_scale;
    float transmittance = 1.0;
    float weighted = 0.0;
    float weight = 0.0;
    for (int i = 0; i < steps && transmittance >= CLOUD_OPAQUE; ++i) {
        float t = start + (float(i) + ray.jitter) * span;
        float altitude = c_altitude(r0, d.y, t);
        vec3 point = vec3(d.x * t + ray.offset.x, altitude, d.z * t + ray.offset.y);
        float density = cloud_density(point, ray.coverage, true);
        if (density <= 0.0) {
            continue;
        }
        float sigma = density * scale;
        float depth = 0.0;
        float along = 0.0;
        float len = CLOUD_LIGHT_STEP;
        for (int j = 0; j < CLOUD_LIGHT_SAMPLES; ++j) {
            float s = along + 0.5 * len;
            depth += cloud_density(point + ray.to_sun * s, ray.coverage, false) * scale * len;
            along += len;
            len *= CLOUD_LIGHT_GROWTH;
        }
        float lit = 0.0;
        float share = 1.0;
        float extinction = 1.0;
        for (int k = 0; k < CLOUD_OCTAVES; ++k) {
            lit += share * phases[k] * exp(-extinction * depth);
            share *= OCTAVE_SHARE;
            extinction *= OCTAVE_EXTINCTION;
        }
        float powder = mix(1.0, 1.0 - exp(-2.0 * sigma * CLOUD_POWDER_LENGTH), CLOUD_POWDER_SHARE);
        float height = c_saturate((altitude - CLOUD_BASE) / (CLOUD_TOP - CLOUD_BASE));
        float kept = exp(-sigma * span);
        vec3 ambient = mix(ray.ambient_bottom, ray.ambient_top, height);
        vec3 source = ray.sun_light * (lit * powder) + ambient;
        result.light += transmittance * source * (1.0 - kept);
        weighted += transmittance * (1.0 - kept) * t;
        weight += transmittance * (1.0 - kept);
        transmittance *= kept;
    }
    result.transmittance = transmittance;
    result.distance = weight > 1e-6 ? weighted / weight : -1.0;
    return result;
}
)";

struct NoiseState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    Image shape;
    Image detail;
    Image weather;
    std::array<VkPipeline, 3> generators{};
    bool made = false;
    bool failed = false;
};

NoiseState& noise_state()
{
    static NoiseState state;
    return state;
}

std::string number(double value)
{
    std::string out = text::format("%.9g", value);
    if (out.find_first_of(".e") == std::string::npos) {
        out += ".0";
    }
    return out;
}

std::string noise_defines()
{
    return "#define SHAPE_SIZE " + std::to_string(rt::kCloudShapeSize) + "\n#define DETAIL_SIZE " +
           std::to_string(rt::kCloudDetailSize) + "\n#define WEATHER_SIZE " + std::to_string(rt::kCloudWeatherSize) +
           "\n";
}

bool make_volume(Backend& state, uint32_t size, bool three_d, Image* out)
{
    const VkImageCreateInfo info{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                 .pNext = nullptr,
                                 .flags = 0,
                                 .imageType = three_d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
                                 .format = VK_FORMAT_R8G8B8A8_UNORM,
                                 .extent = {.width = size, .height = size, .depth = three_d ? size : 1},
                                 .mipLevels = 1,
                                 .arrayLayers = 1,
                                 .samples = VK_SAMPLE_COUNT_1_BIT,
                                 .tiling = VK_IMAGE_TILING_OPTIMAL,
                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                 .queueFamilyIndexCount = 0,
                                 .pQueueFamilyIndices = nullptr,
                                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(state.memory.allocator(), &info, &allocation, &out->image, &out->allocation, nullptr) !=
        VK_SUCCESS) {
        *out = Image{};
        return false;
    }
    const VkImageViewCreateInfo view{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .image = out->image,
                                     .viewType = three_d ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D,
                                     .format = VK_FORMAT_R8G8B8A8_UNORM,
                                     .components = {},
                                     .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                          .baseMipLevel = 0,
                                                          .levelCount = 1,
                                                          .baseArrayLayer = 0,
                                                          .layerCount = 1}};
    if (vkCreateImageView(state.context.device(), &view, nullptr, &out->view) != VK_SUCCESS) {
        state.memory.destroy_image(out);
        return false;
    }
    out->format = VK_FORMAT_R8G8B8A8_UNORM;
    out->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    out->extent = {.width = size, .height = size};
    out->usage = info.usage;
    out->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    out->resting = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}

bool make_noise_objects(Backend& state, std::string* error)
{
    NoiseState& own = noise_state();
    const VkDevice device = state.context.device();
    const VkDescriptorSetLayoutBinding binding{.binding = 0,
                                               .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                               .descriptorCount = 1,
                                               .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                               .pImmutableSamplers = nullptr};
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = 1,
                                                   .pBindings = &binding};
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = &own.set_layout,
                                                 .pushConstantRangeCount = 0,
                                                 .pPushConstantRanges = nullptr};
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = 0.0F;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS ||
        vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS ||
        vkCreateSampler(device, &sampler, nullptr, &own.sampler) != VK_SUCCESS) {
        *error = "the layouts or the sampler";
        return false;
    }
    return true;
}

VkPipeline make_generator(VkDevice device, VkPipelineLayout layout, const char* which, std::string* error)
{
    const std::string source =
        std::string("#version 460\n#define ") + which + "\n" + noise_defines() + kNoise + kNoiseMain;
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, source);
    if (!compiled.ok) {
        *error = compiled.log;
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                               .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
        return VK_NULL_HANDLE;
    }
    const VkComputePipelineCreateInfo info{.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                           .pNext = nullptr,
                                           .flags = 0,
                                           .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                     .module = module,
                                                     .pName = "main",
                                                     .pSpecializationInfo = nullptr},
                                           .layout = layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateComputePipelines failed";
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

void generate(VkCommandBuffer commands, VkPipeline pipeline, Image* image, uint32_t size, bool three_d)
{
    const NoiseState& own = noise_state();
    Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
    const VkDescriptorImageInfo out{
        .sampler = VK_NULL_HANDLE, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &out;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, 1, &write);
    const uint32_t groups = (size + kGroupSide - 1) / kGroupSide;
    vkCmdDispatch(commands, groups, groups, three_d ? groups : 1);
}

} // namespace

bool update_cloud_noise(Backend& state, VkCommandBuffer commands)
{
    NoiseState& own = noise_state();
    if (own.made || own.failed) {
        return own.made;
    }
    std::string error;
    const VkDevice device = state.context.device();
    std::array<VkPipeline, 3> pipelines{};
    bool ok = make_noise_objects(state, &error) && make_volume(state, rt::kCloudShapeSize, true, &own.shape) &&
              make_volume(state, rt::kCloudDetailSize, true, &own.detail) &&
              make_volume(state, rt::kCloudWeatherSize, false, &own.weather);
    if (ok) {
        constexpr std::array<const char*, 3> kWhich{"SHAPE", "DETAIL", "WEATHER"};
        for (std::size_t i = 0; i < pipelines.size() && ok; ++i) {
            core::at(pipelines, i) = make_generator(device, own.layout, core::at(kWhich, i), &error);
            ok = core::at(pipelines, i) != VK_NULL_HANDLE;
        }
    }
    if (ok) {
        generate(commands, core::at(pipelines, 0), &own.shape, rt::kCloudShapeSize, true);
        generate(commands, core::at(pipelines, 1), &own.detail, rt::kCloudDetailSize, true);
        generate(commands, core::at(pipelines, 2), &own.weather, rt::kCloudWeatherSize, false);
        const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                      .pNext = nullptr,
                                      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                             &written, 0, nullptr, 0, nullptr);
        state.bound = BoundState{};
        cloud_noise_used(state.frames.serial());
    }
    // (the generators run once: kept until the device's shutdown, as the submission uses them)
    own.generators = pipelines;
    if (!ok) {
        own.failed = true;
        if (state.reported.insert(kReportFailed).second) {
            get_log("vulkan: rt_clouds: the clouds' noise couldn't be made (%s) - no clouds", error.c_str());
        }
        return false;
    }
    own.made = true;
    if (state.reported.insert(kReportMade).second) {
        get_log("vulkan: rt_clouds: the clouds' noise made (%u^3 shape, %u^3 detail, %u^2 weather)",
                rt::kCloudShapeSize, rt::kCloudDetailSize, rt::kCloudWeatherSize);
    }
    return true;
}

VkDescriptorImageInfo cloud_noise_descriptor(uint32_t which)
{
    const NoiseState& own = noise_state();
    const Image* image = &own.weather;
    if (which == 0) {
        image = &own.shape;
    } else if (which == 1) {
        image = &own.detail;
    }
    return {.sampler = own.sampler, .imageView = image->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
}

void cloud_noise_used(uint64_t serial)
{
    NoiseState& own = noise_state();
    own.shape.used = serial;
    own.detail.used = serial;
    own.weather.used = serial;
}

std::string cloud_glsl(uint32_t first_binding)
{
    std::string out;
    out += "layout(set = 0, binding = " + std::to_string(first_binding) + ") uniform sampler3D u_cloud_shape;\n";
    out += "layout(set = 0, binding = " + std::to_string(first_binding + 1) + ") uniform sampler3D u_cloud_detail;\n";
    out += "layout(set = 0, binding = " + std::to_string(first_binding + 2) + ") uniform sampler2D u_cloud_weather;\n";
    const auto add = [&out](const char* name, const std::string& value) {
        out += "#define ";
        out += name;
        out += " ";
        out += value;
        out += "\n";
    };
    add("CLOUD_GROUND", number(rt::kGroundRadius));
    add("CLOUD_BASE", number(rt::kCloudBase));
    add("CLOUD_TOP", number(rt::kCloudTop));
    add("CLOUD_SHAPE_SCALE", number(rt::kCloudShapeScale));
    add("CLOUD_DETAIL_SCALE", number(rt::kCloudDetailScale));
    add("CLOUD_WEATHER_SCALE", number(rt::kCloudWeatherScale));
    add("CLOUD_DENSITY", number(rt::kCloudDensity));
    add("CLOUD_COVER_EDGE", number(rt::kCoverEdge));
    add("CLOUD_PATCH_LIFT", number(rt::kPatchLift));
    add("CLOUD_CLUMP_LOW", number(rt::kClumpLow));
    add("CLOUD_CLUMP_HIGH", number(rt::kClumpHigh));
    add("CLOUD_THRESHOLD_LEAST", number(rt::kCoverThresholdLeast));
    add("CLOUD_THRESHOLD_MOST", number(rt::kCoverThresholdMost));
    add("CLOUD_DETAIL_EROSION", number(rt::kDetailErosion));
    add("CLOUD_FAR", number(rt::kCloudFar));
    add("CLOUD_LIGHT_STEP", number(rt::kCloudLightStep));
    add("CLOUD_LIGHT_GROWTH", number(rt::kCloudLightGrowth));
    add("CLOUD_LIGHT_SAMPLES", std::to_string(rt::kCloudLightSamples));
    add("CLOUD_FORWARD_G", number(rt::kCloudForwardG));
    add("CLOUD_BACK_G", number(rt::kCloudBackG));
    add("CLOUD_FORWARD_SHARE", number(rt::kCloudForwardShare));
    add("OCTAVE_SHARE", number(rt::kOctaveShare));
    add("OCTAVE_EXTINCTION", number(rt::kOctaveExtinction));
    add("OCTAVE_ASYMMETRY", number(rt::kOctaveAsymmetry));
    add("CLOUD_OCTAVES", std::to_string(rt::kCloudOctaves));
    add("CLOUD_POWDER_LENGTH", number(rt::kCloudPowderLength));
    add("CLOUD_POWDER_SHARE", number(rt::kCloudPowderShare));
    add("CLOUD_OPAQUE", number(rt::kCloudOpaque));
    out += kMarch;
    return out;
}

bool read_cloud_noise(Backend& state, rt::CloudNoise* out)
{
    NoiseState& own = noise_state();
    if (!own.made || !finish_work(state)) {
        return false;
    }
    const auto read = [&state](Image* image, uint32_t size, bool three_d, std::vector<uint8_t>* bytes) {
        const std::size_t count = static_cast<std::size_t>(size) * size * (three_d ? size : 1) * kChannels;
        DeviceBuffer read_back;
        if (!state.memory.create_buffer(count, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferPlace::kReadBack, &read_back)) {
            return false;
        }
        const bool done = state.memory.submit_now([&](VkCommandBuffer commands) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            const VkBufferImageCopy region{.bufferOffset = 0,
                                           .bufferRowLength = 0,
                                           .bufferImageHeight = 0,
                                           .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                .mipLevel = 0,
                                                                .baseArrayLayer = 0,
                                                                .layerCount = 1},
                                           .imageOffset = {.x = 0, .y = 0, .z = 0},
                                           .imageExtent = {.width = size, .height = size, .depth = three_d ? size : 1}};
            vkCmdCopyImageToBuffer(commands, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read_back.buffer, 1,
                                   &region);
            Memory::transition(commands, image, image->resting);
        });
        if (done) {
            vmaInvalidateAllocation(state.memory.allocator(), read_back.allocation, 0, read_back.bytes);
            bytes->resize(count);
            std::memcpy(bytes->data(), read_back.mapped, count);
        }
        state.memory.destroy_buffer(&read_back);
        return done;
    };
    return read(&own.shape, rt::kCloudShapeSize, true, &out->shape) &&
           read(&own.detail, rt::kCloudDetailSize, true, &out->detail) &&
           read(&own.weather, rt::kCloudWeatherSize, false, &out->weather);
}

void release_cloud_noise(Backend& state)
{
    NoiseState& own = noise_state();
    const VkDevice device = state.context.device();
    for (Image* image : {&own.shape, &own.detail, &own.weather}) {
        if (image->image != VK_NULL_HANDLE) {
            state.memory.destroy_image(image);
        }
    }
    if (device != nullptr) {
        for (VkPipeline pipeline : own.generators) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        if (own.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, own.sampler, nullptr);
        }
    }
    own = NoiseState{};
}

} // namespace render::vulkan
