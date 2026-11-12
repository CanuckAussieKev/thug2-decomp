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
    // although the sun is blocked - lamps, a roof or a caster the bake left out - none. Only with the baked sun.
    bool match_bake = true;
    // The sun traced toward (rt_shadow_sun): the one the level's lighting was baked with, found from its vertex colours
    // (engine/gfx/rt_sun.h - none in a level whose bake shows no sun: no shadows there); the frame's first directional
    // light (the game's, which lights the skater - the same in every level); or a fixed direction toward the sun.
    enum class SunSource : uint8_t { kBake, kLight, kFixed };
    SunSource sun = SunSource::kBake;
    std::array<float, 3> fixed_sun{0.0F, 1.0F, 0.0F};
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

// NOT ORIGINAL - ray-traced reflections (vulkan_rt_reflections.h, docs/design/vulkan.md R5): [Graphics] rt_reflections,
// rt_reflection_strength. Nothing is drawn differently while `on` is false, when ray tracing isn't enabled on the device,
// or where no surface is reflective.
inline constexpr float kRtReflectionStrength = 0.5F; // (rt_reflection_strength's default)
struct RtReflectionSettings {
    bool on = false;
    float strength = kRtReflectionStrength; // how far a fully reflective surface's colour goes toward what it mirrors (0 - 1)
};
void set_rt_reflection_settings(const RtReflectionSettings& settings);
[[nodiscard]] RtReflectionSettings rt_reflection_settings();

// NOT ORIGINAL - volumetric fog and light shafts (vulkan_rt_fog.h, docs/design/vulkan.md "Volumetric fog"): [Graphics]
// rt_fog, rt_fog_density, rt_fog_height, rt_fog_anisotropy, rt_fog_steps, rt_fog_mode, rt_fog_haze (and rt_hdr: the
// fog's light unclamped). Nothing is drawn differently while `on` is false, or when ray tracing isn't enabled on the device.
inline constexpr float kRtFogAnisotropy = 0.3F; // (rt_fog_anisotropy's default)
inline constexpr unsigned kRtFogSteps = 32;     // (rt_fog_steps' default)
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
};
void set_rt_fog_settings(const RtFogSettings& settings);
[[nodiscard]] RtFogSettings rt_fog_settings();

} // namespace render::vulkan
