#pragma once

#include "base/maths/vec4.h"

// The walk code's interpolation helper (WalkHangUtil.cpp in the original, beside the walk component's hanging code,
// game/rider/walk_hang.h): used by the walk and skater physics, the peds and the animation blending.
namespace walk {

// 0x0043D990 (cdecl): out = a + (b - a) * t, each component rounded as the original's x87 code does. Returns out.
maths::Vec4* get_lerp(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b, float blend);

} // namespace walk
