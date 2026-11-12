// NOT ORIGINAL - the Vulkan Gpu's draws, rendering passes, transfers and frames (vulkan_backend.h's second half;
// docs/design/vulkan.md, V7). The caller holds the backend's lock.
//
// - Passes are inferred (V5d): a draw or clear opens one on the device's targets (render target 0, the depth surface)
//   with dynamic rendering, loading what is there - or clearing it, when a clear of the whole target opens it; it stays
//   open while the targets stay, and ends at a copy, a transfer into something the stream used, a read-back, the
//   present, or a draw or clear into other targets. Its attachments are moved to their attachment layouts as it opens
//   (a barrier after their earlier use) and back to their resting layout as it ends (textures: to be sampled).
// - A draw's pipeline comes from the state block (vulkan_draw_state.h, vulkan_pipelines.h); its uniform blocks are copied
//   into the draws' ring when they change; its textures and samplers are pushed descriptors; its dynamic states are set
//   when they change. No barrier is recorded per draw.
// - Transfers go before the stream (the uploads command buffer) unless the stream already used what they write.
// - The present copies the back buffer (resolved first when multisampled) into the swapchain's image and ends the frame
//   (vulkan_frames.h).
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_draw_state.h"
#include "gpu_vulkan/vulkan_frames.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_pipelines.h"
#include "gpu_vulkan/vulkan_pixel_program.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_hdr.h"
#include "gpu_vulkan/vulkan_rt_effects.h"
#include "gpu_vulkan/vulkan_rt_fog.h"
#include "gpu_vulkan/vulkan_rt_reflections.h"
#include "gpu_vulkan/vulkan_rt_shadows.h"
#include "gpu_vulkan/vulkan_samplers.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr long kOk = gpu_value::kResultOk;
// The staging ring's size past which a frame's loading is submitted and waited for before it goes on.
constexpr VkDeviceSize kStagingLimit = VkDeviceSize{32} << 20U;
constexpr VkDeviceSize kStagingAlignment = 16;
// A line of counts every so many seconds (and the caches written to disk then).
constexpr double kStatsSeconds = 10.0;
// D3DRS_DEPTHBIAS is in depth units; Vulkan's constant factor in the depth format's smallest step (24 bits).
constexpr float kDepthBiasSteps = 16777216.0F;
constexpr std::size_t kSlopeScaleDepthBias = 175; // D3DRS_SLOPESCALEDEPTHBIAS
constexpr uint32_t kWriteMaskBits = 0xFU;
constexpr std::array<uint8_t, 4> kBlankTexel{0xFF, 0xFF, 0xFF, 0xFF}; // (opaque white)
constexpr VkDeviceSize kLeastUniformAlignment = 16;
constexpr std::size_t kMostClearRects = 8;
constexpr uint64_t kReportBadTarget = 0x7A55U; // (the once-only reports' keys)
constexpr uint64_t kReportGamma = 0x6A33U;
constexpr uint64_t kReportUi = 0x0175U;
constexpr double kMilliseconds = 1000.0;
constexpr double kMegabyte = 1024.0 * 1024.0;

// What reads vertex, index and uniform data.
constexpr VkPipelineStageFlags kBufferReaders = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
constexpr VkAccessFlags kBufferReads =
    VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;

// Two values' bytes the same (the uniform blocks and the viewport: plain floats, no padding).
template <typename T>
bool same_bytes(const T& one, const T& other)
{
    return std::ranges::equal(std::as_bytes(std::span(&one, 1)), std::as_bytes(std::span(&other, 1)));
}

double seconds_now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void memory_barrier(VkCommandBuffer commands, VkPipelineStageFlags from, VkAccessFlags writes, VkPipelineStageFlags to,
                    VkAccessFlags reads)
{
    const VkMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .pNext = nullptr, .srcAccessMask = writes, .dstAccessMask = reads};
    vkCmdPipelineBarrier(commands, from, to, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

// Something logged once (keyed by a hash of what it is about).
bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

// The stream's command buffer (a new one forgets what the last one had bound).
VkCommandBuffer stream(Backend& state)
{
    VkCommandBuffer commands = state.frames.stream();
    if (state.frames.take_stream_restarted()) {
        state.bound = BoundState{};
    }
    return commands;
}

// The image a surface draws into (a texture's level 0, a render target's, a depth surface's); none for the others.
Image* attachment_of(Surface* surface)
{
    if (surface == nullptr) {
        return nullptr;
    }
    Image* image = nullptr;
    switch (surface->kind) {
    case SurfaceKind::kTextureLevel:
        image = surface->level == 0 ? &surface->texture->image : nullptr;
        break;
    case SurfaceKind::kRenderTarget:
    case SurfaceKind::kDepth:
        image = &surface->image;
        break;
    case SurfaceKind::kSystemMemory:
        break;
    }
    return image != nullptr && image->attachment_view != VK_NULL_HANDLE ? image : nullptr;
}

bool is_depth(const Image& image) { return (image.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0; }

void end_query_in_pass(VkCommandBuffer commands, Backend& state, Query* query)
{
    vkCmdEndQuery(commands, query->pool, query->slot);
    query->submission = state.frames.serial();
}

void end_pass(Backend& state)
{
    OpenPass& pass = state.pass;
    if (!pass.open) {
        return;
    }
    VkCommandBuffer commands = state.frames.stream();
    for (Query* query : pass.queries) {
        end_query_in_pass(commands, state, query); // (a query still open when its pass ends: its draws so far)
    }
    vkCmdEndRendering(commands);
    for (Image* image : {pass.colour_image, pass.depth_image}) {
        if (image != nullptr && image->layout != image->resting) {
            Memory::transition(commands, image, image->resting); // (a texture drawn into: ready to be sampled)
        }
    }
    pass = OpenPass{};
}

// What a clear that opens a pass clears (its load operations).
struct PassClear {
    bool colour = false;
    bool depth = false;
    VkClearColorValue colour_value{};
    float z = 1.0F;
};

// The pass on the device's targets open (the one open kept when it is on them and nothing is to be cleared as it
// opens); false when they can't be drawn into.
bool ensure_pass(Backend& state, const PassClear* clear)
{
    Surface* const colour = core::at(state.targets, 0);
    Surface* const depth = state.depth;
    OpenPass& pass = state.pass;
    if (pass.open && pass.colour == colour && pass.depth == depth && clear == nullptr) {
        return true;
    }
    end_pass(state);
    Image* const colour_image = attachment_of(colour);
    if (colour_image == nullptr || is_depth(*colour_image)) {
        if (first_report(state, kReportBadTarget)) {
            get_log("vulkan: a draw into a surface that can't be drawn into - skipped");
        }
        return false;
    }
    Image* depth_image = attachment_of(depth);
    if (depth_image != nullptr && (!is_depth(*depth_image) || depth_image->samples != colour_image->samples)) {
        depth_image = nullptr; // (Direct3D 9 would refuse the draw; drawn without depth)
    }
    const VkExtent2D area{
        .width = depth_image ? std::min(colour_image->extent.width, depth_image->extent.width) : colour_image->extent.width,
        .height =
            depth_image ? std::min(colour_image->extent.height, depth_image->extent.height) : colour_image->extent.height};
    VkCommandBuffer commands = stream(state);
    Memory::transition(commands, colour_image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    if (depth_image != nullptr) {
        Memory::transition(commands, depth_image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true);
    }
    VkRenderingAttachmentInfo colour_info{};
    colour_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour_info.imageView = colour_image->attachment_view;
    colour_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour_info.resolveMode = VK_RESOLVE_MODE_NONE;
    colour_info.loadOp = clear != nullptr && clear->colour ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colour_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (clear != nullptr) {
        colour_info.clearValue.color = clear->colour_value;
    }
    VkRenderingAttachmentInfo depth_info{};
    depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (depth_image != nullptr) {
        depth_info.imageView = depth_image->attachment_view;
        depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_info.resolveMode = VK_RESOLVE_MODE_NONE;
        depth_info.loadOp = clear != nullptr && clear->depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (clear != nullptr) {
            depth_info.clearValue.depthStencil = {.depth = clear->z, .stencil = 0};
        }
    }
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {.offset = {.x = 0, .y = 0}, .extent = area};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour_info;
    info.pDepthAttachment = depth_image != nullptr ? &depth_info : nullptr;
    vkCmdBeginRendering(commands, &info);
    const uint64_t serial = state.frames.serial();
    colour_image->used = serial;
    if (depth_image != nullptr) {
        depth_image->used = serial;
    }
    pass.open = true;
    pass.colour = colour;
    pass.depth = depth;
    pass.colour_image = colour_image;
    pass.depth_image = depth_image;
    pass.area = area;
    ++state.stats.passes;
    return true;
}

// The viewport's rectangle within the pass's area.
VkRect2D viewport_rect(const GpuViewport& viewport, VkExtent2D area)
{
    const uint32_t left = std::min(viewport.x, area.width);
    const uint32_t top = std::min(viewport.y, area.height);
    return {.offset = {.x = static_cast<int32_t>(left), .y = static_cast<int32_t>(top)},
            .extent = {.width = std::min(viewport.width, area.width - left),
                       .height = std::min(viewport.height, area.height - top)}};
}

void set_dynamic_states(Backend& state, VkCommandBuffer commands, const GpuStateBlock& block, const DrawCall& call)
{
    BoundState& bound = state.bound;
    const bool first = !bound.dynamic_set;
    if (first) {
        vkCmdSetFrontFace(commands, VK_FRONT_FACE_CLOCKWISE);
    }
    const GpuViewport& from = block.viewport;
    const bool pre_transformed = fvf_pre_transformed(block.vertex_format);
    // (pre-transformed positions carry their own depth: Direct3D 9 doesn't scale it by the viewport's)
    const VkViewport viewport{.x = static_cast<float>(from.x),
                              .y = static_cast<float>(from.y),
                              .width = static_cast<float>(from.width),
                              .height = static_cast<float>(from.height),
                              .minDepth = pre_transformed ? 0.0F : std::clamp(from.min_z, 0.0F, 1.0F),
                              .maxDepth = pre_transformed ? 1.0F : std::clamp(from.max_z, 0.0F, 1.0F)};
    if (first || !same_bytes(viewport, bound.viewport)) {
        vkCmdSetViewport(commands, 0, 1, &viewport);
        bound.viewport = viewport;
    }
    const VkRect2D scissor{.offset = {.x = 0, .y = 0}, .extent = state.pass.area};
    if (first || !same_bytes(scissor, bound.scissor)) {
        vkCmdSetScissor(commands, 0, 1, &scissor);
        bound.scissor = scissor;
    }
    const VkCullModeFlags cull = cull_mode(block.render_state(GpuRenderState::kCullMode));
    if (first || cull != bound.cull) {
        vkCmdSetCullMode(commands, cull);
        bound.cull = cull;
    }
    const VkPrimitiveTopology primitive = topology(call.primitive);
    if (first || primitive != bound.topology) {
        vkCmdSetPrimitiveTopology(commands, primitive);
        bound.topology = primitive;
    }
    const bool has_depth = state.pass.depth_image != nullptr;
    const bool depth_test = has_depth && block.render_state(GpuRenderState::kZEnable) != 0;
    const bool depth_write = has_depth && block.render_state(GpuRenderState::kZWriteEnable) != 0;
    const VkCompareOp compare = compare_op(block.render_state(GpuRenderState::kZFunc));
    if (first || depth_test != bound.depth_test) {
        vkCmdSetDepthTestEnable(commands, depth_test ? VK_TRUE : VK_FALSE);
        bound.depth_test = depth_test;
    }
    if (first || depth_write != bound.depth_write) {
        vkCmdSetDepthWriteEnable(commands, depth_write ? VK_TRUE : VK_FALSE);
        bound.depth_write = depth_write;
    }
    if (first || compare != bound.depth_compare) {
        vkCmdSetDepthCompareOp(commands, compare);
        bound.depth_compare = compare;
    }
    const std::array<float, 2> bias{
        std::bit_cast<float>(block.render_state(GpuRenderState::kDepthBias)) * kDepthBiasSteps,
        std::bit_cast<float>(core::at(block.render_states, kSlopeScaleDepthBias))};
    if (first || bias != bound.depth_bias) {
        vkCmdSetDepthBias(commands, bias.front(), 0.0F, bias.back());
        bound.depth_bias = bias;
    }
    const Float4 constants = colour_of(block.render_state(GpuRenderState::kBlendFactor));
    if (first || constants != bound.blend_constants) {
        vkCmdSetBlendConstants(commands, constants.data());
        bound.blend_constants = constants;
    }
    bound.dynamic_set = true;
}

// The uniform blocks into the ring (when they differ from the last ones copied).
bool set_uniforms(Backend& state, const GpuStateBlock& block)
{
    BoundState& bound = state.bound;
    VertexUniforms vertex{};
    FragmentUniforms fragment{};
    fill_vertex_uniforms(block, &vertex);
    fill_fragment_uniforms(block, &fragment);
    const auto copy = [&state](const void* data, std::size_t bytes, VkDescriptorBufferInfo* where) {
        const std::span<const uint8_t> raw(static_cast<const uint8_t*>(data), bytes);
        const std::optional<RingSpan> span = state.ring.copy(raw, state.uniform_alignment);
        if (!span) {
            return false;
        }
        *where = {.buffer = span->buffer, .offset = span->offset, .range = bytes};
        return true;
    };
    if (!bound.uniforms_kept || !same_bytes(vertex, bound.vertex_uniforms)) {
        if (!copy(&vertex, sizeof(vertex), &bound.vertex_block)) {
            return false;
        }
        bound.vertex_uniforms = vertex;
    }
    if (!bound.uniforms_kept || !same_bytes(fragment, bound.fragment_uniforms)) {
        if (!copy(&fragment, sizeof(fragment), &bound.fragment_block)) {
            return false;
        }
        bound.fragment_uniforms = fragment;
    }
    bound.uniforms_kept = true;
    return true;
}

bool same_buffer_info(const VkDescriptorBufferInfo& a, const VkDescriptorBufferInfo& b)
{
    return a.buffer == b.buffer && a.offset == b.offset && a.range == b.range;
}

bool same_image_info(const VkDescriptorImageInfo& a, const VkDescriptorImageInfo& b)
{
    return a.sampler == b.sampler && a.imageView == b.imageView && a.imageLayout == b.imageLayout;
}

void push_descriptors(Backend& state, VkCommandBuffer commands, const std::array<VkDescriptorImageInfo, kStageCount>& images)
{
    BoundState& bound = state.bound;
    const bool same = bound.descriptors_set && same_buffer_info(bound.pushed_vertex_block, bound.vertex_block) &&
                      same_buffer_info(bound.pushed_fragment_block, bound.fragment_block) &&
                      std::ranges::equal(images, bound.images, same_image_info);
    if (same) {
        return;
    }
    std::array<VkWriteDescriptorSet, 2 + kStageCount> writes{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
    }
    core::at(writes, 0).dstBinding = kVertexUniformsBinding;
    core::at(writes, 0).descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    core::at(writes, 0).pBufferInfo = &bound.vertex_block;
    core::at(writes, 1).dstBinding = kFragmentUniformsBinding;
    core::at(writes, 1).descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    core::at(writes, 1).pBufferInfo = &bound.fragment_block;
    for (uint32_t stage = 0; stage < kStageCount; ++stage) {
        VkWriteDescriptorSet& write = core::at(writes, 2 + stage);
        write.dstBinding = kFirstTextureBinding + stage;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &core::at(images, stage);
    }
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipelines.layout(), 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    bound.images = images;
    bound.pushed_vertex_block = bound.vertex_block;
    bound.pushed_fragment_block = bound.fragment_block;
    bound.descriptors_set = true;
}

// A texture's image made ready to be sampled; null (the blank texture) when there is none, it was released while still
// set, or the open pass draws into it.
Image* image_to_sample(Backend& state, Texture* texture)
{
    if (texture == nullptr || !state.textures.contains(texture)) {
        return nullptr;
    }
    Image* image = &texture->image;
    if (image->layout != image->resting) {
        if (state.pass.open && (image == state.pass.colour_image || image == state.pass.depth_image)) {
            return nullptr; // (sampled while drawn into)
        }
        VkCommandBuffer commands = outside_pass(state);
        Memory::transition(commands, image, image->resting);
        image->used = state.frames.serial();
    }
    return image;
}

// The stages' textures a draw samples: each one's image (null: the blank texture), made ready to be sampled.
std::array<Image*, kStageCount> sampled_images(Backend& state, const GpuStateBlock& block, uint32_t sampled)
{
    std::array<Image*, kStageCount> images{};
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        if ((sampled & (1U << stage)) == 0U) {
            continue;
        }
        Texture* const texture = core::at(block.textures, stage) ? object(core::at(block.textures, stage)) : nullptr;
        core::at(images, stage) = image_to_sample(state, texture);
    }
    return images;
}

uint8_t topology_class(GpuPrimitive primitive)
{
    switch (primitive) {
    case GpuPrimitive::kPointList:
        return 0;
    case GpuPrimitive::kLineList:
    case GpuPrimitive::kLineStrip:
        return 1;
    default:
        return 2;
    }
}

PipelineKey pipeline_key(const Backend& state, const GpuStateBlock& block, const ShaderKey& shaders,
                         GpuPrimitive primitive)
{
    PipelineKey key;
    key.shaders = shaders;
    key.colour_format = static_cast<uint32_t>(state.pass.colour_image->format);
    key.depth_format =
        state.pass.depth_image != nullptr ? static_cast<uint32_t>(state.pass.depth_image->format) : VK_FORMAT_UNDEFINED;
    key.samples = static_cast<uint8_t>(state.pass.colour_image->samples);
    key.topology_class = topology_class(primitive);
    key.wireframe = block.render_state(GpuRenderState::kFillMode) == gpu_value::kFillWireframe &&
                            state.context.enabled_features().fillModeNonSolid == VK_TRUE
                        ? 1
                        : 0;
    key.blend = block.render_state(GpuRenderState::kAlphaBlendEnable) != 0 ? 1 : 0;
    if (key.blend != 0) {
        key.source_blend = static_cast<uint8_t>(block.render_state(GpuRenderState::kSrcBlend));
        key.dest_blend = static_cast<uint8_t>(block.render_state(GpuRenderState::kDestBlend));
        key.blend_operation = static_cast<uint8_t>(block.render_state(GpuRenderState::kBlendOp));
    }
    key.write_mask = static_cast<uint8_t>(block.render_state(GpuRenderState::kColourWriteEnable) & kWriteMaskBits);
    return key;
}

void log_stats(Backend& state)
{
    const PipelineStats& made = state.pipelines.stats();
    DrawStats& stats = state.stats;
    const double frames = static_cast<double>(std::max<uint64_t>(stats.frames, 1));
    get_log("vulkan: frame %llu: %u pipelines (%u refused), shaders %u compiled in %.1f s and %u from the disk cache, "
            "pipelines made in %.1f s; %.2f ms a frame (%.2f ms of it waiting for the device and the swapchain), %.0f draws and %.1f passes a frame over the last %llu; uploads "
            "%llu (%.1f MB) in %.2f s; %llu waits for the device",
            static_cast<unsigned long long>(state.frame), static_cast<unsigned>(made.pipelines),
            static_cast<unsigned>(made.refused), static_cast<unsigned>(made.shaders_compiled), made.compile_seconds,
            static_cast<unsigned>(made.shaders_from_disk), made.pipeline_seconds,
            stats.frame_seconds * kMilliseconds / frames, stats.wait_seconds * kMilliseconds / frames,
            static_cast<double>(stats.draws) / frames,
            static_cast<double>(stats.passes) / frames, static_cast<unsigned long long>(stats.frames),
            static_cast<unsigned long long>(stats.uploads), static_cast<double>(stats.upload_bytes) / kMegabyte,
            stats.upload_seconds, static_cast<unsigned long long>(stats.finishes));
    const double last = stats.last_present;
    stats = DrawStats{};
    stats.last_present = last;
    stats.last_log = last;
}

// --- the gamma ramp (Gpu::set_gamma_ramp) ---------------------------------------------------------------------------
// Direct3D 9 gives a swap chain's ramp to the display hardware, full screen only (D3DCAPS2_FULLSCREENGAMMA; in a window
// it is ignored without an error); the hardware looks each channel's 8-bit value up in its 256 entries and shows the
// entry's top 8 bits. Vulkan has no ramp: in full screen the frame goes through a pass that does the same lookup before
// it is copied into the swapchain's image - skipped when the ramp leaves every value as it is (the game's default).

// A full-screen pass's fragment shader: each pixel of t0 (the frame) looked up in the ramp's entries (the fragment
// uniform block's place, as 256 vec4s: red, green, blue, unused - each entry's top 8 bits as 0 - 1).
constexpr const char* kGammaFragment = R"(#version 450
layout(set = 0, binding = 1, std140) uniform Ramp { vec4 entry[256]; } ramp;
layout(set = 0, binding = 2) uniform sampler2D t0;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_colour;
void main()
{
    vec4 texel = texelFetch(t0, ivec2(gl_FragCoord.xy), 0);
    ivec3 at = ivec3(clamp(floor(texel.rgb * 255.0 + 0.5), 0.0, 255.0));
    o_colour = vec4(ramp.entry[at.r].r, ramp.entry[at.g].g, ramp.entry[at.b].b, texel.a);
}
)";

constexpr std::size_t kGammaEntries = kGpuGammaSteps;
constexpr uint32_t kRampTopByteShift = 8;
constexpr float kRampByteScale = 255.0F;

// The ramp's entries as the pass reads them; none when the ramp changes nothing.
std::optional<std::array<Float4, kGammaEntries>> gamma_entries(const GpuGammaRamp& ramp)
{
    std::array<Float4, kGammaEntries> entries{};
    bool identity = true;
    for (std::size_t index = 0; index < kGammaEntries; ++index) {
        Float4& entry = core::at(entries, index);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const uint32_t top = static_cast<uint32_t>(core::at(core::at(ramp, channel), index)) >> kRampTopByteShift;
            identity = identity && top == index;
            core::at(entry, channel) = static_cast<float>(top) / kRampByteScale;
        }
        core::at(entry, 3) = 1.0F;
    }
    if (identity) {
        return std::nullopt;
    }
    return entries;
}

// An image of the frame's size and format made (again, when either changed) for the gamma pass.
bool ensure_gamma_image(Backend& state, Image* image, const Image& like, VkImageUsageFlags usage)
{
    if (image->image != VK_NULL_HANDLE && image->extent.width == like.extent.width &&
        image->extent.height == like.extent.height && image->format == like.format) {
        return true;
    }
    retire_image(state, image);
    const ImageRequest request{.format = like.format,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = like.extent,
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = usage,
                               .alpha_one = false};
    return state.memory.create_image(request, image);
}

} // namespace

bool gamma_ramp_applies(const Backend& state) { return !state.settings.windowed && gamma_entries(state.gamma).has_value(); }

Image* record_gamma_pass(Backend& state, Image* frame)
{
    const std::optional<std::array<Float4, kGammaEntries>> entries = gamma_entries(state.gamma);
    if (!entries) {
        return nullptr;
    }
    if (!ensure_gamma_image(state, &state.gamma_source, *frame,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !ensure_gamma_image(state, &state.gamma_target, *frame,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        return nullptr;
    }
    std::string error;
    const VkPipeline pipeline = state.pipelines.screen_pass(frame->format, kGammaFragment, &error);
    const void* const bytes = entries->data();
    const std::span<const uint8_t> raw(static_cast<const uint8_t*>(bytes), sizeof(*entries));
    const std::optional<RingSpan> block = state.ring.copy(raw, state.uniform_alignment);
    if (pipeline == VK_NULL_HANDLE || !block) {
        if (first_report(state, kReportGamma)) {
            get_log("vulkan: no gamma pass (%s) - the frame shown without the ramp", error.c_str());
        }
        return nullptr;
    }
    VkCommandBuffer commands = outside_pass(state);
    // the frame copied into an image the pass can sample
    Memory::transition(commands, frame, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Memory::transition(commands, &state.gamma_source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    const VkImageCopy copy{.srcSubresource = layers,
                           .srcOffset = {},
                           .dstSubresource = layers,
                           .dstOffset = {},
                           .extent = {.width = frame->extent.width, .height = frame->extent.height, .depth = 1}};
    vkCmdCopyImage(commands, frame->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, state.gamma_source.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    Memory::transition(commands, frame, frame->resting);
    Memory::transition(commands, &state.gamma_source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // the pass: one triangle over the target
    Memory::transition(commands, &state.gamma_target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    VkRenderingAttachmentInfo colour{};
    colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour.imageView = state.gamma_target.attachment_view;
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.resolveMode = VK_RESOLVE_MODE_NONE;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkExtent2D area{.width = frame->extent.width, .height = frame->extent.height};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {.offset = {.x = 0, .y = 0}, .extent = area};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour;
    vkCmdBeginRendering(commands, &info);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = 0.0F,
                              .y = 0.0F,
                              .width = static_cast<float>(area.width),
                              .height = static_cast<float>(area.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    const VkRect2D scissor{.offset = {.x = 0, .y = 0}, .extent = area};
    vkCmdSetScissor(commands, 0, 1, &scissor);
    const VkDescriptorBufferInfo ramp{.buffer = block->buffer, .offset = block->offset, .range = raw.size()};
    const VkDescriptorImageInfo image{.sampler = state.samplers.sampler(SamplerKey{}),
                                      .imageView = state.gamma_source.view,
                                      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
    }
    writes.front().dstBinding = kFragmentUniformsBinding;
    writes.front().descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes.front().pBufferInfo = &ramp;
    writes.back().dstBinding = kFirstTextureBinding;
    writes.back().descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes.back().pImageInfo = &image;
    vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipelines.layout(), 0,
                              static_cast<uint32_t>(writes.size()), writes.data());
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    state.bound = BoundState{}; // (the pipeline, descriptors and dynamic states the draws had are gone)
    const uint64_t serial = state.frames.serial();
    frame->used = serial;
    state.gamma_source.used = serial;
    state.gamma_target.used = serial;
    return &state.gamma_target;
}

namespace {

// The back buffer copied into the swapchain's image `index` (resolved first when it is multisampled).
void record_present_copy(Backend& state, uint32_t index)
{
    VkCommandBuffer commands = stream(state);
    Image* source = &state.back_buffer->image;
    if (source->samples != VK_SAMPLE_COUNT_1_BIT) {
        Image& resolved = state.present_source;
        if (resolved.image == VK_NULL_HANDLE || resolved.extent.width != source->extent.width ||
            resolved.extent.height != source->extent.height || resolved.format != source->format) {
            retire_image(state, &resolved);
            const ImageRequest request{.format = source->format,
                                       .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .extent = source->extent,
                                       .levels = 1,
                                       .samples = VK_SAMPLE_COUNT_1_BIT,
                                       .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                       .alpha_one = false};
            if (!state.memory.create_image(request, &resolved)) {
                return;
            }
        }
        Memory::record_resolve(commands, source, &resolved);
        source = &resolved;
    }
    if (!state.settings.windowed) {
        if (Image* const corrected = record_gamma_pass(state, source)) {
            source = corrected; // (full screen: through the gamma ramp)
        }
    }
    const VkImage target = state.context.swapchain_image(index);
    const VkExtent2D size = state.context.extent();
    Memory::transition(commands, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    const VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
    VkImageMemoryBarrier to_copy{};
    to_copy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_copy.srcAccessMask = 0;
    to_copy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_copy.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_copy.image = target;
    to_copy.subresourceRange = range;
    // (the acquire's semaphore is waited for at the transfer stage)
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &to_copy);
    const VkImageSubresourceLayers layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    VkImageBlit region{};
    region.srcSubresource = layers;
    region.srcOffsets[1] = {.x = static_cast<int32_t>(source->extent.width),
                            .y = static_cast<int32_t>(source->extent.height), .z = 1};
    region.dstSubresource = layers;
    region.dstOffsets[1] = {.x = static_cast<int32_t>(size.width), .y = static_cast<int32_t>(size.height), .z = 1};
    vkCmdBlitImage(commands, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
    VkImageMemoryBarrier to_present = to_copy;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &to_present);
    Memory::transition(commands, source, source->resting);
    source->used = state.frames.serial();
}

} // namespace

bool create_draw_objects(Backend& state)
{
    if (!state.context.draws_supported()) {
        get_log("vulkan: the device has no dynamic rendering or push descriptors - it can't draw");
        return false;
    }
    if (!state.staging.init(&state.memory, VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
        !state.frames.init(state.context, &state.memory, {.draws = &state.ring, .staging = &state.staging}) ||
        !state.pipelines.init(state.context.device(), state.context.physical_device(), state.cache_folder)) {
        return false;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.context.physical_device(), &properties);
    state.uniform_alignment = std::max<VkDeviceSize>(properties.limits.minUniformBufferOffsetAlignment, kLeastUniformAlignment);
    state.draws_made = true;
    // the blank texture: one opaque white texel
    const ImageRequest request{.format = VK_FORMAT_B8G8R8A8_UNORM,
                               .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                               .extent = {.width = 1, .height = 1},
                               .levels = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &state.blank)) {
        return false;
    }
    const std::optional<RingSpan> staged = state.staging.copy(kBlankTexel, kStagingAlignment);
    if (!staged) {
        return false;
    }
    Memory::record_upload_level(state.frames.uploads(), &state.blank, 0,
                                {.buffer = staged->buffer, .offset = staged->offset});
    state.stats.last_present = seconds_now();
    state.stats.last_log = state.stats.last_present;
    get_log("vulkan: drawing on %s (dynamic rendering, push descriptors; shader and pipeline caches %s)",
            std::data(properties.deviceName), state.cache_folder.empty() ? "not kept" : "kept beside the exe");
    if (state.context.ray_tracing_enabled()) {
        state.materials.init(state); // NOT ORIGINAL - ray tracing's material table (logged when it can't be made)
    }
    return true;
}

void release_draw_objects(Backend& state)
{
    if (!state.memory.ready()) {
        return;
    }
    state.frames.shutdown(); // (everything waited for, the retired objects destroyed)
    state.materials.shutdown(state);
    state.pipelines.shutdown();
    release_rt_shadows(state); // (NOT ORIGINAL - ray-traced shadows' pipelines: vulkan_rt_shadows.h)
    release_rt_effects(state); // (NOT ORIGINAL - ray-traced ambient occlusion's: vulkan_rt_effects.h)
    release_rt_reflections(state); // (NOT ORIGINAL - ray-traced reflections': vulkan_rt_reflections.h)
    release_hdr(state); // (NOT ORIGINAL - HDR's tone mapping: vulkan_hdr.h)
    release_rt_fog(state); // (NOT ORIGINAL - the volumetric fog's: vulkan_rt_fog.h)
    state.memory.destroy_image(&state.blank);
    state.memory.destroy_image(&state.present_source);
    state.memory.destroy_image(&state.gamma_source);
    state.memory.destroy_image(&state.gamma_target);
    state.staging.shutdown();
    state.pass = OpenPass{};
    state.bound = BoundState{};
    state.draws_made = false;
}

VkCommandBuffer outside_pass(Backend& state)
{
    end_pass(state);
    return stream(state);
}

void prepare_image(Backend& state, Image* image)
{
    if (state.draws_made && image->image != VK_NULL_HANDLE) {
        Memory::transition(state.frames.uploads(), image, image->resting);
    }
}

TransferTarget transfer_commands(Backend& state, uint64_t touched)
{
    if (touched < state.frames.serial()) {
        return {.commands = state.frames.uploads(), .in_stream = false};
    }
    VkCommandBuffer commands = outside_pass(state);
    // the stream's reads before it of the buffers it may overwrite, and the copies before it into them (images have
    // their own barriers)
    memory_barrier(commands, kBufferReaders | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    return {.commands = commands, .in_stream = true};
}

void copy_after_copy(VkCommandBuffer commands)
{
    memory_barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT);
}

void transfer_done(Backend& state, const TransferTarget& target, VkDeviceSize staged)
{
    if (target.in_stream) {
        memory_barrier(target.commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kBufferReaders,
                       kBufferReads);
    }
    ++state.stats.uploads;
    state.stats.upload_bytes += staged;
    if (state.staging.used() > kStagingLimit) {
        finish_work(state);
    }
}

void retire_image(Backend& state, Image* image)
{
    if (image->image == VK_NULL_HANDLE) {
        return;
    }
    if (state.draws_made) {
        state.frames.retire(*image);
        *image = Image{};
    } else {
        state.memory.destroy_image(image);
    }
}

void retire_buffer(Backend& state, DeviceBuffer* buffer)
{
    if (buffer->buffer == VK_NULL_HANDLE) {
        return;
    }
    if (state.draws_made) {
        state.frames.retire(*buffer);
        *buffer = DeviceBuffer{};
    } else {
        state.memory.destroy_buffer(buffer);
    }
}

bool finish_work(Backend& state)
{
    if (!state.draws_made) {
        return true;
    }
    end_pass(state);
    ++state.stats.finishes;
    const bool done = state.frames.finish();
    state.bound = BoundState{};
    return done;
}

long record_draw(Backend& state, const GpuStateBlock& block, const DrawCall& call)
{
    if (!state.draws_made || call.vertices == 0 || block.viewport.width == 0 || block.viewport.height == 0) {
        return kOk;
    }
    const ShaderProgram* program = nullptr;
    if (block.pixel_shader != nullptr) {
        const PixelShader* const shader = object(block.pixel_shader);
        if (state.shaders.contains(shader)) {
            program = &shader->program;
        }
    }
    const ShaderKey shaders = draw_shader_key(block, program, call.primitive == GpuPrimitive::kPointList);
    std::string why;
    if (!supported(shaders, &why)) {
        if (first_report(state, hash_of(shaders.vertex) ^ hash_of(shaders.fragment))) {
            get_log("vulkan: a draw the fixed-function emulation can't make (%s) - skipped: %s / %s", why.c_str(),
                    describe(shaders.vertex).c_str(), describe(shaders.fragment).c_str());
        }
        return kOk;
    }
    const VertexLayout layout = vertex_layout(block.vertex_format);
    if (call.stride < layout.bytes) {
        return kOk; // (the stream's stride shorter than its format: nothing sensible to draw)
    }
    // The pass on other targets ended first: what it drew into can be sampled now (the screen effects draw from the
    // target before into the next one - kept open, its attachment would be taken for one sampled while drawn into).
    if (state.pass.open && (state.pass.colour != core::at(state.targets, 0) || state.pass.depth != state.depth)) {
        end_pass(state);
    }
    const std::array<Image*, kStageCount> images = sampled_images(state, block, sampled_stages(shaders.fragment));
    if (!ensure_pass(state, nullptr)) {
        return kOk;
    }
    const PipelineKey key = pipeline_key(state, block, shaders, call.primitive);
    std::string error;
    const VkPipeline pipeline = state.pipelines.pipeline(key, &error);
    if (pipeline == VK_NULL_HANDLE) {
        if (first_report(state, PipelineKeyHash{}(key))) {
            get_log("vulkan: no pipeline for a draw (%s) - skipped: %s / %s", error.c_str(),
                    describe(shaders.vertex).c_str(), describe(shaders.fragment).c_str());
        }
        return kOk;
    }
    std::array<VkDescriptorImageInfo, kStageCount> descriptors{};
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
        const Image* const image = core::at(images, stage);
        const VkSampler sampler = state.samplers.sampler(image != nullptr ? sampler_key(block, static_cast<uint32_t>(stage))
                                                                          : SamplerKey{});
        if (sampler == VK_NULL_HANDLE) {
            return kOk;
        }
        const Image& shown = image != nullptr ? *image : state.blank;
        core::at(descriptors, stage) = {.sampler = sampler, .imageView = shown.view, .imageLayout = shown.layout};
    }
    VkCommandBuffer commands = stream(state);
    if (!set_uniforms(state, block)) {
        return gpu_value::kResultInvalidCall;
    }
    BoundState& bound = state.bound;
    if (pipeline != bound.pipeline) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        bound.pipeline = pipeline;
    }
    set_dynamic_states(state, commands, block, call);
    push_descriptors(state, commands, descriptors);
    if (call.vertex_buffer != bound.vertex_buffer || call.vertex_offset != bound.vertex_offset ||
        call.stride != bound.vertex_stride) {
        const VkDeviceSize stride = call.stride;
        vkCmdBindVertexBuffers2(commands, 0, 1, &call.vertex_buffer, &call.vertex_offset, nullptr, &stride);
        bound.vertex_buffer = call.vertex_buffer;
        bound.vertex_offset = call.vertex_offset;
        bound.vertex_stride = stride;
    }
    if (call.indexed) {
        if (call.index_buffer != bound.index_buffer || call.index_offset != bound.index_offset ||
            call.index_type != bound.index_type) {
            vkCmdBindIndexBuffer(commands, call.index_buffer, call.index_offset, call.index_type);
            bound.index_buffer = call.index_buffer;
            bound.index_offset = call.index_offset;
            bound.index_type = call.index_type;
        }
        vkCmdDrawIndexed(commands, call.vertices, 1, call.first_index, call.base_vertex, 0);
    } else {
        vkCmdDraw(commands, call.vertices, 1, call.first_vertex, 0);
    }
    const uint64_t serial = state.frames.serial();
    for (Image* image : images) {
        if (image != nullptr) {
            image->used = serial;
        }
    }
    ++state.stats.draws;
    return kOk;
}

long record_clear(Backend& state, const GpuStateBlock& block, std::span<const GpuRect> rects, const ClearRequest& request)
{
    const uint32_t flags = request.flags;
    const uint32_t colour = request.colour;
    const float z = request.z;
    if (!state.draws_made) {
        return kOk;
    }
    const Image* const target = attachment_of(core::at(state.targets, 0));
    if (target == nullptr) {
        return gpu_value::kResultInvalidCall;
    }
    const Float4 rgba = colour_of(colour);
    PassClear clear;
    clear.colour = (flags & gpu_value::kClearTarget) != 0;
    clear.depth = (flags & gpu_value::kClearDepth) != 0;
    clear.z = z;
    std::ranges::copy(rgba, std::begin(clear.colour_value.float32));
    // a clear of the whole of the targets with no pass open on them: the pass opens clearing them
    const bool on_targets =
        state.pass.open && state.pass.colour == core::at(state.targets, 0) && state.pass.depth == state.depth;
    const VkExtent2D whole{.width = target->extent.width, .height = target->extent.height};
    const VkRect2D area = viewport_rect(block.viewport, whole);
    const bool everything = rects.empty() && area.offset.x == 0 && area.offset.y == 0 &&
                            area.extent.width == whole.width && area.extent.height == whole.height;
    if (!on_targets && everything) {
        const bool opened = ensure_pass(state, &clear);
        const bool covered = opened && state.pass.area.width == whole.width && state.pass.area.height == whole.height;
        if (covered) {
            return kOk;
        }
    }
    if (!ensure_pass(state, nullptr)) {
        return kOk;
    }
    std::array<VkClearAttachment, 2> attachments{};
    uint32_t count = 0;
    if (clear.colour) {
        core::at(attachments, count++) = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .colorAttachment = 0, .clearValue = {.color = clear.colour_value}};
    }
    if (clear.depth && state.pass.depth_image != nullptr) {
        VkClearAttachment depth{};
        depth.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth.clearValue.depthStencil = {.depth = z, .stencil = 0};
        core::at(attachments, count++) = depth;
    }
    if (count == 0) {
        return kOk;
    }
    // the viewport (Direct3D 9 clears within it), or each rectangle within it
    const VkRect2D inside = viewport_rect(block.viewport, state.pass.area);
    std::array<VkClearRect, kMostClearRects> areas{};
    uint32_t area_count = 0;
    if (rects.empty()) {
        core::at(areas, area_count++) = {.rect = inside, .baseArrayLayer = 0, .layerCount = 1};
    } else {
        for (const GpuRect& rect : rects) {
            const int32_t left = std::max(rect.left, inside.offset.x);
            const int32_t top = std::max(rect.top, inside.offset.y);
            const int32_t right = std::min(rect.right, inside.offset.x + static_cast<int32_t>(inside.extent.width));
            const int32_t bottom = std::min(rect.bottom, inside.offset.y + static_cast<int32_t>(inside.extent.height));
            if (right > left && bottom > top && area_count < areas.size()) {
                core::at(areas, area_count++) = {
                    .rect = {.offset = {.x = left, .y = top},
                             .extent = {.width = static_cast<uint32_t>(right - left),
                                        .height = static_cast<uint32_t>(bottom - top)}},
                    .baseArrayLayer = 0,
                    .layerCount = 1};
            }
        }
    }
    const bool any = std::ranges::any_of(std::span(areas).first(area_count), [](const VkClearRect& one) {
        return one.rect.extent.width != 0 && one.rect.extent.height != 0;
    });
    if (any) {
        vkCmdClearAttachments(stream(state), count, attachments.data(), area_count, areas.data());
    }
    return kOk;
}

long record_present(Backend& state)
{
    ++state.frame;
    if (!state.draws_made || state.back_buffer == nullptr) {
        return kOk;
    }
    end_pass(state);
    Context& context = state.context;
    if (context.surface_changed()) {
        context.window_resized();
    }
    if (context.swapchain_stale()) {
        finish_work(state);
        context.remake();
        const VkExtent2D made = context.swapchain_stale() ? VkExtent2D{} : context.extent();
        if (made.width != state.shown_extent.width || made.height != state.shown_extent.height) {
            get_log("vulkan: the swapchain made again: %ux%u%s", made.width, made.height,
                    made.width == 0 ? " (minimised: nothing shown until it has a size)" : "");
            state.shown_extent = made;
        }
    }
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore presented = VK_NULL_HANDLE;
    uint32_t index = 0;
    if (!context.swapchain_stale()) {
        const double before_acquire = seconds_now();
        const VkResult result = context.acquire(state.frames.acquire_semaphore(), &index);
        state.stats.wait_seconds += seconds_now() - before_acquire;
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
            acquired = state.frames.acquire_semaphore();
            presented = context.present_semaphore(index);
            record_present_copy(state, index);
        } else {
            context.window_resized(); // (out of date: made again before the next frame)
        }
    }
    const double before_end = seconds_now();
    const bool ended = state.frames.end_frame(acquired, presented);
    state.stats.wait_seconds += seconds_now() - before_end;
    state.bound = BoundState{};
    if (presented != VK_NULL_HANDLE) {
        context.queue_present(index);
    }
    present_ui_windows(state); // (the UI's own windows drawn this frame - vulkan_ui_windows.cpp)
    DrawStats& stats = state.stats;
    const double now = seconds_now();
    stats.frame_seconds += now - stats.last_present;
    stats.last_present = now;
    ++stats.frames;
    if (now - stats.last_log >= kStatsSeconds) {
        log_stats(state);
        state.pipelines.save(); // (when something new was made: the game may end without shutting the device down)
    }
    return ended ? kOk : gpu_value::kResultInvalidCall;
}

void record_begin_query(Backend& state, const GpuStateBlock& /*block*/, Query* query)
{
    query->recorded = false;
    if (!state.draws_made) {
        return;
    }
    const uint64_t serial = state.frames.serial();
    uint64_t& used = core::at(state.query_slot_used, query->index);
    if (!query->fresh) {
        if (used >= serial) {
            return; // (its slot already used in this submission: no reset can go before it - 0 pixels)
        }
        vkCmdResetQueryPool(state.frames.uploads(), query->pool, query->slot, 1);
        query->fresh = true;
    }
    if (!ensure_pass(state, nullptr)) {
        return;
    }
    vkCmdBeginQuery(stream(state), query->pool, query->slot, 0);
    used = serial;
    query->fresh = false;
    query->recorded = true;
    state.pass.queries.push_back(query);
}

void record_end_query(Backend& state, Query* query)
{
    std::vector<Query*>& open = state.pass.queries;
    const auto found = std::ranges::find(open, query);
    if (found != open.end()) {
        end_query_in_pass(state.frames.stream(), state, query);
        open.erase(found);
    }
}

namespace {

// What a UI batch is drawn into: the open pass's colour (and depth) formats, its samples and area.
struct UiTarget {
    VkFormat colour = VK_FORMAT_UNDEFINED;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    uint32_t samples = 1;
    VkExtent2D area{};
};

// The batch's draws recorded in the pass open in the stream (its textures' images in `images`, one per draw, made
// ready to be sampled before the pass opened); the stream's bound state forgotten after.
void draw_ui_batch(Backend& state, const UiBatch& batch, const std::vector<Image*>& images, const UiTarget& target)
{
    const float width = core::at(batch.display, 2);
    const float height = core::at(batch.display, 3);
    std::string error;
    const VkPipeline pipeline = state.pipelines.ui_pass(target.colour, target.depth, target.samples, &error);
    const SamplerKey linear{.address_u = gpu_value::kAddressClamp,
                            .address_v = gpu_value::kAddressClamp,
                            .address_w = gpu_value::kAddressClamp,
                            .mag_filter = gpu_value::kFilterLinear,
                            .min_filter = gpu_value::kFilterLinear};
    const VkSampler sampler = state.samplers.sampler(linear);
    // the display's rectangle to the target's -1 - 1: x' = x * 2 / width - 1 - left * 2 / width (and y the same)
    const float left = core::at(batch.display, 0);
    const float top = core::at(batch.display, 1);
    const std::array<float, 4> transform{2.0F / width, 2.0F / height, -1.0F - (left * 2.0F / width),
                                         -1.0F - (top * 2.0F / height)};
    const void* const transform_bytes = transform.data();
    const void* const index_bytes = batch.indices.data();
    const std::optional<RingSpan> vertices = state.ring.copy(batch.vertices, kLeastUniformAlignment);
    const std::optional<RingSpan> indices =
        state.ring.copy(std::span(static_cast<const uint8_t*>(index_bytes), batch.indices.size_bytes()),
                        kLeastUniformAlignment);
    const std::optional<RingSpan> block =
        state.ring.copy(std::span(static_cast<const uint8_t*>(transform_bytes), sizeof(transform)), state.uniform_alignment);
    if (pipeline == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE || !vertices || !indices || !block) {
        if (first_report(state, kReportUi)) {
            get_log("vulkan: the UI not drawn (%s)", error.empty() ? "no room for its vertices" : error.c_str());
        }
        return;
    }
    VkCommandBuffer commands = stream(state);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkViewport viewport{.x = 0.0F,
                              .y = 0.0F,
                              .width = static_cast<float>(target.area.width),
                              .height = static_cast<float>(target.area.height),
                              .minDepth = 0.0F,
                              .maxDepth = 1.0F};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdBindVertexBuffers(commands, 0, 1, &vertices->buffer, &vertices->offset);
    vkCmdBindIndexBuffer(commands, indices->buffer, indices->offset, VK_INDEX_TYPE_UINT16);
    const VkDescriptorBufferInfo uniforms{.buffer = block->buffer, .offset = block->offset, .range = sizeof(transform)};
    const Image* pushed = nullptr;
    bool any_pushed = false;
    const auto area_width = static_cast<int32_t>(target.area.width);
    const auto area_height = static_cast<int32_t>(target.area.height);
    const uint64_t serial = state.frames.serial();
    for (std::size_t at = 0; at < batch.draws.size(); ++at) {
        const UiDraw& draw = core::at(batch.draws, at);
        const GpuRect clip{.left = std::max(draw.clip.left, 0),
                           .top = std::max(draw.clip.top, 0),
                           .right = std::min(draw.clip.right, area_width),
                           .bottom = std::min(draw.clip.bottom, area_height)};
        if (draw.indices == 0 || clip.right <= clip.left || clip.bottom <= clip.top) {
            continue;
        }
        Image* const image = core::at(images, at);
        if (!any_pushed || image != pushed) {
            const Image& shown = image != nullptr ? *image : state.blank;
            const VkDescriptorImageInfo texture{.sampler = sampler, .imageView = shown.view, .imageLayout = shown.layout};
            std::array<VkWriteDescriptorSet, 2> writes{};
            for (VkWriteDescriptorSet& write : writes) {
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.descriptorCount = 1;
            }
            writes.front().dstBinding = kVertexUniformsBinding;
            writes.front().descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes.front().pBufferInfo = &uniforms;
            writes.back().dstBinding = kFirstTextureBinding;
            writes.back().descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes.back().pImageInfo = &texture;
            vkCmdPushDescriptorSetKHR(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipelines.layout(), 0,
                                      static_cast<uint32_t>(writes.size()), writes.data());
            pushed = image;
            any_pushed = true;
        }
        const VkRect2D scissor{.offset = {.x = clip.left, .y = clip.top},
                               .extent = {.width = static_cast<uint32_t>(clip.right - clip.left),
                                          .height = static_cast<uint32_t>(clip.bottom - clip.top)}};
        vkCmdSetScissor(commands, 0, 1, &scissor);
        vkCmdDrawIndexed(commands, draw.indices, 1, draw.first_index, draw.base_vertex, 0);
        if (image != nullptr) {
            image->used = serial;
        }
        ++state.stats.draws;
    }
    state.bound = BoundState{}; // (the draws' pipeline, descriptors, buffers and dynamic states are gone)
}

// Whether a batch has anything to draw.
bool batch_drawn(const Backend& state, const UiBatch& batch)
{
    return state.draws_made && !batch.draws.empty() && !batch.vertices.empty() && !batch.indices.empty() &&
           core::at(batch.display, 2) > 0.0F && core::at(batch.display, 3) > 0.0F;
}

// The images a batch's draws sample, made ready to be sampled (outside the pass).
std::vector<Image*> batch_images(Backend& state, const UiBatch& batch)
{
    std::vector<Image*> images;
    images.reserve(batch.draws.size());
    for (const UiDraw& draw : batch.draws) {
        images.push_back(image_to_sample(state, draw.texture));
    }
    return images;
}

// A swapchain image's layout change, the stages and accesses either side.
struct ImageMove {
    VkImageLayout from = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout to = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags after = 0;
    VkAccessFlags written = 0;
    VkPipelineStageFlags before = 0;
    VkAccessFlags read = 0;
};

void move_swapchain_image(VkCommandBuffer commands, VkImage image, const ImageMove& move)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = move.written;
    barrier.dstAccessMask = move.read;
    barrier.oldLayout = move.from;
    barrier.newLayout = move.to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
    vkCmdPipelineBarrier(commands, move.after, move.before, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

long record_ui_draws(Backend& state, const UiBatch& batch)
{
    if (!batch_drawn(state, batch)) {
        return kOk;
    }
    // (as a draw: the pass on other targets ended first, so what it drew can be sampled)
    if (state.pass.open && (state.pass.colour != core::at(state.targets, 0) || state.pass.depth != state.depth)) {
        end_pass(state);
    }
    const std::vector<Image*> images = batch_images(state, batch);
    if (!ensure_pass(state, nullptr)) {
        return kOk;
    }
    const OpenPass& pass = state.pass;
    draw_ui_batch(state, batch, images,
                  {.colour = pass.colour_image->format,
                   .depth = pass.depth_image != nullptr ? pass.depth_image->format : VK_FORMAT_UNDEFINED,
                   .samples = static_cast<uint32_t>(pass.colour_image->samples),
                   .area = pass.area});
    return kOk;
}

long record_ui_window_draws(Backend& state, UiWindow* window, const UiBatch& batch)
{
    if (!state.draws_made || window == nullptr || !window->pending) {
        return kOk;
    }
    end_pass(state); // (the game's pass ended: what it drew - the Scene panel's picture - can be sampled)
    const bool drawn = batch_drawn(state, batch);
    const std::vector<Image*> images = drawn ? batch_images(state, batch) : std::vector<Image*>{};
    VkCommandBuffer commands = stream(state);
    const VkImage image = core::at(window->images, window->image);
    // (the acquire's semaphore is waited for at the colour output stage)
    move_swapchain_image(commands, image,
                         {.from = VK_IMAGE_LAYOUT_UNDEFINED,
                          .to = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          .after = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          .written = 0,
                          .before = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          .read = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});
    VkRenderingAttachmentInfo colour{};
    colour.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colour.imageView = core::at(window->views, window->image);
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.resolveMode = VK_RESOLVE_MODE_NONE;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colour.clearValue.color = {.float32 = {0.0F, 0.0F, 0.0F, 1.0F}};
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {.offset = {.x = 0, .y = 0}, .extent = window->extent};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour;
    vkCmdBeginRendering(commands, &info);
    if (drawn) {
        draw_ui_batch(state, batch, images, {.colour = window->format, .samples = 1, .area = window->extent});
    }
    commands = stream(state);
    vkCmdEndRendering(commands);
    move_swapchain_image(commands, image,
                         {.from = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          .to = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          .after = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          .written = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          .before = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                          .read = 0});
    state.bound = BoundState{};
    ++state.stats.passes;
    state.frames.add_wait({.semaphore = core::at(window->acquired, state.frames.slot_index()),
                           .stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT});
    state.frames.add_signal(core::at(window->presented, window->image));
    return kOk;
}

} // namespace render::vulkan
