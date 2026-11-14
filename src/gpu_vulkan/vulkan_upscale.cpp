// NOT ORIGINAL - upscaling (vulkan_upscale.h; docs/design/vulkan.md "Upscaling"): the settings, the view listener, the
// jitter, the preparation pass and the provider's frame.
#include "gpu_vulkan/vulkan_upscale.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_framegen.h"
#include "gpu_vulkan/vulkan_pipelines.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint32_t kGroupSide = 8;
constexpr VkFormat kHdrWorkFormat = VK_FORMAT_R16G16B16A16_SFLOAT; // (the colour in and out)
constexpr float kNoMotionHalf = kNoMotion * 0.5F;
constexpr uint32_t kVendorNvidia = 0x10DE;
constexpr uint32_t kLeastPhases = 8;
constexpr float kPhasesPerRatioSquared = 8.0F;
constexpr float kQualityRatio = 1.0F / 1.5F;
constexpr float kBalancedRatio = 1.0F / 1.7F;
constexpr float kPerformanceRatio = 0.5F;
constexpr float kUltraRatio = 1.0F / 3.0F;
constexpr double kMillisecondsPerSecond = 1000.0;
constexpr float kLongestFrameMilliseconds = 100.0F;
constexpr uint64_t kLogEvery = 600; // (passes between the log's lines)

constexpr float kTwo = 2.0F;   // (clip space: 2 across; a half angle doubled)
constexpr float kHalfF = 0.5F;
constexpr std::size_t kPrepareBindings = 6;
constexpr float kLeastFrameMilliseconds = 0.1F;
constexpr uint64_t kReportMultisampled = 0x75C0'0001ULL;
constexpr uint64_t kReportTargets = 0x75C0'0002ULL;
constexpr uint64_t kReportFailed = 0x75C0'0003ULL;
constexpr uint64_t kReportFirst = 0x75C0'0004ULL;

// The preparation pass (vulkan_upscale.h step 4): per pixel of the drawn rectangle.
constexpr const char* kPrepareSource = R"(#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D depth_map;
layout(set = 0, binding = 1, rgba16f) uniform readonly image2D motion_in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D motion_out;
layout(set = 0, binding = 3, r32f) uniform writeonly image2D depth_out;
layout(set = 0, binding = 4, r32f) uniform writeonly image2D reactive_out;
layout(set = 0, binding = 5, rgba16f) uniform writeonly image2D normal_out;
layout(push_constant) uniform Pass {
    mat4 to_before; // this frame's clip space (jittered) -> the frame before's (Direct3D's row vectors)
    vec4 size;      // the drawn rectangle's width, height; the jitter (pixels)
    vec4 origin;    // its corner in the target; z: 1 - the normals wanted (Ray Reconstruction's guide)
    vec4 lens;      // the projection's [0][0], [1][1], [2][2], [3][2]
} pass;

// A pixel's place in the camera's space (Direct3D's: z forward) from its depth.
vec3 view_point(ivec2 pixel)
{
    ivec2 at = clamp(pixel, ivec2(0), ivec2(pass.size.xy) - 1);
    float depth = texelFetch(depth_map, at + ivec2(pass.origin.xy), 0).r;
    float z = pass.lens.w / max(depth - pass.lens.z, 1e-7);
    vec2 centre = vec2(at);
    vec2 ndc = vec2(centre.x / pass.size.x * 2.0 - 1.0, 1.0 - centre.y / pass.size.y * 2.0);
    return vec3(ndc.x * z / pass.lens.x, ndc.y * z / pass.lens.y, z);
}
void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(pass.size.x) || pixel.y >= int(pass.size.y)) {
        return;
    }
    ivec2 at = pixel + ivec2(pass.origin.xy);
    float depth = texelFetch(depth_map, at, 0).r;
    vec4 drawn = imageLoad(motion_in, at);
    vec2 motion = drawn.xy;
    if (drawn.x >= KNO_MOTION_HALF) {
        // no opaque draw wrote it: the camera's motion of the point at its depth
        vec2 centre = vec2(pixel); // (Direct3D 9's pixel centres: the draws are moved half a pixel to them)
        vec2 ndc = vec2(centre.x / pass.size.x * 2.0 - 1.0, 1.0 - centre.y / pass.size.y * 2.0);
        vec4 before = pass.to_before * vec4(ndc, depth, 1.0);
        vec2 now_uv = (centre - pass.size.zw) / pass.size.xy;
        vec2 before_uv = before.w > 1e-6 ? vec2(before.x / before.w * 0.5 + 0.5, 0.5 - before.y / before.w * 0.5) : now_uv;
        motion = before_uv - now_uv;
    }
    if (pass.origin.z > 0.5) {
        // the shading normal from the nearer neighbours' depths (facing the camera), roughness 1
        vec3 here = view_point(pixel);
        vec3 left = view_point(pixel - ivec2(1, 0));
        vec3 right = view_point(pixel + ivec2(1, 0));
        vec3 up = view_point(pixel - ivec2(0, 1));
        vec3 down = view_point(pixel + ivec2(0, 1));
        vec3 across = abs(right.z - here.z) < abs(here.z - left.z) ? right - here : here - left;
        vec3 along = abs(down.z - here.z) < abs(here.z - up.z) ? down - here : here - up;
        vec3 normal = normalize(cross(across, along));
        if (depth >= 1.0 || any(isnan(normal))) {
            normal = vec3(0.0, 0.0, -1.0);
        }
        if (dot(normal, here) > 0.0) {
            normal = -normal;
        }
        imageStore(normal_out, pixel, vec4(normal, 1.0));
    }
    imageStore(motion_out, pixel, vec4(motion, 0.0, 0.0));
    imageStore(depth_out, pixel, vec4(depth));
    imageStore(reactive_out, pixel, vec4(clamp(drawn.z, 0.0, 0.9)));
}
)";

struct PrepareConstants {
    Float4x4 to_before{};
    Float4 size{};
    Float4 origin{};
    Float4 lens{};
};

struct UpscaleState {
    UpscaleSettings settings;
    std::unique_ptr<UpscaleProvider> provider;
    bool provider_tried = false;
    Extent provider_output{};
    // the preparation pass
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline prepare = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    bool failed = false;
    // the frame's images (the output's size)
    Image colour;
    Image depth;
    Image motion;
    Image reactive;
    Image output;
    Image albedo; // (Ray Reconstruction's guides)
    Image specular;
    Image normals;
    bool reconstruct = false; // DLSS runs with Ray Reconstruction
    bool terms_set = false; // (the check's alternatives)
    UpscaleJitterTerms jitter_terms;
    // the view being drawn
    bool view_on = false;
    Extent last_render{};
    Extent last_output{};
    uint32_t jitter_index = 0;
    bool reset = true;
    std::chrono::steady_clock::time_point last_time;
    uint64_t passes = 0;
    double evaluate_seconds = 0.0; // (processor time recording the provider's work, the log's)
};

UpscaleState& upscale()
{
    static UpscaleState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

Image* image_of(Surface* surface)
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

// An image of the output's size (made again when the size changed), left in VK_IMAGE_LAYOUT_GENERAL.
bool ensure_image(Backend& state, Image* image, VkFormat format, Extent extent, VkImageUsageFlags usage)
{
    if (image->image != VK_NULL_HANDLE && image->format == format && image->extent.width == extent.width &&
        image->extent.height == extent.height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = usage,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    image->resting = VK_IMAGE_LAYOUT_GENERAL;
    prepare_image(state, image);
    return true;
}

VkShaderModule make_module(VkDevice device, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, source);
    if (!compiled.ok) {
        *error = "the preparation shader doesn't compile: " + compiled.log;
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

bool ensure_prepare(Backend& state, std::string* error)
{
    UpscaleState& own = upscale();
    if (own.prepare != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    constexpr auto kBindings = static_cast<uint32_t>(kPrepareBindings);
    std::array<VkDescriptorSetLayoutBinding, kBindings> bindings{};
    for (uint32_t binding = 0; binding < kBindings; ++binding) {
        core::at(bindings, binding) = {.binding = binding,
                                       .descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                                      : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                       .descriptorCount = 1,
                                       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                       .pImmutableSamplers = nullptr};
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = kBindings,
                                                   .pBindings = bindings.data()};
    const VkPushConstantRange constants{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(PrepareConstants)};
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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
                                                 .pPushConstantRanges = &constants};
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &own.layout) != VK_SUCCESS) {
        own.layout = VK_NULL_HANDLE;
        *error = "the pipeline layout";
        return false;
    }
    std::string source = kPrepareSource;
    const std::string marker = "KNO_MOTION_HALF";
    source.replace(source.find(marker), marker.size(), std::to_string(kNoMotionHalf));
    const VkShaderModule module = make_module(device, source, error);
    if (module == VK_NULL_HANDLE) {
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
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &own.prepare);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        own.prepare = VK_NULL_HANDLE;
        *error = "vkCreateComputePipelines failed";
        return false;
    }
    return true;
}

// The provider for the settings, made the first time a view asks (the device's vendor decides `auto`); kept while the
// output's size stays.
UpscaleProvider* ensure_provider(Backend& state, Extent output)
{
    UpscaleState& own = upscale();
    if (own.provider && own.provider_output.width == output.width && own.provider_output.height == output.height) {
        return own.provider.get();
    }
    if (own.provider) {
        // a new size: the provider made again (its history goes)
        finish_work(state); // (the open pass ended, the work so far done: the provider's images free)
        own.provider->release(state);
        own.provider.reset();
    } else if (own.provider_tried) {
        return nullptr;
    }
    own.provider_tried = true;
    own.provider_output = output;
    const UpscaleSettings& settings = own.settings;
    const bool nvidia = state.adapter.vendor_id == kVendorNvidia;
    const bool try_dlss = settings.upscaler == Upscaler::kDlss || (settings.upscaler == Upscaler::kAuto && nvidia);
    std::string why;
    if (try_dlss) {
        own.provider = make_dlss_provider(state, output, settings.ray_reconstruction, &why);
        if (!own.provider) {
            get_log("vulkan: upscaler: DLSS can't run here (%s) - FSR instead", why.c_str());
        }
    }
    if (!own.provider) {
        why.clear();
        own.provider = make_fsr_provider(state, output, &why);
        if (!own.provider) {
            get_log("vulkan: upscaler: FSR can't run here (%s) - no upscaling", why.c_str());
            return nullptr;
        }
    }
    own.reconstruct =
        settings.ray_reconstruction && std::string(own.provider->name()) == "DLSS" && ray_reconstruction_ready();
    get_log("vulkan: upscaler: %s - %s, %.0f%% of %ux%u%s", own.provider->name(), own.provider->describe().c_str(),
            static_cast<double>(upscale_ratio(settings.quality)) * 100.0, output.width, output.height,
            own.reconstruct ? ", the ray-traced effects' denoisers left to Ray Reconstruction" : "");
    own.reset = true;
    return own.provider.get();
}

// The jitter of the frame to come handed to render_pc (rt::set_view_scale).
void publish_scale(bool on)
{
    const UpscaleState& own = upscale();
    if (!on) {
        rt::set_view_scale({});
        return;
    }
    const float ratio = upscale_ratio(own.settings.quality);
    const uint32_t phases = upscale_jitter_phases(1.0F / ratio);
    rt::set_view_scale({.on = true, .scale = ratio, .jitter = upscale_jitter(own.jitter_index % phases, phases)});
}

// The world's near and far planes and vertical field of view from its projection (D3D's: [2][2] = f / (f - n),
// [3][2] = -n f / (f - n), [1][1] = cot(fov / 2)).
void lens_of(const std::array<std::array<float, 4>, 4>& projection, UpscaleFrame* frame)
{
    const float depth_scale = core::at(core::at(projection, 2), 2);
    const float depth_offset = core::at(core::at(projection, 3), 2);
    const float cot_half_fov = core::at(core::at(projection, 1), 1);
    if (depth_scale != 0.0F && depth_scale != 1.0F) {
        frame->near_z = -depth_offset / depth_scale;
        frame->far_z = depth_scale * frame->near_z / (depth_scale - 1.0F);
    }
    if (cot_half_fov > 0.0F) {
        frame->vertical_fov = kTwo * std::atan(1.0F / cot_half_fov);
    }
}

Float4x4 as_float16(const std::array<std::array<float, 4>, 4>& rows)
{
    Float4x4 matrix{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(matrix, (row * 4) + column) = core::at(core::at(rows, row), column);
        }
    }
    return matrix;
}

// The depth made readable by the compute pass (from its tests' writes), and back after.
void depth_barrier(VkCommandBuffer commands, Image* depth, VkImageLayout layout, bool to_compute)
{
    const VkPipelineStageFlags tests =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = to_compute ? VkAccessFlags{VK_ACCESS_MEMORY_WRITE_BIT} : VkAccessFlags{0},
        .dstAccessMask = to_compute ? VkAccessFlags{VK_ACCESS_SHADER_READ_BIT}
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
    vkCmdPipelineBarrier(commands, to_compute ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         to_compute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : tests, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    depth->layout = layout;
}

// Everything written before visible to everything after (the compute pass, the provider, the copies).
void full_barrier(VkCommandBuffer commands)
{
    const VkMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
}

void blit(VkCommandBuffer commands, const Image& from, const std::array<int32_t, 4>& from_rect, const Image& to,
          const std::array<int32_t, 4>& to_rect)
{
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const auto corners = [](const std::array<int32_t, 4>& rect) {
        return std::array<VkOffset3D, 2>{
            VkOffset3D{.x = core::at(rect, 0), .y = core::at(rect, 1), .z = 0},
            VkOffset3D{.x = core::at(rect, 0) + core::at(rect, 2), .y = core::at(rect, 1) + core::at(rect, 3), .z = 1}};
    };
    VkImageBlit region{};
    region.srcSubresource = layers;
    region.dstSubresource = layers;
    const std::array<VkOffset3D, 2> source = corners(from_rect);
    const std::array<VkOffset3D, 2> target = corners(to_rect);
    std::ranges::copy(source, std::begin(region.srcOffsets));
    std::ranges::copy(target, std::begin(region.dstOffsets));
    vkCmdBlitImage(commands, from.image, from.layout, to.image, to.layout, 1, &region, VK_FILTER_NEAREST);
}

// The upscaler's view listener.
class UpscaleListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kUpscale; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override {}
    void view_begins(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            begin_upscaled_view(state, view);
        }
    }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_upscale(state, view);
        }
    }
};

UpscaleListener& listener()
{
    static UpscaleListener instance;
    return instance;
}

// Ray Reconstruction's albedo guide: the opaque world's colour before any ray-traced effect changed it (first of the
// listeners at opaque_drawn).
class AlbedoListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kShadows / 2; }
    void opaque_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        UpscaleState& own = upscale();
        if (!state.device_made || !state.draws_made || !own.view_on || !own.reconstruct) {
            return;
        }
        Image* const colour = image_of(core::at(state.targets, 0));
        const Extent whole = own.provider_output;
        constexpr VkImageUsageFlags kWorkUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (colour == nullptr || (colour->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0 ||
            !ensure_image(state, &own.albedo, kHdrWorkFormat, whole, kWorkUsage)) {
            return;
        }
        const auto width = static_cast<int32_t>(std::min(core::at(view.rect, 2), whole.width));
        const auto height = static_cast<int32_t>(std::min(core::at(view.rect, 3), whole.height));
        VkCommandBuffer commands = outside_pass(state);
        Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Memory::transition(commands, &own.albedo, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        blit(commands, *colour,
             {static_cast<int32_t>(core::at(view.rect, 0)), static_cast<int32_t>(core::at(view.rect, 1)), width, height},
             own.albedo, {0, 0, width, height});
        Memory::transition(commands, &own.albedo, VK_IMAGE_LAYOUT_GENERAL);
        Memory::transition(commands, colour, colour->resting == VK_IMAGE_LAYOUT_UNDEFINED
                                                 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                 : colour->resting);
        state.bound = BoundState{};
        colour->used = state.frames.serial();
        own.albedo.used = colour->used;
    }
};

AlbedoListener& albedo_listener()
{
    static AlbedoListener instance;
    return instance;
}

} // namespace

namespace {

// What the upscalers add to the instance and the device: Streamline's (DLSS) where it is there, and what FSR's shaders
// use where the device has it - compute-shader derivatives (VK_KHR_compute_shader_derivatives), 16-bit floats in
// shaders and storage (its half-precision passes).
class UpscaleAdditions final : public ContextAdditions {
public:
    void instance_extensions(std::vector<const char*>* names) override
    {
        // (Streamline also for frame generation's DLSS-G and Reflex - vulkan_framegen.h)
        dlss_ = dlss_context_additions() != nullptr && (wants_dlss() || frame_generation_wants_dlss() ||
                                                        frame_generation_settings_locked().reflex != Reflex::kOff);
        if (dlss_) {
            dlss_context_additions()->instance_extensions(names);
        }
    }
    void device_extensions(VkPhysicalDevice physical, DeviceWants* wants) override
    {
        if (dlss_) {
            dlss_context_additions()->device_extensions(physical, wants);
        }
        // (frame generation's FSR: its present queues)
        frame_generation_device_wants(physical, wants);
        if (upscale().settings.upscaler == Upscaler::kOff) {
            return; // (Reflex alone: none of the upscalers' shader features)
        }
        derivatives_ = {};
        derivatives_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR;
        storage16_ = {};
        storage16_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
        VkPhysicalDeviceVulkan12Features offered12{};
        offered12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        storage16_.pNext = &offered12;
        derivatives_.pNext = &storage16_;
        VkPhysicalDeviceFeatures2 asked{};
        asked.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        asked.pNext = &derivatives_;
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> offered(count);
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, offered.data());
        const bool has_derivatives = std::ranges::any_of(offered, [](const VkExtensionProperties& extension) {
            return std::string_view(std::data(extension.extensionName)) == VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME;
        });
        vkGetPhysicalDeviceFeatures2(physical, &asked);
        derivatives_.pNext = nullptr;
        storage16_.pNext = nullptr;
        wants->features12.shaderFloat16 = offered12.shaderFloat16;
        wants->shader_int16 = true;
        if (has_derivatives) {
            wants->extensions.push_back(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
            wants->chain.push_back(structure_of(&derivatives_));
        }
        wants->chain.push_back(structure_of(&storage16_));
    }
    void device_made(const DeviceMade& made) override
    {
        if (dlss_) {
            dlss_context_additions()->device_made(made);
        }
        frame_generation_device_made(made);
    }
    bool create_surface(VkInstance instance, SDL_Window* window, VkSurfaceKHR* surface) override
    {
        return dlss_ && dlss_context_additions()->create_surface(instance, window, surface);
    }
    void before_destroy() override
    {
        if (dlss_) {
            dlss_context_additions()->before_destroy();
        }
    }

private:
    // A Vulkan features structure as the chain's common head (its sType and pNext first, as every one's).
    template <typename Features> static VkBaseOutStructure* structure_of(Features* features)
    {
        return std::bit_cast<VkBaseOutStructure*>(std::bit_cast<std::uintptr_t>(features));
    }
    static bool wants_dlss()
    {
        const Upscaler upscaler = upscale().settings.upscaler;
        return upscaler == Upscaler::kDlss || upscaler == Upscaler::kAuto;
    }
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives_{};
    VkPhysicalDevice16BitStorageFeatures storage16_{};
    bool dlss_ = false; // (Streamline's asked for, from the instance on)
};

} // namespace

// --- settings ------------------------------------------------------------------------------------------------------

ContextAdditions* upscale_context_additions()
{
    static UpscaleAdditions additions;
    // (frame generation or Reflex alone asks for them too - vulkan_framegen.h)
    return upscale().settings.upscaler == Upscaler::kOff && !frame_generation_wants_context() ? nullptr : &additions;
}

float upscale_ratio(UpscaleQuality quality)
{
    switch (quality) {
    case UpscaleQuality::kNative:
        return 1.0F;
    case UpscaleQuality::kQuality:
        return kQualityRatio;
    case UpscaleQuality::kBalanced:
        return kBalancedRatio;
    case UpscaleQuality::kPerformance:
        return kPerformanceRatio;
    case UpscaleQuality::kUltra:
        return kUltraRatio;
    }
    return 1.0F;
}

bool parse_upscaler(const std::string& text, Upscaler* upscaler)
{
    if (text == "off" || text == "0" || text.empty()) {
        *upscaler = Upscaler::kOff;
    } else if (text == "dlss") {
        *upscaler = Upscaler::kDlss;
    } else if (text == "fsr") {
        *upscaler = Upscaler::kFsr;
    } else if (text == "auto" || text == "1") {
        *upscaler = Upscaler::kAuto;
    } else {
        return false;
    }
    return true;
}

bool parse_upscale_quality(const std::string& text, UpscaleQuality* quality)
{
    if (text == "dlaa" || text == "native") {
        *quality = UpscaleQuality::kNative;
    } else if (text == "quality") {
        *quality = UpscaleQuality::kQuality;
    } else if (text == "balanced") {
        *quality = UpscaleQuality::kBalanced;
    } else if (text == "performance") {
        *quality = UpscaleQuality::kPerformance;
    } else if (text == "ultra" || text == "ultra_performance") {
        *quality = UpscaleQuality::kUltra;
    } else {
        return false;
    }
    return true;
}

void set_upscale_settings(const UpscaleSettings& settings)
{
    {
        const std::scoped_lock lock(backend().mutex);
        UpscaleState& own = upscale();
        own.settings = settings;
        own.settings.sharpness = std::clamp(settings.sharpness, 0.0F, 1.0F);
        // (a fresh start: the provider made again at the next view - its history thrown away -, the jitter from its
        // first point)
        own.provider_output = {};
        own.provider_tried = false;
        own.reset = true;
        own.jitter_index = 0;
        own.failed = false;
    }
    rt::set_view_scale({});
    if (settings.upscaler != Upscaler::kOff) {
        rt::add_view_listener(&listener());
        rt::add_view_listener(&albedo_listener());
    } else {
        rt::remove_view_listener(&listener());
        rt::remove_view_listener(&albedo_listener());
    }
}

const UpscaleSettings& upscale_settings_locked() { return upscale().settings; }

UpscaleSettings upscale_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return upscale().settings;
}

// --- the jitter ----------------------------------------------------------------------------------------------------

std::array<float, 2> upscale_jitter(uint32_t index, uint32_t phases)
{
    // Halton (2, 3) from 1 (0 would be the corner), each centred on the pixel
    const auto halton = [](uint32_t number, uint32_t base) {
        float fraction = 1.0F;
        float result = 0.0F;
        while (number > 0) {
            fraction /= static_cast<float>(base);
            result += fraction * static_cast<float>(number % base);
            number /= base;
        }
        return result;
    };
    const uint32_t step = (index % std::max(phases, 1U)) + 1;
    return {halton(step, 2) - kHalfF, halton(step, 3) - kHalfF};
}

uint32_t upscale_jitter_phases(float output_over_drawn)
{
    const float ratio = std::max(output_over_drawn, 1.0F);
    return std::max(kLeastPhases, static_cast<uint32_t>(std::ceil(kPhasesPerRatioSquared * ratio * ratio)));
}

// --- the view ------------------------------------------------------------------------------------------------------

void begin_upscaled_view(Backend& state, const rt::ViewDrawn& view)
{
    UpscaleState& own = upscale();
    MotionVectors& motion = state.motion;
    motion.active = false;
    own.view_on = false;
    if (own.settings.upscaler == Upscaler::kOff || own.failed) {
        return;
    }
    const Image* const colour = image_of(core::at(state.targets, 0));
    if (colour == nullptr || colour->attachment_view == VK_NULL_HANDLE) {
        return;
    }
    if (colour->samples != VK_SAMPLE_COUNT_1_BIT) {
        if (first_report(state, kReportMultisampled)) {
            get_log("vulkan: upscaler: the world's target is multisampled ([Graphics] msaa / AntiAliasing) - no "
                    "upscaling while it is (set msaa=0)");
        }
        publish_scale(false);
        return;
    }
    const Extent output{.width = core::at(view.output_rect, 2), .height = core::at(view.output_rect, 3)};
    if (ensure_provider(state, output) == nullptr) {
        publish_scale(false);
        return;
    }
    // the motion vectors (vulkan_motion.h)
    if (!begin_motion(state, view, *colour, output)) {
        return;
    }
    own.view_on = true;
    if (!rt::view_scale().on) {
        publish_scale(true); // (from the next frame: this one's viewport is already set)
    }
}

bool record_upscale(Backend& state, const rt::ViewDrawn& view)
{
    UpscaleState& own = upscale();
    MotionVectors& motion = state.motion;
    const bool was_on = own.view_on;
    own.view_on = false;
    motion.active = false;
    if (!was_on || own.provider == nullptr) {
        return false;
    }
    Image* const colour = image_of(core::at(state.targets, 0));
    Image* const depth = image_of(state.depth);
    constexpr VkImageUsageFlags kCopies = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (colour == nullptr || depth == nullptr || core::at(state.targets, 0) != motion.target ||
        (colour->usage & kCopies) != kCopies || (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 ||
        depth->view == VK_NULL_HANDLE || depth->extent.width < colour->extent.width ||
        depth->extent.height < colour->extent.height) {
        if (first_report(state, kReportTargets)) {
            get_log("vulkan: upscaler: the view's targets can't be read or written (no samplable depth of the "
                    "colour's size, or no copies) - not upscaled there");
        }
        return false;
    }
    const std::array<uint32_t, 4>& drawn = view.rect;
    const std::array<uint32_t, 4>& shown = view.output_rect;
    const Extent render{.width = std::min(core::at(drawn, 2), colour->extent.width - std::min(core::at(drawn, 0), colour->extent.width)),
                        .height = std::min(core::at(drawn, 3), colour->extent.height - std::min(core::at(drawn, 1), colour->extent.height))};
    const Extent output{.width = std::min(core::at(shown, 2), colour->extent.width - std::min(core::at(shown, 0), colour->extent.width)),
                        .height = std::min(core::at(shown, 3), colour->extent.height - std::min(core::at(shown, 1), colour->extent.height))};
    if (render.width == 0 || render.height == 0 || output.width == 0 || output.height == 0 ||
        render.width > output.width || render.height > output.height) {
        return false;
    }
    // A frame drawn before the scale was taken up (the provider's first: the viewport was set already) is shown as
    // drawn - a provider is only given its quality's size.
    {
        const std::array<uint32_t, 4> expected =
            rt::scaled_rect({0, 0, output.width, output.height}, upscale_ratio(own.settings.quality));
        if (render.width != core::at(expected, 2) || render.height != core::at(expected, 3)) {
            publish_scale(true);
            return false;
        }
    }
    std::string error;
    constexpr VkImageUsageFlags kWorkUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const Extent whole = own.provider_output;
    const bool made =
        ensure_prepare(state, &error) && ensure_image(state, &own.colour, kHdrWorkFormat, whole, kWorkUsage) &&
        ensure_image(state, &own.depth, VK_FORMAT_R32_SFLOAT, whole, kWorkUsage) &&
        ensure_image(state, &own.motion, VK_FORMAT_R16G16B16A16_SFLOAT, whole, kWorkUsage) &&
        ensure_image(state, &own.reactive, VK_FORMAT_R32_SFLOAT, whole, kWorkUsage) &&
        ensure_image(state, &own.output, kHdrWorkFormat, whole, kWorkUsage) &&
        (!own.reconstruct || (ensure_image(state, &own.albedo, kHdrWorkFormat, whole, kWorkUsage) &&
                              ensure_image(state, &own.specular, kHdrWorkFormat, whole, kWorkUsage) &&
                              ensure_image(state, &own.normals, kHdrWorkFormat, whole, kWorkUsage)));
    if (!made) {
        own.failed = true;
        publish_scale(false);
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: upscaler: its passes couldn't be made (%s) - upscaling off",
                    error.empty() ? "its images" : error.c_str());
        }
        return false;
    }
    VkCommandBuffer commands = outside_pass(state);
    // 1. the drawn colour copied out (as 16-bit floats)
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &own.colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const auto rect_of = [](const std::array<uint32_t, 4>& rect, const Extent& size) {
        return std::array<int32_t, 4>{static_cast<int32_t>(core::at(rect, 0)), static_cast<int32_t>(core::at(rect, 1)),
                                      static_cast<int32_t>(size.width), static_cast<int32_t>(size.height)};
    };
    blit(commands, *colour, rect_of(drawn, render), own.colour,
         {0, 0, static_cast<int32_t>(render.width), static_cast<int32_t>(render.height)});
    // 2. the preparation pass
    for (Image* image : {&own.colour, &own.depth, &own.motion, &own.reactive, &own.output, &motion.image}) {
        Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL);
    }
    if (own.reconstruct) {
        for (Image* image : {&own.albedo, &own.specular, &own.normals}) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL);
        }
        // (no specular albedo: zeros)
        const VkClearColorValue zero{};
        const VkImageSubresourceRange all{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
        vkCmdClearColorImage(commands, own.specular.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
    }
    const VkImageLayout depth_resting = depth->layout;
    depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);
    PrepareConstants constants{};
    {
        // this frame's clip space (jittered) -> the world -> the frame before's (unjittered)
        const Float4x4 now = motion_multiply(as_float16(view.camera.view), as_float16(view.camera.projection));
        Float4x4 to_world{};
        if (!motion_inverse(now, &to_world)) {
            to_world = now;
        }
        const Float4x4 before = motion_multiply(motion.before.view, motion.before.projection);
        constants.to_before = motion_multiply(to_world, before);
        constants.size = {static_cast<float>(render.width), static_cast<float>(render.height), core::at(view.jitter, 0),
                          core::at(view.jitter, 1)};
        constants.origin = {static_cast<float>(core::at(drawn, 0)), static_cast<float>(core::at(drawn, 1)),
                            own.reconstruct ? 1.0F : 0.0F, 0.0F};
        const std::array<std::array<float, 4>, 4>& lens = view.camera.projection;
        constants.lens = {core::at(core::at(lens, 0), 0), core::at(core::at(lens, 1), 1), core::at(core::at(lens, 2), 2),
                          core::at(core::at(lens, 3), 2)};
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.prepare);
    const VkDescriptorImageInfo depth_info{
        .sampler = own.sampler, .imageView = depth->view, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const std::array<VkDescriptorImageInfo, 5> storage{{
        {.sampler = VK_NULL_HANDLE, .imageView = motion.image.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.motion.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.depth.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.reactive.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        // (the normals' image when Ray Reconstruction wants it; else bound but not written: the motion's)
        {.sampler = VK_NULL_HANDLE,
         .imageView = own.reconstruct ? own.normals.view : own.motion.view,
         .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
    }};
    std::array<VkWriteDescriptorSet, kPrepareBindings> writes{};
    for (uint32_t binding = 0; binding < writes.size(); ++binding) {
        VkWriteDescriptorSet& write = core::at(writes, binding);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = binding == 0 ? &depth_info : &core::at(storage, binding - 1);
    }
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commands, (render.width + kGroupSide - 1) / kGroupSide, (render.height + kGroupSide - 1) / kGroupSide,
                  1);
    full_barrier(commands);
    depth_barrier(commands, depth,
                  depth_resting == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                             : depth_resting,
                  false);
    // 3. the provider
    UpscaleFrame frame;
    frame.commands = commands;
    frame.colour = &own.colour;
    frame.depth = &own.depth;
    frame.motion = &own.motion;
    frame.reactive = &own.reactive;
    frame.output = &own.output;
    if (own.reconstruct) {
        frame.albedo = &own.albedo;
        frame.specular_albedo = &own.specular;
        frame.normal_roughness = &own.normals;
    }
    frame.render = render;
    frame.output_size = output;
    // The jitter, and Direct3D 9's pixel centres: the game's pixel x shows the point at x (the Vulkan Gpu moves every
    // draw half a pixel), an upscaler takes it to show x + 0.5 - so the output would sit half an output pixel off a
    // picture drawn at full size; the offset a provider is told carries that difference, 0.5 (1 - drawn / output).
    const UpscaleJitterTerms terms = own.terms_set ? own.jitter_terms : own.provider->jitter_terms();
    const std::array<float, 2> align{
        core::at(terms.align, 0) * kHalfF * (1.0F - (static_cast<float>(render.width) / static_cast<float>(output.width))),
        core::at(terms.align, 1) * kHalfF *
            (1.0F - (static_cast<float>(render.height) / static_cast<float>(output.height)))};
    frame.jitter = {(core::at(terms.sign, 0) * core::at(view.jitter, 0)) + core::at(align, 0),
                    (core::at(terms.sign, 1) * core::at(view.jitter, 1)) + core::at(align, 1)};
    frame.reset = own.reset || own.last_render.width != render.width || own.last_render.height != render.height ||
                  own.last_output.width != output.width || own.last_output.height != output.height;
    frame.sharpness = own.settings.sharpness;
    const auto time_now = std::chrono::steady_clock::now();
    if (own.passes > 0) {
        frame.frame_milliseconds = std::clamp(
            static_cast<float>(std::chrono::duration<double>(time_now - own.last_time).count() * kMillisecondsPerSecond),
            kLeastFrameMilliseconds, kLongestFrameMilliseconds);
    }
    own.last_time = time_now;
    frame.view = view.camera.view;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            core::at(core::at(frame.projection, row), column) = core::at(motion.now.projection, (row * 4) + column);
        }
    }
    lens_of(frame.projection, &frame);
    // (frame generation - vulkan_framegen.h: a cut throws the history away, for the made frames as well)
    if (frame_generation_cut(state)) {
        frame.reset = true;
    }
    const auto started = std::chrono::steady_clock::now();
    const bool upscaled = own.provider->evaluate(state, frame, &error);
    own.evaluate_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    state.bound = BoundState{}; // (the provider bound its own pipelines and descriptors)
    if (!upscaled) {
        own.failed = true;
        publish_scale(false);
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: upscaler: %s failed (%s) - upscaling off", own.provider->name(), error.c_str());
        }
        Memory::transition(commands, colour, colour->resting);
        return false;
    }
    full_barrier(commands);
    // (frame generation's inputs: this frame's depth, motion and camera - vulkan_framegen.h)
    framegen_inputs(state, frame, view);
    // 4. the output over the view
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    Memory::transition(commands, &own.output, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    blit(commands, own.output, {0, 0, static_cast<int32_t>(output.width), static_cast<int32_t>(output.height)}, *colour,
         rect_of(shown, output));
    Memory::transition(commands, &own.output, VK_IMAGE_LAYOUT_GENERAL);
    Memory::transition(commands, colour, colour->resting == VK_IMAGE_LAYOUT_UNDEFINED
                                             ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                             : colour->resting);
    const uint64_t serial = state.frames.serial();
    for (Image* image : {colour, depth, &own.colour, &own.depth, &own.motion, &own.reactive, &own.output, &motion.image}) {
        image->used = serial;
    }
    own.reset = false;
    own.last_render = render;
    own.last_output = output;
    ++own.passes;
    ++own.jitter_index;
    publish_scale(true);
    if (first_report(state, kReportFirst)) {
        get_log("vulkan: upscaler: the first frame upscaled by %s (%ux%u -> %ux%u, %llu draws with motion)",
                own.provider->name(), render.width, render.height, output.width, output.height,
                static_cast<unsigned long long>(motion.draws));
    }
    if (own.passes % kLogEvery == 0) {
        get_log("vulkan: upscaler: %llu frames by %s (%ux%u -> %ux%u), %.3f ms of processor time each recording it",
                static_cast<unsigned long long>(own.passes), own.provider->name(), render.width, render.height,
                output.width, output.height, own.evaluate_seconds * kMillisecondsPerSecond / static_cast<double>(kLogEvery));
        own.evaluate_seconds = 0.0;
    }
    return true;
}

UpscaleImages upscale_images()
{
    UpscaleState& own = upscale();
    return {.colour = &own.colour, .depth = &own.depth, .motion = &own.motion, .reactive = &own.reactive,
            .output = &own.output};
}

const char* upscale_provider_name()
{
    const UpscaleState& own = upscale();
    return own.provider ? own.provider->name() : nullptr;
}

uint64_t upscale_passes() { return upscale().passes; }

VkImageUsageFlags upscale_depth_usage(const Backend& state, VkFormat format)
{
    if (upscale().settings.upscaler == Upscaler::kOff) {
        return 0;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(state.context.physical_device(), format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT
                                                                                         : 0;
}

void set_upscale_jitter_terms(const UpscaleJitterTerms* terms)
{
    const std::scoped_lock lock(backend().mutex);
    UpscaleState& own = upscale();
    own.terms_set = terms != nullptr;
    own.jitter_terms = terms != nullptr ? *terms : UpscaleJitterTerms{};
}

UpscaleJitterTerms upscale_jitter_terms()
{
    const std::scoped_lock lock(backend().mutex);
    const UpscaleState& own = upscale();
    if (own.terms_set) {
        return own.jitter_terms;
    }
    return own.provider ? own.provider->jitter_terms() : UpscaleJitterTerms{};
}

bool ray_reconstruction_active()
{
    const UpscaleState& own = upscale();
    return own.provider != nullptr && own.reconstruct && !own.failed;
}

void release_upscale(Backend& state)
{
    UpscaleState& own = upscale();
    if (own.provider) {
        own.provider->release(state);
        own.provider.reset();
    }
    own.provider_tried = false;
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        if (own.prepare != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, own.prepare, nullptr);
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
    own.prepare = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    if (state.memory.ready()) {
        for (Image* image : {&own.colour, &own.depth, &own.motion, &own.reactive, &own.output, &own.albedo,
                             &own.specular, &own.normals, &state.motion.image}) {
            state.memory.destroy_image(image);
        }
    }
    state.motion = MotionVectors{};
    own.view_on = false;
    own.reset = true;
    own.failed = false;
}

} // namespace render::vulkan
