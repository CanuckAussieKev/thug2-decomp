// Quaternions (game/mth/quat.h).

#include "game/mth/quat.h"

#include <cmath>

#include "game/mth/matrix.h"
#include "hook/hook.h"

namespace Mth {

namespace {

// The matrix's element at row `r`, column `c`.
double At(const Matrix& m, int r, int c) { return reinterpret_cast<const float*>(&m.row[r])[c]; }

} // namespace

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
