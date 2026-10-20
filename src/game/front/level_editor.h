#pragma once

#include <cstdint>

namespace Gfx {
class Camera;
}
namespace Obj {
class CCompositeObject;
}
namespace Script {
class CStruct;
class CScript;
} // namespace Script

// NOT ORIGINAL - the level editor (ours, added at the user's request; not the game's and not PartyMod's).
//
// The pause menu in a level gets LEVEL EDITOR (the data overlay's scripts\game\menu\gamemenu_pause.qb, made by
// tools/menu_level/make_menu_level.ts: the item and the scripts level_editor_enter / level_editor_exit). Entering it
// takes the pause menu down, takes the skater out of the world (as the goal editor does) and keeps the game paused;
// the script command LevelEditor_Start then hands the view to a free camera. The modules:
//   level_editor.cpp          the camera, the mouse and keys, selecting, dragging, the move arrows, undo / redo,
//                             duplicating, the browser, drawing the lines (rails, the ground marker, the arrows)
//   level_editor_objects.cpp  composite objects with a model (level objects, peds, cars - not the skater): picking by
//                             their model's box, moving and turning them, copies made from their nodes
//   level_editor_scenery.cpp  the level's static scene sectors (LevelGeometry such as LA's fire truck): picking by
//                             the collision ray, groups (a trick-object cluster and the pieces built onto it), moving
//                             vertices and collision in place as the park editor does, grind rails with them,
//                             copies (CScene::CloneSector), hiding, tinting
//   level_editor_layout.cpp   what was changed, saved per level (thug2-decomp-layouts\<scene>.txt, Ctrl+S) and put
//                             back whenever that level loads (not in network games)
//   level_editor_ui.cpp       the UI (Dear ImGui, level_editor_ui.h): a toolbar, the Outliner, the Inspector, a status
//                             bar, the help (F1), the browser (B) and the autosave offer; the lines and gizmos drawn
//                             through its draw list. level_editor_ui_backend.cpp is the only file tied to SDL2 /
//                             Direct3D 9 (ImGui's backends); level_editor_ui_layout.cpp the layout and input routing
//                             (pure, self-tested); level_editor_ui_fonts.cpp Inter and JetBrains Mono in the DLL
//   level_editor_hud.cpp      the old HUD ([features] editor_ui=0, or when the UI can't start): a status panel top
//                             left, the keys at the bottom, a message line, and a panel for the help (F1) and the
//                             browser (B), made of the game's screen elements
// The controls (F1 lists them in the game):
//   - Flying: W A S D, Q / E down / up, Shift faster, the right mouse button looks, the wheel goes forward / back.
//   - Selecting: a click on an object or scenery (scenery takes its group; hovering tints what a click would take);
//     Shift+click adds / removes; Alt+click or a middle click one piece of scenery; Backspace deselects.
//   - Moving: hold and drag (Shift: up / down; the wheel pushes / pulls; Ctrl snaps to 25; Escape puts it back), or
//     drag one of the arrows to move along that axis only; W A S D / Q E nudge; Z / X turn (objects 15 degrees,
//     scenery a quarter); G drops it onto the ground (a marker shows where that is).
//   - Editing: Ctrl+D duplicates; Delete hides scenery (deletes copies of objects); Home puts it back where the level
//     had it; Ctrl+Z / Ctrl+Y undo / redo (64 steps, this session).
//   - Adding: B lists the level's scenery and objects; Enter places a copy where the cursor points.
//   - Saving: Ctrl+S. Unsaved changes are autosaved (every minute, on leaving, and from the crash logger -
//     level_editor_layout.h); an autosave newer than the saved layout is offered on entering: Ctrl+R restores it,
//     Ctrl+Shift+R discards it. A crash logs the editor's last action. Leaving: Escape, Enter (the Pause key) or a controller's Start (level_editor_exit) - Escape
//     first cancels a drag, closes a panel or drops the selection.
//
// thug2-decomp.ini [features] level_editor=1 (default; 0: LevelEditor_Available is false - no menu item - and
// LevelEditor_Start refuses), debug_editor=1 logs picks, moves and the view. `-edit <level>` (or edit_level=) starts the
// game in the editor in that level (game/front/start_level.h; tools/game/edit_level.ts). editor_ui=0 (default 1) brings
// back the old HUD in place of the Dear ImGui panels.
namespace Front::LevelEditor {

// The setting (read once; self-tests may set it).
bool& Enabled();
bool& DebugEnabled();
// The editor is running (between LevelEditor_Start and LevelEditor_Stop).
bool Active();

// Script commands (CFunctions, registered by RegisterScriptFunctions).
bool __cdecl CFunc_Available(Script::CStruct* params, Script::CScript* script); // LevelEditor_Available
bool __cdecl CFunc_Start(Script::CStruct* params, Script::CScript* script);     // LevelEditor_Start
bool __cdecl CFunc_Stop(Script::CStruct* params, Script::CScript* script);      // LevelEditor_Stop
// Our extra script commands, registered after the game's own table (Sys::Main, after 0x005A5E70).
void RegisterScriptFunctions();

// Start listening to SDL's mouse and key events (Sio::InitSdlInput, next to MouseMenus::Init).
void Init();
// Once a frame from the front end's input logic (MouseMenus::Update's stand-down while Active): a requested exit
// (Escape) runs level_editor_exit here.
void InputLogic();
// Once a frame at the start of the render (Nx::RenderWorld): flying, looking, picking, dragging.
void Update();
// Called by the renderer for a viewport's camera before it's read: while Active, the first viewport's camera gets the
// editor's pose (the game's pose noted first, put back by Stop).
void PlaceCamera(Gfx::Camera* camera);
// Called by the renderer once a viewport is set up: what the view's projection is (for the mouse ray and the
// outlines). `viewport`: the device viewport (x, y, width, height in back buffer pixels).
void NoteView(Gfx::Camera* camera, float angle, float aspect, const uint32_t viewport[4]);
// After the 2D pass: the selected scenery's grind rails, and outlines of what couldn't be tinted.
void Draw();
// For the keyboard (Sio): whether the Pause key, `down` now, counts - only once it has been up since the editor
// started (the Enter that chose the menu item doesn't leave at once).
bool PassPauseKey(bool down);
// That gate: a key down counts only once it has been seen up (`released`).
bool PassKey(bool* released, bool down);

// --- the pieces (pure, self-tested) -----------------------------------------------------------------------------

struct V3 {
    float x, y, z;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Normalize(V3 v);

// A camera's frame as the game keeps it (Gfx::Camera m_matrix rows): right, up, back (the view looks along -back).
struct Frame {
    V3 right, up, back;
};
// The frame looking along yaw (about the world's up axis, 0 = along +z, positive turning towards +x) and pitch
// (up positive, radians): forward = (sin yaw cos pitch, sin pitch, cos yaw cos pitch); right = up(world) x back;
// up = back x right.
Frame FrameFromYawPitch(float yaw, float pitch);
// The yaw and pitch of a frame's view direction (-back).
void YawPitchFromBack(V3 back, float* yaw, float* pitch);

// The fly keys this frame: each -1, 0 or 1 (forward/back, right/left, up/down).
struct FlyInput {
    int forward, right, up;
};
// The camera moved: forward along the view, right along the frame's right, up along the world's up; `distance` units.
V3 Fly(V3 pos, const Frame& frame, const FlyInput& input, float distance);
// Mouse look: the yaw / pitch turned by a mouse movement (pixels; right turns right, up looks up), pitch kept within
// +-89 degrees.
void Look(float* yaw, float* pitch, float dx, float dy, float radiansPerPixel);

// The projection as the renderer sets it (Nx::SetCamera 0x004DB200: D3DX's look-at and perspective, the view's width at
// the near plane 4 tan(angle / 2) and its height that / (aspect * 0.90909094)): tangents of the half-angles.
struct Lens {
    float tanX, tanY;
};
Lens LensFor(float angleDegrees, float aspect);
// A back buffer pixel within the viewport (x, y, width, height) to normalised device coordinates (x right, y up,
// -1..1); false outside a zero-sized viewport.
bool PixelToNdc(const uint32_t viewport[4], float px, float py, float* nx, float* ny);
void NdcToPixel(const uint32_t viewport[4], float nx, float ny, float* px, float* py);
// The ray through normalised device coordinates from the camera (direction not normalised: depth 1 along the view).
V3 RayDirection(const Frame& frame, const Lens& lens, float nx, float ny);
// A world point to normalised device coordinates; false when it isn't in front of the near plane (`nearZ`).
bool Project(V3 cameraPos, const Frame& frame, const Lens& lens, V3 point, float nearZ, float* nx, float* ny);

// Ray against a sphere / an oriented box (a model's box: `min` / `max` in the model's space, placed by the rows of
// `axes` - right, up, at - and `origin`; rows needn't be unit). The nearest hit's distance along `dir` (in units of
// dir), false if none in front.
bool RaySphere(V3 origin, V3 dir, V3 centre, float radius, float* t);
bool RayBox(V3 origin, V3 dir, V3 boxOrigin, const V3 axes[3], V3 min, V3 max, float* t);
// Ray against the plane through `point` with `normal`: false if parallel or behind.
bool RayPlane(V3 origin, V3 dir, V3 point, V3 normal, float* t);
// A box's eight corners in world space (bit 0 of the index: max x, bit 1: max y, bit 2: max z).
void BoxCorners(V3 boxOrigin, const V3 axes[3], V3 min, V3 max, V3 out[8]);
// Whether a model's bounding box looks like world coordinates rather than the model's own: it holds the object's
// position but not the model's origin (only told apart when the object is away from the world's origin).
bool BoxLooksWorldSpace(V3 min, V3 max, V3 objectPos);
// A direction turned about the world's up axis by `angle` radians (positive: +z towards +x, as yaw).
V3 TurnAboutUp(V3 v, float angle);
// A line from a to b (camera space depth = distance along the view) clipped to depth >= nearZ: false if all behind.
bool ClipToNear(V3 cameraPos, const Frame& frame, float nearZ, V3* a, V3* b);

// A box placed in the world: p = origin + local.x * axes[0] + local.y * axes[1] + local.z * axes[2], local within
// min..max (an object's model box, or a scene sector's box where it is now).
struct OrientedBox {
    V3 origin;
    V3 axes[3];
    V3 min, max;
};

// --- scenery (the static scene sectors; level_editor_scenery.cpp) ------------------------------------------------

// A direction turned by `quarters` quarter turns about the world's up axis, exactly (no trigonometry): as
// TurnAboutUp(v, quarters * 90 degrees), and as the engine turns a sector's vertices (sMesh::RotateY 0x004D6FB0:
// rotation 1 takes (dx, dz) from the pivot to (dz, -dx), rotation 3 to (-dz, dx)).
V3 QuarterTurn(V3 v, int quarters);
// Where a moved sector is, relative to where it was when the editor first touched it: a point x of it then is now at
// QuarterTurn(x, quarters) + offset.
struct Placement {
    int quarters; // 0..3
    V3 offset;
};
V3 Apply(const Placement& placement, V3 x);
// Moved by `delta`; turned by `quarters` quarter turns about `pivot` (a world point, where it is now).
void MoveBy(Placement* placement, V3 delta);
void TurnAbout(Placement* placement, V3 pivot, int quarters);
// What the engine keeps for a placement: the sector's quarter turns (CSector +0x10, set by SetYRotation 0x0049E760)
// and its geometry's world position (the meshes' pivot, sMesh +0x90's translation), from those when first touched
// (`quarters0`, `position0`). Its vertices are at QuarterTurn(v, quarters) + position for v as loaded.
void EngineState(int quarters0, V3 position0, const Placement& placement, int* quarters, V3* position);
// `outer` after `inner`: Apply(Compose(outer, inner), x) == Apply(outer, Apply(inner, x)) - a copy made of a moved
// sector and moved since is placed by its own placement after its source's (relative to the source as loaded).
Placement Compose(const Placement& outer, const Placement& inner);
// The box (min / max as when first touched) where the placement has put it.
OrientedBox PlacedBox(V3 min, V3 max, const Placement& placement);
// Whether a sector of that box can be moved: not huge (the ground, a whole street: an extent over `limit`) and a
// real box.
bool SectorSizeMovable(V3 min, V3 max, float limit);
// The box around two boxes' (axis-aligned) corners: `have` false takes `min` / `max` as they come.
void GrowBox(V3* min, V3* max, bool have, V3 boxMin, V3 boxMax);
// The axis-aligned box around an oriented box's corners.
void BoundsOf(const OrientedBox& box, V3* min, V3* max);

// The point of the line through `p` along `u` nearest to the ray from `o` along `d` (the move arrows: dragging along
// one axis): its parameter `s` (p + u * s). False when the two are (nearly) parallel.
bool ClosestOnAxis(V3 p, V3 u, V3 o, V3 d, float* s);
// The distance in pixels from (x, y) to the segment (ax, ay)-(bx, by).
float DistanceToSegment(float x, float y, float ax, float ay, float bx, float by);

// A selection grows by the pieces built onto it but not put in its cluster (LA's fire truck: its ladder): a piece
// (axis-aligned bounds `min`..`max`) joins when it is no bigger than the group was (`groupMin`..`groupMax`: no larger
// volume, no wider across than 1.25 times the group's longer side - not the street it stands on or the building beside
// it) and it touches one of the group's boxes (`boxMin`..`boxMax`, within `margin`) above that box's bottom (by more
// than 5 units: not the ground under it).
bool PieceFits(V3 min, V3 max, V3 groupMin, V3 groupMax);
bool PieceTouches(V3 min, V3 max, V3 boxMin, V3 boxMax, float margin);
// A grind rail's segment box, as the rail manager makes it (0x0057C260): the two points' bounds.
void SegmentBox(V3 a, V3 b, V3* min, V3* max);
// The highlight's colour at `seconds`: an RGBA (the engine's per-mesh colour, 0x80 a channel unchanged) between `low`
// and `high`, a pulse a second and a quarter (0 at low).
uint32_t Pulse(uint32_t low, uint32_t high, double seconds);

} // namespace Front::LevelEditor
