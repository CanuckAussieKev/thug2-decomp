// A colour from hue, saturation and value: see hsv_colour.h.

#include "engine/gfx/hsv_colour.h"

#include <array>
#include <cstdint>

#include "base/core/checked_index.h"

namespace render {

// 0x004C82D0
void hsv_to_rgb(const std::array<float, 3>& hsv, std::array<float, 3>* rgb)
{
    float* red_out = &core::at(*rgb, 0);
    float* green_out = &core::at(*rgb, 1);
    float* blue_out = &core::at(*rgb, 2);
    const float hue = core::at(hsv, 0);
    const float saturation = core::at(hsv, 1);
    const float v = core::at(hsv, 2);
    if (saturation == 0.0F) {
        *blue_out = v;
        *green_out = v;
        *red_out = v;
        return;
    }
    const double sector = static_cast<double>(hue) * (1.0F / 60.0F);
    const int sector_index = static_cast<int>(sector);
    const double fraction = sector - sector_index;
    auto low = static_cast<float>((1.0F - static_cast<double>(saturation)) * v);
    auto falling = static_cast<float>((1.0F - (saturation * fraction)) * v);
    auto rising = static_cast<float>((1.0F - ((1.0F - fraction) * saturation)) * v);
    switch (static_cast<uint32_t>(sector_index)) {
    case 0:
        *red_out = v, *green_out = rising, *blue_out = low;
        break;
    case 1:
        *red_out = falling, *green_out = v, *blue_out = low;
        break;
    case 2:
        *red_out = low, *green_out = v, *blue_out = rising;
        break;
    case 3:
        *red_out = low, *green_out = falling, *blue_out = v;
        break;
    case 4:
        *red_out = rising, *green_out = low, *blue_out = v;
        break;
    default:
        *red_out = v, *green_out = low, *blue_out = falling;
        break;
    }
}

} // namespace render
