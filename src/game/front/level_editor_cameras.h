#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_ui.h"

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
namespace Front::LevelEditor::Cameras {

// --- the pure parts (self-tested: src/hook/selftests_editor_map3.cpp) ----------------------------------------------

constexpr float kDefaultDistance = 1000.0f;
constexpr float kMinDistance = 50.0f, kMaxDistance = 20000.0f;
float ClampDistance(float d);
// A node's angles for the editor's view (yaw / pitch as FrameFromYawPitch takes them), and back.
V3 AnglesFor(float yaw, float pitch);
void YawPitchOf(V3 angles, float* yaw, float* pitch);
// Where it looks: the view direction of those angles.
V3 Forward(V3 angles);
// The point looked at `distance` ahead, and the camera's place relative to it (virtual_cam's targetOffset /
// positionOffset).
V3 TargetOf(V3 pos, V3 angles, float distance);
V3 PositionOffset(V3 pos, V3 angles, float distance);
// The frustum's far corners `depth` ahead (the lens's half-angle tangents): right-up, left-up, left-down, right-down.
void FrustumCorners(V3 pos, V3 angles, float tanX, float tanY, float depth, V3 out[4]);

struct Camera {
    std::string name;
    V3 pos = {}, angles = {};
    float distance = kDefaultDistance;
};
bool Same(const Camera& a, const Camera& b);
// The record's text and one read (after the word "camera"; false: malformed - no pos, a bad name or number, a key
// twice).
std::string WriteRecord(const Camera& camera);
bool ReadRecord(std::istringstream& in, Camera* out);
// The stem of fresh names.
constexpr const char* kStem = "ed_camera";

// --- the game side ------------------------------------------------------------------------------------------------

// The cameras there are now (their nodes' places).
std::vector<Camera> All();
bool IsCamera(uint32_t name);
// A camera made where the editor's view is, looking its way - one undo step (its name; "" when it couldn't be).
std::string CreateFromView();
// Looking through it (the editor's camera put there).
bool Preview(uint32_t name);
// The camera put where the view is now (one undo step).
bool AimFromView(uint32_t name);
// Its pitch (degrees) / target distance set (one undo step a change, `done` false while a field is dragged).
bool SetPitch(uint32_t name, float degrees, bool done);
bool SetDistance(uint32_t name, float distance, bool done);
// Removed (one undo step).
bool Remove(uint32_t name);
// Layouts: the cameras now; records made (how many); every camera removed; a new level (forgotten).
std::vector<Camera> Capture();
int Apply(const std::vector<Camera>& cameras);
void ResetAll();
void ForgetLevel();
// Once a frame while the editor runs: what the UI asked for; the nodes' offsets kept in step with their places.
void Update();
// The frustums (the selected camera - `selected`: its node name, 0 none - brighter) over the view.
void AddOverlay(V3 cameraPos, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                uint32_t selected, std::vector<Ui::Line>* lines);
// The Inspector: the selected camera's section (`node`: its name, 0 none), and the Cameras section (new from the
// view, the list with Look through).
void InspectorSection(uint32_t node, ImFont* mono, ImFont* smallFont);
void ListSection(ImFont* mono, ImFont* smallFont);

} // namespace Front::LevelEditor::Cameras
