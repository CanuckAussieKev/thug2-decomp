// Light glows (Nx engine, PC): see glow.h.

#include "engine/gfx/glow.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <array>

#include "engine/gfx/render_state.h"
#include "engine/lists/key_map.h"
#include "platform/platform_d3d.h"
#include "engine/gfx/shader_slots.h"
#include "engine/sys/startup_init.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"

namespace render {

namespace {

// NOT ORIGINAL grouping - glow.cpp's state (the original's globals). Made on first use, never destroyed

// (glow_state()).
struct GlowState {
    lists::KeyMap glow_table_{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(8) at start-up)
    std::array<uint8_t, 0x400> glow_queries_in_use_{}; // (a byte per query: DrawGlows looks through 0x400)
};

GlowState& glow_state()
{
    static core::NoDestroy<GlowState> state;
    return state.get();
}

lists::KeyMap& glows() { return glow_state().glow_table_; }
// A glow's quad: four corners, each x, y, z and a colour.
struct GlowVertex {
    float x, y, z;
    uint32_t colour;
};

// The occlusion queries in use (a byte each).
std::array<uint8_t, 0x400>& queries_in_use() { return glow_state().glow_queries_in_use_; }
// The quad worked out for the last glow (four positions and colours; nothing draws it).
std::array<GlowVertex, 4>& glow_quad()
{
    static core::NoDestroy<std::array<GlowVertex, 4>> glow_quad;
    return glow_quad.get();
}

// The number of pixels a query saw: 0 when it has the answer, 1 when not yet.
int query_result(uint32_t query, uint32_t* pixels)
{
    DWORD seen = *pixels;
    const HRESULT result = os_layer::get_occlusion_result(query, &seen);
    *pixels = seen;
    return result;
}

Glow* next_glow()
{
    lists::KeyMap& table = glows();
    if (!table.step_iterator() || !table.iter_item())
        return nullptr;
    return static_cast<Glow*>(table.iter_item()->value);
}

// The glow's quad of half-size `size` facing the camera (across `u`, up `v`).
void set_quad(const Glow* glow, const std::array<float, 3>& u, const std::array<float, 3>& v, float size, uint32_t colour)
{
    std::array<GlowVertex, 4>& q = glow_quad();
    auto set = [](GlowVertex& corner, int axis, double value) {
        float& to = axis == 0 ? corner.x : axis == 1 ? corner.y : corner.z;
        to = static_cast<float>(value);
    };
    for (int i = 0; i < 3; ++i) {
        const double across = static_cast<double>(core::at(u, i)) * size;
        const double up = static_cast<double>(core::at(v, i)) * size;
        set(core::at(q, 0), i, (core::at(glow->position, i) - across) - up);
        set(core::at(q, 1), i, (core::at(glow->position, i) - across) + up);
        set(core::at(q, 2), i, (up + across) + core::at(glow->position, i));
        set(core::at(q, 3), i, (across + core::at(glow->position, i)) - up);
    }
    for (GlowVertex& corner : q)
        corner.colour = colour;
}

} // namespace

// 0x004DCC70
void update_glows(bool queries)
{
    // The camera's across and up directions from the view matrix's third column (a, b, c): u = (b*0 - c, c*0 - a*0,
    // a - b*0) and v = u x (a, b, c), each then normalised.
    const double a = core::at(renderer().view_matrix().row, 0).z;
    const double b = core::at(renderer().view_matrix().row, 1).z;
    const double c = core::at(renderer().view_matrix().row, 2).z;
    std::array<float, 3> u{};
    std::array<float, 3> v{};
    core::at(u, 0) = static_cast<float>(b * 0.0f - c);
    core::at(u, 1) = static_cast<float>(c * 0.0f - a * 0.0f);
    core::at(u, 2) = static_cast<float>(a - b * 0.0f);
    core::at(v, 0) = static_cast<float>(core::at(u, 1) * c - core::at(u, 2) * b);
    core::at(v, 1) = static_cast<float>(core::at(u, 2) * a - c * core::at(u, 0));
    core::at(v, 2) = static_cast<float>(b * core::at(u, 0) - core::at(u, 1) * a);
    for (std::array<float, 3>* w : {&u, &v}) {
        const double w0 = core::at(*w, 0);
        const double w1 = core::at(*w, 1);
        const double w2 = core::at(*w, 2);
        const double length = std::sqrt((w2 * w2 + w1 * w1) + w0 * w0);
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            for (float& component : *w)
                component = static_cast<float>(component * inverse);
        }
    }
    lists::KeyMap& table = glows();
    table.set_iter_bucket(-1);
    table.set_iter_item(nullptr);
    Glow* glow = next_glow();

    if (queries) {
        // Each glow gets an occlusion query (up to three outstanding).
        renderer().d3d_device()->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        choose_blend_mode(0);
        set_texture(0, nullptr);
        set_pixel_shader(nullptr, nullptr);
        renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        renderer().d3d_device()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        for (; glow; glow = next_glow()) {
            if (glow->query_count_ >= 3)
                continue;
            uint32_t query = 0;
            for (; query < 0x400; ++query) {
                if (!core::at(queries_in_use(), query)) {
                    core::at(queries_in_use(), query) = 1;
                    break;
                }
            }
            if (query == 0x400)
                query = 0x3FF; // (none free: the last, not marked)
            core::at(glow->queries, glow->query_count_++) = query;
            if (query > 0)
                set_quad(glow, u, v, glow->query_size_, 0xFFFFFFFF);
        }
        renderer().d3d_device()->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        renderer().d3d_device()->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        return;
    }

    // Each glow grows towards its size while its queries see it, and fades otherwise.
    choose_blend_mode(5);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader94c), nullptr);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    for (; glow; glow = next_glow()) {
        bool seen = false;
        if (!renderer().radeon8500()) { // (not on a Radeon 8500)
            uint32_t pixels = 0;
            const int result = query_result(core::at(glow->queries, 0), &pixels);
            if (result == 0) {
                core::at(queries_in_use(), core::at(glow->queries, 0)) = 0;
                core::at(glow->queries, 0) = core::at(glow->queries, 1);
                core::at(glow->queries, 1) = core::at(glow->queries, 2);
                core::at(glow->queries, 2) = core::at(glow->queries, 3);
                --glow->query_count_;
                core::at(glow->queries, 3) = 0;
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
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

} // namespace render

namespace render {

// 0x00640E10 (a static initialiser): the glows' table, 256 buckets.
void construct_glow_table() { new (&glow_state().glow_table_) lists::KeyMap(8); }

} // namespace render
