// The fog on PC (Nx engine's platform calls behind SetFogColor / SetFogDistance / EnableFog): see pc_fog.h.

#include <cstdint>

#include "engine/gfx/pc_fog.h"

#include <windows.h>

#include <d3d9.h>

#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"
#include "engine/gfx/engine_parts.h"
#include <bit>

namespace render {

constexpr double kFogDensityScale = 128.0; // the alpha byte of the fog colour is the density, 128 being 1
constexpr int kFogConstantRed = 16;        // the pixel shader constants that hold the density, one a colour channel
constexpr int kFogConstantGreen = 17;
constexpr int kFogConstantBlue = 18;

// 0x004B8E40
void toggle_fog(bool on)
{
    if ((renderer().fog_active() != 0) == on) {
        return;
    }
    renderer().set_fog_active(on);
    if (!engine_parts().loading_screen_up()) {
        renderer().d3d_device()->SetRenderState(D3DRS_FOGENABLE, renderer().fog_active());
    }
}

// 0x004B8E80
void set_fog_distance(float start)
{
    renderer().set_fog_begin_distance(start);
    // the end: 7200 further with distance clipping (0x007D643A), else four times as far
    const double end = view_setup().distance_clipping() ? static_cast<double>(start) + 7200.0F
                                                        : (static_cast<double>(start) * 3.0F) + start;
    renderer().set_fog_end(static_cast<float>(end));
    renderer().d3d_device()->SetRenderState(D3DRS_FOGSTART, std::bit_cast<uint32_t>(renderer().fog_begin_distance()));
    renderer().d3d_device()->SetRenderState(D3DRS_FOGEND, std::bit_cast<uint32_t>(renderer().fog_end()));
}

// 0x004B8EE0
void set_fog_color(uint32_t rgba)
{
    const uint32_t r = rgba & 0xFF;
    const uint32_t g = (rgba >> 8) & 0xFF;
    const uint32_t b = (rgba >> 16) & 0xFF;
    const uint32_t a = rgba >> 24;
    if (a && !renderer().fog_active()) {
        renderer().set_fog_active(1);
        if (!engine_parts().loading_screen_up()) {
            renderer().d3d_device()->SetRenderState(D3DRS_FOGENABLE, 1);
        }
    }
    // the density (alpha / 128, 0-1) for the pixel shaders' fog constant (0x00786F0C)
    double density = static_cast<double>(a) * (1.0 / kFogDensityScale);
    if (!(density < 1.0)) {
        density = 1.0;
    } else if (!(density > 0.0)) {
        density = 0.0;
    }
    renderer().set_ps_constant(kFogConstantRed, static_cast<float>(density));
    renderer().set_ps_constant(kFogConstantGreen, static_cast<float>(density));
    renderer().set_ps_constant(kFogConstantBlue, static_cast<float>(density));
    const uint32_t color = (r << 16) | (g << 8) | b;
    engine_parts().set_fog_colour(color);
    renderer().d3d_device()->SetRenderState(D3DRS_FOGCOLOR, color);
}

} // namespace render
