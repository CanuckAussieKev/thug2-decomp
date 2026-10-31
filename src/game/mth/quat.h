#pragma once

#include "game/mth/vector.h"

namespace Maths {

class Mat4;

// A rotation quaternion (x, y, z the axis times sin(angle / 2), w cos(angle / 2)): the model component's rotations and
// the rigid body's orientation. To a matrix: FetchMat.
class QuatRotation {
public:
    // 0x00420AC0: the rotation of `angle` radians about `axis` (normalised here; a zero axis gives (0, 0, 0)). The
    // sines and cosines are the FPU's. Returns this.
    QuatRotation* InitFromAxisAngle(const Vec4& axis, float angle);
    // 0x0042E1F0 (a constructor: returns this): the rotation of `matrix` (its upper 3x3): from its trace when that's
    // positive, else from its largest diagonal element.
    QuatRotation* FromMatrix(const Mat4& matrix);
    // 0x00420140: this = this * q (the roundings where the original stores).
    QuatRotation& operator*=(const QuatRotation& q);
    // 0x00420B50: `v` turned by this rotation (this * (v, 1) * conjugate), into `out` (w: what the product leaves).
    // Returns out.
    Vec4* SpinBy(Vec4* out, const Vec4& v) const;
    // 0x0042E310: scaled to `length` (left as it is when its length is 0 or not a number). Returns this.
    QuatRotation* MakeUnit(float length);
    // 0x0042BC40 (rigidbody_math.cpp): the rotation matrix (the upper 3x3 of `out`; the rest untouched) - scaled by
    // 2 / the squared length (0 for a zero or not-a-number quaternion). Returns out.
    Mat4* FetchMat(Mat4* out) const;

    float x, y, z, w;
};

} // namespace Mth
