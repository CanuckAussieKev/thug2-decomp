#pragma once

#include "game/mth/vector.h"

namespace Mth {

// A rotation quaternion (x, y, z the axis times sin(angle / 2), w cos(angle / 2)). Only what has source so far: the
// model component's (0x00420140-0x00420B50).
class Quat {
public:
    // 0x00420AC0: the rotation of `angle` radians about `axis` (normalised here; a zero axis gives (0, 0, 0)). The
    // sines and cosines are the FPU's. Returns this.
    Quat* InitFromAxisAngle(const Vector& axis, float angle);
    // 0x00420140: this = this * q (the roundings where the original stores).
    Quat& operator*=(const Quat& q);
    // 0x00420B50: `v` turned by this rotation (this * (v, 1) * conjugate), into `out` (w: what the product leaves).
    // Returns out.
    Vector* Rotate(Vector* out, const Vector& v) const;

    float x, y, z, w;
};

} // namespace Mth
