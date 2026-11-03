#pragma once

#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <array>
#include "engine/maths/vec2.h"
#include "engine/core/checked_index.h"

struct ImFont;

namespace gfx {
class ViewCam;
}
namespace objects {
class GameEntity;
}
namespace script {
class Struct;
class Script;
} // namespace script

// NOT ORIGINAL - the level editor (ours, added at the user's request; not the game's and not PartyMod's).
//
// The pause menu in a level gets LEVEL EDITOR (the data overlay's scripts\game\menu\gamemenu_pause.qb, made by
// tools/pipeline/menu_level/make_menu_level.ts: the item and the scripts level_editor_enter / level_editor_exit). Entering it
// takes the pause menu down, takes the skater out of the world (as the goal editor does) and keeps the game paused;
// the script command LevelEditor_Start then hands the view to a free camera. The modules:
//   level_editor.cpp          the camera, the mouse and keys, selecting, dragging, the move arrows, undo / redo,
//                             duplicating, the browser, drawing the lines (rails, the ground marker, the arrows)
//   level_editor_objects.cpp  composite objects with a model (level objects, peds, cars - not the skater): picking by
//                             their model's box, moving and turning them, copies made from their nodes
//   level_editor_scenery.cpp  the level's static scene sectors (LevelGeometry such as LA's fire truck): picking by
//                             the collision ray, groups (a trick-object cluster and the pieces built onto it), moving
//                             vertices and collision in place as the park editor does, grind rails with them,
//                             copies (WorldScene::duplicate_scene_sector), hiding, tinting
//   level_editor_scale.cpp    scaling scenery and objects (0.25x - 4x on each axis, uniform or stretched): a piece's
//                             vertices, normals and collision kept when first scaled and written again from that copy
//                             (no drift); an object's model scale and its own copy of its collision; the limits, the
//                             layouts' scale=
//   level_editor_layout.cpp   what was changed, saved per level (thug2-decomp-layouts\<scene>.txt, Ctrl+S): the level's
//                             EDITED COPY, a level of its own - put in place only when the level select's "<LEVEL>
//                             (EDITED)" entry (section EDITED LEVELS) loads it or the editor is opened in the level;
//                             the level as shipped always loads as shipped, and no layout ever applies (nor does the
//                             editor start) in a network session. Reset level (Ctrl+Shift+Delete) puts the level back
//                             and deletes its edited copy's files. The design: level_editor_layout.h's top.
//   level_editor_ui.cpp       the UI (Dear ImGui, level_editor_ui.h): a toolbar, the Outliner, the Inspector, a status
//                             bar, the help (F1), the browser (B) and the autosave offer; the lines and gizmos drawn
//                             through its draw list. level_editor_ui_backend.cpp is the only file tied to SDL2 /
//                             Direct3D 9 (ImGui's backends); level_editor_ui_layout.cpp the layout and input routing
//                             (pure); level_editor_ui_fonts.cpp Inter and JetBrains Mono in the exe
//   level_editor_nodes.cpp    the node layer: every node of the level's node arrays in families (spawns, crowns,
//                             flags, rails, ladders, paths, objects, scenery, lights, sounds, particles, triggers,
//                             other) for the Outliner, the read-only Inspector and the gizmos (points, chains,
//                             volumes) the UI draws; picked by screen distance; the online badge on spawns, crowns and
//                             flags in a level shared with THUGPRO (a ported level)
//   level_editor_node_edit.cpp point nodes (spawns, crowns, flags, lights, sounds, triggers, cameras and other nodes)
//                             moved and turned: their pos= / angles= written, what the game keeps of them updated so it
//                             shows at once (or at the next respawn / crown spawn), their undo steps and `set` records
//   level_editor_node_create.cpp nodes added (spawns, team spawns, crowns, CTF flag sets, lights, sound emitters,
//                             proximity nodes) in an overlay node array registered after the level's - its indexes and
//                             crown order kept - and the game's proximity node / emitter / light made for them; a level's
//                             own nodes hidden (not removed: a marker class the walks skip); `node` / `order` / `hide`
//                             records; the limits (10 crowns, 256 restarts, 256 / 150 lights)
//   level_editor_fields.cpp   (phase 6) the full Inspector: every node field typed, editable (live / on reload), own
//                             overrides over shared structures, `set` records' f.<field>= edits written into the edited
//                             copy's node array as it loads; level_editor_fields_ui.cpp its ImGui table
//   level_editor_level_props.cpp (phase 6) the edited copy's level properties (name, start, time of day, ambient,
//                             fog): its `level` record, written into the level structure only for its load
//   level_editor_entities.cpp peds and cars copied (the Place palette's types), model swaps, uncreated objects as ghosts,
//                             bouncy sets and physics fields, moving objects' path speed and preview, sound emitters'
//                             sounds, particle systems moved / copied, `speed` / `emitter` records
//   level_editor_view.cpp     the view options: the collision shown (C; invisible walls magenta, Shift+click one), the
//                             selection hidden (H) or isolated (I or /) while editing only, Alt+H shows all
//   level_editor_walls.cpp    collision switched off and on with the game's own switch (WorldCell::change_live): invisible
//                             walls deleted (Delete) / restored and moved / turned, a drawn piece's collision off by
//                             itself (the Inspector's Collision); undo, `wall` layout records
//   level_editor_overlays.cpp the toolbar's View menu (Lit / Unlit / Wireframe / Collision only, Alt+1..4 - drawn by
//                             engine/gfx/view_mode.h) and Overlays menu (every overlay's checkbox, [editor] overlays=);
//                             level_editor_arcs.cpp jump arcs off the selected scenery's lips (Y),
//                             level_editor_grind_preview.cpp the rail under the cursor whole, level_editor_ruler.cpp
//                             the ruler (M), level_editor_thumbs.cpp the Add palette's thumbnail grid, Recent, Favourites
//   level_editor_checks.cpp   (UX5) the problem checker (Alt+P / the toolbar's Problems: Go to / Fix / Ignore; layout
//                             records that don't resolve kept, never dropped), level_editor_budget.cpp the budget
//                             meter (the engine's limits, on the toolbar), level_editor_notes.cpp note pins
//   level_editor_ux9.cpp      (UX9) the gap wizard (Alt+J, level_editor_gap_wizard.h: two clicks and a form make a
//                             gap's trigger volumes), the skateability overlay (Alt+K, level_editor_skate_view.h), raised /
//                             lowered ground with ramps (Alt+U, level_editor_ground.h: generated pieces, `ground` records)
//   level_editor_hud.cpp      the old HUD ([features] editor_ui=0, or when the UI can't start): a status panel top
//                             left, the keys at the bottom, a message line, and a panel for the help (F1) and the
//                             browser (B), made of the game's screen elements
//   level_editor_camera.cpp   the camera tools: F frames the selection, Alt+drag orbits, the right button + wheel sets
//                             the speed, Ctrl+1..9 / 1..9 camera bookmarks, the numpad's ortho views
//   level_editor_play.cpp     play from here (F5 / Ctrl+P) and back to the editor with the same camera and selection
//   level_editor_playtest.cpp play testing: the skater's trails from test runs (F7's Trail window), gaps validated by
//                             skating, a restart's Play from here, Simulate (F6: the world runs, the skater frozen)
//   level_editor_prefs.cpp    the preferences window ([editor] in the ini) and the first-run tips
// The controls (F1 lists them in the game):
//   - Flying: W A S D, Q / E down / up while the right mouse button is held (it looks too; [editor] wasd_flies=1: without
//     it, as the editor first was), Shift faster, the wheel goes forward / back.
//   - Tools (level_editor_transform.h, level_editor_snap.h): W / E / R (right button up) or Space pick Move / Rotate /
//     Scale - their gizmos' arrows, ring and cubes; ` World / Local; the toolbar's pivot, grid (1 - 100) and rotation
//     snap; while dragging X / Y / Z and digits type exact amounts, Ctrl / Ctrl+Shift / Alt+Shift / Ctrl+Alt snap to the
//     grid / the world's grid / a surface / a vertex; Alt+G a modal transform; Ctrl+Shift+D repeats the last duplicate.
//   - Seeing (the UI): the Outliner lists the level's nodes by family (search; the chips show / hide a family, in
//     the view too - kept in the ini as [editor] families=<bits>); a click on a row or a gizmo selects it (an
//     object's or scenery's node selects the object / scenery; a flag's object selects the flag's node), a
//     double-click takes the camera there; the Inspector shows the selection's node - a point node's position and
//     heading are fields to type or drag (one undo step a field), with when the change shows in the game.
//   - Selecting: a click on an object, scenery or a node's gizmo (scenery takes its group; hovering tints and outlines
//     what a click would take); floors, ceilings, rooms around the camera and see-through decals are passed - but
//     what's nearest along the ray (a floor, a wall, a locked piece) still hides what's behind it, gizmos included -
//     and a click again on the same spot takes the next thing under the cursor (level_editor_picking.h); Shift+click
//     adds / removes; Alt+click or a middle click one piece of scenery, any piece; L locks the selected objects /
//     scenery (clicks don't take them); the toolbar's Pick: what clicks may take; Backspace deselects.
//   - Rails: a rail's gizmos and the Inspector show its points where skaters grind them (the rail manager's - moved
//     with their scenery or by themselves); a rail point moves with its whole rail, or by itself (L with a rail point
//     selected, the Inspector's Whole rail), saved as `rail` records; Home puts it (its rail) back.
//   - Moving (objects, scenery, rail points and point nodes - spawns, crowns, flags, lights, sounds, triggers, other
//     nodes; not ladders, paths or particles yet): hold and drag (Shift: up / down; the wheel pushes / pulls; Ctrl or the toolbar's
//     Snap 25 snaps to 25; Escape puts it back), or drag one of the arrows to move along that axis only; W A S D / Q E
//     nudge; Z / X turn (objects and nodes 15 degrees, scenery a quarter); [ / ] scale scenery (about the middle of its
//     bottom) or objects (about their position) 10% (Shift: 1%), 0.25x to 4x, Alt+[ / ] stretch along the axis the
//     camera faces most (the Inspector's Scale X / Y / Z too); G drops it onto the ground (a marker shows
//     where that is). Moving a spawn, crown or flag of a level shared with THUGPRO warns once a run (the edit is made).
//   - Editing: Ctrl+D duplicates; Delete hides scenery (deletes copies of objects), deletes a node the editor added and
//     hides a level's own spawn / crown / flag / light / sound / proximity node (asking first: it isn't removed - the
//     crowns after a hidden crown each move up a number); Home puts it back where the level had it (a hidden node:
//     shows it again); Ctrl+Z / Ctrl+Y undo / redo (64 steps, this session). Ctrl+Shift+Delete (the toolbar's Reset
//     level; the old HUD: twice) puts everything back as the level has it and deletes its saved layout and autosave,
//     after asking - one undo step brings it back (the steps before it are dropped; the files only with Ctrl+S). An added crown moves up / down among the
//     added crowns (the Inspector); the level's own crowns keep the level's order.
//   - Adding: B lists the level's scenery and objects; Enter places a copy where the cursor points. N (Place) lists new
//     nodes - a multiplayer spawn, a team spawn and a CTF flag set per colour (the exact names the game looks for), a
//     crown, a level light, a sound emitter (silent for now), a proximity node: pick one, click in the view (dropped to
//     the ground, facing the camera's heading; Shift+click places more). Adding, hiding or reordering a spawn, crown or
//     flag of a level shared with THUGPRO warns once a run, as moving one does.
//   - Saving: Ctrl+S (layout version 2: moved nodes as `set <node> pos=x,y,z angles=x,y,z` records, added nodes as
//     `node <name> <Class> ...`, the added crowns' `order crown ...`, hidden ones as `hide <node>`; phase 2 and version 1
//     files still load). Unsaved changes are autosaved (every minute, on leaving, and from the crash logger -
//     level_editor_layout.h); an autosave newer than the saved layout is offered on entering: Ctrl+R restores it,
//     Ctrl+Shift+R discards it. A crash logs the editor's last action. Leaving: Escape, Enter (the Pause key) or a controller's Start (level_editor_exit) - Escape
//     first cancels a drag, closes a panel or drops the selection.
//
//   - Keys are the action registry's (level_editor_actions.h: rebindable, [editor_keys]); the command search
//     (Ctrl+Space / F3), the undo history (Ctrl+H), the Changes panel (Ctrl+Shift+H), box select, select similar
//     (Shift+G), the Outliner's toggles / groups / labels, copy / paste between levels (Ctrl+C / Ctrl+V) and layout
//     snapshots: level_editor_ux3.h.
//   - (UX8 - level_editor_ux8.h) the status bar's context hints (level_editor_hints.h), quick selection sets
//     (Ctrl+Alt+1..9 / Alt+Shift+1..9, the toolbar's Sets), pie menus (Q / Tab, Alt+Q / Shift+Tab), controller mode
//     (Preferences), Alt+wheel drill-through, the follow camera (Shift+F), double-click teleport, walk mode (Shift+`).
// thug2-decomp.ini [features] level_editor=1 (default; 0: LevelEditor_Available is false - no menu item - and
// LevelEditor_Start refuses), debug_editor=1 logs picks, moves and the view. `-edit <level>` (or edit_level=) starts the
// game in the editor in that level (frontend/start_level.h; tools/game/edit_level.ts). editor_ui=0 (default 1) brings
// back the old HUD in place of the Dear ImGui panels.
namespace frontend::level_editor {

constexpr std::size_t kBoxCornerCount = 8; // a box's corners
constexpr int kBoxCorners = 8;            // the same, as a loop's count (a corner's index bits: x, y, z)
// The editor's view: what its camera and its picking share.
constexpr float kNearPlaneMargin = 0.01f;       // a point this close to the near plane is clipped to it
constexpr float kReferenceViewHeight = 720.0f;  // the view height the on-screen sizes are made for
constexpr float kMaxPitchDegrees = 89.0f;       // the camera's pitch stops short of straight up or down
constexpr float kMinFlySpeed = 50.0f;           // the fly camera's speed range
constexpr float kMaxFlySpeed = 20000.0f;
constexpr double kMinFlySpeedDouble = 50.0;     // the same, where the speed is worked out in double
constexpr double kMaxFlySpeedDouble = 20000.0;
constexpr float kFarDistance = 1e30f;           // farther than anything: a search's start
constexpr double kFarDistanceDouble = 1e30;
constexpr float kDegreesPerHalfTurn = 180.0f;
// An ARGB colour's channels (0xAARRGGBB).
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;
constexpr int kAlphaShift = 24;
constexpr std::uint32_t kChannelMask = 0xFF;
constexpr std::uint32_t kRgbMask = 0x00FFFFFFu;
constexpr std::uint32_t kOpaqueAlpha = 0xFF000000u;
// Text the editor reads and writes: hex numbers, the typed characters it keeps.
constexpr int kHexBase = 16;
constexpr int kFirstPrintableChar = 0x20; // below: control characters
constexpr int kDeleteChar = 0x7F;
constexpr int kMaxGeometries = 16;        // an object's geometries the editor looks at
// The picking code's walk over a scene's meshes.
constexpr std::uint32_t kMeshScanLimit = 4096; // the meshes looked at, at most
constexpr int kMeshHiddenFlag = 0x10;     // a mesh flag: not drawn (left out)
constexpr std::size_t kPositionBytes = 12; // a vertex's position, the first three floats
constexpr int kNeutralVertexByte = 0x80;  // a vertex byte's fill: the middle of its range (no stored bytes)

// The axes' letters, by index (0 X, 1 Y, 2 Z).
inline constexpr std::string_view kAxisLetters = "XYZ";

// The setting (read once).
bool& is_switched_on();
bool& debug_enabled();
// The editor is running (between LevelEditor_Start and LevelEditor_Stop).
bool live_now();

// Script commands (CFunctions, registered by register_script_functions).
bool cfunc_available(script::Struct* params, script::Script* script); // LevelEditor_Available
bool cfunc_start(script::Struct* params, script::Script* script);     // LevelEditor_Start
bool cfunc_stop(script::Struct* params, script::Script* script);      // LevelEditor_Stop
// Our extra script commands, registered after the game's own table (sys::Main, after 0x005A5E70).
void register_script_functions();

// Start listening to SDL's mouse and key events (sio::init_sdl_input, next to mouse_menus::prepare_all).
void prepare_all();
// Once a frame from the front end's input logic (mouse_menus::tick_frame's stand-down while Active): a requested exit
// (Escape) runs level_editor_exit here.
void input_logic();
// Once a frame at the start of the render (render::draw_world): flying, looking, picking, dragging.
void tick_frame();
// Called by the renderer for a viewport's camera before it's read: while Active, the first viewport's camera gets the
// editor's pose (the game's pose noted first, put back by Stop).
void place_camera(gfx::ViewCam* camera);
// Called by the renderer once a viewport is set up: what the view's projection is (for the mouse ray and the
// outlines). `viewport`: the device viewport (x, y, width, height in back buffer pixels).
void note_view(gfx::ViewCam* camera, float angle, float aspect, std::span<const uint32_t, 4> viewport);
// Called by the renderer for a viewport's camera (render::draw_viewport): its field of view (degrees) and near plane - the
// game's own, except for the editor's camera in an ortho view (level_editor_camera.h: a long lens from far back).
float adjust_view_angle(gfx::ViewCam* camera, float angle);
float get_near_plane(gfx::ViewCam* camera);
// After the 2D pass: the selected scenery's grind rails, and outlines of what couldn't be tinted.
void blit();
// For the screenshot sidecar (platform/screenshot.h): what the editor shows now - its mode, the level, its camera,
// the selection and the node inspected, what's under the cursor, the changes - as name / value pairs; none while it
// isn't running.
std::vector<std::pair<std::string, std::string>> describe_for_screenshot();
// For the keyboard (Sio): whether the Pause key, `down` now, counts - only once it has been up since the editor
// started (the Enter that chose the menu item doesn't leave at once).
bool pass_pause_key(bool down);
// That gate: a key down counts only once it has been seen up (`released`).
bool pass_key(bool* released, bool down);

// --- the pieces (pure) ------------------------------------------------------------------------------------------

struct V3 {
    float x, y, z;
};
// A ray: where it starts and its direction.
struct Ray {
    V3 origin;
    V3 dir;
};
// A view's yaw and pitch (radians).
struct YawPitch {
    float yaw;
    float pitch;
};
// A panel's place on the screen: its top left and its size (pixels).
struct PaneArea {
    float x;
    float y;
    float width;
    float height;
};
// Where results go: a yaw and a pitch, a ray's origin and direction, a hit point and its normal.
struct YawPitchOut {
    float* yaw{};
    float* pitch{};
};
struct RayOut {
    V3* origin{};
    V3* dir{};
};
struct CursorOut {
    float* x{};
    float* y{};
};
struct CornerOut {
    int* x{};
    int* y{};
};
// (a cell's texture coordinates: left, top, right, bottom)
struct UvOut {
    float* x0{};
    float* y0{};
    float* x1{};
    float* y1{};
};
struct SurfaceOut {
    V3* point{};
    V3* normal{};
};
// The overlays' near depth and the selected one (a node or an item).
struct OverlayPick {
    float near_z_{};
    uint32_t selected{};
};
// The feature map's near depth and the selected node (-1: none).
struct ExtrasPick {
    float near_z_{};
    int selected_node_{};
};
// A pane's fonts: its heading's and its small one.
struct PaneFonts {
    ImFont* heading{};
    ImFont* small_font_{};
};
// Where a cast hit a surface: the point and the surface's normal.
struct SurfaceHit {
    V3 point;
    V3 normal;
};
inline V3 operator+(V3 a, V3 b) { return {.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {.x = a.x * s, .y = a.y * s, .z = a.z * s}; }
inline float get_dot(V3 a, V3 b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }
inline V3 perpendicular(V3 a, V3 b)
{
    return {.x = (a.y * b.z) - (a.z * b.y), .y = (a.z * b.x) - (a.x * b.z), .z = (a.x * b.y) - (a.y * b.x)};
}
V3 make_unit(V3 v);

// A camera's frame as the game keeps it (gfx::ViewCam basis_frame_ rows): right, up, back (the view looks along -back).
struct TickCycle {
    V3 right, up, back;
};
// The frame looking along yaw (about the world's up axis, 0 = along +z, positive turning towards +x) and pitch
// (up positive, radians): forward = (sin yaw cos pitch, sin pitch, cos yaw cos pitch); right = up(world) x back;
// up = back x right.
TickCycle frame_from_yaw_pitch(float yaw, float pitch);
// The yaw and pitch of a frame's view direction (-back).
void yaw_pitch_from_back(V3 back, YawPitchOut view);

// The fly keys this frame: each -1, 0 or 1 (forward/back, right/left, up/down).
struct FlyInput {
    int forward, right, up;
};
// The camera moved: forward along the view, right along the frame's right, up along the world's up; `distance` units.
V3 get_fly(V3 pos, const TickCycle& frame, const FlyInput& input, float distance);
// Mouse look: the yaw / pitch turned by a mouse movement (pixels; right turns right, up looks up), pitch kept within
// +-89 degrees.
void get_look(YawPitchOut view, float dx, float dy, float radians_per_pixel);

// The projection as the renderer sets it (render::choose_camera 0x004DB200: D3DX's look-at and perspective, the view's width at
// the near plane 4 tan(angle / 2) and its height that / (aspect * 0.90909094)): tangents of the half-angles.
struct Lens {
    float tan_x_, tan_y_;
};
// A lens: its angle (degrees) and its aspect.
struct LensShape {
    float angle_degrees_{};
    float aspect{};
};

Lens lens_for(LensShape lens_shape);
// A back buffer pixel within the viewport (x, y, width, height) to normalised device coordinates (x right, y up,
// -1..1); false outside a zero-sized viewport.
bool pixel_to_ndc(std::span<const uint32_t, 4> viewport, float px, float py, float* nx, float* ny);
void ndc_to_pixel(std::span<const uint32_t, 4> viewport, float nx, float ny, float* px, float* py);
// The ray through normalised device coordinates from the camera (direction not normalised: depth 1 along the view).
V3 ray_direction(const TickCycle& frame, const Lens& lens, float nx, float ny);
// A world point to normalised device coordinates; false when it isn't in front of the near plane (`nearZ`).
bool cast_onto(V3 camera_pos, const TickCycle& frame, const Lens& lens, V3 point, float near_z, float* nx, float* ny);

// Ray against a sphere / an oriented box (a model's box: `min` / `max` in the model's space, placed by the rows of
// `axes` - right, up, at - and `origin`; rows needn't be unit). The nearest hit's distance along `dir` (in units of
// dir), false if none in front.
bool ray_sphere(Ray ray, V3 centre, float radius, float* t);
bool ray_box(Ray ray, V3 box_origin, std::span<const V3, 3> axes, V3 min, V3 max, float* t);
// Ray against the plane through `point` with `normal`: false if parallel or behind.
bool ray_plane(Ray ray, V3 point, V3 normal, float* t);
// A box's eight corners in world space (bit 0 of the index: max x, bit 1: max y, bit 2: max z).
void box_corners(V3 box_origin, std::span<const V3, 3> axes, V3 min, V3 max, std::span<V3, kBoxCornerCount> out);
// Whether a model's bounding box looks like world coordinates rather than the model's own: it holds the object's
// position but not the model's origin (only told apart when the object is away from the world's origin).
bool box_looks_world_space(V3 min, V3 max, V3 object_pos);
// A direction turned about the world's up axis by `angle` radians (positive: +z towards +x, as yaw).
V3 turn_about_up(V3 v, float angle);
// A line from a to b (camera space depth = distance along the view) clipped to depth >= nearZ: false if all behind.
bool clip_to_near(V3 camera_pos, const TickCycle& frame, float near_z, V3* a, V3* b);

// A box placed in the world: p = origin + local.x * axes[0] + local.y * axes[1] + local.z * axes[2], local within
// min..max (an object's model box, or a scene sector's box where it is now).
struct OrientedBox {
    V3 origin;
    std::array<V3, 3> axes;
    V3 min, max;
};
// Its size along its own axes (the extents times the axes' lengths: a scaled box's size).
inline V3 box_size(const OrientedBox& box)
{
    const V3 extent = box.max - box.min;
    std::array<float, 3> lengths{};
    for (int i = 0; i < 3; ++i)
        core::at(lengths, i) = std::sqrt(get_dot(core::at(box.axes, i), core::at(box.axes, i)));
    return {.x = extent.x * core::at(lengths, 0), .y = extent.y * core::at(lengths, 1), .z = extent.z * core::at(lengths, 2)};
}

// --- scenery (the static scene sectors; level_editor_scenery.cpp) ------------------------------------------------

// A direction turned by `quarters` quarter turns about the world's up axis, exactly (no trigonometry): as
// turn_about_up(v, quarters * 90 degrees), and as the engine turns a sector's vertices (MeshRef::RotateY 0x004D6FB0:
// rotation 1 takes (dx, dz) from the pivot to (dz, -dx), rotation 3 to (-dz, dx)).
V3 get_quarter_turn(V3 v, int quarters);
// A vector scaled per axis (x by s.x, ...).
inline V3 per_axis(V3 v, V3 s) { return {.x = v.x * s.x, .y = v.y * s.y, .z = v.z * s.z}; }
// A per-axis scale as seen after `quarters` quarter turns about up: odd turns swap its x and z (get_quarter_turn(per_axis(v,
// s), q) == per_axis(get_quarter_turn(v, q), turned_scale(s, q))).
inline V3 turned_scale(V3 s, int quarters) { return (quarters & 1) ? V3{.x = s.z, .y = s.y, .z = s.x} : s; }
inline bool is_unit_scale(V3 s) { return s.x == 1.0f && s.y == 1.0f && s.z == 1.0f; }
inline bool is_uniform_scale(V3 s) { return s.x == s.y && s.y == s.z; }

// Where a moved sector is, relative to where it was when the editor first touched it: a point x of it then is now at
// get_quarter_turn(per_axis(x, scale), quarters) + offset (a scale along each of its own axes as first touched - uniform or
// stretched, level_editor_scale.h; 1, 1, 1 unless scaled).
struct Placement {
    int quarters{}; // 0..3
    V3 offset{};
    V3 scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f};
};
V3 get_apply(const Placement& placement, V3 x);
// Moved by `delta`; turned by `quarters` quarter turns about `pivot` (a world point, where it is now).
void move_by(Placement* placement, V3 delta);
void turn_about(Placement* placement, V3 pivot, int quarters);
// What the engine keeps for a placement: the sector's quarter turns (WorldCell +0x10, set by spin_around_up 0x0049E760)
// and its geometry's world position (the meshes' pivot, MeshRef +0x90's translation), from those when first touched
// (`quarters0`, `position0`). Unscaled, its vertices are at get_quarter_turn(v, quarters) + position for v as loaded (the
// engine has no scale of its own: level_editor_scale.h writes the scaled vertices).
void engine_state(int quarters0, V3 position0, const Placement& placement, int* quarters, V3* position);
// `outer` after `inner`: Apply(compose(outer, inner), x) == Apply(outer, Apply(inner, x)) - a copy made of a moved
// sector and moved since is placed by its own placement after its source's (relative to the source as loaded).
Placement compose(const Placement& outer, const Placement& inner);
// The box (min / max as when first touched) where the placement has put it (its axes as long as the scale says).
OrientedBox placed_box(V3 min, V3 max, const Placement& placement);
// Whether a sector of that box can be moved: not huge (the ground, a whole street: an extent over `limit`) and a
// real box.
bool sector_size_movable(V3 min, V3 max, float limit);
// The box around two boxes' (axis-aligned) corners: `have` false takes `min` / `max` as they come.
void grow_box(V3* min, V3* max, bool have, V3 bounds_low, V3 bounds_high);
// The axis-aligned box around an oriented box's corners.
void bounds_of(const OrientedBox& box, V3* min, V3* max);

// A value on a `grid` (Ctrl / the toolbar's Snap 25 while dragging: 25 units) - the nearest multiple; a grid of 0
// leaves it.
float snap_to_grid(float value, float grid);
// G (drop to the ground): where the collision ray starts below the selection - from 50 units above the base of an
// object or a node (its feet / its place: not under the floor it stands in), from just under the bottom of scenery
// (not hitting itself) - and the selection's position once its base is on the ground found (`groundY`).
V3 ground_ray_start(V3 pos, float base, bool from_above);
V3 dropped_to(V3 pos, float base, float ground_y);

// The point of the line through `p` along `u` nearest to the ray from `o` along `d` (the move arrows: dragging along
// one axis): its parameter `s` (p + u * s). False when the two are (nearly) parallel.
bool closest_on_axis(Ray axis, Ray line, float* s);
// The distance in pixels from (x, y) to the segment (ax, ay)-(bx, by).
float distance_to_segment(maths::Vec2 point, maths::Vec2 a, maths::Vec2 b);

// A selection grows by the pieces built onto it but not put in its cluster (LA's fire truck: its ladder): a piece
// (axis-aligned bounds `min`..`max`) joins when it is no bigger than the group was (`group_min`..`group_max`: no larger
// volume, no wider across than 1.25 times the group's longer side - not the street it stands on or the building beside
// it) and it touches one of the group's boxes (`boundsLow`..`boundsHigh`, within `margin`) above that box's bottom (by more
// than 5 units: not the ground under it).
bool piece_fits(V3 min, V3 max, V3 group_min, V3 group_max);
bool piece_touches(V3 min, V3 max, V3 bounds_low, V3 bounds_high, float margin);
// A grind rail's segment box, as the rail manager makes it (0x0057C260): the two points' bounds.
void segment_box(V3 a, V3 b, V3* min, V3* max);
// The highlight's colour at `seconds`: an RGBA (the engine's per-mesh colour, 0x80 a channel unchanged) between `low`
// and `high`, a pulse a second and a quarter (0 at low).
uint32_t get_pulse(uint32_t low, uint32_t high, double seconds);

} // namespace frontend::level_editor
