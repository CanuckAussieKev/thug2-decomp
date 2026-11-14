#pragma once

#include <array>
#include <cstdint>
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
//     and objects - masks 0x01, 0x02; an object's pixel only the world's, as the shadows) stopping its size short of it
//     (a lamp's own fixture doesn't shadow it), its colour tinted by its material's texture's mean (the texture's
//     smallest level); the others' light added in the share of the traced ones' that got through;
//   - added = strength x albedo x that light: the blend adds it on a float target (HDR) and screens it on an 8-bit one
//     (x (1 - colour): never past white), as the bounce light's apply.
// Deterministic (no noise: nothing to denoise). The sky (the depth's far value) is left alone. Multisampled targets:
// sample 0's depth, the pixel's light written to every sample.
// Off (the default): no listener, no emitters wanted - nothing drawn differently.
namespace render::vulkan {

struct Backend;

inline constexpr uint32_t kLightsShadowMask = 0x01 | 0x02; // (the opaque world and objects)
inline constexpr uint32_t kLightsSurfaceMask = 0x01 | 0x02 | 0x04; // (the camera's ray: + the alpha-tested)
inline constexpr unsigned kMostShadowedLights = 8;

// The pass over the device's bound targets for a viewport (the listener's work, also called by the check program). The
// caller holds the lock. Returns whether it was recorded (false: off, no scene or material table, no lights in view, the
// targets can't be read).
bool record_rt_lights_pass(Backend& state, const rt::ViewDrawn& view);

// For the checks and the log.
[[nodiscard]] uint64_t rt_lights_passes();
[[nodiscard]] std::vector<rt::Emitter> rt_lights_last_view(); // the lights the last pass shone

// The pass's objects released (the device's shutdown; the caller holds the lock).
void release_rt_lights(Backend& state);

// The usage a depth surface of `format` needs for the pass (sampled while rt_lights is on and ray tracing enabled).
[[nodiscard]] VkImageUsageFlags rt_lights_depth_usage(const Backend& state, VkFormat format);

} // namespace render::vulkan
