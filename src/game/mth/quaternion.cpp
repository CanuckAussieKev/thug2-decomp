// Rotation quaternions (Mth::Quat) and the vector rotations made with them: see quaternion.h. From the disassembly of
// 0x00420140-0x0042023E, 0x00420A50-0x00420B4A and 0x00421440-0x00421484. The x87 roundings are kept: values the
// original stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "game/mth/quaternion.h"

#include <cmath>

#include "hook/hook.h"

namespace Mth {

namespace {

// The FPU's sine and cosine (FSIN / FCOS), as the originals take them.
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

} // namespace

// 0x00420AC0
Quat* Quat::InitFromAxisAngle(const Vector& axis, float angle)
{
    double ax = axis.x, ay = axis.y, az = axis.z;
    float length = static_cast<float>(std::sqrt(ax * ax + ay * ay + az * az));
    double half = static_cast<double>(angle) * 0.5;
    double inverse = length > 0.0f ? 1.0 / length : 0.0;
    float s = static_cast<float>(X87Sin(half) * inverse);
    z = s * axis.z;
    y = s * axis.y;
    x = s * axis.x;
    w = static_cast<float>(X87Cos(half));
    return this;
}

// 0x00420140
Quat& Quat::operator*=(const Quat& q)
{
    double ax = x, ay = y, az = z, aw = w;
    double bx = q.x, by = q.y, bz = q.z, bw = q.w;
    double cross0 = bz * ay - by * az;
    float cross1 = static_cast<float>(az * bx - bz * ax);
    float cross2 = static_cast<float>(by * ax - bx * ay);
    float sum0 = static_cast<float>(static_cast<double>(static_cast<float>(bx * aw)) + ax * bw);
    float sum1 = static_cast<float>(by * aw) + static_cast<float>(ay * bw);
    float sum2 = static_cast<float>(bz * aw) + static_cast<float>(az * bw);
    x = static_cast<float>(cross0 + sum0);
    y = sum1 + cross1;
    z = sum2 + cross2;
    w = static_cast<float>(bw * aw - ((bz * az + by * ay) + bx * ax));
    return *this;
}

// 0x00420B50
Vector* Quat::Rotate(Vector* out, const Vector& v) const
{
    Quat conjugate = {-x, -y, -z, w};
    Quat point = {v.x, v.y, v.z, 1.0f};
    Quat result = *this;
    result *= point;
    result *= conjugate;
    *out = {result.x, result.y, result.z, result.w};
    return out;
}

// 0x00420A50
Vector& Vector::RotateAboutX(float angle)
{
    double s = X87Sin(angle);
    double c = X87Cos(angle);
    double oldY = y;
    y = static_cast<float>(c * y - s * z);
    z = static_cast<float>(c * z + oldY * s);
    return *this;
}

// 0x00420A90
Vector& Vector::RotateAboutY(float angle)
{
    double s = X87Sin(angle);
    double c = X87Cos(angle);
    double oldX = x;
    x = static_cast<float>(s * z + c * x);
    z = static_cast<float>(-(oldX * s) + c * z);
    return *this;
}

// 0x00421440
Vector& Vector::RotateAboutAxis(const Vector& axis, float angle)
{
    Quat rotation;
    rotation.InitFromAxisAngle(axis, angle);
    Vector turned;
    *this = *rotation.Rotate(&turned, *this);
    return *this;
}

} // namespace Mth

REPLACE(0x00420AC0, &Mth::Quat::InitFromAxisAngle);
REPLACE(0x00420140, &Mth::Quat::operator*=);
REPLACE(0x00420B50, &Mth::Quat::Rotate);
REPLACE(0x00420A50, &Mth::Vector::RotateAboutX);
REPLACE(0x00420A90, &Mth::Vector::RotateAboutY);
REPLACE(0x00421440, &Mth::Vector::RotateAboutAxis);
