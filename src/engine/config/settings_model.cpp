// NOT ORIGINAL - the settings model's declarations (settings_model.h): every setting once, in the Options screen's
// order within each tab, with its rules. The words are settings_text.h's; the values and defaults README's settings
// table (thug2_options_check holds them against each other).

#include "engine/config/settings_model.h"

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/config/settings_text.h"

namespace config {

namespace {

namespace t = config::text;

// A declaration under construction: Decl("Graphics", "rt_shadows", Kind::kSwitch, "0").tab(...).needs(...).
class Decl {
public:
    Decl(const char* section, const char* key, Kind kind, const char* fallback)
    {
        setting_.section = section;
        setting_.key = key;
        setting_.kind = kind;
        setting_.fallback = fallback;
        if (kind == Kind::kSwitch) {
            setting_.off = "0";
            setting_.choices = {{.value = "0", .label = t::kOff}, {.value = "1", .label = t::kOn}};
        }
    }
    // Offered by the Options screen in `group`'s tab.
    Decl&& tab(Group group, const char* label, const char* help = "") &&
    {
        setting_.group = group;
        setting_.label = label;
        setting_.help = help;
        return std::move(*this);
    }
    Decl&& pick(std::vector<Choice> choices) &&
    {
        setting_.choices = std::move(choices);
        return std::move(*this);
    }
    // Put to use at once: the renderer's part `effect` read again.
    Decl&& live(Effect effect = Effect::kNone) &&
    {
        setting_.apply = Apply::kLive;
        setting_.effect = effect;
        return std::move(*this);
    }
    Decl&& preset() &&
    {
        setting_.apply = Apply::kPreset;
        return std::move(*this);
    }
    Decl&& need(Cond cond, const char* reason = nullptr) &&
    {
        setting_.needs.push_back({.cond = std::move(cond), .reason = reason});
        return std::move(*this);
    }
    Decl&& off(const char* value) &&
    {
        setting_.off = value;
        return std::move(*this);
    }
    Decl&& open() &&
    {
        setting_.open = true;
        return std::move(*this);
    }
    Decl&& alias(const char* from, const char* to) &&
    {
        setting_.aliases.push_back({.from = from, .to = to});
        return std::move(*this);
    }
    Decl&& retire(const char* why) &&
    {
        setting_.retired = why;
        return std::move(*this);
    }
    [[nodiscard]] Setting take() && { return std::move(setting_); }

private:
    Setting setting_;
};

Decl gfx(const char* key, Kind kind, const char* fallback) { return {"Graphics", key, kind, fallback}; }
Decl feature(const char* key, Kind kind, const char* fallback) { return {"features", key, kind, fallback}; }
Decl debug(const char* key, Kind kind, const char* fallback) { return {"debug", key, kind, fallback}; }

// Numbers' choices: each value its own label ("2x" / "50%" written out where the screen shows them so).
Choice c(const char* value, const char* label) { return {.value = value, .label = label}; }

// --- the conditions every effect shares ---

Cond vulkan() { return is("renderer", {"vulkan"}); }
Cond ray_tracing() { return on("ray_tracing"); }

// The time of day runs (engine/gfx/rt_daylight.h tod_may_run): a time other than the level's, the clock running, or a
// level of tod_levels given a time of its own.
bool tod_levels_set(const Reader& reader)
{
    const std::string list = reader.value("tod_levels");
    std::string_view rest(list);
    while (!rest.empty()) {
        const size_t comma = rest.find(',');
        const std::string_view entry = rest.substr(0, comma);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        const size_t colon = entry.find(':');
        if (colon == std::string_view::npos) {
            break; // (as parse_tod_levels: the entries before a bad one used)
        }
        std::string_view tod = entry.substr(colon + 1);
        while (!tod.empty() && tod.front() == ' ') {
            tod.remove_prefix(1);
        }
        while (!tod.empty() && tod.back() == ' ') {
            tod.remove_suffix(1);
        }
        if (!tod.empty() && tod != "level") {
            return true;
        }
    }
    return false;
}
Cond tod_runs()
{
    return any({is_not("tod", {"level"}), is("tod_cycle", {"on"}), computed(&tod_levels_set, {"tod_levels"})});
}

// rt_sun_source a fixed direction (x,y,z or heading,elevation): it beats the clock's sun (rt_sun.h resolve_scene_sun).
bool sun_fixed(const Reader& reader)
{
    const std::string source = reader.value("rt_sun_source");
    return source != "level" && source != "game";
}

// DLSS upscales: asked for (its choice needs an RTX), or auto on an RTX (vulkan_upscale.cpp: DLSS first on NVIDIA).
Cond dlss_upscaling() { return any({is("upscaler", {"dlss"}), all({is("upscaler", {"auto"}), has(Cap::kRtx)})}); }
Cond fsr_upscaling() { return any({is("upscaler", {"fsr"}), all({is("upscaler", {"auto"}), negate(has(Cap::kRtx))})}); }
// DLSS frame generation makes the frames (an RTX 40 / 50, not told to use FSR's).
Cond dlss_frames()
{
    return all({is_not("frame_generation", {"off"}), has(Cap::kRtx40), negate(asked_is("frame_generation_provider", {"fsr"}))});
}

// More frames shown than the display shows: max_fps (0: the original's 60, uncapped: no limit) times frame
// generation's multiplier, past its refresh rate.
bool past_refresh(const Reader& reader)
{
    const int refresh = reader.caps().refresh_hz;
    const std::string mode = reader.value("frame_generation");
    if (refresh <= 0 || mode.size() != 2 || mode.back() != 'x') {
        return false;
    }
    const double multiplier = mode.front() - '0';
    constexpr double kRounding = 0.5; // (a refresh rate of 143.9 Hz is 144)
    const std::string fps = reader.value("max_fps");
    if (fps == "uncapped") {
        return true;
    }
    constexpr double kOriginalRate = 60.0;
    char* end = nullptr;
    const double asked = std::strtod(fps.c_str(), &end);
    const double rate = end == fps.c_str() || asked <= 0.0 ? kOriginalRate : asked;
    return rate * multiplier > static_cast<double>(refresh) + kRounding;
}

std::vector<Choice> percent(const std::vector<std::pair<const char*, const char*>>& list)
{
    std::vector<Choice> choices;
    choices.reserve(list.size());
    for (const auto& [value, label] : list) {
        choices.push_back(c(value, label));
    }
    return choices;
}

std::vector<Decl> declarations()
{
    using G = Group;
    using E = Effect;
    using K = Kind;
    return {
        // ===== Graphics: the preset, the renderer (app/renderer_choice.h), the frame pacing (engine/sys/frame_pacing.h),
        // engine/gfx/graphics_options.h, smooth curves (render_pc/mesh_smoothing.h)
        gfx("graphics_preset", K::kChoice, "")
            .tab(G::kGraphics, t::kOptPreset, t::kOptPresetHelp)
            .pick({c("", t::kPresetCustom), c("original", t::kPresetOriginal), c("high", t::kPresetHigh), c("ultra", t::kPresetUltra)})
            .preset(),
        gfx("renderer", K::kChoice, "vulkan")
            .tab(G::kGraphics, t::kOptRenderer, t::kOptRendererHelp)
            .pick({{.value = "vulkan", .label = "Vulkan", .needs = {{.cond = has(Cap::kVulkan), .reason = t::kNoVulkanGpu}}, .instead = "d3d9"},
                   c("d3d9", "Direct3D 9")}),
        gfx("ray_tracing", K::kSwitch, "0")
            .tab(G::kGraphics, t::kOptRayTracing, t::kOptRayTracingHelp)
            .need(vulkan(), t::kNeedsVulkan)
            .need(has(Cap::kRayQueries))
            .need(has(Cap::k64Bit)),
        // (engine/gfx/texture_sets.h: the upscaled sets tools/pipeline/hd_textures/base.ts installs - offered when installed)
        gfx("texture_set", K::kChoice, "original")
            .tab(G::kGraphics, t::kOptTextureSet, t::kOptTextureSetHelp)
            .pick({c("original", t::kTextureSetOriginal),
                   {.value = "pbrify", .label = "PBRify", .needs = {{.cond = installed("pbrify"), .reason = t::kNotInstalled}}},
                   {.value = "seedvr2", .label = "SeedVR2", .needs = {{.cond = installed("seedvr2"), .reason = t::kNotInstalled}}}})
            .live(),
        gfx("max_fps", K::kChoice, "0")
            .tab(G::kGraphics, t::kOptMaxFps, t::kOptMaxFpsHelp)
            .pick({c("0", t::kMaxFpsOriginal), c("120", "120"), c("144", "144"), c("165", "165"), c("240", "240"), c("uncapped", t::kMaxFpsUncapped)})
            .open()
            .live(),
        // (frame generation and Reflex: gpu_vulkan/vulkan_framegen.h - FSR's makes 2x at most, DLSS-G's 4x on an RTX 50)
        gfx("frame_generation", K::kChoice, "off")
            .tab(G::kGraphics, t::kOptFrameGeneration, t::kOptFrameGenerationHelp)
            .pick({c("off", t::kOff),
                   c("2x", "2x"),
                   {.value = "3x", .label = "3x", .needs = {{.cond = all({has(Cap::kRtx50), negate(asked_is("frame_generation_provider", {"fsr"}))}), .reason = t::kMultiNeedsRtx50}}, .instead = "2x"},
                   {.value = "4x", .label = "4x", .needs = {{.cond = all({has(Cap::kRtx50), negate(asked_is("frame_generation_provider", {"fsr"}))}), .reason = t::kMultiNeedsRtx50}}, .instead = "2x"}})
            .need(vulkan(), t::kNeedsVulkan)
            .need(has(Cap::k64Bit))
            .off("off"),
        gfx("frame_generation_provider", K::kChoice, "auto")
            .pick({c("auto", t::kAuto),
                   {.value = "dlss", .label = "DLSS", .needs = {{.cond = has(Cap::kRtx40), .reason = t::kDlssgNeedsRtx40}}, .instead = "fsr"},
                   c("fsr", "FSR")})
            .need(is_not("frame_generation", {"off"}))
            .off("auto"),
        gfx("reflex", K::kChoice, "off")
            .tab(G::kGraphics, t::kOptReflex, t::kOptReflexHelp)
            .pick({c("off", t::kOff), c("on", t::kOn), c("boost", t::kReflexBoost)})
            .need(vulkan(), t::kNeedsVulkan)
            .need(has(Cap::k64Bit))
            .need(has(Cap::kNvidia))
            .off("off"),
        gfx("present_mode", K::kChoice, "vsync")
            .tab(G::kGraphics, t::kOptPresentMode, t::kOptPresentModeHelp)
            .pick({c("vsync", t::kOn), c("mailbox", t::kPresentMailbox), c("immediate", t::kPresentImmediate)})
            .live(),
        // (the frame-rate counter: devtools/fps_counter.h)
        gfx("show_fps", K::kSwitch, "0").tab(G::kGraphics, t::kOptShowFps, t::kOptShowFpsHelp).live(),
        gfx("anisotropy", K::kNumber, "0")
            .tab(G::kGraphics, t::kOptAnisotropy, t::kOptAnisotropyHelp)
            .pick({c("0", t::kOff), c("2", "2x"), c("4", "4x"), c("8", "8x"), c("16", "16x")}),
        // (multisampling: off while upscaling - the upscaler's back buffer is single-sampled)
        gfx("msaa", K::kNumber, "0")
            .tab(G::kGraphics, t::kOptMsaa, t::kOptMsaaHelp)
            .pick({c("0", t::kOff), c("2", "2x"), c("4", "4x"), c("8", "8x")})
            .need(is("upscaler", {"off"}), t::kOffWhileUpscaling)
            .off("0"),
        gfx("draw_distance_scale", K::kNumber, "1")
            .tab(G::kGraphics, t::kOptDrawDistance, t::kOptDrawDistanceHelp)
            .pick({c("1", "1x"), c("1.5", "1.5x"), c("2", "2x"), c("3", "3x")}),
        gfx("lod_distance_scale", K::kNumber, "1")
            .tab(G::kGraphics, t::kOptLodDistance, t::kOptLodDistanceHelp)
            .pick({c("1", "1x"), c("1.5", "1.5x"), c("2", "2x"), c("3", "3x")}),
        gfx("rt_tessellation", K::kSwitch, "0").tab(G::kGraphics, t::kOptTessellation, t::kOptTessellationHelp),
        gfx("rt_tess_max_deviation", K::kNumber, "2")
            .tab(G::kGraphics, t::kOptTessDeviation, t::kOptTessDeviationHelp)
            .pick({c("1", "1"), c("2", "2"), c("3", "3"), c("4", "4")})
            .need(on("rt_tessellation")),
        // (smooth_tess.h "Smoothing": rails and poles rounded, hard edges kept by the meshes' normals)
        gfx("rt_tess_smooth", K::kSwitch, "0").tab(G::kGraphics, t::kOptTessSmooth, t::kOptTessSmoothHelp).need(on("rt_tessellation")),
        gfx("rt_tess_smooth_strength", K::kNumber, "1")
            .tab(G::kGraphics, t::kOptTessSmoothStrength, t::kOptTessSmoothStrengthHelp)
            .pick({c("0.5", "50%"), c("0.75", "75%"), c("1", "100%")})
            .need(on("rt_tess_smooth")),
        gfx("rt_tess_level", K::kNumber, "3").need(on("rt_tessellation")),
        gfx("rt_tess_test_heights", K::kSwitch, "0").need(on("rt_tessellation")),
        gfx("rt_displacement", K::kNumber, "0")
            .need(on("rt_tessellation"))
            .need(any({has(Cap::kHeightMaps), on("rt_tess_test_heights")}), t::kNeedsHeightMaps)
            .off("0"),
        // PartyMod's (engine/gfx/settings.cpp, platform/sdl_window.cpp)
        gfx("ResolutionX", K::kNumber, "0"),
        gfx("ResolutionY", K::kNumber, "0"),
        gfx("dpi_aware", K::kSwitch, "1"),
        gfx("Windowed", K::kSwitch, "0"),
        gfx("Borderless", K::kSwitch, "0"),
        gfx("AntiAliasing", K::kSwitch, "0")
            .need(is("upscaler", {"off"}), t::kOffWhileUpscaling)
            .need(is("msaa", {"0"}), t::kMsaaIsUsed),
        gfx("HQShadows", K::kSwitch, "0"),
        gfx("DistanceClipping", K::kSwitch, "0"),
        gfx("ClippingDistance", K::kNumber, "100").need(on("DistanceClipping")),
        gfx("Fog", K::kSwitch, "0"),
        gfx("DisableBlur", K::kSwitch, "1"),
        gfx("OcclusionCulling", K::kSwitch, "1"),
        gfx("vulkan_device", K::kNumber, "-1").need(vulkan(), t::kNeedsVulkan),
        gfx("vulkan_validation", K::kSwitch, "0").need(vulkan(), t::kNeedsVulkan),
        // (the split comparison: devtools/compare_split.h - Direct3D 9 draws both sides the same)
        gfx("compare_split", K::kSwitch, "0").need(vulkan(), t::kNeedsVulkan),
        gfx("compare_divider", K::kNumber, "0.5").need(on("compare_split")),
        gfx("compare_labels", K::kSwitch, "1").need(on("compare_split")),

        // ===== Lighting: the sun every effect lights by (engine/gfx/rt_scene.h scene_sun), the time of day
        // (engine/gfx/rt_daylight.h), shadows (gpu_vulkan/vulkan_rt_shadows.h), occlusion and bounce light
        // (vulkan_rt_effects.h), the lamps (vulkan_rt_lights.h)
        gfx("rt_sun_source", K::kChoice, "level")
            .tab(G::kLighting, t::kOptSunSource, t::kOptSunSourceHelp)
            .pick({c("game", t::kOptSunSky), c("level", t::kOptSunLevel)})
            .open()
            .need(vulkan(), t::kNeedsVulkan)
            .need(negate(all({asked_is("rt_sun_source", {"game"}), tod_runs()})), t::kClockSetsSun)
            .off("level")
            .live(E::kSunSource),
        gfx("tod", K::kChoice, "level")
            .tab(G::kLighting, t::kOptTod, t::kOptTodHelp)
            .pick({c("level", t::kOptSunLevel), c("dawn", t::kTodDawn), c("morning", t::kTodMorning), c("noon", t::kTodNoon),
                   c("afternoon", t::kTodAfternoon), c("dusk", t::kTodDusk), c("night", t::kTodNight)})
            .open()
            .live(E::kTimeOfDay),
        gfx("tod_cycle", K::kChoice, "off")
            .tab(G::kLighting, t::kOptTodCycle, t::kOptTodCycleHelp)
            .pick({c("off", t::kOff), c("on", t::kOn)})
            .alias("0", "off")
            .alias("1", "on")
            .live(E::kTimeOfDay),
        gfx("tod_cycle_minutes", K::kNumber, "24")
            .tab(G::kLighting, t::kOptTodCycleMinutes)
            .pick({c("12", "12 min"), c("24", "24 min"), c("48", "48 min"), c("96", "96 min")})
            .need(is("tod_cycle", {"on"}))
            .live(E::kTimeOfDay),
        gfx("tod_levels", K::kText, "").live(E::kTimeOfDay),
        gfx("rt_shadows", K::kSwitch, "0").tab(G::kLighting, t::kOptShadows, t::kOptShadowsHelp).need(ray_tracing()).live(E::kShadows),
        gfx("rt_shadow_strength", K::kNumber, "0.5")
            .tab(G::kLighting, t::kOptShadowStrength)
            .pick(percent({{"0.25", "25%"}, {"0.5", "50%"}, {"0.75", "75%"}, {"1", "100%"}}))
            .need(on("rt_shadows"))
            .live(E::kShadows),
        gfx("rt_shadow_softness", K::kNumber, "0")
            .tab(G::kLighting, t::kOptShadowSoftness)
            .pick({c("0", "Hard"), c("0.5", "0.5"), c("1", "1"), c("2", "2"), c("4", "4")})
            .need(on("rt_shadows"))
            .off("0")
            .live(E::kShadows),
        gfx("rt_shadow_objects", K::kSwitch, "1")
            .tab(G::kLighting, t::kOptShadowObjects, t::kOptShadowObjectsHelp)
            .need(on("rt_shadows"))
            .live(E::kShadows),
        gfx("rt_shadow_rays", K::kNumber, "4")
            .need(on("rt_shadows"))
            .need(is_not("rt_shadow_softness", {"0"}), t::kNeedsSoftShadows)
            .live(E::kShadows),
        gfx("rt_shadow_match_bake", K::kSwitch, "1").need(on("rt_shadows")).live(E::kShadows),
        gfx("rt_shadow_covered", K::kSwitch, "0").need(on("rt_shadows")).live(E::kShadows),
        gfx("rt_shadow_sky_rays", K::kNumber, "5").need(on("rt_shadows")).live(E::kShadows),
        gfx("rt_ao", K::kSwitch, "0").tab(G::kLighting, t::kOptAo, t::kOptAoHelp).need(ray_tracing()).live(E::kAo),
        gfx("rt_ao_strength", K::kNumber, "0.6")
            .tab(G::kLighting, t::kOptAoStrength)
            .pick(percent({{"0.3", "30%"}, {"0.45", "45%"}, {"0.6", "60%"}, {"0.8", "80%"}, {"1", "100%"}}))
            .need(on("rt_ao"))
            .live(E::kAo),
        gfx("rt_ao_radius", K::kNumber, "48")
            .tab(G::kLighting, t::kOptAoRadius)
            .pick({c("24", "24"), c("48", "48"), c("96", "96"), c("192", "192")})
            .need(on("rt_ao"))
            .live(E::kAo),
        gfx("rt_ao_rays", K::kNumber, "4")
            .tab(G::kLighting, t::kOptAoRays)
            .pick({c("1", "1"), c("2", "2"), c("4", "4"), c("8", "8")})
            .need(on("rt_ao"))
            .live(E::kAo),
        // (added for the characters: the occlusion on the skater and the peds - gpu_vulkan/vulkan_rt_effects.cpp)
        gfx("rt_ao_characters", K::kNumber, "1")
            .tab(G::kLighting, t::kOptAoCharacters, t::kOptAoCharactersHelp)
            .pick(percent({{"0.25", "25%"}, {"0.5", "50%"}, {"0.75", "75%"}, {"1", "100%"}}))
            .need(on("rt_ao"))
            .live(E::kAo),
        // (the denoisers: left to DLSS Ray Reconstruction while it runs - vulkan_upscale.h)
        gfx("rt_ao_denoise", K::kSwitch, "1")
            .need(on("rt_ao"))
            .need(negate(on("ray_reconstruction")), t::kRayReconstructionDenoises)
            .live(E::kAo),
        // (the light energy audit - docs/design/vulkan.md "Each light counted once": a relit level's bounce is relit's,
        // traced per vertex, and the objects take it through the light field - one owner, so the pass is out of effect)
        gfx("rt_gi", K::kSwitch, "0")
            .tab(G::kLighting, t::kOptGi, t::kOptGiHelp)
            .need(ray_tracing())
            .need(negate(on("rt_relit")), t::kRelitBouncesLight)
            .live(E::kGi),
        gfx("rt_gi_strength", K::kNumber, "1")
            .tab(G::kLighting, t::kOptGiStrength)
            .pick(percent({{"0.5", "50%"}, {"1", "100%"}, {"1.5", "150%"}, {"2", "200%"}}))
            .need(on("rt_gi"))
            .live(E::kGi),
        gfx("rt_gi_rays", K::kNumber, "2")
            .tab(G::kLighting, t::kOptGiRays)
            .pick({c("1", "1"), c("2", "2"), c("4", "4"), c("8", "8")})
            .need(on("rt_gi"))
            .live(E::kGi),
        gfx("rt_gi_distance", K::kNumber, "600").need(on("rt_gi")).live(E::kGi),
        gfx("rt_gi_denoise", K::kSwitch, "1")
            .need(on("rt_gi"))
            .need(negate(on("ray_reconstruction")), t::kRayReconstructionDenoises)
            .live(E::kGi),
        // (Auto, the default: lit only in the dark - the time of day's night or a dark level)
        gfx("rt_lights", K::kChoice, "auto")
            .tab(G::kLighting, t::kOptLights, t::kOptLightsHelp)
            .pick({c("0", t::kOff), c("auto", t::kAuto), c("1", t::kOn)})
            .need(ray_tracing())
            .off("0")
            .live(E::kLights),
        gfx("rt_lights_strength", K::kNumber, "1")
            .tab(G::kLighting, t::kOptLightsStrength)
            .pick({c("0.35", t::kLow), c("0.6", t::kMedium), c("1", t::kHigh), c("2", "Max")})
            .need(is_not("rt_lights", {"0"}))
            .live(E::kLights),
        gfx("rt_lights_shadowed", K::kNumber, "4")
            .tab(G::kLighting, t::kOptLightsShadowed)
            .pick({c("1", "1"), c("2", "2"), c("4", "4"), c("8", "8")})
            .need(is_not("rt_lights", {"0"}))
            .live(E::kLights),
        gfx("rt_lights_max", K::kNumber, "64").need(is_not("rt_lights", {"0"})).live(E::kLights),
        gfx("rt_lights_emissive", K::kNumber, "1").need(is_not("rt_lights", {"0"})).live(E::kLights),
        gfx("rt_lights_level", K::kNumber, "1").need(is_not("rt_lights", {"0"})).live(E::kLights),

        // ===== Surfaces: relit levels (engine/gfx/rt_relit.h), per-pixel lighting and the surface maps
        // (gpu_vulkan/vulkan_surface.h), detail materials (vulkan_detail.h), reflections (vulkan_rt_reflections.h),
        // grass (vulkan_grass.h), decals and clutter (vulkan_scatter.h), water (vulkan_water.h)
        gfx("rt_relit", K::kSwitch, "0").tab(G::kSurfaces, t::kOptRelit, t::kOptRelitHelp).need(ray_tracing()).live(E::kRelit),
        gfx("rt_relit_strength", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptRelitStrength)
            .pick(percent({{"0.25", "25%"}, {"0.5", "50%"}, {"0.75", "75%"}, {"1", "100%"}}))
            .need(on("rt_relit"))
            .live(E::kRelit),
        gfx("rt_relit_residual", K::kSwitch, "1").need(on("rt_relit")).live(E::kRelit),
        gfx("rt_relit_detail", K::kNumber, "1").need(on("rt_relit")).live(E::kRelit),
        gfx("rt_relit_sky_rays", K::kNumber, "24").need(on("rt_relit")).live(E::kRelit),
        gfx("rt_relit_sun_rays", K::kNumber, "4").need(on("rt_relit")).live(E::kRelit),
        gfx("rt_relit_cache", K::kSwitch, "1").need(on("rt_relit")).live(E::kRelit),
        gfx("rt_pbr", K::kSwitch, "0")
            .tab(G::kSurfaces, t::kOptPbr, t::kOptPbrHelp)
            .need(vulkan(), t::kNeedsVulkan)
            .need(has(Cap::kSurfaceMaps))
            .live(E::kPbr),
        gfx("rt_pbr_specular", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptPbrSpecular)
            .pick({c("0", t::kOff), c("0.5", "50%"), c("1", "100%"), c("1.5", "150%"), c("2", "200%")})
            .need(on("rt_pbr"))
            .live(E::kPbr),
        gfx("rt_pbr_normal_strength", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptPbrNormal)
            .pick({c("0", "Flat"), c("0.5", "50%"), c("1", "100%"), c("1.5", "150%"), c("2", "200%")})
            .need(on("rt_pbr"))
            .live(E::kPbr),
        gfx("rt_pbr_objects", K::kSwitch, "1").need(on("rt_pbr")).live(E::kPbr),
        // (added by surface relief: parallax occlusion mapping on the surface-mapped brick, stone, gravel...)
        gfx("rt_relief", K::kSwitch, "0").tab(G::kSurfaces, t::kOptRelief, t::kOptReliefHelp).need(on("rt_pbr")).live(E::kPbr),
        gfx("rt_relief_strength", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptReliefStrength)
            .pick({c("0.5", t::kLow), c("1", t::kMedium), c("1.5", t::kHigh)})
            .need(on("rt_relief"))
            .live(E::kPbr),
        // (the material library makes it fuller; without it each surface's kind still comes from its surface map)
        gfx("rt_detail", K::kSwitch, "0").tab(G::kSurfaces, t::kOptDetail, t::kOptDetailHelp).need(on("rt_pbr")).live(E::kDetail),
        gfx("rt_detail_strength", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptDetailStrength)
            .pick({c("0.5", t::kLow), c("1", t::kMedium), c("1.5", t::kHigh)})
            .need(on("rt_detail"))
            .live(E::kDetail),
        gfx("rt_delight", K::kSwitch, "0")
            .tab(G::kSurfaces, t::kOptDelight, t::kOptDelightHelp)
            .need(on("rt_pbr"))
            // (with a de-lit texture set chosen too: the renderer leaves the textures that set made flat alone, one by
            // one - gpu_vulkan/vulkan_detail.h; the setting applies to the rest)
            .live(E::kDetail),
        gfx("rt_delight_strength", K::kNumber, "0.5")
            .tab(G::kSurfaces, t::kOptDelightStrength)
            .pick({c("0.25", t::kLow), c("0.5", t::kMedium), c("0.75", t::kHigh)})
            .need(on("rt_delight"))
            .live(E::kDetail),
        // (added for the characters: their surface maps, skin, cloth and hair - on top of rt_pbr)
        gfx("rt_characters", K::kSwitch, "1")
            .tab(G::kSurfaces, t::kOptCharacters, t::kOptCharactersHelp)
            .need(on("rt_pbr"))
            .live(E::kPbr),
        gfx("rt_characters_skin", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptCharactersSkin)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("1.5", t::kHigh)})
            .need(on("rt_characters"))
            .live(E::kPbr),
        gfx("rt_characters_cloth", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptCharactersCloth)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("1.5", t::kHigh)})
            .need(on("rt_characters"))
            .live(E::kPbr),
        gfx("rt_characters_hair", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptCharactersHair)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("1.5", t::kHigh)})
            .need(on("rt_characters"))
            .live(E::kPbr),
        gfx("rt_reflections", K::kSwitch, "0")
            .tab(G::kSurfaces, t::kOptReflections, t::kOptReflectionsHelp)
            .need(ray_tracing())
            .live(E::kReflections),
        gfx("rt_reflection_strength", K::kNumber, "0.5")
            .tab(G::kSurfaces, t::kOptReflectionStrength)
            .pick(percent({{"0.25", "25%"}, {"0.5", "50%"}, {"0.75", "75%"}, {"1", "100%"}}))
            .need(on("rt_reflections"))
            .live(E::kReflections),
        gfx("rt_reflection_fresnel", K::kSwitch, "1")
            .tab(G::kSurfaces, t::kOptFresnel, t::kOptFresnelHelp)
            .need(on("rt_reflections"))
            .live(E::kReflections),
        gfx("rt_glass_reflectance", K::kNumber, "0.2")
            .tab(G::kSurfaces, t::kOptGlassReflectance)
            .pick({c("0.04", "Real"), c("0.1", "10%"), c("0.2", "20%"), c("0.35", "35%")})
            .need(on("rt_reflections"))
            .live(E::kReflections),
        gfx("rt_glass_roughness", K::kNumber, "0.05")
            .tab(G::kSurfaces, t::kOptGlassRoughness)
            .pick({c("0", "Mirror"), c("0.05", t::kLow), c("0.1", t::kMedium), c("0.2", t::kHigh)})
            .need(on("rt_reflections"))
            .live(E::kReflections),
        gfx("rt_reflection_surfaces", K::kNumber, "1")
            .need(all({on("rt_reflections"), on("rt_pbr")}), t::kNeedsReflectionsAndPbr)
            .live(E::kReflections),
        gfx("grass", K::kSwitch, "0").tab(G::kSurfaces, t::kOptGrass, t::kOptGrassHelp).need(vulkan(), t::kNeedsVulkan).live(E::kGrass),
        gfx("grass_density", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptGrassDensity)
            .pick({c("0.5", t::kLow), c("1", t::kMedium), c("1.5", t::kHigh), c("2.5", "Thick")})
            .need(on("grass"))
            .live(E::kGrass),
        gfx("grass_height", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptGrassHeight)
            .pick({c("0.6", "Short"), c("1", t::kMedium), c("1.5", "Long"), c("2.2", "Wild")})
            .need(on("grass"))
            .live(E::kGrass),
        gfx("grass_distance", K::kNumber, "50")
            .tab(G::kSurfaces, t::kOptGrassDistance)
            .pick({c("30", "30 m"), c("50", "50 m"), c("80", "80 m"), c("120", "120 m")})
            .need(on("grass"))
            .live(E::kGrass),
        gfx("rt_decals", K::kSwitch, "0").tab(G::kSurfaces, t::kOptDecals, t::kOptDecalsHelp).need(vulkan(), t::kNeedsVulkan).live(E::kScatter),
        gfx("rt_decals_density", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptDecalsDensity)
            .pick({c("0.5", t::kLow), c("1", t::kMedium), c("2", t::kHigh), c("3", "Grimy")})
            .need(on("rt_decals"))
            .live(E::kScatter),
        gfx("clutter", K::kSwitch, "0").tab(G::kSurfaces, t::kOptClutter, t::kOptClutterHelp).need(vulkan(), t::kNeedsVulkan).live(E::kScatter),
        gfx("clutter_density", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptClutterDensity)
            .pick({c("0.5", t::kLow), c("1", t::kMedium), c("2", t::kHigh), c("3", "Messy")})
            .need(on("clutter"))
            .live(E::kScatter),
        gfx("rt_water", K::kSwitch, "0").tab(G::kSurfaces, t::kOptWater, t::kOptWaterHelp).need(vulkan(), t::kNeedsVulkan).live(E::kWater),
        gfx("rt_water_waves", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptWaterWaves)
            .pick({c("0", "Flat"), c("0.5", "Calm"), c("1", "Normal"), c("1.5", "Choppy"), c("2", "Rough")})
            .need(on("rt_water"))
            .live(E::kWater),
        gfx("rt_water_wind", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptWaterWind)
            .pick({c("0", "Still"), c("0.5", "Light"), c("1", "Breeze"), c("1.5", "Strong"), c("2", "Gale")})
            .need(on("rt_water"))
            .live(E::kWater),
        gfx("rt_water_clarity", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptWaterClarity)
            .pick({c("0.5", "Murky"), c("1", "Normal"), c("2", "Clear"), c("3", "Crystal")})
            .need(on("rt_water"))
            .live(E::kWater),
        gfx("rt_water_foam", K::kNumber, "1")
            .tab(G::kSurfaces, t::kOptWaterFoam)
            .pick({c("0", t::kOff), c("0.5", "50%"), c("1", "100%"), c("2", "200%")})
            .need(on("rt_water"))
            .live(E::kWater),
        gfx("rt_water_tint", K::kText, "auto")
            .tab(G::kSurfaces, t::kOptWaterTint)
            .pick({c("auto", t::kAuto), c("0.08,0.3,0.42", "Deep blue"), c("0.1,0.45,0.45", "Tropical"), c("0.2,0.32,0.22", "Green")})
            .need(on("rt_water"))
            .live(E::kWater),
        // (the surface moved by the waves: render_pc/water_geometry.h - a level's water is split as it loads)
        gfx("rt_water_geometry", K::kSwitch, "1")
            .tab(G::kSurfaces, t::kOptWaterGeometry, t::kOptWaterGeometryHelp)
            .need(on("rt_water"))
            .live(E::kWater),
        gfx("rt_water_wind_heading", K::kText, "auto").need(on("rt_water")).live(E::kWater),
        gfx("rt_water_screen_reflections", K::kSwitch, "1").need(on("rt_water")).live(E::kWater),

        // ===== Sky: the sky and clouds (gpu_vulkan/vulkan_rt_sky_pass.h), the sun, its rays, the bloom and the sky model
        // (vulkan_rt_sun.h, vulkan_rt_sky.h), the fog and haze (vulkan_rt_fog.h)
        gfx("rt_sky", K::kSwitch, "0").tab(G::kSky, t::kOptSky, t::kOptSkyHelp).need(vulkan(), t::kNeedsVulkan).live(E::kSky),
        gfx("rt_clouds", K::kSwitch, "0").tab(G::kSky, t::kOptClouds, t::kOptCloudsHelp).need(on("rt_sky")).live(E::kSky),
        gfx("rt_cloud_coverage", K::kText, "auto")
            .tab(G::kSky, t::kOptCloudCoverage)
            .pick({c("auto", t::kAuto), c("0", "Clear"), c("0.25", "25%"), c("0.5", "50%"), c("0.75", "75%"), c("1", "Overcast")})
            .need(on("rt_clouds"))
            .live(E::kSky),
        gfx("rt_cloud_quality", K::kNumber, "1")
            .tab(G::kSky, t::kOptCloudQuality)
            .pick({c("0", t::kLow), c("1", t::kMedium), c("2", t::kHigh)})
            .need(on("rt_clouds"))
            .live(E::kSky),
        gfx("rt_cloud_speed", K::kNumber, "1").need(on("rt_clouds")).live(E::kSky),
        gfx("rt_cloud_shadows", K::kSwitch, "0").need(on("rt_clouds")).live(E::kSky),
        gfx("rt_sky_match", K::kNumber, "0.8").need(on("rt_sky")).live(E::kSky),
        gfx("rt_sky_model", K::kSwitch, "0").tab(G::kSky, t::kOptSkyModel, t::kOptSkyModelHelp).need(ray_tracing()).live(E::kSun),
        gfx("rt_sun", K::kSwitch, "0").tab(G::kSky, t::kOptSun, t::kOptSunHelp).need(vulkan(), t::kNeedsVulkan).live(E::kSun),
        // (the sun's colour: the disc's, and the light the sky table hands the other effects - made while the sun, its
        // rays, the bloom or the sky model is on: vulkan_rt_scene.cpp)
        gfx("rt_sun_colour", K::kText, "auto")
            .tab(G::kSky, t::kOptSunColour)
            .pick({c("auto", t::kAuto), c("1,0.85,0.7", "Warm"), c("1,1,1", "White"), c("0.85,0.92,1", "Cool")})
            .need(any({on("rt_sun"), on("rt_shafts"), on("rt_bloom"), on("rt_sky_model")}), t::kNeedsSunLight)
            .live(E::kSun),
        gfx("rt_sun_size", K::kNumber, "1").need(on("rt_sun")).live(E::kSun),
        gfx("rt_shafts", K::kSwitch, "0").tab(G::kSky, t::kOptShafts, t::kOptShaftsHelp).need(vulkan(), t::kNeedsVulkan).live(E::kSun),
        gfx("rt_shaft_strength", K::kNumber, "0.8").need(on("rt_shafts")).live(E::kSun),
        gfx("rt_fog", K::kSwitch, "0").tab(G::kSky, t::kOptFog, t::kOptFogHelp).need(ray_tracing()).live(E::kFog),
        // (the haze is the fog pass's in a level without fog: the pass runs with rt_fog alone - vulkan_rt_fog.cpp)
        gfx("rt_fog_haze", K::kSwitch, "1")
            .tab(G::kSky, t::kOptHaze, t::kOptHazeHelp)
            .need(on("rt_fog"))
            .need(has(Cap::kLevelWithoutFog))
            .live(E::kFog),
        // (the haze's air: the sky model's aerial perspective)
        gfx("rt_fog_haze_density", K::kNumber, "0.3")
            .tab(G::kSky, t::kOptHazeDensity)
            .pick({c("0.15", t::kLow), c("0.3", t::kMedium), c("0.6", t::kHigh), c("1", "Thick")})
            .need(all({on("rt_fog_haze"), on("rt_sky_model")}), t::kNeedsHazeAndSkyModel)
            .live(E::kFog),
        gfx("rt_fog_haze_height", K::kNumber, "600")
            .need(all({on("rt_fog_haze"), on("rt_sky_model")}), t::kNeedsHazeAndSkyModel)
            .live(E::kFog),
        gfx("rt_fog_mode", K::kNumber, "0").need(on("rt_fog")).live(E::kFog),
        gfx("rt_fog_density", K::kNumber, "1").need(on("rt_fog")).live(E::kFog),
        gfx("rt_fog_height", K::kNumber, "0").need(on("rt_fog")).live(E::kFog),
        gfx("rt_fog_anisotropy", K::kNumber, "0.3").need(on("rt_fog")).live(E::kFog),
        gfx("rt_fog_steps", K::kNumber, "32").need(on("rt_fog")).live(E::kFog),

        // ===== Colour: HDR and its tone mapping (gpu_vulkan/vulkan_hdr.h; rt_hdr itself the device's targets' format), the
        // bloom and the lens (vulkan_rt_sun.h)
        gfx("rt_hdr", K::kSwitch, "0").tab(G::kColour, t::kOptHdr, t::kOptHdrHelp).need(vulkan(), t::kNeedsVulkan),
        gfx("rt_exposure", K::kNumber, "0")
            .tab(G::kColour, t::kOptExposure)
            .pick({c("-1", "-1"), c("-0.5", "-0.5"), c("0", "0"), c("0.5", "+0.5"), c("1", "+1")})
            .need(on("rt_hdr"))
            .live(E::kHdr),
        gfx("rt_tonemap", K::kNumber, "0").tab(G::kColour, t::kOptTonemap).pick({c("0", "Game"), c("1", "Filmic")}).need(on("rt_hdr")).live(E::kHdr),
        gfx("rt_white_point", K::kNumber, "1.25")
            .tab(G::kColour, t::kOptWhitePoint)
            .pick({c("1", "1"), c("1.25", "1.25"), c("2", "2"), c("4", "4")})
            .need(on("rt_hdr"))
            .live(E::kHdr),
        gfx("rt_saturation", K::kNumber, "1")
            .tab(G::kColour, t::kOptSaturation)
            .pick(percent({{"0.8", "80%"}, {"1", "100%"}, {"1.2", "120%"}, {"1.4", "140%"}}))
            .need(on("rt_hdr"))
            .live(E::kHdr),
        gfx("rt_contrast", K::kNumber, "1")
            .tab(G::kColour, t::kOptContrast)
            .pick(percent({{"0.9", "90%"}, {"1", "100%"}, {"1.1", "110%"}, {"1.2", "120%"}}))
            .need(on("rt_hdr"))
            .live(E::kHdr),
        gfx("rt_grade_lut", K::kText, "").need(on("rt_hdr")).live(E::kHdr),
        gfx("rt_bloom", K::kSwitch, "0").tab(G::kColour, t::kOptBloom, t::kOptBloomHelp).need(vulkan(), t::kNeedsVulkan).live(E::kSun),
        gfx("rt_bloom_strength", K::kNumber, "0.05")
            .tab(G::kColour, t::kOptBloomStrength)
            .pick({c("0.02", t::kLow), c("0.05", t::kMedium), c("0.1", t::kHigh), c("0.2", "Max")})
            .need(on("rt_bloom"))
            .live(E::kSun),
        gfx("rt_lens_dirt", K::kSwitch, "0").need(any({on("rt_bloom"), on("rt_sun")}), t::kNeedsBloomOrSun).live(E::kSun),

        // ===== Camera: motion blur, depth of field, the colour grade (gpu_vulkan/vulkan_camera_fx.h)
        gfx("rt_motion_blur", K::kSwitch, "0")
            .tab(G::kCamera, t::kOptMotionBlur, t::kOptMotionBlurHelp)
            .need(vulkan(), t::kNeedsVulkan)
            .live(E::kCameraFx),
        gfx("rt_motion_blur_strength", K::kNumber, "0.5")
            .tab(G::kCamera, t::kOptMotionBlurStrength)
            .pick({c("0.25", t::kLow), c("0.5", t::kMedium), c("0.75", t::kHigh), c("1", "Max")})
            .need(on("rt_motion_blur"))
            .live(E::kCameraFx),
        gfx("rt_dof", K::kSwitch, "0").tab(G::kCamera, t::kOptDof, t::kOptDofHelp).need(vulkan(), t::kNeedsVulkan).live(E::kCameraFx),
        gfx("rt_dof_strength", K::kNumber, "0.4")
            .tab(G::kCamera, t::kOptDofStrength)
            .pick({c("0.2", t::kLow), c("0.4", t::kMedium), c("0.7", t::kHigh), c("1", "Max")})
            .need(on("rt_dof"))
            .live(E::kCameraFx),
        gfx("rt_grade", K::kChoice, "off")
            .tab(G::kCamera, t::kOptGrade, t::kOptGradeHelp)
            .pick({c("off", t::kOff), c("neutral", t::kGradeNeutral), c("warm", t::kGradeWarm), c("cool", t::kGradeCool),
                   c("filmic", t::kGradeFilmic), c("bleach", t::kGradeBleach), c("skate_video", t::kGradeSkateVideo),
                   c("level", t::kGradeLevel)})
            .need(vulkan(), t::kNeedsVulkan)
            .off("off")
            .live(E::kCameraFx),
        gfx("rt_grade_strength", K::kNumber, "1")
            .tab(G::kCamera, t::kOptGradeStrength)
            .pick(percent({{"0.5", "50%"}, {"0.75", "75%"}, {"1", "100%"}}))
            .need(is_not("rt_grade", {"off", "neutral"}), t::kNeedsLook)
            .live(E::kCameraFx),
        // (an art style with a vignette / grain of its own keeps it: vulkan_camera_fx.cpp)
        gfx("rt_grade_vignette", K::kNumber, "0")
            .tab(G::kCamera, t::kOptGradeVignette)
            .pick({c("0", t::kOff), c("0.35", t::kLow), c("0.65", t::kMedium), c("1", t::kHigh)})
            .need(is_not("rt_grade", {"off"}))
            .need(negate(is("rt_style", {"noir", "thermal"})), t::kStyleOwnVignette)
            .live(E::kCameraFx),
        gfx("rt_grade_grain", K::kNumber, "0")
            .tab(G::kCamera, t::kOptGradeGrain)
            .pick({c("0", t::kOff), c("0.35", t::kLow), c("0.65", t::kMedium), c("1", t::kHigh)})
            .need(is_not("rt_grade", {"off"}))
            .need(negate(is("rt_style", {"noir", "thermal", "comic", "sketch"})), t::kStyleOwnGrain)
            .live(E::kCameraFx),
        gfx("rt_grade_levels", K::kText, "").need(is("rt_grade", {"level"}), t::kNeedsPerLevelGrade).live(E::kCameraFx),

        // ===== Style: the art styles (gpu_vulkan/vulkan_style.h, vulkan_style_looks.h, vulkan_style_film.h) - rt_style
        // first, then each style's knobs; cel is cel shading (engine/gfx/rt_cel.h), fisheye the lens (vulkan_fisheye.h)
        gfx("rt_style", K::kChoice, "off")
            .tab(G::kStyle, t::kOptStyle, t::kOptStyleHelp)
            .pick({c("off", t::kOff), c("toon", t::kStyleToon), c("cel", t::kStyleCel), c("comic", t::kStyleComic),
                   c("sketch", t::kStyleSketch), c("vhs", t::kStyleVhs), c("retro", t::kStyleRetro),
                   c("watercolour", t::kStyleWatercolour), c("noir", t::kStyleNoir), c("thermal", t::kStyleThermal),
                   c("fisheye", t::kStyleFisheye), c("fisheye_vhs", t::kStyleFisheyeVhs)})
            .alias("0", "off")
            .alias("1", "toon")
            .need(vulkan(), t::kNeedsVulkan)
            .off("off")
            .live(E::kStyle),
        gfx("rt_style_strength", K::kNumber, "1")
            .tab(G::kStyle, t::kOptStyleStrength)
            .pick(percent({{"0.5", "50%"}, {"0.75", "75%"}, {"1", "100%"}}))
            .need(is("rt_style", {"toon", "comic", "sketch", "vhs", "retro", "watercolour", "noir", "thermal", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_fisheye_strength", K::kNumber, "0.75")
            .tab(G::kStyle, t::kOptFisheyeStrength)
            .pick({c("0.4", t::kLow), c("0.6", t::kMedium), c("0.75", t::kHigh), c("1", "Max")})
            .need(is("rt_style", {"fisheye", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kCameraFx),
        gfx("rt_fisheye_vignette", K::kNumber, "0.5")
            .tab(G::kStyle, t::kOptFisheyeVignette)
            .pick({c("0", t::kOff), c("0.25", t::kLow), c("0.5", t::kMedium), c("1", t::kHigh)})
            .need(is("rt_style", {"fisheye", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kCameraFx),
        gfx("rt_fisheye_circle", K::kNumber, "0.5")
            .tab(G::kStyle, t::kOptFisheyeCircle, t::kOptFisheyeCircleHelp)
            .pick({c("0", t::kFisheyeFullFrame), c("0.5", t::kFisheyeRounded), c("1", t::kFisheyePorthole)})
            .need(is("rt_style", {"fisheye", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kCameraFx),
        gfx("rt_style_smooth", K::kNumber, "4")
            .tab(G::kStyle, t::kOptStyleSmooth)
            .pick({c("0", t::kOff), c("2", t::kLow), c("4", t::kMedium), c("6", t::kHigh)})
            .need(is("rt_style", {"toon", "comic", "sketch"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_bands", K::kNumber, "5")
            .tab(G::kStyle, t::kOptStyleBands)
            .pick({c("0", t::kOff), c("3", "3"), c("4", "4"), c("5", "5"), c("7", "7")})
            .need(is("rt_style", {"toon", "comic"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_outlines", K::kNumber, "1")
            .tab(G::kStyle, t::kOptStyleOutlines)
            .pick({c("0", t::kOff), c("0.5", "50%"), c("0.75", "75%"), c("1", "100%")})
            .need(is("rt_style", {"toon", "comic", "sketch"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_outline_width", K::kNumber, "1")
            .tab(G::kStyle, t::kOptStyleOutlineWidth)
            .pick({c("0.5", "Thin"), c("1", t::kMedium), c("1.5", "Bold"), c("2", "Heavy")})
            .need(is("rt_style", {"toon", "comic", "sketch"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_saturation", K::kNumber, "1.15")
            .tab(G::kStyle, t::kOptStyleSaturation)
            .pick(percent({{"1", "100%"}, {"1.15", "115%"}, {"1.3", "130%"}, {"1.5", "150%"}}))
            .need(is("rt_style", {"toon", "comic"}), t::kNeedsStyle)
            .live(E::kStyle),
        // (cel shading's knobs - engine/gfx/rt_cel.h)
        gfx("rt_cel_bands", K::kNumber, "3")
            .tab(G::kStyle, t::kOptCelBands)
            .pick({c("2", "2"), c("3", "3"), c("4", "4")})
            .need(is("rt_style", {"cel"}), t::kNeedsStyle)
            .live(E::kCel),
        gfx("rt_cel_shadow_colour", K::kText, "0.62,0.66,0.86")
            .tab(G::kStyle, t::kOptCelShadow)
            .pick({c("0.62,0.66,0.86", t::kCelShadowCool), c("0.72,0.62,0.82", t::kCelShadowDusk),
                   c("0.82,0.68,0.6", t::kCelShadowWarm), c("0.66,0.66,0.66", t::kCelShadowGrey)})
            .need(is("rt_style", {"cel"}), t::kNeedsStyle)
            .live(E::kCel),
        gfx("rt_cel_rim", K::kNumber, "0.5")
            .tab(G::kStyle, t::kOptCelRim)
            .pick({c("0", t::kOff), c("0.3", t::kLow), c("0.5", t::kMedium), c("0.8", t::kHigh)})
            .need(is("rt_style", {"cel"}), t::kNeedsStyle)
            .live(E::kCel),
        gfx("rt_cel_outline_width", K::kNumber, "1")
            .tab(G::kStyle, t::kOptCelOutlines)
            .pick({c("0", t::kOff), c("0.6", "Thin"), c("1", t::kMedium), c("1.6", "Bold")})
            .need(is("rt_style", {"cel"}), t::kNeedsStyle)
            .live(E::kCel),
        gfx("rt_cel_flatten", K::kNumber, "0.5")
            .tab(G::kStyle, t::kOptCelFlatten)
            .pick({c("0", t::kOff), c("0.3", t::kLow), c("0.5", t::kMedium), c("0.8", t::kHigh)})
            .need(is("rt_style", {"cel"}), t::kNeedsStyle)
            .live(E::kCel),
        // (an older ini's rt_cel line: cel shading is rt_style=cel now)
        gfx("rt_cel", K::kSwitch, "0").retire(t::kRetired),
        gfx("rt_style_comic_halftone", K::kNumber, "5")
            .tab(G::kStyle, t::kOptStyleHalftone)
            .pick({c("0", t::kOff), c("4", "Fine"), c("5", t::kMedium), c("7", "Bold")})
            .need(is("rt_style", {"comic"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_sketch_hatch", K::kNumber, "5")
            .tab(G::kStyle, t::kOptStyleHatch)
            .pick({c("4", "Tight"), c("5", t::kMedium), c("7", "Loose")})
            .need(is("rt_style", {"sketch"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_sketch_wash", K::kNumber, "0.35")
            .tab(G::kStyle, t::kOptStyleWash)
            .pick({c("0", t::kOff), c("0.35", t::kLow), c("0.65", t::kMedium), c("1", t::kHigh)})
            .need(is("rt_style", {"sketch"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_paper", K::kNumber, "0.5")
            .tab(G::kStyle, t::kOptStylePaper)
            .pick({c("0", "White"), c("0.5", "Cream"), c("1", "Aged")})
            .need(is("rt_style", {"comic", "sketch"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_grain", K::kNumber, "0.5")
            .tab(G::kStyle, t::kOptStyleGrain)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kHigh)})
            .need(is("rt_style", {"comic", "sketch", "noir", "thermal"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_vignette", K::kNumber, "0.5")
            .tab(G::kStyle, t::kOptStyleVignette)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kHigh)})
            .need(is("rt_style", {"noir", "thermal"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_noir_contrast", K::kNumber, "1.8")
            .tab(G::kStyle, t::kOptStyleContrast)
            .pick({c("1.3", t::kLow), c("1.8", t::kMedium), c("2.5", t::kHigh)})
            .need(is("rt_style", {"noir"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_noir_dark", K::kText, "0,0,0")
            .tab(G::kStyle, t::kOptStyleDark)
            .pick({c("0,0,0", "Black"), c("0.04,0.16,0.2", "Teal"), c("0.05,0.06,0.16", "Navy")})
            .need(is("rt_style", {"noir"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_noir_light", K::kText, "0.95,0.92,0.84")
            .tab(G::kStyle, t::kOptStyleLight)
            .pick({c("0.95,0.92,0.84", "Paper"), c("1,1,1", "White"), c("1,0.62,0.3", "Orange")})
            .need(is("rt_style", {"noir"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_thermal_focus", K::kNumber, "0.6")
            .tab(G::kStyle, t::kOptStyleFocus)
            .pick({c("0", t::kOff), c("0.6", t::kMedium), c("1", t::kHigh)})
            .need(is("rt_style", {"thermal"}), t::kNeedsStyle)
            .live(E::kStyle),
        // (the vhs, retro and watercolour styles' knobs: gpu_vulkan/vulkan_style_film.h)
        gfx("rt_style_vhs_fisheye", K::kNumber, "0.35")
            .tab(G::kStyle, t::kOptVhsFisheye)
            .pick({c("0", t::kOff), c("0.2", t::kLow), c("0.35", t::kMedium), c("0.6", t::kHigh), c("1", "Max")})
            .need(is("rt_style", {"vhs"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_vhs_bleed", K::kNumber, "1")
            .tab(G::kStyle, t::kOptVhsBleed)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("1.5", t::kHigh), c("2", "Max")})
            .need(is("rt_style", {"vhs", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_vhs_wobble", K::kNumber, "1")
            .tab(G::kStyle, t::kOptVhsWobble)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("2", t::kHigh)})
            .need(is("rt_style", {"vhs", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_vhs_noise", K::kNumber, "1")
            .tab(G::kStyle, t::kOptVhsNoise)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("2", t::kHigh)})
            .need(is("rt_style", {"vhs", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_vhs_scanlines", K::kNumber, "0.5")
            .tab(G::kStyle, t::kOptVhsScanlines)
            .pick({c("0", t::kOff), c("0.25", t::kLow), c("0.5", t::kMedium), c("1", t::kHigh)})
            .need(is("rt_style", {"vhs", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_vhs_saturation", K::kNumber, "0.7")
            .tab(G::kStyle, t::kOptVhsSaturation)
            .pick(percent({{"0.5", "50%"}, {"0.7", "70%"}, {"1", "100%"}, {"1.2", "120%"}}))
            .need(is("rt_style", {"vhs", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_vhs_overlay", K::kSwitch, "1")
            .tab(G::kStyle, t::kOptVhsOverlay, t::kOptVhsOverlayHelp)
            .need(is("rt_style", {"vhs", "fisheye_vhs"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_retro_lines", K::kNumber, "240")
            .tab(G::kStyle, t::kOptRetroLines)
            .pick({c("180", "180p"), c("240", "240p"), c("360", "360p"), c("480", "480p")})
            .need(is("rt_style", {"retro"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_retro_bits", K::kNumber, "5")
            .tab(G::kStyle, t::kOptRetroBits)
            .pick({c("3", "9-bit"), c("4", "12-bit"), c("5", "15-bit"), c("6", "18-bit"), c("8", "24-bit")})
            .need(is("rt_style", {"retro"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_retro_dither", K::kNumber, "1")
            .tab(G::kStyle, t::kOptRetroDither)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kHigh)})
            .need(is("rt_style", {"retro"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_retro_snap", K::kNumber, "1")
            .tab(G::kStyle, t::kOptRetroSnap)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kHigh)})
            .need(is("rt_style", {"retro"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_retro_affine", K::kNumber, "1")
            .tab(G::kStyle, t::kOptRetroAffine)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kHigh)})
            .need(is("rt_style", {"retro"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_watercolour_bleed", K::kNumber, "1")
            .tab(G::kStyle, t::kOptWatercolourBleed)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("1.5", t::kHigh), c("2", "Max")})
            .need(is("rt_style", {"watercolour"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_watercolour_flow", K::kNumber, "1")
            .tab(G::kStyle, t::kOptWatercolourFlow)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("2", t::kHigh)})
            .need(is("rt_style", {"watercolour"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_watercolour_granulation", K::kNumber, "1")
            .tab(G::kStyle, t::kOptWatercolourGranulation)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("2", t::kHigh)})
            .need(is("rt_style", {"watercolour"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_watercolour_edges", K::kNumber, "1")
            .tab(G::kStyle, t::kOptWatercolourEdges)
            .pick({c("0", t::kOff), c("0.5", t::kLow), c("1", t::kMedium), c("2", t::kHigh)})
            .need(is("rt_style", {"watercolour"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_watercolour_paper", K::kNumber, "0.6")
            .tab(G::kStyle, t::kOptWatercolourPaper)
            .pick({c("0", t::kOff), c("0.3", t::kLow), c("0.6", t::kMedium), c("1", t::kHigh)})
            .need(is("rt_style", {"watercolour"}), t::kNeedsStyle)
            .live(E::kStyle),
        gfx("rt_style_watercolour_outlines", K::kNumber, "0.35")
            .tab(G::kStyle, t::kOptWatercolourOutlines)
            .pick({c("0", t::kOff), c("0.2", t::kLow), c("0.35", t::kMedium), c("0.6", t::kHigh)})
            .need(is("rt_style", {"watercolour"}), t::kNeedsStyle)
            .live(E::kStyle),

        // ===== Upscaling (gpu_vulkan/vulkan_upscale.h): the device's targets and the provider are made for it
        gfx("upscaler", K::kChoice, "off")
            .tab(G::kUpscaling, t::kOptUpscaler, t::kOptUpscalerHelp)
            .pick({c("off", t::kOff),
                   c("auto", t::kAuto),
                   {.value = "dlss", .label = "DLSS", .needs = {{.cond = has(Cap::kRtx), .reason = t::kDlssNeedsRtx}}, .instead = "fsr"},
                   c("fsr", "FSR")})
            .need(vulkan(), t::kNeedsVulkan)
            .need(has(Cap::k64Bit))
            .off("off"),
        gfx("upscale_quality", K::kChoice, "quality")
            .tab(G::kUpscaling, t::kOptUpscaleQuality, t::kOptUpscaleQualityHelp)
            .pick({c("dlaa", "DLAA"), c("quality", "Quality"), c("balanced", "Balanced"), c("performance", "Perf"), c("ultra", "Ultra perf")})
            .alias("native", "dlaa")
            .need(is_not("upscaler", {"off"})),
        gfx("ray_reconstruction", K::kSwitch, "0")
            .tab(G::kUpscaling, t::kOptRayReconstruction, t::kOptRayReconstructionHelp)
            .need(dlss_upscaling(), t::kNeedsDlss)
            .need(ray_tracing()),
        gfx("upscale_sharpness", K::kNumber, "0").need(fsr_upscaling(), t::kNeedsFsr),
        gfx("fsr_version", K::kNumber, "0").need(fsr_upscaling(), t::kNeedsFsr),

        // ===== Interface, Gameplay, Online, Mods, Sound: the game's features ([features] and others)
        feature("new_frontend", K::kSwitch, "0").tab(G::kInterface, t::kOptNewFrontend, t::kOptNewFrontendHelp),
        feature("hd_ui", K::kSwitch, "0").tab(G::kInterface, t::kOptHdUi, t::kOptHdUiHelp),
        feature("mouse_menus", K::kSwitch, "1").tab(G::kInterface, t::kOptMouse, t::kOptMouseHelp).live(),
        feature("loading_console", K::kSwitch, "1").tab(G::kInterface, t::kOptLoadingConsole, t::kOptLoadingConsoleHelp),
        // (game/front_end/live_pause.h)
        feature("live_pause", K::kChoice, "1")
            .tab(G::kGameplay, t::kOptLivePause, t::kOptLivePauseHelp)
            .pick({c("0", t::kOptLivePauseFreeze), c("1", t::kOptLivePauseLive)})
            .live(),
        feature("skip_intro_movies", K::kSwitch, "0").tab(G::kGameplay, t::kOptSkipIntro, t::kOptSkipIntroHelp),
        feature("restart_tester", K::kSwitch, "1").tab(G::kGameplay, t::kOptRestartTester, t::kOptRestartTesterHelp).live(),
        feature("screenshots", K::kSwitch, "1").tab(G::kGameplay, t::kOptScreenshots, t::kOptScreenshotsHelp),
        Decl("frontend", "respect_locks", K::kChoice, "0")
            .tab(G::kGameplay, t::kOptLocks, t::kOptLocksHelp)
            .pick({c("0", t::kOptLocksAll), c("1", t::kOptLocksUnlocked)})
            .live(),
        Decl("Miscellaneous", "OnlineGameName", K::kChoice, "")
            .tab(G::kOnline, t::kOptIdentity, t::kOptIdentityHelp)
            .pick({c("", t::kIdentityThug2), c("thugpro", t::kIdentityThugpro)})
            .open(),
        Decl("online", "thugpro_peer", K::kSwitch, "0").tab(G::kOnline, t::kOptThugproChat, t::kOptThugproChatHelp),
        feature("thugpro_parts", K::kChoice, "1")
            .tab(G::kOnline, t::kOptThugproParts, t::kOptThugproPartsHelp)
            .pick({c("0", t::kOff), c("1", t::kOptThugproPartsHidden), c("2", t::kOptThugproPartsShown)}),
        feature("anteara_mod", K::kSwitch, "0").tab(G::kMods, t::kOptAnteara, t::kOptAntearaHelp),
        feature("thaw_bmx", K::kSwitch, "0").tab(G::kMods, t::kOptBmx, t::kOptBmxHelp),
        feature("debug_mode", K::kSwitch, "0").tab(G::kMods, t::kOptDebug, t::kOptDebugHelp),
        // (engine/audio/continue_music.h)
        feature("continue_music", K::kSwitch, "0").tab(G::kSound, t::kOptContinueMusic, t::kOptContinueMusicHelp).live(),
        // [features] the ini's alone
        feature("mute_audio", K::kSwitch, "0"),
        feature("widescreen_menus", K::kSwitch, "1"),
        feature("cursor_by_device", K::kSwitch, "1"),
        feature("single_player_any_pad", K::kSwitch, "1"),
        feature("editor_ui", K::kSwitch, "1"),
        feature("debug_editor", K::kSwitch, "0"),
        feature("start_level", K::kText, ""),
        feature("edit_level", K::kText, ""),
        feature("start_at", K::kText, "").need(is_not("start_level", {""}), t::kNeedsStartLevel),
        feature("level_select_loading_screens", K::kSwitch, "1"),
        feature("thaw_bmx_wheels", K::kSwitch, "1").need(on("thaw_bmx")),
        feature("thaw_bmx_shadow", K::kSwitch, "1").need(on("thaw_bmx")),
        feature("ts_scripts", K::kSwitch, "0"),
        feature("language", K::kText, "").need(on("ts_scripts"), t::kNeedsUnpackedScripts),
        feature("create_100_cas", K::kSwitch, "1"),
        feature("allow_multiple_instances", K::kSwitch, "0"),
        feature("automation", K::kSwitch, "0"),
        feature("debug_rail_speed", K::kSwitch, "0"),
        feature("debug_collision", K::kSwitch, "0"),
        feature("debug_sprites", K::kSwitch, "0"),
        // [debug]
        debug("collision_events", K::kSwitch, "0"),
        debug("collision_event_screenshots", K::kSwitch, "1").need(on("collision_events")),
        debug("frame_profile", K::kSwitch, "0"),
        debug("latency_probe", K::kSwitch, "0"),
        debug("load_test", K::kNumber, "0"),
        debug("rt_contract_check", K::kSwitch, "0").need(ray_tracing()),
        debug("rt_lights_log", K::kSwitch, "0").need(is_not("rt_lights", {"0"})),
        debug("rt_water_view", K::kSwitch, "0").need(on("rt_water")),
        debug("rt_water_materials", K::kSwitch, "0").need(on("rt_water")),
        debug("tess_log", K::kSwitch, "0").need(on("rt_tessellation")),
        debug("thaw_bmx_steer", K::kSwitch, "0").need(on("thaw_bmx")),
        debug("thaw_bmx_wheels", K::kSwitch, "0").need(on("thaw_bmx")),
        debug("soft_bounds", K::kSwitch, "0"),
        // [Miscellaneous] PartyMod's, and the online identity
        Decl("Miscellaneous", "UsePS2Controls", K::kSwitch, "1"),
        Decl("Miscellaneous", "UIControls", K::kSwitch, "1"),
        Decl("Miscellaneous", "PreferGlyphs", K::kSwitch, "0"),
        Decl("Miscellaneous", "GlyphStyle", K::kNumber, "1"),
        Decl("Miscellaneous", "OnlineDomain", K::kText, "openspy.net"),
        Decl("Miscellaneous", "OnlineGameKey", K::kText, ""),
        Decl("Miscellaneous", "Debug", K::kSwitch, "0"),
        // [demo] a demo saved as a video (devtools/demo_video.h)
        Decl("demo", "video_fps", K::kNumber, "60"),
        Decl("demo", "video_quality", K::kNumber, "80"),
        Decl("demo", "video_codec", K::kText, "auto"),
        Decl("demo", "video_scale", K::kNumber, "100"),
    };
}

} // namespace

const std::vector<GroupInfo>& groups()
{
    static const std::vector<GroupInfo> kGroups = {
        {.group = Group::kGraphics, .id = "graphics", .name = t::kGroupGraphics},
        {.group = Group::kLighting, .id = "lighting", .name = t::kGroupLighting},
        {.group = Group::kSurfaces, .id = "surfaces", .name = t::kGroupSurfaces},
        {.group = Group::kSky, .id = "sky", .name = t::kGroupSky},
        {.group = Group::kColour, .id = "colour", .name = t::kGroupColour},
        {.group = Group::kCamera, .id = "camera", .name = t::kGroupCamera},
        {.group = Group::kStyle, .id = "style", .name = t::kGroupStyle},
        {.group = Group::kUpscaling, .id = "upscaling", .name = t::kGroupUpscaling},
        {.group = Group::kInterface, .id = "interface", .name = t::kGroupInterface},
        {.group = Group::kGameplay, .id = "gameplay", .name = t::kGroupGameplay},
        {.group = Group::kOnline, .id = "online", .name = t::kGroupOnline},
        {.group = Group::kMods, .id = "mods", .name = t::kGroupMods},
        {.group = Group::kSound, .id = "sound", .name = t::kGroupSound},
    };
    return kGroups;
}

const std::vector<Setting>& settings()
{
    static const std::vector<Setting> kSettings = [] {
        std::vector<Decl> declared = declarations();
        std::vector<Setting> made;
        made.reserve(declared.size());
        for (Decl& each : declared) {
            made.push_back(std::move(each).take());
        }
        return made;
    }();
    return kSettings;
}

const std::vector<Force>& forces()
{
    // (frame generation makes its frames from the upscaler's motion vectors and depth: with none asked for, the upscaler
    // runs at native resolution - DLAA / FSR's anti-aliasing; DLSS-G needs Reflex running; a sun other than the baked one,
    // or the clock's, needs the level relit by it; THUGPRO's lobbies speak its messages)
    static const std::vector<Force> kForces = {
        {.key = "upscaler",
         .value = "auto",
         .when = all({asked_is("upscaler", {"off"}), is_not("frame_generation", {"off"})}),
         .reason = t::kOnForFrameGeneration},
        {.key = "upscale_quality",
         .value = "dlaa",
         .when = all({asked_is("upscaler", {"off"}), is_not("frame_generation", {"off"})}),
         .reason = t::kNativeForFrameGeneration},
        {.key = "reflex", .value = "on", .when = all({asked_is("reflex", {"off"}), dlss_frames()}), .reason = t::kOnForFrameGeneration},
        {.key = "rt_relit", .value = "1", .when = is_not("rt_sun_source", {"level"}), .reason = t::kOnForSun},
        {.key = "rt_relit", .value = "1", .when = tod_runs(), .reason = t::kOnForTimeOfDay},
        {.key = "thugpro_peer", .value = "1", .when = is("OnlineGameName", {"thugpro"}), .reason = t::kOnForThugproLobbies},
    };
    return kForces;
}

const std::vector<Conflict>& conflicts()
{
    static const std::vector<Conflict> kConflicts = {
        {.keys = {"frame_generation", "present_mode"},
         .when = all({is_not("frame_generation", {"off"}), is("present_mode", {"vsync"})}),
         .reason = t::kVsyncHoldsFrames},
        {.keys = {"frame_generation", "max_fps"},
         .when = all({is_not("frame_generation", {"off"}), computed(&past_refresh, {"frame_generation", "max_fps"})}),
         .reason = t::kPastRefresh},
        {.keys = {"rt_sun_source", "tod"},
         .when = all({computed(&sun_fixed, {"rt_sun_source"}), tod_runs()}),
         .reason = t::kFixedSunStopsClock},
    };
    return kConflicts;
}

const std::vector<PresetValue>& preset_values(int choice)
{
    // Original - every effect off, the game as it was; High - the cheaper effects (shadows, occlusion, reflections, the
    // sky and sun, HDR, bloom, smooth curves); Ultra - every effect on (but the bounce pass: relit lights the bounce).
    // Upscaling is left as it is (it trades sharpness for speed, whatever the preset).
    constexpr std::array kKeys = {"ray_tracing", "rt_shadows", "rt_ao", "rt_gi", "rt_reflections", "rt_relit",
                                  "rt_pbr",      "rt_fog",     "rt_sky", "rt_sky_model", "rt_clouds", "rt_sun",
                                  "rt_shafts",   "rt_bloom",   "rt_hdr", "rt_tessellation"};
    constexpr std::array<const char*, kKeys.size()> kOriginal = {"0", "0", "0", "0", "0", "0", "0", "0",
                                                                 "0", "0", "0", "0", "0", "0", "0", "0"};
    constexpr std::array<const char*, kKeys.size()> kHigh = {"1", "1", "1", "0", "1", "0", "0", "0",
                                                             "1", "1", "0", "1", "1", "1", "1", "1"};
    // (Ultra: the bounce pass off - the relit mode it turns on lights the bounce; one owner)
    constexpr std::array<const char*, kKeys.size()> kUltra = {"1", "1", "1", "0", "1", "1", "1", "1",
                                                              "1", "1", "1", "1", "1", "1", "1", "1"};
    const auto list = [&kKeys](const std::array<const char*, kKeys.size()>& values) {
        std::vector<PresetValue> made;
        made.reserve(kKeys.size());
        for (size_t i = 0; i < kKeys.size(); ++i) {
            made.push_back({.key = core::at(kKeys, i), .value = core::at(values, i)});
        }
        return made;
    };
    static const std::vector<PresetValue> kNone;
    static const std::vector<PresetValue> kOriginalList = list(kOriginal);
    static const std::vector<PresetValue> kHighList = list(kHigh);
    static const std::vector<PresetValue> kUltraList = list(kUltra);
    switch (choice) {
    case 1:
        return kOriginalList;
    case 2:
        return kHighList;
    case 3:
        return kUltraList;
    default:
        return kNone;
    }
}

} // namespace config
