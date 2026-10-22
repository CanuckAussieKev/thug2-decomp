// Quaternions (game/mth/quat.h) and the vector rotations made with them. From the disassembly of 0x00420140-0x0042023E,
// 0x00420A50-0x00420B4A, 0x00421440-0x00421484, 0x0042E1F0 and 0x0042E310. The x87 roundings are kept: values the
// original stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "game/mth/quat.h"

#include <cmath>

#include "game/mth/matrix.h"
#include "hook/hook.h"

namespace Mth {

namespace {

// The matrix's element at row `r`, column `c`.
double At(const Matrix& m, int r, int c) { return reinterpret_cast<const float*>(&m.row[r])[c]; }

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

// 0x0042E1F0 (the x87 sums in double; the first branch's 0.5 / s is stored as a float, the second's isn't)
Quat* Quat::FromMatrix(const Matrix& m)
{
    double trace = (At(m, 1, 1) + At(m, 0, 0)) + At(m, 2, 2);
    if (trace > 0.0) {
        double s = std::sqrt(trace + 1.0);
        w = static_cast<float>(0.5 * s);
        const float f = static_cast<float>(0.5 / s);
        x = static_cast<float>((At(m, 2, 1) - At(m, 1, 2)) * f);
        y = static_cast<float>((At(m, 0, 2) - At(m, 2, 0)) * f);
        z = static_cast<float>((At(m, 1, 0) - At(m, 0, 1)) * f);
        return this;
    }
    // From the largest diagonal element.
    int i = At(m, 1, 1) > At(m, 0, 0) ? 1 : 0;
    if (At(m, 2, 2) > At(m, i, i))
        i = 2;
    const int j = (i + 1) % 3, k = (j + 1) % 3;
    double s = std::sqrt((At(m, i, i) - (At(m, k, k) + At(m, j, j))) + 1.0);
    float* q = &x;
    q[i] = static_cast<float>(0.5 * s);
    const double f = 0.5 / s;
    q[j] = static_cast<float>((At(m, j, i) + At(m, i, j)) * f);
    q[k] = static_cast<float>((At(m, k, i) + At(m, i, k)) * f);
    w = static_cast<float>((At(m, k, j) - At(m, j, k)) * f);
    return this;
}

// 0x0042E310
Quat* Quat::Normalize(float length)
{
    double current = std::sqrt(((static_cast<double>(x) * x + static_cast<double>(y) * y) + static_cast<double>(z) * z) +
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

} // namespace Mth

REPLACE(0x0042E1F0, &Mth::Quat::FromMatrix);
REPLACE(0x0042E310, &Mth::Quat::Normalize);
REPLACE(0x00420AC0, &Mth::Quat::InitFromAxisAngle);
REPLACE(0x00420140, &Mth::Quat::operator*=);
REPLACE(0x00420B50, &Mth::Quat::Rotate);
REPLACE(0x00420A50, &Mth::Vector::RotateAboutX);
REPLACE(0x00420A90, &Mth::Vector::RotateAboutY);
REPLACE(0x00421440, &Mth::Vector::RotateAboutAxis);
