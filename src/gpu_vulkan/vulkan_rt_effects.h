#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <volk.h>

#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - ray-traced ambient occlusion and its denoiser (docs/design/vulkan.md, R5; [Graphics] rt_ao - gpu_vulkan.h's
// RtAoSettings). After a viewport's opaque world is drawn (rt::opaque_view_drawn, the point R4's shadows use too), a few
// compute passes over images of the viewport's size, then one full-screen pass over the colour target:
//   1. trace: each pixel's world position rebuilt from the depth (sample 0 of a multisampled one) and the view's camera,
//      its normal from the nearer neighbours' depths (as the shadows do); `rays` ray queries over the cosine-weighted
//      hemisphere around the normal, no longer than `radius` world units; the pixel's occlusion is the share that hit
//      something (0 - 1). The directions: a 2D low-discrepancy sequence (R2) advanced each frame, turned per pixel by a
//      hash of its position - the same each run, so the check can work them out on the processor. The position and
//      normal are kept beside it (the guide);
//   2. temporal accumulation: the pixel's world position projected into the previous frame's view of this viewport; the
//      history read there (bilinear over the four texels whose own guide agrees: on the pixel's plane, a normal within
//      ~25 degrees), clamped to the 3x3 neighbourhood's spread of this frame's occlusion (plus a noise floor for the
//      rays), then blended with this frame's by 1 / the history's length (up to kAoMaxHistory frames). Nothing agrees
//      (disoccluded, off screen, a cut): the history starts again;
//   3. spatial: kAoFilterPasses a-trous passes (a 5x5 B3-spline kernel at steps 1, 2, 4...) whose weights fall with the
//      normals' difference, the distance from the pixel's plane and the occlusion's difference against the noise the
//      history's length leaves;
//   4. apply: colour *= 1 - strength * occlusion (a multiplying blend; alpha kept; every sample of a multisampled target
//      the pixel's factor). The sky (depth's far value) is left alone.
// rt_ao_denoise=0 applies step 1's occlusion as it is (for the check, and to see the raw rays).
// - The instance mask: 0x01 opaque world and 0x02 opaque objects occlude (alpha-tested 0x04 and transparent 0x80 don't,
//   until the material table's alpha test runs in the rays); every hit is opaque, the first ends the ray.
// - The history is kept per viewport (its rectangle and target size): up to kAoViewSlots, the least recently drawn
//   dropped. A viewport not drawn in the frame before starts again.
namespace render::vulkan {

struct Backend;

inline constexpr uint32_t kAoMaxHistory = 32;
inline constexpr uint32_t kAoFilterPasses = 3;
inline constexpr uint32_t kAoViewSlots = 4;
inline constexpr uint32_t kAoMask = 0x01 | 0x02; // (world, objects)

// The scene the rays are traced through: the frame's top-level acceleration structure (R2), VK_NULL_HANDLE when there is
// none (the pass is then skipped, logged once). The same contract as rt_shadows_set_scene (vulkan_rt_shadows.h): set it
// each frame before the first view is drawn, the build recorded earlier on the same queue (the pass has its own barrier
// from the build to its compute shader), the structures alive until the submissions that used them have finished.
// Thread: any (it takes the backend's lock) - rt_effects_set_scene_locked when the caller already holds it.
void rt_effects_set_scene(VkAccelerationStructureKHR tlas);
void rt_effects_set_scene_locked(VkAccelerationStructureKHR tlas);

// The usage a depth surface of `format` gets besides being an attachment: VK_IMAGE_USAGE_SAMPLED_BIT when rt_ao is on,
// ray tracing is enabled and the format can be sampled, else none (make_depth_surface, with rt_shadow_depth_usage's).
[[nodiscard]] VkImageUsageFlags rt_effects_depth_usage(const Backend& state, VkFormat format);

// The passes over the device's bound targets for a viewport (the listener's work, also called by the check program).
// The caller holds the lock. Returns whether they were recorded (false: skipped - off, no scene, the targets can't be
// read; the reason logged once).
bool record_rt_ao_pass(Backend& state, const rt::ViewDrawn& view);

// The AO passes recorded (for the check and the log).
[[nodiscard]] uint64_t rt_ao_passes();

// Every viewport's history forgotten (the next pass starts again; the frame index of the directions back to 0). The
// caller holds the lock.
void rt_effects_reset_history(Backend& state);

// For the check: one of the images of the viewport last drawn with `rect` read back (the frame's work finished first):
// width x height texels of 4 floats each.
enum class RtAoImage : uint8_t {
    kRaw,     // step 1: r occlusion
    kHistory, // step 2: r occlusion, g the history's length
    kFinal,   // what step 4 applied: r occlusion, g the history's length
};
struct RtAoTexels {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::array<float, 4>> texels;
};
bool read_rt_ao_image(Backend& state, const std::array<uint32_t, 4>& rect, RtAoImage which, RtAoTexels* out);

// --- one-bounce indirect light (global illumination; [Graphics] rt_gi - gpu_vulkan.h's RtGiSettings) ----------------
// A listener after the shadows and the occlusion (rt::ViewOrder::kIndirect), the same passes over the same images with
// the denoiser above over a colour instead of a share:
//   1. trace: the pixel's position and normal as above; its albedo - a ray from the camera finds the surface the pixel
//      shows (kept where it ends at the depth's point, as the reflections do) and its material (R3) is read at the hit's
//      texture coordinates (R2's geometry table) at the level its footprint asks for, x the material's colour (the vertex
//      colours, the level's baked light, left out; at most 1) - and `rays` cosine-weighted rays (masks 0x01 + 0x02 +
//      0x04, alpha tested along the way) no longer than `distance`: a hit brings its own albedo (the same way, at a level
//      for the ray's spread) x the sun's light x N.L where a ray toward the sun - masks 0x01 + 0x02 - is clear; a miss
//      brings nothing. Their mean is the sun's light bounced once (E). The sun: the console's `rt sun`, else the level's
//      baked sun (engine/gfx/rt_sun.h; the frame record's directional lights are the skater's), white; a level whose
//      bake shows none (indoors) gets no bounce. THUG2's baked vertex colours already hold the sky's and the ambient
//      light - the bounce is what they lack - so the hits get no ambient light and the sky brings none (the shader
//      keeps both terms, and step 4 a sky to subtract, all 0 for now);
//   2. / 3. the temporal and spatial filters on E's colour (the noise floors scaled by the most one ray can bring);
//   4. apply: added = strength x albedo x max(E - sky, 0) (sky 0: E) - an open floor gets nothing, a floor beside a
//      sunlit wall gets the wall's colour - and the blend adds it scaled by (1 - colour): a screen, so a lit pixel near
//      white gains little and none goes past white, a dark one gains most. On a float colour target (an HDR target) it
//      is added as it is. Pixels whose surface wasn't found (albedo 0) are left alone.
// rt_gi_denoise=0 applies step 1's light as it is.

// The usage the indirect light needs of a depth surface is rt_effects_depth_usage's (it counts both effects).

// The passes over the device's bound targets for a viewport (the listener's work, also called by the check program).
// The caller holds the lock. Returns whether they were recorded (false: skipped - off, no scene, no material table, the
// targets can't be read; the reason logged once).
bool record_rt_gi_pass(Backend& state, const rt::ViewDrawn& view);

// The indirect light passes recorded (for the check and the log).
[[nodiscard]] uint64_t rt_gi_passes();

// Every viewport's indirect light history forgotten (the caller holds the lock).
void rt_gi_reset_history(Backend& state);

// For the check: one of the indirect light's images of the viewport last drawn with `rect` read back.
enum class RtGiImage : uint8_t {
    kRaw,     // step 1: rgb E (the light arriving)
    kHistory, // step 2: rgb E, a the history's length
    kFinal,   // what step 4 applied: rgb E, a the history's length
    kAlbedo,  // step 1: rgb the surface's albedo, a 1 where it was found
};
bool read_rt_gi_image(Backend& state, const std::array<uint32_t, 4>& rect, RtGiImage which, RtAoTexels* out);

// The passes' objects and the histories released - both effects' (the device's shutdown; the caller holds the lock).
void release_rt_effects(Backend& state);

} // namespace render::vulkan
