#include "engine/maths/mat4.h"
#include "platform/d3dx.h"

#include <cmath>
#include <initializer_list>
#include <utility>
#include <array>

#include "engine/core/dbg.h"
#include "engine/maths/maths.h"
#include "engine/maths/trig.h"
#include "engine/core/checked_index.h"

namespace maths {

// 0x00404080
Vec4& Mat4::operator[](int index) { return core::at(row, index); }

// 0x00404DD0 (the FPU's FSIN / FCOS, as for Y, Z and the axis number below)
Mat4& make_pitch_matrix(Mat4& m, float angle)
{
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    core::at(m.row, 0) = {1.0f, 0.0f, 0.0f, 0.0f};
    core::at(m.row, 1) = {0.0f, c, s, 0.0f};
    core::at(m.row, 2) = {0.0f, -s, c, 0.0f};
    core::at(m.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

// 0x00404E20
Mat4& make_yaw_matrix(Mat4& m, float angle)
{
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    core::at(m.row, 0) = {c, 0.0f, -s, 0.0f};
    core::at(m.row, 1) = {0.0f, 1.0f, 0.0f, 0.0f};
    core::at(m.row, 2) = {s, 0.0f, c, 0.0f};
    core::at(m.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

// 0x00404E70
Mat4& make_roll_matrix(Mat4& m, float angle)
{
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    core::at(m.row, 0) = {c, s, 0.0f, 0.0f};
    core::at(m.row, 1) = {-s, c, 0.0f, 0.0f};
    core::at(m.row, 2) = {0.0f, 0.0f, 1.0f, 0.0f};
    core::at(m.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

Mat4& get_multiply(Mat4& out, const Mat4& a, const Mat4& b)
{
    Mat4 result{};
    for (int i = 0; i < 4; ++i) {
        const float* ra = &core::at(a.row, i).x;
        float* rr = &core::at(result.row, i).x;
        for (int j = 0; j < 4; ++j)
            rr[j] = ra[0] * (&core::at(b.row, 0).x)[j] + ra[1] * (&core::at(b.row, 1).x)[j] + ra[2] * (&core::at(b.row, 2).x)[j] +
                    ra[3] * (&core::at(b.row, 3).x)[j];
    }
    out = result;
    return out;
}

// The originals build the rotation and call the statically linked D3DXMatrixMultiply(this, &rotation, this).

// 0x00405670
Mat4& Mat4::pitch_locally(float angle)
{
    Mat4 rotation{};
    return get_multiply(*this, make_pitch_matrix(rotation, angle), *this);
}

// 0x0041A6A0 / 0x0041A6D0 / 0x0041A700 (the originals: D3DXMatrixMultiply(this, this, &rotation))
Mat4& Mat4::spin_around_x(float angle)
{
    Mat4 rotation{};
    return get_multiply(*this, *this, make_pitch_matrix(rotation, angle));
}

Mat4& Mat4::rotate_y(float angle)
{
    Mat4 rotation{};
    return get_multiply(*this, *this, make_yaw_matrix(rotation, angle));
}

Mat4& Mat4::spin_around_z(float angle)
{
    Mat4 rotation{};
    return get_multiply(*this, *this, make_roll_matrix(rotation, angle));
}

// 0x0041A730 (the original: D3DXMatrixMultiply(this, this, &rotation) for each angle, as spin_around_x / Y / Z)
Mat4& Mat4::init_from_angles(const Vec4& angles)
{
    reset_to_identity();
    if (angles.x != 0.0f || angles.y != 0.0f || angles.z != 0.0f) {
        Mat4 rotation{};
        auto multiply = d3dx::matrix_multiply;
        multiply(this, this, &make_pitch_matrix(rotation, angles.x));
        multiply(this, this, &make_yaw_matrix(rotation, angles.y));
        multiply(this, this, &make_roll_matrix(rotation, angles.z));
    }
    return *this;
}

// 0x0041A640
Mat4& Mat4::reset_to_identity()
{
    core::at(row, 0) = {1.0f, 0.0f, 0.0f, 0.0f};
    core::at(row, 1) = {0.0f, 1.0f, 0.0f, 0.0f};
    core::at(row, 2) = {0.0f, 0.0f, 1.0f, 0.0f};
    core::at(row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    return *this;
}

// 0x0041BA30
Mat4& Mat4::scale_tool(const Vec4& scale)
{
    for (Vec4& r : row) {
        r.x *= scale.x;
        r.y *= scale.y;
        r.z *= scale.z;
        r.w *= scale.w;
    }
    return *this;
}

// 0x0048A230
Mat4& Mat4::get_slide(const Vec4& offset)
{
    core::at(row, 3).x += offset.x;
    core::at(row, 3).y += offset.y;
    core::at(row, 3).z += offset.z;
    core::at(row, 3).w += offset.w;
    return *this;
}

// 0x0048A260. Each sum in the original's order (x87, rounded once): x as (row 1 + row 2) + row 0, y and z as
// (row 1 + row 0) + row 2, then the old value.
Mat4& Mat4::offset_local(const Vec4& offset)
{
    auto term = [&](int r, int c) { return static_cast<double>((&core::at(row, r).x)[c]) * (&offset.x)[r]; };
    core::at(row, 3).x = static_cast<float>(term(1, 0) + term(2, 0) + term(0, 0) + core::at(row, 3).x);
    core::at(row, 3).y = static_cast<float>(term(1, 1) + term(0, 1) + term(2, 1) + core::at(row, 3).y);
    core::at(row, 3).z = static_cast<float>(term(1, 2) + term(0, 2) + term(2, 2) + core::at(row, 3).z);
    return *this;
}

// 0x0041F470. Each component's sum in the original's order (x87, rounded once): the w row's term and the x row's
// first, then z's, then y's (for x: w, z, y, then x's term last).
Vec4 operator*(const Vec4& v, const Mat4& m)
{
    auto term = [&](int r, int c) { return static_cast<double>((&core::at(m.row, r).x)[c]) * (&v.x)[r]; };
    Vec4 out{};
    out.w = static_cast<float>(term(3, 3) + term(0, 3) + term(2, 3) + term(1, 3));
    out.z = static_cast<float>(term(3, 2) + term(0, 2) + term(2, 2) + term(1, 2));
    out.y = static_cast<float>(term(3, 1) + term(0, 1) + term(2, 1) + term(1, 1));
    out.x = static_cast<float>(term(3, 0) + term(2, 0) + term(1, 0) + term(0, 0));
    return out;
}

// 0x00405710
Mat4& Mat4::yaw_locally(float angle)
{
    Mat4 rotation{};
    return get_multiply(*this, make_yaw_matrix(rotation, angle), *this);
}

// 0x004057B0
Mat4& Mat4::roll_in_own_frame(float angle)
{
    Mat4 rotation{};
    return get_multiply(*this, make_roll_matrix(rotation, angle), *this);
}

// 0x00404C60
Mat4& build_axis_angle_matrix(Mat4& m, const Vec4& axis, float angle)
{
    // As the original's x87 code, rounding where it stores: the axis made unit (x and y kept in double, z rounded
    // to float), 1 - cos from the FPU's full-precision cosine, the sine rounded to float, the products as below.
    double x = axis.x, y = axis.y;
    float z = axis.z;
    const double length = std::sqrt((static_cast<double>(z) * z + y * y) + x * x);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        x *= inverse;
        y *= inverse;
        z = static_cast<float>(z * inverse);
    }
    const double t = 1.0 - std::cos(static_cast<double>(angle));
    const auto s = static_cast<float>(std::sin(static_cast<double>(angle)));
    auto zz = static_cast<float>(1.0 - static_cast<double>(z) * z);
    auto xx_t = static_cast<float>((1.0 - x * x) * t);
    auto yy_t = static_cast<float>((1.0 - y * y) * t);
    auto zz_t = static_cast<float>(zz * t);
    auto xy_t = static_cast<float>(static_cast<float>(y * x) * t);
    auto zy_t = static_cast<float>(z * y * t);
    auto zx_t = static_cast<float>(z * x * t);
    auto xs = static_cast<float>(x * s);
    const double ys = y * s;
    const double zs = static_cast<double>(z) * s;
    core::at(m.row, 0) = {static_cast<float>(1.0 - xx_t), static_cast<float>(xy_t + zs), static_cast<float>(zx_t - ys), 0.0f};
    core::at(m.row, 1) = {static_cast<float>(xy_t - zs), static_cast<float>(1.0 - yy_t),
                static_cast<float>(zy_t + static_cast<double>(xs)), 0.0f};
    core::at(m.row, 2) = {static_cast<float>(zx_t + ys), static_cast<float>(zy_t - static_cast<double>(xs)),
                static_cast<float>(1.0 - zz_t), 0.0f};
    core::at(m.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

// 0x00404100 (the original calls the statically linked D3DXMatrixMultiply)
Mat4& Mat4::operator=(const MatrixProduct& product)
{
    get_multiply(*this, *product.left, *product.right);
    return *this;
}

// 0x0040AF60 (x87: each sum in double, rounded once; `in` may be `out`)
Vec4& Mat4::xform(Vec4& out, const Vec4& in) const
{
    const double x = in.x;
    const double y = in.y;
    const double z = in.z;
    const double w = in.w;
    const double rx = ((core::at(row, 3).x * w + core::at(row, 1).x * y) + core::at(row, 2).x * z) + x * core::at(row, 0).x;
    const double ry = ((core::at(row, 3).y * w + core::at(row, 1).y * y) + core::at(row, 0).y * x) + core::at(row, 2).y * z;
    const double rz = ((core::at(row, 3).z * w + core::at(row, 1).z * y) + core::at(row, 0).z * x) + core::at(row, 2).z * z;
    const double rw = ((core::at(row, 3).w * w + core::at(row, 1).w * y) + core::at(row, 0).w * x) + core::at(row, 2).w * z;
    out = {static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz), static_cast<float>(rw)};
    return out;
}

// 0x0040B000
Vec4& Mat4::carry_point_through(Vec4& out, const Vec4& in) const
{
    const double x = in.x;
    const double y = in.y;
    const double z = in.z;
    const double rx = ((core::at(row, 2).x * z + core::at(row, 1).x * y) + x * core::at(row, 0).x) + core::at(row, 3).x;
    const double ry = ((core::at(row, 2).y * z + core::at(row, 1).y * y) + core::at(row, 0).y * x) + core::at(row, 3).y;
    const double rz = ((core::at(row, 2).z * z + core::at(row, 1).z * y) + core::at(row, 0).z * x) + core::at(row, 3).z;
    const double rw = ((core::at(row, 2).w * z + core::at(row, 1).w * y) + core::at(row, 0).w * x) + core::at(row, 3).w;
    out = {static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz), static_cast<float>(rw)};
    return out;
}

// 0x0040B090
Vec4& Mat4::rotate_vector(Vec4& out, const Vec4& in) const
{
    const double x = in.x;
    const double y = in.y;
    const double z = in.z;
    const float w = in.w;
    const double rx = (core::at(row, 2).x * z + core::at(row, 1).x * y) + x * core::at(row, 0).x;
    const double ry = (core::at(row, 2).y * z + core::at(row, 1).y * y) + core::at(row, 0).y * x;
    const double rz = (core::at(row, 2).z * z + core::at(row, 1).z * y) + core::at(row, 0).z * x;
    out = {static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz), w};
    return out;
}

// 0x00416D80
bool Mat4::operator==(const Mat4& other) const
{
    for (int i = 0; i < 4; ++i) {
        const float* mine = &core::at(row, i).x;
        const float* theirs = &core::at(other.row, i).x;
        for (int j = 0; j < 4; ++j) {
            if (!(mine[j] == theirs[j]))
                return false;
        }
    }
    return true;
}

// 0x0040B100
Mat4& Mat4::scale_rows(const Vec4& scale)
{
    const std::array<float, 3> factors = {{scale.x, scale.y, scale.z}};
    for (int i = 0; i < 3; ++i) {
        core::at(row, i).x *= core::at(factors, i);
        core::at(row, i).y *= core::at(factors, i);
        core::at(row, i).z *= core::at(factors, i);
        core::at(row, i).w *= core::at(factors, i);
    }
    return *this;
}

// 0x004062D0
float get_angle_in_plane(const Mat4& m, const Vec4& v, int ref_axis, int normal_axis)
{
    const Vec4& normal = core::at(m.row, normal_axis);
    const Vec4& ref = core::at(m.row, ref_axis);

    Vec4 in_plane = v;
    in_plane.flatten_onto_plane(normal);
    if (in_plane.z * in_plane.z + in_plane.x * in_plane.x + in_plane.y * in_plane.y < 0.0001f)
        return 0.0f;
    in_plane.make_unit();

    float cosine = in_plane.z * ref.z + in_plane.y * ref.y + in_plane.x * ref.x;
    if (!(cosine <= 1.0f)) // NaN also becomes 1, as in the original
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    const float angle = std::acos(cosine);

    // Which way round: compare ref x in_plane with the plane normal on its largest axis.
    const Vec4 cross{ref.y * in_plane.z - ref.z * in_plane.y, ref.z * in_plane.x - ref.x * in_plane.z,
                     ref.x * in_plane.y - ref.y * in_plane.x, 0.0f};
    int axis = 0;
    const bool cross_positive = cross.dominant_axis_index(&axis) > 0.0f;
    const bool normal_positive = (&normal.x)[axis] > 0.0f;
    return cross_positive == normal_positive ? angle : -angle;
}

// 0x00405310 (x87: the trace and the length stay on the FPU; the three differences are stored as floats - z's unrounded
// difference squared against its float - and the angle is FPATAN's)
void Mat4::get_rotation_axis_angle(Vec4& axis, float& angle) const
{
    const double trace = ((static_cast<double>(core::at(row, 1).y) + core::at(row, 0).x) + core::at(row, 2).z) - 1.0; // 2 cos(angle)
    const auto x = static_cast<float>(static_cast<double>(core::at(row, 1).z) - core::at(row, 2).y);
    const auto y = static_cast<float>(static_cast<double>(core::at(row, 2).x) - core::at(row, 0).z);
    const double z_difference = static_cast<double>(core::at(row, 0).y) - core::at(row, 1).x;
    const auto z = static_cast<float>(z_difference);
    const double length = // 2 sin(angle)
        std::sqrt((z_difference * z + static_cast<double>(x) * x) + static_cast<double>(y) * y);

    if (length > 0.0) {
        const double inv = 1.0 / length;
        axis = {static_cast<float>(x * inv), static_cast<float>(y * inv), static_cast<float>(z * inv),
                static_cast<float>(inv)};
    } else { // (also a NaN length)
        axis = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    angle = static_cast<float>(std::atan2(length, trace));

    if (!(length <= 0.01f) || !(trace <= 0.0))
        return;

    // Near 180 degrees the differences above vanish: rebuild the axis from the largest diagonal element.
    // (Compared as in the original, so NaNs choose the same case.)
    const float m00 = core::at(row, 0).x, m11 = core::at(row, 1).y, m22 = core::at(row, 2).z;
    const Vec4 y_largest{core::at(row, 1).x + core::at(row, 0).y, m11 + 1.0f + m11 + 1.0f, core::at(row, 1).z + core::at(row, 2).y, 1.0f};
    const Vec4 x_largest{m00 + 1.0f + m00 + 1.0f, core::at(row, 1).x + core::at(row, 0).y, core::at(row, 0).z + core::at(row, 2).x, 1.0f};
    const Vec4 z_largest{core::at(row, 0).z + core::at(row, 2).x, core::at(row, 1).z + core::at(row, 2).y, m22 + 1.0f + m22 + 1.0f, 1.0f};
    if (!(m00 > m11))
        axis = m11 > m22 ? y_largest : z_largest;
    else
        axis = m00 > m22 ? x_largest : z_largest;
    axis.make_unit();
}

// 0x00405850
void Mat4::turn_in_own_frame(const Vec4& angles)
{
    if (angles.x != 0.0f)
        pitch_locally(angles.x);
    if (angles.y != 0.0f)
        yaw_locally(angles.y);
    if (angles.z != 0.0f)
        roll_in_own_frame(angles.z);
}

// 0x00404BA0
void Mat4::echo_out() const
{
    debug::printf("-----------------\n");
    for (const Vec4& r : row)
        debug::printf("%f %f %f %f\n", r.x, r.y, r.z, r.w);
    debug::printf("-----------------\n");
}

// 0x00405170
Mat4& Mat4::square_up_axes(int axis)
{
    int best_index = axis + 1;
    if (best_index == 3)
        best_index = 0;
    int c_index = best_index + 1;
    if (c_index == 3)
        c_index = 0;
    const Vec4& a = core::at(row, axis);
    Vec4& b = core::at(row, best_index);
    Vec4& c = core::at(row, c_index);

    // b = c x a and c = a x b, written out in the original's operand order.
    auto rebuild_b = [&] {
        b = {a.z * c.y - a.y * c.z, c.z * a.x - a.z * c.x, a.y * c.x - c.y * a.x, 0.0f};
        b.make_unit();
    };
    auto rebuild_c = [&] {
        c = {b.z * a.y - b.y * a.z, b.x * a.z - a.x * b.z, a.x * b.y - b.x * a.y, 0.0f};
        c.make_unit();
    };

    const float c_dot_a = c.x * a.x + c.y * a.y + c.z * a.z;
    const float a_dot_b = a.x * b.x + a.y * b.y + a.z * b.z;
    if (std::fabs(c_dot_a) < std::fabs(a_dot_b)) {
        rebuild_b();
        rebuild_c();
    } else {
        rebuild_c();
        rebuild_b();
    }
    return *this;
}

// 0x00405480 (the lengths at the FPU's 53 bits - row 1's stored as a float first - and each compared with 0.99f,
// 0x00645450: a NaN length counts as long enough)
bool Mat4::restore_right_angles()
{
    constexpr float kMinLength = 0.99f;
    const double length1 = static_cast<float>(
        std::sqrt((static_cast<double>(core::at(row, 1).x) * core::at(row, 1).x + static_cast<double>(core::at(row, 1).y) * core::at(row, 1).y) +
                  static_cast<double>(core::at(row, 1).z) * core::at(row, 1).z));
    const double length2 =
        std::sqrt((static_cast<double>(core::at(row, 2).x) * core::at(row, 2).x + static_cast<double>(core::at(row, 2).y) * core::at(row, 2).y) +
                  static_cast<double>(core::at(row, 2).z) * core::at(row, 2).z);
    const double length0 =
        std::sqrt((static_cast<double>(core::at(row, 0).z) * core::at(row, 0).z + static_cast<double>(core::at(row, 0).x) * core::at(row, 0).x) +
                  static_cast<double>(core::at(row, 0).y) * core::at(row, 0).y);
    const bool short0 = length0 < kMinLength;
    const bool short1 = length1 < kMinLength;
    const bool short2 = length2 < kMinLength;

    auto rebuild = [](Vec4& target, const Vec4& a, const Vec4& b) {
        Vec4 cross = cross_vec(a, b);
        target = cross.make_unit();
    };
    if (short0 + short1 + short2 >= 2) {
        reset_to_identity();
        return true;
    }
    if (short0)
        rebuild(core::at(row, 0), core::at(row, 1), core::at(row, 2));
    else if (short1)
        rebuild(core::at(row, 1), core::at(row, 2), core::at(row, 0));
    else if (short2)
        rebuild(core::at(row, 2), core::at(row, 0), core::at(row, 1));
    else
        return false;
    return true;
}

// 0x00404EC0
Mat4& build_axis_angle_matrix(Mat4& m, int axis, float angle)
{
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    const int a = (axis + 1) % 3;
    const int b = (axis + 2) % 3;
    core::at(m.row, 0) = {1.0f, 0.0f, 0.0f, 0.0f};
    core::at(m.row, 1) = {0.0f, 1.0f, 0.0f, 0.0f};
    core::at(m.row, 2) = {0.0f, 0.0f, 1.0f, 0.0f};
    core::at(m.row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    (&core::at(m.row, a).x)[a] = c;
    (&core::at(m.row, a).x)[b] = s;
    (&core::at(m.row, b).x)[a] = -s;
    (&core::at(m.row, b).x)[b] = c;
    return m;
}

// 0x00404F50
Mat4& create_look_at_matrix(Mat4& m, const Vec4& eye, const Vec4& target, const Vec4& up)
{
    float fx = target.x - eye.x, fy = target.y - eye.y, fz = target.z - eye.z;
    const float length = std::sqrt(fy * fy + fx * fx + fz * fz);
    if (length > 0.0f) {
        const float scale = 1.0f / length;
        fx *= scale;
        fy *= scale;
        fz *= scale;
    }
    // right = forward x up, true_up = right x forward
    const float rx = fy * up.z - fz * up.y;
    const float ry = fz * up.x - fx * up.z;
    const float rz = fx * up.y - fy * up.x;
    const float ux = ry * fz - rz * fy;
    const float uy = rz * fx - rx * fz;
    const float uz = rx * fy - ry * fx;

    core::at(m.row, 0) = {rx, ux, fx, 0.0f};
    core::at(m.row, 1) = {ry, uy, fy, 0.0f};
    core::at(m.row, 2) = {rz, uz, fz, 0.0f};
    core::at(m.row, 3) = {-(rz * eye.z + ry * eye.y + rx * eye.x), -(uz * eye.z + uy * eye.y + ux * eye.x),
                -(fy * eye.y + fx * eye.x + fz * eye.z), 1.0f};
    return m;
}

// 0x00405080
Mat4& create_orthographic_matrix(Mat4& m, float width, float height, float near_depth, float far_depth)
{
    core::at(m.row, 0) = {2.0f / width, 0.0f, 0.0f, 0.0f};
    core::at(m.row, 1) = {0.0f, 2.0f / height, 0.0f, 0.0f};
    core::at(m.row, 2) = {0.0f, 0.0f, 2.0f / (far_depth - near_depth), 0.0f};
    core::at(m.row, 3) = {0.0f, 0.0f, -((near_depth + far_depth) / (far_depth - near_depth)), 1.0f};
    return m;
}

// 0x004050F0
Mat4& create_perspective_matrix(Mat4& m, float fov, float aspect, float near_depth, float far_depth)
{
    // (x87: q and the cotangent of FSIN / FCOS kept on the FPU until stored)
    const double q = far_depth / (static_cast<double>(far_depth) - near_depth);
    const double half_fov = static_cast<double>(fov) * 0.5f;
    const double cot = std::cos(half_fov) / std::sin(half_fov);
    core::at(m.row, 0) = {static_cast<float>(aspect * cot), 0.0f, 0.0f, 0.0f};
    core::at(m.row, 1) = {0.0f, static_cast<float>(cot), 0.0f, 0.0f};
    core::at(m.row, 2) = {0.0f, 0.0f, static_cast<float>(q), 1.0f};
    core::at(m.row, 3) = {0.0f, 0.0f, static_cast<float>(-(q * near_depth)), 0.0f};
    return m;
}

// 0x00405950
Mat4& Mat4::spin_by(const Vec4& axis, float angle)
{
    Mat4 rotation{};
    return get_multiply(*this, *this, build_axis_angle_matrix(rotation, axis, angle));
}

// 0x005772A0 (the original: D3DXMatrixMultiply(this, &rotation, this))
Mat4& Mat4::turn_in_own_frame(const Vec4& axis, float angle)
{
    Mat4 rotation{};
    return get_multiply(*this, build_axis_angle_matrix(rotation, axis, angle), *this);
}

// 0x00404120 (each element as scale * element on the FPU: a NaN times a NaN gives the FPU's NaN)
Mat4& Mat4::operator*=(float scale)
{
    if (scale != 1.0f) {
        for (Vec4& r : row) {
            for (float* e : {&r.x, &r.y, &r.z, &r.w})
                *e = scale * *e;
        }
    }
    return *this;
}

// 0x004048D0
Mat4& Mat4::set_from_euler_angles(float x, float y, float z)
{
    // As the original's x87 code: the cosines and sin z stored as floats, sin x and sin y kept on the FPU at full
    // precision (their products rounded to 53 bits), the sums too until each element is stored; cos x * sin y stored
    // once and reused.
    const float cx = std::cos(x);
    const float cy = std::cos(y);
    const float cz = std::cos(z);
    const float sz = std::sin(z);
    auto cxsy = static_cast<float>(std::sin(static_cast<double>(y)) * cx);
    const double sysx = std::sin(static_cast<double>(y)) * std::sin(static_cast<double>(x));
    auto f = [](double v) { return static_cast<float>(v); };
    core::at(row, 0) = {f(static_cast<double>(cz) * cy), f(-(static_cast<double>(sz) * cy)), -std::sin(y), 0.0f};
    core::at(row, 1) = {f(static_cast<double>(sz) * cx - sysx * cz), f(sysx * sz + static_cast<double>(cz) * cx),
              f(-(std::sin(static_cast<double>(x)) * cy)), 0.0f};
    core::at(row, 2) = {f(static_cast<double>(cxsy) * cz + std::sin(static_cast<double>(x)) * sz),
              f(std::sin(static_cast<double>(x)) * cz - static_cast<double>(cxsy) * sz),
              f(static_cast<double>(cy) * cx), 0.0f};
    core::at(row, 3) = {0.0f, 0.0f, 0.0f, 1.0f};
    return *this;
}

namespace {

// Element (r, c), treating the four row vectors as a 4x4 array.
float get_at(const Mat4& m, int r, int c) { return (&core::at(m.row, r).x)[c]; }

// Determinant of the 3x3 matrix left after removing row `skip_row` and column `skip_col`, expanded along its
// first row in the same order as the original: a(ei - fh) - b(di - fg) + c(dh - eg) - on the FPU (53 bits; the
// float products are exact).
double minor(const Mat4& m, int skip_row, int skip_col)
{
    std::array<int, 3> r{};
    std::array<int, 3> c{};
    for (int i = 0, n = 0; i < 4; ++i)
        if (i != skip_row)
            core::at(r, n++) = i;
    for (int i = 0, n = 0; i < 4; ++i)
        if (i != skip_col)
            core::at(c, n++) = i;
    const double a = get_at(m, core::at(r, 0), core::at(c, 0));
    const double b = get_at(m, core::at(r, 0), core::at(c, 1));
    const double cc = get_at(m, core::at(r, 0), core::at(c, 2));
    const double d = get_at(m, core::at(r, 1), core::at(c, 0));
    const double e = get_at(m, core::at(r, 1), core::at(c, 1));
    const double f = get_at(m, core::at(r, 1), core::at(c, 2));
    const double g = get_at(m, core::at(r, 2), core::at(c, 0));
    const double h = get_at(m, core::at(r, 2), core::at(c, 1));
    const double i = get_at(m, core::at(r, 2), core::at(c, 2));
    return a * (e * i - f * h) - b * (d * i - f * g) + cc * (d * h - e * g);
}

} // namespace

// 0x004041E0
double Mat4::get_det() const
{
    return get_at(*this, 0, 0) * minor(*this, 0, 0) - get_at(*this, 0, 1) * minor(*this, 0, 1) +
           get_at(*this, 0, 2) * minor(*this, 0, 2) - get_at(*this, 0, 3) * minor(*this, 0, 3);
}

// 0x004043A0
Mat4& Mat4::get_adjugate(const Mat4& source)
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            auto cofactor = static_cast<float>(minor(source, c, r));
            (&core::at(row, r).x)[c] = ((r + c) & 1) ? -cofactor : cofactor;
        }
    }
    return *this;
}

// 0x00416DF0 (the dot products summed in double precision)
Mat4& Mat4::invert_orthonormal()
{
    const double tx = core::at(row, 3).x, ty = core::at(row, 3).y, tz = core::at(row, 3).z;
    core::at(row, 3).x = static_cast<float>(-((ty * core::at(row, 0).y + tz * core::at(row, 0).z) + tx * core::at(row, 0).x));
    core::at(row, 3).y = static_cast<float>(-((tz * core::at(row, 1).z + ty * core::at(row, 1).y) + tx * core::at(row, 1).x));
    core::at(row, 3).z = static_cast<float>(-((tz * core::at(row, 2).z + ty * core::at(row, 2).y) + tx * core::at(row, 2).x));
    std::swap(core::at(row, 0).y, core::at(row, 1).x);
    std::swap(core::at(row, 0).z, core::at(row, 2).x);
    std::swap(core::at(row, 1).z, core::at(row, 2).y);
    return *this;
}

// 0x00405600
Mat4& Mat4::upend()
{
    Mat4 adjugate{};
    adjugate.get_adjugate(*this);
    const double determinant = get_det();
    if (determinant != 1.0) // (a NaN scales too)
        adjugate *= static_cast<float>(1.0 / determinant);
    *this = adjugate;
    return *this;
}

// 0x00404AB0
Vec4& Mat4::get_euler_angles(Vec4& out) const
{
    float sin_y = core::at(row, 0).z;
    if (sin_y > 1.0f) // (a NaN passes, as in the original: its asin is NaN)
        sin_y = 1.0f;
    else if (sin_y < -1.0f)
        sin_y = -1.0f;

    // (the original: -asin (its C runtime's), FCOS of it unrounded; the cosine and its reciprocals stay on the FPU
    // until stored)
    bool above = false;
    double inverse = 0.0, neg_inverse = 0.0;
    const float y = maths::neg_asin_cos(sin_y, 0.005f, &above, &inverse, &neg_inverse);
    float x = 0.0f, z = 0.0f;
    if (above) {
        auto inv = static_cast<float>(inverse);
        auto neg_inv = static_cast<float>(neg_inverse);
        x = fast_atan2(neg_inv * core::at(row, 1).z, inv * core::at(row, 2).z);
        z = fast_atan2(neg_inv * core::at(row, 0).y, inv * core::at(row, 0).x);
    } else {
        // Gimbal lock: X and Z rotate about the same axis, so put it all in Z.
        x = 0.0f;
        z = fast_atan2(core::at(row, 1).x, core::at(row, 1).y);
    }
    out = {x, y, z, 1.0f};
    return out;
}

} // namespace maths
