// Maths::Vec4::RotateAboutZ (vec4.h), from the disassembly of 0x004882D0 - code Ghidra never made a function of
// (between 0x00488220's end and 0x00488300).

#include "engine/maths/vec4.h"
#include "engine/maths/x87_math.h"

namespace Maths {

// 0x004882D0
Vec4& Vec4::RotateAboutZ(float angle)
{
    double s = X87::Sin(angle);
    double c = X87::Cos(angle);
    double oldX = x;
    x = static_cast<float>(c * x - s * y);
    y = static_cast<float>(oldX * s + c * y);
    return *this;
}

} // namespace Mth
