#pragma once

#include <array>
#include <cstdint>
#include "game/rider/core_physics.h"

// NOT ORIGINAL - the rider files' shared state: the acid drop's hold velocity (kept for debugging: the grind reports
// it) (the original's globals).
namespace skater {

// The rider files' shared state: the acid drop's hold velocity (kept for debugging: the grind reports it). Made on
// first use, never destroyed.
class RiderShared {
public:
    maths::Vec4& acid_hold() { return acid_hold_; }

private:
    // (core_grind.cpp)
    maths::Vec4 acid_hold_{};
};

// The state (made on first use, never destroyed).
RiderShared& rider_shared();

} // namespace skater
