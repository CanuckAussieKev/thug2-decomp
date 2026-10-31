#pragma once

#include <cstdint>

#include "platform/crt.h"

// The x87 instructions and C runtime math the originals use (the maths library's rotations, the skater's and the
// cameras' code...), so results round exactly as the original's do. FSIN / FCOS / FPATAN give 64-bit results whatever
// the precision control (it only rounds + - * / and FSQRT to 53 bits): what the original computes from them in
// registers is computed here in registers too, and only rounded where the original stores. The game's C runtime's
// _CIacos / _CIasin (0x00623630 / 0x00623560: the argument and the result in ST0) are ours (platform/crt.h).
namespace x87 {

// FSQRT (53 bits: the same as a double square root).
inline double get_sqrt(double value)
{
    double result;
    __asm {
        fld value
        fsqrt
        fstp result
    }
    return result;
}

// sin / cos rounded to 53 bits (for a value the original stores as a float or uses once: rounding the 64-bit result
// to 53 bits first changes a float only in a tie, practically never).
inline double get_sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

inline double get_cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

// a / b by FDIV (two NaNs: the FPU's choice of which comes out - the larger significand - not SSE's, the first).
inline double divide(double a, double b)
{
    double result;
    __asm {
        fld a
        fdiv b
        fstp result
    }
    return result;
}

// a * b by FMUL with a in ST0, rounded to float (two NaNs: the FPU's choice, not SSE's).
inline float multiply_f(float a, float b)
{
    float result;
    __asm {
        fld a
        fmul b
        fstp result
    }
    return result;
}

// sin(angle) * factor and sin(a) * sin(b), from the full-precision sines (the product rounded to 53 bits).
inline double sin_times(double angle, double factor)
{
    double result;
    __asm {
        fld angle
        fsin
        fmul factor
        fstp result
    }
    return result;
}

inline double sin_product(double a, double b)
{
    double result;
    __asm {
        fld a
        fsin
        fld b
        fsin
        fmulp st(1), st
        fstp result
    }
    return result;
}

// cos(angle) / sin(angle) from the full-precision cosine and sine.
inline double get_cot(double angle)
{
    double result;
    __asm {
        fld angle
        fld st(0)
        fcos
        fxch st(1)
        fsin
        fdivp st(1), st
        fstp result
    }
    return result;
}

// sin / cos rounded to float (as the original stores them).
inline float get_sin_f(double angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

inline float cos_f(double angle)
{
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

// Whether cos(angle) (at full precision) is above `value`.
inline bool cos_above(double angle, float value)
{
    uint16_t status;
    __asm {
        fld angle
        fcos
        fcomp value
        fnstsw status
    }
    return (status & 0x4100) == 0;
}

// (a, b) turned by `angle`: a' = cos * a + sin * b, b' = cos * b - sin * a - each product and sum rounded to 53
// bits from the full-precision sine and cosine (exact as doubles).
inline void spin_by(double angle, double a, double b, double* rotated_a, double* rotated_b)
{
    double ra, rb;
    __asm {
        fld angle
        fld st(0)
        fsin
        fxch st(1)
        fcos               // cos, sin
        fld a              // a, cos, sin
        fmul st, st(1)     // cos*a
        fld b
        fmul st, st(3)     // sin*b, cos*a, cos, sin
        faddp st(1), st    // cos*a + sin*b, cos, sin
        fstp ra            // cos, sin
        fmul b             // cos*b, sin
        fxch st(1)         // sin, cos*b
        fmul a             // sin*a, cos*b
        fsubp st(1), st    // cos*b - sin*a
        fstp rb
    }
    *rotated_a = ra;
    *rotated_b = rb;
}

// FPATAN: the angle of (x, y) - atan(y / x) in the right quadrant - rounded to float.
inline float atan_2f(double y, double x)
{
    float result;
    __asm {
        fld y
        fld x
        fpatan
        fstp result
    }
    return result;
}

// The difference of two such angles, rounded to float: atan2(y1, x1) - atan2(y2, x2).
inline double atan2_difference(double y1, double x1, double y2, double x2)
{
    double result;
    __asm {
        fld y1
        fld x1
        fpatan
        fld y2
        fld x2
        fpatan
        fsubp st(1), st
        fstp result
    }
    return result;
}

// The game's C runtime's acos (_CIacos 0x00623630), rounded to float; `above_limit` tells whether the unrounded
// result is above `limit` (as the original compares it before rounding).
inline float acos_f(double value, float limit, bool* above_limit)
{
    float result;
    void* function = crt::acos_x87();
    uint16_t status;
    __asm {
        fld value
        call function
        fst result
        fcomp limit
        fnstsw status
    }
    *above_limit = (status & 0x4100) == 0;
    return result;
}

// -asin(value) (the game's _CIasin, 0x00623560) rounded to float, and from its full-precision cosine: whether that
// cosine's size is above `limit`, and 1 / cos, -1 / cos (to 53 bits).
inline float neg_asin_cos(double value, float limit, bool* above, double* inverse, double* neg_inverse)
{
    float angle;
    double inv, neg_inv;
    uint16_t status;
    void* function = crt::asin_x87();
    const float one = 1.0f, minus_one = -1.0f;
    __asm {
        fld value
        call function
        fchs
        fst angle
        fcos               // cos
        fld st(0)
        fabs
        fcomp limit
        fnstsw status
        fld one
        fdiv st, st(1)
        fstp inv
        fld minus_one
        fdiv st, st(1)
        fstp neg_inv
        fstp st(0)
    }
    *above = (status & 0x4100) == 0;
    *inverse = inv;
    *neg_inverse = neg_inv;
    return angle;
}

// _CIacos / _CIasin (0x00623560) to 53 bits.
inline double get_acos(double value)
{
    double result;
    void* function = crt::acos_x87();
    __asm {
        fld value
        call function
        fstp result
    }
    return result;
}

inline double get_asin(double value)
{
    double result;
    void* function = crt::asin_x87();
    __asm {
        fld value
        call function
        fstp result
    }
    return result;
}

} // namespace x87
