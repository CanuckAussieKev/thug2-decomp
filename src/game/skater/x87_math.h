#pragma once

#include <cstdint>

// The x87 instructions and C runtime math the skater's and the cameras' code uses (0x00521BA0-0x00529F62), so
// results round exactly as the original's do. FSIN / FCOS / FPATAN give 64-bit results whatever the precision
// control (it only rounds + - * / and FSQRT to 53 bits): what the original computes from them in registers is
// computed here in registers too, and only rounded where the original stores. The game's own C runtime's
// _CIacos / _CIasin (0x00623630 / 0x00623560: the argument and the result in ST0) are called until the C runtime
// is linked.
namespace X87 {

// FSQRT (53 bits: the same as a double square root).
inline double Sqrt(double value)
{
    double result;
    __asm {
        fld value
        fsqrt
        fstp result
    }
    return result;
}

// sin / cos rounded to float (as the original stores them).
inline float SinF(double angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

inline float CosF(double angle)
{
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

// (a, b) turned by `angle`: a' = cos * a + sin * b, b' = cos * b - sin * a - each product and sum rounded to 53
// bits from the full-precision sine and cosine (exact as doubles).
inline void Rotate(double angle, double a, double b, double* rotatedA, double* rotatedB)
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
    *rotatedA = ra;
    *rotatedB = rb;
}

// FPATAN: the angle of (x, y) - atan(y / x) in the right quadrant - rounded to float.
inline float Atan2F(double y, double x)
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
inline double Atan2Difference(double y1, double x1, double y2, double x2)
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

// The game's C runtime's acos (_CIacos 0x00623630), rounded to float; `aboveLimit` tells whether the unrounded
// result is above `limit` (as the original compares it before rounding).
inline float AcosF(double value, float limit, bool* aboveLimit)
{
    float result;
    uint32_t function = 0x00623630;
    uint16_t status;
    __asm {
        fld value
        call function
        fst result
        fcomp limit
        fnstsw status
    }
    *aboveLimit = (status & 0x4100) == 0;
    return result;
}

// _CIacos / _CIasin (0x00623560) to 53 bits.
inline double Acos(double value)
{
    double result;
    uint32_t function = 0x00623630;
    __asm {
        fld value
        call function
        fstp result
    }
    return result;
}

inline double Asin(double value)
{
    double result;
    uint32_t function = 0x00623560;
    __asm {
        fld value
        call function
        fstp result
    }
    return result;
}

} // namespace X87
