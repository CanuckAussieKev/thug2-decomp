#pragma once

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
struct RtShadowSettings {
    bool on = false;
    float strength = kRtShadowStrength; // how much of the colour a full shadow takes away (0 - 1)
    float softness = 0.0F; // the light's angular radius in degrees (0: hard edges, one ray; up to 5)
    unsigned rays = 4;     // rays per pixel (per sample) when soft (1 - 16)
    bool objects = false;  // the objects (the skater, peds, vehicles) cast shadows too
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
struct RtReflectionSettings {
    bool on = false;
    float strength = kRtReflectionStrength; // how far a fully reflective surface's colour goes toward what it mirrors (0 - 1)
};
void set_rt_reflection_settings(const RtReflectionSettings& settings);
[[nodiscard]] RtReflectionSettings rt_reflection_settings();

} // namespace render::vulkan
