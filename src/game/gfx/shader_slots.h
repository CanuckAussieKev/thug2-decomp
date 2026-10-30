#pragma once

#include <cstdint>

#include "hook/game_global.h"

// The pixel shader handles the pixel shader maker fills in (the original's table of 18 words at 0x007CA920; a slot is
// named by its offset from it), and the vertex-format ids of the mesh loader (6 words at 0x00775B64).

namespace Nx {

enum PixelShaderSlot : int {
    kPixelShader920,
    kPixelShader924, // (grass)
    kPixelShader928,
    kPixelShader92C,
    kPixelShader930,
    kPixelShader934,
    kPixelShader938,
    kPixelShader93C,
    kPixelShader940, // constant alpha x texture alpha
    kPixelShader944, // vertex alpha x texture alpha
    kPixelShader948, // the bloom cut-off
    kPixelShader94C, // vertex colour alone
    kPixelShader950, // texture x vertex colour x2, texture alpha
    kPixelShader954, // the factor alone
    kPixelShader958,
    kPixelShader95C, // vertex colour x2 x the factor, vertex alpha
    kPixelShader960, // texture x constant alpha
    kPixelShader964, // the default: texture x vertex colour x4
    kPixelShaderCount,
};

GAME_GLOBAL_EXTERN(void*[kPixelShaderCount], g_pixelShaders);

// The vertex-format ids, in address order (0x00775B64 + 4 * index; a mesh's type t is index 6 - t).
GAME_GLOBAL_EXTERN(uint32_t[6], g_vertexShaders);

} // namespace Nx
