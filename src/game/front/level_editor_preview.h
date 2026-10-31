#pragma once

#include <cstdint>

#include "game/front/level_editor.h"

namespace Front::LevelEditor::Ui {
struct Model;
}

// NOT ORIGINAL - the level editor's model viewer (ours; see level_editor.h): the Add palette's (B) highlighted piece or
// object drawn off-screen into a small square picture that the palette shows - framed to fit its bounds, turning
// slowly; a drag in the picture orbits it, the wheel zooms.
//
// The drawing is the game's own (no meshes of ours):
//   - a scene sector (scenery): its geometry's meshes (CGeom +0x3C / +0x40, world coordinates) each drawn as the
//     world pass draws them (Nx::RenderScene 0x004DC700: shown 0x40 and not hidden 0x210; the opaque ones first, then
//     the transparent - material +0x20 bit 0x40 -, the material set 0x004D1F00, the mesh drawn 0x004D5290);
//   - an object: each geometry of its model drawn through its instance (CGeom +0x4C) by the engine's single-instance
//     render (0x004D16B0 with flag 8, no culling: its world matrix and bones, its lights - as the game draws
//     it), or its meshes as above when it has none.
// The camera is the renderer's own (Nx::SetCamera 0x004DB200) at the orbit's eye; afterwards the camera's globals
// (the matrices and the camera 0x00786B90-0x00786CF4, the view's shape 0x00786E18, the culling matrix 0x007CA968 /
// 0x007CA980) are put back and the frustum (0x00775B80) recomputed from them; the target, depth surface, viewport and
// the device's transforms are the backend's to switch and put back (UiBackend::BeginOffscreen / EndOffscreen - the
// renderer-independence rules: level_editor_ui_backend.h). The game's frame is drawn by then (LevelEditor::Draw runs
// after the 2D pass), so only what the next frame reads before it sets its camera again matters.
namespace Front::LevelEditor::Preview {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

// How the picture looks at its subject: turned `yaw` about the up axis (radians; 0 looks along +z, as the editor's
// yaw), from `pitch` above (radians, the eye's height angle: positive looks down), `zoom` times closer than framed.
struct Orbit {
    float yaw = 0.6f;
    float pitch = 0.42f;
    float zoom = 1.0f;
};
constexpr float kMinPitch = -1.2f, kMaxPitch = 1.4f;
constexpr float kMinZoom = 0.35f, kMaxZoom = 4.0f;
// A drag in the picture (pixels; right turns the subject right - the eye left -, down raises the eye) - pitch kept
// within kMinPitch..kMaxPitch - and wheel steps (up zooms in, 1.2 times a step; within kMinZoom..kMaxZoom).
void Drag(Orbit* orbit, float dx, float dy, float radiansPerPixel);
void Wheel(Orbit* orbit, float steps);
// The distance from which a sphere of `radius` just fits a view of half-angle `halfAngle` (radians: the narrower of
// the two), with `margin` (1.1: a tenth spare) - radius * margin / sin(halfAngle).
float FitDistance(float radius, float halfAngle, float margin);
// The bounding sphere of an axis-aligned box: its centre and half its diagonal (at least `minRadius`).
void SphereOf(V3 min, V3 max, float minRadius, V3* centre, float* radius);
// The eye and its frame for an orbit about `centre` at `distance` (FitDistance / zoom).
struct Eye {
    V3 pos;
    Frame frame;
};
Eye OrbitEye(V3 centre, float distance, const Orbit& orbit);
// The picture's lens: its horizontal field of view (degrees) and the aspect Nx::SetCamera is given for a square
// picture (its height is the width / (aspect * 0.90909094): equal at 1.1); a tenth spare around the subject.
constexpr float kAngle = 40.0f;
constexpr float kSquareAspect = 1.1f;
constexpr float kMargin = 1.1f;
// The automatic turn: radians per second, and how long after the last drag it waits before turning again.
constexpr float kTurnSpeed = 0.5f;
constexpr double kTurnWait = 2.5;

// --- the viewer -------------------------------------------------------------------------------------------------

// What's shown: a scene sector (its checksum) or an object (its id).
enum class Kind { kNone, kSector, kObject };

// The subject for this frame: a new one starts its orbit again (zoom 1, the default angle).
void Show(Kind kind, uint32_t id);
// The panel's drag / wheel over the picture (Request kPreviewOrbit).
void Input(float dx, float dy, float wheel);
// The subject drawn into the picture (`size` pixels square): its texture id for ImGui (0: nothing drawn - none, it
// couldn't be found, or the backend has no target). Call once a frame, before Ui::Frame, only while the palette is
// open with something highlighted.
uint64_t Render(int size);
// A subject drawn from orbit into the device's target as it is (the camera's globals saved and put back - the
// target, its viewport and clearing are the caller's: Render's picture, the Add palette's thumbnails -
// level_editor_thumbs.h). False: it's gone (nothing drawn).
bool DrawView(Kind kind, uint32_t id, const Orbit& orbit);
// The palette closed: nothing more drawn; the picture's target freed (UiBackend::FreeOffscreen).
void Hide();
// The frame's preview for the UI's model (LevelEditor::Draw, before Ui::Frame): while the browser is open with an entry
// to show (model->preview.index), that entry shown and drawn - its picture, name, size and type filled in; else Hide.
void Frame(Ui::Model* model);

} // namespace Front::LevelEditor::Preview
