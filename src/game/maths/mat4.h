#pragma once

#include "game/maths/vec4.h"

namespace Maths {

class Mat4;

// A pending product left * right, assigned into a matrix with operator=. (Only the assignment has been seen so
// far; this is likely a small helper type for writing m = a * b without a temporary.)
struct MatrixProduct {
    const Mat4* left;
    const Mat4* right;
};

// 4x4 matrix stored as four row vectors.
class Mat4 {
public:
    Vec4 row[4];

    // this = *product.left * *product.right
    Mat4& operator=(const MatrixProduct& product);

    // The rotation as an axis and angle (radians, 0..pi). The axis is (m12 - m21, m20 - m02, m01 - m10)
    // scaled by 1/length, with w = 1/length; near 180 degrees it is rebuilt from the largest diagonal element
    // and normalised (w = 1). With no rotation the axis is (0, 0, 0, 1).
    void GetRotationAxisAngle(Vec4& axis, float& angle) const;

    // Rotate about the local X, Y and Z axes in turn by angles.x/y/z, skipping any that are exactly 0.
    void TurnInOwnFrame(const Vec4& angles);

    // Debug output of the four rows.
    void EchoOut() const;

    // Make the rotation rows orthonormal again, keeping row `axis` (0-2) as is: the other two are rebuilt from
    // cross products and normalised. Whichever of them is less perpendicular to the axis row is rebuilt first.
    Mat4& SquareUpAxes(int axis);
    // 0x00405480: a rotation row (0-2) shorter than 0.99 (a NaN length isn't) rebuilt as the normalised cross
    // product of the other two; with two or three of them short, the identity. Whether anything was repaired.
    bool RestoreRightAngles();

    Mat4& operator=(const Mat4& other);
    // 0x00416D80: all 16 values equal (NaN never is).
    bool operator==(const Mat4& other) const;
    Vec4& operator[](int index);

    // this = rotation * this: rotate about the matrix's own (local) axis.
    Mat4& PitchLocally(float angle);
    Mat4& YawLocally(float angle);
    Mat4& RollInOwnFrame(float angle);

    // this = this * rotation(axis, angle).
    Mat4& SpinBy(const Vec4& axis, float angle);
    // 0x005772A0: this = rotation(axis, angle) * this (about the matrix's own axes).
    Mat4& TurnInOwnFrame(const Vec4& axis, float angle);

    // Scale all 16 values (skipped when scale is exactly 1).
    Mat4& operator*=(float scale);

    // 0x0040AF60: out = in (all four components) times this matrix.
    Vec4& Xform(Vec4& out, const Vec4& in) const;
    // 0x0040B000: out = in as a point (w taken as 1) times this matrix.
    Vec4& CarryPointThrough(Vec4& out, const Vec4& in) const;
    // 0x0040B090: out = in's x, y, z through the rotation part; w copied.
    Vec4& RotateVector(Vec4& out, const Vec4& in) const;
    // 0x0040B100: rows 0, 1 and 2 (all four components) times scale.x, .y and .z.
    Mat4& ScaleRows(const Vec4& scale);

    // The identity.
    Mat4& ResetToIdentity();
    // Each row's x, y, z and w multiplied by scale.x, .y, .z and .w (the columns scaled: as if followed by a
    // scaling).
    Mat4& StretchBy(const Vec4& scale);
    // row 3 (the translation) += `offset` (all four components).
    Mat4& Slide(const Vec4& offset);
    // 0x0048A260: row 3 (x, y, z) += `offset` along the matrix's own axes (offset.x * row 0 + .y * row 1 + .z * row
    // 2; w untouched).
    Mat4& OffsetLocal(const Vec4& offset);
    // this = this * rotation: rotate about the world's X, Y or Z axis.
    Mat4& SpinAroundX(float angle);
    Mat4& RotateY(float angle);
    Mat4& SpinAroundZ(float angle);

    // Rotation from angles about X, Y and Z (radians); translation cleared. Note the X and Z angles turn the
    // opposite way to MakePitchMatrix / MakeRollMatrix.
    Mat4& SetFromEulerAngles(float x, float y, float z);
    // The reverse: angles (x, y, z, w = 1) that SetFromEulerAngles would turn back into this rotation. When
    // the Y angle is near +/-90 degrees (gimbal lock), X is reported as 0 and the rotation goes into Z.
    Vec4& GetEulerAngles(Vec4& out) const;

    // Full 4x4 determinant (to 53 bits: the original returns it unrounded in ST0, and Invert divides by that).
    double Det() const;
    // this = adjugate of `source` (transposed cofactors). `source` must not be this matrix.
    Mat4& Adjugate(const Mat4& source);
    // Invert in place: adjugate / determinant. Like the original, a zero determinant isn't checked for.
    Mat4& Upend();
    // 0x0041A730: the constructor Mat4(angles): the identity turned about the world's X, Y and Z axes by angles.x,
    // .y and .z in turn (SpinAroundX / RotateY / SpinAroundZ) - all three skipped when they're exactly 0 (NaN isn't). Returns
    // this.
    Mat4& InitFromAngles(const Vec4& angles);
    // 0x00416DF0: invert in place assuming the 3x3 part is orthonormal (a rotation): it's transposed and the
    // translation (row 3's x, y, z) becomes -(translation . each old row). The fourth column isn't touched.
    Mat4& InvertOrthonormal();
};

// Fill `m` with a rotation of `angle` radians about `axis` (normalised internally; a zero axis is used as is).
Mat4& BuildAxisAngleMatrix(Mat4& m, const Vec4& axis, float angle);

// Signed angle (radians) from row `ref_axis` of `m` to `v`, measured in the plane whose normal is row
// `normal_axis`. 0 if `v` has (almost) nothing in that plane.
float GetAngleInPlane(const Mat4& m, const Vec4& v, int ref_axis, int normal_axis);
// The same about coordinate axis 0, 1 or 2 (x, y, z), matching CreateRotateX/Y/ZMatrix.
Mat4& BuildAxisAngleMatrix(Mat4& m, int axis, float angle);

// View matrix looking from `eye` towards `target`. Columns are right = forward x up, up' = right x forward and
// forward; only forward is normalised (as in the original).
Mat4& CreateLookAtMatrix(Mat4& m, const Vec4& eye, const Vec4& target, const Vec4& up);
// Orthographic projection: x scaled by 2/width, y by 2/height, z mapped from [nearDepth, farDepth] to [-1, 1].
Mat4& CreateOrthographicMatrix(Mat4& m, float width, float height, float nearDepth, float farDepth);
// Perspective projection. `fov` is in radians; x is scaled by aspect * cot(fov / 2) and y by cot(fov / 2).
Mat4& CreatePerspectiveMatrix(Mat4& m, float fov, float aspect, float nearDepth, float farDepth);

// The row vector `v` (all four components) times `m`.
Vec4 operator*(const Vec4& v, const Mat4& m);

// Standard row-vector matrix product a * b (the same as D3DXMatrixMultiply). `out` may alias a or b.
Mat4& Multiply(Mat4& out, const Mat4& a, const Mat4& b);

// Fill `m` with a rotation of `angle` radians about one axis and return it.
Mat4& MakePitchMatrix(Mat4& m, float angle);
Mat4& MakeYawMatrix(Mat4& m, float angle);
Mat4& MakeRollMatrix(Mat4& m, float angle);

} // namespace Mth
