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
    // All products are taken before writing, so this also works when `normal` is this vector.
    float dot = DotProduct(*this, normal);
    Vector along = normal * dot;
    return *this -= along;
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
    float dot = y * axis.y + z * axis.z + axis.x * x;
    *this = {dot * axis.x, dot * axis.y, dot * axis.z, dot * axis.w};
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
    constexpr float kScale = 57.2957795f; // 180 / pi
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

// 0x00402550
Vector CrossProduct(const Vector& a, const Vector& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, 0.0f};
}

// 0x00402600
Line::Line(const Vector& from, const Vector& to) : start(from), end(to) {}

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
REPLACE(0x00405EB0, &Vector::RotateY90);
REPLACE(0x00405EF0, &Vector::DegreesToRadians);
REPLACE(0x00405F20, &Vector::RadiansToDegrees);
REPLACE(0x00405F50, &Vector::FeetToInches);
REPLACE(0x00405F80, &Vector::NegateXRotateX90);
REPLACE(0x00406050, &Vector::operator-=);
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
