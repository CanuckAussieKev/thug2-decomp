// The device states for the engine's list of 3D primitives (0x0048E370 draws it between these two), and three thin
// device calls.

#include <cstdint>

#include "engine/gfx/primitive_states.h"

#include <windows.h>

#include <d3d9.h>

#include "engine/gfx/render_state.h"
#include "engine/gfx/device.h"

namespace render {

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }

} // namespace

// 0x004B60B0
void __cdecl set_primitive_states()
{
    choose_blend_mode(0x11);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_TEX1);
    set_pixel_shader(nullptr, nullptr);
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    change_phase(kAddressMode0, 0x10001); // (clamped both ways)
    change_phase(kPhaseAlphaTest, 1);
    change_phase(kZWrite, 1);
    change_phase(kZTest, 1);
    change_phase(kCullMode, D3DCULL_NONE);
    input_port()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    input_port()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
}

// 0x004B5FF0
void __cdecl reset_primitive_states()
{
    set_texture(0, nullptr); // (the original passes a third 0)
    change_phase(kCullMode, D3DCULL_CW);
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// 0x004B6030
void __cdecl set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value)
{
    input_port()->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type), value);
}

// 0x004B6050
void __cdecl set_transform(uint32_t type, const void* matrix)
{
    input_port()->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(type), static_cast<const D3DMATRIX*>(matrix));
}

// 0x004B6070
long __cdecl set_render_target_and_depth(void* surface, void* depth)
{
    long result = 0;
    if (input_port()->SetRenderTarget(0, static_cast<IDirect3DSurface9*>(surface)) != 0)
        result = D3DERR_INVALIDCALL;
    if (input_port()->SetDepthStencilSurface(static_cast<IDirect3DSurface9*>(depth)) != 0)
        return D3DERR_INVALIDCALL;
    return result;
}

} // namespace Nx
