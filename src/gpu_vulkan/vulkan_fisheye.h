#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/vulkan_style_film.h"

// NOT ORIGINAL - the fisheye lens ([Graphics] rt_fisheye, rt_fisheye_strength, rt_fisheye_vignette, rt_fisheye_circle -
// gpu_vulkan.h's CameraFxSettings; docs/design/vulkan.md "Fisheye lens"): a 90s skate video's fisheye adapter on a
// camcorder - strong barrel distortion over a wider field of view, the adapter's dark corners (or a round frame), colour
// fringes and softness toward the edges. A camera option of its own, under any art style and any grade.
//
// Not a warp of the normal picture (that would magnify the middle and lose the edges): render_pc widens the game camera's
// field of view for the lens (rt_scene.h ViewLens, engine/gfx/fisheye_lens.h - the culling follows), and this pass maps
// the wider picture through the lens. The camera effects' listener at rt::ViewOrder::kLens (970) runs it over each widened
// view's world: after the upscaler (950), cel shading's outlines (960) and the motion blur and depth of field (965) - at
// the output's resolution, the motion vectors and the upscaler's history the widened picture's -, before the art style
// (975: its overlays, the VHS's REC, stay straight) and the grade (985); the 2D (the HUD, menus, text) is never touched.
//
// One full-screen pass (film_kit's: the viewport's colour copied out and read filtered, clamped inside the viewport).
// Per output pixel, its place from the viewport's centre in the original view's focal units n (x / fx, y / fy - fx, fy
// the original view's focal lengths in pixels: half the viewport / the tangents of its half angles):
//   1. the lens: r = |n|, the angle angle_of(r, s) (fisheye_lens.h), the widened picture's pixel at the centre plus
//      n x tan(angle) / r x (fx, fy) / widen;
//   2. the colour fringes: red read kFringe x s x e^2 further out along that line, blue as much nearer (e = r / reach:
//      1 at the frame's edge), green at the place;
//   3. the softness: each channel the mean of three taps along the line, kSoftShare x the height x s x e^2 apart
//      (weights 1/4, 1/2, 1/4);
//   4. the adapter's vignette: x (1 - rt_fisheye_vignette x kVignetteDepth x smoothstep(kVignetteFrom, 1, e)); with a
//      round frame (or the lens' own reach) inside the corners, black past it over a soft edge kEdgeShare x the height.
// FilmConstants: info x - flags (1: framed); a - the tangents x, y, widen, s; b - the reach (focal units), the vignette,
// the fringe (kFringe x s), the softness (pixels); c - x the frame's soft edge (pixels).
namespace render::vulkan {

struct Backend;

inline constexpr float kFisheyeFringe = 0.006F;        // the colour fringes' share of the radius at the edge, strength 1
inline constexpr float kFisheyeSoftShare = 1.5F / 720.0F; // the softness' tap spacing at the edge (of the height)
inline constexpr float kFisheyeVignetteDepth = 0.92F;
inline constexpr float kFisheyeVignetteFrom = 0.35F;
inline constexpr float kFisheyeEdgeShare = 0.006F;      // the round frame's soft edge (of the height)

// The pass's constants for a view's lens over the viewport `shown` (x, y, width, height).
[[nodiscard]] FilmConstants fisheye_constants(const rt::ViewDrawn::Lens& lens, float vignette, const std::array<uint32_t, 4>& shown);
// The pass on the processor (the check's reference): the viewport's pixels (RGB, row by row) as the pass writes them.
[[nodiscard]] std::vector<FilmRgb> fisheye_reference(const FilmConstants& constants, const std::vector<FilmRgb>& colour);

// The pass over the target (the caller holds the lock: the camera effects' listener). False: nothing recorded (the
// pass can't be made - logged once).
bool record_fisheye(Backend& state, const FilmConstants& constants, const FilmTarget& target);
void release_fisheye(Backend& state);

} // namespace render::vulkan
