#pragma once

namespace Mth {

class Matrix;

// A rotation as a quaternion (x, y, z, w) - the rigid body's orientation. To a matrix: 0x0042BC40 (not rewritten
// yet).
class Quat {
public:
    // 0x0042E1F0 (a constructor: returns this): the rotation of `matrix` (its upper 3x3): from its trace when that's
    // positive, else from its largest diagonal element.
    Quat* FromMatrix(const Matrix& matrix);
    // 0x0042E310: scaled to `length` (left as it is when its length is 0 or not a number). Returns this.
    Quat* Normalize(float length);

    float x, y, z, w;
};

} // namespace Mth
