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
    const auto& rows = matrix.row;
    x = static_cast<float>((((vw * core::at(rows, 3).x) + (vz * core::at(rows, 2).x)) + (vy * core::at(rows, 1).x)) +
                           (vx * core::at(rows, 0).x));
    y = static_cast<float>((((vw * core::at(rows, 3).y) + (vz * core::at(rows, 2).y)) + (vx * core::at(rows, 0).y)) +
                           (vy * core::at(rows, 1).y));
    z = static_cast<float>((((vw * core::at(rows, 3).z) + (vz * core::at(rows, 2).z)) + (vx * core::at(rows, 0).z)) +
                           (vy * core::at(rows, 1).z));
    w = static_cast<float>((((vw * core::at(rows, 3).w) + (vz * core::at(rows, 2).w)) + (vx * core::at(rows, 0).w)) +
                           (vy * core::at(rows, 1).w));
    return *this;
}

} // namespace maths
