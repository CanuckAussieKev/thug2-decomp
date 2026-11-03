#pragma once

#include <cstdint>

#include "editor/level_editor.h"
#include "engine/maths/vec2.h"

namespace frontend::level_editor::ui {
struct ShapeMesh;
}

// NOT ORIGINAL - the level editor's model viewer (ours; see level_editor.h): the Add palette's (B) highlighted piece or
// object drawn off-screen into a small square picture that the palette shows - framed to fit its bounds, turning
// slowly; a drag in the picture orbits it, the wheel zooms.
//
// The drawing is the game's own (no meshes of ours):
//   - a scene sector (scenery): its geometry's meshes (CGeom +0x3C / +0x40, world coordinates) each drawn as the
//     world pass draws them (render::draw_scene 0x004DC700: shown 0x40 and not hidden 0x210; the opaque ones first, then
//     the transparent - material +0x20 bit 0x40 -, the material set 0x004D1F00, the mesh drawn 0x004D5290);
//   - an object: each geometry of its model drawn through its instance (CGeom +0x4C) by the engine's single-instance
//     render (0x004D16B0 with flag 8, no culling: its world matrix and bones, its lights - as the game draws
//     it), or its meshes as above when it has none.
// The camera is the renderer's own (render::choose_camera 0x004DB200) at the orbit's eye; afterwards the camera's globals
// (the matrices and the camera 0x00786B90-0x00786CF4, the view's shape 0x00786E18, the culling matrix 0x007CA968 /
// 0x007CA980) are put back and the frustum (0x00775B80) recomputed from them; the target, depth surface, viewport and
// the device's transforms are the backend's to switch and put back (ui_backend::begin_offscreen / end_offscreen - the
// renderer-independence rules: level_editor_ui_backend.h). The game's frame is drawn by then (level_editor::Blit runs
// after the 2D pass), so only what the next frame reads before it sets its camera again matters.
namespace frontend::level_editor::preview {

constexpr float kDefaultYaw = 0.6F; // the preview's starting view, radians
constexpr float kDefaultPitch = 0.42F;

// --- the pieces (pure) ------------------------------------------------------------------------------------------

// How the picture looks at its subject: turned `yaw` about the up axis (radians; 0 looks along +z, as the editor's
// yaw), from `pitch` above (radians, the eye's height angle: positive looks down), `zoom` times closer than framed.
struct Orbit {
    float yaw = kDefaultYaw;
    float pitch = kDefaultPitch;
    float zoom = 1.0F;
};
constexpr float kMinPitch = -1.2F, kMaxPitch = 1.4F;
constexpr float kMinZoom = 0.35F, kMaxZoom = 4.0F;
// A drag in the picture (pixels; right turns the subject right - the eye left -, down raises the eye) - pitch kept
// within kMinPitch..kMaxPitch - and wheel steps (up zooms in, 1.2 times a step; within kMinZoom..kMaxZoom).
void get_drag(Orbit* orbit, float dx, float dy, float radians_per_pixel);
void get_wheel(Orbit* orbit, float steps);
// What a preview is fitted to: the radius, the half angle and the margin.
struct FitAt {
    float radius{};
    float half_angle_{};
    float margin{};
};

// The distance from which a sphere of `radius` just fits a view of half-angle `half_angle` (radians: the narrower of
// the two), with `margin` (1.1: a tenth spare) - radius * margin / sin(half_angle).
float fit_distance(FitAt fit_at);
// The bounding sphere of an axis-aligned box: its centre and half its diagonal (at least `min_radius`).
void sphere_of(V3 min, V3 max, float min_radius, V3* centre, float* radius);
// The eye and its frame for an orbit about `centre` at `distance` (fit_distance / zoom).
struct Eye {
    V3 pos;
    TickCycle frame;
};
Eye orbit_eye(V3 centre, float distance, const Orbit& orbit);
// The picture's lens: its horizontal field of view (degrees) and the aspect render::choose_camera is given for a square
// picture (its height is the width / (aspect * 0.90909094): equal at 1.1); a tenth spare around the subject.
constexpr float kAngle = 40.0F;
constexpr float kSquareAspect = 1.1F;
constexpr float kMargin = 1.1F;
// The automatic turn: radians per second, and how long after the last drag it waits before turning again.
constexpr float kTurnSpeed = 0.5F;
constexpr double kTurnWait = 2.5;

// --- the viewer -------------------------------------------------------------------------------------------------

// What's shown: a scene sector (its checksum) or an object (its id).
enum class Kind : uint8_t { kNone, kSector, kObject };

// The subject for this frame: a new one starts its orbit again (zoom 1, the default angle).
void get_show(Kind kind, uint32_t id);
// The panel's drag / wheel over the picture (Request kPreviewOrbit).
void intake(maths::Vec2 delta, float wheel);
// The subject drawn into the picture (`size` pixels square): its texture id for ImGui (0: nothing drawn - none, it
// couldn't be found, or the backend has no target). Call once a frame, before Ui::TickCycle, only while the palette is
// open with something highlighted.
uint64_t render(int size);
// A subject drawn from orbit into the device's target as it is (the camera's globals saved and put back - the
// target, its viewport and clearing are the caller's: Render's picture, the Add palette's thumbnails -
// level_editor_thumbs.h). False: it's gone (nothing drawn).
bool draw_view(Kind kind, uint32_t id, const Orbit& orbit);
// The palette closed: nothing more drawn; the picture's target freed (ui_backend::free_offscreen).
void conceal();
// The frame's preview for the UI's model (level_editor::Blit, before Ui::TickCycle): while the browser is open with an entry
// to show (model->preview.index), that entry shown and drawn - its picture, name, size and type filled in; else Hide.
void tick_cycle(ui::ShapeMesh* model);

} // namespace frontend::level_editor::preview
