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

    // The rotation as an axis and angle (radians, 0..pi). The axis is (m12 - m21, m20 - m02, m01 - m10)
    // scaled by 1/length, with w = 1/length; near 180 degrees it is rebuilt from the largest diagonal element
    // and normalised (w = 1). With no rotation the axis is (0, 0, 0, 1).
    void GetRotationAxisAngle(Vector& axis, float& angle) const;

    // Rotate about the local X, Y and Z axes in turn by angles.x/y/z, skipping any that are exactly 0.
    void RotateLocal(const Vector& angles);

    // Debug output of the four rows.
    void Print() const;

    // Make the rotation rows orthonormal again, keeping row `axis` (0-2) as is: the other two are rebuilt from
    // cross products and normalised. Whichever of them is less perpendicular to the axis row is rebuilt first.
    Matrix& OrthoNormalizeAbout(int axis);
    // 0x00405480: a rotation row (0-2) shorter than 0.99 (a NaN length isn't) rebuilt as the normalised cross
    // product of the other two; with two or three of them short, the identity. Whether anything was repaired.
    bool PatchOrthogonality();

    Matrix& operator=(const Matrix& other);
    // 0x00416D80: all 16 values equal (NaN never is).
    bool operator==(const Matrix& other) const;
    Vector& operator[](int index);

    // this = rotation * this: rotate about the matrix's own (local) axis.
    Matrix& RotateXLocal(float angle);
    Matrix& RotateYLocal(float angle);
    Matrix& RotateZLocal(float angle);

    // this = this * rotation(axis, angle).
    Matrix& Rotate(const Vector& axis, float angle);
    // 0x005772A0: this = rotation(axis, angle) * this (about the matrix's own axes).
    Matrix& RotateLocal(const Vector& axis, float angle);

    // Scale all 16 values (skipped when scale is exactly 1).
    Matrix& operator*=(float scale);

    // 0x0040AF60: out = in (all four components) times this matrix.
    Vector& Transform(Vector& out, const Vector& in) const;
    // 0x0040B000: out = in as a point (w taken as 1) times this matrix.
    Vector& TransformAsPos(Vector& out, const Vector& in) const;
    // 0x0040B090: out = in's x, y, z through the rotation part; w copied.
    Vector& RotateVector(Vector& out, const Vector& in) const;
    // 0x0040B100: rows 0, 1 and 2 (all four components) times scale.x, .y and .z.
    Matrix& ScaleRows(const Vector& scale);

    // The identity.
    Matrix& Ident();
    // Each row's x, y, z and w multiplied by scale.x, .y, .z and .w (the columns scaled: as if followed by a
    // scaling).
    Matrix& Scale(const Vector& scale);
    // row 3 (the translation) += `offset` (all four components).
    Matrix& Translate(const Vector& offset);
    // 0x0048A260: row 3 (x, y, z) += `offset` along the matrix's own axes (offset.x * row 0 + .y * row 1 + .z * row
    // 2; w untouched).
    Matrix& TranslateLocal(const Vector& offset);
    // this = this * rotation: rotate about the world's X, Y or Z axis.
    Matrix& RotateX(float angle);
    Matrix& RotateY(float angle);
    Matrix& RotateZ(float angle);

    // Rotation from angles about X, Y and Z (radians); translation cleared. Note the X and Z angles turn the
    // opposite way to CreateRotateXMatrix / CreateRotateZMatrix.
    Matrix& SetFromEulerAngles(float x, float y, float z);
    // The reverse: angles (x, y, z, w = 1) that SetFromEulerAngles would turn back into this rotation. When
    // the Y angle is near +/-90 degrees (gimbal lock), X is reported as 0 and the rotation goes into Z.
    Vector& GetEulerAngles(Vector& out) const;

    // Full 4x4 determinant (to 53 bits: the original returns it unrounded in ST0, and Invert divides by that).
    double Determinant() const;
    // this = adjugate of `source` (transposed cofactors). `source` must not be this matrix.
    Matrix& Adjugate(const Matrix& source);
    // Invert in place: adjugate / determinant. Like the original, a zero determinant isn't checked for.
    Matrix& Invert();
    // 0x0041A730: the constructor Matrix(angles): the identity turned about the world's X, Y and Z axes by angles.x,
    // .y and .z in turn (RotateX / RotateY / RotateZ) - all three skipped when they're exactly 0 (NaN isn't). Returns
    // this.
    Matrix& InitFromAngles(const Vector& angles);
    // 0x00416DF0: invert in place assuming the 3x3 part is orthonormal (a rotation): it's transposed and the
    // translation (row 3's x, y, z) becomes -(translation . each old row). The fourth column isn't touched.
    Matrix& InvertOrthonormal();
};

// Fill `m` with a rotation of `angle` radians about `axis` (normalised internally; a zero axis is used as is).
Matrix& CreateRotateMatrix(Matrix& m, const Vector& axis, float angle);

// Signed angle (radians) from row `ref_axis` of `m` to `v`, measured in the plane whose normal is row
// `normal_axis`. 0 if `v` has (almost) nothing in that plane.
float GetAngleInPlane(const Matrix& m, const Vector& v, int ref_axis, int normal_axis);
// The same about coordinate axis 0, 1 or 2 (x, y, z), matching CreateRotateX/Y/ZMatrix.
Matrix& CreateRotateMatrix(Matrix& m, int axis, float angle);

// View matrix looking from `eye` towards `target`. Columns are right = forward x up, up' = right x forward and
// forward; only forward is normalised (as in the original).
Matrix& CreateLookAtMatrix(Matrix& m, const Vector& eye, const Vector& target, const Vector& up);
// Orthographic projection: x scaled by 2/width, y by 2/height, z mapped from [near_z, far_z] to [-1, 1].
Matrix& CreateOrthographicMatrix(Matrix& m, float width, float height, float near_z, float far_z);
// Perspective projection. `fov` is in radians; x is scaled by aspect * cot(fov / 2) and y by cot(fov / 2).
Matrix& CreatePerspectiveMatrix(Matrix& m, float fov, float aspect, float near_z, float far_z);

// The row vector `v` (all four components) times `m`.
Vector operator*(const Vector& v, const Matrix& m);

// Standard row-vector matrix product a * b (the same as D3DXMatrixMultiply). `out` may alias a or b.
Matrix& Multiply(Matrix& out, const Matrix& a, const Matrix& b);

// Fill `m` with a rotation of `angle` radians about one axis and return it.
Matrix& CreateRotateXMatrix(Matrix& m, float angle);
Matrix& CreateRotateYMatrix(Matrix& m, float angle);
Matrix& CreateRotateZMatrix(Matrix& m, float angle);

} // namespace Mth
