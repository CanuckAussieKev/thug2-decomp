// The graphics options (thug2-decomp.ini).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "engine/gfx/settings.h"

#include "engine/gfx/graphics_options.h"
#include "engine/gfx/occlusion.h"

#include <cstdint>

#include "platform/platform_config.h"
#include "engine/gfx/device.h"
#include "engine/gfx/view_cam.h"
#include "engine/core/no_destroy.h"

namespace render {

namespace {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed
// (graphics_option_flags()).
struct GraphicsOptionFlags {
    uint8_t hq_shadows_{};
    bool fog_setting_{};
    bool distance_clipping_{};
};

GraphicsOptionFlags& graphics_option_flags()
{
    static core::NoDestroy<GraphicsOptionFlags> state;
    return state.get();
}

} // namespace

uint8_t hq_shadows() { return graphics_option_flags().hq_shadows_; }
void set_hq_shadows(uint8_t value) { graphics_option_flags().hq_shadows_ = value; }
bool fog_setting() { return graphics_option_flags().fog_setting_; }
void set_fog_setting(bool value) { graphics_option_flags().fog_setting_ = value; }
bool distance_clipping() { return graphics_option_flags().distance_clipping_; }
void set_distance_clipping(bool value) { graphics_option_flags().distance_clipping_ = value; }

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
    settings_state().settings_.antialiasing = build_info::fetch_flag(build_info::kGraphics, "AntiAliasing", false);
    settings_state().settings_.hq_shadows_ = build_info::fetch_flag(build_info::kGraphics, "HQShadows", false);
    settings_state().settings_.distance_clipping_ = build_info::fetch_flag(build_info::kGraphics, "DistanceClipping", false);
    settings_state().settings_.clipping_distance_ =
        static_cast<uint32_t>(build_info::get_int(build_info::kGraphics, "ClippingDistance", 100));
    settings_state().settings_.fog = build_info::fetch_flag(build_info::kGraphics, "Fog", false);
    settings_state().blur_disabled_ = build_info::fetch_flag(build_info::kGraphics, "DisableBlur", true);
    // (NOT ORIGINAL: engine/gfx/occlusion.h)
    occlusion_off() = !build_info::fetch_flag(build_info::kGraphics, "OcclusionCulling", true);
    // (NOT ORIGINAL: anisotropy, msaa, draw_distance_scale, lod_distance_scale - engine/gfx/graphics_options.h)
    graphics_options::read_in();
}

void write_graphics_settings()
{
    set_antialiasing(static_cast<uint8_t>(settings_state().settings_.antialiasing) != 0);
    graphics_option_flags().hq_shadows_ = static_cast<uint8_t>(settings_state().settings_.hq_shadows_);
    graphics_option_flags().distance_clipping_ = static_cast<uint8_t>(settings_state().settings_.distance_clipping_);
    uint32_t distance = settings_state().settings_.clipping_distance_;
    if (distance > 100)
        distance = 100;
    else if (distance < 1)
        distance = 1;
    // (the engine's distance: 100 to 595)
    gfx::set_draw_distance_setting(static_cast<int>(static_cast<uint32_t>(static_cast<float>(distance) * 5.0f + 95.0f)));
    graphics_option_flags().fog_setting_ = static_cast<uint8_t>(settings_state().settings_.fog);
}

} // namespace render
