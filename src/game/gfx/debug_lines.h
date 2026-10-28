#pragma once

// Debug drawing compiled out of this build: the calls go to the empty function at 0x005480A0. Kept where the
// original makes them, with their arguments.

#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Gfx {

// A line from `start` to `end`, coloured `startColour` .. `endColour` (0xAABBGGRR), for `frames` frames.
inline void AddDebugLine(const Mth::Vector* /*start*/, const Mth::Vector* /*end*/, uint32_t /*startColour*/,
                         uint32_t /*endColour*/, int /*frames*/)
{
}

// A star (three crossed lines) of `size` at `position` (passed by value), for `frames` frames.
inline void AddDebugStar(Mth::Vector /*position*/, float /*size*/, uint32_t /*colour*/, int /*frames*/) {}

// A sphere of `radius` at `center`, turned by `orientation`, drawn with `segments` segments.
inline void AddDebugSphere(const Mth::Vector* /*center*/, float /*radius*/, const Mth::Matrix* /*orientation*/,
                           int /*segments*/, uint32_t /*colour*/, int /*frames*/)
{
}

// A box (`box`: its max, then its min) about `position`, turned by `orientation`, in `colour`.
inline void AddDebugBox(const Mth::Matrix* /*orientation*/, const Mth::Vector* /*position*/,
                        const Mth::Vector* /*box*/, int /*a*/, int /*b*/, int /*c*/, uint32_t /*colour*/)
{
}

} // namespace Gfx
