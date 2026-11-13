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
// NOT ORIGINAL - an art style (vulkan_style.h; docs/design/vulkan.md "Art style"): [Graphics] rt_style,
// rt_style_strength, rt_style_smooth, rt_style_bands, rt_style_outlines, rt_style_outline_width, rt_style_saturation -
// all put to use at once. A post-process over each viewport's world after the tone mapping and the upscaler, before the
// 2D. style 0 (the default): nothing changes. No ray tracing or HDR needed.
inline constexpr float kStyleSmooth = 4.0F;      // (rt_style_smooth's default)
inline constexpr float kStyleBands = 5.0F;       // (rt_style_bands')
inline constexpr float kStyleSaturation = 1.15F; // (rt_style_saturation's)
struct StyleSettings {
    unsigned style = 0;                  // 0: off; 1: toon
    float strength = 1.0F;               // the filtered picture over the original (0 - 1)
    float smooth = kStyleSmooth;         // the painterly smoothing's radius in pixels of a 720-line picture (0: none - 16)
    float bands = kStyleBands;           // lightness bands (0: none - 16)
    float outlines = 1.0F;               // the outlines' strength (0: none - 1)
    float outline_width = 1.0F;          // in pixels of a 720-line picture (0 - 4)
    float saturation = kStyleSaturation; // the colours' chroma x this (0 - 2)
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

} // namespace render::vulkan
