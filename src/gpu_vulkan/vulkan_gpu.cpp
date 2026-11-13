// NOT ORIGINAL - Vulkan's render::Gpu (gpu_vulkan.h): the resources, made on the device the context opened
// (vulkan_gpu_context.cpp), and the draws, clears and presents recorded into the frame (vulkan_draws.cpp). Every
// operation takes the backend's lock (vulkan_backend.h). The state block is the interface's (engine/gfx/gpu.h): the
// setters' send_* only answer here - the draws read the block.

#include "gpu_vulkan/gpu_vulkan.h"

#include <SDL3/SDL_video.h>
#include <algorithm>
#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_jpeg.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_hdr.h"
#include "gpu_vulkan/vulkan_rt_effects.h"
#include "gpu_vulkan/vulkan_rt_fog.h"
#include "gpu_vulkan/vulkan_rt_reflections.h"
#include "gpu_vulkan/vulkan_rt_shadows.h"
#include "gpu_vulkan/vulkan_rt_sky_pass.h"
#include "gpu_vulkan/vulkan_rt_sun.h"
#include "gpu_vulkan/vulkan_upscale.h"
#include "gpu_vulkan/vulkan_pixel_program.h"
#include "gpu_vulkan/vulkan_rt_scene.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_samplers.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr long kOk = gpu_value::kResultOk;
constexpr long kInvalid = gpu_value::kResultInvalidCall;
// D3DUSAGE_DEPTHSTENCIL (a texture that is a depth surface)
constexpr uint32_t kUsageDepthStencil = 0x2;
// The block's transforms: world, view, projection, then the stages' (engine/gfx/gpu.cpp's order).
constexpr std::size_t kWorldSlot = 0;
constexpr std::size_t kViewSlot = 1;
constexpr std::size_t kProjectionSlot = 2;
constexpr std::size_t kFirstStageSlot = 3;
constexpr uint32_t kIndex32Format = 102; // D3DFMT_INDEX32
constexpr uint32_t kIndex16Bytes = 2;
constexpr uint32_t kIndex32Bytes = 4;
constexpr VkDeviceSize kRingAlignment = 16;
constexpr VkDeviceSize kStagingAlignment = 16; // (a multiple of every format's texel or block size)

double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// --- objects and their references --------------------------------------------------------------------------------

// (the images and buffers destroyed once the frames that may use them have finished: vulkan_frames.h)
void destroy_texture(Backend& state, Texture* texture)
{
    state.materials.texture_released(state, texture); // NOT ORIGINAL - its ray-tracing texture slot, if it has one
    retire_image(state, &texture->image);
    state.textures.erase(texture);
}

void destroy_surface(Backend& state, Surface* surface)
{
    retire_image(state, &surface->image);
    state.surfaces.erase(surface);
}

void destroy_buffer(Backend& state, Buffer* buffer)
{
    for (BufferVersion& version : buffer->versions) {
        retire_buffer(state, &version.memory);
    }
    state.buffers.erase(buffer);
}

uint32_t release(Backend& state, Texture* texture)
{
    if (texture == nullptr || texture->refs == 0) {
        return 0;
    }
    const uint32_t left = --texture->refs;
    if (left == 0) {
        destroy_texture(state, texture);
    }
    return left;
}

// A texture level's surface shares its texture's references (as Direct3D 9's do).
uint32_t release(Backend& state, Surface* surface)
{
    if (surface == nullptr) {
        return 0;
    }
    if (surface->kind == SurfaceKind::kTextureLevel) {
        return release(state, surface->texture);
    }
    if (surface->refs == 0) {
        return 0;
    }
    const uint32_t left = --surface->refs;
    if (left == 0) {
        destroy_surface(state, surface);
    }
    return left;
}

uint32_t release(Backend& state, Buffer* buffer)
{
    if (buffer == nullptr || buffer->refs == 0) {
        return 0;
    }
    const uint32_t left = --buffer->refs;
    if (left == 0) {
        destroy_buffer(state, buffer);
    }
    return left;
}

void retain(Surface* surface)
{
    if (surface == nullptr) {
        return;
    }
    if (surface->kind == SurfaceKind::kTextureLevel) {
        ++surface->texture->refs;
    } else {
        ++surface->refs;
    }
}

void retain(Buffer* buffer)
{
    if (buffer) {
        ++buffer->refs;
    }
}

// A binding the device holds a reference to, replaced: the new one retained first, then the old one released.
template <typename T>
void rebind(Backend& state, T** slot, T* object)
{
    retain(object);
    T* const old = *slot;
    *slot = object;
    release(state, old);
}

// --- textures ----------------------------------------------------------------------------------------------------

Extent texture_extent(const Texture& texture) { return {.width = texture.desc.width, .height = texture.desc.height}; }

// The image usage for a texture: sampled and copied always; drawn into for a render-target or depth texture.
VkImageUsageFlags texture_usage(const GpuTextureDesc& desc, const FormatPlan& plan)
{
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((plan.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    } else if ((desc.usage & gpu_value::kUsageRenderTarget) != 0 && !plan.block_compressed) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    return usage;
}

// Texels in the image's format copied into a level of it: staged, and recorded before the frame's stream when the
// stream hasn't used the image yet.
bool upload_image(Backend& state, Image* image, uint32_t level, std::span<const uint8_t> texels)
{
    const std::optional<RingSpan> staged = state.staging.copy(texels, kStagingAlignment);
    if (!staged) {
        return false;
    }
    const TransferTarget target = transfer_commands(state, image->used);
    Memory::record_upload_level(target.commands, image, level, {.buffer = staged->buffer, .offset = staged->offset});
    if (target.in_stream) {
        image->used = state.frames.serial();
    }
    transfer_done(state, target, texels.size());
    return true;
}

// The level's bytes (Direct3D 9's layout) converted and copied into the image.
bool upload_level(Backend& state, Texture* texture, uint32_t level)
{
    const auto start = std::chrono::steady_clock::now();
    const Extent extent = level_extent(texture_extent(*texture), level);
    const LevelLayout layout = d3d_level_layout(texture->desc.format, extent);
    const LevelLayout image_layout = image_level_layout(texture->plan, texture->desc.format, extent);
    std::vector<uint8_t> texels(image_layout.bytes());
    convert_level(texture->plan.conversion, extent, layout, core::at(texture->levels, level), texels);
    const bool done = upload_image(state, &texture->image, level, texels);
    state.stats.upload_seconds += seconds_since(start);
    return done;
}

// A lock's first byte: the area's top left (a block-compressed format's in blocks).
std::size_t lock_offset(uint32_t d3d_format, LevelLayout layout, const GpuRect* area)
{
    if (area == nullptr) {
        return 0;
    }
    const auto left = static_cast<uint32_t>(std::max(area->left, 0));
    const auto top = static_cast<uint32_t>(std::max(area->top, 0));
    constexpr uint32_t kBlockSide = 4;
    if (d3d_block_bytes(d3d_format) != 0) {
        return (static_cast<std::size_t>(top / kBlockSide) * layout.pitch) +
               (static_cast<std::size_t>(left / kBlockSide) * d3d_block_bytes(d3d_format));
    }
    return (static_cast<std::size_t>(top) * layout.pitch) +
           (static_cast<std::size_t>(left) * d3d_pixel_bytes(d3d_format));
}

long lock_level(Texture* texture, uint32_t level, GpuLockedRect* locked, const GpuRect* area, uint32_t flags)
{
    if (level >= texture->image.levels || core::at(texture->locks, level).locked) {
        return kInvalid;
    }
    const LevelLayout layout = d3d_level_layout(texture->desc.format, level_extent(texture_extent(*texture), level));
    std::vector<uint8_t>& bytes = core::at(texture->levels, level);
    if (bytes.empty()) {
        bytes.assign(layout.bytes(), 0);
    }
    const std::size_t offset = lock_offset(texture->desc.format, layout, area);
    if (offset >= bytes.size()) {
        return kInvalid;
    }
    core::at(texture->locks, level) = {.locked = true, .read_only = (flags & gpu_value::kLockReadOnly) != 0};
    locked->pitch = static_cast<int32_t>(layout.pitch);
    locked->bits = std::span(bytes).subspan(offset).data();
    return kOk;
}

long unlock_level(Backend& state, Texture* texture, uint32_t level)
{
    if (level >= texture->image.levels || !core::at(texture->locks, level).locked) {
        return kInvalid;
    }
    const bool read_only = core::at(texture->locks, level).read_only;
    core::at(texture->locks, level) = {};
    if (read_only) {
        return kOk;
    }
    return upload_level(state, texture, level) ? kOk : kInvalid;
}

// --- surfaces ----------------------------------------------------------------------------------------------------

// The image a surface names and its level (a texture level's is its texture's); none for a system-memory surface.
struct ImageLevel {
    Image* image = nullptr;
    uint32_t level = 0;
    const FormatPlan* plan = nullptr;
};
ImageLevel image_of(Surface* surface)
{
    switch (surface->kind) {
    case SurfaceKind::kTextureLevel:
        return {.image = &surface->texture->image, .level = surface->level, .plan = &surface->texture->plan};
    case SurfaceKind::kRenderTarget:
    case SurfaceKind::kDepth:
        return {.image = &surface->image, .level = 0, .plan = &surface->plan};
    case SurfaceKind::kSystemMemory:
        break;
    }
    return {};
}

// A rectangle as a blit's corners (null: the whole level).
ImageArea area_of(const GpuRect* rect, Extent level_size, uint32_t level)
{
    ImageArea area{.level = level, .corners = {}};
    if (rect == nullptr) {
        area.corners.back() = {
            .x = static_cast<int32_t>(level_size.width), .y = static_cast<int32_t>(level_size.height), .z = 1};
        return area;
    }
    area.corners.front() = {.x = rect->left, .y = rect->top, .z = 0};
    area.corners.back() = {.x = rect->right, .y = rect->bottom, .z = 1};
    return area;
}

Surface* new_surface(Backend& state, SurfaceKind kind)
{
    auto made = std::make_unique<Surface>();
    made->kind = kind;
    return state.surfaces.add(std::move(made));
}

long make_render_target(Backend& state, const GpuSurfaceDesc& desc, GpuSurface** out)
{
    *out = nullptr;
    const std::optional<FormatPlan> plan = plan_format(desc.format, state.formats);
    if (!plan || plan->block_compressed || plan->aspect != VK_IMAGE_ASPECT_COLOR_BIT || !state.memory.ready()) {
        return kInvalid;
    }
    Surface* const surface = new_surface(state, SurfaceKind::kRenderTarget);
    surface->plan = *plan;
    surface->d3d_format = desc.format;
    surface->extent = {.width = desc.width, .height = desc.height};
    surface->samples = desc.samples;
    surface->quality = desc.quality;
    surface->lockable = desc.lockable_or_discard;
    const VkSampleCountFlagBits samples = samples_of(desc.samples);
    const ImageRequest request{.format = plan->format,
                               .aspect = plan->aspect,
                               .extent = surface->extent,
                               .levels = 1,
                               .samples = samples,
                               .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               .alpha_one = false};
    if (!state.memory.create_image(request, &surface->image)) {
        destroy_surface(state, surface);
        return kInvalid;
    }
    prepare_image(state, &surface->image);
    *out = surface_handle(surface);
    return kOk;
}

long make_depth_surface(Backend& state, const GpuSurfaceDesc& desc, GpuSurface** out)
{
    *out = nullptr;
    const std::optional<FormatPlan> plan = plan_format(desc.format, state.formats);
    if (!plan || (plan->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0 || !state.memory.ready()) {
        return kInvalid;
    }
    Surface* const surface = new_surface(state, SurfaceKind::kDepth);
    surface->plan = *plan;
    surface->d3d_format = desc.format;
    surface->extent = {.width = desc.width, .height = desc.height};
    surface->samples = desc.samples;
    surface->quality = desc.quality;
    const ImageRequest request{.format = plan->format,
                               .aspect = plan->aspect,
                               .extent = surface->extent,
                               .levels = 1,
                               .samples = samples_of(desc.samples),
                               .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                        rt_shadow_depth_usage(state, plan->format) | // (NOT ORIGINAL - R4, R5)
                                            rt_effects_depth_usage(state, plan->format) |
                                            rt_reflections_depth_usage(state, plan->format) |
                                            rt_fog_depth_usage(state, plan->format) |
                                            rt_sun_depth_usage(state, plan->format) | // (and the sun's: vulkan_rt_sun.h)
                                            rt_sky_depth_usage(state, plan->format) | // (and the sky's)
                                            upscale_depth_usage(state, plan->format), // (the upscaler's: vulkan_upscale.h)
                               .alpha_one = false};
    if (!state.memory.create_image(request, &surface->image)) {
        destroy_surface(state, surface);
        return kInvalid;
    }
    // NOT ORIGINAL - ray-traced shadows (vulkan_rt_shadows.h) sample the depth: kept between passes as an attachment
    // all the same (a samplable image would otherwise rest read-only)
    surface->image.resting = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    prepare_image(state, &surface->image);
    *out = surface_handle(surface);
    return kOk;
}

// A render target's pixels read into its own bytes (Direct3D 9's layout: the formats render targets have are the
// image's own bytes).
bool read_target_bytes(Backend& state, Surface* surface)
{
    const ImageLevel source = image_of(surface);
    if (source.image == nullptr || source.image->samples != VK_SAMPLE_COUNT_1_BIT ||
        source.plan->conversion != TexelConversion::kCopy || !finish_work(state)) {
        return false;
    }
    const Extent extent = level_extent(source.image->extent, source.level);
    // NOT ORIGINAL - HDR (vulkan_hdr.h): a float back buffer whose surface says 8 bits read back as 8 bits
    if (source.image->format == kHdrFormat && d3d_pixel_bytes(surface->d3d_format) == 4 && source.level == 0) {
        return read_hdr_as_bgra8(state, source.image, extent, &surface->bytes);
    }
    const LevelLayout layout = image_level_layout(*source.plan, surface->d3d_format, extent);
    surface->bytes.resize(layout.bytes());
    return state.memory.read_image_level(source.image, source.level, &surface->bytes);
}

// Rows of 32-bit pixels copied from one surface's bytes into another's, clipped to both (the front buffer's read).
void copy_rows(const std::vector<uint8_t>& from, Extent from_size, std::vector<uint8_t>* into, Extent into_size)
{
    constexpr uint32_t kPixelBytes = 4;
    const uint32_t width = std::min(from_size.width, into_size.width);
    const uint32_t height = std::min(from_size.height, into_size.height);
    const std::span<const uint8_t> source(from);
    const std::span<uint8_t> target(*into);
    for (uint32_t line = 0; line < height; ++line) {
        const std::span<const uint8_t> row =
            source.subspan(static_cast<std::size_t>(line) * from_size.width * kPixelBytes,
                           static_cast<std::size_t>(width) * kPixelBytes);
        std::ranges::copy(row, target.subspan(static_cast<std::size_t>(line) * into_size.width * kPixelBytes).begin());
    }
}

// --- buffers -----------------------------------------------------------------------------------------------------

long make_buffer(Backend& state, bool index, const std::array<uint32_t, 4>& args, Buffer** out)
{
    // (args: bytes, usage, format, pool)
    *out = nullptr;
    if (!state.memory.ready() || args.front() == 0) {
        return kInvalid;
    }
    auto made = std::make_unique<Buffer>();
    made->index = index;
    made->bytes = std::get<0>(args);
    made->usage = std::get<1>(args);
    made->format = std::get<2>(args);
    made->pool = std::get<3>(args);
    made->dynamic = (made->usage & gpu_value::kUsageDynamic) != 0;
    const VkBufferUsageFlags usage = (index ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) |
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                     ray_tracing_buffer_usage(state); // (NOT ORIGINAL - build inputs: vulkan_rt_scene.h)
    BufferVersion version;
    if (!state.memory.create_buffer(made->bytes, usage, made->dynamic ? BufferPlace::kUpload : BufferPlace::kDevice,
                                    &version.memory)) {
        return kInvalid;
    }
    made->versions.push_back(version);
    if (!made->dynamic) {
        made->shadow.assign(made->bytes, 0);
    }
    *out = state.buffers.add(std::move(made));
    return kOk;
}

// A dynamic buffer's memory for a discard: the current version if the device is done with it, else one that is free,
// else a new one.
bool discard_version(Backend& state, Buffer* buffer)
{
    const uint64_t completed = state.frames.completed();
    if (core::at(buffer->versions, buffer->current).last_use <= completed) {
        return true;
    }
    for (std::size_t i = 0; i < buffer->versions.size(); ++i) {
        if (core::at(buffer->versions, i).last_use <= completed) {
            buffer->current = i;
            return true;
        }
    }
    BufferVersion version;
    const VkBufferUsageFlags usage =
        (buffer->index ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | ray_tracing_buffer_usage(state);
    if (!state.memory.create_buffer(buffer->bytes, usage, BufferPlace::kUpload, &version.memory)) {
        return false;
    }
    buffer->versions.push_back(version);
    buffer->current = buffer->versions.size() - 1;
    return true;
}

long lock_buffer(Backend& state, Buffer* buffer, const std::array<uint32_t, 2>& range, void** data, uint32_t flags)
{
    // (range: offset, bytes - 0 bytes: to the end, as Direct3D 9's)
    *data = nullptr;
    const uint32_t offset = range.front();
    if (buffer->lock.locked || offset >= buffer->bytes) {
        return kInvalid;
    }
    const uint32_t bytes = range.back() == 0 ? buffer->bytes - offset : std::min(range.back(), buffer->bytes - offset);
    if (buffer->dynamic) {
        if ((flags & gpu_value::kLockDiscard) != 0 && !discard_version(state, buffer)) {
            return kInvalid;
        }
        const DeviceBuffer& memory = core::at(buffer->versions, buffer->current).memory;
        const std::span<uint8_t> whole(static_cast<uint8_t*>(memory.mapped), static_cast<std::size_t>(memory.bytes));
        *data = whole.subspan(offset).data();
    } else {
        *data = std::span(buffer->shadow).subspan(offset).data();
    }
    buffer->lock = {.locked = true, .read_only = (flags & gpu_value::kLockReadOnly) != 0};
    buffer->lock_offset = offset;
    buffer->lock_bytes = bytes;
    return kOk;
}

long unlock_buffer(Backend& state, Buffer* buffer)
{
    if (!buffer->lock.locked) {
        return kInvalid;
    }
    const bool read_only = buffer->lock.read_only;
    buffer->lock = {};
    if (read_only) {
        return kOk;
    }
    BufferVersion& version = core::at(buffer->versions, buffer->current);
    if (buffer->dynamic) {
        state.memory.flush(version.memory, buffer->lock_offset, buffer->lock_bytes);
        return kOk;
    }
    // the part written staged, and copied before the frame's stream when the stream hasn't drawn from the buffer yet
    const auto start = std::chrono::steady_clock::now();
    const std::span<const uint8_t> written = std::span(buffer->shadow).subspan(buffer->lock_offset, buffer->lock_bytes);
    if (written.empty()) {
        return kOk;
    }
    const std::optional<RingSpan> staged = state.staging.copy(written, kStagingAlignment);
    if (!staged) {
        return kInvalid;
    }
    const TransferTarget target = transfer_commands(state, version.last_use);
    const uint64_t serial = state.frames.serial();
    if (version.copied == serial && version.copied_in_stream == target.in_stream) {
        copy_after_copy(target.commands);
    }
    version.copied = serial;
    version.copied_in_stream = target.in_stream;
    const VkBufferCopy region{.srcOffset = staged->offset, .dstOffset = buffer->lock_offset, .size = written.size()};
    vkCmdCopyBuffer(target.commands, staged->buffer, version.memory.buffer, 1, &region);
    if (target.in_stream) {
        version.last_use = state.frames.serial();
    }
    transfer_done(state, target, written.size());
    state.stats.upload_seconds += seconds_since(start);
    return kOk;
}

// --- queries -----------------------------------------------------------------------------------------------------

long make_query(Backend& state, GpuQuery** out)
{
    *out = nullptr;
    if (!state.memory.ready()) {
        return kInvalid;
    }
    // a free slot this submission hasn't used (its reset goes before the submission's stream)
    const uint64_t serial = state.frames.serial();
    auto free_slot = state.query_slots.begin();
    for (; free_slot != state.query_slots.end(); ++free_slot) {
        if (*free_slot == nullptr &&
            core::at(state.query_slot_used, static_cast<std::size_t>(free_slot - state.query_slots.begin())) < serial) {
            break;
        }
    }
    if (free_slot == state.query_slots.end()) {
        const VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                         .pNext = nullptr,
                                         .flags = 0,
                                         .queryType = VK_QUERY_TYPE_OCCLUSION,
                                         .queryCount = kQueriesPerPool,
                                         .pipelineStatistics = 0};
        VkQueryPool pool = VK_NULL_HANDLE;
        if (vkCreateQueryPool(state.memory.device(), &info, nullptr, &pool) != VK_SUCCESS) {
            return kInvalid;
        }
        state.query_pools.push_back(pool);
        state.query_slots.resize(state.query_slots.size() + kQueriesPerPool, nullptr);
        state.query_slot_used.resize(state.query_slots.size(), 0);
        free_slot = state.query_slots.end() - kQueriesPerPool;
    }
    const auto index = static_cast<uint32_t>(free_slot - state.query_slots.begin());
    auto made = std::make_unique<Query>();
    made->pool = core::at(state.query_pools, index / kQueriesPerPool);
    made->slot = index % kQueriesPerPool;
    made->index = index;
    if (state.draws_made) {
        vkCmdResetQueryPool(state.frames.uploads(), made->pool, made->slot, 1);
        made->fresh = true;
    }
    Query* const query = state.queries.add(std::move(made));
    *free_slot = query;
    *out = query_handle(query);
    return kOk;
}

uint32_t release_query_object(Backend& state, Query* query)
{
    if (query == nullptr || query->refs == 0) {
        return 0;
    }
    const uint32_t left = --query->refs;
    if (left == 0) {
        std::ranges::replace(state.query_slots, query, nullptr);
        state.queries.erase(query);
    }
    return left;
}

// --- draws from memory ---------------------------------------------------------------------------------------------

// How many vertices `count` primitives take.
uint32_t vertices_for(GpuPrimitive primitive, uint32_t count)
{
    switch (primitive) {
    case GpuPrimitive::kPointList:
        return count;
    case GpuPrimitive::kLineList:
        return count * 2;
    case GpuPrimitive::kLineStrip:
        return count + 1;
    case GpuPrimitive::kTriangleList:
        return count * 3;
    case GpuPrimitive::kTriangleStrip:
    case GpuPrimitive::kTriangleFan:
        return count + 2;
    }
    return 0;
}

// A draw from memory with indices (DrawIndexedPrimitiveUP's numbers), and the bytes of its indices and of its vertices
// (from vertex 0 to the last the indices reach: min_index + vertex_count).
struct IndexedFromMemory {
    GpuPrimitive primitive = GpuPrimitive::kTriangleList;
    uint32_t min_index = 0;
    uint32_t vertex_count = 0;
    uint32_t count = 0;
    uint32_t index_format = 0;
    uint32_t stride = 0;
};
std::size_t index_bytes_of(const IndexedFromMemory& draw)
{
    const uint32_t index_bytes = draw.index_format == kIndex32Format ? kIndex32Bytes : kIndex16Bytes;
    return static_cast<std::size_t>(vertices_for(draw.primitive, draw.count)) * index_bytes;
}
std::size_t vertex_bytes_of(const IndexedFromMemory& draw)
{
    return static_cast<std::size_t>(draw.min_index + draw.vertex_count) * draw.stride;
}

class VulkanGpu final : public Gpu {
public:
    VulkanGpu() = default;
    VulkanGpu(const VulkanGpu&) = delete;
    VulkanGpu& operator=(const VulkanGpu&) = delete;
    VulkanGpu(VulkanGpu&&) = delete;
    VulkanGpu& operator=(VulkanGpu&&) = delete;
    ~VulkanGpu() = default;

    // What the API holds: the state block's values (Vulkan has no device state to ask).
    long get_render_state(GpuRenderState state, uint32_t* value) override
    {
        const std::scoped_lock lock(backend().mutex);
        *value = state_block().render_state(state);
        return kOk;
    }
    long get_stage_state(uint32_t stage, GpuStageState state, uint32_t* value) override
    {
        const std::scoped_lock lock(backend().mutex);
        *value = state_block().stage_state(stage, state);
        return kOk;
    }
    long get_sampler_state(uint32_t sampler, GpuSamplerState state, uint32_t* value) override
    {
        const std::scoped_lock lock(backend().mutex);
        *value = state_block().sampler_state(sampler, state);
        return kOk;
    }
    long get_transform(GpuTransform transform, float* matrix) override
    {
        const std::scoped_lock lock(backend().mutex);
        const auto number = static_cast<uint32_t>(transform);
        const auto first_stage = static_cast<uint32_t>(GpuTransform::kTexture0);
        std::size_t slot = kGpuTransformCount;
        if (transform == GpuTransform::kWorld) {
            slot = kWorldSlot;
        } else if (transform == GpuTransform::kView) {
            slot = kViewSlot;
        } else if (transform == GpuTransform::kProjection) {
            slot = kProjectionSlot;
        } else if (number >= first_stage && number < first_stage + kGpuStageCount) {
            slot = kFirstStageSlot + (number - first_stage);
        }
        if (slot == kGpuTransformCount) {
            return kInvalid;
        }
        std::ranges::copy(core::at(state_block().transforms, slot), std::span(matrix, kGpuMatrixFloats).begin());
        return kOk;
    }
    long get_viewport(GpuViewport* viewport) override
    {
        const std::scoped_lock lock(backend().mutex);
        default_viewport(backend());
        *viewport = state_block().viewport;
        return kOk;
    }
    long get_vertex_format(uint32_t* fvf) override
    {
        const std::scoped_lock lock(backend().mutex);
        *fvf = state_block().vertex_format;
        return kOk;
    }
    long get_pixel_shader(GpuPixelShader** shader) override
    {
        const std::scoped_lock lock(backend().mutex);
        *shader = state_block().pixel_shader;
        if (*shader) {
            ++object(*shader)->refs;
        }
        return kOk;
    }
    long get_vertex_shader(GpuVertexShader** shader) override
    {
        // (the Gpu makes no vertex shaders: whatever was set, null with the fixed-function pipeline)
        const std::scoped_lock lock(backend().mutex);
        *shader = state_block().vertex_shader;
        return kOk;
    }
    long get_indices(GpuIndexBuffer** buffer) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        retain(state.indices);
        *buffer = state.indices ? index_handle(state.indices) : nullptr;
        return kOk;
    }

    long create_texture(const GpuTextureDesc& desc, GpuTexture** texture) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        *texture = nullptr;
        const std::optional<FormatPlan> plan = plan_format(desc.format, state.formats);
        if (!plan || !state.memory.ready() || desc.width == 0 || desc.height == 0) {
            return kInvalid;
        }
        const Extent top{.width = desc.width, .height = desc.height};
        const uint32_t chain = full_chain_levels(top);
        const uint32_t levels = desc.levels == 0 ? chain : std::min(desc.levels, chain);
        GpuTextureDesc kept = desc;
        if (plan->aspect != VK_IMAGE_ASPECT_COLOR_BIT) {
            kept.usage |= kUsageDepthStencil;
        }
        const ImageRequest request{.format = plan->format,
                                   .aspect = plan->aspect,
                                   .extent = top,
                                   .levels = levels,
                                   .samples = VK_SAMPLE_COUNT_1_BIT,
                                   .usage = texture_usage(desc, *plan),
                                   .alpha_one = plan->alpha_one};
        auto made = std::make_unique<Texture>();
        made->desc = kept;
        made->plan = *plan;
        if (!state.memory.create_image(request, &made->image)) {
            return kInvalid;
        }
        prepare_image(state, &made->image);
        made->levels.resize(levels);
        made->locks.resize(levels);
        made->surfaces.resize(levels);
        *texture = texture_handle(state.textures.add(std::move(made)));
        return kOk;
    }
    long lock_texture(GpuTexture* texture, uint32_t level, GpuLockedRect* locked, const GpuRect* area,
                      uint32_t flags) override
    {
        const std::scoped_lock lock(backend().mutex);
        return lock_level(object(texture), level, locked, area, flags);
    }
    long unlock_texture(GpuTexture* texture, uint32_t level) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return unlock_level(state, object(texture), level);
    }
    long texture_surface(GpuTexture* texture, uint32_t level, GpuSurface** surface) override
    {
        const std::scoped_lock lock(backend().mutex);
        Texture* const owner = object(texture);
        *surface = nullptr;
        if (level >= owner->image.levels) {
            return kInvalid;
        }
        std::unique_ptr<Surface>& kept = core::at(owner->surfaces, level);
        if (!kept) {
            kept = std::make_unique<Surface>();
            kept->kind = SurfaceKind::kTextureLevel;
            kept->texture = owner;
            kept->level = level;
            kept->plan = owner->plan;
            kept->d3d_format = owner->desc.format;
            kept->extent = level_extent(texture_extent(*owner), level);
        }
        ++owner->refs;
        *surface = surface_handle(kept.get());
        return kOk;
    }
    long texture_level_size(GpuTexture* texture, uint32_t level, GpuSize* size) override
    {
        const std::scoped_lock lock(backend().mutex);
        const Texture* const owner = object(texture);
        if (level >= owner->image.levels) {
            return kInvalid;
        }
        const Extent extent = level_extent(texture_extent(*owner), level);
        *size = {.width = extent.width, .height = extent.height};
        return kOk;
    }
    long texture_level_desc(GpuTexture* texture, uint32_t level, GpuLevelDesc* desc) override
    {
        const std::scoped_lock lock(backend().mutex);
        const Texture* const owner = object(texture);
        if (level >= owner->image.levels) {
            return kInvalid;
        }
        const Extent extent = level_extent(texture_extent(*owner), level);
        *desc = {.width = extent.width, .height = extent.height, .format = owner->desc.format};
        return kOk;
    }
    uint32_t texture_level_count(GpuTexture* texture) override
    {
        const std::scoped_lock lock(backend().mutex);
        return object(texture)->image.levels;
    }
    uint32_t release_texture(GpuTexture* texture) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return release(state, object(texture));
    }

    long create_vertex_buffer(uint32_t bytes, uint32_t usage, uint32_t fvf, uint32_t pool,
                              GpuVertexBuffer** buffer) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        Buffer* made = nullptr;
        const long result = make_buffer(state, false, {bytes, usage, fvf, pool}, &made);
        *buffer = made ? vertex_handle(made) : nullptr;
        return result;
    }
    long lock_vertex_buffer(GpuVertexBuffer* buffer, uint32_t offset, uint32_t bytes, void** data,
                            uint32_t flags) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return lock_buffer(state, object(buffer), {offset, bytes}, data, flags);
    }
    long unlock_vertex_buffer(GpuVertexBuffer* buffer) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return unlock_buffer(state, object(buffer));
    }
    uint32_t release_vertex_buffer(GpuVertexBuffer* buffer) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return release(state, object(buffer));
    }
    uint32_t retain_vertex_buffer(GpuVertexBuffer* buffer) override
    {
        const std::scoped_lock lock(backend().mutex);
        Buffer* const kept = object(buffer);
        retain(kept);
        return kept->refs;
    }
    long create_index_buffer(uint32_t bytes, uint32_t usage, uint32_t format, uint32_t pool,
                             GpuIndexBuffer** buffer) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        Buffer* made = nullptr;
        const long result = make_buffer(state, true, {bytes, usage, format, pool}, &made);
        *buffer = made ? index_handle(made) : nullptr;
        return result;
    }
    long lock_index_buffer(GpuIndexBuffer* buffer, uint32_t offset, uint32_t bytes, void** data,
                           uint32_t flags) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return lock_buffer(state, object(buffer), {offset, bytes}, data, flags);
    }
    long unlock_index_buffer(GpuIndexBuffer* buffer) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return unlock_buffer(state, object(buffer));
    }
    uint32_t release_index_buffer(GpuIndexBuffer* buffer) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return release(state, object(buffer));
    }

    long create_render_target(const GpuSurfaceDesc& desc, GpuSurface** surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return make_render_target(state, desc, surface);
    }
    long create_depth_surface(const GpuSurfaceDesc& desc, GpuSurface** surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return make_depth_surface(state, desc, surface);
    }
    // (both pools the game asks for - system memory, scratch - are the processor's memory here)
    long create_offscreen_surface(uint32_t width, uint32_t height, uint32_t format, uint32_t /*pool*/,
                                  GpuSurface** surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        *surface = nullptr;
        const Extent extent{.width = width, .height = height};
        if (d3d_pixel_bytes(format) == 0 || width == 0 || height == 0) {
            return kInvalid;
        }
        Surface* const made = new_surface(state, SurfaceKind::kSystemMemory);
        made->d3d_format = format;
        made->extent = extent;
        made->lockable = true;
        made->bytes.assign(d3d_level_layout(format, extent).bytes(), 0);
        *surface = surface_handle(made);
        return kOk;
    }
    long set_render_target(uint32_t index, GpuSurface* surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (index >= kRenderTargetCount || (index == 0 && surface == nullptr)) {
            return kInvalid;
        }
        rebind(state, &core::at(state.targets, index), surface ? object(surface) : nullptr);
        if (index == 0) {
            // As Direct3D 9's SetRenderTarget: target 0 resets the viewport to the whole of it (the game relies on it -
            // the shadow textures, the screen copies and the back buffer get no viewport of their own).
            const Extent size = object(surface)->extent;
            set_viewport({.x = 0, .y = 0, .width = size.width, .height = size.height, .min_z = 0.0F, .max_z = 1.0F});
        }
        return kOk;
    }
    long set_depth_surface(GpuSurface* surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        rebind(state, &state.depth, surface ? object(surface) : nullptr);
        return kOk;
    }
    long render_target(uint32_t index, GpuSurface** surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        *surface = nullptr;
        if (index >= kRenderTargetCount) {
            return kInvalid;
        }
        Surface* const target = core::at(state.targets, index);
        if (target == nullptr) {
            return gpu_value::kResultNotFound;
        }
        retain(target);
        *surface = surface_handle(target);
        return kOk;
    }
    long depth_surface(GpuSurface** surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        *surface = nullptr;
        if (state.depth == nullptr) {
            return gpu_value::kResultNotFound;
        }
        retain(state.depth);
        *surface = surface_handle(state.depth);
        return kOk;
    }
    long copy_surface(GpuSurface* source, const GpuRect* source_area, GpuSurface* target, const GpuRect* target_area,
                      uint32_t filter) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        const ImageLevel from = image_of(object(source));
        const ImageLevel to = image_of(object(target));
        if (from.image == nullptr || to.image == nullptr || from.plan->block_compressed || to.plan->block_compressed ||
            to.image->samples != VK_SAMPLE_COUNT_1_BIT) {
            return kInvalid;
        }
        const Extent from_size = level_extent(from.image->extent, from.level);
        const Extent to_size = level_extent(to.image->extent, to.level);
        // (recorded into the frame's stream, after what was drawn: the pass ends)
        if (from.image->samples != VK_SAMPLE_COUNT_1_BIT) {
            // a multisampled surface: only resolved whole into one of its size (StretchRect's rule)
            const bool whole = source_area == nullptr && target_area == nullptr && to.level == 0 &&
                               from_size.width == to_size.width && from_size.height == to_size.height;
            if (!whole) {
                return kInvalid;
            }
            if (from.image->format != to.image->format) {
                // NOT ORIGINAL - HDR (vulkan_hdr.h): a float target into an 8-bit one (the screenshot's resolve of
                // the back buffer) resolved in its own format first, then converted by a blit
                if (from.image->format != kHdrFormat && to.image->format != kHdrFormat) {
                    return kInvalid;
                }
                VkCommandBuffer commands = outside_pass(state);
                Image* const resolved = resolve_for_copy(state, commands, from.image);
                if (resolved == nullptr) {
                    return kInvalid;
                }
                Memory::record_blit(commands, resolved, area_of(nullptr, from_size, 0), to.image,
                                    area_of(nullptr, to_size, to.level), VK_FILTER_NEAREST);
            } else {
                Memory::record_resolve(outside_pass(state), from.image, to.image);
            }
        } else {
            Memory::record_blit(outside_pass(state), from.image, area_of(source_area, from_size, from.level), to.image,
                                area_of(target_area, to_size, to.level), filter_of(filter));
        }
        from.image->used = state.frames.serial();
        to.image->used = state.frames.serial();
        return kOk;
    }
    long read_render_target(GpuSurface* target, GpuSurface* system_copy) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        Surface* const from = object(target);
        Surface* const into = object(system_copy);
        if (into->kind != SurfaceKind::kSystemMemory || into->extent.width != from->extent.width ||
            into->extent.height != from->extent.height || !read_target_bytes(state, from)) {
            return kInvalid;
        }
        std::ranges::copy(std::span(from->bytes).first(std::min(from->bytes.size(), into->bytes.size())),
                          into->bytes.begin());
        return kOk;
    }
    uint32_t release_surface(GpuSurface* surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return release(state, object(surface));
    }
    long surface_desc(GpuSurface* surface, GpuSurfaceDesc* desc) override
    {
        const std::scoped_lock lock(backend().mutex);
        const Surface* const kept = object(surface);
        *desc = {.width = kept->extent.width,
                 .height = kept->extent.height,
                 .format = kept->d3d_format,
                 .samples = kept->samples,
                 .quality = kept->quality,
                 .lockable_or_discard = kept->lockable};
        return kOk;
    }
    long lock_surface(GpuSurface* surface, GpuLockedRect* locked, const GpuRect* area, uint32_t flags) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        Surface* const kept = object(surface);
        if (kept->kind == SurfaceKind::kTextureLevel) {
            return lock_level(kept->texture, kept->level, locked, area, flags);
        }
        if (kept->lock.locked || kept->kind == SurfaceKind::kDepth ||
            (kept->kind == SurfaceKind::kRenderTarget && (!kept->lockable || !read_target_bytes(state, kept)))) {
            return kInvalid;
        }
        const LevelLayout layout = d3d_level_layout(kept->d3d_format, kept->extent);
        const std::size_t offset = lock_offset(kept->d3d_format, layout, area);
        if (offset >= kept->bytes.size()) {
            return kInvalid;
        }
        kept->lock = {.locked = true, .read_only = (flags & gpu_value::kLockReadOnly) != 0};
        locked->pitch = static_cast<int32_t>(layout.pitch);
        locked->bits = std::span(kept->bytes).subspan(offset).data();
        return kOk;
    }
    long unlock_surface(GpuSurface* surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        Surface* const kept = object(surface);
        if (kept->kind == SurfaceKind::kTextureLevel) {
            return unlock_level(state, kept->texture, kept->level);
        }
        if (!kept->lock.locked) {
            return kInvalid;
        }
        const bool read_only = kept->lock.read_only;
        kept->lock = {};
        if (kept->kind == SurfaceKind::kRenderTarget && !read_only) {
            return upload_image(state, &kept->image, 0, kept->bytes) ? kOk : kInvalid;
        }
        return kOk;
    }
    long back_buffer(GpuSurface** surface) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        *surface = nullptr;
        if (state.back_buffer == nullptr) {
            return kInvalid;
        }
        retain(state.back_buffer);
        *surface = surface_handle(state.back_buffer);
        return kOk;
    }
    long display_size(GpuSize* size) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        *size = {};
        // (the window's display, else - no window, or SDL doesn't know - the primary one)
        SDL_DisplayID display = state.window != nullptr ? SDL_GetDisplayForWindow(state.window) : 0;
        if (display == 0) {
            display = SDL_GetPrimaryDisplay();
        }
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display);
        if (mode == nullptr) {
            return kInvalid;
        }
        *size = {.width = static_cast<uint32_t>(mode->w), .height = static_cast<uint32_t>(mode->h)};
        return kOk;
    }
    long read_front_buffer(GpuSurface* system_copy) override
    {
        // The screen as shown: the last frame presented, which is the back buffer's image (V7 presents it), into the
        // top left of the display-sized surface; the rest black.
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        Surface* const into = object(system_copy);
        if (into->kind != SurfaceKind::kSystemMemory || d3d_pixel_bytes(into->d3d_format) != 4 ||
            state.back_buffer == nullptr) {
            return kInvalid;
        }
        Surface* const back = state.back_buffer;
        if (back->image.samples != VK_SAMPLE_COUNT_1_BIT) {
            // (multisampled: the resolve the last present copied from)
            if (state.present_source.image == VK_NULL_HANDLE || !finish_work(state)) {
                return kInvalid;
            }
            back->bytes.resize(d3d_level_layout(back->d3d_format, back->extent).bytes());
            if (state.present_source.format == kHdrFormat) { // (NOT ORIGINAL - HDR: converted, vulkan_hdr.h)
                if (!read_hdr_as_bgra8(state, &state.present_source, back->extent, &back->bytes)) {
                    return kInvalid;
                }
            } else if (!state.memory.read_image_level(&state.present_source, 0, &back->bytes)) {
                return kInvalid;
            }
        } else if (!read_target_bytes(state, back)) {
            return kInvalid;
        }
        std::ranges::fill(into->bytes, uint8_t{0});
        copy_rows(back->bytes, back->extent, &into->bytes, into->extent);
        return kOk;
    }
    // NOT ORIGINAL - the upscaler's motion vectors (vulkan_motion.h): the next draws' key and previous vertices.
    void set_motion_source(const MotionSource& source) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        Buffer* const previous = source.previous_vertices != nullptr ? object(source.previous_vertices) : nullptr;
        state.motion.source = {.key = source.key,
                               .previous = previous != nullptr && state.buffers.contains(previous) ? previous : nullptr,
                               .previous_offset = source.previous_offset};
    }
    long save_surface_jpeg(const char* file, GpuSurface* surface) override
    {
        const std::scoped_lock lock(backend().mutex);
        const Surface* const kept = object(surface);
        if (kept->kind != SurfaceKind::kSystemMemory) {
            return kInvalid;
        }
        return write_jpeg_file(file, kept->d3d_format, kept->extent, kept->bytes) ? kOk : kInvalid;
    }

    long create_pixel_shader(const char* source, uint32_t length, GpuPixelShader** shader) override
    {
        // The text kept, and the program it is recognised as (ff_key.h's PixelProgram).
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        auto made = std::make_unique<PixelShader>();
        made->source.assign(source, length);
        made->program = classify_pixel_shader(made->source);
        if (!made->program.known) {
            get_log("vulkan: a pixel shader not recognised - its draws use the texture stages:\n%s", made->source.c_str());
        }
        *shader = shader_handle(state.shaders.add(std::move(made)));
        return kOk;
    }
    uint32_t release_pixel_shader(GpuPixelShader* shader) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        PixelShader* const kept = object(shader);
        if (kept == nullptr || kept->refs == 0) {
            return 0;
        }
        const uint32_t left = --kept->refs;
        if (left == 0) {
            state.shaders.erase(kept);
        }
        return left;
    }

    long create_occlusion_query(GpuQuery** query) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return make_query(state, query);
    }
    long begin_query(GpuQuery* query) override
    {
        // (begun in the frame's stream, in the pass the draws after it go into)
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        Query* const kept = object(query);
        kept->begun = true;
        kept->ended = false;
        record_begin_query(state, state_block(), kept);
        return kOk;
    }
    long end_query(GpuQuery* query) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        Query* const kept = object(query);
        if (!kept->begun) {
            return kInvalid;
        }
        record_end_query(state, kept);
        kept->submission = state.frames.serial();
        kept->begun = false;
        kept->ended = true;
        return kOk;
    }
    long query_result(GpuQuery* query, void* data, uint32_t bytes, uint32_t /*flags*/) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        const Query* const kept = object(query);
        if (!kept->ended) {
            return kept->begun ? gpu_value::kResultNotReady : gpu_value::kResultNotFound;
        }
        if (!kept->recorded) {
            // (no pass to draw into while it was open: nothing drawn)
            if (data != nullptr && bytes >= sizeof(uint32_t)) {
                std::memset(data, 0, sizeof(uint32_t));
            }
            return kOk;
        }
        if (kept->submission > state.frames.completed()) {
            // (its submission not known to have finished: a slot used before may still hold the earlier result)
            return gpu_value::kResultNotReady;
        }
        std::array<uint32_t, 2> result{}; // the count, and whether it is there
        const VkResult asked =
            vkGetQueryPoolResults(state.memory.device(), kept->pool, kept->slot, 1, sizeof(result), result.data(),
                                  sizeof(result), VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (asked == VK_NOT_READY || result.back() == 0) {
            return gpu_value::kResultNotReady;
        }
        if (asked != VK_SUCCESS) {
            return kInvalid;
        }
        // (the pixel count as a DWORD, Direct3D 9's occlusion result)
        const uint32_t count = result.front();
        if (data != nullptr && bytes >= sizeof(count)) {
            std::memcpy(data, &count, sizeof(count));
        }
        return kOk;
    }
    uint32_t release_query(GpuQuery* query) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return release_query_object(state, object(query));
    }

    long set_stream_source(uint32_t stream, GpuVertexBuffer* buffer, uint32_t offset, uint32_t stride) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (stream >= kStreamCount) {
            return kInvalid;
        }
        StreamSource& source = core::at(state.streams, stream);
        rebind(state, &source.buffer, buffer ? object(buffer) : nullptr);
        std::tie(source.offset, source.stride) = std::tuple(offset, stride);
        return kOk;
    }
    long set_indices(GpuIndexBuffer* buffer) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        rebind(state, &state.indices, buffer ? object(buffer) : nullptr);
        return kOk;
    }
    long get_stream_source(uint32_t stream, GpuVertexBuffer** buffer, uint32_t* offset, uint32_t* stride) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        *buffer = nullptr;
        if (stream >= kStreamCount) {
            return kInvalid;
        }
        const StreamSource& source = core::at(state.streams, stream);
        retain(source.buffer);
        *buffer = source.buffer ? vertex_handle(source.buffer) : nullptr;
        std::tie(*offset, *stride) = std::tuple(source.offset, source.stride);
        return kOk;
    }
    // Draws: recorded into the frame's stream (vulkan_draws.cpp) with the pipeline the state block makes.
    long draw(GpuPrimitive primitive, uint32_t first_vertex, uint32_t count) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        default_viewport(state);
        const StreamSource& source = core::at(state.streams, 0);
        if (source.buffer == nullptr || !state.buffers.contains(source.buffer)) {
            return kOk;
        }
        BufferVersion& version = core::at(source.buffer->versions, source.buffer->current);
        const DrawCall call{.primitive = primitive,
                            .vertices = vertices_for(primitive, count),
                            .indexed = false,
                            .vertex_buffer = version.memory.buffer,
                            .vertex_offset = source.offset,
                            .stride = source.stride,
                            .first_vertex = first_vertex};
        const long result = record_draw(state, state_block(), call);
        version.last_use = state.frames.serial();
        return result;
    }
    long draw_indexed(GpuPrimitive primitive, int32_t base_vertex, uint32_t /*min_index*/, uint32_t /*vertex_count*/,
                      uint32_t first_index, uint32_t count) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        default_viewport(state);
        const StreamSource& source = core::at(state.streams, 0);
        Buffer* const indices = state.indices;
        if (source.buffer == nullptr || indices == nullptr || !state.buffers.contains(source.buffer) ||
            !state.buffers.contains(indices)) {
            return kOk;
        }
        BufferVersion& version = core::at(source.buffer->versions, source.buffer->current);
        BufferVersion& index_version = core::at(indices->versions, indices->current);
        const DrawCall call{.primitive = primitive,
                            .vertices = vertices_for(primitive, count),
                            .indexed = true,
                            .vertex_buffer = version.memory.buffer,
                            .vertex_offset = source.offset,
                            .stride = source.stride,
                            .first_vertex = 0,
                            .index_buffer = index_version.memory.buffer,
                            .index_offset = 0,
                            .index_type = indices->format == kIndex32Format ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16,
                            .first_index = first_index,
                            .base_vertex = base_vertex};
        const long result = record_draw(state, state_block(), call);
        version.last_use = state.frames.serial();
        index_version.last_use = state.frames.serial();
        return result;
    }
    long draw_from_memory(GpuPrimitive primitive, uint32_t count, const void* vertices, uint32_t stride) override
    {
        // (the vertices copied into the frame's ring)
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        default_viewport(state);
        const std::size_t bytes = static_cast<std::size_t>(vertices_for(primitive, count)) * stride;
        if (!state.memory.ready() || vertices == nullptr || bytes == 0) {
            return state.memory.ready() ? kOk : kInvalid;
        }
        const std::span<const uint8_t> data(static_cast<const uint8_t*>(vertices), bytes);
        const std::optional<RingSpan> copied = state.ring.copy(data, kRingAlignment);
        if (!copied) {
            return kInvalid;
        }
        const DrawCall call{.primitive = primitive,
                            .vertices = vertices_for(primitive, count),
                            .indexed = false,
                            .vertex_buffer = copied->buffer,
                            .vertex_offset = copied->offset,
                            .stride = stride,
                            .first_vertex = 0};
        return record_draw(state, state_block(), call);
    }
    long draw_indexed_from_memory(GpuPrimitive primitive, uint32_t min_index, uint32_t vertex_count, uint32_t count,
                                  const void* indices, uint32_t index_format, const void* vertices,
                                  uint32_t stride) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        default_viewport(state);
        if (!state.memory.ready()) {
            return kInvalid;
        }
        const IndexedFromMemory draw{.primitive = primitive,
                                     .min_index = min_index,
                                     .vertex_count = vertex_count,
                                     .count = count,
                                     .index_format = index_format,
                                     .stride = stride};
        const std::size_t indices_size = index_bytes_of(draw);
        const std::size_t vertices_size = vertex_bytes_of(draw);
        if (indices == nullptr || vertices == nullptr || indices_size == 0 || vertices_size == 0) {
            return kOk;
        }
        const std::optional<RingSpan> index_copy =
            state.ring.copy(std::span(static_cast<const uint8_t*>(indices), indices_size), kRingAlignment);
        const std::optional<RingSpan> vertex_copy =
            state.ring.copy(std::span(static_cast<const uint8_t*>(vertices), vertices_size), kRingAlignment);
        if (!index_copy || !vertex_copy) {
            return kInvalid;
        }
        const DrawCall call{.primitive = primitive,
                            .vertices = vertices_for(primitive, count),
                            .indexed = true,
                            .vertex_buffer = vertex_copy->buffer,
                            .vertex_offset = vertex_copy->offset,
                            .stride = stride,
                            .first_vertex = 0,
                            .index_buffer = index_copy->buffer,
                            .index_offset = index_copy->offset,
                            .index_type = index_format == kIndex32Format ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16,
                            .first_index = 0,
                            .base_vertex = 0};
        return record_draw(state, state_block(), call);
    }

    long begin_scene() override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.in_scene) {
            return kInvalid;
        }
        state.in_scene = true;
        return kOk;
    }
    long end_scene() override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (!state.in_scene) {
            return kInvalid;
        }
        state.in_scene = false;
        return kOk;
    }
    // A clear: the targets' pass opened clearing them (all of them, no pass open on them), or cleared inside it.
    long clear(uint32_t rect_count, const GpuRect* rects, uint32_t flags, uint32_t colour, float z,
               uint32_t /*stencil*/) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        default_viewport(state);
        const std::span<const GpuRect> areas =
            rects != nullptr ? std::span(rects, rect_count) : std::span<const GpuRect>();
        return record_clear(state, state_block(), areas, {.flags = flags, .colour = colour, .z = z});
    }
    long present() override
    {
        // The back buffer copied into the swapchain and shown; the frame's work submitted (vulkan_frames.h).
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        return record_present(state);
    }
    void set_gamma_ramp(const GpuGammaRamp& ramp) override
    {
        // (applied by the present in full screen: record_gamma_pass)
        const std::scoped_lock lock(backend().mutex);
        backend().gamma = ramp;
    }
    long test_cooperative_level() override
    {
        // (never lost: a swapchain out of date is made again by the present, and the back buffer is the Gpu's own)
        return kOk;
    }

protected:
    // The setters' API side: the block has the values (V7's draws read it).
    long send_render_state(GpuRenderState /*state*/, uint32_t /*value*/) override { return kOk; }
    long send_stage_state(uint32_t /*stage*/, GpuStageState /*state*/, uint32_t /*value*/) override { return kOk; }
    long send_sampler_state(uint32_t /*sampler*/, GpuSamplerState /*state*/, uint32_t /*value*/) override
    {
        return kOk;
    }
    long send_texture(uint32_t /*stage*/, GpuTexture* /*texture*/) override { return kOk; }
    long send_vertex_format(uint32_t /*fvf*/) override { return kOk; }
    long send_pixel_shader(GpuPixelShader* /*shader*/) override { return kOk; }
    long send_pixel_constants(uint32_t /*first*/, const float* /*values*/, uint32_t /*count*/) override { return kOk; }
    long send_vertex_shader(GpuVertexShader* /*shader*/) override { return kOk; }
    long send_vertex_constants(uint32_t /*first*/, const float* /*values*/, uint32_t /*count*/) override { return kOk; }
    long send_material(const GpuMaterial& /*material*/) override { return kOk; }
    long send_light(uint32_t /*index*/, const GpuLight& /*light*/) override { return kOk; }
    long send_light_enabled(uint32_t /*index*/, bool /*on*/) override { return kOk; }
    long send_transform(GpuTransform /*transform*/, const float* /*matrix*/) override { return kOk; }
    long send_viewport(const GpuViewport& /*viewport*/) override { return kOk; }
    // A saved state is the state block alone (the interface keeps its copy): nothing of the API's to save.
    long send_save_state(void** api) override
    {
        *api = nullptr;
        return kOk;
    }
    long send_restore_state(void* /*api*/) override { return kOk; }
    uint32_t send_release_saved_state(void* /*api*/) override { return 0; }

private:
    // Direct3D 9's device has a viewport from its creation and after a reset: the whole of render target 0. The state
    // block's is none then (reset_state_block: zero width and height) - taken as that one at the first draw, clear or
    // question (the game's movies and first loading screen draw before anything sets one). The caller holds the lock.
    void default_viewport(const Backend& state)
    {
        const GpuViewport& now = state_block().viewport;
        const Surface* const target = core::at(state.targets, 0);
        if (now.width == 0 && now.height == 0 && target != nullptr) {
            set_viewport({.x = 0,
                          .y = 0,
                          .width = target->extent.width,
                          .height = target->extent.height,
                          .min_z = 0.0F,
                          .max_z = 1.0F});
        }
    }
};

VulkanGpu& vulkan_gpu()
{
    static core::NoDestroy<VulkanGpu> made;
    return made.get();
}

} // namespace

Backend& backend()
{
    static core::NoDestroy<Backend> state;
    return state.get();
}

Gpu& gpu() { return vulkan_gpu(); }

namespace {

// A format the device samples and filters linearly (optimal tiling).
bool sampled_and_filtered(VkPhysicalDevice device, VkFormat format)
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    constexpr VkFormatFeatureFlags kWanted =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & kWanted) == kWanted;
}

// The back buffer (drawn into, copied into the swapchain by the present) and the depth surface the settings ask for,
// made at their size and samples: the device's own references (Backend::back_buffer / auto_depth), not bound.
bool make_device_surfaces(Backend& state)
{
    GpuSurface* back = nullptr;
    GpuSurface* depth = nullptr;
    const GpuSettings& settings = state.settings;
    // (NOT ORIGINAL - HDR, vulkan_hdr.h: 16-bit floats, its format still reported as the settings' below)
    if (make_render_target(state,
                           {.width = settings.width,
                            .height = settings.height,
                            .format = rt::hdr_colour() ? gpu_value::kFormatA16B16G16R16F : gpu_value::kFormatA8R8G8B8,
                            .samples = settings.samples,
                            .quality = settings.sample_quality,
                            .lockable_or_discard = false},
                           &back) != kOk) {
        return false;
    }
    if (make_depth_surface(state,
                           {.width = settings.width,
                            .height = settings.height,
                            .format = settings.depth_format,
                            .samples = settings.samples,
                            .quality = settings.sample_quality,
                            .lockable_or_discard = true},
                           &depth) != kOk) {
        release(state, object(back));
        return false;
    }
    state.back_buffer = object(back);
    state.back_buffer->d3d_format = settings.colour_format;
    state.auto_depth = object(depth);
    return true;
}

} // namespace

long create_device_objects(Backend& state)
{
    VkPhysicalDevice physical = state.context.physical_device();
    const auto sampled = [&physical](VkFormat format) { return sampled_and_filtered(physical, format); };
    constexpr std::array<VkFormat, 2> kDepthStencil{VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    state.formats = {.r5g6b5 = sampled(VK_FORMAT_R5G6B5_UNORM_PACK16),
                     .a1r5g5b5 = sampled(VK_FORMAT_A1R5G5B5_UNORM_PACK16),
                     .block_compressed = state.context.enabled_features().textureCompressionBC == VK_TRUE &&
                                         sampled(VK_FORMAT_BC1_RGBA_UNORM_BLOCK) &&
                                         sampled(VK_FORMAT_BC2_UNORM_BLOCK) && sampled(VK_FORMAT_BC3_UNORM_BLOCK),
                     .depth_stencil = first_depth_format(physical, kDepthStencil)};
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    state.sampler_limits = {.anisotropy = state.context.enabled_features().samplerAnisotropy == VK_TRUE,
                            .max_anisotropy = properties.limits.maxSamplerAnisotropy};
    if (!state.memory.init(state.context)) {
        return kInvalid;
    }
    state.samplers.init(state.context.device(), state.sampler_limits);
    if (!state.ring.init(&state.memory) || !create_draw_objects(state)) {
        return kInvalid;
    }
    rt::set_hdr_colour(hdr_wanted(state)); // NOT ORIGINAL - [Graphics] rt_hdr (vulkan_hdr.h): before the targets
    if (!make_device_surfaces(state)) {
        return kInvalid;
    }
    rebind(state, &core::at(state.targets, 0), state.back_buffer);
    rebind(state, &state.depth, state.auto_depth);
    vulkan_gpu().reset_state_block();
    create_ray_tracing(state); // NOT ORIGINAL - installed when the device has it (vulkan_rt_scene.h)
    return kOk;
}

bool reset_device_bindings(Backend& state)
{
    const GpuSettings& settings = state.settings;
    const Surface* const back = state.back_buffer;
    if (back == nullptr || back->extent.width != settings.width || back->extent.height != settings.height ||
        back->samples != settings.samples || back->quality != settings.sample_quality) {
        Surface* const old_back = state.back_buffer;
        Surface* const old_depth = state.auto_depth;
        if (!make_device_surfaces(state)) {
            state.back_buffer = old_back;
            state.auto_depth = old_depth;
            return false;
        }
        rebind(state, &core::at(state.targets, 0), state.back_buffer);
        rebind(state, &state.depth, state.auto_depth);
        release(state, old_back); // (the device's own references: destroyed once nothing else holds them)
        release(state, old_depth);
    }
    rebind(state, &core::at(state.targets, 0), state.back_buffer);
    for (std::size_t index = 1; index < kRenderTargetCount; ++index) {
        rebind(state, &core::at(state.targets, index), static_cast<Surface*>(nullptr));
    }
    rebind(state, &state.depth, state.auto_depth);
    for (StreamSource& source : state.streams) {
        rebind(state, &source.buffer, static_cast<Buffer*>(nullptr));
        source = {};
    }
    rebind(state, &state.indices, static_cast<Buffer*>(nullptr));
    return true;
}

void release_device_objects(Backend& state)
{
    if (!state.memory.ready()) {
        return;
    }
    destroy_ui_windows(state); // (the UI's own windows' swapchains and surfaces - vulkan_ui_windows.cpp)
    finish_work(state);
    vkDeviceWaitIdle(state.memory.device());
    release_ray_tracing(state); // NOT ORIGINAL - its structures (vulkan_rt_scene.h)
    state.textures.for_each([&state](Texture* texture) { state.memory.destroy_image(&texture->image); });
    state.surfaces.for_each([&state](Surface* surface) { state.memory.destroy_image(&surface->image); });
    state.buffers.for_each([&state](Buffer* buffer) {
        for (BufferVersion& version : buffer->versions) {
            state.memory.destroy_buffer(&version.memory);
        }
    });
    state.textures.clear();
    state.surfaces.clear();
    state.buffers.clear();
    state.shaders.clear();
    state.queries.clear();
    for (const VkQueryPool pool : state.query_pools) {
        vkDestroyQueryPool(state.memory.device(), pool, nullptr);
    }
    state.query_pools.clear();
    state.query_slots.clear();
    state.back_buffer = nullptr;
    state.auto_depth = nullptr;
    state.targets.fill(nullptr);
    state.depth = nullptr;
    state.streams.fill({});
    state.indices = nullptr;
    release_draw_objects(state);
    state.ring.shutdown();
    state.samplers.clear();
    state.memory.shutdown();
}

const Texture* texture_of(GpuTexture* texture) { return object(texture); }

bool read_texture_level(GpuTexture* texture, uint32_t level, std::vector<uint8_t>* texels)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Texture* const kept = object(texture);
    if (level >= kept->image.levels || !finish_work(state)) {
        return false;
    }
    const Extent extent = level_extent(texture_extent(*kept), level);
    const LevelLayout layout = image_level_layout(kept->plan, kept->desc.format, extent);
    texels->resize(layout.bytes());
    return state.memory.read_image_level(&kept->image, level, texels);
}

bool read_surface(GpuSurface* surface, std::vector<uint8_t>* texels)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    Surface* const kept = object(surface);
    if (!read_target_bytes(state, kept)) {
        return false;
    }
    *texels = kept->bytes;
    return true;
}

bool read_gamma_pass(GpuSurface* surface, std::vector<uint8_t>* texels)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    const ImageLevel source = image_of(object(surface));
    if (source.image == nullptr || source.image->samples != VK_SAMPLE_COUNT_1_BIT) {
        return false;
    }
    Image* const corrected = record_gamma_pass(state, source.image);
    if (corrected == nullptr || !finish_work(state)) {
        return false;
    }
    constexpr std::size_t kPixelBytes = 4; // (the render targets' formats: 32-bit)
    texels->resize(static_cast<std::size_t>(corrected->extent.width) * corrected->extent.height * kPixelBytes);
    const bool read = state.memory.read_image_level(corrected, 0, texels);
    // (the pass's images let go, as at shutdown: the check counts what is left)
    state.memory.destroy_image(&state.gamma_source);
    state.memory.destroy_image(&state.gamma_target);
    return read;
}

namespace {
bool read_buffer_memory(Backend& state, const Buffer* buffer, std::vector<uint8_t>* bytes)
{
    const DeviceBuffer& memory = core::at(buffer->versions, buffer->current).memory;
    return finish_work(state) && state.memory.read_buffer(memory, 0, buffer->bytes, bytes);
}
} // namespace

bool read_vertex_buffer(GpuVertexBuffer* buffer, std::vector<uint8_t>* bytes)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    return read_buffer_memory(state, object(buffer), bytes);
}

bool read_index_buffer(GpuIndexBuffer* buffer, std::vector<uint8_t>* bytes)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    return read_buffer_memory(state, object(buffer), bytes);
}

VkSampler stage_sampler(uint32_t stage)
{
    Backend& state = backend();
    const std::scoped_lock lock(state.mutex);
    return state.samplers.sampler(sampler_key(vulkan_gpu().state_block(), stage));
}

} // namespace render::vulkan
