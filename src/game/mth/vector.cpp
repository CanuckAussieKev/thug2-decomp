#include "game/mth/vector.h"

#include <cmath>
#include <new>

#include "game/core/dbg.h"
#include "hook/hook.h"
#include "hook/shadow.h"

namespace Mth {

// 0x00402320
Vector& Vector::operator=(const Vector& other)
{
    x = other.x;
    y = other.y;
    z = other.z;
    w = other.w;
    return *this;
}

// 0x00402310
float& Vector::operator[](int index)
{
    return (&x)[index];
}

// 0x00402340
Vector& Vector::operator+=(const Vector& other)
{
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;
    return *this;
}

// 0x00406050
Vector& Vector::operator-=(const Vector& other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;
    return *this;
}

// 0x00406080
Vector& Vector::ProjectToPlane(const Vector& normal)
{
    // All products are taken before writing, so this also works when `normal` is this vector. As the original's x87
    // code: the dot product and x's part along the normal stay in double; the other parts are rounded to floats
    // first, each difference rounded once more.
    double dot = (static_cast<double>(normal.x) * x + static_cast<double>(z) * normal.z) + static_cast<double>(y) * normal.y;
    double alongX = dot * normal.x;
    float alongY = static_cast<float>(dot * normal.y);
    float alongZ = static_cast<float>(dot * normal.z);
    float alongW = static_cast<float>(dot * normal.w);
    x = static_cast<float>(x - alongX);
    y = static_cast<float>(static_cast<double>(y) - alongY);
    z = static_cast<float>(static_cast<double>(z) - alongZ);
    w = static_cast<float>(static_cast<double>(w) - alongW);
    return *this;
}

// 0x004060F0
// (The original relies on ProjectToPlane leaving EDX alone; with both rewritten that no longer matters.)
Vector& Vector::RotateToPlane(const Vector& normal)
{
    float length = Length();
    ProjectToPlane(normal);
    if (Length() == 0.0f) {
        x = -normal.z;
        y = normal.x;
        z = -normal.y;
    }
    Normalize();
    x *= length;
    y *= length;
    z *= length;
    w = 0.0f;
    return *this;
}

// 0x004061A0
Vector& Vector::SetDirection(const Vector& direction)
{
    float length = std::sqrt(z * z + y * y + x * x);
    *this = {length * direction.x, length * direction.y, length * direction.z, length * direction.w};
    return *this;
}

// 0x00406200
Vector& Vector::ProjectOnto(const Vector& axis)
{
    // As the original's x87 code: the dot product stays in double, each component rounded once.
    double dot = (static_cast<double>(axis.x) * x + static_cast<double>(z) * axis.z) + static_cast<double>(y) * axis.y;
    *this = {static_cast<float>(dot * axis.x), static_cast<float>(dot * axis.y), static_cast<float>(dot * axis.z),
             static_cast<float>(dot * axis.w)};
    return *this;
}

// 0x00405FA0
void Vector::Print() const
{
    Dbg::Printf("[%f %f %f %f]\n", x, y, z, w);
}

// 0x00406260
float Vector::GetMaxAxis(int* axis) const
{
    int index;
    if (std::fabs(y) <= std::fabs(x))
        index = std::fabs(x) < std::fabs(z) ? 2 : 0;
    else
        index = std::fabs(y) < std::fabs(z) ? 2 : 1;
    if (axis)
        *axis = index;
    return (&x)[index];
}

// 0x00402370
Vector& Vector::operator*=(float scale)
{
    x *= scale;
    y *= scale;
    z *= scale;
    w *= scale;
    return *this;
}

// 0x004023A0
float Vector::LengthSqr() const
{
    return x * x + y * y + z * z;
}

// 0x004023C0
float Vector::Length() const
{
    return std::sqrt(LengthSqr());
}

// 0x004023F0
Vector& Vector::Normalize()
{
    // As the original's x87 code: the length and its inverse stay in double precision; each component is rounded
    // once.
    double length = std::sqrt((static_cast<double>(x) * x + static_cast<double>(y) * y) + static_cast<double>(z) * z);
    if (length > 0.0) {
        double scale = 1.0 / length;
        x = static_cast<float>(x * scale);
        y = static_cast<float>(y * scale);
        z = static_cast<float>(z * scale);
    }
    return *this;
}

// 0x00405EB0
Vector& Vector::RotateY90(int quarter_turns)
{
    float old_x = x;
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
void Vector::DegreesToRadians()
{
    constexpr float kScale = 0.0174532925f; // pi / 180
    x *= kScale;
    y *= kScale;
    z *= kScale;
}

// 0x00405F20
void Vector::RadiansToDegrees()
{
    constexpr float kScale = 57.2957763671875f; // 180 / pi as the exe stores it (0x00645468: 0x42652EE0, one below
                                                // the nearest float, 57.2957795f)
    x *= kScale;
    y *= kScale;
    z *= kScale;
}

// 0x00405F50
void Vector::FeetToInches()
{
    x *= 12.0f;
    y *= 12.0f;
    z *= 12.0f;
}

// 0x00405F80
void Vector::NegateXRotateX90()
{
    float old_y = y;
    x = -x;
    y = -z;
    z = old_y;
}

// 0x00402440
Vector operator+(const Vector& a, const Vector& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

// 0x00402470
Vector operator-(const Vector& a, const Vector& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}

// 0x004024A0
Vector operator*(const Vector& v, float scale)
{
    return {v.x * scale, v.y * scale, v.z * scale, v.w * scale};
}

// 0x004024D0
Vector operator*(float scale, const Vector& v)
{
    return v * scale;
}

// 0x00402500
Vector operator-(const Vector& v)
{
    return {-v.x, -v.y, -v.z, -v.w};
}

// 0x00402530
float DotProduct(const Vector& a, const Vector& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// 0x00402550 (each difference of products rounded once, as the original's x87 code does)
Vector CrossProduct(const Vector& a, const Vector& b)
{
    auto term = [](float p, float q, float r, float s) {
        return static_cast<float>(static_cast<double>(p) * q - static_cast<double>(r) * s);
    };
    return {term(a.y, b.z, a.z, b.y), term(a.z, b.x, a.x, b.z), term(a.x, b.y, a.y, b.x), 0.0f};
}

// 0x00402600
Line::Line(const Vector& from, const Vector& to) : start(from), end(to) {}

// 0x004A86D0
bool Vector::operator!=(const Vector& other) const
{
    return other.x != x || other.y != y || other.z != z || other.w != w;
}

// 0x0040AED0 (all four components exactly; NaN is unequal)
bool Vector::operator==(const Vector& other) const
{
    return x == other.x && y == other.y && z == other.z && w == other.w;
}

// 0x0040AF20 (x87: the reciprocal rounded to a float for x, y, z; w is multiplied by the unrounded one)
Vector operator/(const Vector& v, float divisor)
{
    double inverse = 1.0 / static_cast<double>(divisor);
    float rounded = static_cast<float>(inverse);
    return {rounded * v.x, rounded * v.y, rounded * v.z, static_cast<float>(inverse * v.w)};
}

// 0x00423650
Vector& Vector::operator/=(float divisor)
{
    double inverse = 1.0 / static_cast<double>(divisor);
    x = static_cast<float>(inverse * x);
    y = static_cast<float>(inverse * y);
    z = static_cast<float>(inverse * z);
    w = static_cast<float>(inverse * w);
    return *this;
}

// 0x00423680
double GetDistance(const Vector& a, const Vector& b)
{
    double dx = static_cast<double>(a.x) - b.x;
    double dy = static_cast<double>(a.y) - b.y;
    double dz = static_cast<double>(a.z) - b.z;
    return std::sqrt((dz * dz + dx * dx) + dy * dy);
}

// 0x00406560
bool IsAngleBeyond(const Vector& a, const Vector& b, float degrees)
{
    double lengthA = std::sqrt((static_cast<double>(a.x) * a.x + static_cast<double>(a.y) * a.y) +
                               static_cast<double>(a.z) * a.z);
    if (lengthA < 0.5)
        return false;
    double cosine = ((static_cast<double>(a.x) * b.x + static_cast<double>(b.z) * a.z) +
                     static_cast<double>(a.y) * b.y) / lengthA;
    double lengthB = std::sqrt((static_cast<double>(b.y) * b.y + static_cast<double>(b.z) * b.z) +
                               static_cast<double>(b.x) * b.x);
    if (lengthB < 0.5)
        return false;
    cosine /= lengthB;
    double limit = std::cos(static_cast<double>(degrees) * 0.01745329238474369); // (the float at 0x00645464)
    if (!(cosine >= 0.0) && limit >= 0.0) // (more than 90 degrees apart, a limit of 90 or less)
        return true;
    return cosine < limit;
}

namespace {
// The cosine as the originals take it: rounded to a float, limited to [-1, 1] (NaN passes through).
double ClampedCosine(double dot)
{
    float cosine = static_cast<float>(dot);
    if (cosine > 1.0f)
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    return cosine;
}
} // namespace

// 0x00406460 (x87 code: the lengths, the unit vectors and the dot product in double, the dot product rounded to a
// float; the result is returned unrounded in ST0)
double GetAngle(const Vector& a, const Vector& b)
{
    double ax = a.x, ay = a.y, az = a.z;
    double length = std::sqrt((az * az + ax * ax) + ay * ay);
    if (length > 0.0) {
        double scale = 1.0 / length;
        ax *= scale;
        ay *= scale;
        az *= scale;
    }
    double bx = b.x, by = b.y, bz = b.z;
    length = std::sqrt((bz * bz + by * by) + bx * bx);
    if (length > 0.0) {
        double scale = 1.0 / length;
        bx *= scale;
        by *= scale;
        bz *= scale;
    }
    return std::acos(ClampedCosine((bz * az + bx * ax) + by * ay)) * 57.2957763671875; // (the float at 0x00645468)
}

// 0x00406630
double GetAngleAbout(const Vector& a, const Vector& b, const Vector& axis)
{
    Vector normal = axis;
    double length = std::sqrt((static_cast<double>(normal.z) * normal.z + static_cast<double>(normal.y) * normal.y) +
                              static_cast<double>(normal.x) * normal.x);
    if (length > 0.0) {
        double scale = 1.0 / length;
        normal.x = static_cast<float>(normal.x * scale);
        normal.y = static_cast<float>(normal.y * scale);
        normal.z = static_cast<float>(normal.z * scale);
    }
    Vector from = a;
    Vector to = b;
    from.ProjectToPlane(normal);
    to.ProjectToPlane(normal);
    length = std::sqrt((static_cast<double>(from.z) * from.z + static_cast<double>(from.y) * from.y) +
                       static_cast<double>(from.x) * from.x);
    if (length > 0.0) {
        double scale = 1.0 / length;
        from.x = static_cast<float>(from.x * scale);
        from.y = static_cast<float>(from.y * scale);
        from.z = static_cast<float>(from.z * scale);
    }
    double tx = to.x, ty = to.y, tz = to.z;
    length = std::sqrt((tz * tz + ty * ty) + tx * tx);
    if (length > 0.0) {
        double scale = 1.0 / length;
        tx *= scale;
        ty *= scale;
        tz *= scale;
    }
    double dot = (from.z * tz + ty * from.y) + tx * from.x;
    // Which way round: (normal x from) . to.
    double cx = static_cast<double>(from.z) * normal.y - static_cast<double>(from.y) * normal.z;
    double cy = static_cast<double>(normal.z) * from.x - static_cast<double>(from.z) * normal.x;
    double cz = static_cast<double>(from.y) * normal.x - static_cast<double>(normal.y) * from.x;
    float side = static_cast<float>((cz * tz + cy * ty) + cx * tx);
    double angle = std::acos(ClampedCosine(dot));
    return side < 0.0f ? -angle : angle;
}

} // namespace Mth

// --- replacements -------------------------------------------------------------------------------------
// Members are __thiscall (object in ECX, callee pops arguments) and return the object in EAX, matching the
// originals. Free functions returning a Vector use the hidden result pointer, as the originals do.
namespace {

using Mth::Vector;

struct VectorAdapter {
    Vector* ConstructPoint(float x, float y, float z)
    {
        *reinterpret_cast<Vector*>(this) = {x, y, z, 1.0f};
        return reinterpret_cast<Vector*>(this);
    }
};

struct LineAdapter {
    Mth::Line* Construct(const Vector& start, const Vector& end)
    {
        return new (this) Mth::Line(start, end);
    }
};

} // namespace

REPLACE(0x004022F0, &VectorAdapter::ConstructPoint);
REPLACE(0x00402310, &Vector::operator[]);
REPLACE(0x00402320, &Vector::operator=);
REPLACE(0x00402340, &Vector::operator+=);
REPLACE(0x00402370, &Vector::operator*=);
REPLACE(0x004A86D0, &Vector::operator!=);
REPLACE(0x00405EB0, &Vector::RotateY90);
REPLACE(0x00405EF0, &Vector::DegreesToRadians);
REPLACE(0x00405F20, &Vector::RadiansToDegrees);
REPLACE(0x00405F50, &Vector::FeetToInches);
REPLACE(0x00405F80, &Vector::NegateXRotateX90);
REPLACE(0x00406050, &Vector::operator-=);
REPLACE(0x00406460, Mth::GetAngle);
REPLACE(0x00406560, Mth::IsAngleBeyond);
REPLACE(0x0040AED0, &Vector::operator==);
REPLACE(0x0040AF20, static_cast<Mth::Vector (*)(const Mth::Vector&, float)>(&Mth::operator/));
REPLACE(0x00406630, Mth::GetAngleAbout);
REPLACE(0x00423650, &Vector::operator/=);
REPLACE_SHADOW(0x00423680, Mth::GetDistance);
REPLACE(0x00406080, &Vector::ProjectToPlane);
REPLACE(0x004060F0, &Vector::RotateToPlane);
REPLACE(0x004061A0, &Vector::SetDirection);
REPLACE(0x00406200, &Vector::ProjectOnto);
REPLACE(0x00406260, &Vector::GetMaxAxis);
REPLACE(0x00405FA0, &Vector::Print);
REPLACE_SHADOW(0x004023A0, &Vector::LengthSqr);
REPLACE_SHADOW(0x004023C0, &Vector::Length);
REPLACE(0x004023F0, &Vector::Normalize);
REPLACE(0x00402440, static_cast<Vector (*)(const Vector&, const Vector&)>(&Mth::operator+));
REPLACE(0x00402470, static_cast<Vector (*)(const Vector&, const Vector&)>(&Mth::operator-));
REPLACE(0x004024A0, static_cast<Vector (*)(const Vector&, float)>(&Mth::operator*));
REPLACE(0x004024D0, static_cast<Vector (*)(float, const Vector&)>(&Mth::operator*));
REPLACE(0x00402500, static_cast<Vector (*)(const Vector&)>(&Mth::operator-));
REPLACE_SHADOW(0x00402530, Mth::DotProduct);
REPLACE(0x00402550, Mth::CrossProduct);
REPLACE(0x00402600, &LineAdapter::Construct);
