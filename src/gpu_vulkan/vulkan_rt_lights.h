#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the night lighting: lamps, neon and lit windows as real lights ([Graphics] rt_lights - gpu_vulkan.h's
// RtLightSettings; docs/design/vulkan.md "Lights at night"). A listener after the bounce light (rt::ViewOrder::kLights),
// one full-screen pass over the colour target per viewport:
//   - the view's lights (engine/gfx/rt_lights.h select_view_lights: the frame record's point lights - the level's light
//     nodes, a script's lights - and the level's emitters, found once a world from its glows and self-lit surfaces),
//     those whose reach touches the view, the most important rt_lights_max of them, in a uniform block;
//   - per pixel: the world position and normal from the depth (as the shadows), the surface's albedo (a ray from the
//     camera finds its triangle and its material's texel - as the bounce light does; a pixel whose surface isn't found
//     is left alone), then every light's unshadowed light there - colour x intensity x falloff (rt_lights.h
//     light_falloff) x N.L -, the brightest rt_lights_shadowed of them traced: a ray query toward each (the opaque world
//     and objects - masks 0x01, 0x02 - and the cut-outs by their alpha, 0x04: vulkan_rt_scene.h cutout_rays_glsl; an
//     object's pixel only the world's, as the shadows) stopping its size short of it
//     (a lamp's own fixture doesn't shadow it), its colour tinted by its material's texture's mean (the texture's
//     smallest level); the others' light added in the share of the traced ones' that got through;
//   - added = strength x albedo x that light (lighting round A: no cap on the sum - the tone map compresses it; on the
//     relit world the albedo is the texture's x the light store's tint, and relit lights no lamps per vertex meanwhile): the blend adds it on a float target (HDR) and screens it on an 8-bit one
//     (x (1 - colour): never past white), as the bounce light's apply.
// Deterministic (no noise: nothing to denoise). The sky (the depth's far value) is left alone. Multisampled targets:
// sample 0's depth, the pixel's light written to every sample.
// Off (the default): no listener, no emitters wanted - nothing drawn differently.
namespace render::vulkan {

struct Backend;

// (+ the cut-outs, by their alpha; round PG: what is drawn apart - the clutter, particles, billboards)
inline constexpr uint32_t kLightsShadowMask = 0x01 | 0x02 | 0x04 | 0x08;
inline constexpr uint32_t kLightsSurfaceMask = 0x01 | 0x02 | 0x04; // (the camera's ray: + the alpha-tested)
inline constexpr unsigned kMostShadowedLights = 8;

// The pass over the device's bound targets for a viewport (the listener's work, also called by the check program). The
// caller holds the lock. Returns whether it was recorded (false: off, no scene or material table, no lights in view, the
// targets can't be read).
bool record_rt_lights_pass(Backend& state, const rt::ViewDrawn& view);

// For the checks and the log.
[[nodiscard]] uint64_t rt_lights_passes();
[[nodiscard]] std::vector<rt::Emitter> rt_lights_last_view(); // the lights the last pass shone

// (added by the volumetric light) The lamps the last pass lit - the view's most important first - and their light's
// scale (rt_lights_strength x the lamps on: the clock's dusk to dawn), and the frame they were lit in: the reflections' hits in
// the fallback tier take the same lamps (round WF: the fog's air is the light list's - vulkan_rt_fog.h). The caller holds the lock.
struct RtLitLamps {
    std::vector<rt::Emitter> lights;
    float scale = 0.0F;
    uint64_t frame = 0; // (0: never; Backend::frame otherwise)
};
[[nodiscard]] RtLitLamps rt_lights_lit_locked(const Backend& state);

// (round O - one light for everything) The lamps' record and the light one sends to a surface, as GLSL: `struct
// LightRecord { vec4 at; vec4 colour; vec4 more; vec4 shape; }` (at.xyz the position, at.w the size; colour.rgb colour x
// intensity, colour.w the reach; more.x the material's bits, more.y the core, more.z the shape; shape.xyz the axis or the
// panel's normal, shape.w a line's half length) and `float lamp_geometry(LightRecord light, vec3 here, vec3 normal, out
// vec3 from)` (engine/gfx/rt_lights.cpp lamp_geometry's model). The lamps pass and the reflections' hits read it.
[[nodiscard]] std::string_view rt_lamp_glsl();

// The pass's objects released (the device's shutdown; the caller holds the lock).
void release_rt_lights(Backend& state);

// The usage a depth surface of `format` needs for the pass (sampled while rt_lights is on and ray tracing enabled).
[[nodiscard]] VkImageUsageFlags rt_lights_depth_usage(const Backend& state, VkFormat format);

} // namespace render::vulkan
