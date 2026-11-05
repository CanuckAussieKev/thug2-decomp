#pragma once

#include "base/maths/mat4.h"
#include "base/maths/vec4.h"

namespace objects {

// 0x004E4340: a per-frame blend `rate` (as at 60 frames a second) for a frame of `seconds`:
// rate * 60 * seconds / (1 - rate + rate * 60 * seconds). The camera components' smoothing.
double frame_blend(float rate, float seconds);

// 0x004E4360: the camera kept clear of the world. `position` is pulled in toward `target` where the collision
// feeler (ignoring faces flagged 0x90) hits between them (to 2 short of the hit, at least 13.9 out along the
// line); with `sideways`, it's pushed off walls 8 to either side across `matrix`'s right row (the right side
// first); with `refocus` - and a side wall found - `matrix` is turned to look from the position at `focus`
// (its rows rebuilt with the original's cross products, right and at negated).
void avoid_camera_collisions(maths::Vec4* position, maths::Mat4* matrix, const maths::Vec4* target,
                             const maths::Vec4* focus, bool sideways, bool refocus);

} // namespace objects
