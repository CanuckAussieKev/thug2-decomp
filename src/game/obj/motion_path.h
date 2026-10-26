#pragma once

#include <cstddef>
#include <cstdint>

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// CMotionPathMembers::m_motionFlags (+0x18)
constexpr uint32_t MOTION_FLAG_MOVING = 0x18;   // (two bits: Obj_IsMoving / Obj_StopMoving)
constexpr uint32_t MOTION_FLAG_ON_PATH = 0x10;  // following a path (the speed updated)
constexpr uint32_t MOTION_FLAG_ROTATING = 0x27; // (Obj_IsRotating; Obj_StopRotating also clears 0x40)
// CMotionPathMembers::m_pathFlags (+0x1C)
constexpr uint32_t MOTION_PATH_STOPPING = 0x8;     // Obj_StopAlongPath: slowing to a stop over a distance
constexpr uint32_t MOTION_PATH_NO_HEADING = 0x10;  // Obj_PathHeading off
constexpr uint32_t MOTION_PATH_RANDOM = 0x200;     // Obj_RandomPathMode: a random link at each node

// The motion component's (type 0x0A015E17 "motion") members of 0x00423130-0x00423642: its member-function table, the
// moving / rotating flags, the path speeds and the speed's update along a path - as they see the component (its
// fields by offset). Kept apart from motion.h while the rest of the component is rewritten; the functions are the
// component's own (thiscall on it).
class CMotionPathMembers {
public:
    static constexpr uint32_t kType = 0x0A015E17;

    // 0x00423130 (not a Ghidra function; registered with the component's Create, 0x00426650): its member functions'
    // table (0x00681EB8; filled by the static initialiser at 0x00640E30).
    static void __cdecl RegisterMemberFunctions();
    // 0x00423150: turning (any of MOTION_FLAG_ROTATING).
    bool IsRotating() const;
    // 0x00423520: MOTION_PATH_RANDOM (the path follower takes a random link).
    bool PicksRandomLink() const;
    // 0x00423510: the path follower (+0x1EC; null while not on a path).
    void* GetPathFollower() const { return mp_pathFollower; }
    // 0x00423530: the speed along a path (MOTION_FLAG_ON_PATH): stopping - from the speed when it began, the stop's
    // distance covered (the frame's travel added) eased linearly to the minimum stop speed, 0 at its end; else
    // accelerating (or decelerating) towards the path velocity, by the frame's share, never past it (0 acceleration:
    // at once).
    void UpdatePathSpeed();

    // --- the member functions scripts call (the table at 0x00681EB8) ---
    // 0x00423160 [Obj_IsMoving] / 0x00423170 [Obj_IsRotating]: true / false.
    int IsMovingFromScript(Script::CStruct* params, Script::CScript* script);
    int IsRotatingFromScript(Script::CStruct* params, Script::CScript* script);
    // 0x00423180 [Obj_StopMoving] / 0x00423190 [Obj_StopRotating]: the flags cleared. 1.
    int StopMoving(Script::CStruct* params, Script::CScript* script);
    int StopRotating(Script::CStruct* params, Script::CScript* script);
    // 0x004231A0 [Obj_SetPathVelocity] / 0x00423210 [Obj_SetPathMinStopVel]: the unnamed float, in miles an hour
    // (into inches a second) - Fps: feet a second, ips: inches a second. 0x00423280 [Obj_SetPathAcceleration] /
    // 0x004232F0 [Obj_SetPathDeceleration]: the same per second (Fpsps / ipsps). Not given: as it was. 1.
    int SetPathVelocity(Script::CStruct* params, Script::CScript* script);
    int SetPathMinStopVel(Script::CStruct* params, Script::CScript* script);
    int SetPathAcceleration(Script::CStruct* params, Script::CScript* script);
    int SetPathDeceleration(Script::CStruct* params, Script::CScript* script);
    // 0x00423360 [Obj_PathHeading]: `Off` - MOTION_PATH_NO_HEADING. 1.
    int PathHeading(Script::CStruct* params, Script::CScript* script);
    // 0x004233A0 [Obj_RandomPathMode]: `Off` clears MOTION_PATH_RANDOM, else sets it. 1.
    int RandomPathMode(Script::CStruct* params, Script::CScript* script);
    // 0x004233E0 [Obj_StopAlongPath]: a stop over the unnamed distance (feet; `inches`: inches; 0 - or none: at
    // once), from the speed now down to the minimum stop speed (15 if 0). 1.
    int StopAlongPath(Script::CStruct* params, Script::CScript* script);
    // 0x00423480 [Obj_StartAlongPath]: the stop called off. 1.
    int StartAlongPath(Script::CStruct* params, Script::CScript* script);
    // 0x00423490 [Obj_WaitJumpFinished] / 0x004234B0 [Obj_WaitRotate] / 0x004234D0 [Obj_WaitMove] / 0x004234F0
    // [Obj_WaitStop]: the script waits on the component (wait types 7, 9, 5, 8). 1.
    int WaitJumpFinished(Script::CStruct* params, Script::CScript* script);
    int WaitRotate(Script::CStruct* params, Script::CScript* script);
    int WaitMove(Script::CStruct* params, Script::CScript* script);
    int WaitStop(Script::CStruct* params, Script::CScript* script);

    uint8_t m_base[0x18];           // the base component's
    uint32_t m_motionFlags;         // +0x18 MOTION_FLAG_*
    uint32_t m_pathFlags;           // +0x1C MOTION_PATH_*
    uint32_t m_unknown20;
    float m_acceleration;           // +0x24 inches a second per second
    float m_deceleration;           // +0x28 "
    float m_pathVelocity;           // +0x2C inches a second: the speed aimed at
    float m_velocity;               // +0x30 the speed now
    float m_stopDistance;           // +0x34 Obj_StopAlongPath's (inches)
    uint8_t m_unknown38[0x1A4 - 0x38];
    float m_stopCovered;            // +0x1A4 of the stop's distance
    float m_stopStartVelocity;      // +0x1A8 the speed when it began
    float m_minStopVelocity;        // +0x1AC Obj_SetPathMinStopVel
    uint8_t m_unknown1B0[0x1E0 - 0x1B0];
    float m_frameTime;              // +0x1E0 seconds
    uint8_t m_unknown1E4[0x1EC - 0x1E4];
    void* mp_pathFollower;          // +0x1EC
};
static_assert(offsetof(CMotionPathMembers, m_motionFlags) == 0x18 &&
                  offsetof(CMotionPathMembers, m_acceleration) == 0x24 &&
                  offsetof(CMotionPathMembers, m_stopCovered) == 0x1A4 &&
                  offsetof(CMotionPathMembers, m_frameTime) == 0x1E0 &&
                  offsetof(CMotionPathMembers, mp_pathFollower) == 0x1EC,
              "layout must match the original");

} // namespace Obj
