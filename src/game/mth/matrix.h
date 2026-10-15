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
};

// Standard row-vector matrix product a * b (the same as D3DXMatrixMultiply). `out` may alias a or b.
Matrix& Multiply(Matrix& out, const Matrix& a, const Matrix& b);

// Fill `m` with a rotation of `angle` radians about one axis and return it.
Matrix& CreateRotateXMatrix(Matrix& m, float angle);
Matrix& CreateRotateYMatrix(Matrix& m, float angle);
Matrix& CreateRotateZMatrix(Matrix& m, float angle);

} // namespace Mth
