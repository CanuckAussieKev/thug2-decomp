#pragma once

// The Walk component's fields (offsets into Obj::CWalkComponent - its layout is only partly known) and helpers
// shared by walk_physics.cpp and walk_movement.cpp.

#include <cstdint>

#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"

namespace Obj {
namespace WalkFields {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

constexpr uint32_t kWalk = 0x726E85AA;

// The fields.
constexpr uint32_t kState = 0x18, kStateTime = 0x1C, kPreviousState = 0x20;
constexpr uint32_t kPos = 0x24;              // Mth::Vector: its own copy of the object's position
constexpr uint32_t kVel = 0x34;              // Mth::Vector: the flat velocity (y kept 0)
constexpr uint32_t kVerticalVel = 0x44;      // float
constexpr uint32_t kFacing = 0x48;           // Mth::Vector (flat, unit)
constexpr uint32_t kUp = 0x58;               // Mth::Vector: the display's up, eased back upright
constexpr uint32_t kOldPos = 0x68;           // Mth::Vector: the position as the frame began
constexpr uint32_t kAnimState = 0x78;        // a name: Run, Stand, Skid, Land, AcidDrop, rail, Wallplant...
constexpr uint32_t kFrameLength = 0x80;      // float: this frame's length in seconds
constexpr uint32_t kControlDirection = 0x84; // Mth::Vector: the way the stick points, in the world (x, z)
constexpr uint32_t kControlMagnitude = 0x94; // float: the stick's push (0-1)
constexpr uint32_t kControlFull = 0x98;      // bool: a full push (always with the d-pad)
constexpr uint32_t kAnalogControl = 0x99;    // bool: the left stick (not the d-pad) steers
constexpr uint32_t kOnRail = 0x9B;           // bool
constexpr uint32_t kDisplayOffset = 0x9C;    // float: the model's height offset, restored over time
constexpr uint32_t kContacts = 0xA0;         // 8 x 0xCC
constexpr uint32_t kAnimSpeedScaling = 0x700; // 0 off, 1 Run, 2 HangMove, 3 LadderMove
constexpr uint32_t kSpeed = 0x704, kLastSpeed = 0x708;
constexpr uint32_t kForwardControlLock = 0x70C; // bool
constexpr uint32_t kFrames710 = 0x710;
constexpr uint32_t kCurbOffset = 0x714;      // float: the curb float's height offset
constexpr uint32_t kCurbFloat = 0x718;       // bool: floating over a curb this frame
constexpr uint32_t kCriticalPointOffset = 0x71C; // Mth::Vector: m_critical_point_offset (the game's own message)
constexpr uint32_t kUpright = 0x72C;         // float: the time left turning upright (0: the up vector is eased)
constexpr uint32_t kUprightTime = kUpright;
constexpr uint32_t kUprightAngle = 0x730;    // float: the turn upright (radians), over rotate_upright_duration
constexpr uint32_t kUprightAxis = 0x734;     // Mth::Vector: its axis
constexpr uint32_t kFacingY = 0x744;         // float: the object's at-row y (before the facing is flattened)
constexpr uint32_t kLeaveFor = 0x748;        // int: what the walk ends in - 1 a grind, 2 an acid drop
constexpr uint32_t kGrindPoint = 0x74C;      // Mth::Vector: the rail's point to grind from
constexpr uint32_t kGrindRails = 0x75C, kGrindNode = 0x760; // its rail manager and node
constexpr uint32_t kGrindObject = 0x764;     // CCompositeObject*: the object the rail rides (null: the level's)
constexpr uint32_t kCollCache = 0x790;
constexpr uint32_t kCamera = 0x175C, kWalkCamera = 0x1760;
constexpr uint32_t kHangNode = 0x1764, kHangNextNode = 0x1768, kRailManager = 0x176C;
constexpr uint32_t kInput = 0x1770, kAnimBlender = 0x1774, kModel = 0x1778, kTrigger = 0x177C,
                   kPhysicsControl = 0x1780, kMovableContact = 0x1784, kSkaterState = 0x1788,
                   kCorePhysics = 0x178C, kTaggingControl = 0x1790;
constexpr uint32_t kGroundNormal = 0x1794;   // Mth::Vector
constexpr uint32_t kRunning = 0x17A4, kFlag17A8 = 0x17A8;
constexpr uint32_t kStandardSpeed = 0x17B0;
constexpr uint32_t kObjectRef = 0x184C;      // CSmtPtr
constexpr uint32_t kFlag1864 = 0x1864;       // bool
constexpr uint32_t kRunAdjustment = 0x1868;
constexpr uint32_t kFlag186C = 0x186C, kHeight1870 = 0x1870, kFlag1874 = 0x1874;
constexpr uint32_t kJumpDirection = 0x1878;  // Mth::Vector: the flat direction of the jump's push
constexpr uint32_t kJumpBaseVel = 0x1888;    // Mth::Vector: the velocity the jump carries along unsteered
constexpr uint32_t kJumpTime = 0x1898, kJumpVelocity = 0x189C;
constexpr uint32_t kJumpTargetHeight = 0x18AC; // float: the top of the ledge the jump makes for
constexpr uint32_t kPushing = 0x18B0;        // bool: pushed back off a wall in the air
constexpr uint32_t kPushNormal = 0x18B4;     // Mth::Vector (the plane's normal; +0x18C4 its distance)
constexpr uint32_t kPushPlaneDistance = 0x18C4, kPushHeight = 0x18C8;
constexpr uint32_t kHangInitAnimType = 0x18D8, kClimbScale = 0x18DC, kLadderDirection = 0x18E0;
constexpr uint32_t kTransitionPos = 0x1904;  // Mth::Vector: a transition's point (with the climb offset)
constexpr uint32_t kTransitionDisplayOffset = 0x1918; // float
constexpr uint32_t kTransitionFacing = 0x191C; // Mth::Vector
constexpr uint32_t kTransitionRate = 0x192C, kTransitionTurn = 0x1930;
constexpr uint32_t kClimbOffset = 0x1934;
constexpr uint32_t kTransitionCallback = 0x1948; // a member function pointer: the function, then this's adjustment
constexpr uint32_t kJumpUnsteered = 0x1958;  // bool

// walk_parameters (the global structure of the walking tuning).
constexpr uint32_t kWalkParameters = 0x6775E538;

// A float of walk_parameters (asserted).
inline float WalkParameter(uint32_t name)
{
    float value;
    Script::GetStructure(kWalkParameters)->GetFloat(name, &value, 1);
    return value;
}

// The original's _ftol (0x006236FC): truncated toward zero.
inline int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The game clock with its offset (0x00696D60), and the time since `then` on it.
inline int ClockSince(int then) { return Tmr::GameClockElapsed() + (GameVariable<int>(0x00696D60) - then); }

} // namespace WalkFields
} // namespace Obj
