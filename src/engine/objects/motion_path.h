#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// MotionPathMembers::motion_flags_ (+0x18)
constexpr uint32_t kMotionFlagMoving = 0x18;   // (two bits: Obj_IsMoving / Obj_StopMoving)
constexpr uint32_t kMotionFlagOnPath = 0x10;   // following a path (the speed updated)
constexpr uint32_t kMotionFlagRotating = 0x27; // (Obj_IsRotating; Obj_StopRotating also clears 0x40)
// MotionPathMembers::path_flags_ (+0x1C)
constexpr uint32_t kMotionPathStopping = 0x8;   // Obj_StopAlongPath: slowing to a stop over a distance
constexpr uint32_t kMotionPathNoHeading = 0x10; // Obj_PathHeading off
constexpr uint32_t kMotionPathRandom = 0x200;   // Obj_RandomPathMode: a random link at each node

// The motion component's (type 0x0A015E17 "motion") members of 0x00423130-0x00423642: its member-function table, the
// moving / rotating flags, the path speeds and the speed's update along a path - as they see the component (its
// fields by offset). Kept apart from motion.h while the rest of the component is rewritten; the functions are the
// component's own (thiscall on it).
class MotionPathMembers {
public:
    static constexpr uint32_t kType = 0x0A015E17;

    // 0x00423130 (not a Ghidra function; registered with the component's Create, 0x00426650): its member functions'
    // table (0x00681EB8; filled by the static initialiser at 0x00640E30).
    static void publish_method_table();
    // 0x00423150: turning (any of kMotionFlagRotating).
    [[nodiscard]] bool is_spinning() const;
    // 0x00423520: kMotionPathRandom (the path follower takes a random link).
    [[nodiscard]] bool picks_random_link() const;
    // 0x00423510: the path follower (+0x1EC; null while not on a path).
    [[nodiscard]] void* get_path_follower() const { return path_follower_; }
    // 0x00423530: the speed along a path (kMotionFlagOnPath): stopping - from the speed when it began, the stop's
    // distance covered (the frame's travel added) eased linearly to the minimum stop speed, 0 at its end; else
    // accelerating (or decelerating) towards the path velocity, by the frame's share, never past it (0 acceleration:
    // at once).
    void update_path_speed();

    // --- the member functions scripts call (the table at 0x00681EB8) ---
    // 0x00423160 [Obj_IsMoving] / 0x00423170 [Obj_IsRotating]: true / false.
    int is_moving_from_script(script::Struct* params, script::Script* script);
    int is_rotating_from_script(script::Struct* params, script::Script* script);
    // 0x00423180 [Obj_StopMoving] / 0x00423190 [Obj_StopRotating]: the flags cleared. 1.
    int stop_moving(script::Struct* params, script::Script* script);
    int stop_rotating(script::Struct* params, script::Script* script);
    // 0x004231A0 [Obj_SetPathVelocity] / 0x00423210 [Obj_SetPathMinStopVel]: the unnamed float, in miles an hour
    // (into inches a second) - Fps: feet a second, ips: inches a second. 0x00423280 [Obj_SetPathAcceleration] /
    // 0x004232F0 [Obj_SetPathDeceleration]: the same per second (Fpsps / ipsps). Not given: as it was. 1.
    int set_path_velocity(script::Struct* params, script::Script* script);
    int set_path_min_stop_vel(script::Struct* params, script::Script* script);
    int set_path_acceleration(script::Struct* params, script::Script* script);
    int set_path_deceleration(script::Struct* params, script::Script* script);
    // 0x00423360 [Obj_PathHeading]: `Off` - kMotionPathNoHeading. 1.
    int route_facing(script::Struct* params, script::Script* script);
    // 0x004233A0 [Obj_RandomPathMode]: `Off` clears kMotionPathRandom, else sets it. 1.
    int random_path_mode(script::Struct* params, script::Script* script);
    // 0x004233E0 [Obj_StopAlongPath]: a stop over the unnamed distance (feet; `inches`: inches; 0 - or none: at
    // once), from the speed now down to the minimum stop speed (15 if 0). 1.
    int stop_along_path(script::Struct* params, script::Script* script);
    // 0x00423480 [Obj_StartAlongPath]: the stop called off. 1.
    int start_along_path(script::Struct* params, script::Script* script);
    // 0x00423490 [Obj_WaitJumpFinished] / 0x004234B0 [Obj_WaitRotate] / 0x004234D0 [Obj_WaitMove] / 0x004234F0
    // [Obj_WaitStop]: the script waits on the component (wait types 7, 9, 5, 8). 1.
    int wait_jump_finished(script::Struct* params, script::Script* script);
    int wait_rotate(script::Struct* params, script::Script* script);
    int wait_move(script::Struct* params, script::Script* script);
    int wait_stop(script::Struct* params, script::Script* script);

    std::array<uint8_t, 0x18> base_;    // the base component's
    uint32_t motion_flags_; // +0x18 MOTION_FLAG_*
    uint32_t path_flags_;   // +0x1C MOTION_PATH_*
    uint32_t unknown20_;
    float acceleration_;  // +0x24 inches a second per second
    float slowdown_rate_; // +0x28 "
    float path_velocity_; // +0x2C inches a second: the speed aimed at
    float velocity_;      // +0x30 the speed now
    float stop_distance_; // +0x34 Obj_StopAlongPath's (inches)
    std::array<uint8_t, 0x1A4 - 0x38> unknown38_;
    float stop_covered_;        // +0x1A4 of the stop's distance
    float stop_start_velocity_; // +0x1A8 the speed when it began
    float min_stop_velocity_;   // +0x1AC Obj_SetPathMinStopVel
    std::array<uint8_t, 0x1E0 - 0x1B0> unknown1b0_;
    float step_seconds_; // +0x1E0 seconds
    std::array<uint8_t, 0x1EC - 0x1E4> unknown1e4_;
    void* path_follower_; // +0x1EC
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern std::array<uint32_t, 294> g_motion_path_member_functions;
} // namespace objects
