#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/ui_backend.h"

// NOT ORIGINAL - Vulkan's render::Gpu and render::GpuContext (vulkan_gpu.cpp, vulkan_draws.cpp, vulkan_gpu_context.cpp;
// docs/design/vulkan.md, V6 and V7): the resources - textures in Direct3D 9's formats (BC, the 16-bit ones, P8
// expanded), vertex and index buffers, render targets, depth surfaces (D24S8, else D32S8), system-memory surfaces,
// copies and read-backs, occlusion queries, samplers - and the draws: pipelines from the state block, rendering passes
// inferred from the targets, the frames in flight, the present. app/ installs it with renderer=vulkan. No Vulkan header
// is seen through this one (tools/vulkan_headers.test.ts).
namespace render::vulkan {

// The Vulkan Gpu and its context (made on first use, never destroyed).
Gpu& gpu();
GpuContext& gpu_context();

// The UI backend for the editor's and the new front end's Dear ImGui parts (vulkan_ui_backend.cpp): ImGui's SDL3
// platform backend, its draw lists drawn by this Gpu into the back buffer, its images the Gpu's textures and targets.
UiBackend& ui_backend();

// Whether this machine can draw with the Vulkan Gpu: the loader, and the device the context would pick (`device_index`:
// -1 the best, discrete first) with what the draws need. Asked before the game's window is made.
struct Usable {
    bool usable = false;
    std::string device;  // the device's name (when one was found)
    std::string problem; // why not
};
Usable usable(int device_index);

// Before the context is made: the physical device to use (-1: the best), the folder the shader and pipeline caches are
// kept in (ending in a separator; empty: none kept), and whether to use the Khronos validation layer when it can be
// loaded (its messages go to the log).
struct Choices {
    int device_index = -1;
    std::string cache_folder;
    bool validation = false;
    bool ray_tracing = false; // [Graphics] ray_tracing: the ray-tracing extensions enabled where the device has them
};
void configure(const Choices& choices);

// NOT ORIGINAL - ray-traced sun shadows (vulkan_rt_shadows.h, docs/design/vulkan.md R4): [Graphics] rt_shadows,
// rt_shadow_strength, rt_shadow_softness, rt_shadow_rays, rt_shadow_objects. Nothing is drawn differently while `on` is
// false, or when ray tracing isn't enabled on the device (Choices::ray_tracing and the device's extensions).
inline constexpr float kRtShadowStrength = 0.5F; // (rt_shadow_strength's default)
inline constexpr unsigned kRtShadowSkyRays = 5;  // (rt_shadow_sky_rays')
struct RtShadowSettings {
    bool on = false;
    float strength = kRtShadowStrength; // how much of the colour a full shadow takes away (0 - 1)
    float softness = 0.0F; // the light's angular radius in degrees (0: hard edges, one ray; up to 5)
    unsigned rays = 4;     // rays per pixel (per sample) when soft (1 - 16)
    bool objects = false;  // the objects (the skater, peds, vehicles) cast shadows too
    // Shadows under roofs too (indoors, under bridges and awnings): false (the default) leaves a point that sees no sky
    // as the original lit it - its lamps are baked in - true darkens wherever the sun is blocked.
    bool covered = false;
    unsigned sky_rays = kRtShadowSkyRays; // the sky test's rays a pixel (1 - 16: straight up, the rest slanted round it)
    // Shadows only where the level's bake agrees (engine/gfx/rt_sun.h make_agreement): where its colours are bright
    // although the sun is blocked - lamps, a roof or a caster the bake left out - none. The grid is the baked sun's, or
    // with another scene sun (rt_sun_source=game) the one made from the relit colours for it (rt_scene.h
    // agreement_for); none made for it yet: the traced shadow everywhere.
    bool match_bake = true;
    // (changed by the scene's sun: the sun traced toward is rt_scene.h scene_sun - [Graphics] rt_sun_source -, every
    // effect's; rt_shadow_sun is retired)
};
void set_rt_shadow_settings(const RtShadowSettings& settings);
[[nodiscard]] RtShadowSettings rt_shadow_settings();

// NOT ORIGINAL - ray-traced ambient occlusion (vulkan_rt_effects.h, docs/design/vulkan.md R5): [Graphics] rt_ao,
// rt_ao_strength, rt_ao_radius, rt_ao_rays, rt_ao_denoise. Nothing is drawn differently while `on` is false, or when
// ray tracing isn't enabled on the device.
inline constexpr float kRtAoStrength = 0.6F; // (rt_ao_strength's default)
inline constexpr float kRtAoRadius = 48.0F;  // (rt_ao_radius's default: world units)
struct RtAoSettings {
    bool on = false;
    float strength = kRtAoStrength; // how much of the colour full occlusion takes away (0 - 1)
    float radius = kRtAoRadius;     // how far a ray looks for an occluder (1 - 1000 world units)
    unsigned rays = 4;              // rays per pixel each frame (1 - 16)
    bool denoise = true;            // the temporal and spatial filters (off: each frame's rays as they are)
};
void set_rt_ao_settings(const RtAoSettings& settings);
[[nodiscard]] RtAoSettings rt_ao_settings();

// NOT ORIGINAL - one-bounce ray-traced indirect light (vulkan_rt_effects.h, docs/design/vulkan.md "Global illumination"):
// [Graphics] rt_gi, rt_gi_strength, rt_gi_rays, rt_gi_distance, rt_gi_denoise. Nothing is drawn differently while `on`
// is false, or when ray tracing isn't enabled on the device.
inline constexpr float kRtGiStrength = 1.0F;   // (rt_gi_strength's default)
inline constexpr float kRtGiDistance = 600.0F; // (rt_gi_distance's default: world units)
struct RtGiSettings {
    bool on = false;
    float strength = kRtGiStrength; // how much of the bounced light is added (0 - 4)
    float distance = kRtGiDistance; // how far a ray looks for a surface to bounce from (1 - 5000 world units)
    unsigned rays = 2;              // rays per pixel each frame (1 - 16)
    bool denoise = true;            // the temporal and spatial filters (off: each frame's rays as they are)
};
void set_rt_gi_settings(const RtGiSettings& settings);
[[nodiscard]] RtGiSettings rt_gi_settings();

// NOT ORIGINAL - the night lighting: lamps, glows and lit windows as ray-traced lights (vulkan_rt_lights.h): [Graphics]
// rt_lights, rt_lights_strength, rt_lights_shadowed, rt_lights_max, rt_lights_emissive, rt_lights_level. Nothing is
// drawn differently while `on` is false, or when ray tracing isn't enabled on the device.
inline constexpr float kRtLightsStrength = 1.0F; // (rt_lights_strength's default)
struct RtLightSettings {
    bool on = false;
    float strength = kRtLightsStrength; // how much of the lights' light is added (0 - 8)
    unsigned shadowed = 4;              // the brightest lights at a pixel traced for shadows (1 - 8)
    unsigned max_lights = 64;           // lights a view at most (1 - 256), the most important
    float emissive = 1.0F;              // x the emitters' (glows, self-lit surfaces) intensity (0: none)
    float level = 1.0F;                 // x the level's own point lights' (0: none)
};
void set_rt_light_settings(const RtLightSettings& settings);
[[nodiscard]] RtLightSettings rt_light_settings();

// NOT ORIGINAL - ray-traced reflections (vulkan_rt_reflections.h, docs/design/vulkan.md R5): [Graphics] rt_reflections,
// rt_reflection_strength. Nothing is drawn differently while `on` is false, when ray tracing isn't enabled on the device,
// or where no surface is reflective.
inline constexpr float kRtReflectionStrength = 0.5F; // (rt_reflection_strength's default)
inline constexpr float kRtGlassRoughness = 0.05F;    // (rt_glass_roughness's default: radians)
inline constexpr float kRtGlassReflectance = 0.2F;   // (rt_glass_reflectance's default: glass's F0)
struct RtReflectionSettings {
    bool on = false;
    float strength = kRtReflectionStrength; // how far a fully reflective surface's colour goes toward what it mirrors (0 - 1)
    // (added for the water and glass) rt_reflection_fresnel: how much a surface reflects depends on the angle it is
    // seen at (Schlick's Fresnel) - water and glass little face-on and a mirror at grazing angles, other shiny surfaces
    // as before face-on and more at grazing angles; false: the same at every angle (reflectivity x strength, as before).
    bool fresnel = true;
    // rt_glass_roughness: the half-angle of the cone glass reflects into (radians; 0: a perfect mirror) - a few rays
    // a pixel, blurred over its neighbours.
    float glass_roughness = kRtGlassRoughness;
    // rt_glass_reflectance: glass's reflectance face-on (Schlick's F0). Real glass's is 0.04 - a real window is dark
    // behind; the game's panes are bright textures, and at 0.04 their reflection all but vanished (AU's glass building:
    // a mean change of 1.4 levels against the mirror's 12), so the default is 0.2.
    float glass_reflectance = kRtGlassReflectance;
};
void set_rt_reflection_settings(const RtReflectionSettings& settings);
[[nodiscard]] RtReflectionSettings rt_reflection_settings();

// NOT ORIGINAL - HDR rendering, tone mapping and colour grading (vulkan_hdr.h, docs/design/vulkan.md "HDR"): [Graphics]
// rt_hdr, rt_exposure, rt_tonemap, rt_saturation, rt_contrast, rt_white_point, rt_grade_lut. Independent of ray
// tracing. While `on` is false nothing changes; on, the world is drawn into 16-bit float targets and brought back to the
// display's 0 - 1 by a tone-mapping pass before the 2D. Read when the device is made (`on`; the rest any time).
inline constexpr float kHdrWhitePoint = 1.25F; // (rt_white_point's default)
struct HdrSettings {
    bool on = false;
    float exposure = 0.0F;   // stops (0: 1x; +1: twice the light)
    unsigned tonemap = 0;    // 0: the colours as drawn, a soft shoulder from just below 1 to the white point;
                             // 1: ACES (Narkowicz's fit), applied to the colours made linear (gamma 2.2)
    float saturation = 1.0F; // 1: unchanged; 0: grey; above 1: more
    float contrast = 1.0F;   // 1: unchanged; a power around mid-grey (0.5)
    float white_point = kHdrWhitePoint; // the value (1 - 8) shown as white; 1: everything above 1 clipped
    std::string grade_lut;   // a 3D LUT (.cube) applied last; empty: none (a path, as given)
};
void set_hdr_settings(const HdrSettings& settings);
[[nodiscard]] HdrSettings hdr_settings();
// NOT ORIGINAL - volumetric fog and light shafts (vulkan_rt_fog.h, docs/design/vulkan.md "Volumetric fog"): [Graphics]
// rt_fog, rt_fog_density, rt_fog_height, rt_fog_anisotropy, rt_fog_steps, rt_fog_mode, rt_fog_haze (and rt_hdr: the
// fog's light unclamped). Nothing is drawn differently while `on` is false, or when ray tracing isn't enabled on the device.
inline constexpr float kRtFogAnisotropy = 0.3F; // (rt_fog_anisotropy's default)
inline constexpr unsigned kRtFogSteps = 32;     // (rt_fog_steps' default)
inline constexpr float kRtFogHazeHeight = 600.0F; // (rt_fog_haze_height's default: world units)
inline constexpr float kRtFogHazeDensity = 0.3F;  // (rt_fog_haze_density's default)
enum class RtFogMode : uint8_t {
    kReplace = 0, // the level's own fog replaced by the volumetric one (its colour and distances; shadowed by the world)
    kShafts = 1,  // the level's own fog kept, only the sunlit shafts' light added over it
};
struct RtFogSettings {
    bool on = false;
    float density = 1.0F;                 // x the level's fog thickness (1: as thick as the game's; 0 - 8)
    float height = 0.0F;                  // world units over which the fog thins by e above the camera (0: even)
    float anisotropy = kRtFogAnisotropy;  // Henyey-Greenstein g: 0 even, toward 0.9 bright toward the sun (-0.9 - 0.9)
    unsigned steps = kRtFogSteps;         // samples along each ray (4 - 128)
    RtFogMode mode = RtFogMode::kReplace;
    bool hdr = false;                     // the light added above 1 kept (rt_hdr=1: a floating-point target)
    bool haze = true; // a level without fog gets a thin haze's sunlit shafts (rt_fog_haze; 0: nothing there)
    // (added by the sun's glare) With the sky model (rt_sky_model=1) the haze is a medium of its own - aerial
    // perspective: it dims what is far toward the sky's colour, thicker near the ground: its thickness (x the default:
    // rt_fog_haze_density) and the height over which it thins by e (rt_fog_haze_height, world units above the level's
    // ground). The default thickness was 1 until the water and glass work: Australia's sea a mile out read milky
    // white and Barcelona's harbour whited out toward a low sun; 0.3 keeps the far world soft (no seam where AU's sea
    // meets its sky dome) and the near sea its own colour.
    float haze_density = kRtFogHazeDensity;
    float haze_height = kRtFogHazeHeight;
};
void set_rt_fog_settings(const RtFogSettings& settings);
[[nodiscard]] RtFogSettings rt_fog_settings();

// NOT ORIGINAL - the sun, its glare, the screen-space rays and the bloom (vulkan_rt_sun.h, vulkan_rt_sky.h;
// docs/design/vulkan.md "The sun, its glare and the sky"): [Graphics] rt_sun, rt_sun_size, rt_bloom, rt_bloom_strength,
// rt_shafts, rt_shaft_strength, rt_sky_model, rt_sun_colour, rt_lens_dirt (and rt_hdr). Nothing is drawn differently
// while all are off. The sun's parts need a sun - where the game shows one, else the level's baked one (ray tracing
// enabled); the bloom needs neither.
inline constexpr float kRtBloomStrength = 0.05F; // (rt_bloom_strength's default)
inline constexpr float kRtShaftStrength = 0.8F;  // (rt_shaft_strength's default)
struct RtSunSettings {
    bool sun = false;          // the sun's disc and glare drawn, the original's sun sprite and lens flare hidden
    float sun_size = 1.0F;     // x the disc's default size (0.25 - 4)
    bool bloom = false;        // the frame's bright light spread (energy-conserving, a mip chain)
    float bloom_strength = kRtBloomStrength; // the share of each pixel's light spread (0 - 0.5)
    bool shafts = false;       // screen-space rays from the sun over what blocks it
    float shaft_strength = kRtShaftStrength; // (0 - 4)
    bool sky_model = false;    // the sky model's colours used by the fog (aerial perspective) and the bounce light
    bool colour_set = false;   // rt_sun_colour=r,g,b: the sun's colour given (0 - 1 each), not the model's
    std::array<float, 3> colour{1.0F, 1.0F, 1.0F};
    bool lens_dirt = false;    // a faint dirt on the lens lit by the bloom (procedural)
    bool hdr = false;          // rt_hdr=1: the light kept above 1 (the tone mapping maps it)
};
void set_rt_sun_settings(const RtSunSettings& settings);
[[nodiscard]] RtSunSettings rt_sun_settings();

// NOT ORIGINAL - a modern sky (vulkan_rt_sky_pass.h, docs/design/vulkan.md "The sky"): [Graphics] rt_sky, rt_sky_match,
// rt_clouds, rt_cloud_coverage, rt_cloud_speed, rt_cloud_quality, rt_cloud_shadows (and rt_hdr). Nothing is drawn
// differently while `on` is false: the level's own sky dome as always. No ray tracing needed (the drawn sun comes from
// the game; with ray tracing the baked sun is the fallback).
inline constexpr float kRtSkyMatch = 0.8F;        // (rt_sky_match's default)
inline constexpr float kRtCloudCoverageAuto = -1.0F;
struct RtSkySettings {
    bool on = false;
    float match = kRtSkyMatch;     // how far each level's sky keeps its own colours and clouds (0: the model alone - 1)
    bool clouds = false;           // volumetric clouds in the sky
    float coverage = kRtCloudCoverageAuto; // 0 - 1, or auto (negative): from the level's own sky
    float speed = 1.0F;            // x the clouds' drift (0: still)
    unsigned quality = 1;          // 0: quarter resolution, 32 steps; 1: half, 48; 2: full, 96
    bool cloud_shadows = false;    // the clouds' shadows on the level (darkening, not ray traced)
    bool hdr = false;              // rt_hdr=1: the sky's light above 1 kept
};
void set_rt_sky_settings(const RtSkySettings& settings);
[[nodiscard]] RtSkySettings rt_sky_settings();
// NOT ORIGINAL - upscaling (vulkan_upscale.h, docs/design/vulkan.md "Upscaling"): [Graphics] upscaler,
// upscale_quality, ray_reconstruction, upscale_sharpness. The world drawn at a lower resolution (jittered, with motion
// vectors) and brought up to the window's by NVIDIA DLSS (Streamline) or AMD FSR before the 2D; kOff (the default):
// nothing changes. The libraries are fetched at build time (64-bit builds) and loaded at run time from beside the exe:
// without them, or on a device they don't run on, the setting falls back (dlss to fsr, fsr to off), logged. Read
// when the device is made. Multisampling is off while upscaling (the world's target must have one sample).
enum class Upscaler : uint8_t { kOff, kDlss, kFsr, kAuto };
enum class UpscaleQuality : uint8_t {
    kNative,      // dlaa: 100% - anti-aliasing alone (DLAA / FSR native AA)
    kQuality,     // 67%
    kBalanced,    // 58%
    kPerformance, // 50%
    kUltra,       // ultra performance: 33%
};
struct UpscaleSettings {
    Upscaler upscaler = Upscaler::kOff;
    UpscaleQuality quality = UpscaleQuality::kQuality;
    bool ray_reconstruction = false; // DLSS Ray Reconstruction denoises the ray-traced effects (NVIDIA, dlss)
    float sharpness = 0.0F;          // 0 - 1 (FSR's sharpening pass; 0: none)
    unsigned fsr_major = 0;          // fsr_version: 0 the newest the device runs (FSR 4 where it can), 3: FSR 3.1
};
void set_upscale_settings(const UpscaleSettings& settings);
[[nodiscard]] UpscaleSettings upscale_settings();
// Which provider runs ("DLSS" / "FSR"...; null: none yet / none - the fallbacks may differ from the ini's request).
[[nodiscard]] const char* upscale_provider_name();
// The share of the output's width and height a quality draws (1, 0.667, 0.588, 0.5, 0.333).
[[nodiscard]] float upscale_ratio(UpscaleQuality quality);
// Parsed from the ini's words (off / dlss / fsr / auto; dlaa / native / quality / balanced / performance / ultra);
// false: not one of them.
bool parse_upscaler(const std::string& text, Upscaler* upscaler);
bool parse_upscale_quality(const std::string& text, UpscaleQuality* quality);
// NOT ORIGINAL - per-pixel lighting and surface maps (vulkan_surface.h; docs/design/vulkan.md "Per-pixel lighting and
// surface maps"): [Graphics] rt_pbr, rt_pbr_specular, rt_pbr_normal_strength, rt_pbr_objects. Off: no map is read and
// every shader is as before. No ray tracing needed; with it the bake's sun (rt_sun.h) is the light the maps answer to.
inline constexpr float kRtPbrRoughness = 0.7F;    // (a surface without a map)
inline constexpr float kRtPbrF0 = 0.04F;          // (a dielectric's)
inline constexpr float kRtPbrAmbientShare = 0.45F;
inline constexpr float kRtPbrSkyDetail = 0.5F;
inline constexpr float kRtPbrShadeLow = 0.35F;
inline constexpr float kRtPbrShadeHigh = 0.8F;
inline constexpr float kRtPbrDetailLow = 0.55F;
inline constexpr float kRtPbrDetailHigh = 1.5F;
inline constexpr float kRtPbrGameSun = 0.5F;
struct RtPbrSettings {
    bool on = false;
    float specular = 1.0F;        // rt_pbr_specular: the specular term's strength (0: none)
    float normal_strength = 1.0F; // rt_pbr_normal_strength: the maps' normals' tilt (0: flat, 2: twice as steep)
    float roughness = kRtPbrRoughness;        // the roughness of a surface without a map (objects lit per pixel)
    float f0 = kRtPbrF0;                      // F0 without a map (dielectric)
    float ambient_share = kRtPbrAmbientShare; // a: the share of a sunlit surface's baked light not from the sun
    float sky_detail = kRtPbrSkyDetail;       // how much of a map's detail shows in the shade (toward the sky)
    float shade_low = kRtPbrShadeLow;   // the bake's light level (luma of 2 x v0) at and below which a surface is in shade
    float shade_high = kRtPbrShadeHigh; // ... and from which it is fully sunlit
    float detail_low = kRtPbrDetailLow;   // the bake's scale's range (the map can darken a texel this much at most)
    float detail_high = kRtPbrDetailHigh; // (and brighten it this much)
    float game_sun = kRtPbrGameSun; // without a baked sun (no ray tracing): the game's shown sun's weight (0: none)
    bool objects = true;            // rt_pbr_objects: objects lit by lights per pixel too
};
void set_rt_pbr_settings(const RtPbrSettings& settings);
[[nodiscard]] RtPbrSettings rt_pbr_settings();
// NOT ORIGINAL - detail materials and de-lighting (vulkan_detail.h; docs/design/vulkan.md "Detail materials and
// de-lighting"): [Graphics] rt_detail, rt_detail_strength, rt_delight, rt_delight_strength. They act on the draws rt_pbr
// lights with a surface map (nothing without rt_pbr). rt_detail reads the detail-material library
// (thug2-decomp-materials.t2dm, made by tools/detail_materials). Off: every shader is as before.
inline constexpr float kRtDetailFadeStart = 0.5F; // (the detail's level of detail - log2 texels a pixel - where it fades)
inline constexpr float kRtDetailFadeEnd = 3.0F;   // (... and where it is gone)
inline constexpr float kRtDelightLimit = 1.6F;    // (the most the de-lighting brightens or darkens a texel: a factor)
struct RtDetailSettings {
    bool on = false;          // rt_detail: the detail layer and the kinds' real materials
    float strength = 1.0F;    // rt_detail_strength: the detail layer's strength (0 - 2)
    bool delight = false;     // rt_delight: the colour textures' painted light flattened
    float delight_strength = 0.5F; // rt_delight_strength: 0 - 1 (1: flattened to the texture's mean)
    float fade_start = kRtDetailFadeStart;
    float fade_end = kRtDetailFadeEnd;
    float delight_limit = kRtDelightLimit;
};
void set_rt_detail_settings(const RtDetailSettings& settings);
[[nodiscard]] RtDetailSettings rt_detail_settings();
// NOT ORIGINAL - an art style (vulkan_style.h; docs/design/vulkan.md "Art style"): [Graphics] rt_style and its
// rt_style_* knobs - all put to use at once. A post-process over each viewport's world after the tone mapping and the
// upscaler, before the 2D. off (the default): nothing changes. No ray tracing or HDR needed.
// rt_style's names, in this order (their numbers: the old 0 / 1 still read as off / toon). cel is no post-process: it
// is cel shading, the rendering mode (engine/gfx/rt_cel.h - app/renderer_choice.cpp turns it on with this value).
enum class ArtStyle : uint8_t {
    kOff = 0,
    kToon = 1,        // flat paint, soft shade bands, dark tinted ink (vulkan_style.cpp)
    kCel = 2,         // cel shading: the light in bands where it is worked out, ink round objects (vulkan_cel.h)
    kComic = 3,       // a skate-zine comic: bold black ink, halftone dots, posterized fills (vulkan_style_looks.cpp)
    kSketch = 4,      // pencil on paper: cross-hatching by darkness, graphite lines, paper grain (vulkan_style_looks.cpp)
    kVhs = 5,         // a 90s camcorder tape: fisheye, colour bleed, wobble, REC (vulkan_style_vhs.cpp)
    kRetro = 6,       // a 32-bit console: snapped vertices, warped textures, few colours (vulkan_style_retro.cpp)
    kWatercolour = 7, // a wash painting on paper (vulkan_style_watercolour.cpp)
    kNoir = 8,        // a high-contrast duotone, film grain, a vignette (vulkan_style_looks.cpp)
    kThermal = 9,     // a false-colour heat camera (vulkan_style_looks.cpp)
};
inline constexpr unsigned kArtStyleCount = 10;
// Whether a style is a post-process of its own (vulkan_style.h's pass): every one but off and cel.
[[nodiscard]] constexpr bool style_has_pass(ArtStyle style) { return style != ArtStyle::kOff && style != ArtStyle::kCel; }
// "off", "toon", "comic", ... (an unknown value: "off").
[[nodiscard]] const char* art_style_name(ArtStyle style);
// A name (any case) or the old numbers 0 / 1; false (the style untouched) when the text is neither.
bool parse_art_style(const std::string& text, ArtStyle* style);

inline constexpr float kStyleSmooth = 4.0F;      // (rt_style_smooth's default)
inline constexpr float kStyleBands = 5.0F;       // (rt_style_bands')
inline constexpr float kStyleSaturation = 1.15F; // (rt_style_saturation's)
inline constexpr float kStyleHalftone = 5.0F;    // (rt_style_comic_halftone's)
inline constexpr float kStyleHatch = 5.0F;       // (rt_style_sketch_hatch's)
inline constexpr float kStyleWash = 0.35F;       // (rt_style_sketch_wash's)
inline constexpr float kStylePaper = 0.5F;       // (rt_style_paper's)
inline constexpr float kStyleGrain = 0.5F;       // (rt_style_grain's)
inline constexpr float kStyleVignette = 0.5F;    // (rt_style_vignette's)
inline constexpr float kStyleContrast = 1.8F;    // (rt_style_noir_contrast's)
inline constexpr float kStyleFocus = 0.6F;       // (rt_style_thermal_focus')
inline constexpr std::array<float, 3> kStyleNoirDark{0.0F, 0.0F, 0.0F};       // (rt_style_noir_dark's: black)
inline constexpr std::array<float, 3> kStyleNoirLight{0.95F, 0.92F, 0.84F};   // (rt_style_noir_light's: warm paper)
struct StyleSettings {
    ArtStyle style = ArtStyle::kOff;
    float strength = 1.0F;               // the filtered picture over the original (0 - 1)
    float smooth = kStyleSmooth;         // the painterly smoothing's radius in pixels of a 720-line picture (0: none - 16)
    float bands = kStyleBands;           // lightness bands (0: none - 16)
    float outlines = 1.0F;               // the outlines' strength (0: none - 1)
    float outline_width = 1.0F;          // in pixels of a 720-line picture (0 - 4)
    float saturation = kStyleSaturation; // the colours' chroma x this (0 - 2)
    // (added by comic, sketch, noir and thermal - vulkan_style_looks.h)
    float halftone = kStyleHalftone;     // comic: the halftone screen's dot spacing in 720-line pixels (0: none - 16)
    float hatch = kStyleHatch;           // sketch: the hatching strokes' spacing in 720-line pixels (2 - 16)
    float wash = kStyleWash;             // sketch: the colour wash under the pencil (0: grey - 1)
    float paper = kStylePaper;           // comic, sketch: the paper's tint and grain (0: none - 1)
    float grain = kStyleGrain;           // noir: film grain; thermal: sensor noise; sketch, comic: paper fibre (0 - 1)
    float vignette = kStyleVignette;     // noir, thermal: the corners darkened (0 - 1)
    float contrast = kStyleContrast;     // noir: the tone curve's steepness (0.5 - 4)
    float focus = kStyleFocus;           // thermal: how much warmer things standing out in front glow (0 - 1)
    std::array<float, 3> noir_dark = kStyleNoirDark;   // noir's shadow colour (r, g, b 0 - 1)
    std::array<float, 3> noir_light = kStyleNoirLight; // ... and highlight colour
};
void set_style_settings(const StyleSettings& settings);
[[nodiscard]] StyleSettings style_settings();
// NOT ORIGINAL - grass (vulkan_grass.h; docs/design/vulkan.md "Grass"): [Graphics] grass, grass_density, grass_height,
// grass_distance - all put to use at once. Blades on the ground where the level's collision says grass, drawn after the
// opaque world and the ray-traced effects (each blade takes the shaded ground at its root). `on` false (the default): nothing is gathered or
// drawn. No ray tracing needed.
inline constexpr float kGrassDistance = 50.0F; // (grass_distance's default: metres)
struct GrassSettings {
    bool on = false;
    float density = 1.0F;              // x the blades a square metre (0.25 - 4)
    float height = 1.0F;               // x the blades' height (0.25 - 3)
    float distance = kGrassDistance;   // metres to where the blades have faded into the ground (10 - 200)
};
void set_grass_settings(const GrassSettings& settings);
[[nodiscard]] GrassSettings grass_settings();
// NOT ORIGINAL - water shading (vulkan_water.h; docs/design/vulkan.md "Water"): [Graphics] rt_water, rt_water_waves,
// rt_water_wind, rt_water_wind_heading, rt_water_clarity, rt_water_foam, rt_water_tint, rt_water_geometry. The game's
// water surfaces (the moving materials render_pc marks - engine/gfx/water_surfaces.h - where they face up) drawn with
// waves (with rt_water_geometry, its surface moved by them too), a reflection,
// refraction, colour by depth, shoreline foam and the sun's glitter. on false (the default): every draw as before. No
// ray tracing needed (with it, the baked sun lights the glitter); Direct3D 9 ignores it.
inline constexpr float kWaterHeadingAuto = -1.0F; // rt_water_wind_heading=auto: the level's own
inline constexpr std::array<float, 3> kWaterTint{0.2F, 0.45F, 0.5F}; // (a set tint's default: a harbour's green-blue)
struct WaterSettings {
    bool on = false;
    float waves = 1.0F;   // x the level's wave height (0: flat - 3)
    float wind = 1.0F;    // the wind's strength: the ripples' size and speed (0 - 2)
    float heading = kWaterHeadingAuto; // the way the wind blows, degrees from world +x toward +z (negative: the level's)
    float clarity = 1.0F; // x how far one sees into the level's water (0: opaque - 4)
    float foam = 1.0F;    // the shoreline foam (0: none - 2)
    bool tint_auto = true;              // rt_water_tint=auto: the water's own colour as the game draws it
    std::array<float, 3> tint = kWaterTint; // rt_water_tint=r,g,b (0 - 1)
    bool hdr = false;     // rt_hdr=1: the sun's glitter above 1 kept
    bool screen_reflections = true; // the reflections marched over the screen (else the sky alone)
    bool debug_view = false;        // [debug] rt_water_view=1: where the reflection was found, in colours
    // rt_water_geometry=1 (the default): the water's surface moved by its swell - its meshes split into a grid as a level
    // loads (render_pc/water_geometry.h; a level loaded while this or rt_water was off stays flat until it loads again)
    bool geometry = true;
};
void set_water_settings(const WaterSettings& settings);
[[nodiscard]] WaterSettings water_settings();
// NOT ORIGINAL - the camera effects (vulkan_camera_fx.h; docs/design/vulkan.md "Camera effects"): [Graphics]
// rt_motion_blur, rt_motion_blur_strength, rt_dof, rt_dof_strength, rt_grade, rt_grade_strength, rt_grade_vignette,
// rt_grade_grain, rt_grade_levels, rt_fisheye and its knobs (vulkan_fisheye.h) - all put to use at once.
// Post-processes over each viewport's world: motion blur and depth of field after the upscaler, the fisheye lens after
// them, the grade after the art style, all before the 2D. Everything off (the default): nothing changes - no pass, no
// motion vectors, the camera's field of view as the game sets it. No ray tracing or HDR needed.
// rt_grade's looks (tables made here from formulas, no files): neutral changes no colour (the vignette and grain only),
// level the look rt_grade_levels names for the level being played (neutral when it names none).
enum class GradeLook : uint8_t {
    kOff = 0,
    kNeutral = 1,
    kWarm = 2,        // late afternoon sun: warm highlights, a touch of gold
    kCool = 3,        // overcast morning: blue-green shadows, cooler whites
    kFilmic = 4,      // a film print: a soft S curve, rich mid-tones, quieter highlights
    kBleach = 5,      // bleach bypass: silvery, low saturation, hard contrast
    kSkateVideo = 6,  // a 2000s skate video: punchy colour, deep blacks, teal shadows and warm skin
    kLevel = 7,       // the level's own (rt_grade_levels)
};
inline constexpr unsigned kGradeLookCount = 8;
// "off", "neutral", ... "skate_video", "level" (an unknown value: "off").
[[nodiscard]] const char* grade_look_name(GradeLook look);
// A name (any case; "skate video" and "skatevideo" read as skate_video); false (untouched) when it is none.
bool parse_grade_look(const std::string& text, GradeLook* look);
inline constexpr float kMotionBlurShutter = 0.5F; // (rt_motion_blur_strength's default: a 180-degree shutter)
inline constexpr float kDofStrength = 0.4F;       // (rt_dof_strength's: mild)
inline constexpr float kFisheyeStrength = 0.75F;  // (rt_fisheye_strength's: equisolid - fisheye_lens.h kStrengthDefault)
inline constexpr float kFisheyeVignette = 0.5F;   // (rt_fisheye_vignette's)
struct CameraFxSettings {
    bool motion_blur = false;
    float motion_blur_strength = kMotionBlurShutter; // the shutter's share of the frame's time (0 - 1)
    bool dof = false;
    float dof_strength = kDofStrength; // the aperture (0 - 1): how far from the focus things go soft
    GradeLook grade = GradeLook::kOff;
    float grade_strength = 1.0F; // the look over the picture (0 - 1)
    float grade_vignette = 0.0F; // with a look: the corners darkened (0 - 1)
    float grade_grain = 0.0F;    // with a look: film grain (0 - 1)
    std::string grade_levels;    // rt_grade_levels: "<scene>:<look>,..." - the look of each level under rt_grade=level
    // [Graphics] rt_fisheye - the fisheye lens (vulkan_fisheye.h; engine/gfx/fisheye_lens.h): the camera's field of view
    // widened and the picture drawn through a 90s fisheye adapter. strength: the distortion and the widening together
    // (0 none, 0.5 equidistant, 0.75 equisolid, 1 orthographic); vignette: the adapter's dark corners; circle: 0 the
    // whole frame, 1 a round frame touching the picture's top and bottom.
    bool fisheye = false;
    float fisheye_strength = kFisheyeStrength;
    float fisheye_vignette = kFisheyeVignette;
    float fisheye_circle = 0.0F;
};
void set_camera_fx_settings(const CameraFxSettings& settings);
[[nodiscard]] CameraFxSettings camera_fx_settings();

} // namespace render::vulkan
