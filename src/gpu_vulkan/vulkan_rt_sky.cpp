// NOT ORIGINAL - the sky model on the device (vulkan_rt_sky.h; engine/gfx/rt_sky.h is the model and its reference).
#include "gpu_vulkan/vulkan_rt_sky.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportFailed = 0x534B0001U;
constexpr uint64_t kReportFirst = 0x534B0002U;

constexpr uint32_t kGroupSide = 8;
constexpr double kShortest = 1e-6;
constexpr float kSameDirection = 0.99999F; // (cosine) the table kept while the sun moves less than this
constexpr rt::SkySteps kTableSteps{.view = 32, .light = 16};
constexpr rt::SkySteps kColourSteps{.view = 128, .light = 64}; // the processor's zenith colour (sky_colours)

// The table's pass: rt_sky.h's integrals, few steps (the processor's are the reference).
constexpr const char* kTableBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D o_table;
layout(push_constant) uniform Sky {
    vec4 sun;    // xyz the model's sun (cos e, sin e, 0); w the exposure
    ivec4 steps; // x segments along a view ray, y along a ray toward the sun
} sky;

const float PI = 3.14159265358979;

float ray_length(vec3 o, vec3 d, out bool ground)
{
    float r = length(o);
    float mu = dot(o, d) / r;
    float base = (r * r) * (mu * mu - 1.0);
    ground = false;
    if (mu < 0.0) {
        float inside = base + GROUND * GROUND;
        if (inside >= 0.0) {
            ground = true;
            return max(0.0, -r * mu - sqrt(inside));
        }
    }
    return max(0.0, -r * mu + sqrt(max(0.0, base + TOP * TOP)));
}

vec2 densities(vec3 p)
{
    float h = max(0.0, length(p) - GROUND);
    return vec2(exp(-h / RAYLEIGH_HEIGHT), exp(-h / MIE_HEIGHT));
}

vec2 segment(float len, int i, int n)
{
    float a = float(i) / float(n);
    float b = float(i + 1) / float(n);
    return vec2(len * a * a, len * b * b);
}

vec3 extinction(vec2 depth) { return RAYLEIGH * depth.x + vec3(MIE_SCATTERING / MIE_ALBEDO) * depth.y; }

bool depth_to_top(vec3 o, vec3 d, int steps, out vec2 depth)
{
    bool ground;
    float len = ray_length(o, d, ground);
    depth = vec2(0.0);
    if (ground) {
        return false;
    }
    for (int i = 0; i < steps; ++i) {
        vec2 s = segment(len, i, steps);
        depth += densities(o + d * (0.5 * (s.x + s.y))) * (s.y - s.x);
    }
    return true;
}

vec3 viewer() { return vec3(0.0, GROUND + VIEWER_ALTITUDE, 0.0); }

vec3 transmittance(vec3 d)
{
    vec2 depth;
    if (!depth_to_top(viewer(), d, sky.steps.y, depth)) {
        return vec3(0.0);
    }
    return exp(-extinction(depth));
}

vec3 radiance(vec3 view, vec3 sun)
{
    bool ground;
    vec3 o = viewer();
    float len = ray_length(o, view, ground);
    float mu = dot(view, sun);
    float rayleigh_phase = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float g = MIE_G;
    float mie_phase = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) /
                      ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));
    vec3 sum = vec3(0.0);
    vec2 travelled = vec2(0.0);
    int n = sky.steps.x;
    for (int i = 0; i < n; ++i) {
        vec2 s = segment(len, i, n);
        float ds = s.y - s.x;
        vec3 p = o + view * (0.5 * (s.x + s.y));
        vec2 here = densities(p);
        vec2 to_sun;
        if (depth_to_top(p, sun, sky.steps.y, to_sun)) {
            vec3 tau = extinction(travelled + 0.5 * here * ds + to_sun);
            sum += exp(-tau) * (RAYLEIGH * here.x * rayleigh_phase + vec3(MIE_SCATTERING * here.y * mie_phase)) * ds;
        }
        travelled += here * ds;
    }
    return sum;
}

void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (q.x >= WIDTH || q.y > HEIGHT) {
        return;
    }
    vec3 sun = sky.sun.xyz;
    vec4 value = vec4(0.0, 0.0, 0.0, 1.0);
    if (q.y < HEIGHT) {
        float u = (float(q.x) + 0.5) / float(WIDTH);
        float v = (float(q.y) + 0.5) / float(HEIGHT);
        float azimuth = u * PI;
        float elevation = LOWEST + v * v * (0.5 * PI - LOWEST);
        vec3 view = vec3(cos(elevation) * cos(azimuth), sin(elevation), cos(elevation) * sin(azimuth));
        value.rgb = radiance(view, sun) * sky.sun.w;
    } else if (q.x == 0) {
        value.rgb = transmittance(sun);
    } else if (q.x == 1) {
        vec3 here = transmittance(sun);
        vec3 overhead = transmittance(vec3(0.0, 1.0, 0.0));
        vec3 shown = pow(max(here / max(overhead, vec3(1e-30)), vec3(0.0)), vec3(1.0 / DISPLAY_GAMMA));
        float most = max(shown.r, max(shown.g, shown.b));
        value.rgb = most > 0.0 ? shown / most : vec3(0.0);
    } else if (q.x == 2) {
        value.rgb = radiance(vec3(0.0, 1.0, 0.0), sun) * sky.sun.w;
    }
    imageStore(o_table, q, value);
}
)";

struct TableConstants {
    std::array<float, 4> sun{};
    std::array<int32_t, 4> steps{};
};
constexpr std::size_t kTableConstantBytes = 32;
static_assert(sizeof(TableConstants) == kTableConstantBytes, "the shader's push-constant block");

struct SkyState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    Image table;
    bool valid = false;
    std::array<float, 3> toward{};
    bool failed = false;
    uint64_t updates = 0;
    RtSunSettings settings;
    // sky_colours' last answer (worked out again when the sun moves or the settings change)
    bool colours_valid = false;
    std::array<float, 3> colours_toward{};
    SkyColours colours;
};

SkyState& sky_state()
{
    static SkyState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// A GLSL float literal (never an int: 6360000 squared would overflow).
std::string number(double value)
{
    std::string out = text::format("%.9g", value);
    if (out.find_first_of(".e") == std::string::npos) {
        out += ".0";
    }
    return out;
}

std::string defines()
{
    std::string out;
    const auto add = [&out](const char* name, const std::string& value) {
        out += "#define ";
        out += name;
        out += " ";
        out += value;
        out += "\n";
    };
    add("GROUND", number(rt::kGroundRadius));
    add("TOP", number(rt::kTopRadius));
    add("VIEWER_ALTITUDE", number(rt::kViewerAltitude));
    add("RAYLEIGH", "vec3(" + number(core::at(rt::kRayleigh, 0)) + ", " + number(core::at(rt::kRayleigh, 1)) + ", " +
                        number(core::at(rt::kRayleigh, 2)) + ")");
    add("RAYLEIGH_HEIGHT", number(rt::kRayleighHeight));
    add("MIE_SCATTERING", number(rt::kMieScattering));
    add("MIE_ALBEDO", number(rt::kMieAlbedo));
    add("MIE_HEIGHT", number(rt::kMieHeight));
    add("MIE_G", number(rt::kMieG));
    add("DISPLAY_GAMMA", number(rt::kDisplayGamma));
    add("LOWEST", number(rt::kSkyLutLowest));
    add("WIDTH", std::to_string(rt::kSkyLutWidth));
    add("HEIGHT", std::to_string(rt::kSkyLutHeight));
    return out;
}

bool make_objects(Backend& state, std::string* error)
{
    SkyState& own = sky_state();
    if (own.pipeline != VK_NULL_HANDLE) {
        return true;
    }
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
    const VkPushConstantRange range{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(TableConstants)};
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 0.0F;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS ||
        vkCreateSampler(device, &sampler_info, nullptr, &own.sampler) != VK_SUCCESS) {
        *error = "the descriptor layout or the sampler";
        return false;
    }
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                 .pNext = nullptr,
                                                 .flags = 0,
                                                 .setLayoutCount = 1,
                                                 .pSetLayouts = &own.set_layout,
                                                 .pushConstantRangeCount = 1,
                                                 .pPushConstantRanges = &range};
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
        own.layout = VK_NULL_HANDLE;
        *error = "the pipeline layout";
        return false;
    }
    const std::string source = "#version 460\n" + defines() + kTableBody;
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, source);
    if (!compiled.ok) {
        *error = compiled.log;
        return false;
    }
    const VkShaderModuleCreateInfo module_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                               .pNext = nullptr,
                                               .flags = 0,
                                               .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                               .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
        return false;
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
                                           .layout = own.layout,
                                           .basePipelineHandle = VK_NULL_HANDLE,
                                           .basePipelineIndex = -1};
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &own.pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        own.pipeline = VK_NULL_HANDLE;
        *error = "vkCreateComputePipelines failed";
        return false;
    }
    return true;
}

bool make_table(Backend& state)
{
    SkyState& own = sky_state();
    if (own.table.image != VK_NULL_HANDLE) {
        return true;
    }
    const ImageRequest request{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = {.width = rt::kSkyLutWidth, .height = rt::kSkyLutHeight + 1},
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &own.table)) {
        return false;
    }
    own.table.resting = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}

double elevation_of(const std::array<float, 3>& toward)
{
    return std::asin(std::clamp(static_cast<double>(core::at(toward, 1)), -1.0, 1.0));
}

std::array<float, 3> display_of(const rt::Rgb& linear)
{
    std::array<float, 3> out{};
    for (std::size_t c = 0; c < out.size(); ++c) {
        core::at(out, c) = static_cast<float>(std::pow(std::max(core::at(linear, c), 0.0), 1.0 / rt::kDisplayGamma));
    }
    return out;
}

} // namespace

RtSunSettings& sun_settings_held() { return sky_state().settings; }

bool frame_sun(std::array<float, 3>* toward)
{
    std::array<float, 3> found{};
    bool have = rt::sun_override(&found);
    if (!have) {
        if (!rt::baked_sun_wanted()) {
            rt::set_baked_sun_wanted(true);
        }
        const rt::BakedSun& baked = rt::baked_sun();
        have = baked.scene != 0 && baked.found;
        found = baked.toward;
    }
    const double length = std::hypot(static_cast<double>(core::at(found, 0)), static_cast<double>(core::at(found, 1)),
                                     static_cast<double>(core::at(found, 2)));
    if (!have || !(length > kShortest) || !std::isfinite(length)) {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        core::at(*toward, i) = static_cast<float>(static_cast<double>(core::at(found, i)) / length);
    }
    return true;
}

SkyColours sky_colours(const std::array<float, 3>& toward)
{
    SkyState& own = sky_state();
    if (own.colours_valid && own.colours_toward == toward) {
        return own.colours;
    }
    const RtSunSettings& settings = own.settings;
    SkyColours colours;
    const double elevation = elevation_of(toward);
    colours.sun = settings.colour_set ? settings.colour : rt::sun_colour_at(elevation);
    const rt::Rgb zenith = rt::sky_radiance({0.0, 1.0, 0.0}, rt::sun_direction_at(elevation), kColourSteps);
    colours.zenith = display_of({core::at(zenith, 0) * rt::sky_exposure(), core::at(zenith, 1) * rt::sky_exposure(),
                                 core::at(zenith, 2) * rt::sky_exposure()});
    own.colours = colours;
    own.colours_toward = toward;
    own.colours_valid = true;
    return colours;
}

bool update_sky(Backend& state, VkCommandBuffer commands, const std::array<float, 3>& toward)
{
    SkyState& own = sky_state();
    if (own.failed) {
        return false;
    }
    const float same = (core::at(toward, 0) * core::at(own.toward, 0)) + (core::at(toward, 1) * core::at(own.toward, 1)) +
                       (core::at(toward, 2) * core::at(own.toward, 2));
    if (own.valid && same >= kSameDirection) {
        return true;
    }
    std::string error;
    if (!make_objects(state, &error) || !make_table(state)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: rt_sky: the sky's table couldn't be made (%s) - no sky colours",
                    error.empty() ? "its image" : error.c_str());
        }
        return false;
    }
    const double elevation = elevation_of(toward);
    const rt::Dir model = rt::sun_direction_at(elevation);
    TableConstants constants{};
    constants.sun = {static_cast<float>(core::at(model, 0)), static_cast<float>(core::at(model, 1)),
                     static_cast<float>(core::at(model, 2)), static_cast<float>(rt::sky_exposure())};
    constants.steps = {static_cast<int32_t>(kTableSteps.view), static_cast<int32_t>(kTableSteps.light), 0, 0};
    Memory::transition(commands, &own.table, VK_IMAGE_LAYOUT_GENERAL, true);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.pipeline);
    const VkDescriptorImageInfo out{.sampler = VK_NULL_HANDLE, .imageView = own.table.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &out;
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, 1, &write);
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commands, (rt::kSkyLutWidth + kGroupSide - 1) / kGroupSide,
                  (rt::kSkyLutHeight + 1 + kGroupSide - 1) / kGroupSide, 1);
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
    state.bound = BoundState{};
    own.table.used = state.frames.serial();
    own.toward = toward;
    own.valid = true;
    ++own.updates;
    // what the other effects are told (the bounce light's sun colour)
    const SkyColours colours = sky_colours(toward);
    const bool published = own.settings.sky_model || own.settings.colour_set;
    rt::set_sun_light({.on = published, .toward = toward, .colour = colours.sun, .sky = colours.zenith});
    if (first_report(state, kReportFirst)) {
        get_log("vulkan: rt_sky: the sky's table made (sun %.1f degrees up; its colour %.3f %.3f %.3f%s)",
                elevation * 180.0 / 3.14159265358979, static_cast<double>(core::at(colours.sun, 0)),
                static_cast<double>(core::at(colours.sun, 1)), static_cast<double>(core::at(colours.sun, 2)),
                own.settings.colour_set ? ", set by rt_sun_colour" : "");
    }
    return true;
}

void invalidate_sky()
{
    sky_state().valid = false;
    sky_state().colours_valid = false;
}

void sky_without_sun()
{
    SkyState& own = sky_state();
    if (own.valid) {
        own.valid = false;
        rt::set_sun_light({});
    }
}

VkDescriptorImageInfo sky_table_descriptor()
{
    const SkyState& own = sky_state();
    return {.sampler = own.sampler, .imageView = own.table.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
}

std::string sky_table_glsl(uint32_t binding)
{
    std::string out = "layout(set = 0, binding = " + std::to_string(binding) + ") uniform sampler2D u_sky;\n";
    out += "const float SKY_LOWEST = " + number(rt::kSkyLutLowest) + ";\n";
    out += "const float SKY_ROWS = " + std::to_string(rt::kSkyLutHeight) + ".0;\n";
    out += "const float SKY_GAMMA = " + number(rt::kDisplayGamma) + ";\n";
    out += R"(
vec3 sky_linear(vec3 dir, vec3 to_sun)
{
    vec2 hs = to_sun.xz;
    vec2 hv = dir.xz;
    float ls = length(hs);
    float lv = length(hv);
    float c = (ls > 1e-5 && lv > 1e-5) ? dot(hs, hv) / (ls * lv) : 1.0;
    float azimuth = acos(clamp(c, -1.0, 1.0));
    float elevation = asin(clamp(dir.y, -1.0, 1.0));
    float v = sqrt(clamp((elevation - SKY_LOWEST) / (1.57079632679 - SKY_LOWEST), 0.0, 1.0));
    float row = clamp(v * SKY_ROWS, 0.5, SKY_ROWS - 0.5);
    return max(textureLod(u_sky, vec2(azimuth / 3.14159265358979, row / (SKY_ROWS + 1.0)), 0.0).rgb, vec3(0.0));
}
vec3 sky_display(vec3 dir, vec3 to_sun) { return pow(sky_linear(dir, to_sun), vec3(1.0 / SKY_GAMMA)); }
)";
    return out;
}

bool read_sky_table(Backend& state, std::vector<std::array<float, 4>>* texels)
{
    SkyState& own = sky_state();
    texels->clear();
    if (!own.valid || !finish_work(state)) {
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(rt::kSkyLutWidth) * (rt::kSkyLutHeight + 1);
    constexpr std::size_t kChannels = 4;
    std::vector<uint8_t> bytes(count * kChannels * sizeof(uint16_t));
    if (!state.memory.read_image_level(&own.table, 0, &bytes)) {
        return false;
    }
    texels->resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t c = 0; c < kChannels; ++c) {
            uint16_t half = 0;
            std::memcpy(&half, &core::at(bytes, ((i * kChannels) + c) * sizeof(uint16_t)), sizeof(half));
            core::at(core::at(*texels, i), c) = half_to_float(half);
        }
    }
    return true;
}

uint64_t sky_table_updates() { return sky_state().updates; }

void release_sky(Backend& state)
{
    SkyState& own = sky_state();
    const VkDevice device = state.context.device();
    if (own.table.image != VK_NULL_HANDLE) {
        state.memory.destroy_image(&own.table);
    }
    own.table = Image{};
    if (device != nullptr) {
        if (own.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, own.pipeline, nullptr);
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
    own.pipeline = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.valid = false;
    own.failed = false;
    rt::set_sun_light({});
}

float half_to_float(uint16_t half)
{
    constexpr uint32_t kExponentShift = 10;
    constexpr uint32_t kExponentMask = 0x1F;
    constexpr uint32_t kMantissaMask = 0x3FF;
    constexpr int kBias = 15;
    const uint32_t sign = (static_cast<uint32_t>(half) >> 15U) & 1U;
    const uint32_t exponent = (static_cast<uint32_t>(half) >> kExponentShift) & kExponentMask;
    const uint32_t mantissa = static_cast<uint32_t>(half) & kMantissaMask;
    float value = 0.0F;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), 1 - kBias - static_cast<int>(kExponentShift));
    } else if (exponent == kExponentMask) {
        value = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    } else {
        value = std::ldexp(static_cast<float>(mantissa | (1U << kExponentShift)),
                           static_cast<int>(exponent) - kBias - static_cast<int>(kExponentShift));
    }
    return sign != 0 ? -value : value;
}

} // namespace render::vulkan
