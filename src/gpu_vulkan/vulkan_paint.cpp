// NOT ORIGINAL - paint trails: the paint's fields on the device and the coat on the world's draws (vulkan_paint.h;
// docs/design/vulkan.md "Paint trails").
#include "gpu_vulkan/vulkan_paint.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <volk.h>

#include "base/core/checked_index.h"
#include "engine/gfx/paint_trails.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/surface_trail.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_memory.h"
#include "gpu_vulkan/vulkan_ring.h"
#include "gpu_vulkan/vulkan_surface_trail.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

// The once-only reports' keys (Backend::reported).
constexpr uint64_t kReportUpload = 0x5041494E01U;
constexpr uint64_t kReportFirstCover = 0x5041494E02U;
constexpr uint64_t kReportSampler = 0x5041494E03U;

constexpr VkFormat kCoatFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kPlaceFormat = VK_FORMAT_R32_SFLOAT;
constexpr std::size_t kTexelBytes = 4;

struct PaintState {
    PaintSettings settings;
    Image coats;  // the three windows side by side (x's, y's, z's field)
    Image places;
    std::array<TrailWindow, paint::kAxes> windows{};
    VkSampler nearest = VK_NULL_HANDLE;
    bool failed = false;
    // the view
    rt::ViewDrawn view;
    bool view_known = false;
    uint64_t view_serial = 0;
    uint64_t block_serial = 0;
    uint64_t block_frame = 0;
    uint64_t window_frame = 0; // the frame whose first view placed the windows
    VkDescriptorBufferInfo block{};
    PaintStats stats;
};

PaintState& paint_state()
{
    static PaintState state;
    return state;
}

bool first_report(Backend& state, uint64_t what) { return state.reported.insert(what).second; }

class PaintListener final : public rt::ViewListener {
public:
    [[nodiscard]] int order() const override { return rt::ViewOrder::kSnow; }
    void view_begins(const rt::ViewDrawn& view) override
    {
        Backend& state = backend();
        const std::scoped_lock lock(state.mutex);
        if (state.device_made && state.draws_made) {
            begin_paint_view(state, view);
        }
    }
    void opaque_drawn(const rt::ViewDrawn& /*view*/) override {}
    void world_drawn(const rt::ViewDrawn& /*view*/) override { paint_state().view_known = false; }
};

PaintListener& listener()
{
    static PaintListener instance;
    return instance;
}

uint32_t window_side() { return static_cast<uint32_t>(paint::kWindowTiles * surface_trail::kTileTexels); }

// The two images (made once), the windows brought round the camera.
bool update_windows(Backend& state, const std::array<float, 3>& camera)
{
    PaintState& own = paint_state();
    const uint32_t side = window_side();
    const Extent extent{.width = side * static_cast<uint32_t>(paint::kAxes), .height = side};
    constexpr VkImageUsageFlags kUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (own.coats.image == VK_NULL_HANDLE || own.places.image == VK_NULL_HANDLE) {
        own.coats.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        own.places.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!ensure_trail_image(state, &own.coats, kCoatFormat, extent, kUsage) ||
            !ensure_trail_image(state, &own.places, kPlaceFormat, extent, kUsage)) {
            return false;
        }
        own.coats.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        own.places.resting = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        for (std::size_t axis = 0; axis < paint::kAxes; ++axis) {
            core::at(own.windows, axis) = TrailWindow{.first_column = static_cast<uint32_t>(axis) * side};
        }
    }
    bool done = true;
    paint::read_fields([&](std::span<const surface_trail::Field* const, paint::kAxes> fields) {
        const std::array<Image*, paint::kPlanes> images{&own.coats, &own.places};
        for (std::size_t axis = 0; axis < paint::kAxes && done; ++axis) {
            done = update_trail_window(state, &core::at(own.windows, axis), *core::at(fields, axis), paint::plane_of(axis, camera),
                                       paint::kWindowTiles, images, nullptr);
        }
    });
    own.stats.tiles_copied = 0;
    own.stats.slots_cleared = 0;
    for (std::size_t axis = 0; axis < paint::kAxes; ++axis) {
        const TrailWindow& window = core::at(own.windows, axis);
        own.stats.tiles_copied += window.tiles_copied;
        own.stats.slots_cleared += window.slots_cleared;
        core::at(own.stats.windows, axis) = window.first;
    }
    own.stats.window_size = paint::kWindowTiles;
    return done;
}

// The view's block (once a view and frame).
std::optional<VkDescriptorBufferInfo> paint_block(Backend& state)
{
    PaintState& own = paint_state();
    const uint64_t frame = state.frames.serial();
    if (own.block_serial == own.view_serial && own.block_frame == frame && own.block.buffer != VK_NULL_HANDLE) {
        return own.block;
    }
    const auto first = [&own](std::size_t axis, std::size_t part) {
        return static_cast<float>(core::at(core::at(own.windows, axis).first, part) * surface_trail::kTileTexels);
    };
    PaintUniforms uniforms{};
    uniforms.eye_to_world = trail_eye_to_world(own.view.camera.view);
    uniforms.window = {paint::kTexel, static_cast<float>(window_side()), 1.0F / paint::kTexel, 1.0F};
    uniforms.first_xy = {first(0, 0), first(0, 1), first(1, 0), first(1, 1)};
    uniforms.first_z = {first(2, 0), first(2, 1), paint::kDrawShare, paint::kDrawSlack};
    const void* const data = &uniforms;
    const std::optional<RingSpan> span =
        state.ring.copy(std::span(static_cast<const uint8_t*>(data), sizeof(uniforms)), state.uniform_alignment);
    if (!span) {
        return std::nullopt;
    }
    own.block = {.buffer = span->buffer, .offset = span->offset, .range = sizeof(uniforms)};
    own.block_serial = own.view_serial;
    own.block_frame = frame;
    return own.block;
}

bool ensure_sampler(Backend& state)
{
    PaintState& own = paint_state();
    if (own.nearest != VK_NULL_HANDLE) {
        return true;
    }
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_NEAREST;
    info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0F;
    if (vkCreateSampler(state.context.device(), &info, nullptr, &own.nearest) != VK_SUCCESS) {
        own.nearest = VK_NULL_HANDLE;
        own.failed = true;
        if (first_report(state, kReportSampler)) {
            get_log("vulkan: paint: its sampler couldn't be made - no paint drawn");
        }
        return false;
    }
    return true;
}

} // namespace

// --- settings ----------------------------------------------------------------------------------------------------

void set_paint_settings(const PaintSettings& settings)
{
    paint::set_settings({.on = settings.on, .scale = settings.scale, .colour = settings.colour}); // (cleaned there)
    const paint::Settings clean = paint::settings();
    {
        const std::scoped_lock lock(backend().mutex);
        paint_state().settings = {.on = clean.on, .scale = clean.scale, .colour = clean.colour};
    }
    if (clean.on) {
        rt::add_view_listener(&listener());
    } else {
        rt::remove_view_listener(&listener());
    }
}

PaintSettings paint_settings()
{
    const std::scoped_lock lock(backend().mutex);
    return paint_state().settings;
}

PaintStats paint_stats() { return paint_state().stats; }

void begin_paint_view(Backend& state, const rt::ViewDrawn& view)
{
    PaintState& own = paint_state();
    own.view = view;
    own.view_known = true;
    ++own.view_serial;
    if (!own.settings.on || own.failed) {
        return;
    }
    const uint64_t frame = state.frames.serial();
    if (own.window_frame != frame) { // (the frame's first view places the windows)
        own.window_frame = frame;
        if (!update_windows(state, view.camera.position) && first_report(state, kReportUpload)) {
            get_log("vulkan: paint: the paint couldn't be put on the device - none drawn");
        }
    }
}

void paint_key(Backend& state, const GpuStateBlock& block, ShaderKey* shaders, PaintDescriptors* out)
{
    *out = {};
    PaintState& own = paint_state();
    if (!own.settings.on || own.failed || !own.view_known || own.coats.image == VK_NULL_HANDLE || own.places.image == VK_NULL_HANDLE ||
        !surface_trail::current_ground() || fvf_pre_transformed(block.vertex_format) || shaders->fragment.water != 0U) {
        return;
    }
    ShaderKey with_paint = *shaders;
    with_paint.fragment.trails = static_cast<uint8_t>(with_paint.fragment.trails | kTrailPaint);
    with_paint = normalised(with_paint);
    if ((with_paint.fragment.trails & kTrailPaint) == 0U || !ensure_sampler(state)) {
        return;
    }
    const std::optional<VkDescriptorBufferInfo> uniforms = paint_block(state);
    if (!uniforms) {
        return;
    }
    *shaders = with_paint;
    out->on = true;
    out->block = *uniforms;
    out->coats = {.sampler = own.nearest, .imageView = own.coats.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    out->places = {.sampler = own.nearest, .imageView = own.places.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const uint64_t serial = state.frames.serial();
    own.coats.used = serial;
    own.places.used = serial;
    ++own.stats.covered_draws;
    if (first_report(state, kReportFirstCover)) {
        get_log("vulkan: paint: the first draw painted (%s)", describe(with_paint.fragment).c_str());
    }
}

bool read_paint_windows(Backend& state, bool places, std::vector<uint32_t>* texels, uint32_t* width, uint32_t* height)
{
    PaintState& own = paint_state();
    Image& image = places ? own.places : own.coats;
    texels->clear();
    *width = 0;
    *height = 0;
    if (image.image == VK_NULL_HANDLE) {
        return false;
    }
    std::vector<uint8_t> raw(static_cast<std::size_t>(image.extent.width) * image.extent.height * kTexelBytes);
    if (!finish_work(state) || !state.memory.read_image_level(&image, 0, &raw)) {
        return false;
    }
    texels->resize(raw.size() / kTexelBytes);
    std::memcpy(texels->data(), raw.data(), raw.size());
    *width = image.extent.width;
    *height = image.extent.height;
    return true;
}

void release_paint(Backend& state)
{
    PaintState& own = paint_state();
    const VkDevice device = state.context.device();
    if (device != nullptr && own.nearest != VK_NULL_HANDLE) {
        vkDestroySampler(device, own.nearest, nullptr);
    }
    if (state.memory.ready()) {
        state.memory.destroy_image(&own.coats);
        state.memory.destroy_image(&own.places);
    }
    const PaintSettings settings = own.settings;
    own = PaintState{};
    own.settings = settings;
}

} // namespace render::vulkan
