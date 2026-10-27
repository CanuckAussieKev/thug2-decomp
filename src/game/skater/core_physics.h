#pragma once

#include <cstdint>

#include "game/mth/vector.h"

namespace Script {
class CStruct;
class CScript;
} // namespace Script
namespace Obj {
struct CRailNode;
class CRailManager;
} // namespace Obj

// The skater's core physics component (on the ground, in the air): the pieces PartyMod changes for its
// PS2-style controls (thug2-decomp.ini [Miscellaneous] UsePS2Controls - Skater::Ps2Controls()). Its layout is only partly
// known: fields are reached by offset (see core_physics.cpp and rotation.cpp).
namespace Skater {

// What the acid drop search (CorePhysicsComponent::FindAcidDrop) finds (0x28 bytes).
struct AcidDrop {
    Mth::Vector point;  // +0x00 where on the wall the skater drops to
    Mth::Vector normal; // +0x10 the wall's normal there
    float hitHeight;    // +0x20 the height the search first hit it at
    bool objectHit;     // +0x24 (the feeler's hit object was still there)
    uint8_t pad25[3];
};
static_assert(sizeof(AcidDrop) == 0x28, "layout must match the original");

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
    // Not original (THAW's BMX, game/skater/thaw_bmx.h): CheckForGround's on-the-ground part for the bike on its
    // wheels - the display normal, then with `hit` the terrain, the ride, the triggers, the trick objects.
    void GroundUnderWheels(bool hit);

    // 0x0050CA00: the air update. The rotation and other parts (0x00504910, Spin, ...), the move (velocity and
    // gravity over the frame, plus what the skater rides on), wall riding (off the wall: BreakVert) and wall
    // feelers, then the landing feeler from the last position to the new one: onto something skatable
    // (LandFromAir) or pushed off it; else the air checks and an acid drop if the physics finds one while both
    // triggers are held. PartyMod's PS2 controls take either trigger in the three trigger checks.
    void DoAirPhysics();

    // --- core_jump.cpp: jumping, skitching, the script member functions around them, the wallplant state ---
    // 0x005092E0: the TriggerScript of the rail node being ground (+0x22C) - or of the nearest one before it (back
    // along m_prev, stopping at a loop) that has one - run for the three `events` (const int[3]); its name kept
    // (+0x240 / +0x244), the flag +0xC8 telling whether it's a different node than last time. Nothing without a
    // rail node, its script node or a TriggerScript.
    void TriggerRailScript(const int* events);
    // 0x00509450: jump, by the state: from the ground (the trigger check first: a trigger's script may take over),
    // from the air or a rail - the jump sound unless `params` has no_sound. The speed from the physics values
    // (boneless or not, from vert or not) between their min and max by how long the jump was held (+0x378, up to
    // the max tense time) - or `params`' speed; off vert pushed off the ramp instead (going down: stopped first);
    // then into the air (+0x4EC the time, the event SkaterJump).
    void DoJump(Script::CStruct* params);
    // 0x00509920 / 0x00509990: start skitching on the object at +0x364 (riding it, flag +0x88, the event
    // 0x35224F25 to it) / stop (the event 0x2739B86D, the pointer and the ride let go).
    void BeginSkitch();
    void EndSkitch();
    // 0x00509050: the wallplant state's update: after Physics_Wallplant_Duration into the air (a feeler along the
    // facing for a trigger), else carried along by what the skater rides on.
    void DoWallplantPhysics();
    // The script member functions (all return 1; neither argument used unless said): 0x00509A40 Jump (DoJump with
    // `params`), 0x00509A60 StartSkitch, 0x00509A70 StopSkitch, 0x00509A80 SetState (`params`' unnamed checksum:
    // AIR, Ground or Lip; off a lip runs the rail's TriggerScript, wall riding the up set straight), 0x00509B70
    // TriggerGrindOff (the rail's TriggerScript for grinding off), 0x00509B90 (0x675CF35E: break out of vert, forced,
    // and 0x00504DF0), 0x00509BB0 WalkingWallplant (0x00507500 with 0, 0, 1).
    int Jump(Script::CStruct* params, Script::CScript* script);
    int StartSkitch(Script::CStruct* params, Script::CScript* script);
    int StopSkitch(Script::CStruct* params, Script::CScript* script);
    int SetState(Script::CStruct* params, Script::CScript* script);
    int TriggerGrindOff(Script::CStruct* params, Script::CScript* script);
    int ForceBreakVert(Script::CStruct* params, Script::CScript* script);
    int WalkingWallplant(Script::CStruct* params, Script::CScript* script);

    // --- core_collision.cpp: the skater against walls ahead and to the sides ---
    // 0x00508780 (from the air update): a feeler from `from` to past the new position (raised by
    // Skater_First_Forward_Collision_Height, Skater_First_Forward_Collision_Length further along the move). On
    // something not too much like ground: true (stopped) when 0x004FFDB0 takes the skater there and it's fast upwards
    // or the wall is steep, or when snapping up onto a ledge (Physics_Air_Snap_Up, lowered in steps of 2); on vert,
    // pushed back off the wall (Skater_Min_Distance_To_Wall) and turned along it; otherwise (no wallplant, no
    // 0x00508140) a bonk: the trigger, the sound, the velocity along the wall, the facing turned away from it, and a
    // moving object's push. False when nothing stopped the skater.
    bool CheckAirForwardCollision(const Mth::Vector* from);
    // 0x0050A2C0: a feeler from the skater's side height (Skater_side_collide_height) out to one side (`side` -1 or
    // 1 along the right) `length` long (more in the air: Skater_air_extra_side_col). Never while wall riding. In the air the
    // skater is pushed out of what it hit (a tenth of the overlap) - true only when 0x00508140 took over; otherwise
    // a near-vertical wall puts the skater back (+0x98), then, unless something is above the hit or a trigger takes
    // over, onto it (true).
    bool CheckSideCollision(float side, float length);
    // 0x0050C930: both sides (Skater_side_collide_length); hit on both, the position goes back to where it was.
    void CheckSideCollisions();

    // --- core_grind.cpp: onto a rail ---
    // 0x0050A770: onto the rail `node` (of `manager`) at `point` - nothing when a feeler from the skater to the point
    // hits something more than 6 from it. A node on its own is a point rail: kicked up off it into the air
    // (Physics_Point_Rail_Kick_Upward_Angle, Point_Rail_Speed_Boost), or - holding a spin button (+0x100 / +0xA0)
    // long enough after the last - a point rail spin. Otherwise a lip trick when the skater comes up a vert ramp at
    // the rail (the Lip* angles; not with `noLipTrick`), else a grind: along the rail the way the skater goes (a
    // coin toss when across it; nothing at the rail's end), Rail_Speed_Boost, the bad ledge feelers either side, and
    // the grind trick picked from the pad. `fromObject` (from a car plant or a vehicle): the velocity isn't
    // flattened, and too slow against the facing it starts from the facing.
    void StartGrind(const Mth::Vector* point, Obj::CRailNode* node, Obj::CRailManager* manager, bool noLipTrick,
                    bool fromObject);
    // 0x0050DAE0: in the air (or `always`), grind button (+0x1A0) held, not after a wallplant for
    // Physics_Wallplant_Disallow_Grind_Duration: a rail along the last move (soon after leaving a rail, only one
    // lining up within Rail_Corner_Leave_Angle) - the level's, else each moving rail manager's (the move in its
    // object's space, the point back in the world: the skater then rides that object, less its velocity) - and,
    // when 0x00500020 allows it, onto it (StartGrind).
    void TryGrind(bool always);

    // --- core_states.cpp ---
    // 0x0050BF50: the lip state's update: carried by what the skater rides (0x00427A00: the position and +0x258),
    // the lip's balance (Perfect Rail cheat: its global flag, no lean), button +0x1C0 to flag +0x20, getting off it
    // (0x004FFB10: the rail's TriggerScript twice), the rail node's Name, +0x238 the time.
    void DoLipPhysics();
    // 0x0050C670 (from the skater when it gets back on its board): on the ground or into the air (+0x94 the same;
    // in the air flag +0xC0, and +0xB0 for +0x1534's +0x9B), the matrices and ground normals from the object, the
    // positions kept, the jump / trick state cleared; then onto a rail (`grind`: a point, +0x10 its manager, +0x14
    // its node, +0x18 an object to ride - the old ride let go) or an acid drop (0x00502F20).
    void ResumeSkating(bool onGround, const uint8_t* grind, void* acidDrop);

    // --- core_rail.cpp: grinding ---
    // 0x0050DDF0: the rail state's update (state 4). Carried along by a moving rail; off the rail when 0x004FFB10
    // says so (Rail_jump_rerail_time, flags +0xD0 / +0xD8); a grind / slide's balance (the Perfect Rail cheat). At the
    // rail's last node: 0x00504730. Otherwise the rail's gravity along the segment (the way round flips +0x34C when
    // it turns the skater back), the next segment's corner (Rail_Corner_Leave_Angle; only nodes flagged 4), past the
    // segment's end onto the next one or TransferOrLeaveRail; still grinding: the velocity, the facing (turned 0.3 of
    // the way, display and object matrices rebuilt), the move with what went past the end, and a feeler ahead that
    // throws the skater off the rail on a hit (OffRail). Then the ground normal, the score's rail time (0x00564270),
    // the node's Name.
    void DoRailPhysics();
    // 0x0050C200: at the end of a rail (`end`: where it ends): onto another rail along the move (FindGrindRail within
    // the corner angle, the wall side of this rail; not this rail's next or last node outside the park editor) when
    // the skater is between its two nodes - or, outside the park editor, when the move is at least as long as its
    // segment (then onto its middle) - the velocity along it, the skater at its point; else off the rail into the
    // air (OffRail).
    void TransferOrLeaveRail(const Mth::Vector* end);

    // --- core_ground.cpp ---
    // 0x0050EE70: the ground state's update (state 0). Off vert, no rail, the heading (+0x348, degrees from the facing,
    // FastACos), the velocity turned into the ground's plane when moving, Physics_Ground_Gravity (or +0x360 going up
    // while +0x350 is set) along the ground; the balance trick (none: 0x004FE320 / 0x005019E0 or 0x004FE490 /
    // 0x004FE530; a skitch or a manual: its balance and its Perfect cheat); 0x00506DF0, 0x005055B0. Skitching: the
    // skitched object's velocity (skitch_speed_match) and no move of its own. Carried by what the skater rides on.
    // Then the move, kept to the ground (CheckForGround - skitching: a feeler down the up for the terrain); stopped
    // short on the ground, the rest of the move once more along the velocity. Still on the ground: 0x00503560,
    // 0x004FF590, the side collisions and 0x00503B30 (not skitching), 0x004FF920, 0x004FFB10 (off: 0x004FEBA0, flags
    // +0x48 / +0xD8), 0x00522370 with +0x1B0.
    void DoGroundPhysics();

    // --- core_wallride.cpp ---
    // 0x0050F830: the wall ride state's update (state 2). No rail, +0x39C the time; carried by what the skater rides
    // on; the move under Wall_Ride_Gravity; turned along the wall (Wall_Ride_Turn_Speed, about the wall's normal +0x38C)
    // when not heading up it enough. A feeler ahead along the move: onto the top of the wall (a hit facing up: the
    // ground, TryGrind first) or round a corner (another wall) - or off the wall's edge (0x005051B0 too). Then a
    // feeler down the up: the wall still there (at it again; not a wall: pushed off it), or gone (off, pushed out
    // 18). Button +0x1C0 -> flag +0x20; jumping off (0x004FFB10): out (Wall_Ride_Jump_Out_Speed) and up
    // (Wall_Ride_Jump_Up_Speed), upright.
    void DoWallRidePhysics();

    // --- core_update.cpp ---
    // 0x00510830 (vtable +0x04): the frame. Its length (+0x8C, slow motion included); the frame's flags (+0x254
    // landed this frame, +0x256 began in a lip, +0x255 the timed flags' +0x78); button +0x1C0 -> flag +0x20; the
    // speed limited; flags +0x98 / +0xA0 cleared; the state's update (ground, air, wall ride, lip, rail, wallplant);
    // TryGrind (inlined in the original); free revert, focus, projectiles, special time; event 0xF225FE69 /
    // 0x5E27200A when +0x4F0 (in the air with flag +0x30, not +0x78) changes; +0x14CC cleared out of the air.
    void Update();

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
    // 0x00506BD0 (the script member function CancelRotateDisplayandUpdateMatrix): the model's rotations applied and
    // reset (ApplyModelRotations); whether any was on, 1 or 0.
    int CancelRotateDisplayandUpdateMatrix(Script::CStruct* params, Script::CScript* script);
    // 0x00500670 (from the frame's update): the collision cache (+0x4F4) refilled for a box round the skater - 180
    // either way along x and z, 204 below and 96 above - and made the feelers' current cache (0x00691974).
    void UpdateCollisionCache();
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
    // 0x00500E80 (the script member function CanKick): whether the skater may push off now (0x004FE490), 1 or 0.
    int CanKick(Script::CStruct* params, Script::CScript* script);
    // 0x00500E90 (the script member function OrientToNormal): the ground normals (+0x58, +0x68) set to the skater's
    // up, then the skater turned onto the feeler's last hit normal (+0x130: 0x004FE860). 1.
    int OrientToNormal(Script::CStruct* params, Script::CScript* script);

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
    // 0x00500F10 (the script member function SkateInAble): whether there's vert (+0x229) to skate into. With Lip: a
    // feeler down SkateInAble_LipDownOffset from the position less SkateInAble_LipHorizOffset along up, then (missed)
    // one down SkateInAble_LipExtraCheckDownOffset from the position less SkateInAble_LipExtraCheckHorizOffset along
    // up; else one across the skater, from SkateInAble_HorizOffset along its side row (and SkateInAble_DownOffset
    // down) to as far the other way - towards that side with left. 1 or 0.
    int SkateInAble(Script::CStruct* params, Script::CScript* script);
    // 0x005013E0 (the script member function SetAmmo): the projectile count (+0x2F8) set to the unnamed integer (kept
    // without one); ammo_meter_change respawned on the skater with count = the change.
    int SetAmmo(Script::CStruct* params, Script::CScript* script);
    // 0x005014E0 (the script member function CheckNatasExitDirection): the skater's matrix set to its camera's,
    // turned about Y for `dir` (the D-pad's eight ways): Down not at all, Right / left a quarter turn, DownRight /
    // DownLeft an eighth, UpRight / UpLeft three eighths, Up and anything else half a turn. 1.
    int CheckNatasExitDirection(Script::CStruct* params, Script::CScript* script);
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
    void CheckSideFeeler(const Mth::Vector* start, const Mth::Vector* end, uint32_t side);
    // 0x005055B0: the side collisions (+0x2C0 the reach and +0x2C4 the push, both set): four feelers at
    // Skater_side_collide_height, both ways along the side and forward rows; with a hit, the skater pushed off (the
    // nearer, the harder) and turned (+0x2C8 a second) along a wall it faces into.
    void CheckSideFeelers();
    // 0x005054A0: the model's rotations (+0xC8 / +0x15C / +0x1F0 of the Model component: any on) applied to the
    // skater's matrix (0x004215F0), the velocity turned with its rotation about up, then reset. Whether any was on.
    bool ApplyModelRotations();

    // --- skitch.cpp: skitching ---
    // 0x00503EC0: the grab button held longer than skitch_hold_time on the ground (not driving, not skitching): the
    // nearest skitch point of another object within Skitch_Max_Distance grabbed (+0x24C its index, +0x364 the car);
    // both told Skitched.
    void TryStartSkitch();
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

    // --- core_physics_wallplant.cpp: wall plants and wall rides ---
    // 0x00507500: a wall plant off what the feeler hit: in the air, not too soon after the last (the
    // Physics_Disallow_Rewallplant_Duration / Physics_Wallplant_Delay / Physics_Min_Wallplant_Height checks), with a
    // Wallplant_Trick pressed - else Physics_late_wallplant_frames to press it `late` (the skater backed off 24);
    // `walking`: a feeler 36 behind the facing instead. At the approach angle allowed the skater bounces off (speed
    // less Physics_Wallplant_Speed_Loss, at least the minimum exit speed; up at the vertical exit speed), rides what it
    // hit, is set Physics_Wallplant_Distance_From_Wall off it, upright: Wallplant, state 5. `checkOnly`: whether it
    // could, nothing done. Whether it planted.
    bool WallPlant(bool late, bool checkOnly, bool walking);
    // 0x00508140: starting a wall ride on the wall the feeler hit (+0x22A): triangle pressed (or within
    // Wall_Ride_Triangle_Window), Wall_Ride_Delay after the last ride and plant, fast enough along it, not upside
    // down, head-on or tilted: the skater put on it (+0x38C its normal), facing along it; WallRideLeft / Right,
    // state 2. Whether it started.
    bool StartWallRide();

    // --- core_physics_acid_drop.cpp: the acid drop, the spine transfer ---
    // 0x00502240: looking for an acid drop from `position` (from the higher of it and `lastPosition`) along the
    // horizontal `velocity` (walking: the walking direction): feelers 4000 down, 0.01, then 3 (beyond 100: 27) at a time
    // up to 500 ahead (walking on the ground: Physics_Acid_Drop_Walking_On_Ground_Search_Distance, 200 up), through a
    // collision cache over the search, for a vert wall facing away. Then the arc there: `velocity` turned towards it
    // (`pop`: at least Physics_Acid_Drop_Pop_Speed up; at most twice that), high enough and long enough in the air
    // (Physics_Acid_Drop_Min_Air_Time), and a point up the wall (24 at a time) reached with nothing in the way: `drop`.
    // Whether one was found (`velocity` is put back if not).
    bool FindAcidDrop(bool pop, const Mth::Vector* position, const Mth::Vector* lastPosition, Mth::Vector* velocity,
                      AcidDrop* drop);
    // 0x00502F20: starting the acid drop to `drop`: the horizontal speed to get there in the fall's time (flags block
    // +0xE4), the wall's orientation (+0x3B0, +0x490) and the slerp to it (+0x3F0) over that time; flags +0x78 (in the
    // drop), +0x30, +0xA8; SkaterAcidDropTriggered with DropHeight; Ollied handlers cleared.
    void StartAcidDrop(const AcidDrop* drop);
    // 0x00505F70 (from BreakVert): a spine or hip transfer - the vert wall found by a feeler 4000 down from half a unit
    // back down the wall (the velocity's opposite turned a quarter round), then a target to land on (0x004FEE10: looking
    // back, else - one of input +0x60 / +0x80 held - back and along the wall that way). Too flat a target or one more
    // than 24 away: all the speed straight up; too slow to cover the distance in the fall's time (at least 0.1): false.
    // Something between the skater and the target: +0x2D0 set (BreakVert tries again), true. Otherwise the drop's
    // orientation (a spine: along the target's slope; a hip: straight up or down), the slerp there (+0x3F0, at least
    // 0.9 seconds), +0x490 / +0x37C / the flags block's +0xE4 as the acid drop's, SkaterAwardTransfer or
    // SkaterAwardHipTransfer, the Ollied handler cleared, flags 0x78 set and 0xA8 / 0x38 / 0x48 cleared, +0x14C0. True.
    bool TrySpineTransfer();

    // --- core_member_functions.cpp: the script member functions of the table at 0x006826C8 (named by its entries;
    // each returns 1 unless said otherwise, and uses neither argument unless said) ---
    // 0x004FBE80 (registered with Create): the table made member-function symbols (0x18-byte entries, put in by the
    // static initialiser at 0x006417D0).
    static void __cdecl RegisterMemberFunctions();
    // 0x004FBEA0 (`this` isn't used): an air time from `params` - the unnamed float, in milliseconds; with seconds /
    // second times 1000, with frames / frame times 16 - truncated to an integer.
    int AirTimeThreshold(Script::CStruct* params);
    // Bytes of the component set or read: CanBrakeOff / CanBrakeOn (+0x311 0 / 1), CanKickOn / CanKickOff (+0x310 0 /
    // 1: kicking forbidden), ForceAutoKickOn / ForceAutoKickOff / AutoKickIsOff (+0x2D9), SkaterVehicleOn / Off /
    // IsSkaterOnVehicle (+0x2ED), Canspin (+0x370 0), BailOn / Bailoff / BailIsOn (+0x371), BadLedge (+0x249),
    // LandedFromSpine (+0x372), LandedFromVert / SetLandedFromVert / ResetLandedFromVert (+0x373), AllowLipNoGrind /
    // ClearAllowLipNoGrind (+0x375), NoRailTricks / AllowRailTricks (+0x3A4), Braking (+0x324), InRevert / OutRevert /
    // IsInRevert (+0x300), ProjectilesOn / Off (+0x2F4), FocusOn / IsSkaterFocused (+0x301), EnterPowerslide /
    // ExitPowerslide (+0x2D8), MotoSkateboardOn / Off / IsOnMotoskateboard (+0x302), SkaterIsFlipping /
    // ResetIsFlipping (+0x2DB), SetThrowingProjectile / Unset / IsThrowingProjectile (+0x14C8), EndManualTurnaround
    // (+0x304 0), EnterTantrum / ExitTantrum / InTantrum (+0x1508), EnterTagging / ExitTagging / IsTagging (+0x1509);
    // ResetProjectileInput (+0x308 = -1). The queries return 1 or 0.
    int CanBrakeOff(Script::CStruct* params, Script::CScript* script);           // 0x004FBF40
    int CanBrakeOn(Script::CStruct* params, Script::CScript* script);            // 0x004FBF50
    int CanKickOn(Script::CStruct* params, Script::CScript* script);             // 0x004FBF60
    int CanKickOff(Script::CStruct* params, Script::CScript* script);            // 0x004FBF70
    int ForceAutoKickOn(Script::CStruct* params, Script::CScript* script);       // 0x004FBF80
    int ForceAutoKickOff(Script::CStruct* params, Script::CScript* script);      // 0x004FBF90
    int AutoKickIsOff(Script::CStruct* params, Script::CScript* script);         // 0x004FBFA0
    int SkaterVehicleOn(Script::CStruct* params, Script::CScript* script);       // 0x004FBFB0
    int SkaterVehicleOff(Script::CStruct* params, Script::CScript* script);      // 0x004FBFC0
    int IsSkaterOnVehicle(Script::CStruct* params, Script::CScript* script);     // 0x004FBFD0
    int CanSpin(Script::CStruct* params, Script::CScript* script);               // 0x004FC000
    int BailOn(Script::CStruct* params, Script::CScript* script);                // 0x004FC010
    int BailOff(Script::CStruct* params, Script::CScript* script);               // 0x004FC020
    int BailIsOn(Script::CStruct* params, Script::CScript* script);              // 0x004FC030
    int BadLedge(Script::CStruct* params, Script::CScript* script);              // 0x004FC080
    int LandedFromSpine(Script::CStruct* params, Script::CScript* script);       // 0x004FC090
    int LandedFromVert(Script::CStruct* params, Script::CScript* script);        // 0x004FC0A0
    int SetLandedFromVert(Script::CStruct* params, Script::CScript* script);     // 0x004FC0B0
    int ResetLandedFromVert(Script::CStruct* params, Script::CScript* script);   // 0x004FC0C0
    int AllowLipNoGrind(Script::CStruct* params, Script::CScript* script);       // 0x004FC0D0
    int ClearAllowLipNoGrind(Script::CStruct* params, Script::CScript* script);  // 0x004FC0E0
    int NoRailTricks(Script::CStruct* params, Script::CScript* script);          // 0x004FC0F0
    int AllowRailTricks(Script::CStruct* params, Script::CScript* script);       // 0x004FC100
    int InRevert(Script::CStruct* params, Script::CScript* script);              // 0x004FC1A0
    int OutRevert(Script::CStruct* params, Script::CScript* script);             // 0x004FC1B0
    int IsInRevert(Script::CStruct* params, Script::CScript* script);            // 0x004FC1C0
    int ProjectilesOn(Script::CStruct* params, Script::CScript* script);         // 0x004FC1D0
    int ProjectilesOff(Script::CStruct* params, Script::CScript* script);        // 0x004FC1E0
    int FocusOn(Script::CStruct* params, Script::CScript* script);               // 0x004FC1F0
    int IsSkaterFocused(Script::CStruct* params, Script::CScript* script);       // 0x004FC200
    int ResetProjectileInput(Script::CStruct* params, Script::CScript* script);  // 0x004FC210
    int EnterPowerslide(Script::CStruct* params, Script::CScript* script);       // 0x004FC230
    int ExitPowerslide(Script::CStruct* params, Script::CScript* script);        // 0x004FC240
    int MotoSkateboardOn(Script::CStruct* params, Script::CScript* script);      // 0x004FC250
    int MotoSkateboardOff(Script::CStruct* params, Script::CScript* script);     // 0x004FC260
    int IsOnMotoskateboard(Script::CStruct* params, Script::CScript* script);    // 0x004FC270
    int SkaterIsFlipping(Script::CStruct* params, Script::CScript* script);      // 0x004FC280
    int ResetIsFlipping(Script::CStruct* params, Script::CScript* script);       // 0x004FC290
    int SetThrowingProjectile(Script::CStruct* params, Script::CScript* script); // 0x004FC2A0
    int UnsetThrowingProjectile(Script::CStruct* params, Script::CScript* script); // 0x004FC2B0
    int IsThrowingProjectile(Script::CStruct* params, Script::CScript* script);  // 0x004FC2C0
    int EndManualTurnaround(Script::CStruct* params, Script::CScript* script);   // 0x004FC2D0
    int EnterTantrum(Script::CStruct* params, Script::CScript* script);          // 0x004FC2E0
    int ExitTantrum(Script::CStruct* params, Script::CScript* script);           // 0x004FC2F0
    int InTantrum(Script::CStruct* params, Script::CScript* script);             // 0x004FC300
    int EnterTagging(Script::CStruct* params, Script::CScript* script);          // 0x004FC310
    int ExitTagging(Script::CStruct* params, Script::CScript* script);           // 0x004FC320
    int IsTagging(Script::CStruct* params, Script::CScript* script);             // 0x004FC330
    int Braking(Script::CStruct* params, Script::CScript* script);               // 0x004FC9B0
    // The timed flags read or set (the time noted when one changes): flipped (+0x28), switched (flipped, inverted
    // unless the skater's +0x110 is set), Crouched (+0x20), FirstTimeOnThisRail (+0xC8), SKITCHING (+0x88), InVertAir
    // (+0x30), IsInBail (+0x70); CancelWallpush (+0xB8 on), InBail / NotInBail (+0x70), 0x004FD630 (checksum
    // 0x2A547E37, name not recovered: +0x30 on), OverrideCancelGround (+0x90: on unless Off), NoSpin (+0x370 = 1 and
    // +0x68 off).
    int Flipped(Script::CStruct* params, Script::CScript* script);               // 0x004FC780
    int Switched(Script::CStruct* params, Script::CScript* script);              // 0x004FC7A0
    int Crouched(Script::CStruct* params, Script::CScript* script);              // 0x004FC7E0
    int FirstTimeOnThisRail(Script::CStruct* params, Script::CScript* script);   // 0x004FC800
    int Skitching(Script::CStruct* params, Script::CScript* script);             // 0x004FC820
    int CancelWallpush(Script::CStruct* params, Script::CScript* script);        // 0x004FC840
    int NoSpin(Script::CStruct* params, Script::CScript* script);                // 0x004FCF90
    int InBail(Script::CStruct* params, Script::CScript* script);                // 0x004FCFC0
    int NotInBail(Script::CStruct* params, Script::CScript* script);             // 0x004FCFF0
    int IsInBail(Script::CStruct* params, Script::CScript* script);              // 0x004FD020
    int InVertAir(Script::CStruct* params, Script::CScript* script);             // 0x004FD1C0
    int OverrideCancelGround(Script::CStruct* params, Script::CScript* script);  // 0x004FD530
    int SetVertAir(Script::CStruct* params, Script::CScript* script);            // 0x004FD630
    // 0x004FBFE0 SetSkaterVehicle: the vehicle (+0x2F0) from the unnamed checksum (asserted). 0x004FC180
    // SetAmmoType: the projectile type (+0x2FC) from the unnamed checksum, if any. 0x004FC060 SetGrindTweak: +0x3A8
    // from the unnamed integer, if any. 0x004FC040 LockVelocityDirection: +0x388 = whether `on` is there.
    int SetSkaterVehicle(Script::CStruct* params, Script::CScript* script);      // 0x004FBFE0
    int LockVelocityDirection(Script::CStruct* params, Script::CScript* script); // 0x004FC040
    int SetGrindTweak(Script::CStruct* params, Script::CScript* script);         // 0x004FC060
    int SetAmmoType(Script::CStruct* params, Script::CScript* script);           // 0x004FC180
    // 0x004FC110 SetExtraPush: radius (+0x2C0, 48), speed (+0x2C4, 100) and 0xDFDFEAB8 (+0x2C8, 6) from `params`.
    int SetExtraPush(Script::CStruct* params, Script::CScript* script);
    // 0x004FC340 DoingFlip: whether a timed model rotation is on (+0x2DB) with more than 10 left (+0x2E0). 1 or 0.
    int DoingFlip(Script::CStruct* params, Script::CScript* script);
    // 0x004FC880 / 0x004FC8E0 AirTimeLessThan / AirTimeGreaterThan: the time in the air (in the air or on a wall: since
    // +0x380; otherwise the last one, +0x384 - +0x380) against AirTimeThreshold(params), unsigned. 1 or 0.
    int AirTimeLessThan(Script::CStruct* params, Script::CScript* script);
    int AirTimeGreaterThan(Script::CStruct* params, Script::CScript* script);
    // 0x004FC940 GetAirtime: that time in seconds (as unsigned) added to the script's parameters as 0xAD6BCDB4.
    int GetAirtime(Script::CStruct* params, Script::CScript* script);
    // 0x004FC9C0 RestoreAutoKick: +0x2D9 back to the skater's profile's (the byte at +0x395 of entry +0x2BC of the
    // table at *0x007CE478, 8 bytes each).
    int RestoreAutoKick(Script::CStruct* params, Script::CScript* script);
    // 0x004FC9F0 DoCarPlantBoost: no downward speed, CarPlant_Upward_boost added upwards, then CarPlant_Forward_boost
    // along the horizontal velocity (the facing when slower than 10; (1, 0, 1) when that's under 0.01 too) made unit.
    int DoCarPlantBoost(Script::CStruct* params, Script::CScript* script);
    // 0x004FD040 SetRailSound: slide (the unnamed checksum, asserted) or not: flag +0x58 and the sound component's +0x1E.
    int SetRailSound(Script::CStruct* params, Script::CScript* script);
    // 0x004FD0D0 SetRollingFriction: +0x314 from the unnamed float, if any; with Default and no special friction timer
    // (+0x320 at 0) Physics_Rolling_Friction.
    int SetRollingFriction(Script::CStruct* params, Script::CScript* script);
    // 0x004FD140 LastSpinWas: backside / Frontside against the last spin's way (+0x3A5) and flipped; 1 with neither.
    int LastSpinWas(Script::CStruct* params, Script::CScript* script);
    // 0x004FD1E0 TurnToFaceVelocity: unless barely moving, the skater's facing along its velocity, the right row
    // up x facing and the up row facing x right (each normalized), the display matrix the object's.
    int TurnToFaceVelocity(Script::CStruct* params, Script::CScript* script);
    // 0x004FD360 overridelimits: 0xC6F342D9 or end cancel it (+0x350 = 0); while one is fixed for the level (+0x350
    // -2) nothing else changes. Otherwise max (asserted; +0x354, and 0x73A19E3A +0x358, the same by default), time
    // (+0x350, 1e13 by default), Friction (+0x35C, 2e-6), gravity (+0x360, Physics_Ground_Gravity); 0xDF312928 makes
    // the time -1, CurrentLevel -2.
    int OverrideLimits(Script::CStruct* params, Script::CScript* script);
    // 0x004FD4A0 SetSpecialFriction: the next of the unnamed array's frictions (+0x318 counts them, kept at the last;
    // +0x31C the time of the first) into +0x314; duration (+0x320) if given.
    int SetSpecialFriction(Script::CStruct* params, Script::CScript* script);
    // 0x004FD580 GetMatrixNormal (x, y, z: the skater's up), 0x004FD5E0 GetLastGroundPos (Pos: +0x338), 0x004FD610
    // GetLastGroundHeading (last_heading: +0x348), 0x004FDC30 GetAmmo (ammo: +0x2F8), 0x004FDC50 GetAmmoType
    // (ammo_type: +0x2FC), 0x004FDC70 GetPitchBailDir (pitch_bail_dir: +0x2D4): added to the script's parameters.
    int GetMatrixNormal(Script::CStruct* params, Script::CScript* script);
    int GetLastGroundPos(Script::CStruct* params, Script::CScript* script);
    int GetLastGroundHeading(Script::CStruct* params, Script::CScript* script);
    int GetAmmo(Script::CStruct* params, Script::CScript* script);
    int GetAmmoType(Script::CStruct* params, Script::CScript* script);
    int GetPitchBailDir(Script::CStruct* params, Script::CScript* script);
    // 0x004FD660 IsFacingWall: a feeler from the skater along its facing (made unit) for the unnamed distance (200):
    // a hit on something not vert (face flag 8) whose normal's y is within 0.1 of 0. 1 or 0.
    int IsFacingWall(Script::CStruct* params, Script::CScript* script);
    // 0x004FD840 (checksum 0xA038260D, name not recovered): the same feeler; on such a wall the velocity and the
    // facing turned to point along its normal (SetDirection), the skater flipped (SkaterFlipAndRotate), the display
    // matrix the object's. 1 then, else 0.
    int TurnFromWall(Script::CStruct* params, Script::CScript* script);
    // 0x004FDA80 IsFacingVert: the same feeler from 50 higher; a hit on vert. 1 or 0.
    int IsFacingVert(Script::CStruct* params, Script::CScript* script);
    // 0x004FDC90 FocusOff: +0x301 off, the skater camera's look-around unlocked.
    int FocusOff(Script::CStruct* params, Script::CScript* script);
    // 0x004FDCC0 GetFocusTimeLeft (focus_time: the score's first float), 0x004FDCF0 GetFocusTimesAllowed
    // (focus_allowed: the score's +4) - added to the script's parameters -, 0x004FDD20 SetFocusTimesAllowed (the
    // score's +4 from the unnamed integer, if any). 0 when the skater has no score.
    int GetFocusTimeLeft(Script::CStruct* params, Script::CScript* script);
    int GetFocusTimesAllowed(Script::CStruct* params, Script::CScript* script);
    int SetFocusTimesAllowed(Script::CStruct* params, Script::CScript* script);

    // --- core_physics_life.cpp: set up, taken down, the state changed ---
    // 0x004FC500: the destructor body: its vtable, the collision cache (+0x4F4) taken down, the smart pointers at
    // +0x364 (the object skitched on) and in its two feelers (+0x1F0, +0x140) let go, then the base's.
    void DestroyBody();
    // 0x004FC5C0 (vtable slot 2; `params` isn't used): the rolling friction (+0x314) from Physics_Rolling_Friction;
    // the skater at the origin with no turn, the display matrix the same; the ground normals (+0x58, +0x68, +0x78)
    // straight up; +0x2EC and the projectile count cleared.
    void InitFromStructure(Script::CStruct* params);
    // 0x004FC690 (vtable slot 3): the object's components it works with, +0x150C Input to +0x1538 (0x380D909A).
    void Finalize();
    // 0x004FC3C0: the projectile button (the script value projectile_control) released then pressed (the Trick
    // component's CheckReleasePress); then the projectile input (+0x308) reset to -1. Whether it was.
    bool ProjectileButtonPressed();
    // 0x004FC440: the skater riding switch: flipped (the state's +0x28 byte, as it is) when the skater's +0x110 is
    // set, else not flipped (1 or 0).
    int IsSwitched();
    // 0x004FC470: the terrain under the skater (`terrain`'s low byte) told to the state and sound components.
    void SetTerrain(uint32_t terrain);
    // 0x004FDD50 (when the skater gets off its board): the pending event (+0x4F0: 0x5E27200A broadcast), the rail
    // wait (+0x23C) from 0xE4412EB7, +0x234 at least +0x238; leaving the lip for the air: the state's +0x50 off; the
    // state to the ground (0), and the state's vectors +0xF8 / +0x108 straight up, +0xE4 (0, 0, 0, 1), +0xF4 off.
    void LeaveSkating();
    // 0x004FDE40: every flag of the state component but flipped (+0x28) set and then cleared - each gets the time now
    // (the slide flag +0x58 telling the sound component, +0x1E).
    void ClearFlags();
    // 0x004FDED0: the physics state (the state component's +0x18) changed to `state`: a change notes the time (+0x90)
    // and the old one (+0x94). Off a lip, the skater put at the lip's position (+0x258) unless that's 0, and anything
    // but the lip clears it (0, 0, 0, 1); anything but the ground turns the trick objects on; off a rail the slide
    // flag and sound off; out of the air (state 1) the air time's end (+0x384) and flags +0xC0, +0xD0, +0xD8 off; into
    // the air from anything but a wall ride its start (+0x380), the trick component's +0x13A4, the spin buttons'
    // presses (pad +0xA0, +0x100) and +0x30C. Then the state component and the sound component (+0x18) told.
    void SetPhysicsState(int state);
    // 0x004FE120: turned around: the facing and the right row reversed (w too), the display matrix the object's, the
    // way round (+0x34C) flipped.
    void TurnAround();
    // 0x004FE1D0 (from the state history; the argument isn't used): the flags cleared (ClearFlags), into the air.
    void ResetToAir(void* unused);

    // --- core_physics_ground.cpp: friction, braking and pushing, the ground's normal ---
    // 0x004FE1F0: the velocity slowed by `friction` (its squared length x the frame x friction x 60, rounded to a
    // float, against the squared length): to rest when that's more than there is; nothing when barely moving.
    void ApplyFriction(float friction);
    // 0x004FE320: whether to brake: not auto-kicking nor holding pad +0x180 while slower than 50; or holding +0x40
    // with brakes allowed (+0x311) - slower than 50, neither +0x60 nor +0x80 held, or moving against the facing.
    bool ShouldBrake();
    // 0x004FE420: whether the skater may turn down the slope instead of braking: slower than 0x62A1FA03 and the
    // ground normal's y (+0x6C) under 0xC3527EF2 (script values; names not recovered).
    bool MayTurnOnSlope();
    // 0x004FE490: whether the skater may push off: kicking allowed (+0x310 off), auto-kick (+0x2D9) or pad +0x180
    // held, pad +0x40 not held, and no faster than the push speed stat (crouched or +0x303: 0x92E0247C, else
    // 0x4610C2E3).
    bool CanPushOff();
    // 0x004FE530: a push: along the facing (with the velocity locked, +0x388: along the velocity unless slower than
    // 0.5) by the push stat (crouched or +0x303: 0x3D24128E, else 0x5F9B864D) over the frame.
    void PushOff();
    // 0x004FE6A0: the friction for the stance - crouched 0xBED96EDA, else 0x1A78B6FC (physics values; while the
    // limits are overridden, +0x350, both +0x35C) - applied (ApplyFriction).
    void ApplyStanceFriction();
    // 0x004FE730: the rolling friction (+0x314) over the frame against the speed: to rest when it would overshoot.
    void ApplyRollingFriction();
    // 0x004FE860 (the normal by value, RET 0x10): unless the global setting at *0x007CE5D8 +0x40 is Off, a normal
    // facing up has a feeler 72 along it from the skater - a hit makes it straight up. A new normal becomes the
    // ground normal (+0x68; the old +0x58 kept at +0x78 and the blend +0x88 restarted at 1) and the skater's up,
    // the matrix made orthonormal about it.
    void TurnToNormal(Mth::Vector normal);
    // 0x004FEA50: the drawn ground normal (+0x58) blended from the old (+0x78) to the new (+0x68) as +0x88 runs down
    // at 0xD8120182 a second (a script value); the display matrix's up made it, orthonormal about it, its right and
    // facing the skater's.
    void UpdateGroundNormal();
    // 0x004FEBA0: going up a vert wall (flag +0x40): the wall's normal (+0x278) and the take-off point (+0x268, 1
    // lower) kept; with a horizontal ground normal, the velocity turned into its plane, the skater onto it
    // (TurnToNormal), +0x288 the facing with y reversed, pushed out from the wall by 0x78384871, flags +0x30, +0x38,
    // +0x68 on and +0x298 6; otherwise (or not on vert) flag +0x30 off.
    void StartVertAir();

    // --- core_physics_moves.cpp: the gravity, a landing target, wall push-offs, turning round, flip spins ---
    // 0x004FEE10 (RET 0x18): a vert target to land on along `direction` from the skater: feelers straight down
    // (from 400 above to 4000 below) every 6 from 10 to 500 out, through a collision cache over the search, for a
    // vert face (normal y under 0.707) whose horizontal normal is at most 0.95 along `facing`. Found: its point and
    // normal, whether its object is still there (`objectAlive`) and whether that normal is more than -0.866 along
    // `facing` (`notOpposite`; rounded to a float first). Whether one was found.
    bool FindVertTarget(const Mth::Vector* direction, const Mth::Vector* facing, bool* notOpposite, Mth::Vector* point,
                        Mth::Vector* normal, bool* objectAlive);
    // 0x004FF200: the speed override (+0x3AC, a factor; 1 when off): turned on (+0x255, not in an acid drop or a
    // bail) when faster than 1.25 times the 0x2EACDDB3 stat - the factor the speed over that (at most 0xC6B38BE0),
    // the limits (+0x354, +0x358) lifted. Running down at 0xF9B006AA a second; while above 1 the max speed (+0x354)
    // the stat times the factor (no faster than now x 1.1 off the air, nor the max), +0x358 its share of the stat
    // (every tenth time a line printed - compiled out). Below 1 or under the stat: off (+0x3AC 1, +0x350 0).
    void UpdateSpeedOverride();
    // 0x004FF470: the gravity: Physics_... 0xFAA40754 over the script value 0xC31CA696 (in vert air or an acid
    // drop: 0x441C38A0); the moon cheat (0x9C8C6DF1: its global flag set, 0x007CE4FD) times 0x0EC128F0; while
    // falling, times the script value 0xBE33A41E when that's set. Returned unrounded when multiplied last.
    double GetGravity();
    // 0x004FF590 (RET 4): `velocity` made to point along the facing (or against it, when it was going backwards),
    // keeping its length - unless it's barely moving.
    void AlongFacing(Mth::Vector* velocity);
    // 0x004FF670: a wall push-off: the button (pad +0x1A0) held, 0x0017D543 ms (a script value) since the last one
    // (+0x4E4) and since +0x4E0, facing into the wall (+0x130) more than the angle 0x1483FD01 - 1 degree allows:
    // event 0x4C03635B, then (unless cancelled, flag +0xB8: cleared) the velocity bounced off the wall, its speed
    // less 0x1112FB1C (at least 0xB78542C2; nearly still: 0xB78542C2 backwards), turned into the ground's plane, the
    // skater facing along it, up the ground normal; +0x4E4 the time. Whether it pushed off.
    bool WallPushOff();
    // 0x004FF920: going backwards faster than 0x2BF71EEB (not bailing, skitching or braking): turned round (the
    // facing, right row and display matrix's two rows reversed; 0x3A8D6AB2 run unless 0xB1C84B5A) - except on a
    // vehicle or a balance trick 0xEF24413B / 0x0AC90769 with another board (the trick component's +0x1331 name):
    // 0xA496AEBD run once (+0x304).
    void TurnRoundIfBackwards();
    // 0x004FFB10: the jump button (flag +0x20) let go (pad +0x1C0 up): the time it was held (+0x378), the flag
    // off, the release taken, event 0x8FFEFB28. Whether it was.
    bool JumpReleased();
    // 0x004FFB90: a flip spin (pad +0x160 or +0x180 held, not bailing, none running): the first of the 0xB75D73E5
    // entries whose button sequence (two from the entry, the third by which button) the trick component matches;
    // the model turned about x / y / z by its angles over 50 (+0x2E0), +0x2DB / +0x3A4 / +0x2E4 on, +0x2DC on a
    // half turn about y, and its 0xD8FA65D7 script run with it.
    void CheckFlipSpin();
    // 0x004FFDB0 (RET 4): stepping up onto what's at `point`: a feeler down from 0x786B3272 above (the higher of
    // the skater and the point) to the lower, hitting ground (normal y over 0.5), then one across from above the
    // point to above the hit (the normal added): clear - the skater put there, 0.1 up. Not while bailing. Whether
    // it stepped up.
    bool StepUp(const Mth::Vector* point);

private:
    // (Parts of 0x0050DDF0: still on the rail - along the segment, the matrices, the move and the feeler ahead; and
    // what every way out of the update does.)
    void GrindAlong(Obj::CRailNode* startNode, Obj::CRailNode* goOnTo, bool noNextSegment, float overshoot);
    void FinishRail();
    // (Parts of 0x0050DDF0: on a moving rail, carried along with it; a grind's or a slide's balance.)
    void RideMovingRail();
    void UpdateGrindBalance();
    // Not original: THAW's stall state's update (THAW 0x005E2E30 - core_rail.cpp, game/skater/thaw_bmx.h).
    void DoBikeStallPhysics();
    // (Part of 0x0050EE70: the move along the ground, once more for the rest when stopped short.)
    void MoveOnGround(Mth::Vector step);
    // (Part of 0x0050F830: off the wall's edge - the up straight, pushed out 18, into the air.)
    void OffWallEdge(bool resetDisplay);

    // (Part of 0x0050CA00: landing on something skatable - the velocity into the ground's plane, the ground
    // normal as the new up.)
    void LandFromAir();
};

} // namespace Skater
