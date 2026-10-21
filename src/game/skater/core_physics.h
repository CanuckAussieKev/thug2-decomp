#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script
namespace Mth {
class Vector;
} // namespace Mth
namespace Obj {
struct CRailNode;
class CRailManager;
} // namespace Obj

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

private:
    // (Parts of 0x0050DDF0: still on the rail - along the segment, the matrices, the move and the feeler ahead; and
    // what every way out of the update does.)
    void GrindAlong(Obj::CRailNode* startNode, Obj::CRailNode* goOnTo, bool noNextSegment, float overshoot);
    void FinishRail();
    // (Part of 0x0050EE70: the move along the ground, once more for the rest when stopped short.)
    void MoveOnGround(Mth::Vector step);
    // (Part of 0x0050F830: off the wall's edge - the up straight, pushed out 18, into the air.)
    void OffWallEdge(bool resetDisplay);

    // (Part of 0x0050CA00: landing on something skatable - the velocity into the ground's plane, the ground
    // normal as the new up.)
    void LandFromAir();
};

} // namespace Skater
