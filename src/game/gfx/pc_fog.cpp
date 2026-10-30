// The fog on PC (Nx engine's platform calls behind SetFogColor / SetFogDistance / EnableFog): see pc_fog.h.

#include "game/gfx/pc_fog.h"

#include <windows.h>

#include <d3d9.h>

#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/gfx/settings.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/gfx/device.h"
#include "game/gfx/engine_glue.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }
bool LoadingScreenUp() { return g_loadingScreenUp; }
uint32_t& FogEnabled() { return g_fogEnabled; }

} // namespace

// 0x004B8E40
void __cdecl SetFogEnabled(bool on)
{
    if ((FogEnabled() != 0) == on)
        return;
    FogEnabled() = on;
    if (!LoadingScreenUp())
        Device()->SetRenderState(D3DRS_FOGENABLE, FogEnabled());
}

// 0x004B8E80
void __cdecl SetFogDistance(float start)
{
    g_fogStart = start;
    // the end: 7200 further with distance clipping (0x007D643A), else four times as far
    double end = g_distanceClipping ? static_cast<double>(start) + 7200.0f
                                                : static_cast<double>(start) * 3.0f + start;
    g_fogEnd = static_cast<float>(end);
    Device()->SetRenderState(D3DRS_FOGSTART, reinterpret_cast<uint32_t&>(g_fogStart));
    Device()->SetRenderState(D3DRS_FOGEND, reinterpret_cast<uint32_t&>(g_fogEnd));
}

// 0x004B8EE0
void __cdecl SetFogColor(uint32_t rgba)
{
    uint32_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF, a = rgba >> 24;
    if (a && !FogEnabled()) {
        FogEnabled() = 1;
        if (!LoadingScreenUp())
            Device()->SetRenderState(D3DRS_FOGENABLE, 1);
    }
    // the density (alpha / 128, 0-1) for the pixel shaders' fog constant (0x00786F0C)
    double density = static_cast<double>(a) * (1.0 / 128.0);
    if (!(density < 1.0))
        density = 1.0;
    else if (!(density > 0.0))
        density = 0.0;
    float* constant = &g_pixelShaderConstants[16];
    constant[0] = constant[1] = constant[2] = static_cast<float>(density);
    uint32_t color = (r << 16) | (g << 8) | b;
    g_fogColour = color;
    Device()->SetRenderState(D3DRS_FOGCOLOR, color);
}

} // namespace Nx

REPLACE(0x004B8E40, Nx::SetFogEnabled);
REPLACE(0x004B8E80, Nx::SetFogDistance);
REPLACE(0x004B8EE0, Nx::SetFogColor);
