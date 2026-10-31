#pragma once

// Debug drawing compiled out of this build: the calls go to the empty function at 0x005480A0. Kept where the
// original makes them, with their arguments.

#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

namespace gfx {

// A line from `start` to `end`, coloured `startColour` .. `endColour` (0xAABBGGRR), for `frames` frames.
inline void push_diag_line(const maths::Vec4* /*start*/, const maths::Vec4* /*end*/, uint32_t /*startColour*/,
                           uint32_t /*endColour*/, int /*frames*/)
{}

// A star (three crossed lines) of `size` at `position` (passed by value), for `frames` frames.
inline void push_diag_marker(maths::Vec4 /*position*/, float /*size*/, uint32_t /*colour*/, int /*frames*/) {}

// A sphere of `radius` at `center`, turned by `orientation`, drawn with `segments` segments.
inline void add_debug_sphere(const maths::Vec4* /*center*/, float /*radius*/, const maths::Mat4* /*orientation*/,
                             int /*segments*/, uint32_t /*colour*/, int /*frames*/)
{}

// A box (`box`: its max, then its min) about `position`, turned by `orientation`, in `colour`.
inline void draw_debug_cube(const maths::Mat4* /*orientation*/, const maths::Vec4* /*position*/,
                            const maths::Vec4* /*box*/, int /*a*/, int /*b*/, int /*c*/, uint32_t /*colour*/)
{}

} // namespace gfx
