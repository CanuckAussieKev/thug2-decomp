// NOT ORIGINAL - ray tracing's contract (rt_scene.h): the installed backend, the frame record and the ids.
#include "engine/gfx/rt_scene.h"

#include "base/core/checked_index.h"
#include "engine/gfx/gpu.h"
#include "platform/frame_profile.h"
#include "engine/gfx/rt_sun.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
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
    const frame_profile::Scope phase("fx_opaque"); // (where a frame's time goes - platform/frame_profile.h)
    for (ViewListener* listener : view_listeners()) {
        listener->opaque_drawn(view);
    }
}

void world_view_drawn(const ViewDrawn& view)
{
    const frame_profile::Scope phase("fx_world");
    for (ViewListener* listener : view_listeners()) {
        listener->world_drawn(view);
    }
}

bool view_listened() { return !view_listeners().empty(); }

namespace {
struct FocusPoint {
    std::array<float, 3> point{};
    std::chrono::steady_clock::time_point noted;
    bool known = false;
};
FocusPoint& focus_slot()
{
    static FocusPoint focus;
    return focus;
}
constexpr std::chrono::milliseconds kFocusFresh{500};
} // namespace

void note_focus_point(const std::array<float, 3>& point)
{
    FocusPoint& focus = focus_slot();
    focus.point = point;
    focus.noted = std::chrono::steady_clock::now();
    focus.known = true;
}

bool focus_point(std::array<float, 3>* point)
{
    const FocusPoint& focus = focus_slot();
    if (!focus.known || std::chrono::steady_clock::now() - focus.noted > kFocusFresh) {
        return false;
    }
    *point = focus.point;
    return true;
}

void view_begins(const ViewDrawn& view)
{
    const frame_profile::Scope phase("fx_view_begins");
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

namespace {
ViewLens& view_lens_slot()
{
    static ViewLens lens;
    return lens;
}
} // namespace

void set_view_lens(const ViewLens& lens) { view_lens_slot() = lens; }
ViewLens view_lens() { return view_lens_slot(); }

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
std::atomic<bool>& depth_sampling_slot()
{
    static std::atomic<bool> wanted{false};
    return wanted;
}
} // namespace

void set_depth_sampling_wanted(bool wanted) { depth_sampling_slot().store(wanted, std::memory_order_relaxed); }
bool depth_sampling_wanted() { return depth_sampling_slot().load(std::memory_order_relaxed); }

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

void set_baked_sun(BakedSun sun)
{
    baked_slot().sun = std::move(sun);
    set_sun_agreement({}); // (added by the scene's sun: another fit - the other sun's grid no longer holds)
}
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

// --- (added by the scene's sun) ---------------------------------------------------------------------------------------

namespace {
constexpr float kSameDirection = 0.99999F; // two directions this close (their dot product): the same
struct SceneSunSlot {
    std::mutex mutex;
    SunChoice choice;
    SunRelight relight;
    SceneSun last;     // (the serial's: what scene_sun gave last)
    std::array<SunUsed, static_cast<std::size_t>(SunUser::kCount)> used{};
    BakedSun agreement; // the scene's sun's grid when it isn't the baked one
};
SceneSunSlot& scene_sun_slot()
{
    static SceneSunSlot slot;
    return slot;
}
float dot3(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    return (std::get<0>(a) * std::get<0>(b)) + (std::get<1>(a) * std::get<1>(b)) + (std::get<2>(a) * std::get<2>(b));
}
SunInputs scene_inputs(const SceneSunSlot& slot)
{
    SunInputs in{.choice = slot.choice, .relight = slot.relight};
    in.override_set = sun_override(&in.override_toward);
    in.game_set = game_sun(&in.game_toward);
    const BakedSun& baked = baked_sun();
    in.baked_scene = baked.scene;
    in.baked_found = baked.found;
    in.baked_toward = baked.toward;
    return in;
}
} // namespace

void set_sun_choice(const SunChoice& choice)
{
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.choice = choice;
}

SunChoice sun_choice()
{
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.choice;
}

void set_sun_relight(const SunRelight& relight)
{
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.relight = relight;
}

SunRelight sun_relight()
{
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    return slot.relight;
}

SceneSun scene_sun()
{
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    SceneSun now = resolve_scene_sun(scene_inputs(slot));
    if (now.origin != slot.last.origin || (now.found() && dot3(now.toward, slot.last.toward) < kSameDirection)) {
        now.serial = slot.last.serial + 1;
        slot.last = now;
    } else {
        now = slot.last;
    }
    return now;
}

SceneSun relit_target_sun()
{
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    SunInputs in = scene_inputs(slot);
    in.relight.relights = true;
    return resolve_scene_sun(in);
}

const char* sun_origin_name(SunOrigin origin)
{
    switch (origin) {
    case SunOrigin::kNone:
        return "none (the bake shows no sun)";
    case SunOrigin::kWaiting:
        return "waiting for the baked sun's fit";
    case SunOrigin::kOverride:
        return "the console's";
    case SunOrigin::kGame:
        return "the game's";
    case SunOrigin::kLevel:
        return "the level's baked sun";
    case SunOrigin::kFixed:
        return "rt_sun_source's fixed direction";
    }
    return "?";
}

const char* sun_user_name(SunUser user)
{
    switch (user) {
    case SunUser::kShadows:
        return "shadows";
    case SunUser::kBounce:
        return "bounce";
    case SunUser::kFog:
        return "fog";
    case SunUser::kDrawnSun:
        return "drawn sun";
    case SunUser::kSky:
        return "sky";
    case SunUser::kRelit:
        return "relit";
    case SunUser::kCount:
        break;
    }
    return "?";
}

void note_sun_used(SunUser user, bool have, const std::array<float, 3>& toward)
{
    if (user >= SunUser::kCount) {
        return;
    }
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    core::at(slot.used, static_cast<std::size_t>(user)) = {.noted = true, .have = have, .toward = toward};
}

SunUsed sun_used(SunUser user)
{
    if (user >= SunUser::kCount) {
        return {};
    }
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    return core::at(slot.used, static_cast<std::size_t>(user));
}

void clear_sun_used()
{
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.used = {};
}

void set_sun_agreement(BakedSun agreement)
{
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    slot.agreement = std::move(agreement);
}

const BakedSun* agreement_for(const std::array<float, 3>& toward)
{
    const BakedSun& baked = baked_sun();
    if (baked.scene != 0 && baked.found && dot3(baked.toward, toward) >= kSameDirection) {
        return &baked;
    }
    SceneSunSlot& slot = scene_sun_slot();
    const std::scoped_lock lock(slot.mutex);
    if (!slot.agreement.agreement.empty() && dot3(slot.agreement.toward, toward) >= kSameDirection) {
        return &slot.agreement;
    }
    return nullptr;
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
    const frame_profile::Scope phase("fx_sky");
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
