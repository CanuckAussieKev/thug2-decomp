// Two maths helpers the rigid body's code sits next to: Vector::SetLength (vector.h) and Quat::GetMatrix (quat.h).
// The x87 work is at 53 bits: what the original keeps on the FPU stack is a double here, what it stores a float.

#include <cmath>

#include "game/mth/matrix.h"
#include "game/mth/quat.h"
#include "game/mth/vector.h"

namespace Mth {

namespace {

double Wide(float value) { return value; }
float Narrow(double value) { return static_cast<float>(value); }

} // namespace

// 0x0042BBF0
Vector* Vector::SetLength(float length)
{
    double current = std::sqrt((Wide(x) * x + Wide(y) * y) + Wide(z) * z);
    if (!(current > 0.0))
        return this;
    double scale = length / current;
    x = Narrow(scale * x);
    y = Narrow(scale * y);
    z = Narrow(scale * z);
    return this;
}

// 0x0042BC40: with s = 2 / |q|^2 - x * s kept unrounded, y * s and z * s rounded; the products by x unrounded, the
// others rounded (as the original keeps them).
Matrix* Quat::GetMatrix(Matrix* out) const
{
    double lengthSqr = ((Wide(x) * x + Wide(y) * y) + Wide(z) * z) + Wide(w) * w;
    double s = lengthSqr > 0.0 ? 2.0 / lengthSqr : 0.0;
    double xs = s * x;
    float ys = Narrow(s * y);
    float zs = Narrow(s * z);
    float wx = Narrow(xs * w);
    float wy = Narrow(Wide(ys) * w);
    float wz = Narrow(Wide(zs) * w);
    double xx = xs * x;
    double xy = Wide(ys) * x;
    double xz = Wide(zs) * x;
    float yy = Narrow(Wide(ys) * y);
    float yz = Narrow(Wide(zs) * y);
    double zz = Wide(zs) * z;

    float* m = reinterpret_cast<float*>(out);
    m[0] = Narrow(1.0 - (yy + zz));
    m[4] = Narrow(xy + wz);
    m[8] = Narrow(xz - wy);
    m[1] = Narrow(xy - wz);
    m[5] = Narrow(1.0 - (zz + xx));
    m[9] = Narrow(Wide(yz) + wx);
    m[2] = Narrow(xz + wy);
    m[6] = Narrow(Wide(yz) - wx);
    m[10] = Narrow(1.0 - (Wide(yy) + xx));
    return out;
}

} // namespace Mth
