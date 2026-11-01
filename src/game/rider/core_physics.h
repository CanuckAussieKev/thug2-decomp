#pragma once

#include <cstdint>

#include "engine/core/bits_as.h"
#include "engine/maths/mat4.h"
#include "engine/maths/rotation_blend.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "engine/render/coll_cache.h"
#include "engine/render/probe_ray.h"

namespace script {
class Struct;
class Script;
} // namespace script
namespace objects {
struct GrindPoint;
class GrindRailHub;
class PadInputModule;
class ActivationZoneModule;
class RiderSfxPlayer;
class MoveExecutor;
class RiderTurning;
class RiderPointKeeper;
class RiderTeeterMoves;
class RiderPoseState;
class MovingSurfaceModule;
class RiderBoardPhysics;
class FootMovement;
class TaggingControlComponent;
} // namespace objects

// The skater's core physics component (on the ground, in the air): the pieces PartyMod changes for its
// PS2-style controls (thug2-decomp.ini [Miscellaneous] UsePS2Controls - Skater::ps2_controls()).
namespace skater {

using memory::bits_as;

// What the acid drop search (SkaterPhysicsModule::find_acid_drop) finds (0x28 bytes).
struct AcidDrop {
    maths::Vec4 point;  // +0x00 where on the wall the skater drops to
    maths::Vec4 normal; // +0x10 the wall's normal there
    float hit_height_;  // +0x20 the height the search first hit it at
    bool object_hit_;   // +0x24 (the feeler's hit object was still there)
    uint8_t pad25[3];
};

class SkaterPhysicsModule final : public objects::ObjectModule {
public:
    // --- rotation.cpp: turning in the air ---
    // 0x00504060: spin in the air. Tap mode (+0x2DA): each tap of the left / right spin button (L1/R1: input
    // +0xA0 / +0x100) queues half a turn (+0x30C), turned off at the skater's spin speed. Otherwise holding one
    // turns once it's been held longer than the spin delay - PartyMod's PS2 controls drop the delay (its NOPs
    // at 0x00504239 / 0x0050430B); then the second pair of spin buttons (input +0x60 / +0x80), ramping up
    // between two held times. With no spin, the
    // skater may be turned back to face along the ramp. The turn goes into the object's matrix, the display
    // matrix and the trick's rotation total (in degrees, shown as an integer).
    void get_spin();
    // 0x00507120: break out of vert - both of two buttons held (input +0xC0 / +0x120, PartyMod's "R2/L2"; its
    // PS2 controls take either: its bytes at 0x00507184 / 0x005071AE), or a flag (+0x2D0), or `force`, or the
    // jump button held past a script time with no other button: pushes the skater away from the wall and
    // pitches them forward.
    void break_vert(bool force);

    // --- core_physics.cpp ---
    // 0x004FCBA0 (a script member function: neither argument is used): push off out of a lip trick. The
    // skater's facing, flattened (x, z of the object's matrix row at +0x8C, normalised), picks the push:
    // both triggers (+0xC0 and +0x120 of the input; either one under PartyMod's PS2 controls) or button +0x60
    // held long enough - along the facing; +0x20 or +0x40 held long enough - sideways one way or the other.
    // Button +0x80 held, or nothing pushed: returns 0 and leaves the velocity alone. 1 otherwise.
    int lip_push_off(script::Struct* params, script::Script* script);

    // 0x004FCE90 (the script member function SetSpeed): the skater's speed set to the unnamed float (0 without one)
    // - the velocity's direction kept; with almost none (its length squared below 0.001), the facing's instead
    // (the object's matrix row at +0x8C, all four components). The fourth component is scaled with the rest. 1.
    // Not original: [features] debug_rail_speed=1 logs the calls of the rail speed-up loops (the THAW ports'
    // RailAccelerate2, the level editor's ed_RailSpeedLoop) - when one starts, then once a second.
    int set_speed(script::Struct* params, script::Script* script);

    // 0x00509BD0: keep to the ground while skating. A feeler along the skater's up axis looks for the ground
    // below; on a hit the skater sticks to it (unless it's too steep or too far for a hit on a wall), otherwise
    // it leaves the ground - into the air, or into an acid drop if the physics finds one while both triggers
    // are held (either one under PartyMod's PS2 controls).
    void check_for_ground();
    // Not original (THAW's BMX, game/rider/thaw_bmx.h): check_for_ground's on-the-ground part for the bike on its
    // wheels - the display normal, then with `hit` the terrain, the ride, the triggers, the trick objects.
    void ground_under_wheels(bool hit);

    // 0x0050CA00: the air update. The rotation and other parts (0x00504910, Spin, ...), the move (velocity and
    // gravity over the frame, plus what the skater rides on), wall riding (off the wall: break_vert) and wall
    // feelers, then the landing feeler from the last position to the new one: onto something skatable
    // (land_from_air) or pushed off it; else the air checks and an acid drop if the physics finds one while both
    // triggers are held. PartyMod's PS2 controls take either trigger in the three trigger checks.
    void do_air_physics();

    // --- core_jump.cpp: jumping, skitching, the script member functions around them, the wallplant state ---
    // 0x005092E0: the TriggerScript of the rail node being ground (+0x22C) - or of the nearest one before it (back
    // along last_value_, stopping at a loop) that has one - run for the three `events` (const int[3]); its name kept
    // (+0x240 / +0x244), the flag +0xC8 telling whether it's a different node than last time. Nothing without a
    // rail node, its script node or a TriggerScript.
    void trigger_rail_script(const int* events);
    // 0x00509450: jump, by the state: from the ground (the trigger check first: a trigger's script may take over),
    // from the air or a rail - the jump sound unless `params` has no_sound. The speed from the physics values
    // (boneless or not, from vert or not) between their min and max by how long the jump was held (+0x378, up to
    // the max tense time) - or `params`' speed; off vert pushed off the ramp instead (going down: stopped first);
    // then into the air (+0x4EC the time, the event SkaterJump).
    void perform_leap(script::Struct* params);
    // 0x00509920 / 0x00509990: start skitching on the object at +0x364 (riding it, flag +0x88, the event
    // 0x35224F25 to it) / stop (the event 0x2739B86D, the pointer and the ride let go).
    void begin_skitch();
    void end_skitch();
    // 0x00509050: the wallplant state's update: after Physics_Wallplant_Duration into the air (a feeler along the
    // facing for a trigger), else carried along by what the skater rides on.
    void step_wallplant();
    // The script member functions (all return 1; neither argument used unless said): 0x00509A40 Jump (perform_leap with
    // `params`), 0x00509A60 StartSkitch, 0x00509A70 end_tow_ride, 0x00509A80 change_phase (`params`' unnamed checksum:
    // AIR, Ground or Lip; off a lip runs the rail's TriggerScript, wall riding the up set straight), 0x00509B70
    // TriggerGrindOff (the rail's TriggerScript for grinding off), 0x00509B90 (0x675CF35E: break out of vert, forced,
    // and 0x00504DF0), 0x00509BB0 WalkingWallplant (0x00507500 with 0, 0, 1).
    int get_jump(script::Struct* params, script::Script* script);
    int start_skitch(script::Struct* params, script::Script* script);
    int end_tow_ride(script::Struct* params, script::Script* script);
    int change_phase(script::Struct* params, script::Script* script);
    int trigger_grind_off(script::Struct* params, script::Script* script);
    int force_break_vert(script::Struct* params, script::Script* script);
    int walking_wallplant(script::Struct* params, script::Script* script);

    // --- core_collision.cpp: the skater against walls ahead and to the sides ---
    // 0x00508780 (from the air update): a feeler from `from` to past the new position (raised by
    // Skater_First_Forward_Collision_Height, Skater_First_Forward_Collision_Length further along the move). On
    // something not too much like ground: true (stopped) when 0x004FFDB0 takes the skater there and it's fast upwards
    // or the wall is steep, or when snapping up onto a ledge (Physics_Air_Snap_Up, lowered in steps of 2); on vert,
    // pushed back off the wall (Skater_Min_Distance_To_Wall) and turned along it; otherwise (no wallplant, no
    // 0x00508140) a bonk: the trigger, the sound, the velocity along the wall, the facing turned away from it, and a
    // moving object's push. False when nothing stopped the skater.
    bool check_air_forward_collision(const maths::Vec4* from);
    // 0x0050A2C0: a feeler from the skater's side height (Skater_side_collide_height) out to one side (`side` -1 or
    // 1 along the right) `length` long (more in the air: Skater_air_extra_side_col). Never while wall riding. In the air the
    // skater is pushed out of what it hit (a tenth of the overlap) - true only when 0x00508140 took over; otherwise
    // a near-vertical wall puts the skater back (+0x98), then, unless something is above the hit or a trigger takes
    // over, onto it (true).
    bool check_side_collision(float side, float length);
    // 0x0050C930: both sides (Skater_side_collide_length); hit on both, the position goes back to where it was.
    void test_lateral_hits();

    // --- core_grind.cpp: onto a rail ---
    // 0x0050A770: onto the rail `node` (of `manager`) at `point` - nothing when a feeler from the skater to the point
    // hits something more than 6 from it. A node on its own is a point rail: kicked up off it into the air
    // (Physics_Point_Rail_Kick_Upward_Angle, Point_Rail_Speed_Boost), or - holding a spin button (+0x100 / +0xA0)
    // long enough after the last - a point rail spin. Otherwise a lip trick when the skater comes up a vert ramp at
    // the rail (the Lip* angles; not with `no_lip_trick`), else a grind: along the rail the way the skater goes (a
    // coin toss when across it; nothing at the rail's end), Rail_Speed_Boost, the bad ledge feelers either side, and
    // the grind trick picked from the pad. `from_object` (from a car plant or a vehicle): the velocity isn't
    // flattened, and too slow against the facing it starts from the facing.
    void start_grind(const maths::Vec4* point, objects::GrindPoint* node, objects::GrindRailHub* manager,
                     bool no_lip_trick, bool from_object);
    // 0x0050DAE0: in the air (or `always`), grind button (+0x1A0) held, not after a wallplant for
    // Physics_Wallplant_Disallow_Grind_Duration: a rail along the last move (soon after leaving a rail, only one
    // lining up within Rail_Corner_Leave_Angle) - the level's, else each moving rail manager's (the move in its
    // object's space, the point back in the world: the skater then rides that object, less its velocity) - and,
    // when 0x00500020 allows it, onto it (start_grind).
    void try_grind(bool always);

    // --- core_states.cpp ---
    // 0x0050BF50: the lip state's update: carried by what the skater rides (0x00427A00: the position and +0x258),
    // the lip's balance (Perfect Rail cheat: its global flag, no lean), button +0x1C0 to flag +0x20, getting off it
    // (0x004FFB10: the rail's TriggerScript twice), the rail node's Name, +0x238 the time.
    void step_coping_move();
    // 0x0050C670 (from the skater when it gets back on its board): on the ground or into the air (+0x94 the same;
    // in the air flag +0xC0, and +0xB0 for +0x1534's +0x9B), the matrices and ground normals from the object, the
    // positions kept, the jump / trick state cleared; then onto a rail (`grind`: a point, +0x10 its manager, +0x14
    // its node, +0x18 an object to ride - the old ride let go) or an acid drop (0x00502F20).
    void resume_skating(bool on_ground, const objects::FootMovement* grind_from, const AcidDrop* acid_drop);

    // --- core_rail.cpp: grinding ---
    // 0x0050DDF0: the rail state's update (state 4). Carried along by a moving rail; off the rail when 0x004FFB10
    // says so (Rail_jump_rerail_time, flags +0xD0 / +0xD8); a grind / slide's balance (the Perfect Rail cheat). At the
    // rail's last node: 0x00504730. Otherwise the rail's gravity along the segment (the way round flips +0x34C when
    // it turns the skater back), the next segment's corner (Rail_Corner_Leave_Angle; only nodes flagged 4), past the
    // segment's end onto the next one or transfer_or_leave_rail; still grinding: the velocity, the facing (turned 0.3 of
    // the way, display and object matrices rebuilt), the move with what went past the end, and a feeler ahead that
    // throws the skater off the rail on a hit (OffRail). Then the ground normal, the score's rail time (0x00564270),
    // the node's Name.
    void step_grind_move();
    // 0x0050C200: at the end of a rail (`end`: where it ends): onto another rail along the move (find_grind_rail within
    // the corner angle, the wall side of this rail; not this rail's next or last node outside the park editor) when
    // the skater is between its two nodes - or, outside the park editor, when the move is at least as long as its
    // segment (then onto its middle) - the velocity along it, the skater at its point; else off the rail into the
    // air (OffRail).
    void transfer_or_leave_rail(const maths::Vec4* end);

    // --- core_ground.cpp ---
    // 0x0050EE70: the ground state's update (state 0). Off vert, no rail, the heading (+0x348, degrees from the facing,
    // fast_a_cos), the velocity turned into the ground's plane when moving, Physics_Ground_Gravity (or +0x360 going up
    // while +0x350 is set) along the ground; the balance trick (none: 0x004FE320 / 0x005019E0 or 0x004FE490 /
    // 0x004FE530; a skitch or a manual: its balance and its Perfect cheat); 0x00506DF0, 0x005055B0. Skitching: the
    // skitched object's velocity (skitch_speed_match) and no move of its own. Carried by what the skater rides on.
    // Then the move, kept to the ground (check_for_ground - skitching: a feeler down the up for the terrain); stopped
    // short on the ground, the rest of the move once more along the velocity. Still on the ground: 0x00503560,
    // 0x004FF590, the side collisions and 0x00503B30 (not skitching), 0x004FF920, 0x004FFB10 (off: 0x004FEBA0, flags
    // +0x48 / +0xD8), 0x00522370 with +0x1B0.
    void do_ground_physics();

    // --- core_wallride.cpp ---
    // 0x0050F830: the wall ride state's update (state 2). No rail, +0x39C the time; carried by what the skater rides
    // on; the move under Wall_Ride_Gravity; turned along the wall (Wall_Ride_Turn_Speed, about the wall's normal +0x38C)
    // when not heading up it enough. A feeler ahead along the move: onto the top of the wall (a hit facing up: the
    // ground, try_grind first) or round a corner (another wall) - or off the wall's edge (0x005051B0 too). Then a
    // feeler down the up: the wall still there (at it again; not a wall: pushed off it), or gone (off, pushed out
    // 18). Button +0x1C0 -> flag +0x20; jumping off (0x004FFB10): out (Wall_Ride_Jump_Out_Speed) and up
    // (Wall_Ride_Jump_Up_Speed), upright.
    void step_wall_ride();

    // --- core_update.cpp ---
    // 0x00510830 (vtable +0x04): the frame. Its length (+0x8C, slow motion included); the frame's flags (+0x254
    // landed this frame, +0x256 began in a lip, +0x255 the timed flags' +0x78); button +0x1C0 -> flag +0x20; the
    // speed limited; flags +0x98 / +0xA0 cleared; the state's update (ground, air, wall ride, lip, rail, wallplant);
    // try_grind (inlined in the original); free revert, focus, projectiles, special time; event 0xF225FE69 /
    // 0x5E27200A when +0x4F0 (in the air with flag +0x30, not +0x78) changes; +0x14CC cleared out of the air.
    void tick_frame() override;

    // --- core_physics_state.cpp: the component made, reset and destroyed; timers ---
    // (Fields: core_physics_fields.h. The component is 0x153C bytes, type 0x5BD63B29 SkaterCorePhysics, vtable
    // 0x0064BC48: 0 Destroy, 1 its update 0x00510830, 12 invoke_method_by_name 0x00501600.)
    // 0x00505440 (the component type's maker): a new one (0 if out of memory).
    static void* fabricate();
    // 0x00500CA0: its type; its two feelers (+0xA8, +0x158) and its collision cache (+0x4F4) made; no components
    // (+0x150C-+0x1538); a few flags cleared, +0x308 = -1, +0x2E8 = 1. The rest isn't initialised (zeroed memory).
    SkaterPhysicsModule();
    // 0x00506BE0: back to rest - the old position and +0x98 = the position (not in a network game), no velocity,
    // the ground state, every timed flag set and cleared (flag 7 telling +0x1514's +0x1E so), the ride let go of,
    // and the speed / trick timers and flags cleared.
    void get_reset();
    // 0x00506BA0: `feeler` copied as the last ground hit (+0x158); +0x1514's +0x1D = +0x1AA.
    void remember_floor_probe(const void* feeler);
    // 0x00506BD0 (the script member function CancelRotateDisplayandUpdateMatrix): the model's rotations applied and
    // reset (apply_model_rotations); whether any was on, 1 or 0.
    int cancel_rotate_displayand_update_matrix(script::Struct* params, script::Script* script);
    // 0x00500670 (from the frame's update): the collision cache (+0x4F4) refilled for a box round the skater - 180
    // either way along x and z, 204 below and 96 above - and made the feelers' current cache (0x00691974).
    void refresh_hit_cache();
    // 0x00500710: a free revert earned when Physics_Time_Before_Free_Revert seconds have passed since +0x31C while
    // +0x318 (the ones pending) isn't 0 ("You earned a free revert!!!!"); the next one timed from now.
    void update_free_revert();
    // 0x005007A0: the focus button (pad +0xE0) held longer than focus_button_pressed_time: toggle_bullettime run
    // (once until it's let go: +0x14C1).
    void update_focus_button();
    // 0x00500820: projectiles (+0x2F4 on, +0x2F8 of them, not in states +0x70 / the +0x1538 component's): the
    // throw (0x004FC3C0) at most every 30 frames (+0x14C4) - then walk_ or skate_maybe_throw_projectile run.
    void update_projectiles();
    // 0x00500900: a timed model rotation (+0x2DB on, +0x2E0 left, +0x2E8 a frame): counted down in the air (+0x3A4
    // cleared at 20 or less); at 0 or less, or out of the air at once, the model's rotations reset (0x004208D0;
    // with +0x2DC, the SkaterFlipAndRotate component's 0x005114F0 first).
    void update_model_rotation();
    // 0x00500BC0: a special's time (+0x301 on; the score's first float) counted down by the frame; at 0 or less,
    // KillSpecial run with TimeLimit.
    void update_special_time();
    // 0x00500DE0 (the script member function GetAirTimeLeft): AirTimeLeft = the time until the skater falls back
    // to the height it took off from (+0x11C of the flags block), from its upward speed and the gravity: 0 if
    // that's in the past, 0.00001 if it never gets there. Added to the script's parameters. 1.
    int get_air_time_left(script::Struct* params, script::Script* script);
    // 0x00500E80 (the script member function CanKick): whether the skater may push off now (0x004FE490), 1 or 0.
    int can_kick(script::Struct* params, script::Script* script);
    // 0x00500E90 (the script member function OrientToNormal): the ground normals (+0x58, +0x68) set to the skater's
    // up, then the skater turned onto the feeler's last hit normal (+0x130: 0x004FE860). 1.
    int orient_to_normal(script::Struct* params, script::Script* script);

    // 0x005004B0: a grind's trick: on the motoskateboard (+0x302) Trick_Motoskateboard_Grind, else
    // GrindTrickList[`rail_type` (0-8, else 0)][the four buttons as bits]; set on the skater's script (made if it
    // has none) and run at once; flag +0xE0.
    void start_grind_trick(uint32_t rail_type, bool trick1, bool trick2, bool trick3, bool trick4);

    // --- core_physics_speed.cpp: friction, brakes, the speed limits ---
    // 0x00501850: the horizontal speed limited - to the max-max speed stat (+0x358 while +0x350's timer runs), and
    // above the max speed stat (+0x354) slowed by Physics_Heavy_Air_Friction (0x004FE1F0); the timers +0x350 (not
    // at -1 / -2) and +0x320 (then the rolling friction +0x314 back to Physics_Rolling_Friction) counted down.
    void clamp_skater_speed();
    // 0x005019E0: braking (+0x324): unless the skater may turn on the slope instead (0x004FE420), the speed cut
    // by Physics_Brake_Acceleration a second - to 0 when less than two frames' worth is left.
    void get_brake();
    // 0x00506DF0: on the ground, the friction (0x004FE6A0), then either the turn down a slope (0x00501B00) or the
    // rolling friction (0x004FE730) - only while +0x2D9 or flag +0x70 is set or the rolling friction isn't the
    // physics value.
    void apply_ground_friction();
    // 0x005021D0: unless flag +0x88 is set, a ride on the object the feeler hit (0x00427CD0: true when one starts);
    // a new ride makes the velocity relative to it (its velocity taken off).
    void start_riding_hit_object();

    // --- core_physics_feelers.cpp: the feelers ---
    // 0x005003A0: the feeler (+0xA8) along its line (+0x208 to +0x218), faces with any of `ignore` or without all of
    // `require` passed over, movables too. On a hit: +0x228 skatable (face flag 1, or 8 vert, or neither 2 nor 4 and
    // no steeper than Wall_Non_Skatable_Angle), +0x229 vert (flag 8), +0x22A a wall (flag 4). Whether it hit.
    bool probe_touches_zone(uint32_t ignore, uint32_t require);
    // 0x00500020: whether the skater may grind `rail` (a rail node; +0x40 / +0x44 its neighbours) now: not the
    // last one (+0x22C) or its neighbours again while still in the air after it; a new rail sets flag +0x50 (in a
    // created park: one whose ends are all more than 6 from the last's); the same one only after +0x23C ms since
    // +0x234. Then, unless `anyTime`: not in state 4, and on a vert wall (flag +0x38) only moving up. (The second
    // argument isn't used.)
    bool can_grind_rail(const void* rail, const void* unused, bool any_time);
    // 0x005012B0 (the script member function NearGround): whether there's ground within 20 above or below a point
    // 16 to the skater's side (with Right: the other side). 1 or 0.
    int near_ground(script::Struct* params, script::Script* script);
    // 0x00500F10 (the script member function SkateInAble): whether there's vert (+0x229) to skate into. With Lip: a
    // feeler down SkateInAble_LipDownOffset from the position less SkateInAble_LipHorizOffset along up, then (missed)
    // one down SkateInAble_LipExtraCheckDownOffset from the position less SkateInAble_LipExtraCheckHorizOffset along
    // up; else one across the skater, from SkateInAble_HorizOffset along its side row (and SkateInAble_DownOffset
    // down) to as far the other way - towards that side with left. 1 or 0.
    int skate_in_able(script::Struct* params, script::Script* script);
    // 0x005013E0 (the script member function SetAmmo): the projectile count (+0x2F8) set to the unnamed integer (kept
    // without one); ammo_meter_change respawned on the skater with count = the change.
    int set_ammo(script::Struct* params, script::Script* script);
    // 0x005014E0 (the script member function CheckNatasExitDirection): the skater's matrix set to its camera's,
    // turned about Y for `dir` (the D-pad's eight ways): Down not at all, Right / left a quarter turn, DownRight /
    // DownLeft an eighth, UpRight / UpLeft three eighths, Up and anything else half a turn. 1.
    int check_natas_exit_direction(script::Struct* params, script::Script* script);
    // 0x00501600 (vtable slot 12: a member function by name): Ledge - +0x248; Obj_MoveToNode - the motion
    // component moves the skater, then the display matrix, the shadow, +0x98, the old position and the camera follow
    // (teleported); without NoReset it stops and lands, with orient its horizontal speed turns to the new facing.
    // 2 for any other name.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x005009C0: which end or side of the board (nose / tail 12 along the forward row, left / Right 12 along the
    // side row) has the nearest wall: +0x2D4 (0 if none).
    void find_nearest_wall_side();
    // 0x00501C90: one side collision feeler (`side` 0-3) from `start` to `end`, unless the opposite side hit more
    // than 500 ms ago: a hit's normal added to +0x2AC, its distance kept in +0x2BC if nearer, the side's time noted.
    void check_side_feeler(const maths::Vec4* start, const maths::Vec4* end, uint32_t side);
    // 0x005055B0: the side collisions (+0x2C0 the reach and +0x2C4 the push, both set): four feelers at
    // Skater_side_collide_height, both ways along the side and forward rows; with a hit, the skater pushed off (the
    // nearer, the harder) and turned (+0x2C8 a second) along a wall it faces into.
    void check_side_feelers();
    // 0x005054A0: the model's rotations (+0xC8 / +0x15C / +0x1F0 of the Model component: any on) applied to the
    // skater's matrix (0x004215F0), the velocity turned with its rotation about up, then reset. Whether any was on.
    bool apply_model_rotations();

    // --- skitch.cpp: skitching ---
    // 0x00503EC0: the grab button held longer than skitch_hold_time on the ground (not driving, not skitching): the
    // nearest skitch point of another object within Skitch_Max_Distance grabbed (+0x24C its index, +0x364 the car);
    // both told Skitched.
    void try_start_skitch();
    // 0x00501DB0: skitching: the button pairs move to the next skitch point along the car; the skater held
    // Skitch_Offset behind the point (pulled towards a new one at skitch_suck_speed), turned as the car. Without
    // a car, or 180 or more away: OffMeterBottom.
    void get_skitch();

    // --- core_physics_turning.cpp: turns ---
    // 0x005002D0: jumping off a rail with pad +0x40 / +0x60 held: the velocity and matrices turned by
    // Rail_Jump_Angle one way or the other.
    void turn_for_rail_jump();
    // 0x00503560: turning on the ground (pad +0x40 / +0x60) at Physics_Ground_Rotation - or with +0x20 held
    // Physics_Ground_Sharp_Rotation, ramping up over 600 ms below a speed of 10; the velocity too unless +0x388.
    void turn_on_ground();
    // 0x005039F0: bouncing off a wall of normal `normal`: the angle between the side row and the normal (acos;
    // past 90 degrees, less 180) times Wall_Bounce_Angle_Multiplier and `scale` turns the velocity and the matrices
    // (`turn` = the turn). Returns the angle.
    float deflect_from_wall(const maths::Vec4* normal, float* turn, float scale);
    // 0x00501B00: when the skater may (0x004FE420), turned towards straight down the slope at
    // Skater_Slow_Turn_on_slopes (along the velocity, or the facing when hardly moving). Whether it did.
    bool turn_down_slope();
    // 0x00504730: the natas spin: its rate from natas_spin_factor less how far off balance; the trick's rotation
    // total and its display (as Spin); with CHEAT_PERFECT_RAIL the balance held. +0x1504 = now.
    void natas_spin();
    // 0x00504910: leaning in the air (pad +0x20 / +0x40) by Physics_Air_Lean_stat, after Physics_Air_No_Lean_Time
    // and ramping up until Physics_Air_Ramp_Lean_Time.
    void lean_in_air();
    // 0x00504A60: in an acid drop (flag +0x78): the matrices along the slerp at +0x3F0, eased, over +0x48C seconds.
    void rotate_in_acid_drop();
    // 0x00504B40: tipped over in the air above ground (500 below, not vert, not too steep): flag +0x80; turned back
    // upright at Physics_recover_rate_stat.
    void recover_in_air();
    // 0x00504DF0: leaning sideways in the air: rolled back at skater_upright_sideways_speed.
    void keep_upright();

    // --- core_physics_walls.cpp: walls and ceilings ---
    // 0x00503710: a vert wall plant: the trick button (or within Wall_Ride_Triangle_Window) on vert (flags +0x30,
    // +0xD8) with no rotation and none done yet: the wall within Physics_vert_wallplant_distance below, moving up at
    // least the velocity limit, 60 above the take-off, steep enough (or a vert face), the trick allowed (0x0051DDF0):
    // triggers, the boost, Vert_WallPlant, +0x4E8 set. Whether it planted.
    bool vert_wallplant();
    // 0x00503B30: stopped (no faster than Skate_min_wall_lean_push_speed) and leaning left / right (pad +0x40 / +0x60):
    // a feeler Skate_wall_lean_push_length out at Skate_wall_lean_push_height after Skate_wall_lean_push_time; a wall
    // there pulls the skater towards it, unless that's blocked.
    void lean_push_off_wall();
    // 0x00504EE0: a ceiling within the skater's head height (6 for Physics_Ignore_Ceilings_After_Wallplant_Duration
    // after a wall plant): the skater slid down out of it, its velocity along it. Whether there was one.
    bool keep_off_ceiling();
    // 0x005051B0: the same 18 out along +0x38C (the head over the wall's edge).
    bool keep_off_ceiling_at_wall();
    // 0x00506E50: a feeler ahead along the move at Skater_First_Forward_Collision_Height: something skatable at an
    // angle to the ground pushes the skater out; else its triggers, and a bounce off it (bounce_off_wall_ahead).
    void check_wall_ahead();
    // 0x005059D0: bouncing off a wall ahead of normal `normal` (unless 0x004FF670): turned (deflect_from_wall), slowed
    // the more head-on (beyond Wall_Bounce_Dont_Slow_Angle; a flail beyond Wall_Bounce_Dont_Flail_Speed), moved
    // back out 6 along the normal; another wall ahead then: turned back, half as fast.
    void bounce_off_wall_ahead(const maths::Vec4* normal);

    // --- core_physics_wallplant.cpp: wall plants and wall rides ---
    // 0x00507500: a wall plant off what the feeler hit: in the air, not too soon after the last (the
    // Physics_Disallow_Rewallplant_Duration / Physics_Wallplant_Delay / Physics_Min_Wallplant_Height checks), with a
    // Wallplant_Trick pressed - else Physics_late_wallplant_frames to press it `late` (the skater backed off 24);
    // `walking`: a feeler 36 behind the facing instead. At the approach angle allowed the skater bounces off (speed
    // less Physics_Wallplant_Speed_Loss, at least the minimum exit speed; up at the vertical exit speed), rides what it
    // hit, is set Physics_Wallplant_Distance_From_Wall off it, upright: Wallplant, state 5. `check_only`: whether it
    // could, nothing done. Whether it planted.
    bool wall_plant(bool late, bool check_only, bool walking);
    // 0x00508140: starting a wall ride on the wall the feeler hit (+0x22A): triangle pressed (or within
    // Wall_Ride_Triangle_Window), Wall_Ride_Delay after the last ride and plant, fast enough along it, not upside
    // down, head-on or tilted: the skater put on it (+0x38C its normal), facing along it; WallRideLeft / Right,
    // state 2. Whether it started.
    bool start_wall_ride();

    // --- core_physics_acid_drop.cpp: the acid drop, the spine transfer ---
    // 0x00502240: looking for an acid drop from `position` (from the higher of it and `last_position`) along the
    // horizontal `velocity` (walking: the walking direction): feelers 4000 down, 0.01, then 3 (beyond 100: 27) at a time
    // up to 500 ahead (walking on the ground: Physics_Acid_Drop_Walking_On_Ground_Search_Distance, 200 up), through a
    // collision cache over the search, for a vert wall facing away. Then the arc there: `velocity` turned towards it
    // (`pop`: at least Physics_Acid_Drop_Pop_Speed up; at most twice that), high enough and long enough in the air
    // (Physics_Acid_Drop_Min_Air_Time), and a point up the wall (24 at a time) reached with nothing in the way: `drop`.
    // Whether one was found (`velocity` is put back if not).
    bool find_acid_drop(bool pop, const maths::Vec4* position, const maths::Vec4* last_position, maths::Vec4* velocity,
                        AcidDrop* drop);
    // 0x00502F20: starting the acid drop to `drop`: the horizontal speed to get there in the fall's time (flags block
    // +0xE4), the wall's orientation (+0x3B0, +0x490) and the slerp to it (+0x3F0) over that time; flags +0x78 (in the
    // drop), +0x30, +0xA8; SkaterAcidDropTriggered with DropHeight; Ollied handlers cleared.
    void start_acid_drop(const AcidDrop* drop);
    // 0x00505F70 (from break_vert): a spine or hip transfer - the vert wall found by a feeler 4000 down from half a unit
    // back down the wall (the velocity's opposite turned a quarter round), then a target to land on (0x004FEE10: looking
    // back, else - one of input +0x60 / +0x80 held - back and along the wall that way). Too flat a target or one more
    // than 24 away: all the speed straight up; too slow to cover the distance in the fall's time (at least 0.1): false.
    // Something between the skater and the target: +0x2D0 set (break_vert tries again), true. Otherwise the drop's
    // orientation (a spine: along the target's slope; a hip: straight up or down), the slerp there (+0x3F0, at least
    // 0.9 seconds), +0x490 / +0x37C / the flags block's +0xE4 as the acid drop's, SkaterAwardTransfer or
    // SkaterAwardHipTransfer, the Ollied handler cleared, flags 0x78 set and 0xA8 / 0x38 / 0x48 cleared, +0x14C0. True.
    bool try_spine_transfer();

    // --- core_member_functions.cpp: the script member functions of the table at 0x006826C8 (named by its entries;
    // each returns 1 unless said otherwise, and uses neither argument unless said) ---
    // 0x004FBE80 (registered with Create): the table made member-function symbols (0x18-byte entries, put in by the
    // static initialiser at 0x006417D0).
    static void publish_method_table();
    // 0x004FBEA0 (`this` isn't used): an air time from `params` - the unnamed float, in milliseconds; with seconds /
    // second times 1000, with frames / frame times 16 - truncated to an integer.
    int air_time_threshold(script::Struct* params);
    // Bytes of the component set or read: CanBrakeOff / CanBrakeOn (+0x311 0 / 1), can_kick_on / CanKickOff (+0x310 0 /
    // 1: kicking forbidden), ForceAutoKickOn / ForceAutoKickOff / AutoKickIsOff (+0x2D9), SkaterVehicleOn / Off /
    // IsSkaterOnVehicle (+0x2ED), Canspin (+0x370 0), BailOn / Bailoff / BailIsOn (+0x371), BadLedge (+0x249),
    // came_down_off_spine (+0x372), LandedFromVert / SetLandedFromVert / ResetLandedFromVert (+0x373), AllowLipNoGrind /
    // ClearAllowLipNoGrind (+0x375), NoRailTricks / AllowRailTricks (+0x3A4), Braking (+0x324), InRevert / OutRevert /
    // IsInRevert (+0x300), ProjectilesOn / Off (+0x2F4), FocusOn / IsSkaterFocused (+0x301), EnterPowerslide /
    // ExitPowerslide (+0x2D8), moto_skateboard_on / Off / is_on_motoskateboard (+0x302), skater_is_flipping /
    // ResetIsFlipping (+0x2DB), SetThrowingProjectile / Unset / IsThrowingProjectile (+0x14C8), EndManualTurnaround
    // (+0x304 0), EnterTantrum / ExitTantrum / InTantrum (+0x1508), EnterTagging / ExitTagging / IsTagging (+0x1509);
    // ResetProjectileInput (+0x308 = -1). The queries return 1 or 0.
    int can_brake_off(script::Struct* params, script::Script* script);             // 0x004FBF40
    int can_brake_on(script::Struct* params, script::Script* script);              // 0x004FBF50
    int can_kick_on(script::Struct* params, script::Script* script);               // 0x004FBF60
    int can_kick_off(script::Struct* params, script::Script* script);              // 0x004FBF70
    int force_auto_kick_on(script::Struct* params, script::Script* script);        // 0x004FBF80
    int force_auto_kick_off(script::Struct* params, script::Script* script);       // 0x004FBF90
    int auto_kick_is_off(script::Struct* params, script::Script* script);          // 0x004FBFA0
    int skater_vehicle_on(script::Struct* params, script::Script* script);         // 0x004FBFB0
    int skater_vehicle_off(script::Struct* params, script::Script* script);        // 0x004FBFC0
    int is_skater_on_vehicle(script::Struct* params, script::Script* script);      // 0x004FBFD0
    int can_spin(script::Struct* params, script::Script* script);                  // 0x004FC000
    int bail_on(script::Struct* params, script::Script* script);                   // 0x004FC010
    int bail_off(script::Struct* params, script::Script* script);                  // 0x004FC020
    int bail_is_on(script::Struct* params, script::Script* script);                // 0x004FC030
    int bad_ledge(script::Struct* params, script::Script* script);                 // 0x004FC080
    int came_down_off_spine(script::Struct* params, script::Script* script);       // 0x004FC090
    int landed_from_vert(script::Struct* params, script::Script* script);          // 0x004FC0A0
    int set_landed_from_vert(script::Struct* params, script::Script* script);      // 0x004FC0B0
    int reset_landed_from_vert(script::Struct* params, script::Script* script);    // 0x004FC0C0
    int allow_lip_no_grind(script::Struct* params, script::Script* script);        // 0x004FC0D0
    int clear_allow_lip_no_grind(script::Struct* params, script::Script* script);  // 0x004FC0E0
    int no_rail_tricks(script::Struct* params, script::Script* script);            // 0x004FC0F0
    int allow_rail_tricks(script::Struct* params, script::Script* script);         // 0x004FC100
    int in_revert(script::Struct* params, script::Script* script);                 // 0x004FC1A0
    int out_revert(script::Struct* params, script::Script* script);                // 0x004FC1B0
    int is_in_revert(script::Struct* params, script::Script* script);              // 0x004FC1C0
    int projectiles_on(script::Struct* params, script::Script* script);            // 0x004FC1D0
    int projectiles_off(script::Struct* params, script::Script* script);           // 0x004FC1E0
    int focus_on(script::Struct* params, script::Script* script);                  // 0x004FC1F0
    int is_skater_focused(script::Struct* params, script::Script* script);         // 0x004FC200
    int reset_projectile_input(script::Struct* params, script::Script* script);    // 0x004FC210
    int enter_powerslide(script::Struct* params, script::Script* script);          // 0x004FC230
    int exit_powerslide(script::Struct* params, script::Script* script);           // 0x004FC240
    int moto_skateboard_on(script::Struct* params, script::Script* script);        // 0x004FC250
    int moto_skateboard_off(script::Struct* params, script::Script* script);       // 0x004FC260
    int is_on_motoskateboard(script::Struct* params, script::Script* script);      // 0x004FC270
    int skater_is_flipping(script::Struct* params, script::Script* script);        // 0x004FC280
    int reset_is_flipping(script::Struct* params, script::Script* script);         // 0x004FC290
    int set_throwing_projectile(script::Struct* params, script::Script* script);   // 0x004FC2A0
    int unset_throwing_projectile(script::Struct* params, script::Script* script); // 0x004FC2B0
    int is_throwing_projectile(script::Struct* params, script::Script* script);    // 0x004FC2C0
    int end_manual_turnaround(script::Struct* params, script::Script* script);     // 0x004FC2D0
    int enter_tantrum(script::Struct* params, script::Script* script);             // 0x004FC2E0
    int exit_tantrum(script::Struct* params, script::Script* script);              // 0x004FC2F0
    int in_tantrum(script::Struct* params, script::Script* script);                // 0x004FC300
    int enter_tagging(script::Struct* params, script::Script* script);             // 0x004FC310
    int exit_tagging(script::Struct* params, script::Script* script);              // 0x004FC320
    int is_tagging(script::Struct* params, script::Script* script);                // 0x004FC330
    int get_braking(script::Struct* params, script::Script* script);               // 0x004FC9B0
    // The timed flags read or set (the time noted when one changes): flipped (+0x28), switched (flipped, inverted
    // unless the skater's +0x110 is set), Crouched (+0x20), FirstTimeOnThisRail (+0xC8), SKITCHING (+0x88), InVertAir
    // (+0x30), IsInBail (+0x70); CancelWallpush (+0xB8 on), in_bail / NotInBail (+0x70), 0x004FD630 (checksum
    // 0x2A547E37, name not recovered: +0x30 on), OverrideCancelGround (+0x90: on unless Off), no_spin (+0x370 = 1 and
    // +0x68 off).
    int get_flipped(script::Struct* params, script::Script* script);             // 0x004FC780
    int get_switched(script::Struct* params, script::Script* script);            // 0x004FC7A0
    int get_crouched(script::Struct* params, script::Script* script);            // 0x004FC7E0
    int first_time_on_this_rail(script::Struct* params, script::Script* script); // 0x004FC800
    int skitching(script::Struct* params, script::Script* script);               // 0x004FC820
    int cancel_wallpush(script::Struct* params, script::Script* script);         // 0x004FC840
    int no_spin(script::Struct* params, script::Script* script);                 // 0x004FCF90
    int in_bail(script::Struct* params, script::Script* script);                 // 0x004FCFC0
    int not_in_bail(script::Struct* params, script::Script* script);             // 0x004FCFF0
    int is_in_bail(script::Struct* params, script::Script* script);              // 0x004FD020
    int in_vert_air(script::Struct* params, script::Script* script);             // 0x004FD1C0
    int override_cancel_ground(script::Struct* params, script::Script* script);  // 0x004FD530
    int set_vert_air(script::Struct* params, script::Script* script);            // 0x004FD630
    // 0x004FBFE0 SetSkaterVehicle: the vehicle (+0x2F0) from the unnamed checksum (asserted). 0x004FC180
    // SetAmmoType: the projectile type (+0x2FC) from the unnamed checksum, if any. 0x004FC060 change_rail_adjust: +0x3A8
    // from the unnamed integer, if any. 0x004FC040 LockVelocityDirection: +0x388 = whether `on` is there.
    int set_skater_vehicle(script::Struct* params, script::Script* script);      // 0x004FBFE0
    int lock_velocity_direction(script::Struct* params, script::Script* script); // 0x004FC040
    int change_rail_adjust(script::Struct* params, script::Script* script);      // 0x004FC060
    int set_ammo_type(script::Struct* params, script::Script* script);           // 0x004FC180
    // 0x004FC110 SetExtraPush: radius (+0x2C0, 48), speed (+0x2C4, 100) and 0xDFDFEAB8 (+0x2C8, 6) from `params`.
    int set_extra_push(script::Struct* params, script::Script* script);
    // 0x004FC340 DoingFlip: whether a timed model rotation is on (+0x2DB) with more than 10 left (+0x2E0). 1 or 0.
    int doing_flip(script::Struct* params, script::Script* script);
    // 0x004FC880 / 0x004FC8E0 AirTimeLessThan / AirTimeGreaterThan: the time in the air (in the air or on a wall: since
    // +0x380; otherwise the last one, +0x384 - +0x380) against air_time_threshold(params), unsigned. 1 or 0.
    int air_time_less_than(script::Struct* params, script::Script* script);
    int air_time_greater_than(script::Struct* params, script::Script* script);
    // 0x004FC940 GetAirtime: that time in seconds (as unsigned) added to the script's parameters as 0xAD6BCDB4.
    int get_airtime(script::Struct* params, script::Script* script);
    // 0x004FC9C0 RestoreAutoKick: +0x2D9 back to the skater's profile's (the byte at +0x395 of entry +0x2BC of the
    // table at *0x007CE478, 8 bytes each).
    int restore_auto_kick(script::Struct* params, script::Script* script);
    // 0x004FC9F0 DoCarPlantBoost: no downward speed, CarPlant_Upward_boost added upwards, then CarPlant_Forward_boost
    // along the horizontal velocity (the facing when slower than 10; (1, 0, 1) when that's under 0.01 too) made unit.
    int do_car_plant_boost(script::Struct* params, script::Script* script);
    // 0x004FD040 SetRailSound: slide (the unnamed checksum, asserted) or not: flag +0x58 and the sound component's +0x1E.
    int set_rail_sound(script::Struct* params, script::Script* script);
    // 0x004FD0D0 change_wheel_drag: +0x314 from the unnamed float, if any; with Default and no special friction timer
    // (+0x320 at 0) Physics_Rolling_Friction.
    int change_wheel_drag(script::Struct* params, script::Script* script);
    // 0x004FD140 last_spin_was: backside / Frontside against the last spin's way (+0x3A5) and flipped; 1 with neither.
    int last_spin_was(script::Struct* params, script::Script* script);
    // 0x004FD1E0 turn_to_face_velocity: unless barely moving, the skater's facing along its velocity, the right row
    // up x facing and the up row facing x right (each normalized), the display matrix the object's.
    int turn_to_face_velocity(script::Struct* params, script::Script* script);
    // 0x004FD360 overridelimits: 0xC6F342D9 or end cancel it (+0x350 = 0); while one is fixed for the level (+0x350
    // -2) nothing else changes. Otherwise max (asserted; +0x354, and 0x73A19E3A +0x358, the same by default), time
    // (+0x350, 1e13 by default), Friction (+0x35C, 2e-6), gravity (+0x360, Physics_Ground_Gravity); 0xDF312928 makes
    // the time -1, CurrentLevel -2.
    int override_limits(script::Struct* params, script::Script* script);
    // 0x004FD4A0 SetSpecialFriction: the next of the unnamed array's frictions (+0x318 counts them, kept at the last;
    // +0x31C the time of the first) into +0x314; duration (+0x320) if given.
    int set_special_friction(script::Struct* params, script::Script* script);
    // 0x004FD580 GetMatrixNormal (x, y, z: the skater's up), 0x004FD5E0 GetLastGroundPos (Pos: +0x338), 0x004FD610
    // GetLastGroundHeading (last_heading: +0x348), 0x004FDC30 GetAmmo (ammo: +0x2F8), 0x004FDC50 GetAmmoType
    // (ammo_type: +0x2FC), 0x004FDC70 get_pitch_bail_dir (pitch_bail_dir: +0x2D4): added to the script's parameters.
    int get_matrix_normal(script::Struct* params, script::Script* script);
    int get_last_ground_pos(script::Struct* params, script::Script* script);
    int get_last_ground_heading(script::Struct* params, script::Script* script);
    int get_ammo(script::Struct* params, script::Script* script);
    int get_ammo_type(script::Struct* params, script::Script* script);
    int get_pitch_bail_dir(script::Struct* params, script::Script* script);
    // 0x004FD660 IsFacingWall: a feeler from the skater along its facing (made unit) for the unnamed distance (200):
    // a hit on something not vert (face flag 8) whose normal's y is within 0.1 of 0. 1 or 0.
    int is_facing_wall(script::Struct* params, script::Script* script);
    // 0x004FD840 (checksum 0xA038260D, name not recovered): the same feeler; on such a wall the velocity and the
    // facing turned to point along its normal (change_heading), the skater flipped (SkaterFlipAndRotate), the display
    // matrix the object's. 1 then, else 0.
    int turn_from_wall(script::Struct* params, script::Script* script);
    // 0x004FDA80 IsFacingVert: the same feeler from 50 higher; a hit on vert. 1 or 0.
    int is_facing_vert(script::Struct* params, script::Script* script);
    // 0x004FDC90 FocusOff: +0x301 off, the skater camera's look-around unlocked.
    int focus_off(script::Struct* params, script::Script* script);
    // 0x004FDCC0 GetFocusTimeLeft (focus_time: the score's first float), 0x004FDCF0 GetFocusTimesAllowed
    // (focus_allowed: the score's +4) - added to the script's parameters -, 0x004FDD20 SetFocusTimesAllowed (the
    // score's +4 from the unnamed integer, if any). 0 when the skater has no score.
    int get_focus_time_left(script::Struct* params, script::Script* script);
    int get_focus_times_allowed(script::Struct* params, script::Script* script);
    int set_focus_times_allowed(script::Struct* params, script::Script* script);

    // --- core_physics_life.cpp: set up, taken down, the state changed ---
    // 0x004FC500 (0x00500DB0 the deleting one): the collision cache (+0x4F4) taken down, the smart pointers at +0x364
    // (the object skitched on) and in its two feelers (+0x1F0, +0x140) let go, then the base's.
    ~SkaterPhysicsModule() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x004FC5C0 (vtable slot 2; `params` isn't used): the rolling friction (+0x314) from Physics_Rolling_Friction;
    // the skater at the origin with no turn, the display matrix the same; the ground normals (+0x58, +0x68, +0x78)
    // straight up; +0x2EC and the projectile count cleared.
    void configure_from_data(script::Struct* params) override;
    // 0x004FC690 (vtable slot 3): the object's components it works with, +0x150C Input to +0x1538 (0x380D909A).
    void seal() override;
    // 0x004FC3C0: the projectile button (the script value projectile_control) released then pressed (the Trick
    // component's check_release_press); then the projectile input (+0x308) reset to -1. Whether it was.
    bool projectile_button_pressed();
    // 0x004FC440: the skater riding switch: flipped (the state's +0x28 byte, as it is) when the skater's +0x110 is
    // set, else not flipped (1 or 0).
    int is_sides_swapped();
    // 0x004FC470: the terrain under the skater (`terrain`'s low byte) told to the state and sound components.
    void set_surface_kind(uint32_t terrain);
    // 0x004FDD50 (when the skater gets off its board): the pending event (+0x4F0: 0x5E27200A broadcast), the rail
    // wait (+0x23C) from 0xE4412EB7, +0x234 at least +0x238; leaving the lip for the air: the state's +0x50 off; the
    // state to the ground (0), and the state's vectors +0xF8 / +0x108 straight up, +0xE4 (0, 0, 0, 1), +0xF4 off.
    void leave_skating();
    // 0x004FDE40: every flag of the state component but flipped (+0x28) set and then cleared - each gets the time now
    // (the slide flag +0x58 telling the sound component, +0x1E).
    void clear_flags();
    // 0x004FDED0: the physics state (the state component's +0x18) changed to `state`: a change notes the time (+0x90)
    // and the old one (+0x94). Off a lip, the skater put at the lip's position (+0x258) unless that's 0, and anything
    // but the lip clears it (0, 0, 0, 1); anything but the ground turns the trick objects on; off a rail the slide
    // flag and sound off; out of the air (state 1) the air time's end (+0x384) and flags +0xC0, +0xD0, +0xD8 off; into
    // the air from anything but a wall ride its start (+0x380), the trick component's +0x13A4, the spin buttons'
    // presses (pad +0xA0, +0x100) and +0x30C. Then the state component and the sound component (+0x18) told.
    void set_physics_state(int state);
    // 0x004FE120: turned around: the facing and the right row reversed (w too), the display matrix the object's, the
    // way round (+0x34C) flipped.
    void turn_around();
    // 0x004FE1D0 (from the state history; the argument isn't used): the flags cleared (clear_flags), into the air.
    void reset_to_air(void* unused);

    // --- core_physics_ground.cpp: friction, braking and pushing, the ground's normal ---
    // 0x004FE1F0: the velocity slowed by `friction` (its squared length x the frame x friction x 60, rounded to a
    // float, against the squared length): to rest when that's more than there is; nothing when barely moving.
    void apply_friction(float friction);
    // 0x004FE320: whether to brake: not auto-kicking nor holding pad +0x180 while slower than 50; or holding +0x40
    // with brakes allowed (+0x311) - slower than 50, neither +0x60 nor +0x80 held, or moving against the facing.
    bool should_brake();
    // 0x004FE420: whether the skater may turn down the slope instead of braking: slower than 0x62A1FA03 and the
    // ground normal's y (+0x6C) under 0xC3527EF2 (script values; names not recovered).
    bool may_turn_on_slope();
    // 0x004FE490: whether the skater may push off: kicking allowed (+0x310 off), auto-kick (+0x2D9) or pad +0x180
    // held, pad +0x40 not held, and no faster than the push speed stat (crouched or +0x303: 0x92E0247C, else
    // 0x4610C2E3).
    bool can_push_off();
    // 0x004FE530: a push: along the facing (with the velocity locked, +0x388: along the velocity unless slower than
    // 0.5) by the push stat (crouched or +0x303: 0x3D24128E, else 0x5F9B864D) over the frame.
    void push_off();
    // 0x004FE6A0: the friction for the stance - crouched 0xBED96EDA, else 0x1A78B6FC (physics values; while the
    // limits are overridden, +0x350, both +0x35C) - applied (apply_friction).
    void apply_stance_friction();
    // 0x004FE730: the rolling friction (+0x314) over the frame against the speed: to rest when it would overshoot.
    void apply_wheel_drag();
    // 0x004FE860 (the normal by value, RET 0x10): unless the global setting at *0x007CE5D8 +0x40 is Off, a normal
    // facing up has a feeler 72 along it from the skater - a hit makes it straight up. A new normal becomes the
    // ground normal (+0x68; the old +0x58 kept at +0x78 and the blend +0x88 restarted at 1) and the skater's up,
    // the matrix made orthonormal about it.
    void turn_to_normal(maths::Vec4 normal);
    // 0x004FEA50: the drawn ground normal (+0x58) blended from the old (+0x78) to the new (+0x68) as +0x88 runs down
    // at 0xD8120182 a second (a script value); the display matrix's up made it, orthonormal about it, its right and
    // facing the skater's.
    void update_ground_normal();
    // 0x004FEBA0: going up a vert wall (flag +0x40): the wall's normal (+0x278) and the take-off point (+0x268, 1
    // lower) kept; with a horizontal ground normal, the velocity turned into its plane, the skater onto it
    // (turn_to_normal), +0x288 the facing with y reversed, pushed out from the wall by 0x78384871, flags +0x30, +0x38,
    // +0x68 on and +0x298 6; otherwise (or not on vert) flag +0x30 off.
    void start_vert_air();

    // --- core_physics_moves.cpp: the gravity, a landing target, wall push-offs, turning round, flip spins ---
    // 0x004FEE10 (RET 0x18): a vert target to land on along `direction` from the skater: feelers straight down
    // (from 400 above to 4000 below) every 6 from 10 to 500 out, through a collision cache over the search, for a
    // vert face (normal y under 0.707) whose horizontal normal is at most 0.95 along `facing`. Found: its point and
    // normal, whether its object is still there (`object_alive`) and whether that normal is more than -0.866 along
    // `facing` (`not_opposite`; rounded to a float first). Whether one was found.
    bool find_vert_target(const maths::Vec4* direction, const maths::Vec4* facing, bool* not_opposite,
                          maths::Vec4* point, maths::Vec4* normal, bool* object_alive);
    // 0x004FF200: the speed override (+0x3AC, a factor; 1 when off): turned on (+0x255, not in an acid drop or a
    // bail) when faster than 1.25 times the 0x2EACDDB3 stat - the factor the speed over that (at most 0xC6B38BE0),
    // the limits (+0x354, +0x358) lifted. Running down at 0xF9B006AA a second; while above 1 the max speed (+0x354)
    // the stat times the factor (no faster than now x 1.1 off the air, nor the max), +0x358 its share of the stat
    // (every tenth time a line printed - compiled out). Below 1 or under the stat: off (+0x3AC 1, +0x350 0).
    void update_speed_override();
    // 0x004FF470: the gravity: Physics_... 0xFAA40754 over the script value 0xC31CA696 (in vert air or an acid
    // drop: 0x441C38A0); the moon cheat (0x9C8C6DF1: its global flag set, 0x007CE4FD) times 0x0EC128F0; while
    // falling, times the script value 0xBE33A41E when that's set. Returned unrounded when multiplied last.
    double get_gravity();
    // 0x004FF590 (RET 4): `velocity` made to point along the facing (or against it, when it was going backwards),
    // keeping its length - unless it's barely moving.
    void get_along_facing(maths::Vec4* velocity);
    // 0x004FF670: a wall push-off: the button (pad +0x1A0) held, 0x0017D543 ms (a script value) since the last one
    // (+0x4E4) and since +0x4E0, facing into the wall (+0x130) more than the angle 0x1483FD01 - 1 degree allows:
    // event 0x4C03635B, then (unless cancelled, flag +0xB8: cleared) the velocity bounced off the wall, its speed
    // less 0x1112FB1C (at least 0xB78542C2; nearly still: 0xB78542C2 backwards), turned into the ground's plane, the
    // skater facing along it, up the ground normal; +0x4E4 the time. Whether it pushed off.
    bool wall_push_off();
    // 0x004FF920: going backwards faster than 0x2BF71EEB (not bailing, skitching or braking): turned round (the
    // facing, right row and display matrix's two rows reversed; 0x3A8D6AB2 run unless 0xB1C84B5A) - except on a
    // vehicle or a balance trick 0xEF24413B / 0x0AC90769 with another board (the trick component's +0x1331 name):
    // 0xA496AEBD run once (+0x304).
    void turn_round_if_backwards();
    // 0x004FFB10: the jump button (flag +0x20) let go (pad +0x1C0 up): the time it was held (+0x378), the flag
    // off, the release taken, event 0x8FFEFB28. Whether it was.
    bool jump_released();
    // 0x004FFB90: a flip spin (pad +0x160 or +0x180 held, not bailing, none running): the first of the 0xB75D73E5
    // entries whose button sequence (two from the entry, the third by which button) the trick component matches;
    // the model turned about x / y / z by its angles over 50 (+0x2E0), +0x2DB / +0x3A4 / +0x2E4 on, +0x2DC on a
    // half turn about y, and its 0xD8FA65D7 script run with it.
    void check_flip_spin();
    // 0x004FFDB0 (RET 4): stepping up onto what's at `point`: a feeler down from 0x786B3272 above (the higher of
    // the skater and the point) to the lower, hitting ground (normal y over 0.5), then one across from above the
    // point to above the hit (the normal added): clear - the skater put there, 0.1 up. Not while bailing. Whether
    // it stepped up.
    bool step_up(const maths::Vec4* point);

private:
    // (Parts of 0x0050DDF0: still on the rail - along the segment, the matrices, the move and the feeler ahead; and
    // what every way out of the update does.)
    void grind_along(objects::GrindPoint* start_node, objects::GrindPoint* go_on_to, bool no_next_segment,
                     float overshoot);
    void finish_rail();
    // (Parts of 0x0050DDF0: on a moving rail, carried along with it; a grind's or a slide's balance.)
    void ride_moving_rail();
    void update_grind_balance();
    // Not original: THAW's stall state's update (THAW 0x005E2E30 - core_rail.cpp, game/rider/thaw_bmx.h).
    void do_bike_stall_physics();
    // (Part of 0x0050EE70: the move along the ground, once more for the rest when stopped short.)
    void move_on_ground(maths::Vec4 step);
    // (Part of 0x0050F830: off the wall's edge - the up straight, pushed out 18, into the air.)
    void off_wall_edge(bool reset_display);

    // (Part of 0x0050CA00: landing on something skatable - the velocity into the ground's plane, the ground
    // normal as the new up.)
    void land_from_air();

public:
    // The fields after the base's (+0x18-+0x153B). Other components and the network code read a few of them.
    maths::Mat4 display_matrix_;    // +0x18 the matrix the skater is drawn with
    maths::Vec4 drawn_normal_;      // +0x58 the ground normal the display matrix leans to (blended from old_normal_)
    maths::Vec4 floor_normal_;      // +0x68 the ground normal: the skater's up on the ground
    maths::Vec4 old_normal_;        // +0x78 the ground normal before the last change
    float normal_blend_;            // +0x88 old_normal_ to floor_normal_: 1 at a change, run down to 0
    float frame_length_;            // +0x8C the frame's length (seconds, slow motion included)
    int state_changed_at_;          // +0x90 the game clock when the physics state last changed
    int old_state_;                 // +0x94 the physics state before it
    maths::Vec4 safe_pos_;          // +0x98 a position to go back to (where the frame began)
    render::ProbeRay feeler_;       // +0xA8 the collision feeler every check uses
    render::ProbeRay floor_feeler_; // +0x158 a copy of the feeler at the last ground hit
    maths::Vec4 probe_start_;       // +0x208 the feeler's line (probe_touches_zone)
    maths::Vec4 probe_end_;         // +0x218
    uint8_t hit_skatable_;          // +0x228 set by probe_touches_zone: the face hit is skatable
    uint8_t hit_vert_;              // +0x229 ... vert (face flag 8)
    uint8_t hit_wall_;              // +0x22A ... a wall (face flag 4)
    uint8_t pad22b_[1];
    objects::GrindPoint* rail_node_;      // +0x22C the rail node being ground (or last ground)
    objects::GrindRailHub* rail_manager_; // +0x230 ... and its rail manager
    int rail_time_;                       // +0x234 the game clock when the skater last got on or off a rail
    int lip_time_;                        // +0x238 the game clock in the lip state (each update)
    int rail_wait_;                       // +0x23C ms before the same rail may be ground again
    uint32_t rail_script_node_;           // +0x240 the name of the rail node whose TriggerScript last ran
    uint32_t last_rail_script_;           // +0x244 (the same, kept for the next)
    uint8_t on_ledge_;                    // +0x248 the rail is a ledge (the Ledge member function)
    uint8_t bad_ledge_;                   // +0x249 a bad ledge (BadLedge)
    uint8_t skitch_moving_;               // +0x24A moving between skitch points
    uint8_t pad24b_[1];
    int skitch_point_;            // +0x24C the skitch point held (its index on the car)
    float skitch_direction_;      // +0x250
    uint8_t landed_this_frame_;   // +0x254
    uint8_t frame_began_in_drop_; // +0x255 the frame began in an acid drop (the state's +0x78)
    uint8_t frame_began_in_lip_;  // +0x256
    uint8_t pad257_[1];
    maths::Vec4 lip_pos_;          // +0x258 where the lip trick is (w 1; (0, 0, 0, 1) out of the lip state)
    maths::Vec4 vert_takeoff_;     // +0x268 where the skater went up the vert wall (1 lower)
    maths::Vec4 vert_wall_normal_; // +0x278 the vert wall's normal
    maths::Vec4 vert_facing_;      // +0x288 the facing up the wall, y reversed
    float vert_step_;              // +0x298 the step the vert air feeler searches up by (6 when it starts, halved)
    uint8_t unknown29c_[0x10];
    maths::Vec4 side_normal_sum_; // +0x2AC the side feelers' hit normals added up
    float side_nearest_;          // +0x2BC the nearest side hit's distance
    float side_reach_;            // +0x2C0 the side collisions' reach (SetExtraPush radius; 0: off)
    float side_push_;             // +0x2C4 their push (SetExtraPush speed)
    float side_turn_;             // +0x2C8 their turn a second (SetExtraPush 0xDFDFEAB8)
    uint8_t unknown2cc_[0x4];
    uint8_t transfer_blocked_; // +0x2D0 a spine transfer was blocked: break_vert tries again
    uint8_t pad2d1_[3];
    uint32_t nearest_wall_side_; // +0x2D4 which end or side of the board has the nearest wall (0: none)
    uint8_t powerslide_;         // +0x2D8 EnterPowerslide / ExitPowerslide
    uint8_t auto_kick_;          // +0x2D9 ForceAutoKickOn / Off
    uint8_t tap_spin_;           // +0x2DA spins by taps (half a turn each)
    uint8_t model_rotating_;     // +0x2DB a timed model rotation (a flip spin) is on
    uint8_t model_flipped_;      // +0x2DC ... a half turn about y
    uint8_t pad2dd_[3];
    float model_rotation_left_; // +0x2E0 ... its frames left
    uint8_t flip_spin_started_; // +0x2E4 a flip spin started this air (the trick component's sequence check)
    uint8_t pad2e5_[3];
    float slomo_speed_; // +0x2E8 SetSlomo's speed (1): the model rotation's step a frame, the trick windows' stretch
    uint8_t point_rail_spin_; // +0x2EC in a point rail spin
    uint8_t on_vehicle_;      // +0x2ED SkaterVehicleOn / Off
    uint8_t pad2ee_[2];
    uint32_t vehicle_;       // +0x2F0 SetSkaterVehicle
    uint8_t projectiles_on_; // +0x2F4 ProjectilesOn / Off
    uint8_t pad2f5_[3];
    int ammo_;                  // +0x2F8 the projectile count
    uint32_t ammo_type_;        // +0x2FC SetAmmoType
    uint8_t in_revert_;         // +0x300 InRevert / OutRevert
    uint8_t focus_on_;          // +0x301 FocusOn / FocusOff (the special's time runs)
    uint8_t motoskateboard_;    // +0x302 moto_skateboard_on / Off
    uint8_t crouch_stats_;      // +0x303 push with the crouched stats
    uint8_t manual_turnaround_; // +0x304 the backwards script ran (EndManualTurnaround clears it)
    uint8_t pad305_[3];
    int projectile_input_; // +0x308 -1: none
    float queued_spin_;    // +0x30C tap spins queued (degrees)
    uint8_t no_kick_;      // +0x310 kicking forbidden (can_kick_on / CanKickOff)
    uint8_t can_brake_;    // +0x311 CanBrakeOn / Off
    uint8_t pad312_[2];
    float rolling_friction_;      // +0x314 Physics_Rolling_Friction, or a special friction
    int special_friction_index_;  // +0x318 SetSpecialFriction's next entry (also the free reverts pending)
    int special_friction_start_;  // +0x31C the game clock of the first (and of the last free revert)
    float special_friction_time_; // +0x320 its duration left (0: none)
    uint8_t braking_;             // +0x324
    uint8_t pad325_[3];
    int side_hit_times_[4];       // +0x328 the game clock when each side feeler (0-3) last hit
    maths::Vec4 last_ground_pos_; // +0x338 GetLastGroundPos
    float last_heading_;          // +0x348 degrees from the facing (GetLastGroundHeading)
    uint8_t rail_backwards_;      // +0x34C the way round along the rail (flipped by turn_around)
    uint8_t pad34d_[3];
    float limits_time_;     // +0x350 overridelimits: the time left (0 off, -1 for ever, -2 for the level)
    float limits_max_;      // +0x354 the max speed while overridden
    float limits_max_max_;  // +0x358
    float limits_friction_; // +0x35C
    float limits_gravity_;  // +0x360
    objects::SharedRef<objects::GameEntity> skitch_target_; // +0x364 the object being skitched on
    uint8_t no_spin_;                                       // +0x370 no_spin / Canspin
    uint8_t bail_on_;                                       // +0x371 BailOn / Bailoff
    uint8_t came_down_off_spine_;                           // +0x372
    uint8_t landed_from_vert_;                              // +0x373 LandedFromVert (scripts clear it)
    uint8_t landed_from_air_vert_;                          // +0x374 the last landing was from vert air or an acid drop
    uint8_t allow_lip_no_grind_;                            // +0x375 AllowLipNoGrind
    uint8_t pad376_[2];
    int jump_charge_ms_;       // +0x378 how long the jump was held (ms), up to the max tense time
    float drop_target_height_; // +0x37C the acid drop's height (DropHeight)
    uint32_t air_start_;       // +0x380 the game clock when the skater went into the air
    uint32_t air_end_;         // +0x384 ... and came out of it
    uint8_t lock_velocity_;    // +0x388 LockVelocityDirection: turns leave the velocity alone
    uint8_t pad389_[3];
    maths::Vec4 wall_normal_;    // +0x38C the wall ridden or planted on
    int wall_ride_start_;        // +0x39C the game clock when the wall ride began
    int wallplant_end_;          // +0x3A0 the game clock when the last wallplant ended
    uint8_t no_rail_tricks_;     // +0x3A4 NoRailTricks (also while a flip spin ends)
    uint8_t last_spin_backside_; // +0x3A5 the last spin's way
    uint8_t pad3a6_[2];
    int rail_adjust_;                      // +0x3A8 change_rail_adjust (ms)
    float speed_override_;                 // +0x3AC the speed override's factor (1: off)
    maths::Mat4 drop_matrix_;              // +0x3B0 the acid drop's orientation
    maths::ArcRotationBlender drop_slerp_; // +0x3F0 to it
    float drop_time_;                      // +0x488 the time into the drop
    float drop_duration_;                  // +0x48C
    maths::Vec4 drop_heading_;             // +0x490
    maths::Mat4 drop_last_matrix_;         // +0x4A0
    uint32_t wallplant_time_;              // +0x4E0 the game clock of the last wallplant
    uint32_t last_push_off_;               // +0x4E4 the game clock of the last wall push-off
    uint8_t vert_wallplanted_;             // +0x4E8 a vert wallplant was done
    uint8_t pad4e9_[3];
    int leap_time_;              // +0x4EC the game clock when the skater last jumped
    uint8_t vert_air_announced_; // +0x4F0 the skater was announced as in vert air (0xF225FE69; 0x5E27200A when it ends)
    uint8_t pad4f1_[3];
    render::HitMeshCache hit_cache_; // +0x4F4 the collision cache round the skater
    uint8_t drop_object_hit_;        // +0x14C0 the acid drop's object was still there
    uint8_t focus_held_;             // +0x14C1 the focus button toggled bullet time until it's let go
    uint8_t pad14c2_[2];
    int throw_wait_;              // +0x14C4 frames until the next throw
    uint8_t throwing_projectile_; // +0x14C8 SetThrowingProjectile
    uint8_t pad14c9_[3];
    int late_wallplant_frames_;       // +0x14CC frames left to press the wallplant late (cleared out of the air)
    maths::Vec4 late_velocity_;       // +0x14D0 the velocity, position and wall when a late wallplant was armed
    maths::Vec4 late_pos_;            // +0x14E0
    maths::Vec4 late_wall_normal_;    // +0x14F0
    uint8_t natas_counter_clockwise_; // +0x1500
    uint8_t pad1501_[3];
    int natas_spin_time_; // +0x1504 the game clock of the natas spin's last update
    uint8_t tantrum_;     // +0x1508 EnterTantrum / ExitTantrum
    uint8_t tagging_;     // +0x1509 EnterTagging / ExitTagging
    uint8_t pad150a_[2];
    objects::PadInputModule* input_;                      // +0x150C the object's components (seal)
    objects::ActivationZoneModule* trigger_;              // +0x1510
    objects::RiderSfxPlayer* sound_;                      // +0x1514
    objects::MoveExecutor* trick_;                        // +0x1518
    objects::RiderTurning* rotate_;                       // +0x151C
    objects::RiderPointKeeper* score_;                    // +0x1520
    objects::RiderTeeterMoves* balance_;                  // +0x1524
    objects::RiderPoseState* state_;                      // +0x1528
    objects::MovingSurfaceModule* ride_;                  // +0x152C
    objects::RiderBoardPhysics* physics_control_;         // +0x1530
    objects::FootMovement* walk_;                         // +0x1534
    objects::TaggingControlComponent* tagging_component_; // +0x1538 the projectiles'
};

// 0x004FC4A0 (cdecl): with the script value 0x19FB78FA set, "Tracking%d" and two vectors printed (compiled out).
void tracking_print(int index, const maths::Vec4* a, const maths::Vec4* b);

extern maths::Vec4 g_acid_hold;

} // namespace skater

namespace skater {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern uint32_t g_core_member_functions[660];
} // namespace skater
