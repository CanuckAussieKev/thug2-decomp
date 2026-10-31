#pragma once

// Debug drawing compiled out of this build: the calls go to the empty function at 0x005480A0. Kept where the
// original makes them, with their arguments.

#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Gfx2D {

// A line from `start` to `end`, coloured `startColour` .. `endColour` (0xAABBGGRR), for `frames` frames.
inline void PushDiagLine(const Maths::Vector* /*start*/, const Maths::Vector* /*end*/, uint32_t /*startColour*/,
                         uint32_t /*endColour*/, int /*frames*/)
{
}

// A star (three crossed lines) of `size` at `position` (passed by value), for `frames` frames.
inline void PushDiagMarker(Maths::Vector /*position*/, float /*size*/, uint32_t /*colour*/, int /*frames*/) {}

// A sphere of `radius` at `center`, turned by `orientation`, drawn with `segments` segments.
inline void AddDebugSphere(const Maths::Vector* /*center*/, float /*radius*/, const Maths::Matrix* /*orientation*/,
                           int /*segments*/, uint32_t /*colour*/, int /*frames*/)
{
}

// A box (`box`: its max, then its min) about `position`, turned by `orientation`, in `colour`.
inline void DrawDebugCube(const Maths::Matrix* /*orientation*/, const Maths::Vector* /*position*/,
                        const Maths::Vector* /*box*/, int /*a*/, int /*b*/, int /*c*/, uint32_t /*colour*/)
{
}

} // namespace Gfx
