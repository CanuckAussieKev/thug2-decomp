// The particle systems' vector helpers: see particle_maths.h. The maths follows the original's x87 code (53-bit
// precision: double where it keeps values in registers, float where it stores them).

#include "engine/gfx/particle_maths.h"
#include "base/maths/vec4.h"

#include <bit>
#include <cstdint>

namespace render {

constexpr uint32_t kExponentMask = 0x7F800000U; // a float with all exponent bits set is an infinity or a NaN
constexpr uint32_t kMantissaMask = 0x007FFFFFU;
constexpr uint32_t kQuietNanBit = 0x00400000U;  // the x87 turns a signalling NaN into a quiet one
constexpr double kNegativeHalf = 0.5;           // the vertex of the parabola: -b / 2a

namespace {

float get_store(double value) { return static_cast<float>(value); }

// A float passed through the x87 (FLD / FSTP): a signalling NaN comes out quiet.
float through_x87(float value)
{
    auto bits = std::bit_cast<uint32_t>(value);
    if ((bits & kExponentMask) == kExponentMask && ((bits & kMantissaMask) != 0U)) {
        bits |= kQuietNanBit;
    }
    return std::bit_cast<float>(bits);
}

// A component's value at the quadratic's turning point (t^2 coefficient `square`, t coefficient `linear`) - at 0
// unless `square` is positive.
double get_lowest(float square, float linear, float start)
{
    double root = 0.0;
    if (square > 0.0F) {
        root = static_cast<double>(linear) / square * -kNegativeHalf;
    }
    return (((static_cast<double>(square) * root) + linear) * root) + start;
}

} // namespace

// 0x00494F10
maths::Vec4* quadratic_lowest(maths::Vec4* out, const QuadraticPoints* points)
{
    const maths::Vec4& point_a = points->start;
    const maths::Vec4& point_b = points->p1;
    const maths::Vec4& point_c = points->p2;
    const double t1 = points->t1;
    const double t2 = points->t2;
    // The end's offset scaled by t1, the mid's by t2 (z through a float each time, x through one, y kept).
    const double cx = static_cast<double>(point_c.x) - point_a.x;
    const double cy = static_cast<double>(point_c.y) - point_a.y;
    const float cz = get_store(static_cast<double>(point_c.z) - point_a.z);
    const float cx_t = get_store(cx * t1);
    const double cy_t = cy * t1;
    const double cz_t = static_cast<double>(cz) * t1;
    const double bx = static_cast<double>(point_b.x) - point_a.x;
    const double by = static_cast<double>(point_b.y) - point_a.y;
    const float bz = get_store(static_cast<double>(point_b.z) - point_a.z);
    const float bx_t = get_store(bx * t2);
    const double by_t = by * t2;
    const float bz_t = get_store(static_cast<double>(bz) * t2);
    const float scale = get_store(1.0 / ((t2 - t1) * t1 * t2));
    // The t^1 coefficients: the offsets scaled again (t1 / t2) and differenced.
    const double cx_tt = static_cast<double>(cx_t) * t1;
    const double cy_tt = cy_t * t1;
    const float cz_tt = get_store(cz_t * t1);
    const double bx_tt = static_cast<double>(bx_t) * t2;
    const float by_tt = get_store(by_t * t2);
    const float bz_tt = get_store(static_cast<double>(bz_t) * t2);
    const float dx = get_store(bx_tt - cx_tt);
    const double dy = static_cast<double>(by_tt) - cy_tt;
    const double dz = static_cast<double>(bz_tt) - cz_tt;
    const float linear_x = get_store(static_cast<double>(dx) * scale);
    const float linear_y = get_store(dy * scale);
    const float linear_z = get_store(dz * scale);
    // The t^2 coefficients: the once-scaled offsets differenced.
    const float ex = get_store(static_cast<double>(cx_t) - bx_t);
    const float ey = get_store(cy_t - by_t);
    const double ez = cz_t - bz_t;
    const float square_x = get_store(static_cast<double>(ex) * scale);
    const float square_y = get_store(static_cast<double>(ey) * scale);
    const float square_z = get_store(ez * scale);
    out->w = 0.0F;
    const double x = get_lowest(square_x, linear_x, point_a.x);
    const double y = get_lowest(square_y, linear_y, point_a.y);
    const double z = get_lowest(square_z, linear_z, point_a.z);
    out->x = get_store(x);
    out->y = get_store(y);
    out->z = get_store(z);
    return out;
}

// 0x00495130
maths::Vec4* min_vector(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b)
{
    maths::Vec4 result = *a;
    if (b->x < a->x) {
        result.x = b->x;
    }
    if (b->y < a->y) {
        result.y = b->y;
    }
    if (b->z < a->z) {
        result.z = b->z;
    }
    result.w = through_x87(b->w < a->w ? b->w : a->w);
    *out = result;
    return out;
}

// 0x004951B0
maths::Vec4* max_vector(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b)
{
    maths::Vec4 result = *a;
    if (b->x > a->x) {
        result.x = b->x;
    }
    if (b->y > a->y) {
        result.y = b->y;
    }
    if (b->z > a->z) {
        result.z = b->z;
    }
    result.w = through_x87(b->w > a->w ? b->w : a->w);
    *out = result;
    return out;
}

} // namespace render
