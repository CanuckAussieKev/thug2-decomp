// NOT ORIGINAL - ray tracing's contract (rt_scene.h): the installed backend, the frame record and the ids.
#include "engine/gfx/rt_scene.h"

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace render::rt {

namespace {
constexpr float kLeastViewScale = 0.25F;
constexpr double kRoundHalf = 0.5;
} // namespace

namespace {
RayTracing*& backend_slot()
{
    static RayTracing* backend = nullptr;
    return backend;
}
} // namespace

RayTracing* ray_tracing() { return backend_slot(); }
void set_ray_tracing(RayTracing* backend) { backend_slot() = backend; }

FrameRecord& frame_record()
{
    static FrameRecord record;
    return record;
}

MeshId next_mesh_id()
{
    static MeshId last = 0;
    return ++last;
}

MaterialId next_material_id()
{
    static MaterialId last = 0;
    return ++last;
}

namespace {
std::vector<ViewListener*>& view_listeners()
{
    static std::vector<ViewListener*> listeners;
    return listeners;
}
} // namespace

void add_view_listener(ViewListener* listener)
{
    std::vector<ViewListener*>& listeners = view_listeners();
    if (listener != nullptr && std::ranges::find(listeners, listener) == listeners.end()) {
        // (by order(); after those of the same order - as added)
        const auto at = std::ranges::find_if(
            listeners, [&](const ViewListener* other) { return other->order() > listener->order(); });
        listeners.insert(at, listener);
    }
}

void remove_view_listener(ViewListener* listener) { std::erase(view_listeners(), listener); }

void opaque_view_drawn(const ViewDrawn& view)
{
    for (ViewListener* listener : view_listeners()) {
        listener->opaque_drawn(view);
    }
}

void world_view_drawn(const ViewDrawn& view)
{
    for (ViewListener* listener : view_listeners()) {
        listener->world_drawn(view);
    }
}

bool view_listened() { return !view_listeners().empty(); }

void view_begins(const ViewDrawn& view)
{
    for (ViewListener* listener : view_listeners()) {
        listener->view_begins(view);
    }
}

namespace {
ViewScale& view_scale_slot()
{
    static ViewScale scale;
    return scale;
}
} // namespace

void set_view_scale(const ViewScale& scale) { view_scale_slot() = scale; }
ViewScale view_scale() { return view_scale_slot(); }

std::array<uint32_t, 4> scaled_rect(const std::array<uint32_t, 4>& output, float scale)
{
    const float kept = std::clamp(scale, kLeastViewScale, 1.0F);
    const auto part = [kept](uint32_t size) {
        return std::max(1U, static_cast<uint32_t>((static_cast<double>(size) * kept) + kRoundHalf));
    };
    return {core::at(output, 0), core::at(output, 1), part(core::at(output, 2)), part(core::at(output, 3))};
}

void jitter_projection(std::array<std::array<float, 4>, 4>* projection, std::array<float, 2> jitter, uint32_t width,
                       uint32_t height)
{
    if (width == 0 || height == 0) {
        return;
    }
    const float x = 2.0F * core::at(jitter, 0) / static_cast<float>(width);
    const float y = -2.0F * core::at(jitter, 1) / static_cast<float>(height);
    for (std::array<float, 4>& row : *projection) {
        core::at(row, 0) += x * core::at(row, 3);
        core::at(row, 1) += y * core::at(row, 3);
    }
}

namespace {
std::atomic<bool>& swap_slot()
{
    static std::atomic<bool> swapping{false};
    return swapping;
}
} // namespace

void set_settings_swap(bool swapping) { swap_slot().store(swapping, std::memory_order_relaxed); }
bool settings_swap() { return swap_slot().load(std::memory_order_relaxed); }

namespace {
bool& hdr_slot()
{
    static bool on = false;
    return on;
}
} // namespace

bool hdr_colour() { return hdr_slot(); }
void set_hdr_colour(bool on) { hdr_slot() = on; }

uint32_t world_target_format(uint32_t format)
{
    if (!hdr_colour()) {
        return format;
    }
    if (format == gpu_value::kFormatA8R8G8B8) {
        return gpu_value::kFormatA16B16G16R16F;
    }
    if (format == gpu_value::kFormatX8R8G8B8) {
        return gpu_value::kFormatX16B16G16R16F;
    }
    return format;
}

namespace {
struct SunOverride {
    bool set = false;
    std::array<float, 3> toward{};
};
SunOverride& sun_slot()
{
    static SunOverride sun;
    return sun;
}
} // namespace

void set_sun_override(const std::array<float, 3>* toward_sun)
{
    sun_slot() = toward_sun == nullptr ? SunOverride{} : SunOverride{.set = true, .toward = *toward_sun};
}

namespace {
struct BakedSunSlot {
    BakedSun sun;
    bool wanted = false;
};
BakedSunSlot& baked_slot()
{
    static BakedSunSlot slot;
    return slot;
}
} // namespace

void set_baked_sun(BakedSun sun) { baked_slot().sun = std::move(sun); }
const BakedSun& baked_sun() { return baked_slot().sun; }
void set_baked_sun_wanted(bool wanted) { baked_slot().wanted = wanted; }
bool baked_sun_wanted() { return baked_slot().wanted; }

bool sun_override(std::array<float, 3>* toward_sun)
{
    if (sun_slot().set && toward_sun != nullptr) {
        *toward_sun = sun_slot().toward;
    }
    return sun_slot().set;
}

namespace {
struct SunLightSlot {
    std::mutex mutex;
    SunLight light;
};
SunLightSlot& sun_light_slot()
{
    static SunLightSlot slot;
    return slot;
}
std::atomic<bool>& glare_slot()
{
    static std::atomic<bool> replaced{false};
    return replaced;
}
std::atomic<bool>& object_shadows_slot()
{
    static std::atomic<bool> replaced{false};
    return replaced;
}
} // namespace

void set_sun_light(const SunLight& light)
{
    SunLightSlot& slot = sun_light_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.light = light;
}

SunLight sun_light()
{
    SunLightSlot& slot = sun_light_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.light;
}

void set_sun_glare_replaced(bool replaced) { glare_slot().store(replaced, std::memory_order_relaxed); }
bool sun_glare_replaced() { return glare_slot().load(std::memory_order_relaxed); }

void set_object_shadows_replaced(bool replaced) { object_shadows_slot().store(replaced, std::memory_order_relaxed); }
bool object_shadows_replaced() { return object_shadows_slot().load(std::memory_order_relaxed); }

namespace {
SunOverride& game_sun_slot()
{
    static SunOverride sun;
    return sun;
}
} // namespace

void set_game_sun(const std::array<float, 3>* toward_sun)
{
    game_sun_slot() = toward_sun == nullptr ? SunOverride{} : SunOverride{.set = true, .toward = *toward_sun};
}

bool game_sun(std::array<float, 3>* toward_sun)
{
    if (game_sun_slot().set && toward_sun != nullptr) {
        *toward_sun = game_sun_slot().toward;
    }
    return game_sun_slot().set;
}

namespace {
struct SkySlot {
    std::mutex mutex;
    SkyReplacement replacement;
    bool capture_wanted = false;
    SkyCapture capture;
};
SkySlot& sky_slot()
{
    static SkySlot slot;
    return slot;
}
} // namespace

void set_sky_replacement(const SkyReplacement& replacement)
{
    SkySlot& slot = sky_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.replacement = replacement;
}

SkyReplacement sky_replacement()
{
    SkySlot& slot = sky_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.replacement;
}

void sky_view_drawn(const ViewDrawn& view)
{
    for (ViewListener* listener : view_listeners()) {
        listener->sky_drawn(view);
    }
}

void set_sky_capture_wanted(bool wanted)
{
    SkySlot& slot = sky_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.capture_wanted = wanted;
}

bool sky_capture_wanted()
{
    SkySlot& slot = sky_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.capture_wanted;
}

void set_sky_capture(const SkyCapture& capture)
{
    SkySlot& slot = sky_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.capture = capture;
}

SkyCapture sky_capture()
{
    SkySlot& slot = sky_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.capture;
}

} // namespace render::rt
