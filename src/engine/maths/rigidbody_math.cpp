// Two maths helpers the rigid body's code sits next to: Vec4::set_length (vec4.h) and QuatRotation::fetch_mat (quat_rotation.h).
// The x87 work is at 53 bits: what the original keeps on the FPU stack is a double here, what it stores a float.

#include <cmath>

#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"

namespace maths {

namespace {

double get_wide(float value) { return value; }
float get_narrow(double value) { return static_cast<float>(value); }

} // namespace

// 0x0042BBF0
Vec4* Vec4::set_length(float length)
{
    double current = std::sqrt((get_wide(x) * x + get_wide(y) * y) + get_wide(z) * z);
    if (!(current > 0.0))
        return this;
    double scale = length / current;
    x = get_narrow(scale * x);
    y = get_narrow(scale * y);
    z = get_narrow(scale * z);
    return this;
}

// 0x0042BC40: with s = 2 / |q|^2 - x * s kept unrounded, y * s and z * s rounded; the products by x unrounded, the
// others rounded (as the original keeps them).
Mat4* QuatRotation::fetch_mat(Mat4* out) const
{
    double length_sqr = ((get_wide(x) * x + get_wide(y) * y) + get_wide(z) * z) + get_wide(w) * w;
    double s = length_sqr > 0.0 ? 2.0 / length_sqr : 0.0;
    double xs = s * x;
    float ys = get_narrow(s * y);
    float zs = get_narrow(s * z);
    float wx = get_narrow(xs * w);
    float wy = get_narrow(get_wide(ys) * w);
    float wz = get_narrow(get_wide(zs) * w);
    double xx = xs * x;
    double xy = get_wide(ys) * x;
    double xz = get_wide(zs) * x;
    float yy = get_narrow(get_wide(ys) * y);
    float yz = get_narrow(get_wide(zs) * y);
    double zz = get_wide(zs) * z;

    auto* m = reinterpret_cast<float*>(out);
    m[0] = get_narrow(1.0 - (yy + zz));
    m[4] = get_narrow(xy + wz);
    m[8] = get_narrow(xz - wy);
    m[1] = get_narrow(xy - wz);
    m[5] = get_narrow(1.0 - (zz + xx));
    m[9] = get_narrow(get_wide(yz) + wx);
    m[2] = get_narrow(xz + wy);
    m[6] = get_narrow(get_wide(yz) - wx);
    m[10] = get_narrow(1.0 - (get_wide(yy) + xx));
    return out;
}

} // namespace maths
