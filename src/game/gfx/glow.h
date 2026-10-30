#pragma once

#include <cstddef>
#include <cstdint>
#include "hook/game_global.h"

// Light glows (Nx engine, PC): halos at light positions (the table 0x007CA9D8) that grow while occlusion queries
// see them and fade otherwise.
namespace Nx {

struct sGlow {
    float position[3];
    uint32_t unknownC;
    float maxSize;     // +0x10
    float size;        // +0x14: now
    float querySize;   // +0x18: the quad tested
    float rate;        // +0x1C: of growing and fading
    uint32_t queries[4]; // +0x20: outstanding occlusion queries (oldest first)
    uint32_t queryCount; // +0x30
    uint32_t pixels;     // +0x34: what the last answered query saw
};
static_assert(offsetof(sGlow, queries) == 0x20 && offsetof(sGlow, pixels) == 0x34, "layout must match the original");

// 0x004DCC70: with `queries`, an occlusion query for each glow (no z writes, nothing drawn to colour); else each
// glow's size moved on by the oldest query's answer. Each works out its quad facing the camera (0x007CA9F0) -
// which nothing draws in this build.
void __cdecl UpdateGlows(bool queries);

GAME_GLOBAL_EXTERN(void*, g_vertexColourPixelShader);
} // namespace Nx
