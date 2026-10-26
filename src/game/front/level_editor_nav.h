#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_organize.h"

// NOT ORIGINAL - the level editor's navigation pieces (ours; see level_editor.h, level_editor_ux8.h): the pure parts of
// walk mode, click-to-teleport, the follow camera, wheel drill-through, the pie menus' geometry, a controller stick's
// response and the quick selection sets. The game side (what calls them with the editor's camera, keys and the game's
// collision ray) is level_editor_ux8.cpp.
namespace Front::LevelEditor::Nav {

// --- walk mode: the camera at a skater's eye height, with gravity and collision (Blender's walk navigation) ---------

// Units: the game's (an inch). A skater stands about 72 tall.
constexpr float kEyeHeight = 62.0f;   // the camera above the feet
constexpr float kRadius = 14.0f;      // how near a wall the feet come
constexpr float kStepHeight = 24.0f;  // a kerb or a stair is stepped up (and down, standing on the ground)
constexpr float kWalkSpeed = 220.0f;  // units a second
constexpr float kRunSpeed = 560.0f;   // (Shift)
constexpr float kGravity = 1400.0f;   // units a second squared
constexpr float kJumpSpeed = 330.0f;  // up, at a jump (about 39 units high)
constexpr float kGroundSearch = 5000.0f;

// The collision ray: from `from` to `to`, the first hit (false: none).
using Cast = std::function<bool(V3 from, V3 to, V3* hit)>;

struct Walker {
    V3 feet = {};
    float vy = 0.0f;       // up speed
    bool grounded = false;
};
struct WalkInput {
    float forward = 0.0f, right = 0.0f; // -1..1 (a stick: anything between)
    bool jump = false;                  // pressed (a jump starts only on the ground)
    bool run = false;
};
// One step: moved along the yaw's heading (FrameFromYawPitch's: forward = (sin yaw, 0, cos yaw)) - stopped at walls
// (sliding along them, one axis at a time), stepping up what's no higher than kStepHeight - then falling or standing on
// the ground below (snapped down a step while grounded); with no ground below at all it stays at its height (a level's
// edge: it doesn't fall out of the world). A jump starts on the ground; a ceiling stops it.
void Step(Walker* walker, float yaw, const WalkInput& input, float seconds, const Cast& cast);
// The camera for a walker, and a walker for a camera (dropping from where it is).
inline V3 EyeOf(const Walker& w) { return {w.feet.x, w.feet.y + kEyeHeight, w.feet.z}; }
Walker WalkerAt(V3 camera);

// --- click-to-teleport ----------------------------------------------------------------------------------------------

// Two clicks are a double click: within `maxMs` and `maxPixels` of each other.
bool IsDoubleClick(uint32_t firstMs, float firstX, float firstY, uint32_t secondMs, float secondX, float secondY,
                   uint32_t maxMs = 400, float maxPixels = 6.0f);
// Where a double click on a surface takes the camera: back along the ray from the point hit by kStandOff (or 80% of
// the way there when nearer), and at least kLift above the point - looking the same way.
constexpr float kStandOff = 260.0f;
constexpr float kLift = 60.0f;
V3 TeleportTarget(V3 camera, V3 hit);

// --- the follow camera (Unity's Shift+F) ---------------------------------------------------------------------------

// The selection's movement since the last frame (zero the first frame it's followed, or when `active` is false - the
// next active frame starts afresh).
struct Follower {
    bool have = false;
    V3 last = {};
};
V3 FollowStep(Follower* follower, bool active, V3 target);

// --- wheel drill-through ------------------------------------------------------------------------------------------

// What's under the cursor (`count` things, front to back): from `current` (the one the hover shows; -1 none), `steps`
// notches deeper (positive) or nearer, round the ends. -1 when there's nothing.
int DrillIndex(int current, int steps, int count);
// The click-to-cycle index that makes the hover show `shown` (the hover is the cycle's next: index + 1).
inline int CycleIndexShowing(int shown, int count) { return count > 0 ? (shown - 1 + count) % count : -1; }

// --- pie menus ----------------------------------------------------------------------------------------------------

// The slice under an offset from the pie's centre (screen pixels, y down): slice 0 straight up, then clockwise, each
// 360 / count degrees round its direction; -1 within `deadzone` (or no slices).
int SliceAt(float dx, float dy, int count, float deadzone);
// A slice's direction (unit, screen: y down).
void SliceDirection(int index, int count, float* dx, float* dy);
// A press held this long (ms) opens the pie to run what's pointed at on letting go; a shorter tap keeps it open until a
// click (Blender's pies).
constexpr uint32_t kPieTapMs = 250;

// --- a controller's stick -----------------------------------------------------------------------------------------

// An axis (SDL: -32768..32767) to -1..1: 0 within `deadzone` (a fraction of the range), then rescaled from there and
// squared (fine control near the middle), the sign kept.
float Stick(int raw, float deadzone);

// --- quick selection sets (UX3's groups by the names "Set 1".."Set 9") ---------------------------------------------

constexpr int kQuickSets = 9;
std::string QuickSetName(int number); // "Set 3"
// The group of that name's index (-1 none).
int FindGroup(const Organize::State& state, const std::string& name);
// The group `name` made of `members` (replaced when there is one, else added at the end). False: no members.
bool StoreGroup(Organize::State* state, const std::string& name, const std::vector<Organize::Member>& members);
// `members` taken out of group `index` (false: none of them were in it).
bool RemoveFromGroup(Organize::State* state, int index, const std::vector<Organize::Member>& members);

} // namespace Front::LevelEditor::Nav
