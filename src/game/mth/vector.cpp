#include "game/mth/vector.h"

#include <cmath>
#include <new>

#include "hook/hook.h"

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
    float length = Length();
    if (length > 0.0f) {
        float scale = 1.0f / length;
        x *= scale;
        y *= scale;
        z *= scale;
    }
    return *this;
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
REPLACE(0x00406050, &Vector::operator-=);
REPLACE(0x00406080, &Vector::ProjectToPlane);
REPLACE(0x004023A0, &Vector::LengthSqr);
REPLACE(0x004023C0, &Vector::Length);
REPLACE(0x004023F0, &Vector::Normalize);
REPLACE(0x00402440, static_cast<Vector (*)(const Vector&, const Vector&)>(&Mth::operator+));
REPLACE(0x00402470, static_cast<Vector (*)(const Vector&, const Vector&)>(&Mth::operator-));
REPLACE(0x004024A0, static_cast<Vector (*)(const Vector&, float)>(&Mth::operator*));
REPLACE(0x004024D0, static_cast<Vector (*)(float, const Vector&)>(&Mth::operator*));
REPLACE(0x00402500, static_cast<Vector (*)(const Vector&)>(&Mth::operator-));
REPLACE(0x00402530, Mth::DotProduct);
REPLACE(0x00402550, Mth::CrossProduct);
REPLACE(0x00402600, &LineAdapter::Construct);
