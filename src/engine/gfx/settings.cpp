// The graphics options (thug2-decomp.ini).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "engine/gfx/settings.h"

#include "engine/gfx/graphics_options.h"
#include "engine/gfx/occlusion.h"

#include <cstdint>

#include "engine/config/settings_model.h"
#include "engine/config/settings_state.h"
#include "engine/gfx/view_cam.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"
#include "engine/pad_input/port_controls.h"

namespace render {

constexpr int kMaxDrawDistanceSetting = 100; // the clipping distance setting: 1-100

namespace {

struct GraphicsSettings {
    uint32_t antialiasing;
    uint32_t hq_shadows_;
    uint32_t distance_clipping_;
    uint32_t clipping_distance_; // 1-100
    uint32_t fog;
};
// NOT ORIGINAL grouping - settings.cpp's state (the original's globals). Made on first use, never destroyed
// (settings_state()).
struct SettingsState {
    GraphicsSettings settings_{};
    bool blur_disabled_ = true;
};

SettingsState& settings_state()
{
    static core::NoDestroy<SettingsState> state;
    return state.get();
}

} // namespace

bool& blur_disabled() { return settings_state().blur_disabled_; }

void load_graphics_settings()
{
    // (NOT ORIGINAL: read through the settings model - engine/config/settings_state.h - which has AntiAliasing out of
    // effect while upscaling or while msaa picks the samples)
    const config::Resolution& in_effect = config::running();
    settings_state().settings_.antialiasing = static_cast<uint32_t>(in_effect.on("AntiAliasing"));
    settings_state().settings_.hq_shadows_ = static_cast<uint32_t>(in_effect.on("HQShadows"));
    settings_state().settings_.distance_clipping_ = static_cast<uint32_t>(in_effect.on("DistanceClipping"));
    settings_state().settings_.clipping_distance_ =
        static_cast<uint32_t>(in_effect.integer("ClippingDistance", kMaxDrawDistanceSetting));
    settings_state().settings_.fog = static_cast<uint32_t>(in_effect.on("Fog"));
    settings_state().blur_disabled_ = in_effect.on("DisableBlur");
    // (NOT ORIGINAL: engine/gfx/occlusion.h)
    occlusion_off() = !in_effect.on("OcclusionCulling");
    // (NOT ORIGINAL: anisotropy, msaa, draw_distance_scale, lod_distance_scale - engine/gfx/graphics_options.h)
    graphics_options::read_in();
}

void write_graphics_settings()
{
    renderer().set_antialiasing(static_cast<uint8_t>(settings_state().settings_.antialiasing) != 0);
    view_setup().set_hq_shadows(static_cast<uint8_t>(settings_state().settings_.hq_shadows_));
    view_setup().set_distance_clipping(static_cast<uint8_t>(settings_state().settings_.distance_clipping_) != 0U);
    uint32_t distance = settings_state().settings_.clipping_distance_;
    if (distance > kMaxDrawDistanceSetting) {
        distance = kMaxDrawDistanceSetting;
    } else if (distance < 1) {
        distance = 1;
    }
    // (the engine's distance: 100 to 595)
    gfx::set_draw_distance_setting(static_cast<int>(static_cast<uint32_t>(
        (static_cast<float>(distance) * os_layer::kDrawDistanceStep) + os_layer::kDrawDistanceBase)));
    view_setup().set_fog_setting(static_cast<uint8_t>(settings_state().settings_.fog) != 0U);
}

} // namespace render
