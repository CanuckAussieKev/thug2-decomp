#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

// The few trigonometry steps the original's x87 code shares between files, in plain C++ (they were inline assembly
// until stage 7: the results now differ from the x87's in the last bits at most - src/tests/maths_check.cpp measures
// it). Values the original kept in FPU registers are doubles here; where it stored a float, the result is rounded
// to float at the same point.
namespace maths {

// pi as the original's float constants hold it (the screen elements' 0x00649FC8, the objects' copy).
inline constexpr float kPi = std::numbers::pi_v<float>;
// A full turn in radians, as the original's float constants hold it (0x0064545C).
inline constexpr float kTwoPi = 2.0F * kPi;

// NOT ORIGINAL - the C runtime's maths of a float as the 32-bit build has it: the double function, its result rounded
// once to a float (the 32-bit runtime's sinf, cosf, asinf... are exactly that - its corecrt_math.h). The 64-bit
// runtime has float routines of their own whose last bit differs (sin and cos in 0.1% of results, asin in 6%, acos
// in 2%: thug2_maths_check's corpus), so float maths calls these, not std::, and both builds round the same results.
// On 32-bit x86 they are the runtime's own functions (the same calls as std::); the doubles are std::'s on both.
namespace runtime32 {
#ifdef _M_IX86
using std::acos;
using std::asin;
using std::atan;
using std::atan2;
using std::cos;
using std::exp;
using std::log;
using std::log10;
using std::pow;
using std::sin;
using std::tan;
#else
inline float sin(float x) { return static_cast<float>(std::sin(static_cast<double>(x))); }
inline float cos(float x) { return static_cast<float>(std::cos(static_cast<double>(x))); }
inline float tan(float x) { return static_cast<float>(std::tan(static_cast<double>(x))); }
inline float asin(float x) { return static_cast<float>(std::asin(static_cast<double>(x))); }
inline float acos(float x) { return static_cast<float>(std::acos(static_cast<double>(x))); }
inline float atan(float x) { return static_cast<float>(std::atan(static_cast<double>(x))); }
inline float atan2(float y, float x)
{
    return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
}
inline float exp(float x) { return static_cast<float>(std::exp(static_cast<double>(x))); }
inline float log(float x) { return static_cast<float>(std::log(static_cast<double>(x))); }
inline float log10(float x) { return static_cast<float>(std::log10(static_cast<double>(x))); }
inline float pow(float x, float y)
{
    return static_cast<float>(std::pow(static_cast<double>(x), static_cast<double>(y)));
}
inline double sin(double x) { return std::sin(x); }
inline double cos(double x) { return std::cos(x); }
inline double tan(double x) { return std::tan(x); }
inline double asin(double x) { return std::asin(x); }
inline double acos(double x) { return std::acos(x); }
inline double atan(double x) { return std::atan(x); }
inline double atan2(double y, double x) { return std::atan2(y, x); }
inline double exp(double x) { return std::exp(x); }
inline double log(double x) { return std::log(x); }
inline double log10(double x) { return std::log10(x); }
inline double pow(double x, double y) { return std::pow(x, y); }
#endif
} // namespace runtime32

// (a, b) turned by `angle`: a' = cos * a + sin * b, b' = cos * b - sin * a.
inline void spin_by(double angle, double a, double b, double* rotated_a, double* rotated_b)
{
    const double sine = std::sin(angle);
    const double cosine = std::cos(angle);
    *rotated_a = (cosine * a) + (sine * b);
    *rotated_b = (cosine * b) - (sine * a);
}

// The value an angle is found from and the limit the angle (or its cosine) is compared with.
struct AngleLimit {
    double value;
    float limit;
};

// acos(in.value) rounded to float; `above_limit` tells whether the unrounded angle is above the limit (the original
// compares it before rounding).
inline float acos_f(AngleLimit in, bool* above_limit)
{
    const double angle = std::acos(in.value);
    *above_limit = angle > in.limit;
    return static_cast<float>(angle);
}

// 1 / cos and -1 / cos of an angle.
struct Inverses {
    double inverse;
    double negative;
};

// -asin(in.value) rounded to float, and from the cosine of the unrounded angle: whether its size is above the limit,
// and 1 / cos, -1 / cos.
inline float neg_asin_cos(AngleLimit in, bool* above, Inverses* inverses)
{
    const double angle = -std::asin(in.value);
    const double cosine = std::cos(angle);
    *above = std::fabs(cosine) > in.limit;
    inverses->inverse = 1.0 / cosine;
    inverses->negative = -1.0 / cosine;
    return static_cast<float>(angle);
}

// A double truncated towards zero to 64 bits; a NaN or a value out of range gives the x87's "integer indefinite"
// (0x8000000000000000), as the original's FISTTP did - a plain cast would be undefined there.
inline int64_t truncate_to_i64(double value)
{
    constexpr double kLimit = 9223372036854775808.0; // 2^63
    if (!(value > -kLimit && value < kLimit)) {
        return std::numeric_limits<int64_t>::min();
    }
    return static_cast<int64_t>(value);
}

} // namespace maths
