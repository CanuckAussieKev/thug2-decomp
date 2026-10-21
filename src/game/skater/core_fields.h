#pragma once

// The skater core physics component's fields as the large physics functions use them (core_jump.cpp,
// core_collision.cpp, core_grind.cpp, core_states.cpp): its layout is only partly known, so fields are reached by
// offset; the helpers here name the common ones and the small game calls everything uses.

#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
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
constexpr uint32_t kTimedFlags = 0x1528;     // bools, each followed by the time it last changed; +0x18 the state
constexpr uint32_t kRide = 0x152C;           // +0x18: the ride tracker (Obj::CRideTracker*), if riding something
constexpr uint32_t kTriggerState = 0x1530;   // +0x26: a trigger's script took over

// The timed flags' state (+0x18): what the skater is doing.
enum State : int { kGround = 0, kAir = 1, kRail = 2, kLip = 3, kWall = 4, kWallplant = 5 };

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
    int time = GameFunction<int(__cdecl*)()>(0x0044ECE0)();
    return time + GameVariable<int>(0x00696D60);
}
// The time since `stamp` (the clock's elapsed time, as the original works it out).
inline uint32_t Since(uint32_t stamp)
{
    uint32_t now = static_cast<uint32_t>(GameFunction<int(__cdecl*)()>(0x0044ECE0)());
    return now + (static_cast<uint32_t>(GameVariable<int>(0x00696D60)) - stamp);
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
    return GameFunction<float(__cdecl*)(uint32_t, int, void*)>(0x00580400)(name, 1, object);
}
inline int PhysicsInteger(uint32_t name, void* object)
{
    return GameFunction<int(__cdecl*)(uint32_t, int, void*)>(0x00580440)(name, 1, object);
}

// 0x00434BF0: the trigger component told the feeler `feeler` hit something, for the three `events` (const int[3]).
inline void TriggerFeeler(void* component, uint32_t events, uint32_t feelerOffset)
{
    GameFunction<void(__thiscall*)(void*, const int*, void*)>(0x00434BF0)(
        At<void*>(component, kTriggerComponent), reinterpret_cast<const int*>(events), &At<uint8_t>(component, feelerOffset));
}
// Whether a trigger's script took over (+0x1530's +0x26).
inline bool TriggerTookOver(void* component) { return At<uint8_t>(At<uint8_t*>(component, kTriggerState), 0x26) != 0; }

// 0x005003A0: the collision feeler from kFeelerStart to kFeelerEnd: true on a hit (kHitPoint, kHitNormal, ...).
// (`a` and `b`: two face-flag masks - 0x10, 0 for the ground checks; 0, 8 across a wall.)
inline bool Feel(void* component, uint32_t a, uint32_t b)
{
    return GameFunction<bool(__thiscall*)(void*, uint32_t, uint32_t)>(0x005003A0)(component, a, b);
}

} // namespace Skater::CoreFields
