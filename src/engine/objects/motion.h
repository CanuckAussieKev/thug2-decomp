#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Script;
class Struct;
} // namespace script

namespace maths {
class ArcRotationBlender;
}

namespace objects {

class PathFollower;
class FollowTrail;
class ObjectGroup;

// KinematicsModule::motion_flags_ (+0x18): what the object is doing.
constexpr uint32_t kMotionRotateX = 0x1; // turning about its x axis (Obj_RotX, a look-at)
constexpr uint32_t kMotionRotateY = 0x2; // ... y (Obj_RotY)
constexpr uint32_t kMotionRotateZ = 0x4; // ... z (Obj_RotZ); bit `axis` for axis 0-2
constexpr uint32_t kMotionRotateAxes = 0x7;
constexpr uint32_t kMotionMoving = 0x8;            // moving to a point (Obj_MoveTo*)
constexpr uint32_t kMotionFollowingPath = 0x10;    // along a path of nodes (Obj_FollowPath*)
constexpr uint32_t kMotionTurning = 0x20;          // turning to an orientation over time (Obj_Rotate)
constexpr uint32_t kMotionLookingAt = 0x40;        // the axes' turns were set up by a look-at (Obj_LookAt*)
constexpr uint32_t kMotionHovering = 0x80;         // bobbing up and down (Obj_Hover)
constexpr uint32_t kMotionJumping = 0x100;         // Obj_Jump
constexpr uint32_t kMotionFollowingLeader = 0x200; // along the trail of another object (obj_follow_leader)
// Obj_WaitMove waits for these; Obj_WaitRotate for kMotionRotateAxes | kMotionTurning.
constexpr uint32_t kMotionMoveMask = kMotionMoving | kMotionFollowingPath;
constexpr uint32_t kMotionRotateMask = kMotionRotateAxes | kMotionTurning;

// KinematicsModule::motion_flags2_ (+0x1C)
constexpr uint32_t kMotion2StickToGround = 0x4;    // Obj_StickToGround (not `off`)
constexpr uint32_t kMotion2StoppedOnPath = 0x8;    // Obj_StopAlongPath (Obj_StartAlongPath / FollowPath clear it)
constexpr uint32_t kMotion2NoPathHeading = 0x10;   // the path's heading isn't taken (Obj_PathHeading off)
constexpr uint32_t kMotion2RotateForeverX = 0x20;  // the axis turns without a target angle (0x20 << axis)
constexpr uint32_t kMotion2ConstantHeight = 0x100; // obj_set_constant_height: a path doesn't change its height
constexpr uint32_t kMotion2RandomLinks = 0x200;    // Obj_FollowPathLinked picks a random link (from obj_follow_leader)
constexpr uint32_t kMotion2LevelHeading = 0x400;   // the path's heading kept level (peds: game_objects.cpp)

// The motion component (type 0x0A015E17 "motion", vtable 0x0064620C, 0x24C bytes; one of the motion components
// add_motion_components gives an object): moves an object about by script - along paths of nodes (with a
// PathFollower), to points, turning about its axes or to an orientation over time, looking at things, jumping,
// hovering, following another object's trail, sticking to the ground.
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 / 4 / 9 the empty 0x005480A0, 5 0x004E3F90 (slot 2 again), 6
// step_wait_state, 7 the base's Suspend, 8 / 10 the empty 0x005BAB90, 11 0x0042BB20 (the table entry's member function,
// else slot 12), 12 invoke_method_by_name. Its member functions are in the table at 0x00681EB8 (registered by
// 0x00423130; the member function pointers filled in by the dynamic initialiser 0x00640E30).
class KinematicsModule final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x0A015E17;

    // --- life ---
    // 0x00426650 (registered with the component types): a new one (memory::alloc_zeroed; the constructor inlined).
    static memory::owner<ObjectModule*> __cdecl fabricate();
    // 0x00424AA0: its type, no path follower or path group,
    // only the ground's height taken (+0x70), no ground offset; then Reset.
    KinematicsModule();
    // 0x00424AE0: the destructor body: the path follower deleted (its vtable), the turn's interpolator and the
    // follow trail freed, then the base's.
    ~KinematicsModule() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x00423D00: every motion setting back to rest (identity-like vectors, no flags, no speeds); +0x70 and the
    // pointers are kept.
    void get_reset();
    // 0x00423C90: a path follower for `owner` if there's none yet (memory::alloc_zeroed 0xE4).
    void init_path_follower(GameEntity* owner);

    // --- the vtable ---
    // 0x00426720 (slot 2): its node (Name, 0 if none) as both nodes; oriented to that node (0x00425970).
    void configure_from_data(script::Struct* params) override;
    // 0x004236C0 (slot 6): a script waiting on it (wait types 5-9) stops waiting when what it waits for is over: 5
    // moving (kMotionMoveMask), 7 the jump, 8 the path's speed down to 0 (not above 0), 9 turning
    // (kMotionRotateMask); 6 never.
    void step_wait_state(script::Script* script, uint32_t arg) override;
    // 0x004274D0 (slot 12): Obj_MoveToNode (0x8819DD8B) runs glide_to_node (0x00426C10), 1; anything else 2.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x004270E0 (slot 1): the frame's length (slow motion included); hovering (back to the base height, then the
    // bob), the axes' turns (0x00425C70), Obj_Rotate's turn (0x00424440), then moving to a point, along a path or
    // after a leader (the first that applies), the jump (0x00424870: the position it gives); the ground offset
    // eased in (384 per second squared towards it, the speed zeroed when it changes direction or overshoots);
    // objects with ids over 15 (not skaters) get their matrix as the display matrix.
    void tick_frame() override;

    // --- the update's parts ---
    // 0x00423740: the hover's bob for now: sin(2 pi * (the game clock mod the period) / period) * amplitude (the
    // clock's doubled count taken as unsigned), added to the height (the height without it kept in hover_base_).
    void update_hover();
    // 0x00424200: moving to a point - the speed accelerated (60 * frame * acceleration, to the maximum), the distance
    // covered; arrived (true): at the target, the arrival node told, no velocity; else on the line from the start
    // (that fraction of the way) with the velocity of the step. x87 roundings kept.
    bool update_move_to();
    // 0x00426E70: along the path (not for peds - those with a pedlogic component): the path's speed (0x00423530),
    // the follower moved that far (along the facing unless kMotion2NoPathHeading; at the end, no longer
    // following, speed 0); the facing from the path (kept level with kMotion2LevelHeading; the other rows rebuilt
    // unless sticking to the ground); the position the follower's (the height kept with kMotion2ConstantHeight;
    // with kMotion2StickToGround, kept and then stuck to the ground); the velocity of the step.
    void update_path();
    // 0x004246A0: after the leader: the trail's update and its position and facing (the height kept with
    // LeaveYUnaffected).
    void update_follow_leader();

    // --- paths ---
    // 0x00423FC0: along the path from `node`: out of the old path's group into the new one's (path manager), a
    // path follower if needed, started there.
    void start_path(uint32_t node);
    // 0x00424020: at the node `node`: it's the current node; its SpawnObjScript (with its Params) is spawned on the
    // object, else its switchobjscript becomes the object's script. (A missing node: the original crashes too.)
    void arrive_at_node(uint32_t node);
    // 0x00424180 (thiscall, RET 8; its result unused): on along a link of the node it's at (originalnode: its own
    // node) - a random one with kMotion2RandomLinks when there are several, else the first; none: nothing.
    void follow_path_linked(script::Struct* params, script::Script* script);
    // 0x004244C0: of the objects on its path (its group), the nearest one within `range` that faces the same way
    // and is ahead of it - and, if both follow paths, on the same segment (the same node, else the same last node).
    GameEntity* get_next_obj_on_path(float range);
    // 0x004246F0: Off: no longer following, the trail freed. Else following the object Name at `distance` (a new
    // trail if needed, emptied), LeaveYUnaffected, OrientY (the trail's free facing); the object's lock dropped
    // (its lockobj component: a crash without one).
    void trail_behind_lead(script::Struct* params);

    // --- turning (motion_rotate.cpp) ---
    // 0x00423F10: the axis' speed, maximum, acceleration and deceleration made positive - negative when its target
    // angle is.
    void set_rotation_direction(int axis);
    // The body of Obj_RotX / RotY / RotZ (three copies in the original): Angle (degrees; times `sign`) to turn
    // through - 0: that axis stops -, then deceleration (to speed: the speed to reach) or Acceleration (with speed: the
    // speed to reach; the speed now kept only while already turning) or just speed (degrees per second); with no
    // angle and no deceleration it turns for ever.
    void rotate_about_axis_by(script::Struct* params, int axis, float sign);
    // 0x004258B0: the orientation turned `angle` radians about its own axis 0-2 (rotation * matrix), that row kept
    // as the others are made orthonormal again; any other axis: nothing.
    void rotate_about_axis(int axis, float angle);
    // 0x00425A80: one frame of an axis' turn - accelerated (to the speed to reach, going its way), decelerated (to
    // it: done slowing, no turn this frame, finished when that speed is 0), then turned by speed * frame (only as
    // far as the target, when there is one: finished). Whether it's finished.
    bool update_axis_rotation(int axis);
    // 0x00425C70: each turning axis' frame; finished axes stop.
    void update_rotations();
    // 0x00425520: Obj_Rotate's turn from the orientation now to `target` over `time` seconds (0: at once) - its
    // interpolator made on first use (from the manager, set up first if needed).
    void start_rotation(script::Struct* params, const maths::Mat4* target);
    // 0x00424440: one frame of it (done at the end, or when there's no time).
    void update_turn();
    // 0x004240E0: axis `axis` set to turn towards `target` seen from `from` (the angle in the plane across that axis
    // from row `reference_axis`): true (turning) when the angle is beyond `threshold`.
    bool setup_look_axis(const maths::Vec4* target, const maths::Vec4* from, int reference_axis, int axis,
                         float threshold);
    // 0x004252E0: turn to look at `target`: instant (about y at once when beyond AngleThreshold degrees; true if
    // turned), else the axes 0xA4AECB20 (bits, default y) turned over `time`, or at `speed` (degrees per second,
    // default 360) with Acceleration - true if any turns.
    bool get_look_at(script::Struct* params, const maths::Vec4* target);
    // 0x00425710: look at the node of its node's link LinkNum (1 based, default 1; out of range: the first).
    void look_at_node_linked(script::Struct* params, script::Script* script);
    // 0x004257A0: look at the node Name (none: nothing).
    void look_at_node(script::Struct* params, script::Script* script);
    // 0x00425800: look at the unnamed vector (FLAG_MAX_COORDS: converted) - relative to its position unless
    // `absolute`.
    void look_at_position(script::Struct* params, bool absolute);
    // 0x00427500: look at the nearest object of Type (skater, ped, gameobj, Car), else the object of id Name in its
    // manager's list (not itself). False when there's none (or LookAt turns nothing).
    bool look_at_object(script::Struct* params, script::Script* script);
    // 0x00425970: the orientation from the node's angles (x, then y, then z: matrix * rotation each; none: the
    // identity), as the display matrix too.
    void face_node(script::Struct* node);

    // --- jumping, the ground, moving to points (motion_ground.cpp) ---
    // 0x00424870: one frame of the jump into jump_pos_: up by speed * frame + gravity * frame^2 / 2 (the speed
    // then changed by gravity * frame), along the heading at the start's speed; coming down, it lands at
    // land_height or on whatever a feeler from col_dist_above over to col_dist_below under finds (no longer
    // jumping - the position isn't snapped to it).
    void update_jump();
    // 0x00425F40: false without DistAbove / DistBelow. Height only (+0x70): the right axis rebuilt from up and at,
    // the ground between |DistAbove| feet over and |DistBelow| under found (get_ground_hit, the cached triangle first)
    // and the height to make up kept as the ground offset. Else three feelers (behind right, behind left, ahead -
    // 72 back or ahead, 30 to the side) with up taken as (0, 1, 0): with all three, the height half way between
    // the back's middle and the front, the right axis along the back points, at halfway to the direction from the
    // back to the front, up = at x right. Whether that was found.
    bool stick_to_ground();
    // 0x004269F0 (RET 0x1C: the target by value): moving to `target` - at once (no time or speed given: there now,
    // oriented to `node` with `orient`; true), over `time` seconds, or at `speed` (mph, fps or ips) reached by
    // Acceleration (the same units). False when it moves.
    bool move_to(script::Struct* params, script::Script* script, maths::Vec4 target, uint32_t node);
    // 0x00426C10: move_to the node Name (at once: arrived there; else the node told on arriving).
    void glide_to_node(script::Struct* params, script::Script* script);
    // 0x00426CB0: move_to the unnamed vector (FLAG_MAX_COORDS: converted) - relative to its position unless
    // `absolute`.
    void move_to_position(script::Struct* params, script::Script* script, bool absolute);
    // 0x00426D80: move_to its node's link LinkNum (1 based; else a random one with randomlink when there are
    // several, else the first).
    void move_to_link(script::Struct* params, script::Script* script);
    // The body of Obj_MoveForward / obj_move_left (two copies in the original): move_to `dist` along `direction`.
    void move_along(script::Struct* params, script::Script* script, const maths::Vec4& direction);

    // --- script member functions (the table at 0x00681EB8; all true) ---
    bool obj_store_pos(script::Struct* params, script::Script* script);  // 0x004237D0
    bool obj_store_node(script::Struct* params, script::Script* script); // 0x00423810 (the path's node)
    // 0x00423830: `off`: off; else on, the height = the unnamed float (if given).
    bool obj_set_constant_height(script::Struct* params, script::Script* script);
    // 0x00423890: `link` = a random link of its node, 1 based, into the script's parameters (none: nothing).
    bool obj_get_random_link(script::Struct* params, script::Script* script);
    // 0x004238D0: `off`: back to the height without the bob, not hovering. Else hovering with Amp (default as it
    // was) and Freq (per second, default 1): the period 1000 / Freq ms (truncated) varied by a random -10..+10 %
    // (at least 1 ms), from the height now.
    bool obj_hover(script::Struct* params, script::Script* script);
    // 0x004239F0: `Speed` into the script's parameters, in miles per hour (fps: feet per second; ips: inches per
    // second): moving to a point, that move's speed; locked to an object (an object locked by its lockobj), how far
    // it went this frame / the frame's length; else its velocity's length.
    bool obj_get_speed(script::Struct* params, script::Script* script);
    // 0x00423B30: a jump: speed (default 150) up, gravity (default -360), the start, col_dist_above / below (6, 6);
    // heading (a vector: its y scales the speed and is then 0) or use_current_heading (the display matrix's at)
    // moves it along too; land_height (where it lands, else the ground found).
    bool obj_jump(script::Struct* params, script::Script* script);
    // 0x00425150: `ob` (the id of get_next_obj_on_path within Range feet, default 100) into the script's parameters
    // (removed first; none found: not there).
    bool obj_get_next_obj_on_path(script::Struct* params, script::Script* script);
    // 0x004251C0: the unnamed float (feet unless `inches`) as the path follower's corner distance (a follower made
    // if needed); none given: nothing.
    bool obj_set_path_turn_dist(script::Struct* params, script::Script* script);
    bool obj_follow_path(script::Struct* params, script::Script* script);        // 0x00425220 (from the node Name)
    bool obj_follow_path_linked(script::Struct* params, script::Script* script); // 0x00425270
    bool obj_follow_path_stored(script::Struct* params, script::Script* script); // 0x00425290 (obj_store_node's)
    bool obj_follow_leader(script::Struct* params, script::Script* script);      // 0x004252C0
    // 0x00424B90 / 0x00424D50 / 0x00424F50: turn about x / y / z (rotate_about_axis_by; a look-at's turns stopped
    // first). FLAG_MAX_COORDS: x's angle negated; y and z swapped.
    bool obj_rot_x(script::Struct* params, script::Script* script);
    bool obj_rot_y(script::Struct* params, script::Script* script);
    bool obj_rot_z(script::Struct* params, script::Script* script);
    bool obj_rotate(script::Struct* params, script::Script* script);              // 0x00426770 (rotate_to)
    bool obj_look_at_node(script::Struct* params, script::Script* script);        // 0x00426790
    bool obj_look_at_node_linked(script::Struct* params, script::Script* script); // 0x004267B0
    bool obj_look_at_node_stored(script::Struct* params, script::Script* script); // 0x004267D0 (obj_store_node's)
    bool obj_look_at_pos(script::Struct* params, script::Script* script);         // 0x00426810 (absolute)
    bool obj_look_at_pos_stored(script::Struct* params, script::Script* script);  // 0x00426880 (Obj_StorePos's)
    bool obj_look_at_rel_pos(script::Struct* params, script::Script* script);     // 0x004268A0
    bool obj_look_at_object(script::Struct* params, script::Script* script);      // 0x004276A0 (look_at_object's)
    // 0x004268C0: DistAbove / DistBelow (feet); `off`: no longer sticking. Else sticking - false (and nothing
    // more) with both 0, else stuck now.
    bool obj_stick_to_ground(script::Struct* params, script::Script* script);
    // 0x00426960: at the unnamed node, oriented to it, its model brought up to date (a crash without one).
    bool obj_skip_to_restart(script::Struct* params, script::Script* script);
    bool obj_move_to_pos_stored(script::Struct* params, script::Script* script); // 0x004272D0 (Obj_StorePos's)
    bool obj_move_forward(script::Struct* params, script::Script* script);       // 0x00427320 (along at)
    bool obj_move_left(script::Struct* params, script::Script* script);          // 0x004273D0 (along right)
    bool obj_move_to_pos(script::Struct* params, script::Script* script);        // 0x00427470 (absolute)
    bool obj_move_to_rel_pos(script::Struct* params, script::Script* script);    // 0x00427490
    bool obj_move_to_link(script::Struct* params, script::Script* script);       // 0x004274B0

    uint32_t motion_flags_;          // +0x18 MOTION_*
    uint32_t motion_flags2_;         // +0x1C MOTION2_*
    float move_speed_;               // +0x20 moving to a point: the speed now (per second)
    float path_acceleration_;        // +0x24 Obj_SetPathAcceleration
    float path_deceleration_;        // +0x28 Obj_SetPathDeceleration
    float path_max_speed_;           // +0x2C Obj_SetPathVelocity
    float path_speed_;               // +0x30 along the path: the speed now
    float path_turn_distance_;       // +0x34
    float ground_above_;             // +0x38 Obj_StickToGround DistAbove (feet)
    float ground_below_;             // +0x3C DistBelow
    maths::Vec4 ground_triangle_[3]; // +0x40 the last ground triangle hit (0x00425CB0 tries it first)
    bool ground_height_only_; // +0x70 (constructor: true; cars: false) - false: three feelers turn it to the ground
    maths::Vec4 ground_triangles_[3][3];  // +0x74 the three feelers' last triangles
    ObjectGroup* path_group_;             // +0x104 the path's group of objects (path manager)
    maths::Vec4 move_target_;             // +0x108
    float move_distance_;                 // +0x118 from the start to the target
    float move_done_;                     // +0x11C how far it has gone
    float move_max_speed_;                // +0x120 with an acceleration: the speed to reach
    float move_acceleration_;             // +0x124
    uint32_t move_arrive_node_;           // +0x128 Obj_MoveToNode: the node told (0x00424020) on arriving (0: none)
    maths::Vec4 rot_target_;              // +0x12C per axis: the angle to turn through (radians; 0 forever)
    maths::Vec4 rot_done_;                // +0x13C per axis: turned so far
    maths::Vec4 rot_max_speed_;           // +0x14C per axis: the speed to reach (radians per second)
    maths::Vec4 spin_rates_;              // +0x15C per axis: the speed now
    maths::Vec4 rot_acceleration_;        // +0x16C
    maths::Vec4 rot_deceleration_;        // +0x17C
    maths::Vec4 move_start_;              // +0x18C
    float ground_speed_;                  // +0x19C the ground offset's speed of change
    float ground_offset_;                 // +0x1A0 how far the height has still to go to the ground
    float path_speed_1a_4_;               // +0x1A4 (the path's speed handling: 0x00423530)
    float path_speed_1a_8_;               // +0x1A8
    float path_min_stop_speed_;           // +0x1AC Obj_SetPathMinStopVel
    maths::Vec4 stored_pos_;              // +0x1B0 Obj_StorePos
    uint32_t saved_node_id_;              // +0x1C0 obj_store_node
    float turn_time_;                     // +0x1C4 Obj_Rotate's time (0: at once)
    float turn_elapsed_;                  // +0x1C8
    maths::ArcRotationBlender* turn_ptr_; // +0x1CC Obj_Rotate's interpolator (0x98 bytes)
    float hover_base_;                    // +0x1D0 the height without the bob
    float bob_now_;                       // +0x1D4 the bob now
    float bob_height_;                    // +0x1D8
    int bob_period_;                      // +0x1DC ms
    float step_seconds_;                  // +0x1E0 this frame's length (seconds, slow motion included)
    uint32_t own_node_name_;              // +0x1E4 its own node (Name)
    uint32_t list_link_;                  // +0x1E8 the node it's at
    memory::owner<PathFollower*> path_follower_; // +0x1EC
    FollowTrail* trail_;                  // +0x1F0 obj_follow_leader's
    bool leave_y_unaffected_;             // +0x1F4 obj_follow_leader's LeaveYUnaffected
    uint8_t pad1f5_[3];
    float jump_speed_;           // +0x1F8 up (per second)
    float leap_pull_;            // +0x1FC
    maths::Vec4 leap_direction_; // +0x200 along (x, z; w kept)
    bool jump_has_heading_;      // +0x210
    maths::Vec4 jump_pos_;       // +0x214 the jump's position this frame
    float jump_start_speed_;     // +0x224
    float jump_col_above_;       // +0x228 col_dist_above
    float jump_col_below_;       // +0x22C col_dist_below
    uint32_t unknown230_;
    bool jump_has_land_height_; // +0x234
    float leap_touchdown_y_;    // +0x238
    maths::Vec4 jump_start_;    // +0x23C

    // 0x00425600 (thiscall; called by its member function for Obj_Rotate, 0x00426770): turn the object to an
    // orientation over time - `relative = (x, y, z)`: the object's orientation now turned by those angles about its
    // own axes (maths::Mat4::turn_in_own_frame); `absolute = (x, y, z)`: the orientation of those angles
    // (maths::Mat4::set_from_euler_angles). Degrees; with FLAG_MAX_COORDS (0x26AF9DC9) the vector is first converted
    // (z negated, then negate_x_rotate_x90). Neither given: nothing. The turn is started by 0x00425520 (`time` from
    // the params).
    void rotate_to(script::Struct* params);
};

// 0x00425CB0: the line from `start` to `end` against the cached triangle (three corners) - hit: the point. Else a feeler along
// it: hit, the face it hit becomes the cached triangle and the point is the feeler's. Whether anything was hit.
bool __cdecl get_ground_hit(maths::Vec4* triangle, const maths::Vec4* end, const maths::Vec4* start, maths::Vec4* out);

} // namespace objects
