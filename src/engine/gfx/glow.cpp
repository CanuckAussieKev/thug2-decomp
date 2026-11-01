// Light glows (Nx engine, PC): see glow.h.

#include "engine/gfx/device.h"
#include "engine/gfx/glow.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <new>

#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/lists/key_map.h"
#include "platform/platform_d3d.h"
#include "engine/gfx/shader_slots.h"
#include "engine/sys/startup_init.h"
#include "engine/core/raw_view.h"

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
lists::KeyMap g_glow_table{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(8) at start-up)
lists::KeyMap& glows() { return g_glow_table; }
uint8_t g_glow_queries_in_use[0x400]; // (a byte per query: DrawGlows looks through 0x400)
float g_glow_quad[16];

// The occlusion queries in use (a byte each).
uint8_t* queries_in_use() { return g_glow_queries_in_use; }
// The quad worked out for the last glow (four positions and colours; nothing draws it).
float* glow_quad() { return g_glow_quad; }

// The number of pixels a query saw: 0 when it has the answer, 1 when not yet.
int query_result(uint32_t query, uint32_t* pixels)
{
    return os_layer::get_occlusion_result(query, raw::as<DWORD>(pixels));
}

Glow* next_glow()
{
    lists::KeyMap& table = glows();
    if (!table.step_iterator() || !table.iter_item_)
        return nullptr;
    return static_cast<Glow*>(table.iter_item_->value);
}

// The glow's quad of half-size `size` facing the camera (across `u`, up `v`).
void set_quad(const Glow* glow, const float* u, const float* v, float size, uint32_t colour)
{
    float* q = glow_quad();
    auto* colours = raw::as<uint32_t>(q);
    for (int i = 0; i < 3; ++i) {
        const double across = static_cast<double>(u[i]) * size;
        const double up = static_cast<double>(v[i]) * size;
        q[i] = static_cast<float>((glow->position[i] - across) - up);
        q[4 + i] = static_cast<float>((glow->position[i] - across) + up);
        q[8 + i] = static_cast<float>((up + across) + glow->position[i]);
        q[12 + i] = static_cast<float>((across + glow->position[i]) - up);
    }
    colours[3] = colour;
    colours[7] = colour;
    colours[11] = colour;
    colours[15] = colour;
}

} // namespace

// 0x004DCC70
void update_glows(bool queries)
{
    // The camera's across and up directions from the view matrix's third column (a, b, c): u = (b*0 - c, c*0 - a*0,
    // a - b*0) and v = u x (a, b, c), each then normalised.
    const double a = g_view_matrix.row[0].z;
    const double b = g_view_matrix.row[1].z;
    const double c = g_view_matrix.row[2].z;
    float u[3], v[3];
    u[0] = static_cast<float>(b * 0.0f - c);
    u[1] = static_cast<float>(c * 0.0f - a * 0.0f);
    u[2] = static_cast<float>(a - b * 0.0f);
    v[0] = static_cast<float>(u[1] * c - u[2] * b);
    v[1] = static_cast<float>(u[2] * a - c * u[0]);
    v[2] = static_cast<float>(b * u[0] - u[1] * a);
    for (float* w : {u, v}) {
        const double length = std::sqrt((static_cast<double>(w[2]) * w[2] + static_cast<double>(w[1]) * w[1]) +
                                        static_cast<double>(w[0]) * w[0]);
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            for (int i = 0; i < 3; ++i)
                w[i] = static_cast<float>(w[i] * inverse);
        }
    }
    lists::KeyMap& table = glows();
    table.iter_bucket_ = -1;
    table.iter_item_ = nullptr;
    Glow* glow = next_glow();

    if (queries) {
        // Each glow gets an occlusion query (up to three outstanding).
        input_port()->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        choose_blend_mode(0);
        set_texture(0, nullptr);
        set_pixel_shader(nullptr, nullptr);
        input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        input_port()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        for (; glow; glow = next_glow()) {
            if (glow->query_count_ >= 3)
                continue;
            uint32_t query = 0;
            for (; query < 0x400; ++query) {
                if (!queries_in_use()[query]) {
                    queries_in_use()[query] = 1;
                    break;
                }
            }
            if (query == 0x400)
                query = 0x3FF; // (none free: the last, not marked)
            glow->queries[glow->query_count_++] = query;
            if (query > 0)
                set_quad(glow, u, v, glow->query_size_, 0xFFFFFFFF);
        }
        input_port()->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        input_port()->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        return;
    }

    // Each glow grows towards its size while its queries see it, and fades otherwise.
    choose_blend_mode(5);
    set_pixel_shader(g_pixel_shaders[kPixelShader94c], nullptr);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    for (; glow; glow = next_glow()) {
        bool seen = false;
        if (!g_radeon8500) { // (not on a Radeon 8500)
            uint32_t pixels = 0;
            const int result = query_result(glow->queries[0], &pixels);
            if (result == 0) {
                queries_in_use()[glow->queries[0]] = 0;
                glow->queries[0] = glow->queries[1];
                glow->queries[1] = glow->queries[2];
                glow->queries[2] = glow->queries[3];
                --glow->query_count_;
                glow->queries[3] = 0;
                glow->pixels = pixels;
                seen = pixels != 0;
            } else if (result == 1) {
                seen = glow->pixels != 0;
            }
        }
        if (seen)
            glow->size =
                static_cast<float>((static_cast<double>(glow->max_size_) - glow->size) * glow->rate + glow->size);
        else
            glow->size = static_cast<float>(glow->size - static_cast<double>(glow->rate) * glow->size);
        if (glow->size > 0.0f)
            set_quad(glow, u, v, glow->size, 0x40FFFFFF);
    }
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

} // namespace render

namespace render {

// 0x00640E10 (a static initialiser): the glows' table, 256 buckets.
void construct_glow_table() { new (&g_glow_table) lists::KeyMap(8); }

} // namespace render
