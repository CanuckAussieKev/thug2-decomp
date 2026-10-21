#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script
namespace Mth {
class Vector;
} // namespace Mth

// The skater's core physics component (on the ground, in the air): the pieces PartyMod changes for its
// PS2-style controls (thug2-decomp.ini [Miscellaneous] UsePS2Controls - Skater::Ps2Controls()). Its layout is only partly
// known: fields are reached by offset (see core_physics.cpp and rotation.cpp).
namespace Skater {

class CorePhysicsComponent {
public:
    // --- rotation.cpp: turning in the air ---
    // 0x00504060: spin in the air. Tap mode (+0x2DA): each tap of the left / right spin button (L1/R1: input
    // +0xA0 / +0x100) queues half a turn (+0x30C), turned off at the skater's spin speed. Otherwise holding one
    // turns once it's been held longer than the spin delay - PartyMod's PS2 controls drop the delay (its NOPs
    // at 0x00504239 / 0x0050430B); then the second pair of spin buttons (input +0x60 / +0x80), ramping up
    // between two held times. With no spin, the
    // skater may be turned back to face along the ramp. The turn goes into the object's matrix, the display
    // matrix and the trick's rotation total (in degrees, shown as an integer).
    void Spin();
    // 0x00507120: break out of vert - both of two buttons held (input +0xC0 / +0x120, PartyMod's "R2/L2"; its
    // PS2 controls take either: its bytes at 0x00507184 / 0x005071AE), or a flag (+0x2D0), or `force`, or the
    // jump button held past a script time with no other button: pushes the skater away from the wall and
    // pitches them forward.
    void BreakVert(bool force);

    // --- core_physics.cpp ---
    // 0x004FCBA0 (a script member function: neither argument is used): push off out of a lip trick. The
    // skater's facing, flattened (x, z of the object's matrix row at +0x8C, normalised), picks the push:
    // both triggers (+0xC0 and +0x120 of the input; either one under PartyMod's PS2 controls) or button +0x60
    // held long enough - along the facing; +0x20 or +0x40 held long enough - sideways one way or the other.
    // Button +0x80 held, or nothing pushed: returns 0 and leaves the velocity alone. 1 otherwise.
    int LipPushOff(Script::CStruct* params, Script::CScript* script);

    // 0x004FCE90 (the script member function SetSpeed): the skater's speed set to the unnamed float (0 without one)
    // - the velocity's direction kept; with almost none (its length squared below 0.001), the facing's instead
    // (the object's matrix row at +0x8C, all four components). The fourth component is scaled with the rest. 1.
    // Not original: [features] debug_rail_speed=1 logs the calls of the rail speed-up loops (the THAW ports'
    // RailAccelerate2, the level editor's ed_RailSpeedLoop) - when one starts, then once a second.
    int SetSpeed(Script::CStruct* params, Script::CScript* script);

    // 0x00509BD0: keep to the ground while skating. A feeler along the skater's up axis looks for the ground
    // below; on a hit the skater sticks to it (unless it's too steep or too far for a hit on a wall), otherwise
    // it leaves the ground - into the air, or into an acid drop if the physics finds one while both triggers
    // are held (either one under PartyMod's PS2 controls).
    void CheckForGround();

    // 0x0050CA00: the air update. The rotation and other parts (0x00504910, Spin, ...), the move (velocity and
    // gravity over the frame, plus what the skater rides on), wall riding (off the wall: BreakVert) and wall
    // feelers, then the landing feeler from the last position to the new one: onto something skatable
    // (LandFromAir) or pushed off it; else the air checks and an acid drop if the physics finds one while both
    // triggers are held. PartyMod's PS2 controls take either trigger in the three trigger checks.
    void DoAirPhysics();

    // --- core_physics_state.cpp: the component made, reset and destroyed; timers ---
    // (Fields: core_physics_fields.h. The component is 0x153C bytes, type 0x5BD63B29 SkaterCorePhysics, vtable
    // 0x0064BC48: 0 Destroy, 1 its update 0x00510830, 12 CallMemberFunction 0x00501600.)
    // 0x00505440 (the component type's maker): a new one (0 if out of memory).
    static void* __cdecl Create();
    // 0x00500CA0: the base's constructor, then its vtable and type; its two feelers (+0xA8, +0x158) and its collision
    // cache (+0x4F4) made; no components (+0x150C-+0x1538); a few flags cleared, +0x308 = -1, +0x2E8 = 1. The rest
    // isn't initialised. Returns this.
    CorePhysicsComponent* Construct();
    // 0x00500DB0 (vtable slot 0): the destructor body (0x004FC500), then freed with `flags` bit 0. Returns this.
    CorePhysicsComponent* Destroy(unsigned int flags);
    // 0x00506BE0: back to rest - the old position and +0x98 = the position (not in a network game), no velocity,
    // the ground state, every timed flag set and cleared (flag 7 telling +0x1514's +0x1E so), the ride let go of,
    // and the speed / trick timers and flags cleared.
    void Reset();
    // 0x00506BA0: `feeler` copied as the last ground hit (+0x158); +0x1514's +0x1D = +0x1AA.
    void SetLastGroundFeeler(const void* feeler);
    // 0x00500710: a free revert earned when Physics_Time_Before_Free_Revert seconds have passed since +0x31C while
    // +0x318 (the ones pending) isn't 0 ("You earned a free revert!!!!"); the next one timed from now.
    void UpdateFreeRevert();
    // 0x005007A0: the focus button (pad +0xE0) held longer than focus_button_pressed_time: toggle_bullettime run
    // (once until it's let go: +0x14C1).
    void UpdateFocusButton();
    // 0x00500820: projectiles (+0x2F4 on, +0x2F8 of them, not in states +0x70 / the +0x1538 component's): the
    // throw (0x004FC3C0) at most every 30 frames (+0x14C4) - then walk_ or skate_maybe_throw_projectile run.
    void UpdateProjectiles();
    // 0x00500900: a timed model rotation (+0x2DB on, +0x2E0 left, +0x2E8 a frame): counted down in the air (+0x3A4
    // cleared at 20 or less); at 0 or less, or out of the air at once, the model's rotations reset (0x004208D0;
    // with +0x2DC, the SkaterFlipAndRotate component's 0x005114F0 first).
    void UpdateModelRotation();
    // 0x00500BC0: a special's time (+0x301 on; the score's first float) counted down by the frame; at 0 or less,
    // KillSpecial run with TimeLimit.
    void UpdateSpecialTime();
    // 0x00500DE0 (the script member function GetAirTimeLeft): AirTimeLeft = the time until the skater falls back
    // to the height it took off from (+0x11C of the flags block), from its upward speed and the gravity: 0 if
    // that's in the past, 0.00001 if it never gets there. Added to the script's parameters. 1.
    int GetAirTimeLeft(Script::CStruct* params, Script::CScript* script);

    // 0x005004B0: a grind's trick: on the motoskateboard (+0x302) Trick_Motoskateboard_Grind, else
    // GrindTrickList[`railType` (0-8, else 0)][the four buttons as bits]; set on the skater's script (made if it
    // has none) and run at once; flag +0xE0.
    void StartGrindTrick(uint32_t railType, bool trick1, bool trick2, bool trick3, bool trick4);

    // --- core_physics_speed.cpp: friction, brakes, the speed limits ---
    // 0x00501850: the horizontal speed limited - to the max-max speed stat (+0x358 while +0x350's timer runs), and
    // above the max speed stat (+0x354) slowed by Physics_Heavy_Air_Friction (0x004FE1F0); the timers +0x350 (not
    // at -1 / -2) and +0x320 (then the rolling friction +0x314 back to Physics_Rolling_Friction) counted down.
    void LimitSpeed();
    // 0x005019E0: braking (+0x324): unless the skater may turn on the slope instead (0x004FE420), the speed cut
    // by Physics_Brake_Acceleration a second - to 0 when less than two frames' worth is left.
    void Brake();
    // 0x00506DF0: on the ground, the friction (0x004FE6A0), then either the turn down a slope (0x00501B00) or the
    // rolling friction (0x004FE730) - only while +0x2D9 or flag +0x70 is set or the rolling friction isn't the
    // physics value.
    void ApplyGroundFriction();
    // 0x005021D0: unless flag +0x88 is set, a ride on the object the feeler hit (0x00427CD0: true when one starts);
    // a new ride makes the velocity relative to it (its velocity taken off).
    void StartRidingHitObject();

    // --- core_physics_feelers.cpp: the feelers ---
    // 0x005003A0: the feeler (+0xA8) along its line (+0x208 to +0x218), faces with any of `ignore` or without all of
    // `require` passed over, movables too. On a hit: +0x228 skatable (face flag 1, or 8 vert, or neither 2 nor 4 and
    // no steeper than Wall_Non_Skatable_Angle), +0x229 vert (flag 8), +0x22A a wall (flag 4). Whether it hit.
    bool CheckFeeler(uint32_t ignore, uint32_t require);
    // 0x00500020: whether the skater may grind `rail` (a rail node; +0x40 / +0x44 its neighbours) now: not the
    // last one (+0x22C) or its neighbours again while still in the air after it; a new rail sets flag +0x50 (in a
    // created park: one whose ends are all more than 6 from the last's); the same one only after +0x23C ms since
    // +0x234. Then, unless `anyTime`: not in state 4, and on a vert wall (flag +0x38) only moving up. (The second
    // argument isn't used.)
    bool CanGrindRail(const void* rail, const void* unused, bool anyTime);
    // 0x005012B0 (the script member function NearGround): whether there's ground within 20 above or below a point
    // 16 to the skater's side (with Right: the other side). 1 or 0.
    int NearGround(Script::CStruct* params, Script::CScript* script);
    // 0x00501600 (vtable slot 12: a member function by name): Ledge - +0x248; Obj_MoveToNode - the motion
    // component moves the skater, then the display matrix, the shadow, +0x98, the old position and the camera follow
    // (teleported); without NoReset it stops and lands, with orient its horizontal speed turns to the new facing.
    // 2 for any other name.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x005009C0: which end or side of the board (nose / tail 12 along the forward row, left / Right 12 along the
    // side row) has the nearest wall: +0x2D4 (0 if none).
    void FindNearestWallSide();
    // 0x00501C90: one side collision feeler (`side` 0-3) from `start` to `end`, unless the opposite side hit more
    // than 500 ms ago: a hit's normal added to +0x2AC, its distance kept in +0x2BC if nearer, the side's time noted.
    void CheckSideCollision(const Mth::Vector* start, const Mth::Vector* end, uint32_t side);
    // 0x005055B0: the side collisions (+0x2C0 the reach and +0x2C4 the push, both set): four feelers at
    // Skater_side_collide_height, both ways along the side and forward rows; with a hit, the skater pushed off (the
    // nearer, the harder) and turned (+0x2C8 a second) along a wall it faces into.
    void CheckSideCollisions();
    // 0x005054A0: the model's rotations (+0xC8 / +0x15C / +0x1F0 of the Model component: any on) applied to the
    // skater's matrix (0x004215F0), the velocity turned with its rotation about up, then reset. Whether any was on.
    bool ApplyModelRotations();

    // --- skitch.cpp: skitching ---
    // 0x00503EC0: the grab button held longer than skitch_hold_time on the ground (not driving, not skitching): the
    // nearest skitch point of another object within Skitch_Max_Distance grabbed (+0x24C its index, +0x364 the car);
    // both told Skitched.
    void StartSkitch();
    // 0x00501DB0: skitching: the button pairs move to the next skitch point along the car; the skater held
    // Skitch_Offset behind the point (pulled towards a new one at skitch_suck_speed), turned as the car. Without
    // a car, or 180 or more away: OffMeterBottom.
    void Skitch();

    // --- core_physics_turning.cpp: turns ---
    // 0x005002D0: jumping off a rail with pad +0x40 / +0x60 held: the velocity and matrices turned by
    // Rail_Jump_Angle one way or the other.
    void TurnForRailJump();
    // 0x00503560: turning on the ground (pad +0x40 / +0x60) at Physics_Ground_Rotation - or with +0x20 held
    // Physics_Ground_Sharp_Rotation, ramping up over 600 ms below a speed of 10; the velocity too unless +0x388.
    void TurnOnGround();
    // 0x005039F0: bouncing off a wall of normal `normal`: the angle between the side row and the normal (acos;
    // past 90 degrees, less 180) times Wall_Bounce_Angle_Multiplier and `scale` turns the velocity and the matrices
    // (`turn` = the turn). Returns the angle.
    float BounceOffWall(const Mth::Vector* normal, float* turn, float scale);
    // 0x00501B00: when the skater may (0x004FE420), turned towards straight down the slope at
    // Skater_Slow_Turn_on_slopes (along the velocity, or the facing when hardly moving). Whether it did.
    bool TurnDownSlope();
    // 0x00504730: the natas spin: its rate from natas_spin_factor less how far off balance; the trick's rotation
    // total and its display (as Spin); with CHEAT_PERFECT_RAIL the balance held. +0x1504 = now.
    void NatasSpin();
    // 0x00504910: leaning in the air (pad +0x20 / +0x40) by Physics_Air_Lean_stat, after Physics_Air_No_Lean_Time
    // and ramping up until Physics_Air_Ramp_Lean_Time.
    void LeanInAir();
    // 0x00504A60: in an acid drop (flag +0x78): the matrices along the slerp at +0x3F0, eased, over +0x48C seconds.
    void RotateInAcidDrop();
    // 0x00504B40: tipped over in the air above ground (500 below, not vert, not too steep): flag +0x80; turned back
    // upright at Physics_recover_rate_stat.
    void RecoverInAir();
    // 0x00504DF0: leaning sideways in the air: rolled back at skater_upright_sideways_speed.
    void KeepUpright();

    // --- core_physics_walls.cpp: walls and ceilings ---
    // 0x00503710: a vert wall plant: the trick button (or within Wall_Ride_Triangle_Window) on vert (flags +0x30,
    // +0xD8) with no rotation and none done yet: the wall within Physics_vert_wallplant_distance below, moving up at
    // least the velocity limit, 60 above the take-off, steep enough (or a vert face), the trick allowed (0x0051DDF0):
    // triggers, the boost, Vert_WallPlant, +0x4E8 set. Whether it planted.
    bool VertWallplant();
    // 0x00503B30: stopped (no faster than Skate_min_wall_lean_push_speed) and leaning left / right (pad +0x40 / +0x60):
    // a feeler Skate_wall_lean_push_length out at Skate_wall_lean_push_height after Skate_wall_lean_push_time; a wall
    // there pulls the skater towards it, unless that's blocked.
    void LeanPushOffWall();
    // 0x00504EE0: a ceiling within the skater's head height (6 for Physics_Ignore_Ceilings_After_Wallplant_Duration
    // after a wall plant): the skater slid down out of it, its velocity along it. Whether there was one.
    bool KeepOffCeiling();
    // 0x005051B0: the same 18 out along +0x38C (the head over the wall's edge).
    bool KeepOffCeilingAtWall();
    // 0x00506E50: a feeler ahead along the move at Skater_First_Forward_Collision_Height: something skatable at an
    // angle to the ground pushes the skater out; else its triggers, and a bounce off it (BounceOffWallAhead).
    void CheckWallAhead();
    // 0x005059D0: bouncing off a wall ahead of normal `normal` (unless 0x004FF670): turned (BounceOffWall), slowed
    // the more head-on (beyond Wall_Bounce_Dont_Slow_Angle; a flail beyond Wall_Bounce_Dont_Flail_Speed), moved
    // back out 6 along the normal; another wall ahead then: turned back, half as fast.
    void BounceOffWallAhead(const Mth::Vector* normal);

private:
    // (Part of 0x0050CA00: landing on something skatable - the velocity into the ground's plane, the ground
    // normal as the new up.)
    void LandFromAir();
};

} // namespace Skater
