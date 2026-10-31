// The graphics options (thug2-decomp.ini).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "engine/gfx/settings.h"

#include "engine/gfx/graphics_options.h"
#include "engine/gfx/occlusion.h"

#include <cstdint>

#include "platform/platform_config.h"
#include "engine/gfx/device.h"
#include "engine/gfx/view_cam.h"

namespace render {

uint8_t g_hq_shadows;
bool g_fog_setting;
bool g_distance_clipping;

namespace {

struct GraphicsSettings {
    uint32_t antialiasing;
    uint32_t hq_shadows_;
    uint32_t distance_clipping_;
    uint32_t clipping_distance_; // 1-100
    uint32_t fog;
};
GraphicsSettings s_settings;
bool s_blur_disabled = true;

} // namespace

bool& blur_disabled() { return s_blur_disabled; }

void load_graphics_settings()
{
    s_settings.antialiasing = build_info::fetch_flag(build_info::kGraphics, "AntiAliasing", false);
    s_settings.hq_shadows_ = build_info::fetch_flag(build_info::kGraphics, "HQShadows", false);
    s_settings.distance_clipping_ = build_info::fetch_flag(build_info::kGraphics, "DistanceClipping", false);
    s_settings.clipping_distance_ = static_cast<uint32_t>(build_info::get_int(build_info::kGraphics, "ClippingDistance", 100));
    s_settings.fog = build_info::fetch_flag(build_info::kGraphics, "Fog", false);
    s_blur_disabled = build_info::fetch_flag(build_info::kGraphics, "DisableBlur", true);
    // (NOT ORIGINAL: engine/gfx/occlusion.h)
    occlusion_off() = !build_info::fetch_flag(build_info::kGraphics, "OcclusionCulling", true);
    // (NOT ORIGINAL: anisotropy, msaa, draw_distance_scale, lod_distance_scale - engine/gfx/graphics_options.h)
    graphics_options::read_in();
}

void write_graphics_settings()
{
    reinterpret_cast<uint8_t&>(g_antialiasing) = static_cast<uint8_t>(s_settings.antialiasing);
    g_hq_shadows = static_cast<uint8_t>(s_settings.hq_shadows_);
    g_distance_clipping = static_cast<uint8_t>(s_settings.distance_clipping_);
    uint32_t distance = s_settings.clipping_distance_;
    if (distance > 100)
        distance = 100;
    else if (distance < 1)
        distance = 1;
    // (the engine's distance: 100 to 595)
    reinterpret_cast<uint32_t&>(gfx::g_draw_distance_setting) = static_cast<uint32_t>(static_cast<float>(distance) * 5.0f + 95.0f);
    g_fog_setting = static_cast<uint8_t>(s_settings.fog);
}

} // namespace Nx
