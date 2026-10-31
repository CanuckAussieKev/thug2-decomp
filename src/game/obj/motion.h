#pragma once

#include <cstddef>
#include <cstdint>

#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/obj/entity.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

namespace Maths {
class ArcRotationBlender;
}

namespace GameObjects {

class CPathFollower;
class CFollowTrail;
class CObjectGroup;

// CKinematicsModule::m_motionFlags (+0x18): what the object is doing.
constexpr uint32_t MOTION_ROTATE_X = 0x1;          // turning about its x axis (Obj_RotX, a look-at)
constexpr uint32_t MOTION_ROTATE_Y = 0x2;          // ... y (Obj_RotY)
constexpr uint32_t MOTION_ROTATE_Z = 0x4;          // ... z (Obj_RotZ); bit `axis` for axis 0-2
constexpr uint32_t MOTION_ROTATE_AXES = 0x7;
constexpr uint32_t MOTION_MOVING = 0x8;            // moving to a point (Obj_MoveTo*)
constexpr uint32_t MOTION_FOLLOWING_PATH = 0x10;   // along a path of nodes (Obj_FollowPath*)
constexpr uint32_t MOTION_TURNING = 0x20;          // turning to an orientation over time (Obj_Rotate)
constexpr uint32_t MOTION_LOOKING_AT = 0x40;       // the axes' turns were set up by a look-at (Obj_LookAt*)
constexpr uint32_t MOTION_HOVERING = 0x80;         // bobbing up and down (Obj_Hover)
constexpr uint32_t MOTION_JUMPING = 0x100;         // Obj_Jump
constexpr uint32_t MOTION_FOLLOWING_LEADER = 0x200; // along the trail of another object (Obj_FollowLeader)
// Obj_WaitMove waits for these; Obj_WaitRotate for MOTION_ROTATE_AXES | MOTION_TURNING.
constexpr uint32_t MOTION_MOVE_MASK = MOTION_MOVING | MOTION_FOLLOWING_PATH;
constexpr uint32_t MOTION_ROTATE_MASK = MOTION_ROTATE_AXES | MOTION_TURNING;

// CKinematicsModule::m_motionFlags2 (+0x1C)
constexpr uint32_t MOTION2_STICK_TO_GROUND = 0x4;   // Obj_StickToGround (not `off`)
constexpr uint32_t MOTION2_STOPPED_ON_PATH = 0x8;   // Obj_StopAlongPath (Obj_StartAlongPath / FollowPath clear it)
constexpr uint32_t MOTION2_NO_PATH_HEADING = 0x10;  // the path's heading isn't taken (Obj_PathHeading off)
constexpr uint32_t MOTION2_ROTATE_FOREVER_X = 0x20; // the axis turns without a target angle (0x20 << axis)
constexpr uint32_t MOTION2_CONSTANT_HEIGHT = 0x100; // Obj_SetConstantHeight: a path doesn't change its height
constexpr uint32_t MOTION2_RANDOM_LINKS = 0x200;    // Obj_FollowPathLinked picks a random link (from Obj_FollowLeader)
constexpr uint32_t MOTION2_LEVEL_HEADING = 0x400;   // the path's heading kept level (peds: game_objects.cpp)

// The motion component (type 0x0A015E17 "motion", vtable 0x0064620C, 0x24C bytes; one of the motion components
// AddMotionComponents gives an object): moves an object about by script - along paths of nodes (with a
// CPathFollower), to points, turning about its axes or to an orientation over time, looking at things, jumping,
// hovering, following another object's trail, sticking to the ground.
//
// Vtable: 0 Destroy, 1 Update, 2 ConfigureFromData, 3 / 4 / 9 the empty 0x005480A0, 5 0x004E3F90 (slot 2 again), 6
// StepWaitState, 7 the base's Suspend, 8 / 10 the empty 0x005BAB90, 11 0x0042BB20 (the table entry's member function,
// else slot 12), 12 InvokeMethodByName. Its member functions are in the table at 0x00681EB8 (registered by
// 0x00423130; the member function pointers filled in by the dynamic initialiser 0x00640E30).
class CKinematicsModule : public CObjectModule {
public:
    static constexpr uint32_t kType = 0x0A015E17;

    // --- life ---
    // 0x00426650 (registered with the component types): a new one (MemSys::AllocZeroed; the constructor inlined).
    static CObjectModule* __cdecl Fabricate();
    // 0x00424AA0: the constructor (returns this): the base's, its vtable and type, no path follower or path group,
    // only the ground's height taken (+0x70), no ground offset; then Reset.
    CKinematicsModule* MakeNew();
    // 0x00424AE0: the destructor body: the path follower deleted (its vtable), the turn's interpolator and the
    // follow trail freed, then the base's.
    void DestroyBody();
    // 0x004266F0 (vtable slot 0): the scalar deleting destructor.
    CKinematicsModule* Teardown(unsigned int flags);
    // 0x00423D00: every motion setting back to rest (identity-like vectors, no flags, no speeds); +0x70 and the
    // pointers are kept.
    void Reset();
    // 0x00423C90: a path follower for `owner` if there's none yet (MemSys::AllocZeroed 0xE4).
    void InitPathFollower(CGameEntity* owner);

    // --- the vtable ---
    // 0x00426720 (slot 2): its node (Name, 0 if none) as both nodes; oriented to that node (0x00425970).
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x004236C0 (slot 6): a script waiting on it (wait types 5-9) stops waiting when what it waits for is over: 5
    // moving (MOTION_MOVE_MASK), 7 the jump, 8 the path's speed down to 0 (not above 0), 9 turning
    // (MOTION_ROTATE_MASK); 6 never.
    void StepWaitState(Scripts::CScript* script, uint32_t arg);
    // 0x004274D0 (slot 12): Obj_MoveToNode (0x8819DD8B) runs GlideToNode (0x00426C10), 1; anything else 2.
    int InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    // 0x004270E0 (slot 1): the frame's length (slow motion included); hovering (back to the base height, then the
    // bob), the axes' turns (0x00425C70), Obj_Rotate's turn (0x00424440), then moving to a point, along a path or
    // after a leader (the first that applies), the jump (0x00424870: the position it gives); the ground offset
    // eased in (384 per second squared towards it, the speed zeroed when it changes direction or overshoots);
    // objects with ids over 15 (not skaters) get their matrix as the display matrix.
    void TickFrame();

    // --- the update's parts ---
    // 0x00423740: the hover's bob for now: sin(2 pi * (the game clock mod the period) / period) * amplitude (the
    // clock's doubled count taken as unsigned), added to the height (the height without it kept in m_hoverBase).
    void UpdateHover();
    // 0x00424200: moving to a point - the speed accelerated (60 * frame * acceleration, to the maximum), the distance
    // covered; arrived (true): at the target, the arrival node told, no velocity; else on the line from the start
    // (that fraction of the way) with the velocity of the step. x87 roundings kept.
    bool UpdateMoveTo();
    // 0x00426E70: along the path (not for peds - those with a pedlogic component): the path's speed (0x00423530),
    // the follower moved that far (along the facing unless MOTION2_NO_PATH_HEADING; at the end, no longer
    // following, speed 0); the facing from the path (kept level with MOTION2_LEVEL_HEADING; the other rows rebuilt
    // unless sticking to the ground); the position the follower's (the height kept with MOTION2_CONSTANT_HEIGHT;
    // with MOTION2_STICK_TO_GROUND, kept and then stuck to the ground); the velocity of the step.
    void UpdatePath();
    // 0x004246A0: after the leader: the trail's update and its position and facing (the height kept with
    // LeaveYUnaffected).
    void UpdateFollowLeader();

    // --- paths ---
    // 0x00423FC0: along the path from `node`: out of the old path's group into the new one's (path manager), a
    // path follower if needed, started there.
    void StartPath(uint32_t node);
    // 0x00424020: at the node `node`: it's the current node; its SpawnObjScript (with its Params) is spawned on the
    // object, else its switchobjscript becomes the object's script. (A missing node: the original crashes too.)
    void ArriveAtNode(uint32_t node);
    // 0x00424180 (thiscall, RET 8; its result unused): on along a link of the node it's at (originalnode: its own
    // node) - a random one with MOTION2_RANDOM_LINKS when there are several, else the first; none: nothing.
    void FollowPathLinked(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x004244C0: of the objects on its path (its group), the nearest one within `range` that faces the same way
    // and is ahead of it - and, if both follow paths, on the same segment (the same node, else the same last node).
    CGameEntity* GetNextObjOnPath(float range);
    // 0x004246F0: Off: no longer following, the trail freed. Else following the object Name at `distance` (a new
    // trail if needed, emptied), LeaveYUnaffected, OrientY (the trail's free facing); the object's lock dropped
    // (its lockobj component: a crash without one).
    void TrailBehindLead(Scripts::CStruct* params);

    // --- turning (motion_rotate.cpp) ---
    // 0x00423F10: the axis' speed, maximum, acceleration and deceleration made positive - negative when its target
    // angle is.
    void SetRotationDirection(int axis);
    // The body of Obj_RotX / RotY / RotZ (three copies in the original): Angle (degrees; times `sign`) to turn
    // through - 0: that axis stops -, then deceleration (to speed: the speed to reach) or Acceleration (with speed: the
    // speed to reach; the speed now kept only while already turning) or just speed (degrees per second); with no
    // angle and no deceleration it turns for ever.
    void RotateAboutAxisBy(Scripts::CStruct* params, int axis, float sign);
    // 0x004258B0: the orientation turned `angle` radians about its own axis 0-2 (rotation * matrix), that row kept
    // as the others are made orthonormal again; any other axis: nothing.
    void RotateAboutAxis(int axis, float angle);
    // 0x00425A80: one frame of an axis' turn - accelerated (to the speed to reach, going its way), decelerated (to
    // it: done slowing, no turn this frame, finished when that speed is 0), then turned by speed * frame (only as
    // far as the target, when there is one: finished). Whether it's finished.
    bool UpdateAxisRotation(int axis);
    // 0x00425C70: each turning axis' frame; finished axes stop.
    void UpdateRotations();
    // 0x00425520: Obj_Rotate's turn from the orientation now to `target` over `time` seconds (0: at once) - its
    // interpolator made on first use (from the manager, set up first if needed).
    void StartRotation(Scripts::CStruct* params, const Maths::Mat4* target);
    // 0x00424440: one frame of it (done at the end, or when there's no time).
    void UpdateTurn();
    // 0x004240E0: axis `axis` set to turn towards `target` seen from `from` (the angle in the plane across that axis
    // from row `referenceAxis`): true (turning) when the angle is beyond `threshold`.
    bool SetupLookAxis(const Maths::Vec4* target, const Maths::Vec4* from, int referenceAxis, int axis, float threshold);
    // 0x004252E0: turn to look at `target`: instant (about y at once when beyond AngleThreshold degrees; true if
    // turned), else the axes 0xA4AECB20 (bits, default y) turned over `time`, or at `speed` (degrees per second,
    // default 360) with Acceleration - true if any turns.
    bool LookAt(Scripts::CStruct* params, const Maths::Vec4* target);
    // 0x00425710: look at the node of its node's link LinkNum (1 based, default 1; out of range: the first).
    void LookAtNodeLinked(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x004257A0: look at the node Name (none: nothing).
    void LookAtNode(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x00425800: look at the unnamed vector (FLAG_MAX_COORDS: converted) - relative to its position unless
    // `absolute`.
    void LookAtPosition(Scripts::CStruct* params, bool absolute);
    // 0x00427500: look at the nearest object of Type (skater, ped, gameobj, Car), else the object of id Name in its
    // manager's list (not itself). False when there's none (or LookAt turns nothing).
    bool LookAtObject(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x00425970: the orientation from the node's angles (x, then y, then z: matrix * rotation each; none: the
    // identity), as the display matrix too.
    void FaceNode(Scripts::CStruct* node);

    // --- jumping, the ground, moving to points (motion_ground.cpp) ---
    // 0x00424870: one frame of the jump into m_jumpPos: up by speed * frame + gravity * frame^2 / 2 (the speed
    // then changed by gravity * frame), along the heading at the start's speed; coming down, it lands at
    // land_height or on whatever a feeler from col_dist_above over to col_dist_below under finds (no longer
    // jumping - the position isn't snapped to it).
    void UpdateJump();
    // 0x00425F40: false without DistAbove / DistBelow. Height only (+0x70): the right axis rebuilt from up and at,
    // the ground between |DistAbove| feet over and |DistBelow| under found (GroundHit, the cached triangle first)
    // and the height to make up kept as the ground offset. Else three feelers (behind right, behind left, ahead -
    // 72 back or ahead, 30 to the side) with up taken as (0, 1, 0): with all three, the height half way between
    // the back's middle and the front, the right axis along the back points, at halfway to the direction from the
    // back to the front, up = at x right. Whether that was found.
    bool StickToGround();
    // 0x004269F0 (RET 0x1C: the target by value): moving to `target` - at once (no time or speed given: there now,
    // oriented to `node` with `orient`; true), over `time` seconds, or at `speed` (mph, fps or ips) reached by
    // Acceleration (the same units). False when it moves.
    bool MoveTo(Scripts::CStruct* params, Scripts::CScript* script, Maths::Vec4 target, uint32_t node);
    // 0x00426C10: MoveTo the node Name (at once: arrived there; else the node told on arriving).
    void GlideToNode(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x00426CB0: MoveTo the unnamed vector (FLAG_MAX_COORDS: converted) - relative to its position unless
    // `absolute`.
    void MoveToPosition(Scripts::CStruct* params, Scripts::CScript* script, bool absolute);
    // 0x00426D80: MoveTo its node's link LinkNum (1 based; else a random one with randomlink when there are
    // several, else the first).
    void MoveToLink(Scripts::CStruct* params, Scripts::CScript* script);
    // The body of Obj_MoveForward / Obj_MoveLeft (two copies in the original): MoveTo `dist` along `direction`.
    void MoveAlong(Scripts::CStruct* params, Scripts::CScript* script, const Maths::Vec4& direction);

    // --- script member functions (the table at 0x00681EB8; all true) ---
    bool Obj_StorePos(Scripts::CStruct* params, Scripts::CScript* script);          // 0x004237D0
    bool Obj_StoreNode(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00423810 (the path's node)
    // 0x00423830: `off`: off; else on, the height = the unnamed float (if given).
    bool Obj_SetConstantHeight(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x00423890: `link` = a random link of its node, 1 based, into the script's parameters (none: nothing).
    bool Obj_GetRandomLink(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x004238D0: `off`: back to the height without the bob, not hovering. Else hovering with Amp (default as it
    // was) and Freq (per second, default 1): the period 1000 / Freq ms (truncated) varied by a random -10..+10 %
    // (at least 1 ms), from the height now.
    bool Obj_Hover(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x004239F0: `Speed` into the script's parameters, in miles per hour (fps: feet per second; ips: inches per
    // second): moving to a point, that move's speed; locked to an object (an object locked by its lockobj), how far
    // it went this frame / the frame's length; else its velocity's length.
    bool Obj_GetSpeed(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x00423B30: a jump: speed (default 150) up, gravity (default -360), the start, col_dist_above / below (6, 6);
    // heading (a vector: its y scales the speed and is then 0) or use_current_heading (the display matrix's at)
    // moves it along too; land_height (where it lands, else the ground found).
    bool Obj_Jump(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x00425150: `ob` (the id of GetNextObjOnPath within Range feet, default 100) into the script's parameters
    // (removed first; none found: not there).
    bool Obj_GetNextObjOnPath(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x004251C0: the unnamed float (feet unless `inches`) as the path follower's corner distance (a follower made
    // if needed); none given: nothing.
    bool Obj_SetPathTurnDist(Scripts::CStruct* params, Scripts::CScript* script);
    bool Obj_FollowPath(Scripts::CStruct* params, Scripts::CScript* script);       // 0x00425220 (from the node Name)
    bool Obj_FollowPathLinked(Scripts::CStruct* params, Scripts::CScript* script); // 0x00425270
    bool Obj_FollowPathStored(Scripts::CStruct* params, Scripts::CScript* script); // 0x00425290 (Obj_StoreNode's)
    bool Obj_FollowLeader(Scripts::CStruct* params, Scripts::CScript* script);     // 0x004252C0
    // 0x00424B90 / 0x00424D50 / 0x00424F50: turn about x / y / z (RotateAboutAxisBy; a look-at's turns stopped
    // first). FLAG_MAX_COORDS: x's angle negated; y and z swapped.
    bool Obj_RotX(Scripts::CStruct* params, Scripts::CScript* script);
    bool Obj_RotY(Scripts::CStruct* params, Scripts::CScript* script);
    bool Obj_RotZ(Scripts::CStruct* params, Scripts::CScript* script);
    bool Obj_Rotate(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00426770 (RotateTo)
    bool Obj_LookAtNode(Scripts::CStruct* params, Scripts::CScript* script);       // 0x00426790
    bool Obj_LookAtNodeLinked(Scripts::CStruct* params, Scripts::CScript* script); // 0x004267B0
    bool Obj_LookAtNodeStored(Scripts::CStruct* params, Scripts::CScript* script); // 0x004267D0 (Obj_StoreNode's)
    bool Obj_LookAtPos(Scripts::CStruct* params, Scripts::CScript* script);        // 0x00426810 (absolute)
    bool Obj_LookAtPosStored(Scripts::CStruct* params, Scripts::CScript* script);  // 0x00426880 (Obj_StorePos's)
    bool Obj_LookAtRelPos(Scripts::CStruct* params, Scripts::CScript* script);     // 0x004268A0
    bool Obj_LookAtObject(Scripts::CStruct* params, Scripts::CScript* script);     // 0x004276A0 (LookAtObject's)
    // 0x004268C0: DistAbove / DistBelow (feet); `off`: no longer sticking. Else sticking - false (and nothing
    // more) with both 0, else stuck now.
    bool Obj_StickToGround(Scripts::CStruct* params, Scripts::CScript* script);
    // 0x00426960: at the unnamed node, oriented to it, its model brought up to date (a crash without one).
    bool Obj_SkipToRestart(Scripts::CStruct* params, Scripts::CScript* script);
    bool Obj_MoveToPosStored(Scripts::CStruct* params, Scripts::CScript* script); // 0x004272D0 (Obj_StorePos's)
    bool Obj_MoveForward(Scripts::CStruct* params, Scripts::CScript* script);     // 0x00427320 (along at)
    bool Obj_MoveLeft(Scripts::CStruct* params, Scripts::CScript* script);        // 0x004273D0 (along right)
    bool Obj_MoveToPos(Scripts::CStruct* params, Scripts::CScript* script);       // 0x00427470 (absolute)
    bool Obj_MoveToRelPos(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00427490
    bool Obj_MoveToLink(Scripts::CStruct* params, Scripts::CScript* script);      // 0x004274B0

    uint32_t m_motionFlags;    // +0x18 MOTION_*
    uint32_t m_motionFlags2;   // +0x1C MOTION2_*
    float m_moveSpeed;         // +0x20 moving to a point: the speed now (per second)
    float m_pathAcceleration;  // +0x24 Obj_SetPathAcceleration
    float m_pathDeceleration;  // +0x28 Obj_SetPathDeceleration
    float m_pathMaxSpeed;      // +0x2C Obj_SetPathVelocity
    float m_pathSpeed;         // +0x30 along the path: the speed now
    float m_pathTurnDistance;  // +0x34
    float m_groundAbove;       // +0x38 Obj_StickToGround DistAbove (feet)
    float m_groundBelow;       // +0x3C DistBelow
    Maths::Vec4 m_groundTriangle[3]; // +0x40 the last ground triangle hit (0x00425CB0 tries it first)
    bool m_groundHeightOnly;   // +0x70 (constructor: true; cars: false) - false: three feelers turn it to the ground
    uint8_t pad71[3];
    Maths::Vec4 m_groundTriangles[3][3]; // +0x74 the three feelers' last triangles
    CObjectGroup* m_pathGroup; // +0x104 the path's group of objects (path manager)
    Maths::Vec4 m_moveTarget;  // +0x108
    float m_moveDistance;      // +0x118 from the start to the target
    float m_moveDone;          // +0x11C how far it has gone
    float m_moveMaxSpeed;      // +0x120 with an acceleration: the speed to reach
    float m_moveAcceleration;  // +0x124
    uint32_t m_moveArriveNode; // +0x128 Obj_MoveToNode: the node told (0x00424020) on arriving (0: none)
    Maths::Vec4 m_rotTarget;   // +0x12C per axis: the angle to turn through (radians; 0 forever)
    Maths::Vec4 m_rotDone;     // +0x13C per axis: turned so far
    Maths::Vec4 m_rotMaxSpeed; // +0x14C per axis: the speed to reach (radians per second)
    Maths::Vec4 m_rotSpeed;    // +0x15C per axis: the speed now
    Maths::Vec4 m_rotAcceleration; // +0x16C
    Maths::Vec4 m_rotDeceleration; // +0x17C
    Maths::Vec4 m_moveStart;   // +0x18C
    float m_groundSpeed;       // +0x19C the ground offset's speed of change
    float m_groundOffset;      // +0x1A0 how far the height has still to go to the ground
    float m_pathSpeed1A4;      // +0x1A4 (the path's speed handling: 0x00423530)
    float m_pathSpeed1A8;      // +0x1A8
    float m_pathMinStopSpeed;  // +0x1AC Obj_SetPathMinStopVel
    Maths::Vec4 m_storedPos;   // +0x1B0 Obj_StorePos
    uint32_t m_storedNode;     // +0x1C0 Obj_StoreNode
    float m_turnTime;          // +0x1C4 Obj_Rotate's time (0: at once)
    float m_turnElapsed;       // +0x1C8
    Maths::ArcRotationBlender* mp_turn; // +0x1CC Obj_Rotate's interpolator (0x98 bytes)
    float m_hoverBase;         // +0x1D0 the height without the bob
    float m_hoverOffset;       // +0x1D4 the bob now
    float m_hoverAmplitude;    // +0x1D8
    int m_hoverPeriod;         // +0x1DC ms
    float m_frameTime;         // +0x1E0 this frame's length (seconds, slow motion included)
    uint32_t m_firstNode;      // +0x1E4 its own node (Name)
    uint32_t m_listLink;           // +0x1E8 the node it's at
    CPathFollower* mp_pathFollower; // +0x1EC
    CFollowTrail* mp_trail;    // +0x1F0 Obj_FollowLeader's
    bool m_leaveYUnaffected;   // +0x1F4 Obj_FollowLeader's LeaveYUnaffected
    uint8_t pad1F5[3];
    float m_jumpSpeed;         // +0x1F8 up (per second)
    float m_jumpGravity;       // +0x1FC
    Maths::Vec4 m_jumpHeading; // +0x200 along (x, z; w kept)
    bool m_jumpHasHeading;     // +0x210
    uint8_t pad211[3];
    Maths::Vec4 m_jumpPos;     // +0x214 the jump's position this frame
    float m_jumpStartSpeed;    // +0x224
    float m_jumpColAbove;      // +0x228 col_dist_above
    float m_jumpColBelow;      // +0x22C col_dist_below
    uint32_t m_unknown230;
    bool m_jumpHasLandHeight;  // +0x234
    uint8_t pad235[3];
    float m_jumpLandHeight;    // +0x238
    Maths::Vec4 m_jumpStart;   // +0x23C

    // 0x00425600 (thiscall; called by its member function for Obj_Rotate, 0x00426770): turn the object to an
    // orientation over time - `relative = (x, y, z)`: the object's orientation now turned by those angles about its
    // own axes (Maths::Mat4::TurnInOwnFrame); `absolute = (x, y, z)`: the orientation of those angles
    // (Maths::Mat4::SetFromEulerAngles). Degrees; with FLAG_MAX_COORDS (0x26AF9DC9) the vector is first converted
    // (z negated, then NegateXRotateX90). Neither given: nothing. The turn is started by 0x00425520 (`time` from
    // the params).
    void RotateTo(Scripts::CStruct* params);
};
static_assert(offsetof(CKinematicsModule, m_motionFlags) == 0x18 && offsetof(CKinematicsModule, m_moveSpeed) == 0x20 &&
                  offsetof(CKinematicsModule, m_pathSpeed) == 0x30 &&
                  offsetof(CKinematicsModule, m_groundTriangle) == 0x40 &&
                  offsetof(CKinematicsModule, m_groundHeightOnly) == 0x70 &&
                  offsetof(CKinematicsModule, m_groundTriangles) == 0x74 && offsetof(CKinematicsModule, m_pathGroup) == 0x104 &&
                  offsetof(CKinematicsModule, m_moveTarget) == 0x108 &&
                  offsetof(CKinematicsModule, m_moveArriveNode) == 0x128 &&
                  offsetof(CKinematicsModule, m_rotTarget) == 0x12C && offsetof(CKinematicsModule, m_moveStart) == 0x18C &&
                  offsetof(CKinematicsModule, m_groundSpeed) == 0x19C &&
                  offsetof(CKinematicsModule, m_pathMinStopSpeed) == 0x1AC &&
                  offsetof(CKinematicsModule, m_storedPos) == 0x1B0 && offsetof(CKinematicsModule, mp_turn) == 0x1CC &&
                  offsetof(CKinematicsModule, m_hoverBase) == 0x1D0 && offsetof(CKinematicsModule, m_frameTime) == 0x1E0 &&
                  offsetof(CKinematicsModule, mp_pathFollower) == 0x1EC &&
                  offsetof(CKinematicsModule, m_leaveYUnaffected) == 0x1F4 &&
                  offsetof(CKinematicsModule, m_jumpSpeed) == 0x1F8 &&
                  offsetof(CKinematicsModule, m_jumpHasHeading) == 0x210 &&
                  offsetof(CKinematicsModule, m_jumpPos) == 0x214 &&
                  offsetof(CKinematicsModule, m_jumpHasLandHeight) == 0x234 &&
                  offsetof(CKinematicsModule, m_jumpStart) == 0x23C && sizeof(CKinematicsModule) == 0x24C,
              "layout must match the original (0x00426650 allocates 0x24C bytes)");

// 0x00425CB0 (ECX the triangle, EDX the end; the start and `out` on the stack, the caller pops: GroundHitStub):
// the line from `start` to `end` against the cached triangle (three corners) - hit: the point. Else a feeler along
// it: hit, the face it hit becomes the cached triangle and the point is the feeler's. Whether anything was hit.
bool __cdecl GroundHit(Maths::Vec4* triangle, const Maths::Vec4* end, const Maths::Vec4* start, Maths::Vec4* out);
void GroundHitStub();

} // namespace Obj
