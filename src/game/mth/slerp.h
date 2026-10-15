#pragma once

#include "game/mth/matrix.h"

namespace Mth {

// Interpolates between two orientation matrices. Setting the matrices works out the rotation that takes
// `start` to `end` as an axis and an angle; the interpolation (0x00405A20, not yet rewritten) turns part of
// that angle about the axis.
class SlerpInterpolator {
public:
    SlerpInterpolator(const Matrix& start, const Matrix& end);

    // Store both matrices and compute the relative rotation. Returns the "small rotation" flag (the original
    // leaves it in EAX).
    int SetMatrices(const Matrix& start, const Matrix& end);

    // Go the other way round: negate the axis and use 2*pi - angle.
    void InvertDirection();

    Matrix start;
    Matrix end;
    Vector axis;          // w holds 1 / (2 sin angle) when computed from the matrices, else 1
    float angle;          // radians, 0..pi
    bool small_rotation;  // angle below the game's threshold (2 degrees by default)
};

} // namespace Mth
