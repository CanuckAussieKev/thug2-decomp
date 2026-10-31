#pragma once

#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"

namespace render {
class ProbeRay;
}
namespace script {
class Script;
class Struct;
} // namespace Script
namespace maths {
class Mat4;
}
namespace objects {
struct GrindPoint;
struct Record24;
struct Record34;
} // namespace Obj

// The Walk component (type 0x726E85AA, vtable 0x0064C75C; made by 0x0052A5C0, its member function table 0x006847F8
// registered by 0x00525E40, 0x1960 bytes): the skater on foot - walking, running, jumping, hanging, climbing. Its code
// is in walk_physics.cpp, walk_movement.cpp and walk_collision.cpp (0x00524E10-0x00529F62) and walk_moves.cpp
// (0x00529F80-0x0052D6A0); its layout is only partly known, so fields are reached by offset (walk_fields.h names
// them for the first three). Its tuning is the global structure walk_parameters.
//
// Vtable: 0 the scalar deleting destructor (0x0052A290), 1 Update (0x0052D4B0), 2 configure_from_data, 3 Finalize.
namespace objects {

class FootMovement : public ObjectModule {
public:
    // Its states (+0x18; the names are the table at 0x0064C618): 0 WALKING_GROUND, 1 WALKING_AIR, 2 / 3 / 4 the
    // hanging and climbing ones.

    // --- life (walk_physics.cpp) ---
    // 0x00525E40: its member function table (0x006847F8) registered.
    static void __cdecl publish_method_table();
    // 0x005288B0: the destructor body: its smart pointer (+0x184C) let go, its collision cache (+0x790) and its
    // eight contacts (+0xA0, 0xCC each, the last first) destroyed, then the base's.
    void destroy_body();
    // 0x00526060 (slot 2): its camera from camera= (an object id: the object and its WalkCamera component).
    void configure_from_data(script::Struct* params);
    // 0x00525FB0 (slot 3): the object's Input, animblender, Model, Trigger, SkaterPhysicsControl, MovableContact,
    // SkaterState, SkaterCorePhysics and TaggingControl components.
    void seal();
    // 0x00525E60: its camera object and that object's WalkCamera component.
    void choose_camera(GameEntity* camera);
    // 0x005260E0: the state changed (the previous one kept, the time it changed): what leaving the air, landing,
    // hanging or climbing resets; entering a hang from the air debounces the pad's hang controls
    // (hang_control_debounce_time).
    void change_phase(int state);
    // 0x005260C0: the model's offset (+0x1F8) back to none.
    void clear_model_offset();

    // --- small queries ---
    // 0x00524E10: the d-pad steers it (the left stick centred - the input's +0x228 / +0x22C exactly 0 - and the
    // flag +0x98 set).
    bool using_dpad();
    // 0x00524E60: the camera's offset: its facing (+0x48) x -18 in states 2-4, and in the air for 150 ms after
    // leaving states 2 / 3; else (0, 0, 0, 1). Returns `out`.
    maths::Vec4* get_camera_offset(maths::Vec4* out);
    // 0x00528560: its facing. Returns `out`.
    maths::Vec4* get_facing(maths::Vec4* out);
    // 0x00528220: the flat direction `index` sevenths of a turn from its facing. Returns `out`.
    maths::Vec4* get_turned_facing(maths::Vec4* out, int index);
    // 0x00528370: the run adjustment (+0x1868): 1 while the score holds a combo, else down at run_adjust_rate a
    // second to 0.
    void update_run_adjustment();
    // 0x00528400: on the ground and 100 ms after landing: the speed kept from rising past the last frame's
    // (+0x704 / +0x708); a stopped run becomes `stop_anim` if that's Skid, else Stand.
    void clamp_skater_speed(uint32_t stop_anim);
    // 0x005284C0 (`this` isn't used): gravity (walk_parameters); with the moon cheat on, half of it (the cheat
    // told, the global 0x007CE4FD set).
    static double __cdecl get_gravity();
    // 0x00528280 (stdcall; `this` isn't used): the ground below `position`: `feeler` from feeler_height x 2 above
    // to stand_pos_search_depth below; its point (true) or `position` (false) into `out`.
    static bool __stdcall find_stand_position(const maths::Vec4* position, maths::Vec4* out, render::ProbeRay* feeler);

    // --- movement (walk_movement.cpp) ---
    // 0x00526260: from wall_turn_speed_threshold up, the facing turned toward the velocity's direction
    // (wall_turn_factor a second at max_wall_turn_speed_threshold and above, at once past a whole turn's rate).
    void turn_facing_to_velocity();
    // 0x00526430 (returns unrounded in ST0): `speed` on a steep slope (the ground normal's y at most 0.95) when
    // faster than walk_speed: slowed by how much `direction` runs along the slope.
    double adjust_speed_for_slope(float speed, const maths::Vec4* direction);
    // 0x00527420: the up vector (+0x58) eased back upright at lerp_upright_rate (when +0x72C is 0); snapped when
    // nearly there.
    void update_upright();
    // 0x00527720: a facing to turn to at `rate` (+0x191C / +0x192C) and the turn to it (+0x1930, the shorter way).
    void set_transition_facing(const maths::Vec4* facing, float rate);
    // 0x005277B0: its collision cache refilled for the box around it (feeler_length + 1 across, walker_height + 1
    // above, snap_down_height + 1 below) and made the feelers' current one.
    void refresh_hit_cache();
    // 0x00527BE0: the display offset (+0x9C) eased back to 0 at display_offset_restore_rate (not while hanging, 2,
    // or in state 4).
    void restore_display_offset();
    // 0x00527C50: pushed back off the plane it hit (+0x18B4, its distance +0x18C4) within push_feeler_length, at
    // push_strength, the velocity into it taken away; ended above +0x18C8.
    void update_push();
    // 0x00527DB0: its position, velocity (flat, the vertical part apart), up and facing (flat, unit) from the object.
    void copy_from_object();
    // 0x00527F20: the control from the pad: the direction (the camera's forward turned by the stick's angle) and
    // the push (the stick's, or the d-pad's: walk_point on the ground without X, else full).
    void update_control();
    // 0x00528120 (returns unrounded in ST0): the speed the push asks for: up to walk_speed at walk_point, then up
    // to the run speed (0x0043DAA0); +0x17A4 says which.
    double get_control_speed();

    // 0x00526850: in the air, once the jump's delay (+0x1898) has run out: the velocity (less what the jump carries
    // along, +0x1888) eased toward the push across the jump's direction and along it (jump_adjust_speed,
    // jump_accel_factor; across: nothing when unsteered, +0x1958).
    void steer_jump();
    // 0x00528950: on the ground: the facing turned toward the push (in place past slop_lock_angle, against walls
    // slower), and `velocity` (in and out) made the way it goes at the speed eased toward what the push asks for.
    void update_ground_velocity(maths::Vec4* velocity);
    // 0x00529800: while +0x72C runs down, the object's matrix turned about +0x734 by its share of +0x730 over
    // rotate_upright_duration.
    void update_rotate_upright();
    // 0x005298D0: the object put at the transition's point (+0x1904 and the climb offset), upright facing its
    // facing; the display offset from +0x1918; then the transition's member function (+0x1948), if any.
    void apply_transition();
    // 0x00529CE0: the object's position, orientation (from the up vector and the facing tilted back by +0x744),
    // velocity (by state) and the model's offset from the component.
    void copy_to_object();

    // --- collision (walk_collision.cpp) ---
    // 0x005264F0: the curb float: a feeler ahead along the ground (curb_float_feeler_length) and down onto what it
    // meets from snap_up_height: the offset (+0x714) eased toward that height (curb_float_lerp_up_rate).
    void update_curb_float();
    // 0x00526BA0: in the air (not pushed off a wall): a ceiling over the head - the velocity into it taken away,
    // moved back down out of it, the object's 0x6E84ACF3 event.
    void check_ceiling();
    // 0x00526E00: X and the pad's +0x20 button just pressed against a wall ahead, high enough and not too soon:
    // walking_wallplant_switch ("walking sticker slap").
    void check_sticker_slap();
    // 0x00527080: the ground below the critical point: its height (the skater state's +0x11C), the shadow, the
    // model's shade; nothing below at all: "uber frig" - back where the frame began, turned around, on the ground.
    void update_ground_below();
    // 0x005278D0: the critical point's offset (+0x71C) eased out at 1000 a second; what its path went through
    // moves the position back off it ("adjusting position due to bad critical point").
    void ease_critical_point_offset();
    // 0x00529160: a ledge over the head (jump_obstruction_check_height) with room beyond it
    // (jump_obstruction_check_back): the jump's velocity and time to land on it.
    void plan_jump_over_obstruction();
    // 0x005293F0: X in the air against a wall (a contact) - off it: up by the hold of X, away mirrored off the wall;
    // the trigger and trick objects told; Wallplant.
    void try_wallplant();
    // 0x005299F0: a rail along the move (the level's, or one riding an object) the core physics allows and nothing
    // in the way to: grind it (+0x748 = 1, rail). True if so.
    bool try_grind();

    // --- member functions (by their records) ---
    bool walk_ground(script::Struct* params, script::Script* script);              // 0x00525E90
    bool walk_scale_anim_speed(script::Struct* params, script::Script* script);      // 0x00525EA0
    bool walk_release_hang(script::Struct* params, script::Script* script);         // 0x00525F60
    bool walk_get_state_time(script::Struct* params, script::Script* script);        // 0x00528580
    bool walk_get_speed_scale(script::Struct* params, script::Script* script);       // 0x005285C0
    bool walk_get_hang_init_anim_type(script::Struct* params, script::Script* script); // 0x005286A0
    bool walk_get_state_duration(script::Struct* params, script::Script* script);    // 0x005286C0
    bool walk_get_previous_state(script::Struct* params, script::Script* script);    // 0x00528710
    bool walk_get_state(script::Struct* params, script::Script* script);            // 0x00528740
    bool walk_get_hang_angle(script::Struct* params, script::Script* script);        // 0x00528770
    bool walk_set_forward_control_lock(script::Struct* params, script::Script* script); // 0x00528860

    // --- walk_moves.cpp (skater-6: 0x00529F80-0x0052D6A0) ---
    // 0x0052A5C0: a new one (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // 0x0052A190: the constructor: the base's, its vtable and type, the eight contacts (0xCC each at +0xA0), the
    // collision cache (+0x790), the trigger feeler (+0x17B4), no components yet, no control (0, 0, 0, 1), no extra
    // velocity (0, 0, 0, 1). Returns this.
    FootMovement* make_new();
    // 0x0052A290 (vtable slot 0): the scalar deleting destructor (the body is 0x005288B0).
    FootMovement* teardown(unsigned int flags);
    // 0x0052D4B0 (vtable slot 1): one frame: the animation state (+0x78, the previous kept at +0x7C) started afresh,
    // the frame's length, the step smoothing's height taken off, then the state's update (0 update_ground, 1
    // update_air, 2 hanging 0x00440BF0, 3 on a ladder 0x004425B0, 4 update_anim_wait); unless the walk was halted,
    // the step smoothing eased down and put back, the state's checks (an acid drop after half a second on the
    // ground), the animation state sent to the object as an event, the animation speed and the camera.
    void tick_frame();
    // 0x0052D300 (state 0): riding, the contacts, moved by the velocity and onto the ground; inside a moving object
    // it takes that object's velocity.
    void update_ground();
    // 0x0052CEF0 (state 1): moved by the velocity, the extra velocity (+0x189C) and gravity; ledges and ladders
    // grabbed; inside a moving object it is put back and stopped, standing on it.
    void update_air();
    // 0x0052AD10 (state 4: ANIMWAIT): placed along an animation - between two points (+0x18E4, +0x1904) by how far
    // the animation is, turned toward +0x191C, the height +0x9C eased - and carried by what it rides on.
    void update_anim_wait();
    // 0x0052A700: the air's move from `from` to `to` against the world. Into a wall (face flag 0x2): 0.1 off it and
    // the rest of the move slid along it (0.05 off anything that meets). Onto anything else: landed - on the ground
    // (its triggers set off, what it stands on told), the velocity kept only along the facing and relative to what
    // it landed on (stopped when heading away, or slow without control), the Land animation.
    void collide_air_move(const maths::Vec4* from, const maths::Vec4* to);
    // 0x0052B370: onto the ground under it (snap_up_height above to snap_down_height below): big steps told to the
    // object, the step smoothing, the ground's triggers when it changes. None (or a wall): off the edge into the air
    // (SkaterOffEdge) unless a hang, a drop or a ladder takes over.
    void settle_onto_floor();
    // 0x0052AF40: walking begins (from skating): on the ground (`onGround`) or in the air - then thrown along the
    // turn under way off a vert ramp, the heading from the velocity (or the object's axes), off what it rode.
    void get_begin(bool on_ground);
    // 0x0052B7D0: a jump (also off a hang: release_hang): up at jump_velocity times how long jump was held (or
    // hang_jump_factor off a hang or ladder); running, pushed along the control's direction; off what it rode, in the
    // air, SkaterJump.
    void get_jump();
    // 0x0052BC70: a jump over the barrier ahead (barrier_jump_*): when a contact ahead, faced head-on, has nothing
    // in the way at the highest barrier's height - up fast enough to clear the barrier's top. Whether it jumped.
    bool try_barrier_jump();
    // 0x0052C2C0: the eight contacts (feelers around it, and along the fall in the air) found again; on the ground a
    // barrier jump after barrier_jump_delay, a jump, the push down steep ground; pushed out of what is close, the
    // velocity slid along what it walks into (stopped in a corner or when turned back), the run animation's speed
    // kept up. False when it touches nothing it moves into (or jumped).
    bool update_contacts();
    // 0x00529F80: the walk camera's override facing for the animation state (+0x78): along the hang (turned 0.95
    // radians for moving left or right), the facing on a ladder or while waiting for an animation; else none.
    void update_camera_override();
    // 0x0052A2C0: the animation speed for the speed mode (+0x700): 1 the run (speed over +0x17B0), 2 hanging and
    // 3 ladder moves (over walk_parameters' hang_move_animation_speed / ladder_move_speed - the ladder's also sent
    // over the network when it changed by 0.1 or more).
    void update_anim_speed();
    // 0x0052A620: carried by what it stands on (its ride tracker at the ride component's +0x18): the move added to
    // the position; a turn also turns the facing (kept level).
    void update_ride();
    // 0x0052AB90: off what it stood on: that object's velocity (+0x1888) added to `velocity` (and its y to
    // `vertical`), the position moved on by it for the frame when heading the same way; the ride tracker let go.
    void leave_ride(maths::Vec4* velocity, float* vertical);
    // 0x0052B2D0: into the air: state 1, the heading (+0x1878) the object's velocity flattened, the ride left,
    // +0x1888 the velocity flattened.
    void go_to_air();
    // 0x0052A180 (Walk_AnimWaitComplete), 0x0052C280 (release_hang), 0x0052C290 (Walk_ForceToAirFromHang: into the air
    // and the object's AIR event): member functions; 1 (true). 0x0052C2B0: go_to_air.
    int clip_finished_waiting(script::Struct* params, script::Script* script);
    int release_hang(script::Struct* params, script::Script* script);
    int force_to_air_from_hang(script::Struct* params, script::Script* script);
    void go_to_air_for(void* unused);

    // --- walk_hang.cpp (objects-5: 0x0043DA70-0x004425B0, WalkHangUtil.cpp in the original) ---
    // Hanging: the skater holds a rail (the rail manager's node pair +0x1764 / +0x1768, the fraction along it
    // +0x18CC) and can shimmy along it, pull up onto the ledge or drop; a rail is grabbed from a drop or a jump
    // (find_rail with the walk component as the checker). Ladders are rail-manager nodes too (climbing nodes): climbed
    // along +0x18CC, onto the top or off the bottom. While an animation plays (+0x1944 set) a callback waits at
    // +0x1948 for its end - one of the small functions here.

    // 0x0043DA70 (cdecl): a walk parameter (the global structure walk_parameters' float `name`).
    static float __cdecl get_param(uint32_t name);
    // 0x0043DAA0: the running speed: run_speed to combo_run_speed by the blend at +0x1868 (unrounded, as the
    // original leaves it in ST0).
    double run_speed();
    // 0x0043DC00: how far below the rail the skater's origin hangs on a ladder-like rail: (1 - its +0x20) *
    // hang_vert_origin_offset when the node at +0x1778's +0x208 has flag 8, else 0 (unrounded).
    double hang_height_offset();
    // 0x0043DC60: the hang's critical point above the origin: hang_critical_point_vert_offset - hang_vert_origin_offset
    // * max_cas_scaling (unrounded).
    double critical_point_up();
    // The animation-end callbacks: 0x0043DB00 (pulled up onto a ledge: walking again, the feeler's hit given to the
    // triggers and the movable contact), 0x0043DB90 (dropped from a hang: in the air, its facing kept), 0x0043DCF0
    // (dropped onto a rail: hanging - the offset, the rail script, tagging allowed again).
    void end_pull_up();
    void end_drop_from_hang();
    void end_drop_to_hang();
    // 0x0043DDD0: whether a hanging skater's side is clear: `feeler` from the critical point (`hang`: the point and
    // the direction out from the wall, +0x10) moved out along `side` (hang_obstruction_feeler_side), then up by
    // hang_obstruction_feeler_up - and back the other way. True if neither hits.
    bool side_clear(render::ProbeRay* feeler, const maths::Vec4* hang, const maths::Vec4* side);
    // 0x0043DFC0: hanging on the rail +0x1764 / +0x1768 `distance` from its start (`fromEnd`: its end), facing away
    // from it - the side `direction` says; turning round more than 60 degrees flushes the walk camera (+0x1760). The hang
    // offset (+0x71C) from the new facing.
    void face_hang_rail(float distance, bool from_end, const maths::Vec4* direction);
    // 0x0043E230: the rail joining `start` / `end` (the segment the skater is at, `*t` along it): past the start
    // (t < 0) the segment before it, else the one after `end` - if switched on and grabbable, else a joining rail
    // (find_joining_rail, without flags 8 / 0x20) whose direction is close enough (a dot product of at least -0.866,
    // or it's refused: "BAD ANGLE ON COINCIDENT RAIL"). Its nodes into `new_start` / `new_end`, `*t` flipped when it
    // runs the other way. Whether there's one.
    bool next_hang_rail(float* t, GrindPoint* start, GrindPoint* end, GrindPoint** new_start, GrindPoint** new_end);
    // 0x0043E530: a pull-up onto the ledge above the hang: the ground near `target` (find_stand_position; failing that, 12 to
    // one side, then 12 to the other - `target` moved), seven feelers round it (none may hit) and one from behind the
    // skater up past the pull_up_obstruction_height (neither may hit). Then the animated move (PullUpFromHang) towards
    // it, ending walking (the ledge's feeler kept for the triggers) or, with no ground found, in the air. Whether it
    // starts.
    bool pull_up(maths::Vec4* target);
    // 0x0043EC40: a drop from walking to hang on a rail below (not while +0x1528's +0x70, not faster than
    // drop_to_hang_speed_factor x the running speed, not while tagging): find_rail along the move from the last
    // position, facing away (the skate module's rails, else each moving rail manager's, in its object's space - riding
    // that object), within Drop_To_Climb_Max_Snap. Then the animated move (DropToHang) onto it.
    bool drop_to_hang();
    // 0x0043F030: grabbing a rail between `start` and `end` (raised by hang_vert_origin_offset) while falling no faster
    // than 50: find_rail within Climb_Max_Snap, as DropToHang; then hanging at once (the rail script, the skater core's
    // +0x1528 flag +0xA0 set).
    bool grab_rail(maths::Vec4 start, maths::Vec4 end);
    // 0x0043F3A0: whether shimmying along the hang is blocked: feelers from the hang point `point` (up, out of the wall
    // by `out`) sideways along the move (`move`, `left` or not), at three heights and down at the critical point -
    // near the segment's end (`segment`, `start` / `end`, `along` it) turned onto the next segment first. True if any
    // hits.
    bool shimmy_blocked(const maths::Vec4* out, float along, const maths::Vec4* point, bool left, const maths::Vec4* move,
                       maths::Vec4 segment, GrindPoint* start, GrindPoint* end);
    // 0x0043FC70: the position on the hang: the rail's point along +0x18CC, raised by +0x18D0 and out along the
    // facing by +0x18D4. Riding something that turns (the ride tracker's +0xAC), the facing set across the rail
    // again, flat (the side it was on; its w the side's sign - the original's).
    void place_on_hang();
    // 0x0043FE30: whether hanging at `point` on the rail find_rail found (`rail`, its 0x34-byte record: the point
    // set, its facing out from the wall) has room: both sides clear (side_clear with `feeler`), shimmying neither way
    // blocked (from the hang point: lowered by hang_vert_origin_offset, out by hang_horiz_origin_offset), and a
    // feeler from the position to the critical point clear - or, on the ground, up by hop_obstruction_feeler_up
    // and over from there.
    bool hang_clear(render::ProbeRay* feeler, const maths::Vec4* point, Record34* rail);
    // 0x00440130: shimmying `move` along the hang (left for more than 0): unless blocked (shimmy_blocked; the Hang
    // animation), on along the rail - past an end onto the next rail (next_hang_rail; face_hang_rail the rest of the
    // way) or stopped there; HangMoveLeft / HangMoveRight. A feeler from the old critical point to the new one: if
    // it hits, all put back (the Hang animation).
    void shimmy(float move);
    // 0x00440690: find_rail's checker for a hang: the rail segment `start`-`end` (`onRail` on it closest to the line,
    // `onLine` on the line, `along` it). Not too steep (hang_max_rail_ascent), not above the line when in the air;
    // the facing across it towards the skater's facing (`away`: from it), turned by the rail's one-sided flags
    // (0x40 / 0x80); room to hang (hang_clear) facing that way or, if the flags allow, the other. Into `result`: the
    // facing, how far along, out and up from the rail, and the animation (SWING, or WALL when a feeler out from the
    // hang point hits).
    bool check_rail(const maths::Vec4* start, const maths::Vec4* end, const maths::Vec4* on_rail, const maths::Vec4* on_line,
                   float along, Record34* result, bool away);
    // 0x00440BF0 (state 2): hanging, each frame: carried by what it rides on (else the ride let go); with the control
    // pushed forward a pull-up (twice: pull_up_offset_forward ahead, then half that); turned round: let go (in the
    // air, SkaterExitHang); else shimmied by the control's sideways part (hang_move_speed, eased by
    // hang_move_lerp_rate, below hang_move_cutoff the Hang animation).
    void update_hang();

    // Ladders (the rail manager's climbing nodes; the skater's node +0x1764 and its other end, how far up +0x18CC).
    // The animation-end callbacks: 0x00441020 / 0x00441070 (off the bottom / the top onto the ground: walking, the
    // ledge feeler's hit to the triggers and the movable contact), 0x00441860 (off the top with no ground: in the
    // air), 0x004410C0 / 0x00441190 (onto it at the bottom / the top: on the ladder, ladder_bottom_offset_up up /
    // ladder_top_offset_up down from the top).
    void end_ladder_off_bottom();
    void end_ladder_off_top();
    void end_ladder_off_top_to_air();
    void end_onto_ladder_bottom();
    void end_onto_ladder_top();
    // find_ladder's checkers (into `record`, a 0x24-byte record: the point to climb from, the facing - the ladder's
    // orientation's third row - and how far along). The ladder (`bottom` to `top`) must stand within about 6
    // degrees of upright. 0x00441260 (`this` isn't used): onto it from the top - ladder_top_offset_up below the top,
    // ladder_climb_offset out of it. 0x004418B0: onto it from the bottom - faced within max_onto_ladder_angle,
    // ladder_bottom_offset_up above the bottom, ladder_climb_offset out, nothing in the way to 6 behind that.
    // 0x004413E0 (find_ladder_on_line's): grabbed where the line meets it (`on_ladder`, `along` it), between the two
    // offsets, nothing in the way from the skater; the skater core's flag +0xA0 of +0x1528 set.
    bool check_ladder_top(const maths::Vec4* bottom, const maths::Vec4* top, const maths::Mat4* orientation,
                        Record24* record);
    bool check_ladder_bottom(const maths::Vec4* bottom, const maths::Vec4* top, const maths::Mat4* orientation,
                           Record24* record);
    bool check_ladder_line(const maths::Vec4* bottom, const maths::Vec4* top, const maths::Mat4* orientation,
                         const maths::Vec4* on_ladder, float along, Record24* record);
    // 0x00441680 / 0x00441B80: off the ladder's bottom (ladder_bottom_offset_forward back, down) / top
    // (ladder_top_offset_forward ahead, up): the ground there (find_stand_position), the animated move
    // (LadderOffBottom / LadderOffTop) - off the top with no ground, into the air at its end.
    void climb_off_bottom();
    void climb_off_top();
    // 0x00441DE0 / 0x00441FC0: onto a ladder of the level's from its bottom (within button_horiz_snap_distance when
    // `button`, else max_horiz_snap_distance; max_vert_snap_distance) / its top (find_ladder): stopped, the ride let
    // go, the animated move (LadderOntoBottom / LadderOntoTop). Not while the skater core's +0x1528's +0x70.
    // Whether it started.
    bool climb_onto_bottom(bool button);
    bool climb_onto_top();
    // 0x00442170: a ladder of the level's grabbed where the line `start`-`end` passes it (find_ladder_on_line): on it at
    // once (the ride let go, stopped, the Ladder animation). Whether one was.
    bool grab_ladder(const maths::Vec4* start, const maths::Vec4* end);
    // 0x00442280: climbing by the control's up / down part (ladder_move_speed): off the bottom or the top past
    // their offsets, else placed along it (LadderMoveUp / LadderMoveDown; Ladder at an end).
    void climb_ladder();
    // 0x004425B0 (state 3): on a ladder, each frame: its rail script for the first 25 ticks; climbing with the
    // control within ladder_control_tolerance of up or down, else still (Ladder).
    void update_ladder();
};

// One of the Walk component's eight contacts (+0xA0, 0xCC bytes): a feeler at +0x1C. 0x00528880: constructed (its
// feeler's). Returns this. 0x00528890: the destructor (the feeler's object let go).
struct WalkContact {
    WalkContact* make_new();
    void teardown();
};

} // namespace Obj
