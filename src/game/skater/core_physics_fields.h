#pragma once

// The skater's core physics component's fields and the small helpers its code shares (the component's layout is
// only partly known: its fields are reached by offset). Used by core_physics_state.cpp, core_physics_speed.cpp and
// the other files of 0x00500020-0x00508773.

#include <cmath>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/control_pad.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"

namespace Skater::Physics {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// --- the component's fields ---
constexpr uint32_t kObject = 0x0C;            // the skater (a composite object: its fields below)
constexpr uint32_t kDisplayMatrix = 0x18;     // the matrix it's drawn with
constexpr uint32_t kGroundNormal = 0x68;      // (and +0x58, +0x78: copies of the ground's normal)
constexpr uint32_t kFrameLength = 0x8C;       // the frame's length (seconds)
constexpr uint32_t kFrameStart = 0x90;        // (the clock when the physics was reset)
constexpr uint32_t kSafePosition = 0x98;      // (the object's position when reset)
constexpr uint32_t kFeeler = 0xA8;            // a collision feeler (Nx::CFeeler, 0xB0 bytes)
constexpr uint32_t kHitFlags = 0xF8;          // the feeler's hit face's flags (16 bits)
constexpr uint32_t kHitPoint = 0x120;         // the feeler's hit: point, then normal
constexpr uint32_t kHitNormal = 0x130;
constexpr uint32_t kLastGroundFeeler = 0x158; // a copy of the last ground hit's feeler
constexpr uint32_t kFeelerStart = 0x208;      // the feeler's line
constexpr uint32_t kFeelerEnd = 0x218;
constexpr uint32_t kHitSkatable = 0x228;      // (set by the feeler check 0x005003A0)
constexpr uint32_t kHitVert = 0x229;
constexpr uint32_t kHitWall = 0x22A;
constexpr uint32_t kInput = 0x150C;           // the pad: a button every 0x20 bytes (down, then times)
constexpr uint32_t kTriggerComponent = 0x1510;
constexpr uint32_t kOtherComponent = 0x1514;  // (+0x1C, +0x1D, +0x1E: bytes it's told)
constexpr uint32_t kSkaterComponent = 0x1518;
constexpr uint32_t kTimedFlags = 0x1528;      // bools, each followed by the time it last changed (+0x18: the state)
constexpr uint32_t kRide = 0x152C;            // +0x18: a smart pointer to what the skater rides on
constexpr uint32_t kStateComponent = 0x1530;  // (+0x24, +0x26: bools; +0x2C: the walking component)

// --- the skater object's fields ---
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kOldPosition = 0x5C;
constexpr uint32_t kMatrix = 0x6C;            // rows: +0x6C, up +0x7C, forward +0x8C, position +0x9C
constexpr uint32_t kUp = 0x7C;
constexpr uint32_t kForward = 0x8C;
constexpr uint32_t kVelocity = 0xAC;

// --- the timed flags (offsets into the flags block) ---
constexpr uint32_t kState = 0x18;             // an int: 0 on the ground, 1 in the air, ...

inline Mth::Vector& VectorAt(void* object, uint32_t offset) { return At<Mth::Vector>(object, offset); }
inline void* ObjectOf(void* component) { return At<void*>(component, kObject); }
inline uint8_t* FlagsOf(void* component) { return At<uint8_t*>(component, kTimedFlags); }
inline uint8_t* InputOf(void* component) { return At<uint8_t*>(component, kInput); }

// The game's clock (0x0044ECE0) plus the offset at 0x00696D60: the time a flag or button changed.
inline int Now() { return Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60); }
// The time since `stamp` (unsigned, as the original compares it): the clock, plus (the offset - stamp).
inline uint32_t Since(uint32_t stamp)
{
    uint32_t now = static_cast<uint32_t>(Tmr::GameClockElapsed());
    return now + (static_cast<uint32_t>(GameVariable<int>(0x00696D60)) - stamp);
}

// A timed flag set to `value` (the time noted if it changes).
inline void SetTimedFlag(void* component, uint32_t flag, uint8_t value)
{
    uint8_t* f = FlagsOf(component) + flag;
    if (*f == value)
        return;
    *f = value;
    At<int>(f, 4) = Now();
}

// A physics value (0x00580400 / 0x00580440): the skater's tuning structure's `name`, else the global script value
// (asserted when `required`). The float comes back on the x87 stack, rounded (it's a stored float).
inline float PhysicsFloat(uint32_t name, void* object, int required = 1)
{
    return Obj::GetPhysicsFloat(name, required, static_cast<Obj::CCompositeObject*>(object));
}
inline int PhysicsInteger(uint32_t name, void* object, int required = 1)
{
    return Obj::GetPhysicsInt(name, required, static_cast<Obj::CCompositeObject*>(object));
}
// A value set by the skater's stats (0x00580790, thiscall on the object: a script range scaled by the stat level).
// (Ours returns a float.)
inline double StatValue(void* object, uint32_t name)
{
    return static_cast<Obj::CSkater*>(object)->GetScaledPhysics(name);
}
// 0x004FF470: the gravity (a physics value over a script one; in some states times another). It may come back
// unrounded (a product left on the x87 stack).
inline double Gravity(void* component) { return GameFunction<double(__thiscall*)(void*)>(0x004FF470)(component); }
// 0x004FDED0: the physics state (+0x18 of the flags) changed to `state`, with what goes with it.
inline void SetState(void* component, int state)
{
    GameFunction<void(__thiscall*)(void*, int)>(0x004FDED0)(component, state);
}

// 0x0042BBF0 (Mth::Vector::SetLength): x, y and z scaled to `length` (left alone at length 0).
inline void SetLength(Mth::Vector& v, float length) { v.SetLength(length); }

// A button's times: since it was last pressed (0x0046B920) / released (0x0046B900); its press this frame taken
// (0x0046B7E0).
inline int TimeSincePress(uint8_t* button) { return reinterpret_cast<Obj::CControlButton*>(button)->GetPressedTime(); }
inline int TimeSinceRelease(uint8_t* button)
{
    return reinterpret_cast<Obj::CControlButton*>(button)->GetReleasedTime();
}
inline void TakePress(uint8_t* button) { reinterpret_cast<Obj::CControlButton*>(button)->ClearTrigger(); }

// x87 sine and cosine (fsin / fcos), as the original computes them inline.
inline double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
inline double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

// A vector's squared length and length as the original's Vector::LengthSqr (0x004023A0) / Length (0x004023C0)
// leave them on the x87 stack: x * x + y * y + z * z, unrounded.
inline double LengthSqrUnrounded(const Mth::Vector& v)
{
    double x = v.x, y = v.y, z = v.z;
    return x * x + y * y + z * z;
}
inline double LengthUnrounded(const Mth::Vector& v) { return std::sqrt(LengthSqrUnrounded(v)); }

// The C runtime's _ftol2 (0x006236FC): truncated to a 64-bit integer, the low half kept.
inline uint32_t Ftol(double value) { return static_cast<uint32_t>(static_cast<int64_t>(value)); }

} // namespace Skater::Physics
