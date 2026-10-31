// The device states for the engine's list of 3D primitives (0x0048E370 draws it between these two), and three thin
// device calls.

#include <cstdint>

#include "game/gfx/primitive_states.h"

#include <windows.h>

#include <d3d9.h>

#include "game/gfx/render_state.h"
#include "game/gfx/device.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }

} // namespace

// 0x004B60B0
void __cdecl SetPrimitiveStates()
{
    SetBlendMode(0x11);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_TEX1);
    SetPixelShader(nullptr, nullptr);
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    SetState(kAddressMode0, 0x10001); // (clamped both ways)
    SetState(kAlphaRef, 1);
    SetState(kZWrite, 1);
    SetState(kZTest, 1);
    SetState(kCullMode, D3DCULL_NONE);
    Device()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    Device()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
}

// 0x004B5FF0
void __cdecl ResetPrimitiveStates()
{
    SetTexture(0, nullptr); // (the original passes a third 0)
    SetState(kCullMode, D3DCULL_CW);
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// 0x004B6030
void __cdecl SetTextureStageState(uint32_t stage, uint32_t type, uint32_t value)
{
    Device()->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type), value);
}

// 0x004B6050
void __cdecl SetTransform(uint32_t type, const void* matrix)
{
    Device()->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(type), static_cast<const D3DMATRIX*>(matrix));
}

// 0x004B6070
long __cdecl SetRenderTargetAndDepth(void* surface, void* depth)
{
    long result = 0;
    if (Device()->SetRenderTarget(0, static_cast<IDirect3DSurface9*>(surface)) != 0)
        result = D3DERR_INVALIDCALL;
    if (Device()->SetDepthStencilSurface(static_cast<IDirect3DSurface9*>(depth)) != 0)
        return D3DERR_INVALIDCALL;
    return result;
}

} // namespace Nx
