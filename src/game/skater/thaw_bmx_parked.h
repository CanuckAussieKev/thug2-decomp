#pragma once

#include <cstdint>

namespace Mdl {
struct FrontEnd;
}

// NOT ORIGINAL - THAW's parked bikes (ours, added at the user's request, 2026-10-27; part of THAW's BMX in THUG2,
// game/skater/thaw_bmx.h): getting on by walking up to a bike, as THAW does.
//
// THAW has two kinds (its bike_scripts.qb): the level's own bike - a pedestrian node <zone>_TRG_Ped_BikeGuy whose
// body is the bike (profile Ped_BMX: si_bmx.skin on the si_bmx skeleton) lying in bmx_getup's first frame
// (THUG3_Ped_Bike_Guy), listed per level in bike_ped_ids - and the bike spot, the bike left where the rider got off
// (create_bike_spot). Both offer "Press \ms to ride the bike" in a speech box when the skater comes within 12 feet
// (setup_ped_speech / bike_spot_setup's AnyObjectInRadius); accepted, toggle_bike_on_off walks the skater to the bike
// (skater_walk_to_bike: turned to it, walked there, standing as it faces) and gets on (bmx_finish_on: the level's bike
// switched off - switch_all_bike_peds off -, the spot gone). Getting off leaves a spot and switches the level's bike
// back on (bmx_finish_off). Here (the scripts: tools/pipeline/thaw_bmx/ride.ts):
// - the level's bike is `thaw_bmx_home`: at THAW's own place in the THAW levels ported to THUG2 (the converter reads
//   bike_ped_ids and the nodes into thaw_bmx_home_bikes), else near where the skater started the level (StartSpot
//   below: 8 feet to the side, on the ground, nothing in between). It is there whenever the skater is off the bike,
//   in single-player games other than the story and classic modes (the script thaw_bmx_home_bike decides);
// - the offer is a polling script on each bike (its distance to the skater, THAW's 12 feet) and THUG2's speech box
//   (create_speech_box, as its ped_speech_got_trigger shows one; THUG2's own setup_ped_speech needs a node, which these
//   bikes aren't); the walk is ThawBmx_WalkToBike below, a frame at a time.
namespace Skater::ThawBmx {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

struct Point {
    float x, y, z;
};
// The walk's speed (THAW's skater_walk_to_bike: Obj_MoveToPos speed=10 - 10 feet a second, 2 units a frame) and its
// turn (Obj_LookAtPos speed=360: a turn a second, 6 degrees a frame).
constexpr float kWalkStep = 2.0f;
constexpr float kWalkTurnDegrees = 6.0f;
// How far from the start the level's bike goes when THAW has no place for it (feet x 12).
constexpr float kStartSpotDistance = 96.0f;

// One step from `from` towards `to`: `step` along the ground (x / z), the height in proportion. True (and `to`) when
// it's within the step.
bool StepToward(Point from, Point to, float step, Point* out);
// A heading (degrees; 0 = +z, 90 = +x) turned towards `target` by at most `maxStep` (the short way round), in
// [-180, 180). True when it got there.
bool TurnToward(float heading, float target, float maxStep, float* out);
// The heading of a direction along the ground (0 for none).
float HeadingOf(float x, float z);
// Where the level's bike may go near a start at `pos` facing `heading`: `distance` to the right, the left, behind,
// ahead (in that order).
void StartSpotCandidates(Point pos, float heading, float distance, Point out[4]);

// --- the game ---------------------------------------------------------------------------------------------------

// ThawBmx_HomeSpot (pos, heading: the level's bike - THAW's place, else the start spot; false: neither),
// ThawBmx_WalkToBike id=<object> (the local skater a step towards it; true once there, facing as it faces - or when
// it's gone), ThawBmx_BikeReachable id=<object> (false when collision lies between the skater and it: THAW's
// CollisionBetweenSkaterAndPoint). With RegisterScriptFunctions while the feature is on.
void RegisterParkedFunctions();
// From Update, every frame in a level: the start spot noted once a level is up, the level's bike made again when it's
// missing and the skater is off the bike.
void UpdateParked(Mdl::FrontEnd* frontEnd);

} // namespace Skater::ThawBmx
