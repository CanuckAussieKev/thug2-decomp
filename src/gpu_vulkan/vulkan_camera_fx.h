#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/gfx/rt_scene.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - the camera effects (docs/design/vulkan.md "Camera effects"; [Graphics] rt_motion_blur, rt_dof, rt_grade
// and their knobs - gpu_vulkan.h's CameraFxSettings): motion blur, depth of field and a colour grade, post-processes over
// each viewport's world, before the 2D (the HUD, the menus and text are never touched). Independent of ray tracing, HDR,
// the upscaler and the art style. Everything off (the default): no listener, no motion vectors, nothing drawn.
//
// Where they run (rt::ViewOrder): the motion blur and the depth of field at kCameraEffects (965) - after the tone mapping
// (900: the colour is the display's 0 - 1), the upscaler (950) and cel shading's outlines (960), at the output's
// resolution (a lens's blur is the picture's, seen at its final size; the upscaler's history isn't smeared), before the
// art style (975: a style's own overlays - the VHS's REC, the retro's cells - stay sharp); the grade at kGrade (985),
// last, over whatever the picture became. The depth and the motion vectors are the drawn picture's (ViewDrawn::rect, the
// upscaler's lower resolution): each output pixel reads the drawn pixel under it.
//
// 1. The preparation (compute, at the output's size, the viewport's rectangle): per output pixel the drawn pixel under it
//    - its linear depth z (the view's distance; the far plane where nothing was drawn, at most kCameraFarthest), and with
//    the motion blur its motion: the motion vectors' (vulkan_motion.h: the move to where the point was the frame before,
//    0 - 1 of the viewport) or, where no draw wrote one (the sky, a cleared pixel) or none were written (the view's
//    attachment never opened), the camera's motion of the point at its depth. The motion in output pixels times
//    shutter / 2 - the half streak's vector v -, its length at most the tile's side K. Into an RGBA16F image: v, z.
// 2. Motion blur (Morgan McGuire's reconstruction filter, "A Reconstruction Filter for Plausible Motion Blur", I3D 2012,
//    with its 2013 refinements - the per-pixel jitter, the centre's own weight):
//    a. (compute) the tiles: per K x K tile the longest v (K = the output's height / 36, 8 - 64 pixels);
//    b. (compute) the neighbourhood: per tile the longest of its 3 x 3 tiles' (a streak reaches past its own tile);
//    c. (full screen) per output pixel X, with the neighbourhood's vector n (|n| < half a pixel: X as it is): the colour
//       at X and kBlurTaps others along n, at t in -1 - 1 jittered per pixel (a hash), each weighted by
//       f x cone(d, |v_Y|) + b x cone(d, |v_X|) + 2 x cylinder(d, |v_Y|) x cylinder(d, |v_X|) - d = |t n|, f how much Y is
//       in front of X, b the other way round (soft over kBlurSoftDepth of the nearer one's distance), cone(d, l) =
//       clamp(1 - d / l), cylinder(d, l) = 1 - smoothstep(0.95 l, 1.05 l, d) -; X's own at 1 / |v_X|. Their mean written.
//    The shutter is rt_motion_blur_strength, the share of the frame's time the shutter is open (0.5: 180 degrees). The
//    motion vectors span the time since the frame before - with [Graphics] max_fps a render-only frame's own step, not a
//    tick -: a faster frame rate blurs less, as a faster camera would. Nothing is blurred in a frame after a cut
//    (frame_blend::cuts: a teleport, a respawn), after the camera jumped (moved more than kCutDistance or turned more than
//    kCutTurn since the frame before - a cut nothing reported, a level loaded), while the game's own screen blur is on
//    (ViewDrawn::game_blur: SetScreenBlur with DisableBlur=0 - the original's effect stays as it was) or under a menu.
// 3. Depth of field (a scatter-as-gather bokeh at half resolution, near and far fields apart), focused on what the
//    camera follows (ViewDrawn::focus_distance: the skater camera's look-at point - none known: no depth of field), off
//    under a menu and in the level editor (ViewDrawn::menu). The circle of confusion of a depth z, in output pixels
//    (positive behind the focus, negative before it):
//        c(z) = clamp(A x (1 - focus / z), -A, A),  A = rt_dof_strength x kDofAperture x the output's height
//    a. (compute, half resolution) per 2 x 2 block: its colour's mean, its circle - the nearest (most negative) one
//       when any is before the focus, else the far ones' mean -, halved (half-resolution pixels);
//    b. (compute) the near field's reach: per kDofTile x kDofTile half-resolution tile, the largest near circle over its
//       3 x 3 tiles (a blurred foreground spills over what is behind it);
//    c. (compute, half resolution) per pixel, a disc of radius R = max(its own circle, the near reach) gathered with
//       kDofTaps points on a Vogel spiral: a far sample counts where its own circle reaches the pixel (its weight
//       clamp(c_s - |o| + 1)), the pixel's own far colour among them; a near sample likewise with its near circle. Out:
//       the far field's mean and the near field's mean and coverage (how much of the disc the near samples cover);
//    d. (full screen) per output pixel: its own colour where its circle is under half a pixel, the far field mixed in by
//       smoothstep(0.5, 2, c) behind the focus, then the near field over it by its coverage.
//    At the focus the picture is left exactly as it was.
// 4. The grade (one full-screen pass): the look's table - kGradeTableSize^3 entries over the colour cube, made on the
//    processor from the look's formulas (vulkan_grade_looks.cpp) - read trilinearly, mixed over the colour by
//    rt_grade_strength; the vignette (rt_grade_vignette: the corners darkened by up to kGradeVignetteDepth, the
//    picture's aspect kept) and the grain (rt_grade_grain: a monochrome hash per cell of height / 720 pixels, a new one
//    each frame, strongest in the mid-tones, kGradeGrainAmount at 1). An art style with its own vignette or grain (noir,
//    thermal; comic, sketch: their grain) keeps it: the grade's stands aside. Clamped to 0 - 1; alpha kept.
//    rt_grade=level takes the look rt_grade_levels lists for the level ("<scene>:<look>,...", the scene's name as
//    LoadScene names it); a level it doesn't list: neutral.
// Multisampled targets: the grade runs (the colour resolved, written to every sample); the blur and the depth of field
// don't (logged once). A split view: each viewport its own (the focus is the one the camera noted last).
namespace render::vulkan {

struct Backend;

inline constexpr float kCameraFarthest = 60000.0F;   // the linear depth's ceiling (a 16-bit float holds it)
inline constexpr float kBlurTileShare = 1.0F / 36.0F; // K: the output's height x this
inline constexpr int kBlurLeastTile = 8;
inline constexpr int kBlurMostTile = 64;
inline constexpr int kBlurTaps = 16;
inline constexpr float kBlurSoftDepth = 0.05F;        // (of the nearer one's distance)
inline constexpr float kCutDistance = 300.0F;         // world units between two frames' cameras: a cut
inline constexpr float kCutTurnCosine = 0.866F;       // cos 30 degrees: a cut
inline constexpr float kDofAperture = 0.03F;          // A at strength 1: of the output's height
inline constexpr int kDofTile = 8;                    // (half-resolution pixels)
inline constexpr int kDofTaps = 24;
inline constexpr uint32_t kGradeTableSize = 33;
inline constexpr float kGradeVignetteDepth = 0.55F;
inline constexpr float kGradeGrainAmount = 0.08F;

using CameraRgb = std::array<float, 3>;

// --- the grade's looks (vulkan_grade_looks.cpp) -----------------------------------------------------------------------

// A look's colour for a colour (both the display's 0 - 1): the formula its table is made from. neutral, off, level: as
// given.
[[nodiscard]] CameraRgb grade_look_colour(GradeLook look, CameraRgb colour);
// The look's table: size^3 entries, red the fastest (as a .cube's).
[[nodiscard]] std::vector<CameraRgb> grade_table(GradeLook look, uint32_t size = kGradeTableSize);
// The look rt_grade=level gives a level (its main scene's checksum) from rt_grade_levels' text; neutral when it names
// none (a name it can't read is logged once by set_camera_fx_settings).
struct GradeLevel {
    uint32_t scene = 0;
    GradeLook look = GradeLook::kNeutral;
};
// "<scene>:<look>,..." read (spaces allowed; a pair that isn't one: false, the rest still read).
bool parse_grade_levels(const std::string& text, std::vector<GradeLevel>* levels);

// What the grade pass is given (also the twin's input).
struct GradeParameters {
    GradeLook look = GradeLook::kOff; // the look drawn (level resolved)
    float strength = 1.0F;
    float vignette = 0.0F;
    float grain = 0.0F;
    float cell = 1.0F;     // the grain's cell (output pixels)
    uint32_t seed = 0;     // the frame's (0 - 65535)
};
// The pass on the processor (the check's reference): the viewport's pixels (`size`, row by row, the display's 0 - 1)
// as the pass writes them.
[[nodiscard]] std::vector<CameraRgb> grade_reference(const GradeParameters& parameters, const std::vector<CameraRgb>& colour,
                                                     Extent size);
// The table read trilinearly (the shader's arithmetic).
[[nodiscard]] CameraRgb grade_table_colour(const std::vector<CameraRgb>& table, uint32_t size, CameraRgb colour);

// --- the passes ------------------------------------------------------------------------------------------------------

// The passes for a view (the listeners' work; also the check's). The caller holds the lock. False: nothing recorded.
bool record_camera_pass(Backend& state, const rt::ViewDrawn& view);
bool record_grade_pass(Backend& state, const rt::ViewDrawn& view);
// The view's motion begun for the blur when no upscaler took it (the listener's view_begins; the check's).
void begin_camera_view(Backend& state, const rt::ViewDrawn& view);

// What ran (counts since start-up; the check's and the log's).
struct CameraPassCounts {
    uint64_t blurs = 0;
    uint64_t focuses = 0;  // depth of field passes
    uint64_t grades = 0;
    uint64_t cut_skips = 0; // frames the blur left out after a cut or a jump
};
[[nodiscard]] CameraPassCounts camera_pass_counts();
// The last grade's parameters (its frame's seed; the check's reference input).
[[nodiscard]] GradeParameters grade_last_parameters();
// The device time of the last measured passes (ms; negative: not measured yet) - the blur and depth of field together,
// and the grade.
struct CameraTimings {
    double camera = -1.0;
    double grade = -1.0;
};
[[nodiscard]] CameraTimings camera_timings();

// The objects released (the device's shutdown; the caller holds the lock).
void release_camera_fx(Backend& state);

} // namespace render::vulkan
