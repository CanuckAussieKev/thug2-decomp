// NOT ORIGINAL - cel shading on the Vulkan Gpu (vulkan_cel.h, vulkan_cel_draw.h; engine/gfx/rt_cel.h).
#include "gpu_vulkan/vulkan_cel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/glsl_compiler.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_cel_draw.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_motion.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr uint64_t kReportFirst = 0x43454C01U; // (Backend::reported's keys)
constexpr uint64_t kReportPipeline = 0x43454C02U;
constexpr uint64_t kReportNoDepth = 0x43454C03U;
constexpr uint64_t kReportTarget = 0x43454C04U;

constexpr std::size_t kViewSlot = 1; // (GpuStateBlock::transforms: world, view, projection)
constexpr std::size_t kSide = 4;
constexpr uint32_t kFvfNormalBit = 0x010U;
constexpr float kBaseHeight = 720.0F;
constexpr float kEdgeWidth = 0.75F;      // a band's edge: this many of the derivative's width (about a pixel)
constexpr float kRayOffset = 2.0F;       // the ray's start off the surface (the relit mode's own)
constexpr float kRayAlong = 1.0F;        // ... and along the sun
constexpr float kRayLength = 1.0e6F;     // (further than any level)
constexpr float kFadeStart = 2500.0F;    // the world's lines faded out over this distance (world units) ...
constexpr float kFadeEnd = 9000.0F;
constexpr float kCreaseWeight = 0.6F;    // a crease's line against a silhouette's
constexpr float kLeastStep = 0.6F;       // a depth step shallower than this (world units) ...
constexpr float kLeastStepShare = 0.001F; // ... plus this share of the distance draws no line (decals, stains)
constexpr float kHalf = 0.5F;
constexpr uint32_t kTimerPairs = 8;
constexpr uint64_t kTimingLogEvery = 300;
constexpr uint32_t kBindingColour = 0;
constexpr uint32_t kBindingDepth = 1;
constexpr uint32_t kBindingCount = 2;

// --- the world's outlines' pass ----------------------------------------------------------------------------------

constexpr const char* kVertexSource = R"(#version 460
void main()
{
    vec2 at = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(at * 2.0 - 1.0, 0.0, 1.0);
}
)";

// (the toon filter's measure of a bend in the depth - vulkan_style.h - one-sided on the object in front)
constexpr const char* kOutlineBody = R"(
layout(push_constant) uniform Cel {
    ivec4 out_rect;   // the colour's viewport in its target
    ivec4 depth_rect; // the depth's
    vec4 projection;  // P22, P32, P23, P33
    vec4 depth_map;   // x: the depth range's minimum; y: 1 / its span; z: h (depth pixels); w: f (depth pixels)
    vec4 fade;        // x, y: the lines faded out from - to (world units); z: a crease's weight; w: the lines' strength
    vec4 ink;         // x: the ink's lightness; y: its chroma; z, w: the least step (world units) + its share of z
} pc;
layout(set = 0, binding = 0) uniform sampler2D u_colour;
#ifdef MULTISAMPLED
layout(set = 0, binding = 1) uniform sampler2DMS u_depth;
#else
layout(set = 0, binding = 1) uniform sampler2D u_depth;
#endif
layout(location = 0) out vec4 o_colour;

const float kSky = 1.0e9;

float finite(float c) { return c >= 0.0 ? min(c, 1.0) : 0.0; }

float depth_at(ivec2 p)
{
    float d = texelFetch(u_depth, p, 0).r;
    if (!(d < 1.0)) {
        return kSky;
    }
    float ndc = (d - pc.depth_map.x) * pc.depth_map.y;
    float z = (pc.projection.y - ndc * pc.projection.w) / (ndc * pc.projection.z - pc.projection.x);
    float w = z * pc.projection.z + pc.projection.w;
    float along = abs(pc.projection.z) > 0.5 ? w : abs(z);
    return (along > 0.0 && along < kSky) ? along : kSky;
}

float axis_edge(float s)
{
    float v = abs(s) * pc.depth_map.w / pc.depth_map.z;
    float e = smoothstep(0.18, 0.45, v);
    if (s < -0.02) {
        e = 0.0;
    }
    if (!(s > 0.02)) {
        e *= pc.fade.z;
    }
    return e;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 local = p - pc.out_rect.xy;
    vec3 own = vec3(finite(texelFetch(u_colour, p, 0).r), finite(texelFetch(u_colour, p, 0).g),
                    finite(texelFetch(u_colour, p, 0).b));
    vec2 scale = vec2(pc.depth_rect.zw) / vec2(pc.out_rect.zw);
    ivec2 lo = pc.depth_rect.xy;
    ivec2 hi = pc.depth_rect.xy + pc.depth_rect.zw - 1;
    ivec2 q = lo + min(ivec2((vec2(local) + 0.5) * scale), pc.depth_rect.zw - 1);
    float zc = depth_at(q);
    float e = 0.0;
    if (zc < kSky * 0.5) {
        int h = int(pc.depth_map.z);
        float ex = 0.0;
        float ey = 0.0;
        // (a step must be deeper than a decal's lift off its wall: kept from 2 x the floor on, none under it)
        float floor_step = pc.ink.z + pc.ink.w * zc;
        if (q.x - h >= lo.x && q.x + h <= hi.x) {
            float za = depth_at(q - ivec2(h, 0));
            float zb = depth_at(q + ivec2(h, 0));
            ex = axis_edge(1.0 - zc * (1.0 / za + 1.0 / zb) * 0.5) *
                 smoothstep(floor_step, 2.0 * floor_step, max(abs(za - zc), abs(zb - zc)));
        }
        if (q.y - h >= lo.y && q.y + h <= hi.y) {
            float za = depth_at(q - ivec2(0, h));
            float zb = depth_at(q + ivec2(0, h));
            ey = axis_edge(1.0 - zc * (1.0 / za + 1.0 / zb) * 0.5) *
                 smoothstep(floor_step, 2.0 * floor_step, max(abs(za - zc), abs(zb - zc)));
        }
        e = max(ex, ey) * (1.0 - smoothstep(pc.fade.x, pc.fade.y, zc)) * pc.fade.w;
    }
    float y = dot(own, vec3(0.2126, 0.7152, 0.0722));
    vec3 ink = clamp((vec3(y) + (own - vec3(y)) * pc.ink.y) * pc.ink.x, 0.0, 1.0);
    o_colour = vec4(mix(own, ink, clamp(e, 0.0, 1.0)), 1.0);
}
)";

struct OutlineConstants {
    std::array<int32_t, 4> out_rect{};
    std::array<int32_t, 4> depth_rect{};
    std::array<float, 4> projection{};
    std::array<float, 4> depth_map{};
    std::array<float, 4> fade{};
    std::array<float, 4> ink{};
};
constexpr std::size_t kOutlineConstantBytes = 96;
static_assert(sizeof(OutlineConstants) == kOutlineConstantBytes, "the shader's push-constant block");

struct OutlinePipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool multisampled_depth = false;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

struct SourceState {
    Buffer* vertices = nullptr;
    VkDeviceSize offset = 0;
    Buffer* stream = nullptr;
};

struct FrameView {
    uint64_t serial = 0;
    uint64_t world_serial = 0;
    bool known = false;
    rt::CelWorld world;
    rt::SceneSun sun;
};

struct CelState {
    std::atomic<uint64_t> serial_seen{0};
    rt::CelSettings settings;
    SourceState source;
    VkAccelerationStructureKHR scene = VK_NULL_HANDLE;
    FrameView frame;
    CelCounts counts;
    // the outlines' pass
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    std::vector<OutlinePipeline> pipelines;
    Image source_image; // the viewport's colour copied out
    bool failed = false;
    uint64_t upscales_seen = 0;
    VkQueryPool timer = VK_NULL_HANDLE;
    double tick_milliseconds = 0.0;
    std::array<uint64_t, kTimerPairs> timer_serials{};
    uint64_t timed = 0;
    uint64_t window_samples = 0;
    double window_milliseconds = 0.0;
};

CelState& cel()
{
    static CelState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The settings as last set (read again when they change: the Options screen's writes).
const rt::CelSettings& settings_now()
{
    CelState& own = cel();
    const uint64_t serial = rt::cel_settings_serial();
    if (own.serial_seen.load(std::memory_order_relaxed) != serial) {
        own.settings = rt::cel_settings();
        own.serial_seen.store(serial, std::memory_order_relaxed);
    }
    return own.settings;
}

// What the frame's draws share (looked up once a frame: rt_scene's getters take locks).
const FrameView& frame_view(const Backend& state)
{
    FrameView& view = cel().frame;
    const uint64_t serial = state.frames.serial();
    const uint64_t world = rt::cel_world_serial();
    if (!view.known || view.serial != serial || view.world_serial != world) {
        view = {.serial = serial, .world_serial = world, .known = true, .world = rt::cel_world(), .sun = rt::scene_sun()};
    }
    return view;
}

// The toward-the-sun the draws trace and shade with: the relit stream's own (its N.L was taken toward it), else the
// scene's.
bool sun_toward(const FrameView& view, std::array<float, 3>* toward)
{
    if (view.world.relit) {
        *toward = view.world.toward;
        return true;
    }
    if (view.sun.found()) {
        *toward = view.sun.toward;
        return true;
    }
    return false;
}

class OutlineListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kCel; }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override { cel().upscales_seen = upscale_passes(); }
    void world_drawn(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            record_cel_outline_pass(state, view);
        }
    }
};

OutlineListener& listener()
{
    static OutlineListener instance;
    return instance;
}

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

std::array<uint32_t, 4> clip_rect(const std::array<uint32_t, 4>& rect, const Extent& extent)
{
    const uint32_t left = std::min(core::at(rect, 0), extent.width);
    const uint32_t top = std::min(core::at(rect, 1), extent.height);
    return {left, top, std::min(core::at(rect, 2), extent.width - left), std::min(core::at(rect, 3), extent.height - top)};
}

std::array<int32_t, 4> signed_rect(const std::array<uint32_t, 4>& rect)
{
    return {static_cast<int32_t>(core::at(rect, 0)), static_cast<int32_t>(core::at(rect, 1)),
            static_cast<int32_t>(core::at(rect, 2)), static_cast<int32_t>(core::at(rect, 3))};
}

VkShaderModule make_module(VkDevice device, RtShaderStage stage, const std::string& source, std::string* error)
{
    const CompiledShader compiled = compile_rt_glsl(stage, source);
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

bool ensure_layouts(Backend& state, std::string* error)
{
    CelState& own = cel();
    if (own.layout != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        core::at(bindings, binding) = {.binding = binding,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       .descriptorCount = 1,
                                       .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                                       .pImmutableSamplers = nullptr};
    }
    const VkDescriptorSetLayoutCreateInfo set_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                   .pNext = nullptr,
                                                   .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                   .bindingCount = static_cast<uint32_t>(bindings.size()),
                                                   .pBindings = bindings.data()};
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
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
    const VkPushConstantRange constants{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(OutlineConstants)};
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
    own.vertex = make_module(device, RtShaderStage::kVertex, kVertexSource, error);
    return own.vertex != VK_NULL_HANDLE;
}

VkPipeline outline_pipeline(Backend& state, VkFormat format, VkSampleCountFlagBits samples, bool multisampled_depth,
                            std::string* error)
{
    CelState& own = cel();
    for (const OutlinePipeline& made : own.pipelines) {
        if (made.format == format && made.samples == samples && made.multisampled_depth == multisampled_depth) {
            return made.pipeline;
        }
    }
    const VkDevice device = state.context.device();
    std::string source = "#version 460\n";
    if (multisampled_depth) {
        source += "#define MULTISAMPLED\n";
    }
    source += kOutlineBody;
    const VkShaderModule fragment = make_module(device, RtShaderStage::kFragment, source, error);
    if (fragment == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = own.vertex,
         .pName = "main",
         .pSpecializationInfo = nullptr},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = nullptr,
         .flags = 0,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = fragment,
         .pName = "main",
         .pSpecializationInfo = nullptr},
    }};
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0F;
    const VkPipelineMultisampleStateCreateInfo multisample{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                           .pNext = nullptr,
                                                           .flags = 0,
                                                           .rasterizationSamples = samples,
                                                           .sampleShadingEnable = VK_FALSE,
                                                           .minSampleShading = 0.0F,
                                                           .pSampleMask = nullptr,
                                                           .alphaToCoverageEnable = VK_FALSE,
                                                           .alphaToOneEnable = VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_FALSE;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = own.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult made = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragment, nullptr);
    if (made != VK_SUCCESS) {
        *error = "vkCreateGraphicsPipelines failed";
        return VK_NULL_HANDLE;
    }
    own.pipelines.push_back(
        {.format = format, .samples = samples, .multisampled_depth = multisampled_depth, .pipeline = pipeline});
    return pipeline;
}

bool ensure_source(Backend& state, const Image& colour)
{
    Image* image = &cel().source_image;
    if (image->image != VK_NULL_HANDLE && image->format == colour.format && image->extent.width == colour.extent.width &&
        image->extent.height == colour.extent.height) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = colour.format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = colour.extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, image)) {
        return false;
    }
    prepare_image(state, image);
    return true;
}

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
    vkCmdPipelineBarrier(commands, to_pass ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         to_pass ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : tests, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    depth->layout = layout;
}

void ensure_timer(Backend& state)
{
    CelState& own = cel();
    if (own.timer != VK_NULL_HANDLE || own.tick_milliseconds < 0.0) {
        return;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    constexpr double kNanosecondsPerMillisecond = 1.0e6;
    const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                     .pNext = nullptr,
                                     .flags = 0,
                                     .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                     .queryCount = kTimerPairs * 2,
                                     .pipelineStatistics = 0};
    if (!(properties.limits.timestampPeriod > 0.0F) ||
        vkCreateQueryPool(state.context.device(), &info, nullptr, &own.timer) != VK_SUCCESS) {
        own.timer = VK_NULL_HANDLE;
        own.tick_milliseconds = -1.0;
        return;
    }
    own.tick_milliseconds = static_cast<double>(properties.limits.timestampPeriod) / kNanosecondsPerMillisecond;
}

void read_timer(Backend& state, uint32_t pair)
{
    CelState& own = cel();
    struct Stamp {
        uint64_t value = 0;
        uint64_t available = 0;
    };
    std::array<Stamp, 2> stamps{};
    const VkResult got =
        vkGetQueryPoolResults(state.context.device(), own.timer, pair * 2, 2, sizeof(stamps), stamps.data(), sizeof(Stamp),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if ((got != VK_SUCCESS && got != VK_NOT_READY) || stamps.front().available == 0 || stamps.back().available == 0 ||
        stamps.back().value < stamps.front().value) {
        return;
    }
    const double ms = static_cast<double>(stamps.back().value - stamps.front().value) * own.tick_milliseconds;
    own.counts.outline_milliseconds = ms;
    ++own.window_samples;
    own.window_milliseconds += ms;
    if (own.window_samples >= kTimingLogEvery) {
        const CelCounts& counts = own.counts;
        get_log("vulkan: rt_cel: the outlines' pass takes %.3f ms on the device (the last %llu passes' average); draws "
                "so far: %llu world with the stream, %llu banding the bake, %llu lit (%llu hulls), %llu tracing the sun",
                own.window_milliseconds / static_cast<double>(own.window_samples),
                static_cast<unsigned long long>(own.window_samples), static_cast<unsigned long long>(counts.world_stream),
                static_cast<unsigned long long>(counts.world_bake), static_cast<unsigned long long>(counts.lit),
                static_cast<unsigned long long>(counts.hulls), static_cast<unsigned long long>(counts.shadowed));
        own.window_samples = 0;
        own.window_milliseconds = 0.0;
    }
}

bool begin_timing(Backend& state, VkCommandBuffer commands, uint32_t* pair)
{
    CelState& own = cel();
    ensure_timer(state);
    *pair = static_cast<uint32_t>(own.timed % kTimerPairs);
    uint64_t& written = core::at(own.timer_serials, *pair);
    if (own.timer == VK_NULL_HANDLE || (written != 0 && state.frames.completed() < written)) {
        return false;
    }
    if (written != 0) {
        read_timer(state, *pair);
    }
    written = state.frames.serial();
    vkCmdResetQueryPool(commands, own.timer, *pair * 2, 2);
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, own.timer, *pair * 2);
    return true;
}

void copy_colour(VkCommandBuffer commands, Image* colour, const std::array<uint32_t, 4>& shown)
{
    Image* source = &cel().source_image;
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkOffset3D corner{.x = static_cast<int32_t>(core::at(shown, 0)), .y = static_cast<int32_t>(core::at(shown, 1)), .z = 0};
    const VkExtent3D size{.width = core::at(shown, 2), .height = core::at(shown, 3), .depth = 1};
    if (colour->samples == VK_SAMPLE_COUNT_1_BIT) {
        const VkImageCopy region{
            .srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdCopyImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
        const VkImageResolve region{
            .srcSubresource = layers, .srcOffset = corner, .dstSubresource = layers, .dstOffset = corner, .extent = size};
        vkCmdResolveImage(commands, colour->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source->image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    Memory::transition(commands, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

} // namespace

// --- settings --------------------------------------------------------------------------------------------------------

void set_cel_settings(const rt::CelSettings& settings)
{
    rt::set_cel_settings(settings);
    if (settings.on) {
        rt::add_view_listener(&listener());
        rt::set_depth_sampling_wanted(true); // (the outlines read the depth)
        if (!rt::baked_sun_wanted()) {
            rt::set_baked_sun_wanted(true); // (the sun the draws trace toward)
        }
        const rt::CelSettings kept = rt::cel_settings();
        get_log("rt_cel: cel shading on (%u bands, shadow colour %.2f %.2f %.2f, rim %.2f, outlines %.2f, flatten %.2f)",
                kept.bands, static_cast<double>(std::get<0>(kept.shadow_colour)),
                static_cast<double>(std::get<1>(kept.shadow_colour)), static_cast<double>(std::get<2>(kept.shadow_colour)),
                static_cast<double>(kept.rim), static_cast<double>(kept.outline_width), static_cast<double>(kept.flatten));
    } else {
        rt::remove_view_listener(&listener());
    }
}

// --- the draws ---------------------------------------------------------------------------------------------------------

bool cel_on() { return settings_now().on; }

void cel_set_scene_locked(VkAccelerationStructureKHR tlas) { cel().scene = tlas; }

VkAccelerationStructureKHR cel_scene() { return cel().scene; }

void set_cel_source_locked(Backend& state, const Gpu::CelSource& source)
{
    Buffer* const vertices = source.vertices != nullptr ? object(source.vertices) : nullptr;
    Buffer* const stream = source.stream != nullptr ? object(source.stream) : nullptr;
    const bool known = vertices != nullptr && stream != nullptr && state.buffers.contains(vertices) &&
                       state.buffers.contains(stream);
    cel().source = known ? SourceState{.vertices = vertices, .offset = source.offset, .stream = stream} : SourceState{};
}

bool cel_owns_shadows(const Backend& state)
{
    if (!settings_now().on || !state.context.ray_tracing_enabled() || cel().scene == VK_NULL_HANDLE) {
        return false;
    }
    return frame_view(state).world.relit;
}

CelDraw cel_draw(Backend& state, const GpuStateBlock& block, const ShaderKey& key, const DrawCall& call)
{
    CelDraw draw;
    const rt::CelSettings& settings = settings_now();
    if (!settings.on) {
        return draw;
    }
    const FragmentKey& fragment = key.fragment;
    const bool program = fragment.program == PixelProgram::kMaterial || fragment.program == PixelProgram::kTextureVertexX4 ||
                         fragment.program == PixelProgram::kTextureConstantAlpha;
    if (!program || fragment.unlit != 0 || fvf_pre_transformed(key.vertex.fvf) || key.vertex.points ||
        (key.vertex.coord_mask & 1U) == 0U) {
        return draw;
    }
    const bool lit = key.vertex.lighting && key.vertex.light_count > 0 && (key.vertex.fvf & kFvfNormalBit) != 0U;
    draw.flags = kSurfaceLighting | kSurfaceCel;
    bool traced = false;
    if (lit) {
        draw.flags = static_cast<uint8_t>(draw.flags | kSurfaceLit);
        // (the skater's and the peds' meshes blend with an alpha test - their cut-outs: those outlined too; a blended
        // draw without one - glass, a glow - not)
        draw.hull = settings.outline_width > 0.0F && (block.render_state(GpuRenderState::kAlphaBlendEnable) == 0 ||
                                                       block.render_state(GpuRenderState::kAlphaTestEnable) != 0);
        traced = true;
    } else {
        const SourceState& source = cel().source;
        if (source.vertices != nullptr && source.stream != nullptr && !source.vertices->versions.empty() &&
            !source.stream->versions.empty() &&
            core::at(source.vertices->versions, source.vertices->current).memory.buffer == call.vertex_buffer &&
            source.offset == call.vertex_offset && frame_view(state).world.relit) {
            draw.flags = static_cast<uint8_t>(draw.flags | kSurfaceCelStream);
            draw.stream = source.stream;
            traced = true;
        }
    }
    if (traced && cel_owns_shadows(state)) {
        std::array<float, 3> toward{};
        if (sun_toward(frame_view(state), &toward)) {
            draw.flags = static_cast<uint8_t>(draw.flags | kSurfaceCelShadow);
        }
    }
    return draw;
}

void count_cel_draw(uint8_t flags, bool hull)
{
    CelCounts& counts = cel().counts;
    if ((flags & kSurfaceCel) == 0U) {
        return;
    }
    if ((flags & kSurfaceLit) != 0U) {
        ++counts.lit;
    } else if ((flags & kSurfaceCelStream) != 0U) {
        ++counts.world_stream;
    } else {
        ++counts.world_bake;
    }
    counts.shadowed += (flags & kSurfaceCelShadow) != 0U ? 1U : 0U;
    counts.hulls += hull ? 1U : 0U;
}

VkBuffer cel_stream_buffer(Backend& state, Buffer* stream)
{
    BufferVersion& version = core::at(stream->versions, stream->current);
    version.last_use = state.frames.serial();
    return version.memory.buffer;
}

void fill_cel_uniforms(const Backend& state, const GpuStateBlock& block, CelUniforms* uniforms)
{
    const rt::CelSettings& settings = settings_now();
    const FrameView& view = frame_view(state);
    *uniforms = {};
    const auto steps = static_cast<float>(settings.bands - 1);
    uniforms->bands = {steps, rt::kCelBandBias, kEdgeWidth, rt::cel_bake_stops(settings.bands)};
    uniforms->shadow = {std::get<0>(settings.shadow_colour), std::get<1>(settings.shadow_colour),
                        std::get<2>(settings.shadow_colour), settings.rim};
    std::array<float, 3> toward{};
    if (sun_toward(view, &toward)) {
        uniforms->sun_world = {std::get<0>(toward), std::get<1>(toward), std::get<2>(toward), 1.0F};
    }
    uniforms->sun_light = {std::get<0>(view.world.sun), std::get<1>(view.world.sun), std::get<2>(view.world.sun),
                           rt::kCelRimThreshold};
    // camera space to the world: the view's inverse (row vectors: world = eye x V^-1), each row of the result one of
    // the world's axes
    Float4x4 inverse{};
    if (motion_inverse(core::at(block.transforms, kViewSlot), &inverse)) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            core::at(uniforms->eye_to_world, axis) = {core::at(inverse, (0 * kSide) + axis), core::at(inverse, (1 * kSide) + axis),
                                                      core::at(inverse, (2 * kSide) + axis),
                                                      core::at(inverse, (3 * kSide) + axis)};
        }
    }
    uniforms->ray = {kRayOffset, kRayAlong, kRayLength, rt::kCelAmbientStops};
    uniforms->flatten = {rt::kCelFlattenBias, settings.flatten, rt::kCelResidualStep, 0.0F};
    const float height = static_cast<float>(std::max<uint32_t>(block.viewport.height, 1));
    uniforms->outline = {rt::kCelHullPixels * settings.outline_width * height / kBaseHeight, rt::kCelInkLightness,
                         rt::kCelInkChroma, rt::kCelShadowLow};
    uniforms->limits = {rt::kCelShadowHigh, 0.0F, 0.0F, 0.0F};
}

CelCounts cel_counts() { return cel().counts; }

// --- the world's outlines ------------------------------------------------------------------------------------------

bool record_cel_outline_pass(Backend& state, const rt::ViewDrawn& view)
{
    CelState& own = cel();
    const rt::CelSettings& settings = settings_now();
    if (!settings.on || !(settings.outline_width > 0.0F) || own.failed) {
        return false;
    }
    Image* const colour = image_of(core::at(state.targets, 0));
    if (colour == nullptr || colour->attachment_view == VK_NULL_HANDLE || (colour->aspect & VK_IMAGE_ASPECT_COLOR_BIT) == 0 ||
        (colour->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        if (first_report(state, kReportTarget)) {
            get_log("vulkan: rt_cel: a view drawn into a target that can't be read - no world outlines there");
        }
        return false;
    }
    const bool upscaled = upscale_passes() != own.upscales_seen && view.output_rect != view.rect &&
                          core::at(view.output_rect, 2) != 0 && core::at(view.output_rect, 3) != 0;
    own.upscales_seen = upscale_passes();
    const std::array<uint32_t, 4> shown = clip_rect(upscaled ? view.output_rect : view.rect, colour->extent);
    Image* const depth = image_of(state.depth);
    if (core::at(shown, 2) == 0 || core::at(shown, 3) == 0 || depth == nullptr) {
        return false;
    }
    const std::array<uint32_t, 4> depth_rect = clip_rect(view.rect, depth->extent);
    if ((depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 || (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 ||
        depth->view == VK_NULL_HANDLE || core::at(depth_rect, 2) == 0 || core::at(depth_rect, 3) == 0) {
        if (first_report(state, kReportNoDepth)) {
            get_log("vulkan: rt_cel: no samplable depth with the view - no world outlines there");
        }
        return false;
    }
    std::string error;
    const VkPipeline pipeline =
        ensure_layouts(state, &error)
            ? outline_pipeline(state, colour->format, colour->samples, depth->samples != VK_SAMPLE_COUNT_1_BIT, &error)
            : VK_NULL_HANDLE;
    if (pipeline == VK_NULL_HANDLE || !ensure_source(state, *colour)) {
        own.failed = true;
        if (first_report(state, kReportPipeline)) {
            get_log("vulkan: rt_cel: the outlines' pass couldn't be made (%s) - no world outlines",
                    error.empty() ? "its image" : error.c_str());
        }
        return false;
    }
    // the constants: the projection's terms (the linear depth), h and f in depth pixels
    constexpr std::size_t kRowZ = 2;
    constexpr std::size_t kRowW = 3;
    const auto& projection = view.camera.projection;
    const auto depth_height = static_cast<float>(core::at(depth_rect, 3));
    const float spacing =
        std::max(1.0F, std::round(settings.outline_width * rt::kCelWorldLinePixels * depth_height / kBaseHeight));
    const float span = core::at(view.depth_range, 1) - core::at(view.depth_range, 0);
    OutlineConstants constants{};
    constants.out_rect = signed_rect(shown);
    constants.depth_rect = signed_rect(depth_rect);
    constants.projection = {core::at(core::at(projection, kRowZ), kRowZ), core::at(core::at(projection, kRowW), kRowZ),
                            core::at(core::at(projection, kRowZ), kRowW), core::at(core::at(projection, kRowW), kRowW)};
    constants.depth_map = {core::at(view.depth_range, 0), span > 0.0F ? 1.0F / span : 1.0F, spacing,
                           std::abs(core::at(core::at(projection, 1), 1)) * depth_height * kHalf};
    constants.fade = {kFadeStart, kFadeEnd, kCreaseWeight, 1.0F};
    constants.ink = {rt::kCelInkLightness, rt::kCelInkChroma, kLeastStep, kLeastStepShare};

    VkCommandBuffer commands = outside_pass(state);
    uint32_t pair = 0;
    const bool timing = begin_timing(state, commands, &pair);
    copy_colour(commands, colour, shown);
    depth_barrier(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true);
    Memory::transition(commands, colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{.offset = {.x = static_cast<int32_t>(core::at(shown, 0)), .y = static_cast<int32_t>(core::at(shown, 1))},
                        .extent = {.width = core::at(shown, 2), .height = core::at(shown, 3)}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = area;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = static_cast<float>(area.offset.x),
                              .y = static_cast<float>(area.offset.y),
                              .width = static_cast<float>(area.extent.width),
                              .height = static_cast<float>(area.extent.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    const std::array<VkDescriptorImageInfo, kBindingCount> images{{
        {.sampler = own.sampler, .imageView = own.source_image.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {.sampler = own.sampler, .imageView = depth->view, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
    }};
    std::array<VkWriteDescriptorSet, kBindingCount> writes{};
    for (uint32_t binding = 0; binding < kBindingCount; ++binding) {
        VkWriteDescriptorSet& write = core::at(writes, binding);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &core::at(images, binding);
    }
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, own.layout, 0, kBindingCount, writes.data());
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutlineConstants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    depth_barrier(commands, depth, depth->resting, false);
    if (timing) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, own.timer, (pair * 2) + 1);
        ++own.timed;
    }
    if (colour->layout != colour->resting) {
        Memory::transition(commands, colour, colour->resting);
    }
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    colour->used = serial;
    own.source_image.used = serial;
    ++own.counts.outline_passes;
    if (first_report(state, kReportFirst)) {
        get_log("vulkan: rt_cel: the first world outlines (%ux%u%s, a line every %.0f depth pixel%s)", core::at(shown, 2),
                core::at(shown, 3), upscaled ? " upscaled" : "", static_cast<double>(spacing), spacing > 1.0F ? "s" : "");
    }
    return true;
}

void release_cel(Backend& state)
{
    CelState& own = cel();
    const VkDevice device = state.context.device();
    if (device != nullptr) {
        for (const OutlinePipeline& made : own.pipelines) {
            vkDestroyPipeline(device, made.pipeline, nullptr);
        }
        if (own.vertex != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, own.vertex, nullptr);
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
        if (own.timer != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, own.timer, nullptr);
        }
    }
    state.memory.destroy_image(&own.source_image);
    own.pipelines.clear();
    own.vertex = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.timer = VK_NULL_HANDLE;
    own.tick_milliseconds = 0.0;
    own.timer_serials = {};
    own.failed = false;
    own.scene = VK_NULL_HANDLE;
    own.source = {};
    own.frame = {};
}

} // namespace render::vulkan
