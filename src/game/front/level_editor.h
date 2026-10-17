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
// tools/menu_level/make_menu_level.py: the item and the scripts level_editor_enter / level_editor_exit). Entering it
// takes the pause menu down, takes the skater out of the world (as the goal editor does) and keeps the game paused;
// the script command LevelEditor_Start then hands the view to a free camera:
//   - Holding the left button on something selects it and drags it: on the horizontal plane at its base (with the
//     camera about level with that, across the view at its height instead); Shift: up / down. The camera holds still
//     while dragging and turns to the selection when it's let go; it moves only once the cursor has gone a few pixels,
//     and Escape during a drag puts it back. Seen from about level, the mouse moves it in the view's terms (up / down:
//     away / back); the wheel pushes / pulls it while dragging; Ctrl snaps to a 25-unit grid. Ctrl+Z / Ctrl+Y undo /
//     redo moves, turns, drops, resets and hiding (64 deep, this session); G drops the selection onto the ground below
//     it; Home puts it back where the level had it; Delete hides scenery; a marker (a line down to the ground and a
//     cross) shows where the ground is.
//   - W / A / S / D fly forward / left / back / right along the view, Q / E (or Space) down / up, Shift 4x
//     faster; no collision. Holding the right mouse button looks around (the cursor hidden meanwhile).
//   - The object under the cursor (a composite object with a model: level objects, peds, cars - not the skater) is
//     tinted warmer (its geometry's own colour, put back after - Scenery::Tint; only a model the engine can't colour
//     is outlined by its bounding box, yellow). A left click selects it (a cyan pulse) and dragging moves it on the
//     horizontal plane at its height; with Shift held, up and down only. The mouse wheel turns the selected object
//     about the up axis (15 degrees a notch) while it's dragged or under the cursor; otherwise it changes the flying
//     speed. A click on nothing deselects.
//   - Escape, Enter (the Pause key) or a controller's Start leave it (level_editor_exit: LevelEditor_Stop, the skater
//     back, the pause menu again).
//   - Scenery (the level's static scene sectors - LevelGeometry such as the fire truck in Los Angeles) is picked
//     too: the game's collision ray finds the sector under the cursor (sectors without collision by their boxes).
//     A click selects the sector with the rest of its trick-object cluster (the node's TrickObject Cluster=, e.g.
//     the fire truck's cab, body and wheels) and the pieces built onto them (the truck's ladder, in other clusters);
//     hovering tints what a click would take. Alt+click (or a middle click) adds or removes a single sector. Dragging,
//     the keys and Z / X (quarter turns for scenery) move the selection: the sectors' vertices and collision are
//     moved in place as the park editor moves its pieces, and its grind rails with them (level_editor_scenery.cpp;
//     the rails are drawn as lines while selected).
// Moves are not saved anywhere (nothing persists past the level).
//
// thug2-decomp.ini [features] level_editor=1 (default; 0: LevelEditor_Available is false - no menu item - and
// LevelEditor_Start refuses), debug_editor=1 logs picks, moves and the view.
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
