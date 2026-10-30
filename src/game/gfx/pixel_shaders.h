#pragma once

#include <cstdint>

#include "hook/game_global.h"

namespace Lst {
class HashTable;
}

// The pixel shaders (Nx engine, PC, ps.1.1): a few fixed ones made at start-up (their handles at 0x007CA920-64),
// and one generated per multi-pass material configuration, cached by a key made of the passes' blend modes.
namespace Nx {

// 0x004DD7F0: once - the fixed shaders (with pixel shader support; else stand-in numbers 1-10 for the stand-in
// texture stages) and the shadow caster table.
void __cdecl CreatePixelShaders();
// 0x004DD2C0: the shader for a material's passes (+8 the count, +0x80 their blend modes): from the cache, or
// written out, assembled and cached.
void* __cdecl GeneratedPixelShader(const void* material);
// 0x004DDC00: the shader a material draws with, into *shader: a fixed one for one pass, else generated.
void __cdecl MaterialPixelShader(const void* material, void** shader);

GAME_GLOBAL_EXTERN(Lst::HashTable*, g_shadowCasters);

} // namespace Nx
