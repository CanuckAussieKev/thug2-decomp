#pragma once

#include "game/mth/vector.h"

namespace Mth {

class Matrix;

// A rotation quaternion (x, y, z the axis times sin(angle / 2), w cos(angle / 2)): the model component's rotations and
// the rigid body's orientation. To a matrix: GetMatrix.
class Quat {
public:
    // 0x00420AC0: the rotation of `angle` radians about `axis` (normalised here; a zero axis gives (0, 0, 0)). The
    // sines and cosines are the FPU's. Returns this.
    Quat* InitFromAxisAngle(const Vector& axis, float angle);
    // 0x0042E1F0 (a constructor: returns this): the rotation of `matrix` (its upper 3x3): from its trace when that's
    // positive, else from its largest diagonal element.
    Quat* FromMatrix(const Matrix& matrix);
    // 0x00420140: this = this * q (the roundings where the original stores).
    Quat& operator*=(const Quat& q);
    // 0x00420B50: `v` turned by this rotation (this * (v, 1) * conjugate), into `out` (w: what the product leaves).
    // Returns out.
    Vector* Rotate(Vector* out, const Vector& v) const;
    // 0x0042E310: scaled to `length` (left as it is when its length is 0 or not a number). Returns this.
    Quat* Normalize(float length);
    // 0x0042BC40 (rigidbody_math.cpp): the rotation matrix (the upper 3x3 of `out`; the rest untouched) - scaled by
    // 2 / the squared length (0 for a zero or not-a-number quaternion). Returns out.
    Matrix* GetMatrix(Matrix* out) const;

    float x, y, z, w;
};

} // namespace Mth
