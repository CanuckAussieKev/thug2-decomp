#pragma once

// The skater core physics component's fields as the large physics functions use them (core_jump.cpp,
// core_collision.cpp, core_grind.cpp, core_states.cpp): its layout is only partly known, so fields are reached by
// offset; the helpers here name the common ones and the small game calls everything uses.

#include <cmath>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/trigger_component.h"
#include "game/rider/core_physics.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_state.h"
#include "game/rider/rider.h"
#include "platform/game_clock.h"

namespace skater::core_fields {

template <typename T>
T& get_at(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
inline maths::Vec4& vector_at(void* object, uint32_t offset) { return get_at<maths::Vec4>(object, offset); }
inline maths::Mat4& matrix_at(void* object, uint32_t offset) { return get_at<maths::Mat4>(object, offset); }

// The component's fields (offsets).
constexpr uint32_t kObject = 0xC;         // the skater's object (below)
constexpr uint32_t kDisplayMatrix = 0x18; // (a copy of the object's matrix)
constexpr uint32_t kFloorUpVector = 0x68;
constexpr uint32_t kFrameLength = 0x8C; // the frame's length (seconds)
constexpr uint32_t kFeeler = 0xA8;      // a collision feeler (ProbeRay: +0x78 its hit point, +0x88 normal)
constexpr uint32_t kHitPoint = 0x120; // the feeler's hit: point, then normal
constexpr uint32_t kHitNormal = 0x130;
constexpr uint32_t kProbeOrigin = 0x208; // the feeler's line
constexpr uint32_t kProbeTip = 0x218;
constexpr uint32_t kHitSkatable = 0x228;    // (set by the feeler check 0x005003A0)
constexpr uint32_t kRailNode = 0x22C;       // the rail node being ground (GrindPoint*)
constexpr uint32_t kRailManager = 0x230;    // ... and its rail manager
constexpr uint32_t kSkitched = 0x364;       // the object being skitched on (a smart pointer)
constexpr uint32_t kLeapDuration = 0x4EC;   // when the skater last jumped
constexpr uint32_t kInput = 0x150C;         // the pad: a button every 0x20 bytes (pressed, then times)
constexpr uint32_t kActivationZoneSlot = 0x1510;
constexpr uint32_t kSfxModuleSlot = 0x1514;
constexpr uint32_t kSkaterComponent = 0x1518;
constexpr uint32_t kBalanceTrick =
    0x1524;                              // the balance trick component (+0x18 the trick's kind, +0xD4 a lip's balance)
constexpr uint32_t kTimedFlags = 0x1528; // bools, each followed by the time it last changed; +0x18 the state
constexpr uint32_t kRide = 0x152C;       // +0x18: the ride tracker (GameObjects::RideTracker*), if riding something

// The timed flags' state (+0x18): what the skater is doing.
// (kBikeStall: not original - THAW's stall state, only on THAW's bike: game/rider/thaw_bmx.h)
enum State : int { kGround = 0, kAir = 1, kWallRide = 2, kLip = 3, kRail = 4, kWallplant = 5, kBikeStall = 6 };

// The object's fields.
constexpr uint32_t kPosition = 0x4C;
constexpr uint32_t kOldPosition = 0x5C;
constexpr uint32_t kMatrix = 0x6C;
constexpr uint32_t kRight = 0x6C; // the matrix's rows: right, up, facing
constexpr uint32_t kUp = 0x7C;
constexpr uint32_t kFacing = 0x8C;
constexpr uint32_t kVelocity = 0xAC;

inline objects::GameEntity* object_of(const SkaterPhysicsModule* component) { return component->entity_; }
// The state component's bytes (its timed flags are reached by offset) and the input component's (a button every
// 0x20 bytes from +0x20).
inline uint8_t* flags_of(const SkaterPhysicsModule* component) { return reinterpret_cast<uint8_t*>(component->state_); }
inline int state_of(const SkaterPhysicsModule* component) { return component->state_->cur_phase_; }
inline uint8_t* input_of(const SkaterPhysicsModule* component) { return reinterpret_cast<uint8_t*>(component->input_); }

// The game's clock (0x0044ECE0, plus the offset at 0x00696D60).
inline int get_now()
{
    int time = frame_clock::game_clock_elapsed();
    return time + objects::g_clock_offset;
}
// The time since `stamp` (the clock's elapsed time, as the original works it out).
inline uint32_t get_since(uint32_t stamp)
{
    uint32_t now = static_cast<uint32_t>(frame_clock::game_clock_elapsed());
    return now + (static_cast<uint32_t>(objects::g_clock_offset) - stamp);
}

// A timed flag: a bool and, after it, the time it last changed.
inline void set_timed_flag(void* flags, uint32_t offset, uint8_t value)
{
    uint8_t& flag = get_at<uint8_t>(flags, offset);
    if (flag == value)
        return;
    flag = value;
    get_at<int>(flags, offset + 4) = get_now();
}
inline void set_flag(SkaterPhysicsModule* component, uint32_t offset, uint8_t value)
{
    set_timed_flag(flags_of(component), offset, value);
}
inline uint8_t bit_mark(const SkaterPhysicsModule* component, uint32_t offset)
{
    return get_at<uint8_t>(flags_of(component), offset);
}

// 0x00580400 / 0x00580440: a physics value (the skater's own tuning structure for `object`, else the global script
// value).
inline float tuning_float_lookup(uint32_t name, void* object)
{
    return objects::read_tuning_float(name, 1, static_cast<objects::GameEntity*>(object));
}
inline int physics_integer(uint32_t name, void* object)
{
    return objects::read_tuning_int(name, 1, static_cast<objects::GameEntity*>(object));
}

// 0x00434BF0: the trigger component told the feeler `feeler` hit something, for the three `events` (const int[3]).
inline void trigger_feeler(SkaterPhysicsModule* component, const int* events, render::ProbeRay* feeler)
{
    component->trigger_->probe_touches_zone(events, feeler);
}
// Whether a trigger's script took over (the physics control component's +0x26).
inline bool trigger_took_over(const SkaterPhysicsModule* component)
{
    return component->physics_control_->flag26_ != 0;
}

// A vector's length as the original's 0x004023C0 leaves it in ST0: sqrt((x*x + y*y) + z*z) in double, unrounded
// (maths::Vec4::len_of rounds it to a float).
inline double length_of(const maths::Vec4& v)
{
    return std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
                     static_cast<double>(v.z) * v.z);
}

// The component's feeler (+0xA8) copied into a feeler on the stack (0x004F8200, the copy constructor), copied back
// (0x0043A930, assignment), and a stack feeler's destructor (0x0041C350: its smart pointer let go). The stack feelers
// are raw storage: the original's code decides when the destructor runs (some paths let go of the smart pointer
// inline instead - release_feeler_object).
inline void copy_feeler(void* to, const void* from)
{
    static_cast<render::ProbeRay*>(to)->copy_construct(*static_cast<const render::ProbeRay*>(from));
}
inline void assign_feeler(void* to, const void* from)
{
    static_cast<render::ProbeRay*>(to)->get_assign(*static_cast<const render::ProbeRay*>(from));
}
// The ride tracker (+0x152C's +0x18) and what its smart pointer points at; 0x00427C70: stop riding.
inline objects::RideTracker*& get_tracker(SkaterPhysicsModule* component) { return component->ride_->ride_; }
inline void stop_riding(SkaterPhysicsModule* component) { component->ride_->stop_riding(); }

inline void destroy_feeler(void* feeler) { static_cast<render::ProbeRay*>(feeler)->destruct(); }

// 0x005003A0: the collision feeler from kProbeOrigin to kProbeTip: true on a hit (kHitPoint, kHitNormal, ...).
// (`a` and `b`: two face-flag masks - 0x10, 0 for the ground checks; 0, 8 across a wall.)
inline bool get_feel(SkaterPhysicsModule* component, uint32_t a, uint32_t b)
{
    return component->probe_touches_zone(a, b);
}

} // namespace skater::core_fields
