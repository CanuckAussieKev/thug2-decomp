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

private:
    // (Part of 0x0050CA00: landing on something skatable - the velocity into the ground's plane, the ground
    // normal as the new up.)
    void LandFromAir();
};

} // namespace Skater
