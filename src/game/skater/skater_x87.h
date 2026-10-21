#pragma once

// The x87's own FSIN / FCOS / FPTAN / FSQRT, which the skater's original code uses (the C runtime's sin and cos round
// differently). The game runs the x87 at 53 bits, so a double holds the result exactly; callers round to float where
// the original stores one.
namespace Skater {

inline double X87Cos(double angle)
{
    double result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}

inline double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

inline double X87Tan(double angle)
{
    double result;
    __asm {
        fld angle
        fptan
        fstp st(0)
        fstp result
    }
    return result;
}

} // namespace Skater
