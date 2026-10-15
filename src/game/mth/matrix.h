#pragma once

#include "game/mth/vector.h"

namespace Mth {

// 4x4 matrix stored as four row vectors.
class Matrix {
public:
    Vector row[4];

    Matrix& operator=(const Matrix& other);
    Vector& operator[](int index);

    // this = rotation * this: rotate about the matrix's own (local) axis.
    Matrix& RotateXLocal(float angle);
    Matrix& RotateYLocal(float angle);
    Matrix& RotateZLocal(float angle);

    // this = this * rotation(axis, angle).
    Matrix& Rotate(const Vector& axis, float angle);

    // Scale all 16 values (skipped when scale is exactly 1).
    Matrix& operator*=(float scale);

    // Rotation from angles about X, Y and Z (radians); translation cleared. Note the X and Z angles turn the
    // opposite way to CreateRotateXMatrix / CreateRotateZMatrix.
    Matrix& SetFromEulerAngles(float x, float y, float z);
};

// Fill `m` with a rotation of `angle` radians about `axis` (normalised internally; a zero axis is used as is).
Matrix& CreateRotateMatrix(Matrix& m, const Vector& axis, float angle);

// Standard row-vector matrix product a * b (the same as D3DXMatrixMultiply). `out` may alias a or b.
Matrix& Multiply(Matrix& out, const Matrix& a, const Matrix& b);

// Fill `m` with a rotation of `angle` radians about one axis and return it.
Matrix& CreateRotateXMatrix(Matrix& m, float angle);
Matrix& CreateRotateYMatrix(Matrix& m, float angle);
Matrix& CreateRotateZMatrix(Matrix& m, float angle);

} // namespace Mth
