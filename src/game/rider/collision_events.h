#pragma once

// NOT ORIGINAL - [debug] collision_events (thug2-decomp.ini, default 0): logs each time the skater's velocity turns
// round (reverses) on the ground, with what caused it - to find the spots in converted levels where the collision is
// wrong (no ground, an invisible wall or ramp, a face flagged unskatable...) by skating round and reading the log
// afterwards (tools/game/collision_events.ts groups them). Our own debugging aid, not in the original game.
//
// How it works:
// - The skater's physics paths that turn, reflect or stop the skater against collision call note_path after they ran
//   (core_physics_walls.cpp, core_collision.cpp, core_physics.cpp): which path, the face the component's feeler hit
//   and the feeler's line. Nothing is changed; with the switch off note_path returns at once.
// - end_frame (the end of SkaterPhysicsModule::tick_frame, once a frame) keeps the last 90 frames (find_reversal). A
//   reversal: on the ground now, moving (5+ inches a second); within the last 60 frames a ground frame moving 60+
//   along a direction at least 145 degrees from today's (dot < -0.8); and between them a sudden change - one frame's
//   direction turning by more than 60 degrees or the speed going through zero (a turn, even a sharp one, is a few
//   degrees a frame and isn't one). Not counted when the skater was on a rail / lip / wall in between or in the air
//   for more than 20 frames (a quarter pipe's air).
// - Checkpoints through the frame's physics (Checkpoint: core_update.cpp, core_ground.cpp, check_for_ground) keep the
//   velocity at each and the feeler's line; the event says between which two it turned round
//   (flipped=<before>><after>@<frame>).
// - The event's reason (choose_reason):
//   - "uber-frig": the velocity exactly negated between one frame's physics and the next (flipped=update-rest>
//     update-start): the skater's adjust-physics component (SkaterAdjustPhysics, 0x004F8420 - not rewritten) found
//     NO collision within uber_frig_current_height feet under the skater and put it back where the frame began (the
//     core's +0x98), velocity negated - a hole in the level's collision just ahead (the probes show where). Its face
//     is the last ground the skater stood on, its ray the frig's own down feeler;
//   - else the strongest path that ran between the two frames (Path below);
//   - none: "slope" - the ground's own gravity turned the skater back - only when the ground was steeper than 5
//     degrees somewhere in the window; on flatter ground it's the player turning round (braking, pushing back,
//     reverting) and nothing is logged.
// - Three probe lines (each twice: as the skater's feelers see the world - faces flagged 0x10 skipped - and seeing
//   every face): down through the face's hit point, down 24 inches beyond it, and ahead from the old position at the
//   forward feeler's height - beyond and ahead along the travel direction BEFORE the reversal (probe_dir), i.e. where
//   the skater was going, not where he goes after it.
// - The main line, "collision-event: key=value ..." (format_event), and a second, "collision-event-detail: n=<the
//   same> ..." (format_detail): the skater's state and input, the deciding ray, the face's corners and object and its
//   nearest edge (open: no face of its object across it and nothing under a probe just beyond it - a dropped piece),
//   the level's collision / scene files (size and time, to tell a reconverted file), the approach, the camera.
// - Rate limit: the same spot (within 40 inches) again within 1 second isn't logged; the next line there says how
//   many were left out (repeats=).
// - On screen: "collision event <n>: <reason>" for 2 s (sys::Screenshot::show_notice; " (screenshot)" when one was
//   taken). A level's first frame logs "collision-event: logging on in <level>".
// - [debug] collision_event_screenshots (default 1 - 0 turns them off): a screenshot of the event's frame
//   (sys::Screenshot::request_event_shot; the reversal is seen the frame it happens, while the camera still looks the
//   way the skater was going - it follows his turn only over the next frames), named in the log.

#include <cstdint>
#include <string>

#include "engine/maths/vec4.h"

namespace skater {
class SkaterPhysicsModule;
} // namespace skater

namespace skater::collision_events {

// Which physics path ran (the order is the priority when several ran: the first wins).
enum class RouteWay : uint8_t {
    kWallAheadCorner,  // 0x005059D0's second feeler: into a corner - turned back by pi less the bounce, halved
    kWallAhead,        // 0x00506E50 -> 0x005059D0: the forward feeler hit a wall / unskatable face - bounced off it
    kWallAheadPlant,   // 0x005059D0 left to 0x004FF670 (the wall push-off) instead
    kSideWall,         // 0x0050A2C0 on the ground: a near-vertical wall to the side - put back, turned off it
    kAirBonk,          // 0x00508780: a wall ahead in the air - the velocity along it, pushed off
    kAirWallVert,      // 0x00508780 on vert: back at the hit, the velocity along the wall
    kCeiling,          // 0x00504EE0 / 0x005051B0: kept under a ceiling, the velocity along it
    kGroundUnskatable, // 0x00509BD0: the ground feeler hit a face that isn't skatable - off the ground
    kGroundRejected,   // 0x00509BD0: ground too steep against the last or too far below - off the ground
    kNoGround,         // 0x00509BD0: nothing under the skater - off the ground
    kWallAheadClimb,   // 0x00506E50: the forward feeler hit skatable ground at an angle - moved onto it
    kTotalOf,
};
const char* path_name(RouteWay path);

// [debug] collision_events (read once).
bool get_on();

// After a path ran: `component` the skater's core physics component (its feeler's hit and line are read), `before`
// the skater's velocity when the path began.
void note_path(SkaterPhysicsModule* component, RouteWay path, const maths::Vec4& before);

// The end of the core physics component's update.
void end_frame(SkaterPhysicsModule* component);

// A point in the frame's physics (label: a name without spaces): the velocity there is kept, and an event says
// between which two the velocity turned round (flipped=<before>><after>).
void checkpoint(SkaterPhysicsModule* component, const char* label);

// --- the decisions (pure) ----------------

// A frame as the detector sees it.
struct Motion {
    uint32_t frame = 0;
    int state = 0; // the SkaterState state: 0 ground, 1 air, 2 wall ride, 3 lip, 4 rail, 5 wallplant
    float vel[3] = {};
};
// The reversal's earlier frame (an index into `frames`, oldest first, the newest last), or -1 for none.
int find_reversal(const Motion* frames, int count);
// The ground counts as a slope below this normal y (cos 5 degrees).
constexpr float kSlopeNormalY = 0.9962f;
// The reason, or null when the event isn't logged (no path, the ground never steeper than 5 degrees: the player
// turned round). `path`: the strongest path's name (null: none).
const char* choose_reason(bool uber_frig, const char* path, float ground_min_y);

// --- the lines' text (pure) ----------------

struct Face {
    bool hit = false;
    bool object = false;   // a movable object's collision (else level geometry)
    uint32_t checksum = 0; // the collision object's (the sector's) / the object's id
    int index = -1;
    uint16_t flags = 0;
    uint8_t terrain = 0;
    float point[3] = {};
    float normal[3] = {};
    void* coll_obj_ = nullptr;       // (not printed: the render::HitMesh hit, for its geometry)
};

struct Occurrence {
    int number = 0;
    std::string level;
    uint32_t frame = 0;
    const char* reason = "";
    std::string paths;  // the paths that ran, comma-separated ("-": none)
    float spot[3] = {}; // where to look: the face's hit point, or the skater
    float pos_before_[3] = {}, pos[3] = {};
    float vel_before_[3] = {}, vel_after_[3] = {};
    float heading_before_ = 0, heading = 0;
    float ground_normal_[3] = {};
    float ground_min_y_ = 1; // the ground normal's lowest y between the two frames
    int frames = 0;          // frames between the two
    Face face;
    Face probes[6];           // down at the hit, down beyond, ahead - each as the skater sees, then every face
    float probe_dir_[3] = {}; // the direction of the beyond / ahead probes: the travel before the reversal
    std::string flipped;      // the checkpoints the velocity turned round between ("-": not seen)
    int repeats = 0;          // events left out at this spot since the last line (the rate limit)
    std::string screenshot;   // "-", "requested" or "busy"
};

struct Detail {
    int number = 0;
    const char* state = "-";   // ground / air / wallride / lip / rail / wallplant
    const char* physics = "-"; // skating / walking
    float speed = 0;
    bool trick = false;
    const char* balance = "-";       // manual / nose_manual / skitch
    std::string input = "-";         // the buttons held at the event ('+'-joined)
    std::string input_window_ = "-"; // ... at any frame of the window
    float stick[2] = {};
    bool ray = false; // the feeler that decided it
    float probe_origin_[3] = {}, ray_end_[3] = {};
    bool geometry = false; // the face's corners and object
    float corners[3][3] = {};
    float bounds_low_[3] = {}, bounds_high_[3] = {};
    int edge = -1; // the face's edge nearest the spot (corner k to k+1)
    float edge_distance_ = 0;
    int edge_open_ = -1;                          // 1 open, 0 a face across it, -1 not known
    std::string col_file_ = "-", scn_file_ = "-"; // "<size>@<time>"
    std::string approach = "-";                   // positions over the last second: "x,y,z,<state letter>;..."
    bool camera = false;
    float camera_pos_[3] = {}, camera_dir_[3] = {};
};

// The face flags' names, '|'-joined ("0" for none).
std::string flag_names(uint16_t flags);
// A face as one value: "none" or "sector:<name>;face:<i>;flags:0x<f>;decoded:<names>;terrain:<t>;n:<x,y,z>;at:<x,y,z>"
// ("object:<name>" for a movable's). `name` names a checksum (no spaces).
std::string face_text(const Face& face, std::string (*name)(uint32_t));
// The main line, starting "collision-event: ".
std::string format_event(const Occurrence& event, std::string (*name)(uint32_t));
// The second line, starting "collision-event-detail: n=<n> ".
std::string format_detail(const Detail& detail);
// The pad's buttons held in `mask` (bit i: the PadSnapshot's i-th button), '+'-joined ("-" for none).
std::string pad_button_labels(uint16_t mask);

} // namespace skater::collision_events
