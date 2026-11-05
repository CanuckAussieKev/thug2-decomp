#pragma once

#include <array>

namespace render {

// 0x004C82D0: hue (degrees), saturation and value to RGB (0-1) - pc_texture.h's rgb_to_hsv the other way.
void hsv_to_rgb(const std::array<float, 3>& hsv, std::array<float, 3>* rgb);

} // namespace render
