#pragma once

// NOT ORIGINAL - [debug] collision_events (thug2-decomp.ini, default 0): logs each time the skater's velocity turns
// round (reverses) on the ground, with what caused it - to find the spots in converted levels where the collision is
// wrong (no ground, an invisible wall or ramp, a face flagged unskatable...) by skating round and reading the log
// afterwards (tools/game/collision_events.ts groups them). Our own debugging aid, not in the original game.
//
// How it works:
// - The skater's physics paths that turn, reflect or stop the skater against collision call NotePath after they ran
//   (core_physics_walls.cpp, core_collision.cpp, core_physics.cpp): which path, the velocity before and after, and
//   the face the component's feeler hit. Nothing is changed; with the switch off NotePath returns at once.
// - EndFrame (the end of CorePhysicsComponent::Update, once a frame) keeps the last 90 frames' velocities. A reversal:
//   on the ground now, moving (5+ inches a second); within the last 60 frames a ground frame moving 60+ along a
//   direction at least 145 degrees from today's (dot < -0.8); and between them a sudden change - one frame's
//   direction turning by more than 60 degrees or the speed going through zero (a turn, even a sharp one, is a few
//   degrees a frame and isn't one). Not counted when the skater was on a rail / lip / wall in between or in the air
//   for more than 20 frames (a quarter pipe's air).
// - The event's reason is the strongest path that ran between the two frames (Reason below); none: "slope" - the
//   ground's own gravity turned the skater back (the ground under the skater at its steepest is the event's face).
// - Then three probe lines (each twice: as the skater's feelers see the world - faces flagged 0x10 skipped - and
//   seeing every face): down through the face's hit point, down 24 inches beyond it along the old travel direction,
//   and ahead from the old position along it at the forward feeler's height.
// - One log line, "collision-event: key=value ..." (FormatEvent below). The same spot (within 40 inches) is logged
//   once in 5 seconds.
// - [debug] collision_event_screenshots=1 (default 0): a screenshot of the event's frame too (Sys::Screenshot::
//   RequestEventShot; the reversal is seen the frame it happens, while the camera still looks the way the skater was
//   going - the camera follows the skater's turn only over the next frames), named in the log line.

#include <cstdint>
#include <string>

#include "game/mth/vector.h"

namespace Skater::CollisionEvents {

// Which physics path ran (the order is the priority when several ran: the first wins).
enum class Path : uint8_t {
    WallAheadCorner,  // 0x005059D0's second feeler: into a corner - turned back by pi less the bounce, halved
    WallAhead,        // 0x00506E50 -> 0x005059D0: the forward feeler hit a wall / unskatable face - bounced off it
    WallAheadPlant,   // 0x005059D0 left to 0x004FF670 (the wall push-off) instead
    SideWall,         // 0x0050A2C0 on the ground: a near-vertical wall to the side - put back, turned off it
    AirBonk,          // 0x00508780: a wall ahead in the air - the velocity along it, pushed off
    AirWallVert,      // 0x00508780 on vert: back at the hit, the velocity along the wall
    Ceiling,          // 0x00504EE0 / 0x005051B0: kept under a ceiling, the velocity along it
    GroundUnskatable, // 0x00509BD0: the ground feeler hit a face that isn't skatable - off the ground
    GroundRejected,   // 0x00509BD0: ground too steep against the last or too far below - off the ground
    NoGround,         // 0x00509BD0: nothing under the skater - off the ground
    WallAheadClimb,   // 0x00506E50: the forward feeler hit skatable ground at an angle - moved onto it
    Count,
};
const char* PathName(Path path);

// [debug] collision_events (read once).
bool On();

// After a path ran: `component` the skater's core physics component (its feeler's hit is read), `before` the
// skater's velocity when the path began.
void NotePath(void* component, Path path, const Mth::Vector& before);

// The end of the core physics component's update.
void EndFrame(void* component);

// --- the line's text (pure; self-tested) ---

struct Face {
    bool hit = false;
    bool object = false;    // a movable object's collision (else level geometry)
    uint32_t checksum = 0;  // the collision object's (the sector's) / the object's id
    int index = -1;
    uint16_t flags = 0;
    uint8_t terrain = 0;
    float point[3] = {};
    float normal[3] = {};
};

struct Event {
    int number = 0;
    std::string level;
    uint32_t frame = 0;
    const char* reason = "";
    std::string paths;       // the paths that ran, comma-separated ("-": none)
    float spot[3] = {};      // where to look: the face's hit point, or the skater
    float posBefore[3] = {}, pos[3] = {};
    float velBefore[3] = {}, velAfter[3] = {};
    float headingBefore = 0, heading = 0;
    float groundNormal[3] = {};
    float groundMinY = 1;    // the ground normal's lowest y between the two frames
    int frames = 0;          // frames between the two
    Face face;
    Face probes[6];          // down at the hit, down beyond, ahead - each as the skater sees, then every face
    std::string screenshot;  // "-" or "requested"
};

// The face flags' names, '|'-joined ("0" for none).
std::string FlagNames(uint16_t flags);
// A face as one value: "none" or "sector:<name>;face:<i>;flags:0x<f>;decoded:<names>;terrain:<t>;n:<x,y,z>;at:<x,y,z>"
// ("object:<name>" for a movable's). `name` names a checksum (no spaces).
std::string FaceText(const Face& face, std::string (*name)(uint32_t));
// The whole line, starting "collision-event: ".
std::string FormatEvent(const Event& event, std::string (*name)(uint32_t));

} // namespace Skater::CollisionEvents
