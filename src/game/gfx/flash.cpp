// Screen flashes: a coloured (optionally textured) rectangle over part of the screen.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/gfx/flash.h"

#include <cstdint>

#include "game/gfx/device.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/gfx/viewport_list.h"

namespace Nx {

namespace {

struct FlashVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
static_assert(sizeof(FlashVertex) == 0x1C, "the draw's stride");

// A render state by the renderer's numbering (SetState).
void SetSamplerState(int state, int value) { SetState(state, static_cast<uint32_t>(value)); }

} // namespace

// 0x004B9030
void DrawFlash(const Flash* flash)
{
    // The viewport (fractions of the screen) the flash belongs to.
    const float* view = reinterpret_cast<const float*>(GetActiveViewport(flash->viewport));
    double width = g_backBufferWidth;
    double height = g_backBufferHeight;
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
        SetTexture(0, *reinterpret_cast<void**>(image + 0x10)); // (the original also passes 0, unused)
        SetSamplerState(4, 0x10001);
    } else {
        SetTexture(0, nullptr);
    }
    SetBlendMode(5);
    SetSamplerState(1, 0);
    SetSamplerState(2, 0);
    SetPixelShader(nullptr, nullptr); // (no pixel shader)
    SetVertexFormat(0x144);
    ++g_drawCalls; // draw calls
    g_drawnTriangles += 2; // triangles
    // Originally from PartyMod Fixes (its reorder_flash_vertices at 0x004B91E7): the original passes the first three vertices in the
    // wrong order; rotate them.
    FlashVertex first = vertices[0];
    vertices[0] = vertices[1];
    vertices[1] = vertices[2];
    vertices[2] = first;
    void** device = reinterpret_cast<void**>(g_drawDevice);
    using DrawPrimitiveUP = long(__stdcall*)(void*, int, unsigned int, const void*, unsigned int);
    reinterpret_cast<DrawPrimitiveUP>((*reinterpret_cast<void***>(device))[83])(device, 5 /* triangle strip */, 2,
                                                                                 vertices, sizeof(FlashVertex));
}

} // namespace Nx
