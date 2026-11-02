// Two maths helpers the rigid body's code sits next to: Vec4::set_length (vec4.h) and QuatRotation::fetch_mat (quat_rotation.h).
// The x87 work is at 53 bits: what the original keeps on the FPU stack is a double here, what it stores a float.

#include <cmath>
#include <span>

#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/core/checked_index.h"
#include "engine/core/raw_view.h"

namespace maths {

namespace {

double get_wide(float value) { return value; }
float get_narrow(double value) { return static_cast<float>(value); }

} // namespace

// 0x0042BBF0
Vec4* Vec4::set_length(float length)
{
    const double current = std::sqrt((get_wide(x) * x + get_wide(y) * y) + get_wide(z) * z);
    if (!(current > 0.0))
        return this;
    const double scale = length / current;
    x = get_narrow(scale * x);
    y = get_narrow(scale * y);
    z = get_narrow(scale * z);
    return this;
}

// 0x0042BC40: with s = 2 / |q|^2 - x * s kept unrounded, y * s and z * s rounded; the products by x unrounded, the
// others rounded (as the original keeps them).
Mat4* QuatRotation::fetch_mat(Mat4* out) const
{
    const double length_sqr = ((get_wide(x) * x + get_wide(y) * y) + get_wide(z) * z) + get_wide(w) * w;
    const double s = length_sqr > 0.0 ? 2.0 / length_sqr : 0.0;
    const double xs = s * x;
    const float ys = get_narrow(s * y);
    const float zs = get_narrow(s * z);
    const float wx = get_narrow(xs * w);
    const float wy = get_narrow(get_wide(ys) * w);
    const float wz = get_narrow(get_wide(zs) * w);
    const double xx = xs * x;
    const double xy = get_wide(ys) * x;
    const double xz = get_wide(zs) * x;
    const float yy = get_narrow(get_wide(ys) * y);
    const float yz = get_narrow(get_wide(zs) * y);
    const double zz = get_wide(zs) * z;

    const std::span<float, 16> m = out->floats();
    core::at(m, 0) = get_narrow(1.0 - (yy + zz));
    core::at(m, 4) = get_narrow(xy + wz);
    core::at(m, 8) = get_narrow(xz - wy);
    core::at(m, 1) = get_narrow(xy - wz);
    core::at(m, 5) = get_narrow(1.0 - (zz + xx));
    core::at(m, 9) = get_narrow(get_wide(yz) + wx);
    core::at(m, 2) = get_narrow(xz + wy);
    core::at(m, 6) = get_narrow(get_wide(yz) - wx);
    core::at(m, 10) = get_narrow(1.0 - (get_wide(yy) + xx));
    return out;
}

} // namespace maths
