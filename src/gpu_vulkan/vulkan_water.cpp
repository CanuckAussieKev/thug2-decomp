// NOT ORIGINAL - water shading on the Vulkan Gpu (vulkan_water.h; docs/design/vulkan.md "Water").
#include "gpu_vulkan/vulkan_water.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/water_surfaces.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportFirst = 0x57415401U;
constexpr uint64_t kReportFailed = 0x57415402U;
constexpr uint64_t kReportTarget = 0x57415403U;
constexpr uint64_t kReportNoDepth = 0x57415404U;
constexpr uint64_t kReportLevel = 0x57415405U; // (+ the level's checksum: once per level)

constexpr uint32_t kGroupSide = 8;
constexpr uint32_t kBlendSourceAlpha = 5;   // D3DBLEND_SRCALPHA
constexpr uint32_t kBlendInverseAlpha = 6;  // D3DBLEND_INVSRCALPHA
constexpr float kDefaultSky = 0.6F;         // the sky's grey where nothing else is known
constexpr float kSkyBlue = 0.75F;           // (its blue)
constexpr double kDegrees = std::numbers::pi / 180.0;

// --- the shaders -------------------------------------------------------------------------------------------------

// Each wave's numbers (and the processor's twin's): PCG's hash of n as 0 - 1.
constexpr const char* kHashGlsl = R"(
float water_hash(uint n)
{
    uint h = n * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return float(h) * (1.0 / 4294967296.0);
}
)";

// The ripple texture, one level a dispatch: 64 waves of whole cycles across the tile (2 - 24, mostly along +x - the wind -
// within 63 degrees of it), amplitude |k|^-1.7, each faded out between half and all of the level's Nyquist limit (a box
// filter's band, roughly: far water flattens); the slope scaled to an RMS of 0.22 at the full level, the height to 0 - 1
// (3 RMS), and 16 more waves (6 - 20 cycles, any way) for the foam's pattern.
constexpr const char* kWavesBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D o_waves;
layout(push_constant) uniform Level { ivec4 size; } level;

void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    int size = level.size.x;
    if (p.x >= size || p.y >= size) {
        return;
    }
    vec2 x = (vec2(p) + 0.5) / float(size);
    float nyquist = float(size) * 0.5;
    vec2 slope = vec2(0.0);
    float height = 0.0;
    float slope_power = 0.0;
    float height_power = 0.0;
    for (uint i = 0u; i < 64u; ++i) {
        float r = water_hash(i * 3u + 1u);
        float a = water_hash(i * 3u + 2u);
        float phase0 = water_hash(i * 3u + 3u) * 6.2831853;
        float cycles = 2.0 + 22.0 * r * r;
        float angle = (a - 0.5) * 2.2;
        vec2 k = floor(vec2(cos(angle), sin(angle)) * cycles + 0.5);
        if (k.x == 0.0 && k.y == 0.0) {
            k = vec2(1.0, 0.0);
        }
        float kl = length(k);
        float amp = pow(kl, -1.7);
        slope_power += 0.5 * (amp * 6.2831853 * kl) * (amp * 6.2831853 * kl);
        height_power += 0.5 * amp * amp;
        float keep = 1.0 - smoothstep(0.5 * nyquist, nyquist, max(abs(k.x), abs(k.y)));
        float phase = 6.2831853 * dot(k, x) + phase0;
        height += keep * amp * sin(phase);
        slope += keep * amp * 6.2831853 * k * cos(phase);
    }
    float foam = 0.0;
    float foam_power = 0.0;
    for (uint i = 0u; i < 16u; ++i) {
        float r = water_hash(i * 3u + 1001u);
        float a = water_hash(i * 3u + 1002u) * 6.2831853;
        float phase0 = water_hash(i * 3u + 1003u) * 6.2831853;
        vec2 k = floor(vec2(cos(a), sin(a)) * (6.0 + 14.0 * r) + 0.5);
        if (k.x == 0.0 && k.y == 0.0) {
            k = vec2(0.0, 1.0);
        }
        foam_power += 0.5;
        float keep = 1.0 - smoothstep(0.5 * nyquist, nyquist, max(abs(k.x), abs(k.y)));
        foam += keep * sin(6.2831853 * dot(k, x) + phase0);
    }
    vec2 s = slope * (0.22 / sqrt(slope_power));
    float h = clamp(0.5 + 0.5 * height / (3.0 * sqrt(height_power)), 0.0, 1.0);
    float f = clamp(0.5 + 0.5 * foam / (2.0 * sqrt(foam_power)), 0.0, 1.0);
    imageStore(o_waves, p, vec4(s, h, f));
}
)";

// The depth made linear (as the art style's: the projection's w; 1e9 where nothing was drawn).
constexpr const char* kLinearBody = R"(
layout(local_size_x = 8, local_size_y = 8) in;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
layout(set = 0, binding = 0, r32f) uniform writeonly image2D o_linear;
layout(push_constant) uniform Linear {
    ivec4 rect;
    vec4 projection; // P22, P32, P23, P33
    vec4 depth_map;  // x: the depth range's minimum; y: 1 / its span
} pc;

const float kSky = 1.0e9;

float linear_depth(float d)
{
    if (!(d < 1.0)) {
        return kSky;
    }
    float ndc = (d - pc.depth_map.x) * pc.depth_map.y;
    float z = (pc.projection.y - ndc * pc.projection.w) / (ndc * pc.projection.z - pc.projection.x);
    float w = z * pc.projection.z + pc.projection.w;
    float along = abs(pc.projection.z) > 0.5 ? w : abs(z);
    return (along > 0.0 && along < kSky) ? along : kSky;
}

void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pc.rect.zw))) {
        return;
    }
    ivec2 p = pc.rect.xy + local;
    imageStore(o_linear, p, vec4(linear_depth(texelFetch(u_depth, p, 0).r), 0.0, 0.0, 0.0));
}
)";

struct LinearConstants {
    std::array<int32_t, 4> rect{};
    std::array<float, 4> projection{};
    std::array<float, 4> depth_map{};
};

struct WaterState {
    WaterSettings settings;
    bool failed = false;
    // the passes' objects
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE; // (push: binding 0 storage image, 1 sampled depth)
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline waves_pipeline = VK_NULL_HANDLE;
    std::array<VkPipeline, 2> linear{}; // single-sampled depth, multisampled
    VkSampler clamp_linear = VK_NULL_HANDLE;
    VkSampler clamp_nearest = VK_NULL_HANDLE;
    VkSampler repeat_mipped = VK_NULL_HANDLE;
    Image waves;
    std::array<VkImageView, kWaterWavesLevels> wave_levels{};
    bool waves_made = false;
    Image scene;  // the colour behind the view's water
    Image linear_depth; // its linear depth
    // the view being drawn (the listener's)
    bool view_known = false;
    rt::ViewDrawn view;
    uint64_t view_serial = 0;
    uint64_t copied_serial = 0; // the view whose copy is made (0: none)
    bool copy_has_depth = false;
    uint64_t block_serial = 0;  // the view whose block is in the ring
    uint64_t block_frame = 0;
    VkDescriptorBufferInfo block{};
    Image* sky_image = nullptr; // bound for the last water draw
    bool opaque_done = false;   // the view's opaque world finished (rt::opaque_view_drawn)
    bool replaying = false;     // a kept opaque water draw is being drawn again
    struct Deferred {
        GpuStateBlock block;
        DrawCall call;
    };
    std::vector<Deferred> deferred; // the view's opaque water draws, kept for opaque_drawn
    WaterStats stats;
};

WaterState& water_state()
{
    static WaterState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The view being drawn: water is shaded only in its rectangle, with its camera.
class WaterListener final : public rt::ViewListener {
public:
    void view_begins(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        WaterState& own = water_state();
        own.view = view;
        own.view_known = true;
        own.opaque_done = false;
        own.deferred.clear();
        ++own.view_serial;
    }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        (void)view;
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        WaterState& own = water_state();
        own.opaque_done = true;
        if (!state.device_made || !state.draws_made || own.deferred.empty()) {
            own.deferred.clear();
            return;
        }
        // the opaque water drawn now, as water (the copy made at the first of them: the whole opaque world without
        // the water, the effects' light in it)
        own.replaying = true;
        for (const WaterState::Deferred& kept : own.deferred) {
            (void)record_draw(state, kept.block, kept.call); // (the game's own states: the depth as it drew it)
            ++own.stats.replayed;
        }
        own.replaying = false;
        own.deferred.clear();
    }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        (void)view;
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        water_state().view_known = false;
    }
    [[nodiscard]] int order() const override { return kWaterOrder; }

private:
    static constexpr int kWaterOrder = 450; // (draws nothing itself: only notes the view)
};

WaterListener& listener()
{
    static WaterListener instance;
    return instance;
}

// --- the objects ---------------------------------------------------------------------------------------------------

VkShaderModule make_module(VkDevice device, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, source);
    if (!compiled.ok) {
        *error = compiled.log;
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                        .pNext = nullptr,
                                        .flags = 0,
                                        .codeSize = compiled.spirv.size() * sizeof(uint32_t),
                                        .pCode = compiled.spirv.data()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        *error = "vkCreateShaderModule failed";
        return VK_NULL_HANDLE;
    }
    return module;
}

VkPipeline make_compute(VkDevice device, VkPipelineLayout layout, const std::string& source, std::string* error)
{
    const VkShaderModule module = make_module(device, source, error);
    if (module == VK_NULL_HANDLE) {
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

VkSampler make_sampler(VkDevice device, VkFilter filter, VkSamplerAddressMode address, float max_lod)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address;
    info.addressModeV = address;
    info.addressModeW = address;
    info.maxLod = max_lod;
    VkSampler sampler = VK_NULL_HANDLE;
    return vkCreateSampler(device, &info, nullptr, &sampler) == VK_SUCCESS ? sampler : VK_NULL_HANDLE;
}

std::string source_of(const char* body, bool multisampled)
{
    std::string source = "#version 460\n";
    if (multisampled) {
        source += "#define MULTISAMPLED\n";
    }
    source += kHashGlsl;
    source += body;
    return source;
}

// The layouts, samplers and pipelines (once); false (logged) when one can't be made.
bool ensure_objects(Backend& state, std::string* error)
{
    WaterState& own = water_state();
    if (own.linear.back() != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    if (own.layout == VK_NULL_HANDLE) {
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
            {.binding = 0,
             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
             .descriptorCount = 1,
             .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
            {.binding = 1,
             .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .descriptorCount = 1,
             .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
             .pImmutableSamplers = nullptr},
        }};
        const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                       .pNext = nullptr,
                                                       .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                       .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                       .pBindings = bindings.data()};
        if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &own.set_layout) != VK_SUCCESS) {
            own.set_layout = VK_NULL_HANDLE;
            *error = "the descriptor layout";
            return false;
        }
        const VkPushConstantRange constants{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(LinearConstants)};
        const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                     .pNext = nullptr,
                                                     .flags = 0,
                                                     .setLayoutCount = 1,
                                                     .pSetLayouts = &own.set_layout,
                                                     .pushConstantRangeCount = 1,
                                                     .pPushConstantRanges = &constants};
        if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
            own.layout = VK_NULL_HANDLE;
            *error = "the pipeline layout";
            return false;
        }
    }
    if (own.clamp_linear == VK_NULL_HANDLE) {
        own.clamp_linear = make_sampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0F);
        own.clamp_nearest = make_sampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0F);
        own.repeat_mipped = make_sampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                         static_cast<float>(kWaterWavesLevels));
        if (own.clamp_linear == VK_NULL_HANDLE || own.clamp_nearest == VK_NULL_HANDLE || own.repeat_mipped == VK_NULL_HANDLE) {
            *error = "the samplers";
            return false;
        }
    }
    if (own.waves_pipeline == VK_NULL_HANDLE) {
        own.waves_pipeline = make_compute(device, own.layout, source_of(kWavesBody, false), error);
        if (own.waves_pipeline == VK_NULL_HANDLE) {
            return false;
        }
    }
    for (std::size_t variant = 0; variant < own.linear.size(); ++variant) {
        if (core::at(own.linear, variant) == VK_NULL_HANDLE) {
            core::at(own.linear, variant) = make_compute(device, own.layout, source_of(kLinearBody, variant == 1), error);
            if (core::at(own.linear, variant) == VK_NULL_HANDLE) {
                return false;
            }
        }
    }
    return true;
}

// An image of `format`, `extent` and `levels` for `usage` (one sample), made again when they changed; a storage image
// rests in GENERAL.
bool ensure_image(Backend& state, Image* image, VkFormat format, Extent extent, VkImageUsageFlags usage, uint32_t levels = 1)
{
    if (image->image != VK_NULL_HANDLE && image->format == format && image->extent.width == extent.width &&
        image->extent.height == extent.height && image->levels == levels) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = levels,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = usage,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    if ((usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0) {
        image->resting = VK_IMAGE_LAYOUT_GENERAL;
    }
    prepare_image(state, image);
    return true;
}

Image* image_of_surface(Surface* surface)
{
    if (surface == nullptr) {
        return nullptr;
    }
    switch (surface->kind) {
    case SurfaceKind::kTextureLevel:
        return surface->level == 0 ? &surface->texture->image : nullptr;
    case SurfaceKind::kRenderTarget:
    case SurfaceKind::kDepth:
        return &surface->image;
    case SurfaceKind::kSystemMemory:
        break;
    }
    return nullptr;
}

// A compute pass's writes made visible to the passes after it (compute and fragment).
void compute_done(VkCommandBuffer commands)
{
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &written, 0,
                         nullptr, 0, nullptr);
}

void push_images(VkCommandBuffer commands, VkImageView storage, const Image* depth)
{
    const WaterState& own = water_state();
    const VkDescriptorImageInfo out{.sampler = VK_NULL_HANDLE, .imageView = storage, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo in{};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
    }
    VkWriteDescriptorSet& storage_write = writes.front();
    storage_write.dstBinding = 0;
    storage_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storage_write.pImageInfo = &out;
    uint32_t count = 1;
    if (depth != nullptr) {
        in = {.sampler = own.clamp_nearest, .imageView = depth->view, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet& depth_write = writes.back();
        depth_write.dstBinding = 1;
        depth_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        depth_write.pImageInfo = &in;
        count = 2;
    }
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, count, writes.data());
}

// The ripple texture made (once): each level by the compute shader into its own view.
bool make_waves(Backend& state, std::string* error)
{
    WaterState& own = water_state();
    if (own.waves_made) {
        return true;
    }
    constexpr VkImageUsageFlags kUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!ensure_image(state, &own.waves, VK_FORMAT_R16G16B16A16_SFLOAT, {.width = kWaterWavesSize, .height = kWaterWavesSize},
                      kUsage, kWaterWavesLevels)) {
        *error = "the ripple texture";
        return false;
    }
    const VkDevice device = state.context.device();
    for (uint32_t level = 0; level < kWaterWavesLevels; ++level) {
        const VkImageViewCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = own.waves.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .components = {},
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = level, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};
        if (vkCreateImageView(device, &info, nullptr, &core::at(own.wave_levels, level)) != VK_SUCCESS) {
            core::at(own.wave_levels, level) = VK_NULL_HANDLE;
            *error = "the ripple texture's views";
            return false;
        }
    }
    VkCommandBuffer commands = outside_pass(state);
    Memory::transition(commands, &own.waves, VK_IMAGE_LAYOUT_GENERAL, true);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.waves_pipeline);
    for (uint32_t level = 0; level < kWaterWavesLevels; ++level) {
        const uint32_t size = kWaterWavesSize >> level;
        LinearConstants constants{};
        constants.rect = {static_cast<int32_t>(size), 0, 0, 0};
        vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        push_images(commands, core::at(own.wave_levels, level), nullptr);
        vkCmdDispatch(commands, (size + kGroupSide - 1) / kGroupSide, (size + kGroupSide - 1) / kGroupSide, 1);
    }
    compute_done(commands);
    own.waves.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Memory::transition(commands, &own.waves, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    own.waves.used = state.frames.serial();
    own.waves_made = true;
    return true;
}

// The rectangle `rect` inside an image of `extent` (empty when it falls outside).
std::array<uint32_t, 4> clip_rect(const std::array<uint32_t, 4>& rect, const Extent& extent)
{
    const uint32_t left = std::min(core::at(rect, 0), extent.width);
    const uint32_t top = std::min(core::at(rect, 1), extent.height);
    return {left, top, std::min(core::at(rect, 2), extent.width - left), std::min(core::at(rect, 3), extent.height - top)};
}

// The depth moved to be sampled by the compute pass, and back to its resting layout after it.
void depth_barrier(VkCommandBuffer commands, Image* depth, VkImageLayout layout, bool to_pass)
{
    const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_pass ? VkAccessFlags{VK_ACCESS_MEMORY_WRITE_BIT} : VkAccessFlags{0},
        .dstAccessMask = to_pass ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
                                 : VkAccessFlags{VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        .oldLayout = depth->layout,
        .newLayout = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depth->image,
        .subresourceRange =
            {.aspectMask = depth->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
    };
    vkCmdPipelineBarrier(commands, to_pass ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         to_pass ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : tests, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

// The view's colour and linear depth copied (the first water draw of the view): false when the colour can't be read.
bool copy_behind(Backend& state, Image* colour, Image* depth)
{
    WaterState& own = water_state();
    const std::array<uint32_t, 4> rect = clip_rect(own.view.rect, colour->extent);
    if (core::at(rect, 2) == 0 || core::at(rect, 3) == 0 ||
        !ensure_image(state, &own.scene, colour->format, colour->extent, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }
    const bool depth_ok = depth != nullptr && (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 &&
                          (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 && depth->view != VK_NULL_HANDLE &&
                          depth->extent.width == colour->extent.width && depth->extent.height == colour->extent.height &&
                          ensure_image(state, &own.linear_depth, VK_FORMAT_R32_SFLOAT, depth->extent,
                                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    if (!depth_ok && first_report(state, kReportNoDepth)) {
        get_log("vulkan: rt_water: no samplable depth with the view - the water drawn as deep water there");
    }
    VkCommandBuffer commands = outside_pass(state);
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &own.scene, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{.x = static_cast<int32_t>(core::at(rect, 0)), .y = static_cast<int32_t>(core::at(rect, 1)), .z = 0};
    const VkExtent3D size{.width = core::at(rect, 2), .height = core::at(rect, 3), .depth = 1};
    if (colour->samples == VK_SAMPLE_COUNT_1_BIT) {
        const VkImageCopy region{.srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.scene.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
        const VkImageResolve region{.srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdResolveImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, own.scene.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    Memory::transition(commands, &own.scene, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    own.copy_has_depth = false;
    if (depth_ok) {
        // (the last view's reads of the linear depth done before it is written again)
        Memory::transition(commands, &own.linear_depth, VK_IMAGE_LAYOUT_GENERAL);
        const VkMemoryBarrier reads_done{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                         .pNext = nullptr,
                                         .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                         .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                             &reads_done, 0, nullptr, 0, nullptr);
        constexpr std::size_t kRowZ = 2; // (the projection's rows and columns: z, w)
        constexpr std::size_t kRowW = 3;
        const auto& projection = own.view.camera.projection;
        const float span = core::at(own.view.depth_range, 1) - core::at(own.view.depth_range, 0);
        LinearConstants constants{};
        constants.rect = {static_cast<int32_t>(core::at(rect, 0)), static_cast<int32_t>(core::at(rect, 1)),
                          static_cast<int32_t>(core::at(rect, 2)), static_cast<int32_t>(core::at(rect, 3))};
        constants.projection = {core::at(core::at(projection, kRowZ), kRowZ), core::at(core::at(projection, kRowW), kRowZ),
                                core::at(core::at(projection, kRowZ), kRowW), core::at(core::at(projection, kRowW), kRowW)};
        constants.depth_map = {core::at(own.view.depth_range, 0), span > 0.0F ? 1.0F / span : 1.0F, 0.0F, 0.0F};
        depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, core::at(own.linear, depth->samples != VK_SAMPLE_COUNT_1_BIT ? 1 : 0));
        vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        push_images(commands, own.linear_depth.view, depth);
        vkCmdDispatch(commands, (core::at(rect, 2) + kGroupSide - 1) / kGroupSide, (core::at(rect, 3) + kGroupSide - 1) / kGroupSide, 1);
        depth_barrier(commands, depth, depth->resting, false);
        compute_done(commands);
        own.copy_has_depth = true;
    }
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    own.scene.used = serial;
    if (depth_ok) {
        depth->used = serial;
        own.linear_depth.used = serial;
    }
    ++own.stats.copies;
    return true;
}

// The level's sky capture made ready to be sampled (null: none).
Image* sky_image(Backend& state)
{
    const rt::SkyCapture capture = rt::sky_capture();
    if (capture.texture == nullptr || capture.serial == 0) {
        return nullptr;
    }
    Texture* const texture = object(capture.texture);
    if (!state.textures.contains(texture)) {
        return nullptr;
    }
    Image* image = &texture->image;
    if (image->image == VK_NULL_HANDLE || image->view == VK_NULL_HANDLE) {
        return nullptr;
    }
    if (image->layout != image->resting) {
        VkCommandBuffer commands = outside_pass(state);
        Memory::transition(commands, image, image->resting);
    }
    return image->resting == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? image : nullptr;
}

// A row-vector 4 x 4 matrix (D3D's layout) inverted (Gauss-Jordan in double precision; a singular one: the identity).
std::array<std::array<float, 4>, 4> inverted(const std::array<std::array<float, 4>, 4>& matrix)
{
    constexpr std::size_t kWide = 8; // (the matrix and the identity side by side)
    constexpr double kSingular = 1.0e-12;
    std::array<std::array<double, kWide>, 4> work{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(core::at(work, row), column) = static_cast<double>(core::at(core::at(matrix, row), column));
        }
        core::at(core::at(work, row), 4 + row) = 1.0;
    }
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < 4; ++row) {
            if (std::abs(core::at(core::at(work, row), column)) > std::abs(core::at(core::at(work, pivot), column))) {
                pivot = row;
            }
        }
        if (std::abs(core::at(core::at(work, pivot), column)) < kSingular) {
            return {{{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}}};
        }
        std::swap(core::at(work, pivot), core::at(work, column));
        const double scale = 1.0 / core::at(core::at(work, column), column);
        for (double& value : core::at(work, column)) {
            value *= scale;
        }
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = core::at(core::at(work, row), column);
            for (std::size_t k = 0; k < kWide; ++k) {
                core::at(core::at(work, row), k) -= factor * core::at(core::at(work, column), k);
            }
        }
    }
    std::array<std::array<float, 4>, 4> out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(core::at(out, row), column) = static_cast<float>(core::at(core::at(work, row), 4 + column));
        }
    }
    return out;
}

Float4x4 flat(const std::array<std::array<float, 4>, 4>& matrix)
{
    Float4x4 out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(out, (row * 4) + column) = core::at(core::at(matrix, row), column);
        }
    }
    return out;
}

float finite_or(float value, float fallback, float low, float high)
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

// The PCG hash the shader's water_hash is (the reference's): `number`'s 32 bits mixed, as 0 - 1.
constexpr uint32_t kPcgMultiplier = 747796405U;
constexpr uint32_t kPcgIncrement = 2891336453U;
constexpr uint32_t kPcgShiftBits = 28U;
constexpr uint32_t kPcgMixMultiplier = 277803737U;
constexpr uint32_t kPcgFinalShift = 22U;
constexpr float kTwoTo32 = 4294967296.0F;
float hash01(uint32_t number)
{
    uint32_t hash = (number * kPcgMultiplier) + kPcgIncrement;
    hash = ((hash >> ((hash >> kPcgShiftBits) + 4U)) ^ hash) * kPcgMixMultiplier;
    hash = (hash >> kPcgFinalShift) ^ hash;
    return static_cast<float>(hash) * (1.0F / kTwoTo32);
}

float smoothstep(float low, float high, float value)
{
    constexpr float kThree = 3.0F; // (Hermite's 3 t^2 - 2 t^3)
    constexpr float kTwo = 2.0F;
    const float along = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
    return along * along * (kThree - (kTwo * along));
}

} // namespace

// --- settings ----------------------------------------------------------------------------------------------------

void set_water_settings(const WaterSettings& settings)
{
    WaterSettings kept = settings;
    constexpr float kMostWaves = 3.0F;
    constexpr float kMostWind = 2.0F;
    constexpr float kMostClarity = 4.0F;
    constexpr float kMostFoam = 2.0F;
    constexpr float kFullTurn = 360.0F;
    kept.waves = finite_or(kept.waves, 1.0F, 0.0F, kMostWaves);
    kept.wind = finite_or(kept.wind, 1.0F, 0.0F, kMostWind);
    kept.clarity = finite_or(kept.clarity, 1.0F, 0.0F, kMostClarity);
    kept.foam = finite_or(kept.foam, 1.0F, 0.0F, kMostFoam);
    kept.heading = std::isfinite(kept.heading) && kept.heading >= 0.0F ? std::fmod(kept.heading, kFullTurn) : kWaterHeadingAuto;
    for (float& channel : kept.tint) {
        channel = finite_or(channel, 0.0F, 0.0F, 1.0F);
    }
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        water_state().settings = kept;
    }
    water::set_wanted(kept.on);
    // (the level's sky captured for the reflections' misses - the sky's own setting may want it too)
    rt::set_sky_capture_wanted(kept.on || rt_sky_settings().on);
    if (kept.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

WaterSettings water_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return water_state().settings;
}

WaterUniforms water_uniforms(const WaterSettings& settings, const WaterView& view)
{
    const water::LevelWater level = water::level_water();
    const rt::ViewDrawn& drawn = view.view;
    WaterUniforms out{};
    out.world_to_eye = flat(drawn.camera.view);
    out.eye_to_world = flat(inverted(drawn.camera.view));
    out.projection = flat(drawn.camera.projection);
    out.viewport = {static_cast<float>(core::at(drawn.rect, 0)), static_cast<float>(core::at(drawn.rect, 1)),
                    static_cast<float>(core::at(drawn.rect, 2)), static_cast<float>(core::at(drawn.rect, 3))};
    out.target = {view.target.width > 0 ? 1.0F / static_cast<float>(view.target.width) : 0.0F,
                  view.target.height > 0 ? 1.0F / static_cast<float>(view.target.height) : 0.0F, view.copied ? 1.0F : 0.0F,
                  settings.hdr ? 1.0F : 0.0F};
    const float heading = settings.heading >= 0.0F ? settings.heading : level.heading;
    const double angle = static_cast<double>(heading) * kDegrees;
    out.time = {static_cast<float>(drawn.game_seconds), static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)),
                settings.wind};
    // (rt_water_waves above 1 grows as its square: Choppy 1.5 x 2.25, Rough 2 x 4 the level's swell)
    const float waves = settings.waves <= 1.0F ? settings.waves : settings.waves * settings.waves;
    out.waves = {std::max(level.waves, 0.0F) * waves, std::max(level.visibility * settings.clarity, 1.0F),
                 settings.foam, kWaterDetail};
    out.tint = {core::at(settings.tint, 0), core::at(settings.tint, 1), core::at(settings.tint, 2), settings.tint_auto ? 1.0F : 0.0F};
    // the sun: the scene's (the shadows', the drawn sun's), else the one the game shows
    std::array<float, 3> toward{};
    bool sun = false;
    const rt::SceneSun scene = rt::scene_sun();
    if (scene.found()) {
        toward = scene.toward;
        sun = true;
    } else {
        sun = rt::game_sun(&toward);
    }
    out.sun = {core::at(toward, 0), core::at(toward, 1), core::at(toward, 2), sun ? 1.0F : 0.0F};
    const rt::SunLight light = rt::sun_light();
    const std::array<float, 3> colour = light.on ? light.colour : std::array<float, 3>{1.0F, 1.0F, 1.0F};
    out.sun_colour = {core::at(colour, 0), core::at(colour, 1), core::at(colour, 2), settings.hdr ? kWaterGlitterHdr : kWaterGlitterSdr};
    const std::array<float, 3> sky = drawn.fog.on ? drawn.fog.colour : std::array<float, 3>{kDefaultSky, kDefaultSky, kSkyBlue};
    out.sky = {core::at(sky, 0), core::at(sky, 1), core::at(sky, 2), view.sky_bound ? 1.0F : 0.0F};
    out.camera = {core::at(drawn.camera.position, 0), core::at(drawn.camera.position, 1), core::at(drawn.camera.position, 2),
                  kWaterRefraction};
    out.misc = {settings.screen_reflections ? 1.0F : 0.0F, std::max(level.swell_length, 1.0F), settings.debug_view ? 1.0F : 0.0F,
                0.0F};
    return out;
}

std::vector<std::array<float, 4>> water_waves_reference(uint32_t level)
{
    // (the numbers kWavesBody uses, in its order)
    constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
    constexpr float kHalf = 0.5F;
    constexpr float kLeastCycles = 2.0F;
    constexpr float kMoreCycles = 22.0F;
    constexpr float kSpread = 2.2F;        // (radians: the waves' directions within +-63 degrees of the wind)
    constexpr float kFalloff = -1.7F;      // (the amplitude's power of |k|)
    constexpr float kSlopeRms = 0.22F;
    constexpr float kHeightRmses = 3.0F;
    constexpr float kFoamRmses = 2.0F;
    constexpr float kFoamLeastCycles = 6.0F;
    constexpr float kFoamMoreCycles = 14.0F;
    constexpr uint32_t kFoamWaves = 16;
    constexpr uint32_t kFoamSeed = 1000;
    constexpr uint32_t kNumbersEach = 3; // (each wave's three hashes)
    const uint32_t size = kWaterWavesSize >> std::min(level, kWaterWavesLevels - 1);
    const float nyquist = static_cast<float>(size) * kHalf;
    struct Wave {
        float kx = 0.0F;
        float kz = 0.0F;
        float amp = 0.0F;
        float phase = 0.0F;
        float keep = 0.0F;
    };
    const auto round_half_up = [](float value) { return std::floor(value + kHalf); };
    const auto keep_of = [nyquist](const Wave& wave) {
        return 1.0F - smoothstep(kHalf * nyquist, nyquist, std::max(std::abs(wave.kx), std::abs(wave.kz)));
    };
    std::vector<Wave> waves;
    std::vector<Wave> foams;
    float slope_power = 0.0F;
    float height_power = 0.0F;
    for (uint32_t i = 0; i < kWaterWaveCount; ++i) {
        const float length_hash = hash01((i * kNumbersEach) + 1U);
        const float angle_hash = hash01((i * kNumbersEach) + 2U);
        Wave wave;
        wave.phase = hash01((i * kNumbersEach) + kNumbersEach) * kTwoPi;
        const float cycles = kLeastCycles + (kMoreCycles * length_hash * length_hash);
        const float angle = (angle_hash - kHalf) * kSpread;
        wave.kx = round_half_up(std::cos(angle) * cycles);
        wave.kz = round_half_up(std::sin(angle) * cycles);
        if (wave.kx == 0.0F && wave.kz == 0.0F) {
            wave.kx = 1.0F;
        }
        const float length = std::hypot(wave.kx, wave.kz);
        wave.amp = std::pow(length, kFalloff);
        slope_power += kHalf * (wave.amp * kTwoPi * length) * (wave.amp * kTwoPi * length);
        height_power += kHalf * wave.amp * wave.amp;
        wave.keep = keep_of(wave);
        waves.push_back(wave);
    }
    for (uint32_t i = 0; i < kFoamWaves; ++i) {
        const float length_hash = hash01((i * kNumbersEach) + kFoamSeed + 1U);
        const float angle = hash01((i * kNumbersEach) + kFoamSeed + 2U) * kTwoPi;
        Wave wave;
        wave.phase = hash01((i * kNumbersEach) + kFoamSeed + kNumbersEach) * kTwoPi;
        const float cycles = kFoamLeastCycles + (kFoamMoreCycles * length_hash);
        wave.kx = round_half_up(std::cos(angle) * cycles);
        wave.kz = round_half_up(std::sin(angle) * cycles);
        if (wave.kx == 0.0F && wave.kz == 0.0F) {
            wave.kz = 1.0F;
        }
        wave.keep = keep_of(wave);
        foams.push_back(wave);
    }
    const float slope_scale = kSlopeRms / std::sqrt(slope_power);
    const float height_scale = 1.0F / (kHeightRmses * std::sqrt(height_power));
    const float foam_scale = 1.0F / (kFoamRmses * std::sqrt(kHalf * static_cast<float>(kFoamWaves)));
    std::vector<std::array<float, 4>> out(static_cast<std::size_t>(size) * size);
    for (uint32_t row = 0; row < size; ++row) {
        for (uint32_t column = 0; column < size; ++column) {
            const float across = (static_cast<float>(column) + kHalf) / static_cast<float>(size);
            const float down = (static_cast<float>(row) + kHalf) / static_cast<float>(size);
            float slope_x = 0.0F;
            float slope_z = 0.0F;
            float height = 0.0F;
            for (const Wave& wave : waves) {
                const float phase = (kTwoPi * ((wave.kx * across) + (wave.kz * down))) + wave.phase;
                height += wave.keep * wave.amp * std::sin(phase);
                const float rise = wave.keep * wave.amp * kTwoPi * std::cos(phase);
                slope_x += rise * wave.kx;
                slope_z += rise * wave.kz;
            }
            float foam = 0.0F;
            for (const Wave& wave : foams) {
                foam += wave.keep * std::sin((kTwoPi * ((wave.kx * across) + (wave.kz * down))) + wave.phase);
            }
            core::at(out, (static_cast<std::size_t>(row) * size) + column) = {
                slope_x * slope_scale, slope_z * slope_scale, std::clamp(kHalf + (kHalf * height * height_scale), 0.0F, 1.0F),
                std::clamp(kHalf + (kHalf * foam * foam_scale), 0.0F, 1.0F)};
        }
    }
    return out;
}

// --- the draws -----------------------------------------------------------------------------------------------------

bool water_candidate(const Backend& state, const GpuStateBlock& block)
{
    const WaterState& own = water_state();
    if (!own.settings.on || own.failed || !own.view_known || !state.draws_made) {
        return false;
    }
    const GpuTexture* const texture = core::at(block.textures, 0);
    if (texture == nullptr || (texture != water::current_texture() && !own.replaying)) {
        return false;
    }
    if (fvf_pre_transformed(block.vertex_format)) {
        return false;
    }
    // opaque, or blended by its alpha over what is behind (what the shader composes itself)
    if (block.render_state(GpuRenderState::kAlphaBlendEnable) != 0U &&
        (block.render_state(GpuRenderState::kSrcBlend) != kBlendSourceAlpha ||
         block.render_state(GpuRenderState::kDestBlend) != kBlendInverseAlpha)) {
        return false;
    }
    // drawn into the view's own rectangle
    const GpuViewport& viewport = block.viewport;
    return viewport.x == core::at(own.view.rect, 0) && viewport.y == core::at(own.view.rect, 1) &&
           viewport.width == core::at(own.view.rect, 2) && viewport.height == core::at(own.view.rect, 3);
}

bool defer_water_draw(Backend& state, const GpuStateBlock& block, const DrawCall& call)
{
    (void)state;
    WaterState& own = water_state();
    constexpr std::size_t kMostKept = 256; // (a frame's opaque water draws: a few)
    if (own.replaying || own.opaque_done || block.render_state(GpuRenderState::kAlphaBlendEnable) != 0U ||
        own.deferred.size() >= kMostKept) {
        return false;
    }
    own.deferred.push_back({.block = block, .call = call});
    return true;
}

bool water_key(Backend& state, const GpuStateBlock& block, const DrawCall& call, ShaderKey* shaders, WaterDescriptors* water)
{
    *water = {};
    if (!water_candidate(state, block)) {
        return true;
    }
    if (defer_water_draw(state, block, call)) {
        return false;
    }
    ShaderKey with_water = *shaders;
    with_water.fragment.water = 1;
    with_water = normalised(with_water);
    if (with_water.fragment.water != 0U && prepare_water_draw(state, block, water)) {
        *shaders = with_water;
    }
    return true;
}

bool prepare_water_draw(Backend& state, const GpuStateBlock& block, WaterDescriptors* out)
{
    (void)block;
    WaterState& own = water_state();
    *out = {};
    std::string error;
    if (!ensure_objects(state, &error) || !make_waves(state, &error)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: rt_water: the water's passes couldn't be made (%s) - water drawn as before", error.c_str());
        }
        return false;
    }
    Image* const colour = image_of_surface(core::at(state.targets, 0));
    if (colour == nullptr || (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 ||
        (colour->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        ++own.stats.refused;
        if (first_report(state, kReportTarget)) {
            get_log("vulkan: rt_water: water drawn into a target that can't be read - drawn as before there");
        }
        return false;
    }
    if (own.copied_serial != own.view_serial) {
        if (!copy_behind(state, colour, image_of_surface(state.depth))) {
            ++own.stats.refused;
            return false;
        }
        own.copied_serial = own.view_serial;
    }
    Image* const sky = sky_image(state);
    const uint64_t frame = state.frames.serial();
    if (own.block_serial != own.view_serial || own.block_frame != frame || (sky != nullptr) != (own.sky_image != nullptr)) {
        const WaterUniforms uniforms =
            water_uniforms(own.settings, {.view = own.view, .target = colour->extent, .copied = own.copy_has_depth, .sky_bound = sky != nullptr});
        const void* const data = &uniforms;
        const std::span<const uint8_t> raw(static_cast<const uint8_t*>(data), sizeof(uniforms));
        const std::optional<RingSpan> span = state.ring.copy(raw, state.uniform_alignment);
        if (!span) {
            return false;
        }
        own.block = {.buffer = span->buffer, .offset = span->offset, .range = sizeof(uniforms)};
        own.block_serial = own.view_serial;
        own.block_frame = frame;
    }
    own.sky_image = sky;
    const Image& depth_shown = own.copy_has_depth ? own.linear_depth : state.blank;
    const Image& sky_shown = sky != nullptr ? *sky : state.blank;
    out->on = true;
    out->block = own.block;
    out->scene = {.sampler = own.clamp_linear, .imageView = own.scene.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    out->depth = {.sampler = own.clamp_nearest,
                  .imageView = depth_shown.view,
                  .imageLayout = own.copy_has_depth ? VK_IMAGE_LAYOUT_GENERAL : depth_shown.layout};
    out->sky = {.sampler = own.clamp_linear, .imageView = sky_shown.view, .imageLayout = sky_shown.layout};
    out->waves = {.sampler = own.repeat_mipped, .imageView = own.waves.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (first_report(state, kReportFirst)) {
        get_log("vulkan: rt_water: the first water draw (material %08X; %ux%u view, %s, %s, the sky %s)", water::current_material(),
                core::at(own.view.rect, 2), core::at(own.view.rect, 3), colour->samples == 1 ? "one sample" : "multisampled",
                own.copy_has_depth ? "depth copied" : "no depth", sky != nullptr ? "captured" : "not captured");
    }
    const water::LevelWater level = water::level_water();
    constexpr uint32_t kLevelShift = 32U; // (the level's checksum above the report's key)
    if (first_report(state, kReportLevel + (uint64_t{level.level} << kLevelShift))) {
        get_log("vulkan: rt_water: level %08X - waves %.2f x %.2f (swell %.0f units), visibility %.0f x %.2f units, wind %.2f "
                "toward %.0f degrees, foam %.2f, tint %s",
                level.level, static_cast<double>(level.waves), static_cast<double>(own.settings.waves),
                static_cast<double>(level.swell_length), static_cast<double>(level.visibility),
                static_cast<double>(own.settings.clarity), static_cast<double>(own.settings.wind),
                static_cast<double>(own.settings.heading >= 0.0F ? own.settings.heading : level.heading),
                static_cast<double>(own.settings.foam), own.settings.tint_auto ? "auto" : "set");
    }
    // (the images the draw reads used by this submission - the draw is recorded into it next)
    for (Image* image : {&own.scene, &own.waves}) {
        image->used = frame;
    }
    if (own.copy_has_depth) {
        own.linear_depth.used = frame;
    }
    if (sky != nullptr) {
        sky->used = frame;
    }
    ++own.stats.draws;
    return true;
}

WaterStats water_stats()
{
    const std::scoped_lock lock(backend().mutex);
    return water_state().stats;
}

bool read_water_depth(Backend& state, std::vector<float>* depth, Extent* size)
{
    WaterState& own = water_state();
    if (own.linear_depth.image == VK_NULL_HANDLE) {
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<std::size_t>(own.linear_depth.extent.width) * own.linear_depth.extent.height * sizeof(float));
    if (!state.memory.read_image_level(&own.linear_depth, 0, &bytes)) {
        return false;
    }
    depth->resize(bytes.size() / sizeof(float));
    std::memcpy(depth->data(), bytes.data(), depth->size() * sizeof(float));
    *size = own.linear_depth.extent;
    return true;
}

bool read_water_waves(Backend& state, uint32_t level, std::vector<uint16_t>* halves, Extent* size)
{
    WaterState& own = water_state();
    if (!own.waves_made || level >= kWaterWavesLevels) {
        return false;
    }
    const uint32_t side = kWaterWavesSize >> level;
    constexpr std::size_t kTexelBytes = 4 * sizeof(uint16_t);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(side) * side * kTexelBytes);
    if (!state.memory.read_image_level(&own.waves, level, &bytes)) {
        return false;
    }
    halves->resize(bytes.size() / sizeof(uint16_t));
    std::memcpy(halves->data(), bytes.data(), halves->size() * sizeof(uint16_t));
    *size = {.width = side, .height = side};
    return true;
}

void release_water(Backend& state)
{
    WaterState& own = water_state();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (VkPipeline pipeline : {own.waves_pipeline, core::at(own.linear, 0), core::at(own.linear, 1)}) {
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        for (VkImageView view : own.wave_levels) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
            }
        }
        if (own.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, own.layout, nullptr);
        }
        if (own.set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, own.set_layout, nullptr);
        }
        for (VkSampler sampler : {own.clamp_linear, own.clamp_nearest, own.repeat_mipped}) {
            if (sampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, sampler, nullptr);
            }
        }
    }
    if (state.memory.ready()) {
        for (Image* image : {&own.waves, &own.scene, &own.linear_depth}) {
            state.memory.destroy_image(image);
        }
    }
    const WaterSettings settings = own.settings;
    const WaterStats stats = own.stats;
    own = WaterState{};
    own.settings = settings;
    own.stats = stats;
}

} // namespace render::vulkan
