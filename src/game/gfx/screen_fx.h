#pragma once

#include <cstdint>

// Full-screen effects (Nx engine, PC): the frame is drawn into one of the screen-copy render targets
// (0x00786D1C, the current one 0x00786D68) instead of the back buffer, then downsampled, blurred (the motion blur,
// render.h's DrawScreenBlur) and bloomed back.
namespace Nx {

// 0x004DE5E0: draw `source` over the whole of `target` (made the render target) - textured, offset by du / dv
// texels plus half a texel - or, without a source, a flat quad - with `blend`, the colour, and with pixel shader
// 0x007CA950 if `shader`. Fog is off while it draws.
void __cdecl DrawScreenQuad(uint32_t blend, void* target, void* source, float du, float dv, uint32_t colour,
                            bool shader);
// 0x004DEA80: the frame's render target - the current screen copy when blurring (0x00787328 over 2) or with the
// bloom (0x00705720, pixel shaders) - cleared to the fog colour (with distance clipping and fog) or the clear colour.
// Sets 0x007CC244 when it is used.
void __cdecl BeginScreenEffects();
// 0x004DE980: with the screen copy in use, a quarter-size copy of it (for the blur), and the copy the target again.
void __cdecl CopyScreenQuarter();
// 0x004DEB70: the bloom: the bright parts of the quarter copy (above the cut-off colour 0x0067E98C), blurred four
// taps each way at a quarter and a sixteenth size (offsets 0x0067EF98, weights 0x0067EF88), and - if 0x0067EF68 -
// added back over the frame.
void __cdecl Bloom();

} // namespace Nx
