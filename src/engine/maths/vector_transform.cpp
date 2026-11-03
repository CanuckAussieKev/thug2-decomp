// A vector times a full 4x4 matrix (maths::Vec4::multiply_by, engine/maths/vec4.h).

#include "engine/core/checked_index.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

namespace maths {

// 0x00434200 (the x87 sums in double, each stored once; the terms added as the original does: w first, then z,
// then x / y in the order below)
Vec4& Vec4::multiply_by(const Mat4& matrix)
{
    const double vx = x;
    const double vy = y;
    const double vz = z;
    const double vw = w;
    const auto& m = matrix.row;
    x = static_cast<float>((((vw * core::at(m, 3).x) + (vz * core::at(m, 2).x)) + (vy * core::at(m, 1).x)) +
                           (vx * core::at(m, 0).x));
    y = static_cast<float>((((vw * core::at(m, 3).y) + (vz * core::at(m, 2).y)) + (vx * core::at(m, 0).y)) +
                           (vy * core::at(m, 1).y));
    z = static_cast<float>((((vw * core::at(m, 3).z) + (vz * core::at(m, 2).z)) + (vx * core::at(m, 0).z)) +
                           (vy * core::at(m, 1).z));
    w = static_cast<float>((((vw * core::at(m, 3).w) + (vz * core::at(m, 2).w)) + (vx * core::at(m, 0).w)) +
                           (vy * core::at(m, 1).w));
    return *this;
}

} // namespace maths
