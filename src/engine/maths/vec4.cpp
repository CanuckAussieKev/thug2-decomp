#include "engine/maths/vec4.h"

#include <cmath>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/raw_view.h"

namespace maths {

// 0x00402310
float& Vec4::operator[](int index) { return (&x)[index]; }

// 0x00402340
Vec4& Vec4::operator+=(const Vec4& other)
{
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;
    return *this;
}

// 0x00406050
Vec4& Vec4::operator-=(const Vec4& other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;
    return *this;
}

// 0x00406080
Vec4& Vec4::flatten_onto_plane(const Vec4& normal)
{
    // All products are taken before writing, so this also works when `normal` is this vector. As the original's x87
    // code: the dot product and x's part along the normal stay in double; the other parts are rounded to floats
    // first, each difference rounded once more.
    const double dot =
        (static_cast<double>(normal.x) * x + static_cast<double>(z) * normal.z) + static_cast<double>(y) * normal.y;
    const double along_x = dot * normal.x;
    auto along_y = static_cast<float>(dot * normal.y);
    auto along_z = static_cast<float>(dot * normal.z);
    auto along_w = static_cast<float>(dot * normal.w);
    x = static_cast<float>(x - along_x);
    y = static_cast<float>(static_cast<double>(y) - along_y);
    z = static_cast<float>(static_cast<double>(z) - along_z);
    w = static_cast<float>(static_cast<double>(w) - along_w);
    return *this;
}

// 0x004060F0
// (The original relies on flatten_onto_plane leaving EDX alone; with both rewritten that no longer matters.)
Vec4& Vec4::align_to_plane(const Vec4& normal)
{
    const float length = len_of();
    flatten_onto_plane(normal);
    if (len_of() == 0.0f) {
        x = -normal.z;
        y = normal.x;
        z = -normal.y;
    }
    make_unit();
    x *= length;
    y *= length;
    z *= length;
    w = 0.0f;
    return *this;
}

// 0x004061A0
Vec4& Vec4::change_heading(const Vec4& direction)
{
    const float length = std::sqrt(z * z + y * y + x * x);
    *this = {length * direction.x, length * direction.y, length * direction.z, length * direction.w};
    return *this;
}

// 0x00406200
Vec4& Vec4::project_onto(const Vec4& axis)
{
    // As the original's x87 code: the dot product stays in double, each component rounded once.
    const double dot =
        (static_cast<double>(axis.x) * x + static_cast<double>(z) * axis.z) + static_cast<double>(y) * axis.y;
    *this = {static_cast<float>(dot * axis.x), static_cast<float>(dot * axis.y), static_cast<float>(dot * axis.z),
             static_cast<float>(dot * axis.w)};
    return *this;
}

// 0x00405FA0
void Vec4::echo_out() const { debug::printf("[%f %f %f %f]\n", x, y, z, w); }

// 0x00406260
float Vec4::dominant_axis_index(int* axis) const
{
    int index = 0;
    if (std::fabs(y) <= std::fabs(x))
        index = std::fabs(x) < std::fabs(z) ? 2 : 0;
    else
        index = std::fabs(y) < std::fabs(z) ? 2 : 1;
    if (axis)
        *axis = index;
    return (&x)[index];
}

// 0x00402370
Vec4& Vec4::operator*=(float scale)
{
    x *= scale;
    y *= scale;
    z *= scale;
    w *= scale;
    return *this;
}

// 0x004023A0
float Vec4::magnitude_squared() const { return x * x + y * y + z * z; }

// 0x004023C0
float Vec4::len_of() const { return std::sqrt(magnitude_squared()); }

// 0x004023F0
Vec4& Vec4::make_unit()
{
    // As the original's x87 code: the length and its inverse stay in double precision; each component is rounded
    // once.
    const double length =
        std::sqrt((static_cast<double>(x) * x + static_cast<double>(y) * y) + static_cast<double>(z) * z);
    if (length > 0.0) {
        const double scale = 1.0 / length;
        x = static_cast<float>(x * scale);
        y = static_cast<float>(y * scale);
        z = static_cast<float>(z * scale);
    }
    return *this;
}

// 0x00405EB0
Vec4& Vec4::turn_quarter_about_y(int quarter_turns)
{
    const float old_x = x;
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (quarter_turns) {
    case 1:
        x = z;
        z = -old_x;
        break;
    case 2:
        x = -x;
        z = -z;
        break;
    case 3:
        x = -z;
        z = old_x;
        break;
    }
    return *this;
}

// 0x00405EF0
void Vec4::degree_to_radian_conv()
{
    constexpr float kScale = 0.0174532925f; // pi / 180
    x *= kScale;
    y *= kScale;
    z *= kScale;
}

// 0x00405F20
void Vec4::convert_to_degrees()
{
    constexpr float kScale = 57.2957763671875f; // 180 / pi as the exe stores it (0x00645468: 0x42652EE0, one below
                                                // the nearest float, 57.2957795f)
    x *= kScale;
    y *= kScale;
    z *= kScale;
}

// 0x00405F50
void Vec4::feet_as_inches()
{
    x *= 12.0f;
    y *= 12.0f;
    z *= 12.0f;
}

// 0x00405F80
void Vec4::negate_x_rotate_x90()
{
    const float prev_y = y;
    x = -x;
    y = -z;
    z = prev_y;
}

// 0x00402440
Vec4 operator+(const Vec4& a, const Vec4& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }

// 0x00402470
Vec4 operator-(const Vec4& a, const Vec4& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }

// 0x004024A0
Vec4 operator*(const Vec4& v, float scale) { return {v.x * scale, v.y * scale, v.z * scale, v.w * scale}; }

// 0x004024D0
Vec4 operator*(float scale, const Vec4& v) { return v * scale; }

// 0x00402500
Vec4 operator-(const Vec4& v) { return {-v.x, -v.y, -v.z, -v.w}; }

// 0x00402530
float dot_vec(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// 0x00402550 (each difference of products rounded once, as the original's x87 code does)
Vec4 cross_vec(const Vec4& a, const Vec4& b)
{
    auto term = [](float p, float q, float r, float s) {
        return static_cast<float>(static_cast<double>(p) * q - static_cast<double>(r) * s);
    };
    return {term(a.y, b.z, a.z, b.y), term(a.z, b.x, a.x, b.z), term(a.x, b.y, a.y, b.x), 0.0f};
}

// 0x00402600
Seg::Seg(const Vec4& from, const Vec4& to) : start(from), end(to) {}

// 0x004A86D0
bool Vec4::operator!=(const Vec4& other) const { return other.x != x || other.y != y || other.z != z || other.w != w; }

// 0x0040AED0 (all four components exactly; NaN is unequal)
bool Vec4::operator==(const Vec4& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }

// 0x0040AF20 (x87: the reciprocal rounded to a float for x, y, z; w is multiplied by the unrounded one)
Vec4 operator/(const Vec4& v, float divisor)
{
    const double inverse = 1.0 / static_cast<double>(divisor);
    auto rounded = static_cast<float>(inverse);
    return {rounded * v.x, rounded * v.y, rounded * v.z, static_cast<float>(inverse * v.w)};
}

// 0x00423650
Vec4& Vec4::operator/=(float divisor)
{
    const double inverse = 1.0 / static_cast<double>(divisor);
    x = static_cast<float>(inverse * x);
    y = static_cast<float>(inverse * y);
    z = static_cast<float>(inverse * z);
    w = static_cast<float>(inverse * w);
    return *this;
}

// 0x00423680
double get_distance(const Vec4& a, const Vec4& b)
{
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    const double dz = static_cast<double>(a.z) - b.z;
    return std::sqrt((dz * dz + dx * dx) + dy * dy);
}

// 0x00406560
bool is_angle_beyond(const Vec4& a, const Vec4& b, float degrees)
{
    const double length_a =
        std::sqrt((static_cast<double>(a.x) * a.x + static_cast<double>(a.y) * a.y) + static_cast<double>(a.z) * a.z);
    if (length_a < 0.5)
        return false;
    double cosine =
        ((static_cast<double>(a.x) * b.x + static_cast<double>(b.z) * a.z) + static_cast<double>(a.y) * b.y) / length_a;
    const double length_b =
        std::sqrt((static_cast<double>(b.y) * b.y + static_cast<double>(b.z) * b.z) + static_cast<double>(b.x) * b.x);
    if (length_b < 0.5)
        return false;
    cosine /= length_b;
    const double limit = std::cos(static_cast<double>(degrees) * 0.01745329238474369); // (the float at 0x00645464)
    if (!(cosine >= 0.0) && limit >= 0.0) // (more than 90 degrees apart, a limit of 90 or less)
        return true;
    return cosine < limit;
}

namespace {
// The cosine as the originals take it: rounded to a float, limited to [-1, 1] (NaN passes through).
double clamped_cosine(double dot)
{
    auto cosine = static_cast<float>(dot);
    if (cosine > 1.0f)
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    return cosine;
}
} // namespace

// 0x00406460 (x87 code: the lengths, the unit vectors and the dot product in double, the dot product rounded to a
// float; the result is returned unrounded in ST0)
double read_turn_angle(const Vec4& a, const Vec4& b)
{
    double ax = a.x, ay = a.y, az = a.z;
    double length = std::sqrt((az * az + ax * ax) + ay * ay);
    if (length > 0.0) {
        const double scale = 1.0 / length;
        ax *= scale;
        ay *= scale;
        az *= scale;
    }
    double bx = b.x, by = b.y, bz = b.z;
    length = std::sqrt((bz * bz + by * by) + bx * bx);
    if (length > 0.0) {
        const double scale = 1.0 / length;
        bx *= scale;
        by *= scale;
        bz *= scale;
    }
    return std::acos(clamped_cosine((bz * az + bx * ax) + by * ay)) * 57.2957763671875; // (the float at 0x00645468)
}

// 0x00406630
double get_turn_around_axis(const Vec4& a, const Vec4& b, const Vec4& axis)
{
    Vec4 normal = axis;
    double length = std::sqrt((static_cast<double>(normal.z) * normal.z + static_cast<double>(normal.y) * normal.y) +
                              static_cast<double>(normal.x) * normal.x);
    if (length > 0.0) {
        const double scale = 1.0 / length;
        normal.x = static_cast<float>(normal.x * scale);
        normal.y = static_cast<float>(normal.y * scale);
        normal.z = static_cast<float>(normal.z * scale);
    }
    Vec4 from = a;
    Vec4 to = b;
    from.flatten_onto_plane(normal);
    to.flatten_onto_plane(normal);
    length = std::sqrt((static_cast<double>(from.z) * from.z + static_cast<double>(from.y) * from.y) +
                       static_cast<double>(from.x) * from.x);
    if (length > 0.0) {
        const double scale = 1.0 / length;
        from.x = static_cast<float>(from.x * scale);
        from.y = static_cast<float>(from.y * scale);
        from.z = static_cast<float>(from.z * scale);
    }
    double tx = to.x, ty = to.y, tz = to.z;
    length = std::sqrt((tz * tz + ty * ty) + tx * tx);
    if (length > 0.0) {
        const double scale = 1.0 / length;
        tx *= scale;
        ty *= scale;
        tz *= scale;
    }
    const double dot = (from.z * tz + ty * from.y) + tx * from.x;
    // Which way round: (normal x from) . to.
    const double cx = static_cast<double>(from.z) * normal.y - static_cast<double>(from.y) * normal.z;
    const double cy = static_cast<double>(normal.z) * from.x - static_cast<double>(from.z) * normal.x;
    const double cz = static_cast<double>(from.y) * normal.x - static_cast<double>(normal.y) * from.x;
    auto side = static_cast<float>((cz * tz + cy * ty) + cx * tx);
    const double angle = std::acos(clamped_cosine(dot));
    return side < 0.0f ? -angle : angle;
}

// 0x0041A820
double get_distance_squared(const Vec4& a, const Vec4& b)
{
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    const double dz = static_cast<double>(a.z) - b.z;
    return dx * dx + dz * dz + dy * dy;
}

} // namespace maths

// --- replacements -------------------------------------------------------------------------------------
// Members are __thiscall (object in ECX, callee pops arguments) and return the object in EAX, matching the
// originals. Free functions returning a Vector use the hidden result pointer, as the originals do.
namespace {

using maths::Vec4;

struct VectorAdapter {
    Vec4* construct_point(float x, float y, float z)
    {
        *raw::as<Vec4>(this) = {x, y, z, 1.0f};
        return raw::as<Vec4>(this);
    }
};

struct LineAdapter {
    maths::Seg* make_new(const Vec4& start, const Vec4& end) { return new (this) maths::Seg(start, end); }
};

} // namespace
