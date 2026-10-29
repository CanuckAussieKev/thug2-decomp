// Mth::Vector::RotateAboutZ (vector.h), from the disassembly of 0x004882D0 - code Ghidra never made a function of
// (between 0x00488220's end and 0x00488300).

#include "game/mth/vector.h"
#include "game/mth/x87_math.h"
#include "hook/hook.h"

namespace Mth {

// 0x004882D0
Vector& Vector::RotateAboutZ(float angle)
{
    double s = X87::Sin(angle);
    double c = X87::Cos(angle);
    double oldX = x;
    x = static_cast<float>(c * x - s * y);
    y = static_cast<float>(oldX * s + c * y);
    return *this;
}

} // namespace Mth

REPLACE(0x004882D0, &Mth::Vector::RotateAboutZ);
