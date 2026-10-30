#pragma once

// The skater core physics component's fields as the large physics functions use them (core_jump.cpp,
// core_collision.cpp, core_grind.cpp, core_states.cpp): its layout is only partly known, so fields are reached by
// offset; the helpers here name the common ones and the small game calls everything uses.

#include <cmath>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/control_pad.h"
#include "game/obj/movable_contact.h"
#include "game/obj/ride_tracker.h"
#include "game/obj/trigger_component.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"

namespace Skater::CoreFields {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
inline Mth::Vector& VectorAt(void* object, uint32_t offset) { return At<Mth::Vector>(object, offset); }
inline Mth::Matrix& MatrixAt(void* object, uint32_t offset) { return At<Mth::Matrix>(object, offset); }

// The component's fields (offsets).
constexpr uint32_t kObject = 0xC;            // the skater's object (below)
constexpr uint32_t kDisplayMatrix = 0x18;    // (a copy of the object's matrix)
constexpr uint32_t kGroundNormal = 0x68;
constexpr uint32_t kFrameLength = 0x8C;      // the frame's length (seconds)
constexpr uint32_t kFeeler = 0xA8;           // a collision feeler (CFeeler: +0x78 its hit point, +0x88 normal)
constexpr uint32_t kSurfaceFlags = 0xFA;
constexpr uint32_t kHitPoint = 0x120;        // the feeler's hit: point, then normal
constexpr uint32_t kHitNormal = 0x130;
constexpr uint32_t kOtherFeeler = 0x158;
constexpr uint32_t kFeelerStart = 0x208;     // the feeler's line
constexpr uint32_t kFeelerEnd = 0x218;
constexpr uint32_t kHitSkatable = 0x228;     // (set by the feeler check 0x005003A0)
constexpr uint32_t kRailNode = 0x22C;        // the rail node being ground (CRailNode*)
constexpr uint32_t kRailManager = 0x230;     // ... and its rail manager
constexpr uint32_t kRailScriptNode = 0x240;  // the rail node whose TriggerScript last ran (its name)
constexpr uint32_t kLastRailScript = 0x244;  // (the same, kept for the next)
constexpr uint32_t kSkitched = 0x364;        // the object being skitched on (a smart pointer)
constexpr uint32_t kTenseTime = 0x378;       // how long the jump was held (ms), up to the max tense time
constexpr uint32_t kJumpTime = 0x4EC;        // when the skater last jumped
constexpr uint32_t kInput = 0x150C;          // the pad: a button every 0x20 bytes (pressed, then times)
constexpr uint32_t kTriggerComponent = 0x1510;
constexpr uint32_t kSoundComponent = 0x1514;
constexpr uint32_t kSkaterComponent = 0x1518;
constexpr uint32_t kOtherFlags = 0x151C;
constexpr uint32_t kBalanceTrick = 0x1524;    // the balance trick component (+0x18 the trick's kind, +0xD4 a lip's balance)
constexpr uint32_t kTimedFlags = 0x1528;     // bools, each followed by the time it last changed; +0x18 the state
constexpr uint32_t kRide = 0x152C;           // +0x18: the ride tracker (Obj::CRideTracker*), if riding something
constexpr uint32_t kTriggerState = 0x1530;   // +0x26: a trigger's script took over

// The timed flags' state (+0x18): what the skater is doing.
// (kBikeStall: not original - THAW's stall state, only on THAW's bike: game/skater/thaw_bmx.h)
enum State : int { kGround = 0, kAir = 1, kWallRide = 2, kLip = 3, kRail = 4, kWallplant = 5, kBikeStall = 6 };

// The object's fields.
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kOldPosition = 0x5C;
constexpr uint32_t kMatrix = 0x6C;
constexpr uint32_t kRight = 0x6C;            // the matrix's rows: right, up, facing
constexpr uint32_t kUp = 0x7C;
constexpr uint32_t kFacing = 0x8C;
constexpr uint32_t kVelocity = 0xAC;

inline void* ObjectOf(void* component) { return At<void*>(component, kObject); }
inline uint8_t* FlagsOf(void* component) { return At<uint8_t*>(component, kTimedFlags); }
inline int StateOf(void* component) { return At<int>(FlagsOf(component), 0x18); }
inline uint8_t* InputOf(void* component) { return At<uint8_t*>(component, kInput); }

// The game's clock (0x0044ECE0, plus the offset at 0x00696D60).
inline int Now()
{
    int time = Tmr::GameClockElapsed();
    return time + Obj::g_clockOffset;
}
// The time since `stamp` (the clock's elapsed time, as the original works it out).
inline uint32_t Since(uint32_t stamp)
{
    uint32_t now = static_cast<uint32_t>(Tmr::GameClockElapsed());
    return now + (static_cast<uint32_t>(Obj::g_clockOffset) - stamp);
}

// A timed flag: a bool and, after it, the time it last changed.
inline void SetTimedFlag(void* flags, uint32_t offset, uint8_t value)
{
    uint8_t& flag = At<uint8_t>(flags, offset);
    if (flag == value)
        return;
    flag = value;
    At<int>(flags, offset + 4) = Now();
}
inline void SetFlag(void* component, uint32_t offset, uint8_t value) { SetTimedFlag(FlagsOf(component), offset, value); }
inline uint8_t Flag(void* component, uint32_t offset) { return At<uint8_t>(FlagsOf(component), offset); }

// 0x00580400 / 0x00580440: a physics value (the skater's own tuning structure for `object`, else the global script
// value).
inline float PhysicsFloat(uint32_t name, void* object)
{
    return Obj::GetPhysicsFloat(name, 1, static_cast<Obj::CCompositeObject*>(object));
}
inline int PhysicsInteger(uint32_t name, void* object)
{
    return Obj::GetPhysicsInt(name, 1, static_cast<Obj::CCompositeObject*>(object));
}

// 0x00434BF0: the trigger component told the feeler `feeler` hit something, for the three `events` (const int[3]).
inline void TriggerFeeler(void* component, const int* events, uint32_t feelerOffset)
{
    static_cast<Obj::CTriggerComponent*>(At<void*>(component, kTriggerComponent))
        ->CheckFeeler(events, &At<Nx::CFeeler>(component, feelerOffset));
}
// Whether a trigger's script took over (+0x1530's +0x26).
inline bool TriggerTookOver(void* component) { return At<uint8_t>(At<uint8_t*>(component, kTriggerState), 0x26) != 0; }

// A vector's length as the original's 0x004023C0 leaves it in ST0: sqrt((x*x + y*y) + z*z) in double, unrounded
// (Mth::Vector::Length rounds it to a float).
inline double LengthOf(const Mth::Vector& v)
{
    return std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) + static_cast<double>(v.z) * v.z);
}

// The component's feeler (+0xA8) copied into a feeler on the stack (0x004F8200, the copy constructor), copied back
// (0x0043A930, assignment), and a stack feeler's destructor (0x0041C350: its smart pointer let go). The stack feelers
// are raw storage: the original's code decides when the destructor runs (some paths let go of the smart pointer
// inline instead - ReleaseFeelerObject).
inline void CopyFeeler(void* to, const void* from)
{
    static_cast<Nx::CFeeler*>(to)->CopyConstruct(*static_cast<const Nx::CFeeler*>(from));
}
inline void AssignFeeler(void* to, const void* from)
{
    static_cast<Nx::CFeeler*>(to)->Assign(*static_cast<const Nx::CFeeler*>(from));
}
// The ride tracker (+0x152C's +0x18) and what its smart pointer points at; 0x00427C70: stop riding.
inline Obj::CRideTracker*& Tracker(void* component) { return At<Obj::CRideTracker*>(At<void*>(component, kRide), 0x18); }
inline void StopRiding(void* component)
{
    static_cast<Obj::CMovableContactComponent*>(At<void*>(component, kRide))->StopRiding();
}

inline void DestroyFeeler(void* feeler) { static_cast<Nx::CFeeler*>(feeler)->Destruct(); }

// 0x005003A0: the collision feeler from kFeelerStart to kFeelerEnd: true on a hit (kHitPoint, kHitNormal, ...).
// (`a` and `b`: two face-flag masks - 0x10, 0 for the ground checks; 0, 8 across a wall.)
inline bool Feel(void* component, uint32_t a, uint32_t b)
{
    return static_cast<CorePhysicsComponent*>(component)->CheckFeeler(a, b);
}

} // namespace Skater::CoreFields
