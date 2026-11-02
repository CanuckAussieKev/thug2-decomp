// Screen flashes: a coloured (optionally textured) rectangle over part of the screen.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "engine/gfx/flash.h"

#include <cstdint>
#include <array>

#include "engine/gfx/device.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/viewport_list.h"
#include "engine/gfx/viewport.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"

namespace render {

namespace {

struct FlashVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
static_assert(sizeof(FlashVertex) == 0x1C, "the draw's stride");

// A render state by the renderer's numbering (change_phase).
void set_sampler_state(int state, int value) { change_phase(state, static_cast<uint32_t>(value)); }

} // namespace

// 0x004B9030
void draw_flash(const Flash* flash)
{
    // The viewport (fractions of the screen) the flash belongs to.
    const ViewRegion* view = fetch_current_view(flash->viewport);
    const double width = g_back_buffer_width;
    const double height = g_back_buffer_height;
    // (x87: each product is rounded once, each sum to float)
    auto left = static_cast<float>(width * view->pos_x_);
    auto top = static_cast<float>(height * view->y_);
    auto right = static_cast<float>(width * view->span_ + left);
    auto bottom = static_cast<float>(height * view->level_y_ + top);
    float z = flash->z;
    uint32_t color = static_cast<uint32_t>(flash->alpha) << 24 | static_cast<uint32_t>(flash->red) << 16 |
                     static_cast<uint32_t>(flash->green) << 8 | flash->blue;
    std::array<FlashVertex, 4> vertices = {{{left, top, z, 1.0f, color, 0.0f, 0.0f},
                               {right, top, z, 1.0f, color, 1.0f, 0.0f},
                               {right, bottom, z, 1.0f, color, 1.0f, 1.0f},
                               {left, bottom, z, 1.0f, color, 0.0f, 1.0f}}};
    // (without a texture the original leaves u/v unset; they aren't used then)
    if (const BitmapTexture* texture = flash->texture) {
        set_texture(0, image_of(texture)->texture); // (the original also passes 0, unused)
        set_sampler_state(4, 0x10001);
    } else {
        set_texture(0, nullptr);
    }
    choose_blend_mode(5);
    set_sampler_state(1, 0);
    set_sampler_state(2, 0);
    set_pixel_shader(nullptr, nullptr); // (no pixel shader)
    set_vertex_format(0x144);
    ++g_draw_calls;         // draw calls
    g_drawn_triangles += 2; // triangles
    // Originally from PartyMod Fixes (its reorder_flash_vertices at 0x004B91E7): the original passes the first three vertices in the
    // wrong order; rotate them.
    const FlashVertex first = core::at(vertices, 0);
    core::at(vertices, 0) = core::at(vertices, 1);
    core::at(vertices, 1) = core::at(vertices, 2);
    core::at(vertices, 2) = first;
    g_draw_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices.data(), sizeof(FlashVertex));
}

} // namespace render
