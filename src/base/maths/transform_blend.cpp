// NOT ORIGINAL - transforms blended between two simulation ticks (see transform_blend.h).

#include "base/maths/transform_blend.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "base/core/checked_index.h"

namespace maths::transform_blend {

namespace {

using Axis = std::array<double, 3>;

constexpr double kShortestAxis = 1e-6;    // an axis shorter than this: not a rotation and scale
constexpr double kMostShear = 2e-3;       // two axes' cosine above this: sheared
constexpr double kNearlyEqual = 0.9995;   // quaternions this close are blended linearly (then normalised)

double dot(const Axis& a, const Axis& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); }
Axis cross(const Axis& a, const Axis& b)
{
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}
Axis scaled(const Axis& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
Axis minus(const Axis& a, const Axis& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double length(const Axis& a) { return std::sqrt(dot(a, a)); }

Axis axis_of(const Mat4& matrix, int row)
{
    const Vec4& v = core::at(matrix.row, row);
    return {v.x, v.y, v.z};
}

bool same_bits(const Mat4& a, const Mat4& b) { return std::memcmp(a.data(), b.data(), sizeof(float) * kMatrixFloats) == 0; }

// The quaternion of an orthonormal, right-handed basis (rows u0, u1, u2). The rotation's column-vector matrix is the
// rows' transpose: m[i][j] = u_j[i].
Quat quat_of(const std::array<Axis, 3>& u)
{
    auto m = [&](int i, int j) { return core::at(core::at(u, j), i); };
    const double trace = m(0, 0) + m(1, 1) + m(2, 2);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        w = 0.25 * s;
        x = (m(2, 1) - m(1, 2)) / s;
        y = (m(0, 2) - m(2, 0)) / s;
        z = (m(1, 0) - m(0, 1)) / s;
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const double s = std::sqrt(1.0 + m(0, 0) - m(1, 1) - m(2, 2)) * 2.0;
        w = (m(2, 1) - m(1, 2)) / s;
        x = 0.25 * s;
        y = (m(0, 1) + m(1, 0)) / s;
        z = (m(0, 2) + m(2, 0)) / s;
    } else if (m(1, 1) > m(2, 2)) {
        const double s = std::sqrt(1.0 + m(1, 1) - m(0, 0) - m(2, 2)) * 2.0;
        w = (m(0, 2) - m(2, 0)) / s;
        x = (m(0, 1) + m(1, 0)) / s;
        y = 0.25 * s;
        z = (m(1, 2) + m(2, 1)) / s;
    } else {
        const double s = std::sqrt(1.0 + m(2, 2) - m(0, 0) - m(1, 1)) * 2.0;
        w = (m(1, 0) - m(0, 1)) / s;
        x = (m(0, 2) + m(2, 0)) / s;
        y = (m(1, 2) + m(2, 1)) / s;
        z = 0.25 * s;
    }
    const double n = std::sqrt((x * x) + (y * y) + (z * z) + (w * w));
    return {static_cast<float>(x / n), static_cast<float>(y / n), static_cast<float>(z / n), static_cast<float>(w / n)};
}

} // namespace

bool decompose(const Mat4& matrix, Pose* pose)
{
    std::array<Axis, 3> u{axis_of(matrix, 0), axis_of(matrix, 1), axis_of(matrix, 2)};
    std::array<double, 3> s{};
    for (int i = 0; i < 3; ++i) {
        const double len = length(core::at(u, i));
        if (!std::isfinite(len) || len < kShortestAxis) {
            return false;
        }
        core::at(s, i) = len;
        core::at(u, i) = scaled(core::at(u, i), 1.0 / len);
    }
    if (std::fabs(dot(u[0], u[1])) > kMostShear || std::fabs(dot(u[0], u[2])) > kMostShear ||
        std::fabs(dot(u[1], u[2])) > kMostShear) {
        return false;
    }
    if (dot(u[0], cross(u[1], u[2])) < 0.0) { // mirrored: the X axis' scale negative
        s[0] = -s[0];
        u[0] = scaled(u[0], -1.0);
    }
    // (made exactly orthonormal: Gram-Schmidt, the Z axis from the other two)
    u[1] = minus(u[1], scaled(u[0], dot(u[1], u[0])));
    u[1] = scaled(u[1], 1.0 / length(u[1]));
    u[2] = cross(u[0], u[1]);
    pose->rotation = quat_of(u);
    for (int i = 0; i < 3; ++i) {
        core::at(pose->scale, i) = static_cast<float>(core::at(s, i));
    }
    const Vec4& t = core::at(matrix.row, 3);
    pose->translation = {t.x, t.y, t.z};
    const float* values = matrix.data();
    for (int i = 0; i < kMatrixFloats; ++i) {
        if (!std::isfinite(values[i])) { // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            return false;
        }
    }
    return true;
}

Mat4 compose(const Pose& pose)
{
    const double x = pose.rotation[0];
    const double y = pose.rotation[1];
    const double z = pose.rotation[2];
    const double w = pose.rotation[3];
    // (the column-vector matrix m; the rows of ours are its columns)
    const std::array<std::array<double, 3>, 3> m = {{
        {1.0 - (2.0 * ((y * y) + (z * z))), 2.0 * ((x * y) - (z * w)), 2.0 * ((x * z) + (y * w))},
        {2.0 * ((x * y) + (z * w)), 1.0 - (2.0 * ((x * x) + (z * z))), 2.0 * ((y * z) - (x * w))},
        {2.0 * ((x * z) - (y * w)), 2.0 * ((y * z) + (x * w)), 1.0 - (2.0 * ((x * x) + (y * y)))},
    }};
    Mat4 out{};
    for (int row = 0; row < 3; ++row) {
        const double s = core::at(pose.scale, row);
        Vec4& axis = core::at(out.row, row);
        axis.x = static_cast<float>(m[0][row] * s);
        axis.y = static_cast<float>(m[1][row] * s);
        axis.z = static_cast<float>(m[2][row] * s);
        axis.w = 0.0F;
    }
    core::at(out.row, 3) = {.x = pose.translation[0], .y = pose.translation[1], .z = pose.translation[2], .w = 1.0F};
    return out;
}

Quat slerp(const Quat& from, const Quat& to, float alpha)
{
    double d = 0.0;
    for (int i = 0; i < 4; ++i) {
        d += static_cast<double>(core::at(from, i)) * core::at(to, i);
    }
    double sign = 1.0;
    if (d < 0.0) { // (the other way round is shorter)
        d = -d;
        sign = -1.0;
    }
    double a = 1.0 - alpha;
    double b = alpha * sign;
    if (d < kNearlyEqual) {
        const double theta = std::acos(std::min(d, 1.0));
        const double sin_theta = std::sin(theta);
        a = std::sin((1.0 - alpha) * theta) / sin_theta;
        b = sign * std::sin(alpha * theta) / sin_theta;
    }
    std::array<double, 4> q{};
    double n = 0.0;
    for (int i = 0; i < 4; ++i) {
        core::at(q, i) = (a * core::at(from, i)) + (b * core::at(to, i));
        n += core::at(q, i) * core::at(q, i);
    }
    n = std::sqrt(n);
    return {static_cast<float>(q[0] / n), static_cast<float>(q[1] / n), static_cast<float>(q[2] / n),
            static_cast<float>(q[3] / n)};
}

float angle_between(const Quat& from, const Quat& to)
{
    double d = 0.0;
    for (int i = 0; i < 4; ++i) {
        d += static_cast<double>(core::at(from, i)) * core::at(to, i);
    }
    return static_cast<float>(2.0 * std::acos(std::min(std::fabs(d), 1.0)));
}

Mat4 blend(const Mat4& from, const Mat4& to, float alpha)
{
    if (!(alpha > 0.0F) || same_bits(from, to)) { // (NaN: the earlier one)
        return alpha >= 1.0F ? to : from;
    }
    if (alpha >= 1.0F) {
        return to;
    }
    Pose a;
    Pose b;
    if (!decompose(from, &a) || !decompose(to, &b)) {
        return to;
    }
    Pose mixed;
    mixed.rotation = slerp(a.rotation, b.rotation, alpha);
    for (int i = 0; i < 3; ++i) {
        core::at(mixed.scale, i) = static_cast<float>(core::at(a.scale, i) + ((static_cast<double>(core::at(b.scale, i)) - core::at(a.scale, i)) * alpha));
        core::at(mixed.translation, i) = static_cast<float>(
            core::at(a.translation, i) + ((static_cast<double>(core::at(b.translation, i)) - core::at(a.translation, i)) * alpha));
    }
    Mat4 out = compose(mixed);
    // (the rows' w as the later matrix has them)
    for (int row = 0; row < 4; ++row) {
        core::at(out.row, row).w = core::at(to.row, row).w;
    }
    return out;
}

bool is_jump(const Mat4& from, const Mat4& to, const JumpLimits& limits)
{
    if (same_bits(from, to)) {
        return false;
    }
    Pose a;
    Pose b;
    if (!decompose(from, &a) || !decompose(to, &b)) {
        return true;
    }
    const double dx = static_cast<double>(b.translation[0]) - a.translation[0];
    const double dy = static_cast<double>(b.translation[1]) - a.translation[1];
    const double dz = static_cast<double>(b.translation[2]) - a.translation[2];
    if ((dx * dx) + (dy * dy) + (dz * dz) > static_cast<double>(limits.distance) * limits.distance) {
        return true;
    }
    // (a mirror that comes or goes is a jump too)
    if ((a.scale[0] < 0.0F) != (b.scale[0] < 0.0F)) {
        return true;
    }
    return angle_between(a.rotation, b.rotation) > limits.angle;
}

} // namespace maths::transform_blend
