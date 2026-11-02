#pragma once

// The Walk component's fields (offsets into GameObjects::FootMovement - its layout is only partly known) and helpers
// shared by walk_physics.cpp and walk_movement.cpp.

#include <cstdint>

#include "engine/objects/control_pad.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/core/raw_view.h"

namespace objects::walk_fields {

constexpr uint32_t kWalk = 0x726E85AA;

// The fields.
constexpr uint32_t kState = 0x18;
constexpr uint32_t kPos = 0x24;               // maths::Vec4: its own copy of the object's position
constexpr uint32_t kVel = 0x34;               // maths::Vec4: the flat velocity (y kept 0)
constexpr uint32_t kFacing = 0x48;            // maths::Vec4 (flat, unit)
constexpr uint32_t kUp = 0x58;                // maths::Vec4: the display's up, eased back upright
constexpr uint32_t kFrameLength = 0x80;       // float: this frame's length in seconds
constexpr uint32_t kContacts = 0xA0;          // 8 x 0xCC
constexpr uint32_t kSpeed = 0x704;
constexpr uint32_t kUpright = 0x72C;             // float: the time left turning upright (0: the up vector is eased)
constexpr uint32_t kCamera = 0x175C, kWalkCamera = 0x1760;
constexpr uint32_t kRailManager = 0x176C;
constexpr uint32_t kInput = 0x1770, kAnimBlender = 0x1774, kModel = 0x1778, kTrigger = 0x177C, kPhysicsControl = 0x1780,
                   kMovableContact = 0x1784, kSkaterState = 0x1788, kCorePhysics = 0x178C, kTaggingControl = 0x1790;
constexpr uint32_t kFloorUpVector = 0x1794; // maths::Vec4
constexpr uint32_t kRunning = 0x17A4;
constexpr uint32_t kLeapDuration = 0x1898, kJumpVelocity = 0x189C;
constexpr uint32_t kTransitionCallback = 0x1948; // a member function pointer: the function, then this's adjustment

// walk_parameters (the global structure of the walking tuning).
constexpr uint32_t kWalkParameters = 0x6775E538;

// A float of walk_parameters (asserted).
inline float walk_parameter(uint32_t name)
{
    float value = 0.0f;
    script::read_sub_struct(kWalkParameters)->fetch_float(name, &value, 1);
    return value;
}

// The original's _ftol (0x006236FC): truncated toward zero.
inline int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The game clock with its offset (0x00696D60), and the time since `then` on it.
inline int clock_since(int then) { return frame_clock::game_clock_elapsed() + (objects::clock_offset() - then); }

} // namespace objects::walk_fields
