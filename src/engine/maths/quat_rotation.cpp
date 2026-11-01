// Quaternions (engine/maths/quat_rotation.h) and the vector rotations made with them. From the disassembly of 0x00420140-0x0042023E,
// 0x00420A50-0x00420B4A, 0x00421440-0x00421484, 0x0042E1F0 and 0x0042E310. The x87 roundings are kept: values the
// original stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "engine/maths/quat_rotation.h"

#include <cmath>

#include "engine/maths/mat4.h"

namespace maths {

namespace {

// The matrix's element at row `r`, column `c`.
double get_at(const Mat4& m, int r, int c) { return reinterpret_cast<const float*>(&m.row[r])[c]; }

} // namespace

// 0x00420AC0
QuatRotation* QuatRotation::init_from_axis_angle(const Vec4& axis, float angle)
{
    double ax = axis.x, ay = axis.y, az = axis.z;
    auto length = static_cast<float>(std::sqrt(ax * ax + ay * ay + az * az));
    double half = static_cast<double>(angle) * 0.5;
    double inverse = length > 0.0f ? 1.0 / length : 0.0;
    auto s = static_cast<float>(std::sin(half) * inverse);
    z = s * axis.z;
    y = s * axis.y;
    x = s * axis.x;
    w = static_cast<float>(std::cos(half));
    return this;
}

// 0x00420140
QuatRotation& QuatRotation::operator*=(const QuatRotation& q)
{
    double ax = x, ay = y, az = z, aw = w;
    double bx = q.x, by = q.y, bz = q.z, bw = q.w;
    double cross0 = bz * ay - by * az;
    auto cross1 = static_cast<float>(az * bx - bz * ax);
    auto cross2 = static_cast<float>(by * ax - bx * ay);
    auto sum0 = static_cast<float>(static_cast<double>(static_cast<float>(bx * aw)) + ax * bw);
    float sum1 = static_cast<float>(by * aw) + static_cast<float>(ay * bw);
    float sum2 = static_cast<float>(bz * aw) + static_cast<float>(az * bw);
    x = static_cast<float>(cross0 + sum0);
    y = sum1 + cross1;
    z = sum2 + cross2;
    w = static_cast<float>(bw * aw - ((bz * az + by * ay) + bx * ax));
    return *this;
}

// 0x00420B50
Vec4* QuatRotation::spin_by(Vec4* out, const Vec4& v) const
{
    QuatRotation conjugate = {-x, -y, -z, w};
    QuatRotation point = {v.x, v.y, v.z, 1.0f};
    QuatRotation result = *this;
    result *= point;
    result *= conjugate;
    *out = {result.x, result.y, result.z, result.w};
    return out;
}

// 0x00420A50
Vec4& Vec4::rotate_about_x(float angle)
{
    double s = std::sin(static_cast<double>(angle));
    double c = std::cos(static_cast<double>(angle));
    double old_y = y;
    y = static_cast<float>(c * y - s * z);
    z = static_cast<float>(c * z + old_y * s);
    return *this;
}

// 0x00420A90
Vec4& Vec4::rotate_about_y(float angle)
{
    double s = std::sin(static_cast<double>(angle));
    double c = std::cos(static_cast<double>(angle));
    double old_x = x;
    x = static_cast<float>(s * z + c * x);
    z = static_cast<float>(-(old_x * s) + c * z);
    return *this;
}

// 0x00421440
Vec4& Vec4::rotate_about_axis(const Vec4& axis, float angle)
{
    QuatRotation rotation;
    rotation.init_from_axis_angle(axis, angle);
    Vec4 turned;
    *this = *rotation.spin_by(&turned, *this);
    return *this;
}

// 0x0042E1F0 (the x87 sums in double; the first branch's 0.5 / s is stored as a float, the second's isn't)
QuatRotation* QuatRotation::from_matrix(const Mat4& m)
{
    double trace = (get_at(m, 1, 1) + get_at(m, 0, 0)) + get_at(m, 2, 2);
    if (trace > 0.0) {
        double s = std::sqrt(trace + 1.0);
        w = static_cast<float>(0.5 * s);
        const auto f = static_cast<float>(0.5 / s);
        x = static_cast<float>((get_at(m, 2, 1) - get_at(m, 1, 2)) * f);
        y = static_cast<float>((get_at(m, 0, 2) - get_at(m, 2, 0)) * f);
        z = static_cast<float>((get_at(m, 1, 0) - get_at(m, 0, 1)) * f);
        return this;
    }
    // From the largest diagonal element.
    int i = get_at(m, 1, 1) > get_at(m, 0, 0) ? 1 : 0;
    if (get_at(m, 2, 2) > get_at(m, i, i))
        i = 2;
    const int j = (i + 1) % 3, k = (j + 1) % 3;
    double s = std::sqrt((get_at(m, i, i) - (get_at(m, k, k) + get_at(m, j, j))) + 1.0);
    float* q = &x;
    q[i] = static_cast<float>(0.5 * s);
    const double f = 0.5 / s;
    q[j] = static_cast<float>((get_at(m, j, i) + get_at(m, i, j)) * f);
    q[k] = static_cast<float>((get_at(m, k, i) + get_at(m, i, k)) * f);
    w = static_cast<float>((get_at(m, k, j) - get_at(m, j, k)) * f);
    return this;
}

// 0x0042E310
QuatRotation* QuatRotation::make_unit(float length)
{
    double current =
        std::sqrt(((static_cast<double>(x) * x + static_cast<double>(y) * y) + static_cast<double>(z) * z) +
                  static_cast<double>(w) * w);
    if (!(current > 0.0))
        return this;
    double scale = length / current;
    x = static_cast<float>(scale * x);
    y = static_cast<float>(scale * y);
    z = static_cast<float>(scale * z);
    w = static_cast<float>(scale * w);
    return this;
}

} // namespace maths
