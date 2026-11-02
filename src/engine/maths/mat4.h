#pragma once

#include "engine/maths/vec4.h"
#include <array>
#include <span>

namespace maths {

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
    std::array<Vec4, 4> row;

    // this = *product.left * *product.right
    Mat4& operator=(const MatrixProduct& product);

    // The rotation as an axis and angle (radians, 0..pi). The axis is (m12 - m21, m20 - m02, m01 - m10)
    // scaled by 1/length, with w = 1/length; near 180 degrees it is rebuilt from the largest diagonal element
    // and normalised (w = 1). With no rotation the axis is (0, 0, 0, 1).
    void get_rotation_axis_angle(Vec4& axis, float& angle) const;

    // Rotate about the local X, Y and Z axes in turn by angles.x/y/z, skipping any that are exactly 0.
    void turn_in_own_frame(const Vec4& angles);

    // Debug output of the four rows.
    void echo_out() const;

    // Make the rotation rows orthonormal again, keeping row `axis` (0-2) as is: the other two are rebuilt from
    // cross products and normalised. Whichever of them is less perpendicular to the axis row is rebuilt first.
    Mat4& square_up_axes(int axis);
    // 0x00405480: a rotation row (0-2) shorter than 0.99 (a NaN length isn't) rebuilt as the normalised cross
    // product of the other two; with two or three of them short, the identity. Whether anything was repaired.
    bool restore_right_angles();

    // (0x00404090: the copy assignment, the four rows - the implicit one; Mat4 stays trivially copyable)
    // 0x00416D80: all 16 values equal (NaN never is).
    bool operator==(const Mat4& other) const;
    Vec4& operator[](int index);
    // The 16 values, row by row, as an array of floats (for D3D / D3DX and the original's float-array code).
    float* data() { return row.front().data(); }
    [[nodiscard]] const float* data() const { return row.front().data(); }
    // The same 16 values as a span (checked element access; the rows are contiguous).
    std::span<float, 16> floats() { return std::span<float, 16>(data(), 16); }
    [[nodiscard]] std::span<const float, 16> floats() const { return std::span<const float, 16>(data(), 16); }

    // this = rotation * this: rotate about the matrix's own (local) axis.
    Mat4& pitch_locally(float angle);
    Mat4& yaw_locally(float angle);
    Mat4& roll_in_own_frame(float angle);

    // this = this * rotation(axis, angle).
    Mat4& spin_by(const Vec4& axis, float angle);
    // 0x005772A0: this = rotation(axis, angle) * this (about the matrix's own axes).
    Mat4& turn_in_own_frame(const Vec4& axis, float angle);

    // Scale all 16 values (skipped when scale is exactly 1).
    Mat4& operator*=(float scale);

    // 0x0040AF60: out = in (all four components) times this matrix.
    Vec4& xform(Vec4& out, const Vec4& in) const;
    // 0x0040B000: out = in as a point (w taken as 1) times this matrix.
    Vec4& carry_point_through(Vec4& out, const Vec4& in) const;
    // 0x0040B090: out = in's x, y, z through the rotation part; w copied.
    Vec4& rotate_vector(Vec4& out, const Vec4& in) const;
    // 0x0040B100: rows 0, 1 and 2 (all four components) times scale.x, .y and .z.
    Mat4& scale_rows(const Vec4& scale);

    // The identity.
    Mat4& reset_to_identity();
    // Each row's x, y, z and w multiplied by scale.x, .y, .z and .w (the columns scaled: as if followed by a
    // scaling).
    Mat4& scale_tool(const Vec4& scale);
    // row 3 (the translation) += `offset` (all four components).
    Mat4& get_slide(const Vec4& offset);
    // 0x0048A260: row 3 (x, y, z) += `offset` along the matrix's own axes (offset.x * row 0 + .y * row 1 + .z * row
    // 2; w untouched).
    Mat4& offset_local(const Vec4& offset);
    // this = this * rotation: rotate about the world's X, Y or Z axis.
    Mat4& spin_around_x(float angle);
    Mat4& rotate_y(float angle);
    Mat4& spin_around_z(float angle);

    // Rotation from angles about X, Y and Z (radians); translation cleared. Note the X and Z angles turn the
    // opposite way to make_pitch_matrix / make_roll_matrix.
    Mat4& set_from_euler_angles(float x, float y, float z);
    // The reverse: angles (x, y, z, w = 1) that set_from_euler_angles would turn back into this rotation. When
    // the Y angle is near +/-90 degrees (gimbal lock), X is reported as 0 and the rotation goes into Z.
    Vec4& get_euler_angles(Vec4& out) const;

    // Full 4x4 determinant (to 53 bits: the original returns it unrounded in ST0, and Invert divides by that).
    [[nodiscard]] double get_det() const;
    // this = adjugate of `source` (transposed cofactors). `source` must not be this matrix.
    Mat4& get_adjugate(const Mat4& source);
    // Invert in place: adjugate / determinant. Like the original, a zero determinant isn't checked for.
    Mat4& upend();
    // 0x0041A730: the constructor Mat4(angles): the identity turned about the world's X, Y and Z axes by angles.x,
    // .y and .z in turn (spin_around_x / RotateY / spin_around_z) - all three skipped when they're exactly 0 (NaN isn't). Returns
    // this.
    Mat4& init_from_angles(const Vec4& angles);
    // 0x00416DF0: invert in place assuming the 3x3 part is orthonormal (a rotation): it's transposed and the
    // translation (row 3's x, y, z) becomes -(translation . each old row). The fourth column isn't touched.
    Mat4& invert_orthonormal();
};

// Fill `m` with a rotation of `angle` radians about `axis` (normalised internally; a zero axis is used as is).
Mat4& build_axis_angle_matrix(Mat4& m, const Vec4& axis, float angle);

// Signed angle (radians) from row `ref_axis` of `m` to `v`, measured in the plane whose normal is row
// `normal_axis`. 0 if `v` has (almost) nothing in that plane.
float get_angle_in_plane(const Mat4& m, const Vec4& v, int ref_axis, int normal_axis);
// The same about coordinate axis 0, 1 or 2 (x, y, z), matching CreateRotateX/Y/ZMatrix.
Mat4& build_axis_angle_matrix(Mat4& m, int axis, float angle);

// View matrix looking from `eye` towards `target`. Columns are right = forward x up, up' = right x forward and
// forward; only forward is normalised (as in the original).
Mat4& create_look_at_matrix(Mat4& m, const Vec4& eye, const Vec4& target, const Vec4& up);
// Orthographic projection: x scaled by 2/width, y by 2/height, z mapped from [near_depth, far_depth] to [-1, 1].
Mat4& create_orthographic_matrix(Mat4& m, float width, float height, float near_depth, float far_depth);
// Perspective projection. `fov` is in radians; x is scaled by aspect * cot(fov / 2) and y by cot(fov / 2).
Mat4& create_perspective_matrix(Mat4& m, float fov, float aspect, float near_depth, float far_depth);

// The row vector `v` (all four components) times `m`.
Vec4 operator*(const Vec4& v, const Mat4& m);

// Standard row-vector matrix product a * b (the same as D3DXMatrixMultiply). `out` may alias a or b.
Mat4& get_multiply(Mat4& out, const Mat4& a, const Mat4& b);

// Fill `m` with a rotation of `angle` radians about one axis and return it.
Mat4& make_pitch_matrix(Mat4& m, float angle);
Mat4& make_yaw_matrix(Mat4& m, float angle);
Mat4& make_roll_matrix(Mat4& m, float angle);

} // namespace maths
