#pragma once

#include "engine/maths/vec4.h"

namespace maths {

class Mat4;

// A rotation quaternion (x, y, z the axis times sin(angle / 2), w cos(angle / 2)): the model component's rotations and
// the rigid body's orientation. To a matrix: fetch_mat.
class QuatRotation {
public:
    // 0x00420AC0: the rotation of `angle` radians about `axis` (normalised here; a zero axis gives (0, 0, 0)). The
    // sines and cosines are the FPU's. Returns this.
    QuatRotation* init_from_axis_angle(const Vec4& axis, float angle);
    // 0x0042E1F0 (a constructor: returns this): the rotation of `matrix` (its upper 3x3): from its trace when that's
    // positive, else from its largest diagonal element.
    QuatRotation* from_matrix(const Mat4& matrix);
    // 0x00420140: this = this * q (the roundings where the original stores).
    QuatRotation& operator*=(const QuatRotation& q);
    // 0x00420B50: `v` turned by this rotation (this * (v, 1) * conjugate), into `out` (w: what the product leaves).
    // Returns out.
    Vec4* spin_by(Vec4* out, const Vec4& v) const;
    // 0x0042E310: scaled to `length` (left as it is when its length is 0 or not a number). Returns this.
    QuatRotation* make_unit(float length);
    // 0x0042BC40 (rigidbody_math.cpp): the rotation matrix (the upper 3x3 of `out`; the rest untouched) - scaled by
    // 2 / the squared length (0 for a zero or not-a-number quaternion). Returns out.
    Mat4* fetch_mat(Mat4* out) const;

    // The four components as an array of floats (for the APIs that take one), as Vec4::data().
    float* data() { return &x; }
    [[nodiscard]] const float* data() const { return &x; }

    float x, y, z, w;
};

// Quaternions kept as a Vec4 (x, y, z, w) - the objects' and nodes' orientations - turned into and from one.
inline QuatRotation quat_of(const Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z, .w = v.w}; }
inline Vec4 vec4_of(const QuatRotation& q) { return {.x = q.x, .y = q.y, .z = q.z, .w = q.w}; }
// fetch_mat / from_matrix / make_unit / *= on a quaternion kept as a Vec4.
inline void quat_to_matrix(const Vec4& quat, Mat4* matrix) { quat_of(quat).fetch_mat(matrix); }
inline Vec4 quat_from_matrix(const Mat4& matrix)
{
    QuatRotation q{};
    q.from_matrix(matrix);
    return vec4_of(q);
}
inline void quat_make_unit(Vec4* quat, float length)
{
    QuatRotation q = quat_of(*quat);
    q.make_unit(length);
    *quat = vec4_of(q);
}
inline void quat_multiply(Vec4* a, const Vec4& b)
{
    QuatRotation q = quat_of(*a);
    q *= quat_of(b);
    *a = vec4_of(q);
}

} // namespace maths
