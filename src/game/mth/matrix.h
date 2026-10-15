#pragma once

#include "game/mth/vector.h"

namespace Mth {

// 4x4 matrix stored as four row vectors.
class Matrix {
public:
    Vector row[4];

    Matrix& operator=(const Matrix& other);
};

// Fill `m` with a rotation of `angle` radians about one axis and return it.
Matrix& CreateRotateXMatrix(Matrix& m, float angle);
Matrix& CreateRotateYMatrix(Matrix& m, float angle);
Matrix& CreateRotateZMatrix(Matrix& m, float angle);

} // namespace Mth
