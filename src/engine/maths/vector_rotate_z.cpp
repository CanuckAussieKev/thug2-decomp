// maths::Vec4::rotate_about_z (vec4.h), from the disassembly of 0x004882D0 - code Ghidra never made a function of
// (between 0x00488220's end and 0x00488300).

#include "engine/maths/vec4.h"
#include "engine/maths/x87_math.h"

namespace maths {

// 0x004882D0
Vec4& Vec4::rotate_about_z(float angle)
{
    double s = x87::get_sin(angle);
    double c = x87::get_cos(angle);
    double old_x = x;
    x = static_cast<float>(c * x - s * y);
    y = static_cast<float>(old_x * s + c * y);
    return *this;
}

} // namespace Mth
