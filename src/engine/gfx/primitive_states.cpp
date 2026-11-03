// The device states for the engine's list of 3D primitives (0x0048E370 draws it between these two), and three thin
// device calls.

#include <cstdint>

#include "engine/gfx/primitive_states.h"

#include <windows.h>

#include <d3d9.h>

#include "engine/gfx/render_state.h"
#include "engine/gfx/renderer.h"

namespace render {

// 0x004B60B0
void set_primitive_states()
{
    choose_blend_mode(kBlendModePrimitive);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_TEX1);
    set_pixel_shader(nullptr, nullptr);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    change_phase(kAddressMode0, kClampBothWays); // (clamped both ways)
    change_phase(kPhaseAlphaTest, 1);
    change_phase(kZWrite, 1);
    change_phase(kZTest, 1);
    change_phase(kCullMode, D3DCULL_NONE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
}

// 0x004B5FF0
void reset_primitive_states()
{
    set_texture(0, nullptr); // (the original passes a third 0)
    change_phase(kCullMode, D3DCULL_CW);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// 0x004B6030
void set_texture_stage_state(uint32_t stage, uint32_t type, uint32_t value)
{
    renderer().d3d_device()->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type), value);
}

// 0x004B6050
void set_transform(uint32_t type, const void* matrix)
{
    renderer().d3d_device()->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(type), static_cast<const D3DMATRIX*>(matrix));
}

// 0x004B6070
long set_render_target_and_depth(RenderTargets targets)
{
    const auto surface = targets.surface;
    const auto depth = targets.depth;
    long result = 0;
    if (renderer().d3d_device()->SetRenderTarget(0, static_cast<IDirect3DSurface9*>(surface)) != 0)
        result = D3DERR_INVALIDCALL;
    if (renderer().d3d_device()->SetDepthStencilSurface(static_cast<IDirect3DSurface9*>(depth)) != 0)
        return D3DERR_INVALIDCALL;
    return result;
}

} // namespace render
