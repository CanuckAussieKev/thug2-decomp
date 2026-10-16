#pragma once

#include <cstdint>

namespace Nx {

// Work out the loading bar's colours from its settings: the gradient's end colours, its width in pixels, the
// border colour, and the per-pixel gradient table.
void UpdateLoadingBarColors();

// The gradient table's buffer on PC (100000 bytes, in place of the original's 0x00701688).
uint8_t* LoadingBarGradientBuffer();

} // namespace Nx
