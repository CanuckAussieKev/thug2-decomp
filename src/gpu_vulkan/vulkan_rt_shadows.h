#pragma once

#include <cstdint>
#include <volk.h>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - ray-traced sun shadows (docs/design/vulkan.md, R4; [Graphics] rt_shadows - gpu_vulkan.h's
// RtShadowSettings): after a viewport's opaque world is drawn (rt::opaque_view_drawn), one full-screen pass over the
// colour target reads the depth target, rebuilds each pixel's world position from the view's camera, traces a ray query
// toward the scene's sun through the scene the acceleration structures hold - one shadow model (lighting round A,
// docs/design/vulkan.md "Each light counted once"): the sun's visibility at every lit pixel (its world casters', the
// objects' and the clouds' share) takes the place of the light the pixel was drawn with -
//   - a relit world pixel (its light store, rt_cel.h, read at its triangle): its colour was written with the vertex's
//     own visibility; the pass scales it by its light with the pixel's visibility over its light with the vertex's
//     (the lamps, the sky and the bounce untouched) - lighter or darker;
//   - every other lit pixel (the skater, the objects, a world vertex the game animates): it loses its direct light's
//     share (rt_daylight.h direct_share_now - the lighting state's) where the sun doesn't reach it.
// No darkening over a bake: the passes need the level relit (the settings model), and a relit level holds none of the
// bake's light. (Retired with the old model: the shadow's strength, the bake's agreement grid and the sky test.)
//
// - The sun: the scene's (engine/gfx/rt_scene.h scene_sun - the clock's while it runs, else the level's own from its
//   relit data); the console's `rt sun` overrides it. None (a bake with no sun at its own time): no pass.
// - Multisampled targets: each sample traced on its own (the pass runs per sample, reading that sample's depth) when the
//   device has sample-rate shading, else per pixel from sample 0's depth (written to every sample).
// - The normal for the ray's offset comes from the depth: the neighbour on the nearer side along x and along y (not the
//   derivatives, which cross silhouettes). Surfaces facing away from the light count as shadowed without a ray.
// - Skipped pixels: the depth's far value (the sky, nothing drawn). Skipped passes: no directional light in the frame
//   record (rt::frame_record) - the last one seen is kept for frames that have none -, no scene (rt_shadows_set_scene).
// - The instance mask a ray is traced with (VkAccelerationStructureInstanceKHR::mask, set by the TLAS's builder):
//     kMaskWorld   0x01  opaque level geometry that casts shadows      - always traced
//     kMaskObjects 0x02  opaque objects: the skater, peds, vehicles    - traced with rt_shadow_objects=1
//     0x04               alpha-tested casters (foliage, fences, a cannon's spoked wheels) - traced by their alpha
//     0x08               (round PG) drawn apart: the clutter's items, the particles, the billboards - always traced
//     0x80               transparent / never a shadow caster           - never traced
//   The first hit ends the ray. The alpha-tested casters (added: their shadows were missing - a Boston cannon's wheels
//   cast only their rims, leaving a gap between the wheels' shadows and the barrel's) count where their material's
//   alpha test passes at the hit (the material table, R3, and the frame's geometry table) - in the game's frames; where
//   there is no geometry table for the scene traced (the check program's own structures) only the opaque casters are
//   traced, every hit opaque, as before.
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

// NOT ORIGINAL (the freeze audit) - whether this pass draws the clouds' shadows this frame: on, a scene and a sun, not
// left to the cel draws. Then the clouds' shadow is the sun's visibility here (vulkan_rt_sky_pass.h CloudShadowMap read
// by the pass: only the sun's own light drops - the sky's and the lamps' stay, and a pixel already in the sun's shade
// loses nothing more), and the sky pass draws no shadow of its own over the colour (it multiplied every light by the
// clouds' share: a street in a building's morning shade went near black under a blue sky). The caller holds the lock.
[[nodiscard]] bool rt_shadows_take_cloud_shadows(Backend& state);

// The pass's objects released (the device's shutdown; the caller holds the lock).
void release_rt_shadows(Backend& state);

} // namespace render::vulkan
