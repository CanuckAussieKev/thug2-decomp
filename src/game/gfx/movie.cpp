// Drawing a frame of a movie (the Bink player's texture) as a screen quad.

#include "game/gfx/movie.h"

#include <cstdint>
#include <cstdio>

#include "hook/game.h"
#include "hook/hook.h"

// PartyMod's replacements for two of the calls (src/partymod/gfx.c): the texture set notes the movie's size,
// the draw fits the quad to 4:3 inside the back buffer (black bars).
extern "C" void movie_setTexture(int index, void* texture, int unknown);
extern "C" void __stdcall movie_drawPrim(void* device, int type, uint32_t count, void* vertices, uint32_t stride);

namespace Nx {

namespace {

struct MovieVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
static_assert(sizeof(MovieVertex) == 0x1C, "the draw's stride");

void** Device() { return GameVariable<void**>(0x007D69E0); }
template <typename F>
F Method(int index) { return reinterpret_cast<F>((*reinterpret_cast<void***>(Device()))[index]); }
void SetTextureStageState(int stage, int type, int value)
{
    Method<long(__stdcall*)(void*, int, int, int)>(0x10C / 4)(Device(), stage, type, value);
}

} // namespace

// 0x0044FA80
void DrawMovieFrame(void* texture, float x, float y, float width, float height, float alpha)
{
    if (!texture)
        return;
    GameFunction<void(__cdecl*)(int)>(0x004DA760)(0);                 // blend mode
    GameFunction<void(__cdecl*)(int, int)>(0x004DAB60)(4, 0x10001);
    GameFunction<void(__cdecl*)(int)>(0x004D9DB0)(0x144);             // the vertex format
    Method<long(__stdcall*)(void*, int, int)>(0xE4 / 4)(Device(), 22, 1); // D3DRS_CULLMODE: none
    Method<long(__stdcall*)(void*, uint32_t)>(0x164 / 4)(Device(), 0x144); // SetFVF
    GameFunction<void(__cdecl*)(int, int)>(0x004DAA80)(0, 0);
    SetTextureStageState(0, 1, 5); // colour: modulate
    SetTextureStageState(0, 2, 2);
    SetTextureStageState(0, 3, 1);
    SetTextureStageState(0, 4, 3);
    SetTextureStageState(1, 1, 1); // stage 1 off
    GameFunction<void(__cdecl*)(int, void*, int)>(0x004DA730)(0, nullptr, 0);
    // (a leftover from testing: the name of a frame dump that's never written)
    char dumpName[256];
    std::snprintf(dumpName, sizeof(dumpName), "e:\\movietest\\movie%04d.bmp", GameVariable<int>(0x00696D9C));
    ++GameVariable<int>(0x00696D9C);
    // PartyMod: its movie_setTexture in place of the renderer's texture set (0x004DA730) at 0x0044FB8C
    movie_setTexture(0, texture, 0);

    // (x87: alpha * 255 truncated; each corner is position + size * the screen scale, rounded once)
    uint32_t color = static_cast<uint32_t>(static_cast<int>(static_cast<double>(alpha) * GameVariable<float>(0x00646B88)))
                         << 24 |
                     0x808080;
    MovieVertex vertices[4];
    vertices[0] = {x, y, 0.0f, 1.0f, color, 0.0f, 0.0f};
    vertices[1] = vertices[0];
    vertices[1].x = static_cast<float>(static_cast<double>(GameVariable<float>(0x0067D7C8)) * width + x);
    vertices[1].u = 1.0f;
    vertices[2] = vertices[0];
    vertices[2].y = static_cast<float>(static_cast<double>(GameVariable<float>(0x0067D7CC)) * height + y);
    vertices[2].v = 1.0f;
    vertices[3] = vertices[1];
    vertices[3].y = vertices[2].y;
    vertices[3].v = 1.0f;
    if (Method<long(__stdcall*)(void*)>(0xC / 4)(Device()) == 0) // TestCooperativeLevel: the device isn't lost
        // PartyMod: its movie_drawPrim in place of DrawPrimitiveUP at 0x0044FC88
        movie_drawPrim(Device(), 5 /* triangle strip */, 2, vertices, sizeof(MovieVertex));
    SetTextureStageState(0, 1, 4); // colour: modulate back
}

} // namespace Nx

REPLACE(0x0044FA80, Nx::DrawMovieFrame);
