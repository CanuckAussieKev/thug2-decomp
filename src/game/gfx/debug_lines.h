#pragma once

// Debug drawing compiled out of this build: the calls go to the empty function at 0x005480A0. Kept where the
// original makes them, with their arguments.

#include <cstdint>

#include "game/mth/vector.h"

namespace Gfx {

// A line from `start` to `end`, coloured `startColour` .. `endColour` (0xAABBGGRR), for `frames` frames.
inline void AddDebugLine(const Mth::Vector* /*start*/, const Mth::Vector* /*end*/, uint32_t /*startColour*/,
                         uint32_t /*endColour*/, int /*frames*/)
{
}

} // namespace Gfx
