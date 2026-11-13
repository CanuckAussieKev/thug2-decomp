#pragma once

#include <cstdint>
#include <volk.h>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - ray-traced sun shadows (docs/design/vulkan.md, R4; [Graphics] rt_shadows - gpu_vulkan.h's
// RtShadowSettings): after a viewport's opaque world is drawn (rt::opaque_view_drawn), one full-screen pass over the
// colour target reads the depth target, rebuilds each pixel's world position from the view's camera, traces a ray query
// toward the frame's directional light through the scene the acceleration structures hold, and darkens the colour where
// the ray is blocked:
//     colour *= 1 - strength * occlusion
// It modulates what the original drew (its baked vertex lighting, its own shadow textures): it adds the shadows the
// world casts in the sun and takes nothing else away.
//
// - The sun (rt_shadow_sun): by default the one the level's lighting was baked with (engine/gfx/rt_sun.h - found from
//   the world's vertex colours in the level's first frame; the game's own directional lights are the same two in
//   every level and light the skater, not the level). A level whose bake shows no sun (indoors) gets no pass at all;
//   nor before the fit is made (a new world's first frame, before it is drawn). `light` traces toward the frame record's first
//   directional light, `x,y,z` toward a fixed direction; the console's `rt sun` overrides either (rt_scene.h).
// - Where the bake disagrees (rt_shadow_match_bake, with the baked sun): occlusion times the level's agreement grid at
//   the pixel's x, z (binding 2; rt_sun.h make_agreement) - none where the bake is bright although the sun is blocked.
// - The sky test (rt_shadow_covered=0, the default): a blocked pixel also traces rt_shadow_sky_rays rays up (straight up,
//   the rest slanted 31 degrees round it); none reaching the sky - a roof over it - leaves it as the level lit it, each
//   one that does lets in a share of the shadow (all of it from half the rays on).
//
// - Multisampled targets: each sample traced on its own (the pass runs per sample, reading that sample's depth) when the
//   device has sample-rate shading, else per pixel from sample 0's depth (written to every sample).
// - The normal for the ray's offset comes from the depth: the neighbour on the nearer side along x and along y (not the
//   derivatives, which cross silhouettes). Surfaces facing away from the light count as shadowed without a ray.
// - Skipped pixels: the depth's far value (the sky, nothing drawn). Skipped passes: no directional light in the frame
//   record (rt::frame_record) - the last one seen is kept for frames that have none -, no scene (rt_shadows_set_scene).
// - The instance mask a ray is traced with (VkAccelerationStructureInstanceKHR::mask, set by the TLAS's builder):
//     kMaskWorld   0x01  opaque level geometry that casts shadows      - always traced
//     kMaskObjects 0x02  opaque objects: the skater, peds, vehicles    - traced with rt_shadow_objects=1
//     0x04               alpha-tested casters (foliage, fences)        - not traced yet (needs the material table, R3)
//     0x80               transparent / never a shadow caster           - never traced
//   Every hit counts as opaque (gl_RayFlagsOpaqueEXT) and the first one ends the ray.
// - The skater: not a caster by default (rt_shadow_objects=0) - the original draws its own shadow under it, and a
//   low-poly body shadowing itself looks wrong - but it receives the world's shadows. First-person and the editor's
//   views are handled the same: any camera, any projection (the inverse of view x projection).
namespace render::vulkan {

struct Backend;

inline constexpr uint32_t kMaskWorld = 0x01;
inline constexpr uint32_t kMaskObjects = 0x02;

// The scene the rays are traced through: the frame's top-level acceleration structure, made by the TLAS's builder
// (R2), VK_NULL_HANDLE when there is none (the pass is then skipped, logged once).
// Contract for the caller:
//   - call it each frame before the frame's first view is drawn (or whenever the handle changes); the handle is used by
//     every pass recorded until the next call;
//   - the structure's build is recorded on the same queue earlier in submission order (the uploads command buffer, or
//     the stream before the views) - the pass records its own barrier from acceleration-structure build writes to its
//     fragment shader's reads;
//   - the structure (and the BLASes it points at) stays alive until the submissions that recorded passes with it have
//     finished (retired through the frames, as images and buffers are) - or set_scene(VK_NULL_HANDLE) first when a
//     destruction can't wait.
// Thread: any (it takes the backend's lock) - except while the caller already holds it: rt_shadows_set_scene_locked.
void rt_shadows_set_scene(VkAccelerationStructureKHR tlas);
void rt_shadows_set_scene_locked(VkAccelerationStructureKHR tlas);

// The usage a depth surface of `format` gets besides being an attachment: VK_IMAGE_USAGE_SAMPLED_BIT when rt_shadows
// is on, ray tracing is enabled and the format can be sampled, else none (make_depth_surface; the image's resting
// layout stays the attachment's).
[[nodiscard]] VkImageUsageFlags rt_shadow_depth_usage(const Backend& state, VkFormat format);

// The pass over the device's bound targets for a viewport (the listener's work, also called by the check program). The
// caller holds the lock. Returns whether a pass was recorded (false: skipped - off, no scene, no light, the targets
// can't be read; the reason logged once).
bool record_rt_shadow_pass(Backend& state, const rt::ViewDrawn& view);

// The passes recorded (for the check and the log).
[[nodiscard]] uint64_t rt_shadow_passes();

// The pass's objects released (the device's shutdown; the caller holds the lock).
void release_rt_shadows(Backend& state);

} // namespace render::vulkan
