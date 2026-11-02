#pragma once

// NOT ORIGINAL - the state the files of this folder share (the original's globals), one record per file that
// defines it, each reached through its accessor (made on first use, never destroyed). The header is the folder's
// own: other subsystems use the folder's functions.

#include <array>
#include <cstdint>
#include "game/rider/core_physics.h"

namespace skater {

// NOT ORIGINAL grouping - core_grind.cpp's state shared with the folder's other files (the original's globals). Made on
// first use, never destroyed (core_grind_shared()).
struct CoreGrindShared {
    maths::Vec4 acid_hold_{};
};
CoreGrindShared& core_grind_shared();

} // namespace skater
