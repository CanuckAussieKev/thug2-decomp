#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor.h"

namespace Front::LevelEditor::Overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's jump arcs (ours; see level_editor.h): with scenery selected and the overlay on (Y;
// level_editor_overlays.h), each lip of the selection is found in its collision and the flight a skater takes off it
// drawn at three speeds, with where it lands - so a gap can be judged makeable while the pieces are placed.
//
// The flight is the game's air physics (Skater::CorePhysicsComponent::DoAirPhysics 0x0050CA00, rewritten in
// game/skater/core_physics.cpp): each frame position += velocity t + (0, g, 0) t^2 / 2, velocity.y += g t, with
//   g = Physics_Air_Gravity / Physics_Air_hang_Stat  (-1350 / 1.0 in skater_physics) - off a kicker or ramp;
//   g = Physics_Air_Gravity / Physics_Vert_hang_Stat (-1350 / 1.1) - in vert air (off a quarter pipe's vert lip)
// as 0x004FF470 picks them (its flags +0x30 / +0x78: vert air; the moon cheat's Moon_gravity and a falling factor it
// also applies are left out). Air friction (Physics_Standing_Air_Friction, 1e-5) is left out too.
// The launch: the skater leaves the lip at its speed along the surface there (the lip's tangent, up the slope) and
// ollies on it: 0x00509450 (the jump) clamps a falling velocity.y to 0 and adds the jump speed to velocity.y - the
// full charge's Physics_Jump_Speed_Stat (414 - 450 by the ollie stat). The speeds, each by the skater's speed stat
// (CSkater::GetStat 3; Skater_Default_Stats without a skater) on its (low, high) pair as GetScaledValue 0x005805B0
// scales it (low + (high - low) stat / 10, then its limit=) - the switch and difficulty factors left out:
//   push: Skater_Max_Crouched_Kick_Speed_Stat (532 - 675, limit 900): what pushing reaches on the flat;
//   top:  Skater_Max_Speed_Stat (757 - 900): the most rolling downhill reaches;
//   max:  Skater_Max_Max_Speed_Stat (957 - 1100): the cap (speed boosts, transfers).
// The values are read from the game's own script structures (skater_physics or the ridden vehicle's - GetPhysicsFloat
// 0x00580400) each time the selection changes; ArcsFallback below when there is no level.
namespace Front::LevelEditor::Arcs {

// --- the pieces (pure, self-tested: src/hook/selftests_editor_ux4.cpp) -------------------------------------------

struct Face {
    V3 a, b, c;
    uint16_t flags;
};
// A lip: the top edge of a run of skatable sloped faces facing one way. `point` the middle of the edge, `tangent` the
// unit direction up the surface there (the launch direction), `heading` its horizontal part (unit), `across` the
// edge's direction (unit, horizontal) and `width` its length; `vert`: the surface there is (near) vertical or vert
// (face flag 0x08) - the skater goes straight up and comes back down.
struct Lip {
    V3 point, tangent, heading, across;
    float width = 0.0f;
    bool vert = false;
    float height = 0.0f; // (above the run's lowest point)
};
// The lips of a piece's collision faces (world space): skatable faces (not 0x10 / 0x40 / 0x04, not 0x02 alone) that
// slope (their outward normal's y between kSteepNormalY and kFlatNormalY), grouped by the heading of their upward slope
// (within kHeadingTolerance); near-vertical faces join the group they face the front of. The outward side: the winding's
// normal, or its reverse when most of the sloped area's normals point down (one convention for the piece) - so a kicker's
// back wall faces away from its slope and isn't taken for a vert lip. Each group's lip is at its highest vertices; its
// tangent the top faces' (by area). At most `maxLips`, the largest groups (by area) first; groups rising less than
// kMinLipHeight aren't lips.
constexpr float kFlatNormalY = 0.996f;      // (flatter than ~5 degrees: ground, a deck)
constexpr float kSteepNormalY = 0.1f;       // (steeper than ~84 degrees: near vertical)
constexpr float kHeadingTolerance = 0.94f;  // (cosine: within ~20 degrees)
constexpr float kMinLipHeight = 6.0f;
constexpr float kVertTangentY = 0.97f;      // (a tangent this steep launches straight up)
std::vector<Lip> FindLips(const std::vector<Face>& faces, int maxLips);

// A stat-scaled physics value: (low, high) by `stat` (0..10) as GetScaledValue scales it - low + (high - low) stat / 10
// - then held to `limit` (a maximum for a rising pair, a minimum for a falling one) when `hasLimit`.
float ScaledStat(float low, float high, float stat, bool hasLimit, float limit);

struct Physics {
    float airGravity = -1350.0f;  // (Physics_Air_Gravity / Physics_Air_hang_Stat)
    float vertGravity = -1227.3f; // (/ Physics_Vert_hang_Stat)
    float jump = 432.0f;          // (Physics_Jump_Speed_Stat at the ollie stat)
    float speeds[3] = {603.5f, 828.5f, 1028.5f}; // push, top, max
    float speedStat = 5.0f, ollieStat = 5.0f;
    std::string source = "the defaults (no level)";
};
// What skater_physics gives at stat 5 (Skater_Default_Stats) - used when the scripts can't be read.
Physics ArcsFallback();
constexpr int kSpeeds = 3;
const char* SpeedName(int index);
uint32_t SpeedColour(int index); // (ARGB)

// The velocity leaving the lip at `speed`, with the ollie's `jump` (a falling y clamped to 0 first, as 0x00509450).
V3 LaunchVelocity(const Lip& lip, float speed, float jump);

// A flight from `start` at `velocity` under `gravity` (y, negative), frame by frame (`dt` seconds) for at most `maxTime`
// seconds, each step's segment given to `cast` (from, to -> the hit and its normal; true on a hit) once past the first
// `skip` seconds (the lip itself). `points` the positions (the start first, the landing last when it landed).
struct Flight {
    std::vector<V3> points;
    bool landed = false;
    V3 landing = {}, normal = {};
    float time = 0.0f;
    V3 apex = {};
};
using Cast = std::function<bool(V3 from, V3 to, V3* hit, V3* normal)>;
Flight Fly(V3 start, V3 velocity, float gravity, float dt, float maxTime, float skip, const Cast& cast);
// The highest a vertical launch at `speed` (+ the jump) rises: v^2 / 2|g|.
float RiseHeight(float verticalSpeed, float gravity);
// A distance in units with feet ("812 (67.7 ft)": the game's units are inches).
std::string Distance(float units);

// --- the game side ----------------------------------------------------------------------------------------------

// The physics values for the current skater (read again when `refresh`).
const Physics& CurrentPhysics(bool refresh);
// The arcs over the view for the scenery selected (the sectors' checksums) - nothing when the overlay is off or
// nothing is selected. The lips' and flights' sums are made again only when the selection or its place changes.
void AddOverlay(const Overlays::Painter& painter, const std::vector<uint32_t>& sectors);
// For the status bar / help: what the arcs show now ("2 lips, lands 812 (67.7 ft) at top speed").
std::string Describe();

} // namespace Front::LevelEditor::Arcs
