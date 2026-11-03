#pragma once

#include <cstdint>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_ui.h"
#include "engine/core/checksums.h"

struct ImFont;

// NOT ORIGINAL - the level editor's camera nodes (ours; see level_editor.h - the feature map's "camera nodes" row): a
// camera made from the editor's view (V, or the Inspector's Cameras section), previewed (looking through it), moved and
// turned as any point node (drag, the arrows, Z / X its heading; the Inspector its pitch and target distance), deleted
// (Delete), drawn as a frustum with a line to what it looks at.
//
// THUG2's levels have no camera nodes (studied: every shipped level's node array - their Type=Camera nodes are
// ProximNode sound-reverb boxes). Their intro, goal and "view goals" cameras are script structures: {virtual_cam
// targetid=world targetOffset=(the point looked at) positionOffset=(the camera's place relative to it) frames=...}
// (e.g. BO_scripts.qb's BO_Goal_Intro_Cameras, every goal's view_goals_cameras). So a camera node is our own:
//   {Pos=(...) Angles=(pitch, heading, 0) Name=ed_cameraNN Class=GenericNode Type=ed_camera
//    targetOffset=(...) positionOffset=(...)}
// Pos and Angles as every node has them (the game's angles-to-matrix 0x0041A730 turns the node's at row to where it
// looks - self-tested), and targetOffset / positionOffset as a virtual_cam takes them (the target `dist` units ahead):
// a script can make a virtual_cam of the node's own fields. The node is in the editor's overlay (level_editor_
// node_create.h - owned by this module: its own layout record), never parsed by the game (GenericNode makes nothing);
// its offsets are kept in step with its place every frame.
//
// Layout record (version 2 still; an older reader skips the kind):
//   camera <name> pos=x,y,z angles=x,y,z dist=d
namespace frontend::level_editor::cameras {

// --- the pure parts ------------------------------------------------------------------------------------------------

constexpr float kDefaultDistance = 1000.0F;
constexpr float kMinDistance = 50.0F, kMaxDistance = 20000.0F;
float clamp_distance(float d);
// A node's angles for the editor's view (yaw / pitch as frame_from_yaw_pitch takes them), and back.
V3 angles_for(float yaw, float pitch);
void yaw_pitch_of(V3 angles, YawPitchOut view);
// Where it looks: the view direction of those angles.
V3 get_forward(V3 angles);
// The point looked at `distance` ahead, and the camera's place relative to it (virtual_cam's targetOffset /
// positionOffset).
V3 target_of(V3 pos, V3 angles, float distance);
V3 position_offset(V3 pos, V3 angles, float distance);
// A camera's position and angles.
struct CameraPlace {
    V3 pos{};
    V3 angles{};
};

// The frustum's far corners `depth` ahead (the lens's half-angle tangents): right-up, left-up, left-down, right-down.
void frustum_corners(CameraPlace camera_place, float tan_x, float tan_y, float depth, std::span<V3, 4> out);

struct ViewCam {
    std::string name;
    V3 pos = {}, angles = {};
    float distance = kDefaultDistance;
};
bool get_same(const ViewCam& a, const ViewCam& b);
// The record's text and one read (after the word "camera"; false: malformed - no pos, a bad name or number, a key
// twice).
std::string write_record(const ViewCam& camera);
bool read_record(std::istringstream& in, ViewCam* out);
// The stem of fresh names.
constexpr const char* kStem = "ed_camera";

// (map4) Playing a camera: the script command LevelEditor_PlayCamera camera=<node> [time=<seconds>] (the editor's
// Camera triggers call it - level_editor_triggers.h - and so may a level's own scripts) plays the node as the game
// plays its goal / view-goals cameras: PlaySkaterCamAnim {name=ed_camera_shot virtual_cam targetid=world
// targetOffset=... positionOffset=... frames=<n> skippable=1} (cfunc_play_skater_cam_anim 0x005AD830 - view_goals_menu.qb's
// play_single_view_goals_cam is the same call), without play_hold: after its frames the camera animation ends and
// the skater's camera is back (a button skips it). The offsets: an editor camera's from its place now (target_of /
// PositionOffset), else the node's targetOffset / positionOffset, else its Pos / Angles looking kDefaultDistance
// ahead. A shot still playing is killed first (KillSkaterCamAnim name=ed_camera_shot, 0x005ADD70). Refused while the
// editor runs (its own camera; Look through is the preview there).
constexpr float kShotDefaultSeconds = 3.0F, kShotMinSeconds = 0.5F, kShotMaxSeconds = 60.0F;
// The animation's frames for `seconds` (kept within the limits; not a number: the default) - 60 a second.
int shot_frames(float seconds);

// --- the game side ------------------------------------------------------------------------------------------------

// The cameras there are now (their nodes' places).
std::vector<ViewCam> get_all();
// LevelEditor_PlayCamera (registered with the editor's other commands - level_editor.h's register_script_functions).
bool cfunc_play_camera(script::Struct* params, script::Script* script);
bool is_camera(uint32_t name);
// A camera made where the editor's view is, looking its way - one undo step (its name; "" when it couldn't be).
std::string create_from_view();
// Looking through it (the editor's camera put there).
bool get_preview(uint32_t name);
// The camera put where the view is now (one undo step).
bool aim_from_view(uint32_t name);
// Its pitch (degrees) / target distance set (one undo step a change, `done` false while a field is dragged).
bool set_pitch(checksums::Name camera, float degrees, bool done);
bool place_at_range(checksums::Name camera, float distance, bool done);
// Removed (one undo step).
bool drop_out(uint32_t name);
// Layouts: the cameras now; records made (how many); every camera removed; a new level (forgotten).
std::vector<ViewCam> get_capture();
int get_apply(const std::vector<ViewCam>& cameras);
void reset_all();
void forget_level();
// Once a frame while the editor runs: what the UI asked for; the nodes' offsets kept in step with their places.
void tick_frame();
// The frustums (the selected camera - `selected`: its node name, 0 none - brighter) over the view.
void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, OverlayPick overlay_pick, std::vector<ui::Seg>* lines);
// The Inspector: the selected camera's section (`node`: its name, 0 none), and the Cameras section (new from the
// view, the list with Look through).
void inspector_section(uint32_t node, ImFont* mono, ImFont* small_font);
void list_section(ImFont* mono, ImFont* small_font);

} // namespace frontend::level_editor::cameras
