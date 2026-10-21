#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Mth {
class SlerpInterpolator;
}

namespace Obj {

class CPathFollower;
class CFollowTrail;
class CObjectGroup;

// CMotionComponent::m_motionFlags (+0x18): what the object is doing.
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

// CMotionComponent::m_motionFlags2 (+0x1C)
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
// Vtable: 0 Destroy, 1 Update, 2 InitFromStructure, 3 / 4 / 9 the empty 0x005480A0, 5 0x004E3F90 (slot 2 again), 6
// ProcessWait, 7 the base's Suspend, 8 / 10 the empty 0x005BAB90, 11 0x0042BB20 (the table entry's member function,
// else slot 12), 12 CallMemberFunction. Its member functions are in the table at 0x00681EB8 (registered by
// 0x00423130; the member function pointers filled in by the dynamic initialiser 0x00640E30).
class CMotionComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x0A015E17;

    // --- life ---
    // 0x00426650 (registered with the component types): a new one (Mem::Calloc; the constructor inlined).
    static CBaseComponent* __cdecl Create();
    // 0x00424AA0: the constructor (returns this): the base's, its vtable and type, no path follower or path group,
    // only the ground's height taken (+0x70), no ground offset; then Reset.
    CMotionComponent* Construct();
    // 0x00424AE0: the destructor body: the path follower deleted (its vtable), the turn's interpolator and the
    // follow trail freed, then the base's.
    void DestroyBody();
    // 0x004266F0 (vtable slot 0): the scalar deleting destructor.
    CMotionComponent* Destroy(unsigned int flags);
    // 0x00423D00: every motion setting back to rest (identity-like vectors, no flags, no speeds); +0x70 and the
    // pointers are kept.
    void Reset();
    // 0x00423C90: a path follower for `owner` if there's none yet (Mem::Calloc 0xE4).
    void InitPathFollower(CCompositeObject* owner);

    // --- the vtable ---
    // 0x00426720 (slot 2): its node (Name, 0 if none) as both nodes; oriented to that node (0x00425970).
    void InitFromStructure(Script::CStruct* params);
    // 0x004236C0 (slot 6): a script waiting on it (wait types 5-9) stops waiting when what it waits for is over: 5
    // moving (MOTION_MOVE_MASK), 7 the jump, 8 the path's speed down to 0 (not above 0), 9 turning
    // (MOTION_ROTATE_MASK); 6 never.
    void ProcessWait(Script::CScript* script, uint32_t arg);
    // 0x004274D0 (slot 12): Obj_MoveToNode (0x8819DD8B) runs MoveToNode (0x00426C10), 1; anything else 2.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x004270E0 (slot 1): the frame's length (slow motion included); hovering (back to the base height, then the
    // bob), the axes' turns (0x00425C70), Obj_Rotate's turn (0x00424440), then moving to a point, along a path or
    // after a leader (the first that applies), the jump (0x00424870: the position it gives); the ground offset
    // eased in (384 per second squared towards it, the speed zeroed when it changes direction or overshoots);
    // objects with ids over 15 (not skaters) get their matrix as the display matrix.
    void Update();

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
    void FollowPathLinked(Script::CStruct* params, Script::CScript* script);
    // 0x004244C0: of the objects on its path (its group), the nearest one within `range` that faces the same way
    // and is ahead of it - and, if both follow paths, on the same segment (the same node, else the same last node).
    CCompositeObject* GetNextObjOnPath(float range);
    // 0x004246F0: Off: no longer following, the trail freed. Else following the object Name at `distance` (a new
    // trail if needed, emptied), LeaveYUnaffected, OrientY (the trail's free facing); the object's lock dropped
    // (its lockobj component: a crash without one).
    void FollowLeader(Script::CStruct* params);

    // --- turning (motion_rotate.cpp) ---
    // 0x00423F10: the axis' speed, maximum, acceleration and deceleration made positive - negative when its target
    // angle is.
    void SetRotationDirection(int axis);
    // The body of Obj_RotX / RotY / RotZ (three copies in the original): Angle (degrees; times `sign`) to turn
    // through - 0: that axis stops -, then deceleration (to speed: the speed to reach) or Acceleration (with speed: the
    // speed to reach; the speed now kept only while already turning) or just speed (degrees per second); with no
    // angle and no deceleration it turns for ever.
    void RotateAboutAxisBy(Script::CStruct* params, int axis, float sign);
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
    void StartRotation(Script::CStruct* params, const Mth::Matrix* target);
    // 0x00424440: one frame of it (done at the end, or when there's no time).
    void UpdateTurn();
    // 0x004240E0: axis `axis` set to turn towards `target` seen from `from` (the angle in the plane across that axis
    // from row `referenceAxis`): true (turning) when the angle is beyond `threshold`.
    bool SetupLookAxis(const Mth::Vector* target, const Mth::Vector* from, int referenceAxis, int axis, float threshold);
    // 0x004252E0: turn to look at `target`: instant (about y at once when beyond AngleThreshold degrees; true if
    // turned), else the axes 0xA4AECB20 (bits, default y) turned over `time`, or at `speed` (degrees per second,
    // default 360) with Acceleration - true if any turns.
    bool LookAt(Script::CStruct* params, const Mth::Vector* target);
    // 0x00425710: look at the node of its node's link LinkNum (1 based, default 1; out of range: the first).
    void LookAtNodeLinked(Script::CStruct* params, Script::CScript* script);
    // 0x004257A0: look at the node Name (none: nothing).
    void LookAtNode(Script::CStruct* params, Script::CScript* script);
    // 0x00425800: look at the unnamed vector (FLAG_MAX_COORDS: converted) - relative to its position unless
    // `absolute`.
    void LookAtPosition(Script::CStruct* params, bool absolute);
    // 0x00427500: look at the nearest object of Type (skater, ped, gameobj, Car), else the object of id Name in its
    // manager's list (not itself). False when there's none (or LookAt turns nothing).
    bool LookAtObject(Script::CStruct* params, Script::CScript* script);
    // 0x00425970: the orientation from the node's angles (x, then y, then z: matrix * rotation each; none: the
    // identity), as the display matrix too.
    void OrientToNode(Script::CStruct* node);

    // --- script member functions (the table at 0x00681EB8; all true) ---
    bool Obj_StorePos(Script::CStruct* params, Script::CScript* script);          // 0x004237D0
    bool Obj_StoreNode(Script::CStruct* params, Script::CScript* script);         // 0x00423810 (the path's node)
    // 0x00423830: `off`: off; else on, the height = the unnamed float (if given).
    bool Obj_SetConstantHeight(Script::CStruct* params, Script::CScript* script);
    // 0x00423890: `link` = a random link of its node, 1 based, into the script's parameters (none: nothing).
    bool Obj_GetRandomLink(Script::CStruct* params, Script::CScript* script);
    // 0x004238D0: `off`: back to the height without the bob, not hovering. Else hovering with Amp (default as it
    // was) and Freq (per second, default 1): the period 1000 / Freq ms (truncated) varied by a random -10..+10 %
    // (at least 1 ms), from the height now.
    bool Obj_Hover(Script::CStruct* params, Script::CScript* script);
    // 0x004239F0: `Speed` into the script's parameters, in miles per hour (fps: feet per second; ips: inches per
    // second): moving to a point, that move's speed; locked to an object (an object locked by its lockobj), how far
    // it went this frame / the frame's length; else its velocity's length.
    bool Obj_GetSpeed(Script::CStruct* params, Script::CScript* script);
    // 0x00423B30: a jump: speed (default 150) up, gravity (default -360), the start, col_dist_above / below (6, 6);
    // heading (a vector: its y scales the speed and is then 0) or use_current_heading (the display matrix's at)
    // moves it along too; land_height (where it lands, else the ground found).
    bool Obj_Jump(Script::CStruct* params, Script::CScript* script);
    // 0x00425150: `ob` (the id of GetNextObjOnPath within Range feet, default 100) into the script's parameters
    // (removed first; none found: not there).
    bool Obj_GetNextObjOnPath(Script::CStruct* params, Script::CScript* script);
    // 0x004251C0: the unnamed float (feet unless `inches`) as the path follower's corner distance (a follower made
    // if needed); none given: nothing.
    bool Obj_SetPathTurnDist(Script::CStruct* params, Script::CScript* script);
    bool Obj_FollowPath(Script::CStruct* params, Script::CScript* script);       // 0x00425220 (from the node Name)
    bool Obj_FollowPathLinked(Script::CStruct* params, Script::CScript* script); // 0x00425270
    bool Obj_FollowPathStored(Script::CStruct* params, Script::CScript* script); // 0x00425290 (Obj_StoreNode's)
    bool Obj_FollowLeader(Script::CStruct* params, Script::CScript* script);     // 0x004252C0
    // 0x00424B90 / 0x00424D50 / 0x00424F50: turn about x / y / z (RotateAboutAxisBy; a look-at's turns stopped
    // first). FLAG_MAX_COORDS: x's angle negated; y and z swapped.
    bool Obj_RotX(Script::CStruct* params, Script::CScript* script);
    bool Obj_RotY(Script::CStruct* params, Script::CScript* script);
    bool Obj_RotZ(Script::CStruct* params, Script::CScript* script);
    bool Obj_Rotate(Script::CStruct* params, Script::CScript* script);           // 0x00426770 (RotateTo)
    bool Obj_LookAtNode(Script::CStruct* params, Script::CScript* script);       // 0x00426790
    bool Obj_LookAtNodeLinked(Script::CStruct* params, Script::CScript* script); // 0x004267B0
    bool Obj_LookAtNodeStored(Script::CStruct* params, Script::CScript* script); // 0x004267D0 (Obj_StoreNode's)
    bool Obj_LookAtPos(Script::CStruct* params, Script::CScript* script);        // 0x00426810 (absolute)
    bool Obj_LookAtPosStored(Script::CStruct* params, Script::CScript* script);  // 0x00426880 (Obj_StorePos's)
    bool Obj_LookAtRelPos(Script::CStruct* params, Script::CScript* script);     // 0x004268A0
    bool Obj_LookAtObject(Script::CStruct* params, Script::CScript* script);     // 0x004276A0 (LookAtObject's)

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
    Mth::Vector m_groundTriangle[3]; // +0x40 the last ground triangle hit (0x00425CB0 tries it first)
    bool m_groundHeightOnly;   // +0x70 (constructor: true; cars: false) - false: three feelers turn it to the ground
    uint8_t pad71[3];
    Mth::Vector m_groundTriangles[3][3]; // +0x74 the three feelers' last triangles
    CObjectGroup* m_pathGroup; // +0x104 the path's group of objects (path manager)
    Mth::Vector m_moveTarget;  // +0x108
    float m_moveDistance;      // +0x118 from the start to the target
    float m_moveDone;          // +0x11C how far it has gone
    float m_moveMaxSpeed;      // +0x120 with an acceleration: the speed to reach
    float m_moveAcceleration;  // +0x124
    uint32_t m_moveArriveNode; // +0x128 Obj_MoveToNode: the node told (0x00424020) on arriving (0: none)
    Mth::Vector m_rotTarget;   // +0x12C per axis: the angle to turn through (radians; 0 forever)
    Mth::Vector m_rotDone;     // +0x13C per axis: turned so far
    Mth::Vector m_rotMaxSpeed; // +0x14C per axis: the speed to reach (radians per second)
    Mth::Vector m_rotSpeed;    // +0x15C per axis: the speed now
    Mth::Vector m_rotAcceleration; // +0x16C
    Mth::Vector m_rotDeceleration; // +0x17C
    Mth::Vector m_moveStart;   // +0x18C
    float m_groundSpeed;       // +0x19C the ground offset's speed of change
    float m_groundOffset;      // +0x1A0 how far the height has still to go to the ground
    float m_pathSpeed1A4;      // +0x1A4 (the path's speed handling: 0x00423530)
    float m_pathSpeed1A8;      // +0x1A8
    float m_pathMinStopSpeed;  // +0x1AC Obj_SetPathMinStopVel
    Mth::Vector m_storedPos;   // +0x1B0 Obj_StorePos
    uint32_t m_storedNode;     // +0x1C0 Obj_StoreNode
    float m_turnTime;          // +0x1C4 Obj_Rotate's time (0: at once)
    float m_turnElapsed;       // +0x1C8
    Mth::SlerpInterpolator* mp_turn; // +0x1CC Obj_Rotate's interpolator (0x98 bytes)
    float m_hoverBase;         // +0x1D0 the height without the bob
    float m_hoverOffset;       // +0x1D4 the bob now
    float m_hoverAmplitude;    // +0x1D8
    int m_hoverPeriod;         // +0x1DC ms
    float m_frameTime;         // +0x1E0 this frame's length (seconds, slow motion included)
    uint32_t m_firstNode;      // +0x1E4 its own node (Name)
    uint32_t m_node;           // +0x1E8 the node it's at
    CPathFollower* mp_pathFollower; // +0x1EC
    CFollowTrail* mp_trail;    // +0x1F0 Obj_FollowLeader's
    bool m_leaveYUnaffected;   // +0x1F4 Obj_FollowLeader's LeaveYUnaffected
    uint8_t pad1F5[3];
    float m_jumpSpeed;         // +0x1F8 up (per second)
    float m_jumpGravity;       // +0x1FC
    Mth::Vector m_jumpHeading; // +0x200 along (x, z; w kept)
    bool m_jumpHasHeading;     // +0x210
    uint8_t pad211[3];
    Mth::Vector m_jumpPos;     // +0x214 the jump's position this frame
    float m_jumpStartSpeed;    // +0x224
    float m_jumpColAbove;      // +0x228 col_dist_above
    float m_jumpColBelow;      // +0x22C col_dist_below
    uint32_t m_unknown230;
    bool m_jumpHasLandHeight;  // +0x234
    uint8_t pad235[3];
    float m_jumpLandHeight;    // +0x238
    Mth::Vector m_jumpStart;   // +0x23C

    // 0x00425600 (thiscall; called by its member function for Obj_Rotate, 0x00426770): turn the object to an
    // orientation over time - `relative = (x, y, z)`: the object's orientation now turned by those angles about its
    // own axes (Mth::Matrix::RotateLocal); `absolute = (x, y, z)`: the orientation of those angles
    // (Mth::Matrix::SetFromEulerAngles). Degrees; with FLAG_MAX_COORDS (0x26AF9DC9) the vector is first converted
    // (z negated, then NegateXRotateX90). Neither given: nothing. The turn is started by 0x00425520 (`time` from
    // the params).
    void RotateTo(Script::CStruct* params);
};
static_assert(offsetof(CMotionComponent, m_motionFlags) == 0x18 && offsetof(CMotionComponent, m_moveSpeed) == 0x20 &&
                  offsetof(CMotionComponent, m_pathSpeed) == 0x30 &&
                  offsetof(CMotionComponent, m_groundTriangle) == 0x40 &&
                  offsetof(CMotionComponent, m_groundHeightOnly) == 0x70 &&
                  offsetof(CMotionComponent, m_groundTriangles) == 0x74 && offsetof(CMotionComponent, m_pathGroup) == 0x104 &&
                  offsetof(CMotionComponent, m_moveTarget) == 0x108 &&
                  offsetof(CMotionComponent, m_moveArriveNode) == 0x128 &&
                  offsetof(CMotionComponent, m_rotTarget) == 0x12C && offsetof(CMotionComponent, m_moveStart) == 0x18C &&
                  offsetof(CMotionComponent, m_groundSpeed) == 0x19C &&
                  offsetof(CMotionComponent, m_pathMinStopSpeed) == 0x1AC &&
                  offsetof(CMotionComponent, m_storedPos) == 0x1B0 && offsetof(CMotionComponent, mp_turn) == 0x1CC &&
                  offsetof(CMotionComponent, m_hoverBase) == 0x1D0 && offsetof(CMotionComponent, m_frameTime) == 0x1E0 &&
                  offsetof(CMotionComponent, mp_pathFollower) == 0x1EC &&
                  offsetof(CMotionComponent, m_leaveYUnaffected) == 0x1F4 &&
                  offsetof(CMotionComponent, m_jumpSpeed) == 0x1F8 &&
                  offsetof(CMotionComponent, m_jumpHasHeading) == 0x210 &&
                  offsetof(CMotionComponent, m_jumpPos) == 0x214 &&
                  offsetof(CMotionComponent, m_jumpHasLandHeight) == 0x234 &&
                  offsetof(CMotionComponent, m_jumpStart) == 0x23C && sizeof(CMotionComponent) == 0x24C,
              "layout must match the original (0x00426650 allocates 0x24C bytes)");

} // namespace Obj
