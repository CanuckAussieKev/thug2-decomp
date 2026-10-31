// The graphics options (thug2-decomp.ini).
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md

#include "game/gfx/settings.h"

#include "game/gfx/graphics_options.h"
#include "game/gfx/occlusion.h"

#include <cstdint>

#include "game/sys/config.h"
#include "game/gfx/device.h"
#include "game/gfx/camera.h"

namespace Engine3D {

uint8_t g_hqShadows;
bool g_fogSetting;
bool g_distanceClipping;

namespace {

struct GraphicsSettings {
    uint32_t antialiasing;
    uint32_t hqShadows;
    uint32_t distanceClipping;
    uint32_t clippingDistance; // 1-100
    uint32_t fog;
};
GraphicsSettings s_settings;
bool s_blurDisabled = true;

} // namespace

bool& BlurDisabled() { return s_blurDisabled; }

void LoadGraphicsSettings()
{
    s_settings.antialiasing = BuildInfo::FetchFlag(BuildInfo::kGraphics, "AntiAliasing", false);
    s_settings.hqShadows = BuildInfo::FetchFlag(BuildInfo::kGraphics, "HQShadows", false);
    s_settings.distanceClipping = BuildInfo::FetchFlag(BuildInfo::kGraphics, "DistanceClipping", false);
    s_settings.clippingDistance = static_cast<uint32_t>(BuildInfo::GetInt(BuildInfo::kGraphics, "ClippingDistance", 100));
    s_settings.fog = BuildInfo::FetchFlag(BuildInfo::kGraphics, "Fog", false);
    s_blurDisabled = BuildInfo::FetchFlag(BuildInfo::kGraphics, "DisableBlur", true);
    // (NOT ORIGINAL: game/gfx/occlusion.h)
    OcclusionOff() = !BuildInfo::FetchFlag(BuildInfo::kGraphics, "OcclusionCulling", true);
    // (NOT ORIGINAL: anisotropy, msaa, draw_distance_scale, lod_distance_scale - game/gfx/graphics_options.h)
    GraphicsOptions::ReadIn();
}

void WriteGraphicsSettings()
{
    reinterpret_cast<uint8_t&>(g_antialiasing) = static_cast<uint8_t>(s_settings.antialiasing);
    g_hqShadows = static_cast<uint8_t>(s_settings.hqShadows);
    g_distanceClipping = static_cast<uint8_t>(s_settings.distanceClipping);
    uint32_t distance = s_settings.clippingDistance;
    if (distance > 100)
        distance = 100;
    else if (distance < 1)
        distance = 1;
    // (the engine's distance: 100 to 595)
    reinterpret_cast<uint32_t&>(Gfx2D::g_drawDistanceSetting) = static_cast<uint32_t>(static_cast<float>(distance) * 5.0f + 95.0f);
    g_fogSetting = static_cast<uint8_t>(s_settings.fog);
}

} // namespace Nx
