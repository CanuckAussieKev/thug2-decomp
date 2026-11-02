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

// (a, b) turned by `angle`: a' = cos * a + sin * b, b' = cos * b - sin * a.
inline void spin_by(double angle, double a, double b, double* rotated_a, double* rotated_b)
{
    const double sine = std::sin(angle);
    const double cosine = std::cos(angle);
    *rotated_a = cosine * a + sine * b;
    *rotated_b = cosine * b - sine * a;
}

// acos(value) rounded to float; `above_limit` tells whether the unrounded angle is above `limit` (the original
// compares it before rounding).
inline float acos_f(double value, float limit, bool* above_limit)
{
    const double angle = std::acos(value);
    *above_limit = angle > limit;
    return static_cast<float>(angle);
}

// -asin(value) rounded to float, and from the cosine of the unrounded angle: whether its size is above `limit`, and
// 1 / cos, -1 / cos.
inline float neg_asin_cos(double value, float limit, bool* above, double* inverse, double* neg_inverse)
{
    const double angle = -std::asin(value);
    const double cosine = std::cos(angle);
    *above = std::fabs(cosine) > limit;
    *inverse = 1.0 / cosine;
    *neg_inverse = -1.0 / cosine;
    return static_cast<float>(angle);
}

// A double truncated towards zero to 64 bits; a NaN or a value out of range gives the x87's "integer indefinite"
// (0x8000000000000000), as the original's FISTTP did - a plain cast would be undefined there.
inline int64_t truncate_to_i64(double value)
{
    constexpr double kLimit = 9223372036854775808.0; // 2^63
    if (!(value > -kLimit && value < kLimit))
        return std::numeric_limits<int64_t>::min();
    return static_cast<int64_t>(value);
}

} // namespace maths
