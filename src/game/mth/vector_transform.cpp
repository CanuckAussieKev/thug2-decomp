// A vector times a full 4x4 matrix (Mth::Vector::MultiplyBy, game/mth/vector.h).

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "hook/hook.h"

namespace Mth {

// 0x00434200 (the x87 sums in double, each stored once; the terms added as the original does: w first, then z,
// then x / y in the order below)
Vector& Vector::MultiplyBy(const Matrix& matrix)
{
    const double vx = x, vy = y, vz = z, vw = w;
    const Vector* m = matrix.row;
    x = static_cast<float>(((vw * m[3].x + vz * m[2].x) + vy * m[1].x) + vx * m[0].x);
    y = static_cast<float>(((vw * m[3].y + vz * m[2].y) + vx * m[0].y) + vy * m[1].y);
    z = static_cast<float>(((vw * m[3].z + vz * m[2].z) + vx * m[0].z) + vy * m[1].z);
    w = static_cast<float>(((vw * m[3].w + vz * m[2].w) + vx * m[0].w) + vy * m[1].w);
    return *this;
}

} // namespace Mth

REPLACE(0x00434200, &Mth::Vector::MultiplyBy);
