// Quaternions (engine/maths/quat_rotation.h) and the vector rotations made with them. From the disassembly of 0x00420140-0x0042023E,
// 0x00420A50-0x00420B4A, 0x00421440-0x00421484, 0x0042E1F0 and 0x0042E310. The x87 roundings are kept: values the
// original stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "engine/maths/quat_rotation.h"

#include <array>
#include <cmath>

#include "engine/maths/mat4.h"
#include "engine/core/checked_index.h"

namespace maths {

namespace {
constexpr double kHalfAngleScale = 0.5; // the quaternion's components are half-angle values

// The matrix's element at row `r`, column `c`.
double get_at(const Mat4& matrix, int row_index, int column) { return core::at(matrix.row, row_index)[column]; }

} // namespace

// 0x00420AC0
QuatRotation* QuatRotation::init_from_axis_angle(const Vec4& axis, float angle)
{
    const double ax = axis.x;
    const double ay = axis.y;
    const double az = axis.z;
    auto length = static_cast<float>(std::sqrt((ax * ax) + (ay * ay) + (az * az)));
    const double half = static_cast<double>(angle) * 0.5;
    const double inverse = length > 0.0F ? 1.0 / length : 0.0;
    auto sin_half = static_cast<float>(std::sin(half) * inverse);
    z = sin_half * axis.z;
    y = sin_half * axis.y;
    x = sin_half * axis.x;
    w = static_cast<float>(std::cos(half));
    return this;
}

// 0x00420140
QuatRotation& QuatRotation::operator*=(const QuatRotation& other)
{
    const double ax = x;
    const double ay = y;
    const double az = z;
    const double aw = w;
    const double bx = other.x;
    const double by = other.y;
    const double bz = other.z;
    const double bw = other.w;
    const double cross0 = (bz * ay) - (by * az);
    auto cross1 = static_cast<float>((az * bx) - (bz * ax));
    auto cross2 = static_cast<float>((by * ax) - (bx * ay));
    auto sum0 = static_cast<float>(static_cast<double>(static_cast<float>(bx * aw)) + (ax * bw));
    const float sum1 = static_cast<float>(by * aw) + static_cast<float>(ay * bw);
    const float sum2 = static_cast<float>(bz * aw) + static_cast<float>(az * bw);
    x = static_cast<float>(cross0 + sum0);
    y = sum1 + cross1;
    z = sum2 + cross2;
    w = static_cast<float>((bw * aw) - (((bz * az) + (by * ay)) + (bx * ax)));
    return *this;
}

// 0x00420B50
Vec4* QuatRotation::spin_by(Vec4* out, const Vec4& v) const
{
    const QuatRotation conjugate = {.x = -x, .y = -y, .z = -z, .w = w};
    const QuatRotation point = {.x = v.x, .y = v.y, .z = v.z, .w = 1.0F};
    QuatRotation result = *this;
    result *= point;
    result *= conjugate;
    *out = {.x = result.x, .y = result.y, .z = result.z, .w = result.w};
    return out;
}

// 0x00420A50
Vec4& Vec4::rotate_about_x(float angle)
{
    const double sine = std::sin(static_cast<double>(angle));
    const double cosine = std::cos(static_cast<double>(angle));
    const double old_y = y;
    y = static_cast<float>((cosine * y) - (sine * z));
    z = static_cast<float>((cosine * z) + (old_y * sine));
    return *this;
}

// 0x00420A90
Vec4& Vec4::rotate_about_y(float angle)
{
    const double sine = std::sin(static_cast<double>(angle));
    const double cosine = std::cos(static_cast<double>(angle));
    const double old_x = x;
    x = static_cast<float>((sine * z) + (cosine * x));
    z = static_cast<float>(-(old_x * sine) + (cosine * z));
    return *this;
}

// 0x00421440
Vec4& Vec4::rotate_about_axis(const Vec4& axis, float angle)
{
    QuatRotation rotation{};
    rotation.init_from_axis_angle(axis, angle);
    Vec4 turned{};
    *this = *rotation.spin_by(&turned, *this);
    return *this;
}

// 0x0042E1F0 (the x87 sums in double; the first branch's 0.5 / s is stored as a float, the second's isn't)
QuatRotation* QuatRotation::from_matrix(const Mat4& matrix)
{
    const double trace = (get_at(matrix, 1, 1) + get_at(matrix, 0, 0)) + get_at(matrix, 2, 2);
    if (trace > 0.0) {
        const double root = std::sqrt(trace + 1.0);
        w = static_cast<float>(kHalfAngleScale * root);
        const auto factor = static_cast<float>(0.5 / root);
        x = static_cast<float>((get_at(matrix, 2, 1) - get_at(matrix, 1, 2)) * factor);
        y = static_cast<float>((get_at(matrix, 0, 2) - get_at(matrix, 2, 0)) * factor);
        z = static_cast<float>((get_at(matrix, 1, 0) - get_at(matrix, 0, 1)) * factor);
        return this;
    }
    // From the largest diagonal element.
    int largest = get_at(matrix, 1, 1) > get_at(matrix, 0, 0) ? 1 : 0;
    if (get_at(matrix, 2, 2) > get_at(matrix, largest, largest)) {
        largest = 2;
    }
    const int next = (largest + 1) % 3;
    const int after_next = (next + 1) % 3;
    const double root = std::sqrt(
        (get_at(matrix, largest, largest) - (get_at(matrix, after_next, after_next) + get_at(matrix, next, next))) +
        1.0);
    const std::array<float*, 3> components = {{&x, &y, &z}};
    *core::at(components, largest) = static_cast<float>(kHalfAngleScale * root);
    const double factor = 0.5 / root;
    *core::at(components, next) =
        static_cast<float>((get_at(matrix, next, largest) + get_at(matrix, largest, next)) * factor);
    *core::at(components, after_next) =
        static_cast<float>((get_at(matrix, after_next, largest) + get_at(matrix, largest, after_next)) * factor);
    w = static_cast<float>((get_at(matrix, after_next, next) - get_at(matrix, next, after_next)) * factor);
    return this;
}

// 0x0042E310
QuatRotation* QuatRotation::make_unit(float length)
{
    const double current =
        std::sqrt((((static_cast<double>(x) * x) + (static_cast<double>(y) * y)) + (static_cast<double>(z) * z)) +
                  (static_cast<double>(w) * w));
    if (!(current > 0.0)) {
        return this;
    }
    const double scale = length / current;
    x = static_cast<float>(scale * x);
    y = static_cast<float>(scale * y);
    z = static_cast<float>(scale * z);
    w = static_cast<float>(scale * w);
    return this;
}

} // namespace maths
