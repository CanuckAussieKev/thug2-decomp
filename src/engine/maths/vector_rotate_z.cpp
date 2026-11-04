// maths::Vec4::rotate_about_z (vec4.h), from the disassembly of 0x004882D0 - code Ghidra never made a function of
// (between 0x00488220's end and 0x00488300).

#include "engine/maths/vec4.h"

#include <cmath>

namespace maths {

// 0x004882D0
Vec4& Vec4::rotate_about_z(float angle)
{
    const double sine = std::sin(static_cast<double>(angle));
    const double cosine = std::cos(static_cast<double>(angle));
    const double old_x = x;
    x = static_cast<float>((cosine * x) - (sine * y));
    y = static_cast<float>((old_x * sine) + (cosine * y));
    return *this;
}

} // namespace maths
