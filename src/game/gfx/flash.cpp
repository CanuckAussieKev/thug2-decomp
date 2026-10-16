// Screen flashes: a coloured (optionally textured) rectangle over part of the screen.

#include "game/gfx/flash.h"

#include <cstdint>

#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

struct FlashVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
static_assert(sizeof(FlashVertex) == 0x1C, "the draw's stride");

// The renderer's state calls (not rewritten yet; cdecl).
void SetTexture(int stage, void* texture, int unknown) { GameFunction<void(__cdecl*)(int, void*, int)>(0x004DA730)(stage, texture, unknown); }
void SetSamplerState(int state, int value) { GameFunction<void(__cdecl*)(int, int)>(0x004DAB60)(state, value); }

} // namespace

// 0x004B9030
void DrawFlash(const Flash* flash)
{
    // The viewport (fractions of the screen) the flash belongs to.
    const float* view = GameFunction<const float*(__cdecl*)(int)>(0x004A06D0)(flash->viewport);
    double width = GameVariable<int>(0x00786D6C);
    double height = GameVariable<int>(0x00786D70);
    // (x87: each product is rounded once, each sum to float)
    float left = static_cast<float>(width * view[1]);
    float top = static_cast<float>(height * view[2]);
    float right = static_cast<float>(width * view[3] + left);
    float bottom = static_cast<float>(height * view[4] + top);
    float z = flash->z;
    uint32_t color = static_cast<uint32_t>(flash->alpha) << 24 | static_cast<uint32_t>(flash->red) << 16 |
                     static_cast<uint32_t>(flash->green) << 8 | flash->blue;
    FlashVertex vertices[4] = {{left, top, z, 1.0f, color, 0.0f, 0.0f},
                               {right, top, z, 1.0f, color, 1.0f, 0.0f},
                               {right, bottom, z, 1.0f, color, 1.0f, 1.0f},
                               {left, bottom, z, 1.0f, color, 0.0f, 1.0f}};
    // (without a texture the original leaves u/v unset; they aren't used then)
    if (uint8_t* texture = flash->texture) {
        uint8_t* image = (texture[8] & 1) ? **reinterpret_cast<uint8_t***>(texture + 0x10)
                                          : *reinterpret_cast<uint8_t**>(texture + 0x10);
        SetTexture(0, *reinterpret_cast<void**>(image + 0x10), 0);
        SetSamplerState(4, 0x10001);
    } else {
        SetTexture(0, nullptr, 0);
    }
    GameFunction<void(__cdecl*)(int)>(0x004DA760)(5); // blend mode
    SetSamplerState(1, 0);
    SetSamplerState(2, 0);
    GameFunction<void(__cdecl*)(int, int)>(0x004DAA80)(0, 0);
    GameFunction<void(__cdecl*)(int)>(0x004D9DB0)(0x144); // the vertex format
    ++GameVariable<int>(0x00786EBC); // draw calls
    GameVariable<int>(0x00786EC0) += 2; // triangles
    // PartyMod (its reorder_flash_vertices at 0x004B91E7): the original passes the first three vertices in the
    // wrong order; rotate them.
    FlashVertex first = vertices[0];
    vertices[0] = vertices[1];
    vertices[1] = vertices[2];
    vertices[2] = first;
    void** device = GameVariable<void**>(0x00786D0C);
    using DrawPrimitiveUP = long(__stdcall*)(void*, int, unsigned int, const void*, unsigned int);
    reinterpret_cast<DrawPrimitiveUP>((*reinterpret_cast<void***>(device))[83])(device, 5 /* triangle strip */, 2,
                                                                                 vertices, sizeof(FlashVertex));
}

} // namespace Nx

REPLACE(0x004B9030, Nx::DrawFlash);
