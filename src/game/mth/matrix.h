#pragma once

#include "game/mth/vector.h"

namespace Mth {

class Matrix;

// A pending product left * right, assigned into a matrix with operator=. (Only the assignment has been seen so
// far; this is likely a small helper type for writing m = a * b without a temporary.)
struct MatrixProduct {
    const Matrix* left;
    const Matrix* right;
};

// 4x4 matrix stored as four row vectors.
class Matrix {
public:
    Vector row[4];

    // this = *product.left * *product.right
    Matrix& operator=(const MatrixProduct& product);

    // Make the rotation rows orthonormal again, keeping row `axis` (0-2) as is: the other two are rebuilt from
    // cross products and normalised. Whichever of them is less perpendicular to the axis row is rebuilt first.
    Matrix& OrthoNormalizeAbout(int axis);

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
    // The reverse: angles (x, y, z, w = 1) that SetFromEulerAngles would turn back into this rotation. When
    // the Y angle is near +/-90 degrees (gimbal lock), X is reported as 0 and the rotation goes into Z.
    Vector& GetEulerAngles(Vector& out) const;

    // Full 4x4 determinant.
    float Determinant() const;
    // this = adjugate of `source` (transposed cofactors). `source` must not be this matrix.
    Matrix& Adjugate(const Matrix& source);
    // Invert in place: adjugate / determinant. Like the original, a zero determinant isn't checked for.
    Matrix& Invert();
};

// Fill `m` with a rotation of `angle` radians about `axis` (normalised internally; a zero axis is used as is).
Matrix& CreateRotateMatrix(Matrix& m, const Vector& axis, float angle);
// The same about coordinate axis 0, 1 or 2 (x, y, z), matching CreateRotateX/Y/ZMatrix.
Matrix& CreateRotateMatrix(Matrix& m, int axis, float angle);

// View matrix looking from `eye` towards `target`. Columns are right = forward x up, up' = right x forward and
// forward; only forward is normalised (as in the original).
Matrix& CreateLookAtMatrix(Matrix& m, const Vector& eye, const Vector& target, const Vector& up);
// Orthographic projection: x scaled by 2/width, y by 2/height, z mapped from [near_z, far_z] to [-1, 1].
Matrix& CreateOrthographicMatrix(Matrix& m, float width, float height, float near_z, float far_z);
// Perspective projection. `fov` is in radians; x is scaled by aspect * cot(fov / 2) and y by cot(fov / 2).
Matrix& CreatePerspectiveMatrix(Matrix& m, float fov, float aspect, float near_z, float far_z);

// Standard row-vector matrix product a * b (the same as D3DXMatrixMultiply). `out` may alias a or b.
Matrix& Multiply(Matrix& out, const Matrix& a, const Matrix& b);

// Fill `m` with a rotation of `angle` radians about one axis and return it.
Matrix& CreateRotateXMatrix(Matrix& m, float angle);
Matrix& CreateRotateYMatrix(Matrix& m, float angle);
Matrix& CreateRotateZMatrix(Matrix& m, float angle);

} // namespace Mth
