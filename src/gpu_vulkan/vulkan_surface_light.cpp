// NOT ORIGINAL - round V: the world lit once per surface (vulkan_surface_light.h; docs/design/vulkan.md "Round V").
#include "gpu_vulkan/vulkan_surface_light.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/enum_ops.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/surface_trail.h"
#include "engine/gfx/water_surfaces.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_cel_draw.h"
#include "gpu_vulkan/vulkan_hdr.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_rt_glsl.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "gpu_vulkan/vulkan_water.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportFirstView = 0x52560001U;
constexpr uint64_t kReportNotHere = 0x52560002U;
constexpr uint64_t kReportFailed = 0x52560003U;
constexpr uint64_t kReportLeftOver = 0x52560004U;
constexpr uint64_t kReportNotRelit = 0x52560005U;

constexpr uint32_t kBlendZero = 1;         // D3DBLEND_ZERO
constexpr uint32_t kBlendOne = 2;          // D3DBLEND_ONE
constexpr uint32_t kBlendSourceAlpha = 5;  // D3DBLEND_SRCALPHA
constexpr uint32_t kBlendInverseAlpha = 6; // D3DBLEND_INVSRCALPHA
constexpr uint32_t kGroupSide = 8;
constexpr uint32_t kBeginBindings = 7;
constexpr std::size_t kMostKept = 16384; // (a view's world draws: a few hundred; past this, drawn as they come)

// The inputs begun: the prepass's depth copied (read as the depth surface's), the visibility "not traced", nothing
// occluded, no lamps - over the view's rectangle (rect: x, y, width, height).
constexpr const char* kBeginSource = R"(#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D u_depth;
layout(set = 0, binding = 1, r32f) uniform writeonly image2D o_depth;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_visibility;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D o_lamps;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D o_lamps_on;
layout(set = 0, binding = 5, rgba16f) uniform writeonly image2D o_decals;
layout(set = 0, binding = 6, rgba32ui) uniform writeonly uimage2D o_guides;
layout(push_constant) uniform Pass { ivec4 rect; } pc;
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(local, pc.rect.zw))) {
        return;
    }
    ivec2 p = pc.rect.xy + local;
    imageStore(o_depth, p, vec4(texelFetch(u_depth, p, 0).r, 0.0, 0.0, 0.0));
    imageStore(o_visibility, p, vec4(-1.0, 0.0, 1.0, 0.0));
    imageStore(o_lamps, p, vec4(0.0));
    imageStore(o_lamps_on, p, vec4(0.0));
    imageStore(o_decals, p, vec4(1.0));
    imageStore(o_guides, p, uvec4(0u));
}
)";

// A draw kept for the view's shading: the game's state block and call, and what record_draw reads besides them - the
// per-draw state render_pc names before each draw (the light store's source, the trails' ground, the water's material
// and body) - named again while it is drawn.
struct KeptDraw {
    GpuStateBlock block;
    DrawCall call;
    CelSourceNamed cel;
    bool ground = false;
    bool character = false;
    const GpuTexture* water_texture = nullptr;
    uint32_t water_material = 0;
    water::WaterBody water_body;
    bool water_waves = false;
    bool prepassed = false; // its depth written by the prepass: drawn without writing it again
};

enum class Phase : uint8_t {
    kIdle,    // no view lit per surface: draws as they come
    kKeeping, // a view begun: its world's draws kept (once decided)
    kInputs,  // its inputs begun: the passes write them
};

struct LightState {
    Phase phase = Phase::kIdle;
    SurfaceDrawRole role = SurfaceDrawRole::kAsItComes;
    bool decided = false;      // whether this view is lit per surface was decided (at its first world draw)
    bool on = false;           // ... and it is
    bool prepass_blended = false; // the prepass now is a blended draw's (kLightDepthHalf)
    rt::ViewDrawn view;
    Surface* target = nullptr; // the view's colour target and depth at its beginning
    Surface* depth = nullptr;
    std::vector<KeptDraw> kept;
    Image depth_copy;
    Image visibility;
    Image lamps;
    Image lamps_on;
    Image decals;
    Image guides;
    uint64_t guides_frame = 0; // the frame whose view wrote them (0: none)
    bool guides_forced = false; // (the checks': written without Ray Reconstruction)
    std::vector<DeviceBuffer> kept_buffers; // (kept draws' vertices and indices whose buffers were written again)
    std::unordered_set<VkBuffer> kept_reads; // the buffers the kept draws read now
    SurfaceInputs inputs;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline begin = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE; // (nearest, clamped: the depth, and the inputs for the draws)
    bool failed = false;
    SurfaceLightStats stats;
};

LightState& light_state()
{
    static LightState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// What a draw's blend does: nothing (off, or one x the source + zero x what is there - the game's opaque passes drawn
// with the blend on), an alpha blend (by the source's alpha), or something else (an added glow, a multiplied mark).
enum class Blend : uint8_t { kNone, kAlpha, kOther };
Blend blend_of(const GpuStateBlock& block)
{
    if (block.render_state(GpuRenderState::kAlphaBlendEnable) == 0) {
        return Blend::kNone;
    }
    const uint32_t source = block.render_state(GpuRenderState::kSrcBlend);
    const uint32_t dest = block.render_state(GpuRenderState::kDestBlend);
    if (source == kBlendOne && dest == kBlendZero) {
        return Blend::kNone;
    }
    return source == kBlendSourceAlpha && dest == kBlendInverseAlpha ? Blend::kAlpha : Blend::kOther;
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

bool ensure_objects(Backend& state, std::string* error)
{
    LightState& own = light_state();
    if (own.begin != VK_NULL_HANDLE) {
        return true;
    }
    const VkDevice device = state.context.device();
    std::array<VkDescriptorSetLayoutBinding, kBeginBindings> bindings{};
    for (uint32_t binding = 0; binding < kBeginBindings; ++binding) {
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
                                                   .bindingCount = kBeginBindings,
                                                   .pBindings = bindings.data()};
    const VkPushConstantRange constants{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(std::array<int32_t, 4>)};
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
    const CompiledShader compiled = compile_rt_glsl(RtShaderStage::kCompute, kBeginSource);
    if (!compiled.ok) {
        *error = "the inputs' shader doesn't compile: " + compiled.log;
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
    const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &own.begin);
    vkDestroyShaderModule(device, module, nullptr);
    if (made != VK_SUCCESS) {
        own.begin = VK_NULL_HANDLE;
        *error = "vkCreateComputePipelines failed";
        return false;
    }
    return true;
}

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

// Whether the view now begun can be lit per surface (asked at its first world draw: by then its targets are bound and
// the frame's scene is built).
bool view_can_be_lit(Backend& state)
{
    LightState& own = light_state();
    if (own.failed || !state.context.ray_tracing_enabled() || !state.linear_phase) {
        if (!own.failed && first_report(state, kReportNotRelit + 1)) {
            get_log("vulkan: per-surface lighting: a view not lit per surface (%s) - drawn as before",
                    state.linear_phase ? "no ray tracing" : "not in the linear frame");
        }
        return false;
    }
    Image* const colour = image_of_surface(own.target);
    Image* const depth = image_of_surface(own.depth);
    if (colour == nullptr || depth == nullptr || colour->format != kHdrFormat || colour->samples != VK_SAMPLE_COUNT_1_BIT ||
        depth->samples != VK_SAMPLE_COUNT_1_BIT || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 ||
        (depth->usage & VK_IMAGE_USAGE_SAMPLED_BIT) == 0 || depth->view == VK_NULL_HANDLE ||
        depth->extent.width < colour->extent.width || depth->extent.height < colour->extent.height) {
        if (first_report(state, kReportNotHere)) {
            get_log("vulkan: per-surface lighting: a view whose targets can't take it (multisampled, not the linear "
                    "frame, or no samplable depth) - its passes draw over the colour as before");
        }
        return false;
    }
    // (cel shading bands the light itself - its own path)
    const bool can = current_scene(state).tlas != VK_NULL_HANDLE && rt::cel_world().relit && !rt::cel_settings().on;
    if (!can && first_report(state, kReportNotRelit)) {
        get_log("vulkan: per-surface lighting: a view not lit per surface (%s) - drawn as before",
                current_scene(state).tlas == VK_NULL_HANDLE ? "no scene to trace yet"
                : !rt::cel_world().relit                    ? "the level isn't relit"
                                                            : "cel shading");
    }
    return can;
}

// The view's depth cleared to the far plane (as the draws' passes, its stencil isn't attached) - outside a pass.
void clear_depth(Backend& state)
{
    Image* const depth = image_of_surface(light_state().depth);
    if (depth == nullptr || (depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0) {
        return;
    }
    // (a pass of its own that clears the depth as it opens - the depth surfaces are attachments, not transfer targets)
    VkCommandBuffer commands = outside_pass(state);
    Memory::transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_info.imageView = depth->attachment_view;
    depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_info.resolveMode = VK_RESOLVE_MODE_NONE;
    depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_info.clearValue.depthStencil = {.depth = 1.0F, .stencil = 0};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {.offset = {.x = 0, .y = 0}, .extent = {.width = depth->extent.width, .height = depth->extent.height}};
    info.layerCount = 1;
    info.pDepthAttachment = &depth_info;
    vkCmdBeginRendering(commands, &info);
    vkCmdEndRendering(commands);
    depth->used = state.frames.serial();
    state.bound = BoundState{};
}

// The kept draws drawn in their order (`inputs`: with the view's light inputs, else as they came), render_pc's per-draw
// state named for each and put back after.
void draw_kept(Backend& state, bool inputs)
{
    LightState& own = light_state();
    const CelSourceNamed cel_before = cel_source_now();
    const bool ground_before = surface_trail::current_ground();
    const bool character_before = surface_trail::current_character();
    const GpuTexture* const water_before = water::current_texture();
    const uint32_t material_before = water::current_material();
    const water::WaterBody body_before = water::current_body();
    const bool waves_before = water::current_waves();
    own.role = inputs ? SurfaceDrawRole::kShaded : SurfaceDrawRole::kAsItComes;
    std::vector<KeptDraw> kept;
    kept.swap(own.kept);
    own.kept_reads.clear();
    // The depth cleared: the draws are made again as the game made them, each with its own depth test and write, in its
    // order - the prepass's depth (the nearest surface, translucent ones' mostly opaque parts too: what the inputs were
    // made for) would hide what a translucent layer drawn later lets through (a shirt's print, a road's edge).
    if (!kept.empty()) {
        clear_depth(state);
    }
    for (KeptDraw& draw : kept) {
        cel_source_put(draw.cel);
        surface_trail::set_current_ground(draw.ground);
        surface_trail::set_current_character(draw.character);
        water::set_current(draw.water_texture, draw.water_material);
        water::set_current_body(draw.water_body);
        water::set_current_waves(draw.water_waves);
        (void)record_draw(state, draw.block, draw.call);
        ++own.stats.shaded;
    }
    own.role = SurfaceDrawRole::kAsItComes;
    for (DeviceBuffer& copy : own.kept_buffers) {
        retire_buffer(state, &copy); // (freed once this frame's submission is done)
    }
    own.kept_buffers.clear();
    cel_source_put(cel_before);
    surface_trail::set_current_ground(ground_before);
    surface_trail::set_current_character(character_before);
    water::set_current(water_before, material_before);
    water::set_current_body(body_before);
    water::set_current_waves(waves_before);
    kept.clear();
    if (own.kept.empty()) {
        own.kept.swap(kept); // (the vector's storage kept for the next view)
    }
}

// A view's leftovers drawn as they came (a view that ended without its opaque_drawn).
void flush(Backend& state)
{
    LightState& own = light_state();
    own.phase = Phase::kIdle; // (before the draws: they aren't kept again)
    own.inputs = {};
    if (!own.kept.empty()) {
        if (first_report(state, kReportLeftOver)) {
            get_log("vulkan: per-surface lighting: a view's kept draws drawn without their inputs (the view ended before "
                    "its opaque world was told)");
        }
        draw_kept(state, false);
    }
    own.phase = Phase::kIdle;
    own.decided = false;
    own.on = false;
}

// ViewOrder::kSurfaceInputs: the inputs begun from the prepass's depth.
bool begin_inputs(Backend& state)
{
    LightState& own = light_state();
    std::string error;
    Image* const colour = image_of_surface(own.target);
    Image* const depth = image_of_surface(own.depth);
    constexpr VkImageUsageFlags kUsage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (colour == nullptr || depth == nullptr || !ensure_objects(state, &error)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: per-surface lighting couldn't be made (%s) - the passes draw over the colour as before",
                    error.empty() ? "its targets" : error.c_str());
        }
        return false;
    }
    const Extent extent{.width = colour->extent.width, .height = colour->extent.height};
    if (!ensure_image(state, &own.depth_copy, VK_FORMAT_R32_SFLOAT, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !ensure_image(state, &own.visibility, kHdrFormat, extent, kUsage) ||
        !ensure_image(state, &own.lamps, kHdrFormat, extent, kUsage) ||
        !ensure_image(state, &own.lamps_on, kHdrFormat, extent, kUsage) ||
        !ensure_image(state, &own.decals, kHdrFormat, extent,
                      kUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !ensure_image(state, &own.guides, VK_FORMAT_R32G32B32A32_UINT, extent,
                      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        own.failed = true;
        if (first_report(state, kReportFailed)) {
            get_log("vulkan: per-surface lighting: its images couldn't be made - the passes draw over the colour as before");
        }
        return false;
    }
    const uint32_t left = std::min(core::at(own.view.rect, 0), extent.width);
    const uint32_t top = std::min(core::at(own.view.rect, 1), extent.height);
    const uint32_t width = std::min(core::at(own.view.rect, 2), extent.width - left);
    const uint32_t height = std::min(core::at(own.view.rect, 3), extent.height - top);
    VkCommandBuffer commands = outside_pass(state);
    Memory::transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    for (Image* image : {&own.depth_copy, &own.visibility, &own.lamps, &own.lamps_on, &own.decals, &own.guides}) {
        Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.begin);
    const VkDescriptorImageInfo depth_info{
        .sampler = own.sampler, .imageView = depth->view, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const std::array<VkDescriptorImageInfo, kBeginBindings - 1> storage{{
        {.sampler = VK_NULL_HANDLE, .imageView = own.depth_copy.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.visibility.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.lamps.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.lamps_on.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.decals.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
        {.sampler = VK_NULL_HANDLE, .imageView = own.guides.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
    }};
    std::array<VkWriteDescriptorSet, kBeginBindings> writes{};
    for (uint32_t binding = 0; binding < kBeginBindings; ++binding) {
        VkWriteDescriptorSet& write = core::at(writes, binding);
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = binding == 0 ? &depth_info : &core::at(storage, binding - 1);
    }
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_COMPUTE, own.layout, 0, kBeginBindings, writes.data());
    const std::array<int32_t, 4> rect{static_cast<int32_t>(left), static_cast<int32_t>(top), static_cast<int32_t>(width),
                                      static_cast<int32_t>(height)};
    vkCmdPushConstants(commands, own.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rect), rect.data());
    vkCmdDispatch(commands, (width + kGroupSide - 1) / kGroupSide, (height + kGroupSide - 1) / kGroupSide, 1);
    const VkMemoryBarrier written{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                  .pNext = nullptr,
                                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                  .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &written,
                         0, nullptr, 0, nullptr);
    state.bound = BoundState{};
    const uint64_t serial = state.frames.serial();
    for (Image* image : {&own.depth_copy, &own.visibility, &own.lamps, &own.lamps_on, &own.decals, &own.guides, depth}) {
        image->used = serial;
    }
    own.inputs = {.visibility = &own.visibility,
                  .lamps = &own.lamps,
                  .lamps_on = &own.lamps_on,
                  .depth = &own.depth_copy,
                  .decals = &own.decals,
                  .guides = &own.guides};
    return true;
}

// The listener: the view's beginning, its inputs begun, its draws shaded, its end.
class LightListener final : public rt::ViewListener {
public:
    explicit LightListener(bool shading) : shading_(shading) {}
    [[nodiscard]] int order() const override
    {
        return shading_ ? rt::ViewOrder::kSurfaceShading : rt::ViewOrder::kSurfaceInputs;
    }
    void view_begins(const rt::ViewDrawn& view) override
    {
        if (shading_) {
            return;
        }
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (!state.device_made || !state.draws_made) {
            return;
        }
        flush(state);
        LightState& own = light_state();
        own.phase = Phase::kKeeping;
        own.view = view;
        own.target = core::at(state.targets, 0);
        own.depth = state.depth;
        own.decided = false;
        own.on = false;
    }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        LightState& own = light_state();
        if (!state.device_made || !state.draws_made) {
            return;
        }
        if (!shading_) {
            if (own.phase != Phase::kKeeping || !own.on) {
                flush(state);
                return;
            }
            if (!begin_inputs(state)) {
                flush(state);
                return;
            }
            own.phase = Phase::kInputs;
            return;
        }
        if (own.phase != Phase::kInputs) {
            return;
        }
        // the passes' writes read by the draws
        VkCommandBuffer commands = outside_pass(state);
        for (Image* image : {&own.depth_copy, &own.visibility, &own.lamps, &own.lamps_on, &own.decals, &own.guides}) {
            Memory::transition(commands, image, VK_IMAGE_LAYOUT_GENERAL, true);
        }
        own.guides_frame = ray_reconstruction_active() || own.guides_forced ? state.frames.serial() : 0;
        own.phase = Phase::kIdle;
        state.bound = BoundState{}; // (the inputs pushed afresh: draw_kept's draws then keep them)
        const SurfaceLightStats before = own.stats;
        const std::size_t kept = own.kept.size();
        draw_kept(state, true);
        if (own.stats.views <= 3 || own.stats.views % 3600 == 0) {
            get_log("vulkan: per-surface lighting: view %llu: %zu draws kept, %llu prepassed so far, %llu shaded now; %llu "
                    "world draws into other targets, %llu buffers kept before a write so far",
                    static_cast<unsigned long long>(own.stats.views), kept,
                    static_cast<unsigned long long>(own.stats.prepassed),
                    static_cast<unsigned long long>(own.stats.shaded - before.shaded),
                    static_cast<unsigned long long>(own.stats.elsewhere),
                    static_cast<unsigned long long>(own.stats.rewritten));
        }
        own.inputs = {};
        own.decided = false;
        own.on = false;
        // (Ray Reconstruction's albedo guide: the world now shaded - vulkan_upscale.h)
        record_reconstruction_albedo(state, own.view);
    }
    void world_drawn(const rt::ViewDrawn& /*view*/) override
    {
        if (shading_) {
            return;
        }
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            flush(state);
        }
    }

private:
    bool shading_ = false;
};

LightListener& inputs_listener()
{
    static LightListener instance(false);
    return instance;
}

LightListener& shading_listener()
{
    static LightListener instance(true);
    return instance;
}

} // namespace

void install_surface_light()
{
    rt::add_view_listener(&inputs_listener());
    rt::add_view_listener(&shading_listener());
}

void uninstall_surface_light()
{
    rt::remove_view_listener(&inputs_listener());
    rt::remove_view_listener(&shading_listener());
}

void release_surface_light(Backend& state)
{
    LightState& own = light_state();
    own.kept.clear();
    own.phase = Phase::kIdle;
    own.role = SurfaceDrawRole::kAsItComes;
    own.inputs = {};
    for (Image* image : {&own.depth_copy, &own.visibility, &own.lamps, &own.lamps_on, &own.decals, &own.guides}) {
        if (image->image != VK_NULL_HANDLE) {
            state.memory.destroy_image(image);
        }
        *image = Image{};
    }
    const VkDevice device = state.context.device();
    if (device != VK_NULL_HANDLE) {
        if (own.begin != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, own.begin, nullptr);
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
    own.begin = VK_NULL_HANDLE;
    own.layout = VK_NULL_HANDLE;
    own.set_layout = VK_NULL_HANDLE;
    own.sampler = VK_NULL_HANDLE;
    own.failed = false;
}

bool surface_light_keep(Backend& state, const GpuStateBlock& block, const DrawCall& call)
{
    LightState& own = light_state();
    if (own.phase != Phase::kKeeping || own.role != SurfaceDrawRole::kAsItComes || !rt::world_surfaces_drawing()) {
        return false;
    }
    if (core::at(state.targets, 0) != own.target || state.depth != own.depth) {
        own.stats.elsewhere += own.on ? 1U : 0U;
        return false;
    }
    if (!own.decided) {
        own.decided = true;
        own.on = view_can_be_lit(state);
        if (own.on) {
            ++own.stats.views;
            if (first_report(state, kReportFirstView)) {
                get_log("vulkan: per-surface lighting: the first view lit per surface (%ux%u) - its world kept, its "
                        "depth drawn first, its light inputs made, then its draws shaded",
                        core::at(own.view.rect, 2), core::at(own.view.rect, 3));
            }
        }
    }
    if (!own.on || own.kept.size() >= kMostKept) {
        return false;
    }
    KeptDraw& kept = own.kept.emplace_back();
    kept.block = block;
    kept.call = call;
    own.kept_reads.insert(call.vertex_buffer);
    if (call.indexed) {
        own.kept_reads.insert(call.index_buffer);
    }
    kept.cel = cel_source_now();
    kept.ground = surface_trail::current_ground();
    kept.character = surface_trail::current_character();
    kept.water_texture = water::current_texture();
    kept.water_material = water::current_material();
    kept.water_body = water::current_body();
    kept.water_waves = water::current_waves();
    ++own.stats.kept;
    // (the prepass finds the nearest surface the light inputs are made for: every draw that writes depth but an added or
    // multiplied one - an alpha-blended one by its mostly opaque part; the draws are made again over a cleared depth)
    const bool writes_depth = block.render_state(GpuRenderState::kZEnable) != 0 &&
                              block.render_state(GpuRenderState::kZWriteEnable) != 0 && blend_of(block) != Blend::kOther;
    if (writes_depth && !water_candidate(state, block)) {
        // the prepass: the depth alone, at once (record_draw makes its key depth alone - surface_light_finish): the kept
        // block itself drawn with no colour, no blend and no lights (the lights' work isn't the depth's; a lit draw's
        // alpha then its vertex colour's), then put back
        own.prepass_blended = blend_of(block) != Blend::kNone;
        GpuStateBlock& depth_only = kept.block;
        uint32_t& write = core::at(depth_only.render_states, core::to_underlying(GpuRenderState::kColourWriteEnable));
        uint32_t& blend = core::at(depth_only.render_states, core::to_underlying(GpuRenderState::kAlphaBlendEnable));
        uint32_t& lighting = core::at(depth_only.render_states, core::to_underlying(GpuRenderState::kLighting));
        const uint32_t write_was = write;
        const uint32_t blend_was = blend;
        const uint32_t lighting_was = lighting;
        const uint32_t lights_was = depth_only.lights_enabled;
        write = 0;
        blend = 0;
        lighting = 0;
        depth_only.lights_enabled = 0;
        own.role = SurfaceDrawRole::kPrepass;
        (void)record_draw(state, depth_only, call);
        own.role = SurfaceDrawRole::kAsItComes;
        write = write_was;
        blend = blend_was;
        lighting = lighting_was;
        depth_only.lights_enabled = lights_was;
        kept.prepassed = true;
        ++own.stats.prepassed;
    }
    return true;
}

SurfaceDrawRole surface_draw_role() { return light_state().role; }

void surface_light_force(const GpuStateBlock& block, bool relit, ShaderKey* shaders)
{
    (void)block;
    if (light_state().role == SurfaceDrawRole::kAsItComes) {
        return;
    }
    FragmentKey& fragment = shaders->fragment;
    if (fragment.surface != 0 || fragment.water != 0U || fvf_pre_transformed(shaders->vertex.fvf)) {
        return; // (lit per pixel already, water, or not a 3D draw)
    }
    const VertexKey& vertex = shaders->vertex;
    if (relit) {
        fragment.surface = kSurfaceLighting; // (record_draw adds kSurfaceRelit with the store's words)
    } else if (vertex.lighting && vertex.light_count > 0 && (vertex.fvf & kFvfNormal) != 0U) {
        fragment.surface = kSurfaceLighting | kSurfaceLit;
    } else {
        return;
    }
    *shaders = normalised(*shaders); // (a program that can't take it: cleared again)
}

void surface_light_finish(Backend& state, const GpuStateBlock& block, ShaderKey* shaders, SurfaceInputDescriptors* out)
{
    *out = {};
    LightState& own = light_state();
    FragmentKey& fragment = shaders->fragment;
    if (own.role == SurfaceDrawRole::kPrepass) {
        // (its vertex half kept: the shaded draw's, so both put every vertex at the same depth)
        fragment.surface = 0;
        fragment.detail = 0;
        fragment.trails = 0;
        fragment.water = 0;
        fragment.light = static_cast<uint8_t>(kLightDepthOnly | (own.prepass_blended ? kLightDepthHalf : 0U));
        return;
    }
    if (own.role != SurfaceDrawRole::kShaded || own.inputs.visibility == nullptr || fragment.water != 0U ||
        fvf_pre_transformed(shaders->vertex.fvf) || (fragment.surface & (kSurfaceCel | kSurfaceCelHull)) != 0U) {
        return;
    }
    if ((fragment.surface & kSurfaceLighting) != 0U) {
        fragment.light = kLightInputs;
    } else if (fragment.surface == 0) {
        // (a colour lit already: its light by shares - not on a draw that adds or multiplies, a glow or a decal)
        if (blend_of(block) == Blend::kOther) {
            return;
        }
        fragment.light = kLightShare;
    } else {
        return;
    }
    out->on = true;
    const std::array<const Image*, 6> images{own.inputs.visibility, own.inputs.lamps, own.inputs.lamps_on,
                                             own.inputs.depth,      own.inputs.decals, own.inputs.guides};
    // (Ray Reconstruction's guides: written while it denoises the view)
    if (ray_reconstruction_active() || own.guides_forced) {
        fragment.light = static_cast<uint8_t>(fragment.light | kLightGuides);
        out->guides = true;
    }
    for (std::size_t i = 0; i < images.size(); ++i) {
        core::at(out->images, i) = {.sampler = own.sampler,
                                    .imageView = core::at(images, i)->view,
                                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    }
    (void)state;
}

const SurfaceInputs* surface_inputs(Backend& state)
{
    (void)state;
    const LightState& own = light_state();
    return own.phase == Phase::kInputs && own.inputs.visibility != nullptr ? &own.inputs : nullptr;
}

bool surface_light_view(const Backend& state)
{
    (void)state;
    const LightState& own = light_state();
    return own.on && (own.phase == Phase::kKeeping || own.phase == Phase::kInputs);
}

void surface_light_buffer_rewritten(Backend& state, VkBuffer buffer, VkDeviceSize bytes, VkCommandBuffer commands,
                                    const void* mapped)
{
    LightState& own = light_state();
    if (own.kept.empty() || buffer == VK_NULL_HANDLE || bytes == 0) {
        return;
    }
    if (!own.kept_reads.contains(buffer)) {
        return;
    }
    DeviceBuffer copy;
    constexpr VkBufferUsageFlags kUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (!state.memory.create_buffer(bytes, kUsage, commands != nullptr ? BufferPlace::kDevice : BufferPlace::kUpload,
                                    &copy)) {
        if (first_report(state, kReportFailed + 0x10)) {
            get_log("vulkan: per-surface lighting: a kept draw's buffer couldn't be kept before it was written again");
        }
        return;
    }
    if (commands != nullptr) {
        const VkBufferCopy region{.srcOffset = 0, .dstOffset = 0, .size = bytes};
        vkCmdCopyBuffer(commands, buffer, copy.buffer, 1, &region);
        // (the copy read before the write that follows it writes)
        const VkMemoryBarrier done{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                   .pNext = nullptr,
                                   .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                                                    VK_ACCESS_INDEX_READ_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &done, 0, nullptr,
                             0, nullptr);
    } else if (mapped != nullptr && copy.mapped != nullptr) {
        std::memcpy(copy.mapped, mapped, static_cast<std::size_t>(bytes));
        state.memory.flush(copy, 0, bytes);
    }
    for (KeptDraw& kept : own.kept) {
        if (kept.call.vertex_buffer == buffer) {
            kept.call.vertex_buffer = copy.buffer;
        }
        if (kept.call.indexed && kept.call.index_buffer == buffer) {
            kept.call.index_buffer = copy.buffer;
        }
    }
    own.kept_buffers.push_back(copy);
    own.kept_reads.erase(buffer); // (no kept draw reads it now)
    ++own.stats.rewritten;
}

void set_surface_light_guides_forced(bool forced) { light_state().guides_forced = forced; }

Image* surface_light_guides(Backend& state)
{
    LightState& own = light_state();
    return own.guides_frame != 0 && own.guides_frame == state.frames.serial() && own.guides.image != VK_NULL_HANDLE
               ? &own.guides
               : nullptr;
}

void surface_light_forget_buffer(const void* buffer)
{
    for (KeptDraw& kept : light_state().kept) {
        if (buffer == nullptr || kept.cel.vertices == buffer || kept.cel.stream == buffer) {
            kept.cel = {};
        }
    }
}

SurfaceLightStats surface_light_stats() { return light_state().stats; }

} // namespace render::vulkan
