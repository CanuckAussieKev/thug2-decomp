// The fog on PC (Nx engine's platform calls behind SetFogColor / SetFogDistance / EnableFog): see pc_fog.h.

#include <cstdint>

#include "engine/gfx/pc_fog.h"

#include <windows.h>

#include <d3d9.h>

#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/settings.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
bool loading_screen_up() { return g_loading_screen_up; }
uint32_t& fog_render_flag() { return g_fog_active; }

} // namespace

// 0x004B8E40
void __cdecl toggle_fog(bool on)
{
    if ((fog_render_flag() != 0) == on)
        return;
    fog_render_flag() = on;
    if (!loading_screen_up())
        input_port()->SetRenderState(D3DRS_FOGENABLE, fog_render_flag());
}

// 0x004B8E80
void __cdecl set_fog_distance(float start)
{
    g_fog_begin_distance = start;
    // the end: 7200 further with distance clipping (0x007D643A), else four times as far
    double end = g_distance_clipping ? static_cast<double>(start) + 7200.0f
                                                : static_cast<double>(start) * 3.0f + start;
    g_fog_end = static_cast<float>(end);
    input_port()->SetRenderState(D3DRS_FOGSTART, reinterpret_cast<uint32_t&>(g_fog_begin_distance));
    input_port()->SetRenderState(D3DRS_FOGEND, reinterpret_cast<uint32_t&>(g_fog_end));
}

// 0x004B8EE0
void __cdecl set_fog_color(uint32_t rgba)
{
    uint32_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF, a = rgba >> 24;
    if (a && !fog_render_flag()) {
        fog_render_flag() = 1;
        if (!loading_screen_up())
            input_port()->SetRenderState(D3DRS_FOGENABLE, 1);
    }
    // the density (alpha / 128, 0-1) for the pixel shaders' fog constant (0x00786F0C)
    double density = static_cast<double>(a) * (1.0 / 128.0);
    if (!(density < 1.0))
        density = 1.0;
    else if (!(density > 0.0))
        density = 0.0;
    float* constant = &g_ps_constant_bank[16];
    constant[0] = constant[1] = constant[2] = static_cast<float>(density);
    uint32_t color = (r << 16) | (g << 8) | b;
    g_fog_colour = color;
    input_port()->SetRenderState(D3DRS_FOGCOLOR, color);
}

} // namespace Nx
