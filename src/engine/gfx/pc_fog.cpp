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
#include "engine/core/checked_index.h"
#include <bit>

namespace render {

namespace {


} // namespace

// 0x004B8E40
void toggle_fog(bool on)
{
    if ((fog_active() != 0) == on)
        return;
    set_fog_active(on);
    if (!loading_screen_up())
        d3d_device()->SetRenderState(D3DRS_FOGENABLE, fog_active());
}

// 0x004B8E80
void set_fog_distance(float start)
{
    set_fog_begin_distance(start);
    // the end: 7200 further with distance clipping (0x007D643A), else four times as far
    const double end =
        distance_clipping() ? static_cast<double>(start) + 7200.0f : static_cast<double>(start) * 3.0f + start;
    set_fog_end(static_cast<float>(end));
    d3d_device()->SetRenderState(D3DRS_FOGSTART, std::bit_cast<uint32_t>(fog_begin_distance()));
    d3d_device()->SetRenderState(D3DRS_FOGEND, std::bit_cast<uint32_t>(fog_end()));
}

// 0x004B8EE0
void set_fog_color(uint32_t rgba)
{
    const uint32_t r = rgba & 0xFF;
    const uint32_t g = (rgba >> 8) & 0xFF;
    const uint32_t b = (rgba >> 16) & 0xFF;
    const uint32_t a = rgba >> 24;
    if (a && !fog_active()) {
        set_fog_active(1);
        if (!loading_screen_up())
            d3d_device()->SetRenderState(D3DRS_FOGENABLE, 1);
    }
    // the density (alpha / 128, 0-1) for the pixel shaders' fog constant (0x00786F0C)
    double density = static_cast<double>(a) * (1.0 / 128.0);
    if (!(density < 1.0))
        density = 1.0;
    else if (!(density > 0.0))
        density = 0.0;
    core::at(ps_constant_bank(), 16) = core::at(ps_constant_bank(), 17) = core::at(ps_constant_bank(), 18) =
        static_cast<float>(density);
    const uint32_t color = (r << 16) | (g << 8) | b;
    set_fog_colour(color);
    d3d_device()->SetRenderState(D3DRS_FOGCOLOR, color);
}

} // namespace render
