// The renderer's cached Direct3D 9 state: render states, sampler states, textures, blend modes, pixel shaders.

#include "game/gfx/render_state.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <initializer_list>

#include "game/gfx/device.h"
#include "game/gfx/graphics_options.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/pixel_shaders.h"
#include "game/gfx/render.h"
#include "game/gfx/view_mode.h"
#include "game/gfx/shader_slots.h"
#include "game/gfx/engine_glue.h"

namespace Nx {

bool g_shadersAvailable;
uint32_t g_blendMode;
uint32_t g_blendOp;
uint32_t g_srcBlend;
uint32_t g_destBlend;
uint32_t g_alphaBlend;
uint32_t g_alphaTest;
uint32_t g_alphaRef;
uint32_t g_specular;
uint8_t g_dither;
uint8_t g_zWrite;
uint8_t g_zTest;
uint32_t g_depthBias;
uint32_t g_lighting;
uint32_t g_cullMode;
bool g_nearCamera;
uint32_t g_addressModes[4];
uint32_t g_mipLodBiases[4];
uint32_t g_filters[4];
void* g_textures[4];
uint32_t g_borderColours[4];
uint32_t g_vertexFormat;
uint32_t g_pixelShaderLocked;
void* g_currentPixelShader;
float g_pixelShaderConstants[20];
uint32_t g_instanceColourOn;
uint8_t g_instanceColour[4];
uint8_t g_pixelShaderChanged;
uint32_t g_vertexFormatLocked;
uint32_t g_skippedPasses;
uint32_t g_submitLocked;
uint32_t g_blendModeOverride;
uint32_t g_fogEnabled;
uint8_t g_localViewer;
const float* g_worldTransform;
float g_worldTransformCopy[16];

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }

// What the device was last given (the cache).
uint32_t& BlendMode() { return g_blendMode; }
uint32_t& BlendOp() { return g_blendOp; }
uint32_t& SrcBlend() { return g_srcBlend; }
uint32_t& DestBlend() { return g_destBlend; }
uint32_t& AlphaBlend() { return g_alphaBlend; }
uint32_t& AlphaTest() { return g_alphaTest; }
uint32_t& AlphaRef() { return g_alphaRef; }
uint32_t& Specular() { return g_specular; }
uint8_t& Dither() { return g_dither; }
uint8_t& ZWrite() { return g_zWrite; }
uint8_t& ZTest() { return g_zTest; }
uint32_t& DepthBias() { return g_depthBias; }
uint32_t& Lighting() { return g_lighting; }
uint32_t& CullMode() { return g_cullMode; }
uint32_t* AddressModes() { return g_addressModes; } // [4]: U low 16 bits, V high
uint32_t* MipLodBiases() { return g_mipLodBiases; } // [4]: float bits
uint32_t* Filters() { return g_filters; }      // [4]: min low 16 bits, mag high
void** Textures() { return g_textures; }           // [4]
uint32_t& VertexFormat() { return g_vertexFormat; }
uint32_t& PixelShaderLocked() { return g_pixelShaderLocked; }
void*& CurrentPixelShader() { return g_currentPixelShader; }
float* PixelShaderConstants() { return g_pixelShaderConstants; } // [5][4]; [4] is 0x00786F0C
uint8_t& PixelShaderChanged() { return g_pixelShaderChanged; }
uint32_t& VertexFormatLocked() { return g_vertexFormatLocked; }
uint32_t& BlendModeOverride() { return g_blendModeOverride; }
uint32_t& Fog() { return g_fogEnabled; }
uint32_t& FogColor() { return g_fogColour; }
uint8_t& LocalViewer() { return g_localViewer; }

// Device capabilities (found at start-up).
bool HasPixelShaders() { return g_shadersAvailable; }
bool HasBorderAddressing() { return g_borderAddressingSupported; }
bool HasBlendFactor() { return g_blendFactorSupported; }

// The pixel shaders (handles made at start-up) the stand-in texture stages know.
void* Shader(int slot) { return g_pixelShaders[slot]; }

void SetTss(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) { Device()->SetTextureStageState(stage, type, value); }
void SetSampler(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) { Device()->SetSamplerState(sampler, type, value); }
void SetDeviceRenderState(D3DRENDERSTATETYPE state, DWORD value) { Device()->SetRenderState(state, value); }

// D3DTADDRESS for the renderer's addressing modes (border needs the device's support; else clamp).
bool AddressValue(uint32_t mode, DWORD* value)
{
    switch (mode) {
    case 0: *value = D3DTADDRESS_WRAP; return true;
    case 1: *value = D3DTADDRESS_CLAMP; return true;
    case 2: *value = HasBorderAddressing() ? D3DTADDRESS_BORDER : D3DTADDRESS_CLAMP; return true;
    default: return false; // (nothing sent, but the cache takes the value)
    }
}

constexpr float kDepthBiasUnit = std::bit_cast<float>(0xB60637BDu); // 0x0064AAA4
constexpr float k255 = 255.0f;                                      // 0x00646B88

// The CRT's _ftol (0x006236FC): truncation, as a 64-bit integer.
int64_t Ftol(double value) { return static_cast<int64_t>(value); }

} // namespace

// 0x004DAB60
void __cdecl SetState(int state, uint32_t value)
{
    switch (state) {
    case kZWrite:
        if (value) {
            if (!ZWrite()) {
                Nx::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
                ZWrite() = 1;
            }
        } else if (ZWrite() == 1) {
            Nx::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            ZWrite() = 0;
        }
        return;
    case kZTest:
        if (value) {
            if (!ZTest()) {
                Nx::SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
                ZTest() = 1;
            }
        } else if (ZTest() == 1) {
            Nx::SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            ZTest() = 0;
        }
        return;
    case kAlphaRef:
        if (value == AlphaRef())
            return;
        AlphaRef() = value;
        if (value) {
            Nx::SetRenderState(D3DRS_ALPHAREF, value);
            if (!AlphaTest()) {
                Nx::SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
                AlphaTest() = 1;
            }
        } else if (AlphaTest()) {
            Nx::SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            AlphaTest() = 0;
        }
        return;
    case kAddressMode0:
    case kAddressMode0 + 1:
    case kAddressMode0 + 2:
    case kAddressMode0 + 3: {
        int sampler = state - kAddressMode0;
        uint32_t& cached = AddressModes()[sampler];
        uint32_t u = value & 0xFFFF;
        if (u != (cached & 0xFFFF)) {
            DWORD address;
            if (AddressValue(u, &address))
                SetSampler(sampler, D3DSAMP_ADDRESSU, address);
            cached = (cached & 0xFFFF0000) | u;
        }
        uint32_t v = value & 0xFFFF0000;
        if (v == (cached & 0xFFFF0000))
            return;
        DWORD address;
        if (AddressValue(v >> 16, &address))
            SetSampler(sampler, D3DSAMP_ADDRESSV, address);
        cached = (cached & 0xFFFF) | v;
        return;
    }
    case kMipLodBias0:
    case kMipLodBias0 + 1:
    case kMipLodBias0 + 2:
    case kMipLodBias0 + 3: {
        int sampler = state - kMipLodBias0;
        if (value != MipLodBiases()[sampler]) {
            SetSampler(sampler, D3DSAMP_MIPMAPLODBIAS, value);
            MipLodBiases()[sampler] = value;
        }
        return;
    }
    case kCullMode:
        if (value != CullMode()) {
            CullMode() = value;
            Nx::SetRenderState(D3DRS_CULLMODE, value);
        }
        return;
    case kAlphaBlend:
        if (value != AlphaBlend()) {
            AlphaBlend() = value;
            Nx::SetRenderState(D3DRS_ALPHABLENDENABLE, value != 0);
        }
        return;
    case kAlphaTest:
        if (value != AlphaTest()) {
            AlphaTest() = value;
            Nx::SetRenderState(D3DRS_ALPHATESTENABLE, value != 0);
        }
        return;
    case kSpecular:
        if (value != Specular()) {
            Specular() = value;
            Nx::SetRenderState(D3DRS_SPECULARENABLE, value != 0);
            Nx::SetRenderState(D3DRS_LOCALVIEWER, value != 0);
        }
        return;
    case kFog:
        if (value != Fog()) {
            Fog() = value;
            Nx::SetRenderState(D3DRS_FOGENABLE, value != 0);
        }
        return;
    case kDepthBias:
        if (value != DepthBias()) {
            DepthBias() = value;
            // (unsigned, scaled; x87 at double precision, stored as a float)
            float bias = static_cast<float>(static_cast<double>(value) * kDepthBiasUnit);
            Nx::SetRenderState(D3DRS_DEPTHBIAS, std::bit_cast<uint32_t>(bias));
        }
        return;
    case kLighting:
        if (value != Lighting()) {
            Lighting() = value;
            Nx::SetRenderState(D3DRS_LIGHTING, value != 0);
        }
        return;
    case kFilter0:
    case kFilter0 + 1:
    case kFilter0 + 2:
    case kFilter0 + 3: {
        int sampler = state - kFilter0;
        uint32_t& cached = Filters()[sampler];
        uint32_t mag = value & 0xFFFF0000;
        if (mag != (cached & 0xFFFF0000)) {
            SetSampler(sampler, D3DSAMP_MAGFILTER, (value & 0x10000) ? D3DTEXF_LINEAR : D3DTEXF_POINT);
            cached = (cached & 0xFFFF) | mag;
        }
        uint32_t min = value & 0xFFFF;
        if (min != (cached & 0xFFFF)) {
            // NOT ORIGINAL - [Graphics] anisotropy (game/gfx/graphics_options.h): anisotropic in place of linear.
            // Original behaviour: (value & 1) ? D3DTEXF_LINEAR : D3DTEXF_POINT
            SetSampler(sampler, D3DSAMP_MINFILTER,
                       GraphicsOptions::MinFilter((value & 1) != 0, GraphicsOptions::ActiveAnisotropy()));
            cached = (cached & 0xFFFF0000) | min;
        }
        return;
    }
    case kLocalViewer:
        if (value != LocalViewer()) {
            LocalViewer() = value != 0;
            Nx::SetRenderState(D3DRS_LOCALVIEWER, value);
        }
        return;
    default: // (0xC-0xF, 0x17-0x1F: nothing)
        return;
    }
}

// 0x004D9DB0
void __cdecl SetVertexFormat(uint32_t fvf)
{
    if (VertexFormatLocked() == 0 && VertexFormat() != fvf) {
        Device()->SetFVF(fvf);
        VertexFormat() = fvf;
    }
}

// 0x004DA730
void __cdecl SetTexture(int stage, void* texture)
{
    if (Textures()[stage] != texture) {
        Device()->SetTexture(stage, static_cast<IDirect3DBaseTexture9*>(texture));
        Textures()[stage] = texture;
    }
}

// 0x004DA760
void __cdecl SetBlendMode(uint32_t mode)
{
    uint32_t wanted = BlendModeOverride() ? BlendModeOverride() : mode;
    if (wanted == BlendMode())
        return;
    uint32_t kind = wanted & 0xFFFFFF;
    if (kind != (BlendMode() & 0xFFFFFF)) {
        // Fog goes black under the additive and subtractive modes (1-4).
        if (Fog()) {
            if (kind >= 1 && kind <= 4)
                Nx::SetRenderState(D3DRS_FOGCOLOR, 0);
            else
                SetDeviceRenderState(D3DRS_FOGCOLOR, FogColor());
        }
        uint32_t op, src, dest;
        switch (kind) {
        case 0:
        case 0xB: op = D3DBLENDOP_ADD; src = D3DBLEND_ONE; dest = D3DBLEND_ZERO; break;
        case 1: op = D3DBLENDOP_ADD; src = D3DBLEND_SRCALPHA; dest = D3DBLEND_ONE; break;
        case 2: op = D3DBLENDOP_ADD; src = D3DBLEND_BLENDFACTOR; dest = D3DBLEND_ONE; break;
        case 3: op = D3DBLENDOP_REVSUBTRACT; src = D3DBLEND_SRCALPHA; dest = D3DBLEND_ONE; break;
        case 4: op = D3DBLENDOP_REVSUBTRACT; src = D3DBLEND_BLENDFACTOR; dest = D3DBLEND_ONE; break;
        case 5:
        case 0xC: op = D3DBLENDOP_ADD; src = D3DBLEND_SRCALPHA; dest = D3DBLEND_INVSRCALPHA; break;
        case 6: op = D3DBLENDOP_ADD; src = D3DBLEND_BLENDFACTOR; dest = D3DBLEND_INVBLENDFACTOR; break;
        case 7:
        case 8: op = D3DBLENDOP_ADD; src = D3DBLEND_ZERO; dest = D3DBLEND_SRCALPHA; break;
        case 9: op = D3DBLENDOP_ADD; src = D3DBLEND_DESTCOLOR; dest = D3DBLEND_ONE; break;
        case 0xA: op = D3DBLENDOP_ADD; src = D3DBLEND_DESTCOLOR; dest = D3DBLEND_BLENDFACTOR; break;
        case 0xD: op = D3DBLENDOP_ADD; src = D3DBLEND_INVSRCALPHA; dest = D3DBLEND_SRCALPHA; break;
        case 0xF: op = D3DBLENDOP_ADD; src = D3DBLEND_ZERO; dest = D3DBLEND_SRCCOLOR; break;
        case 0x11: op = D3DBLENDOP_ADD; src = D3DBLEND_ONE; dest = D3DBLEND_INVSRCALPHA; break;
        default: // (0xE, 0x10 and anything bigger: the argument itself - not the override - for all three)
            op = src = dest = mode;
            break;
        }
        // Without blend factors: the nearest the colour and source alpha can do.
        if (!HasBlendFactor()) {
            if (src == D3DBLEND_BLENDFACTOR && dest == D3DBLEND_INVBLENDFACTOR) {
                src = D3DBLEND_ONE;
                dest = D3DBLEND_INVSRCALPHA;
            } else {
                if (src == D3DBLEND_BLENDFACTOR)
                    src = D3DBLEND_ZERO;
                if (dest == D3DBLEND_BLENDFACTOR)
                    dest = D3DBLEND_SRCALPHA;
                else if (dest == D3DBLEND_INVBLENDFACTOR)
                    dest = D3DBLEND_INVSRCALPHA;
            }
        }
        if (op != BlendOp()) {
            SetDeviceRenderState(D3DRS_BLENDOP, op);
            BlendOp() = op;
        }
        if (src != SrcBlend()) {
            SetDeviceRenderState(D3DRS_SRCBLEND, src);
            SrcBlend() = src;
        }
        if (dest != DestBlend()) {
            SetDeviceRenderState(D3DRS_DESTBLEND, dest);
            DestBlend() = dest;
        }
    }
    // The blend factor: the mode's top byte doubled (0x80 and up: full), in all four channels.
    uint32_t alpha = wanted & 0xFF000000;
    if (alpha != (BlendMode() & 0xFF000000)) {
        alpha = alpha >= 0x80000000 ? 0xFF000000 : alpha << 1;
        SetDeviceRenderState(D3DRS_BLENDFACTOR, (((alpha >> 8 | alpha) >> 8 | alpha) >> 8) | alpha);
    }
    BlendMode() = wanted;
}

// 0x004D9DE0
void __cdecl SetFixedFunctionStages(void* shader, uint32_t factor)
{
    // The last stage-0 state each one sets, then stage 1 off.
    D3DTEXTURESTAGESTATETYPE lastType;
    DWORD lastValue;
    if (shader == Shader(kPixelShader964)) {
        SetTss(0, D3DTSS_COLOROP, D3DTOP_MODULATE4X);
        SetTss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        SetTss(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        SetTss(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
        SetTss(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        SetTss(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        SetTss(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        return;
    }
    if (shader == Shader(kPixelShader95C)) { // vertex colour x2 x the factor, vertex alpha
        SetDeviceRenderState(D3DRS_TEXTUREFACTOR, factor);
        SetTss(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        SetTss(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        SetTss(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
        SetTss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        lastType = D3DTSS_ALPHAARG1;
        lastValue = D3DTA_DIFFUSE;
    } else if (shader != Shader(kPixelShader958) && shader == Shader(kPixelShader954)) { // the factor alone
        SetDeviceRenderState(D3DRS_TEXTUREFACTOR, factor);
        SetTss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        SetTss(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        SetTss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        lastType = D3DTSS_ALPHAARG1;
        lastValue = D3DTA_TFACTOR;
    } else if (shader != Shader(kPixelShader958) && shader == Shader(kPixelShader950)) { // texture x vertex colour x2, texture alpha
        SetTss(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        SetTss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        SetTss(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        SetTss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        SetTss(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        lastType = D3DTSS_ALPHAARG2;
        lastValue = D3DTA_DIFFUSE;
    } else if (shader != Shader(kPixelShader958) && shader == Shader(kPixelShader94C)) { // vertex colour alone
        SetTss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        SetTss(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        SetTss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        lastType = D3DTSS_ALPHAARG1;
        lastValue = D3DTA_DIFFUSE;
    } else if (shader != Shader(kPixelShader958) && shader == Shader(kPixelShader944)) { // texture alpha x vertex alpha x2
        SetTss(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        SetTss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE);
        SetTss(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE | D3DTA_ALPHAREPLICATE);
        SetTss(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
        SetTss(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        lastType = D3DTSS_ALPHAARG2;
        lastValue = D3DTA_DIFFUSE;
    } else { // kPixelShader958 and any other: the texture alone
        SetTss(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        SetTss(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        SetTss(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        lastType = D3DTSS_ALPHAARG1;
        lastValue = D3DTA_TEXTURE;
    }
    SetTss(0, lastType, lastValue);
    SetTss(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
}

// 0x004DAA10
void __cdecl SetFixedFunctionStagesForMaterial(void* shader, const void* material)
{
    if (!material) {
        SetFixedFunctionStages(shader, 0xFFFFFFFF);
        return;
    }
    const float* color = reinterpret_cast<const float*>(static_cast<const uint8_t*>(material) + 0x40);
    uint32_t r = static_cast<uint8_t>(Ftol(static_cast<double>(color[0]) * k255));
    uint32_t g = static_cast<uint8_t>(Ftol(static_cast<double>(color[1]) * k255));
    uint32_t b = static_cast<uint8_t>(Ftol(static_cast<double>(color[2]) * k255));
    SetFixedFunctionStages(shader, (((r | 0xFFFFFF00) << 8 | g) << 8) | b);
}

// 0x004DAA80
void __cdecl SetPixelShader(void* shader, const void* material)
{
    if (!HasPixelShaders()) {
        SetFixedFunctionStagesForMaterial(shader, material);
        return;
    }
    if (PixelShaderLocked() == 0) {
        if (CurrentPixelShader() != shader) {
            // Not original - the level editor's Unlit view (game/gfx/view_mode.h): the shader's unlit twin during the 3D
            // passes. Original behaviour: the device gets `shader`.
            Device()->SetPixelShader(static_cast<IDirect3DPixelShader9*>(ViewMode::DeviceShader(shader)));
            CurrentPixelShader() = shader;
            PixelShaderChanged() = 1;
        }
        Device()->SetPixelShaderConstantF(0, PixelShaderConstants(), 5);
    }
    PixelShaderChanged() = 0;
}

// 0x004DAAF0
void __cdecl SetPixelShaderWithConstants(void* shader, uint32_t count)
{
    if (PixelShaderLocked() == 0) {
        if (CurrentPixelShader() != shader) {
            // Not original - the Unlit view's twin (view_mode.h), as in SetPixelShader.
            Device()->SetPixelShader(static_cast<IDirect3DPixelShader9*>(ViewMode::DeviceShader(shader)));
            CurrentPixelShader() = shader;
            PixelShaderChanged() = 1;
        }
        Device()->SetPixelShaderConstantF(0, PixelShaderConstants(), count);
        Device()->SetPixelShaderConstantF(4, PixelShaderConstants() + 16, 1);
    }
    PixelShaderChanged() = 0;
}

// 0x004DA0E0
float __cdecl LastSphereDistance() { return g_lastSphereDistance; }

// 0x004DB620
void __cdecl SetWorldTransform(const float* matrix)
{
    if (!matrix) {
        g_worldTransform = nullptr;
        return;
    }
    float* world = g_worldTransformCopy;
    g_worldTransform = world;
    for (int row = 0; row < 4; ++row) {
        world[row * 4 + 0] = matrix[row * 4 + 0];
        world[row * 4 + 1] = matrix[row * 4 + 1];
        world[row * 4 + 2] = matrix[row * 4 + 2];
        world[row * 4 + 3] = row == 3 ? 1.0f : 0.0f;
    }
}

// 0x004D7D70
void __cdecl ResetRenderState()
{
    LocalViewer() = 0;
    // The world, view and projection transforms (0x00786B90, 0x00786BD0, 0x00786C10): identity.
    for (Mth::Matrix* matrix : {&g_worldMatrix, &g_viewMatrix, &g_projectionMatrix}) {
        float* m = reinterpret_cast<float*>(matrix);
        for (int i = 0; i < 16; ++i)
            m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    Lighting() = 0;
    Specular() = 0;
    CullMode() = D3DCULL_CW;
    g_nearCamera = 1;
    Dither() = 1;
    ZTest() = 1;
    ZWrite() = 1;
    AlphaBlend() = 1;
    AlphaTest() = 1;
    AlphaRef() = 0;
    for (int i = 0; i < 4; ++i) {
        AddressModes()[i] = 0;
        MipLodBiases()[i] = 0;
        Textures()[i] = nullptr;
        Filters()[i] = 0x10000;
        g_borderColours[i] = 0;
    }
    // (none of these is a mode: the next one is always sent)
    BlendMode() = 0xDEADBABE;
    BlendOp() = 0xDEADBABE;
    SrcBlend() = 0xDEADBABE;
    DestBlend() = 0xDEADBABE;
    g_pendingScreenShot[0] = 0;
    reinterpret_cast<uint8_t&>(g_screenShotNameExact) = 0;
    // Pixel shader constant 4: (0, 0.5, 0, 0.5).
    reinterpret_cast<uint32_t&>(g_pixelShaderConstants[16]) = 0;
    g_pixelShaderConstants[17] = 0.5f;
    reinterpret_cast<uint32_t&>(g_pixelShaderConstants[18]) = 0;
    g_pixelShaderConstants[19] = 0.5f;
    PixelShaderLocked() = 0;
    CurrentPixelShader() = nullptr;
    PixelShaderChanged() = 0;
    g_camera.customTransforms = 0;
    g_billboardFlag = 0;
    VertexFormatLocked() = 0;
    g_skippedPasses = 0;
    g_submitLocked = 0;
    BlendModeOverride() = 0;
    g_camera.clearColourAndDepth = true;
    g_camera.backgroundColour = 0;
    g_camera.letterboxActive = 0; // the letterbox
    // The default light: its direction (0x00786F48) and colours, as bit patterns.
    static const uint32_t kLight[] = {
        0xBF000000, 0xBF5DB3D7, 0,          0,          0x3F160419, 0x3F160419, 0x3F160419, 0x3F800000,
        0x3F800000, 0,          0,          0,          0,          0,          0,          0x3F800000,
        0x3F800000, 0,          0,          0,          0,          0,          0,          0x3F800000,
    };
    for (int i = 0; i < 24; ++i)
        reinterpret_cast<uint32_t&>(g_lightConstants[4 + i]) = kLight[i];
    reinterpret_cast<uint32_t&>(g_lightConstants[0]) = 0x3F1624DD; // the ambient colour
    reinterpret_cast<uint32_t&>(g_lightConstants[1]) = 0x3F1624DD;
    reinterpret_cast<uint32_t&>(g_lightConstants[2]) = 0x3F1624DD;
    reinterpret_cast<uint32_t&>(g_lightConstants[3]) = 0x3F800000;
    Fog() = 0;
    FogColor() = 0;
    reinterpret_cast<uint32_t&>(g_fogStart) = 0xC3700000; // fog start and end, as bit patterns (-240, -24600)
    reinterpret_cast<uint32_t&>(g_fogEnd) = 0xC6C03000;
}

// 0x004D80D0
void __cdecl ApplyRenderState()
{
    IDirect3DDevice9* device = Device();
    device->SetRenderState(D3DRS_LOCALVIEWER, LocalViewer());
    Device()->SetRenderState(D3DRS_COLORVERTEX, FALSE);
    Device()->SetRenderState(D3DRS_LIGHTING, Lighting());
    Device()->SetRenderState(D3DRS_SPECULARENABLE, Specular());
    Device()->SetRenderState(D3DRS_CULLMODE, CullMode());
    Device()->SetRenderState(D3DRS_DITHERENABLE, Dither());
    Device()->SetRenderState(D3DRS_ZENABLE, ZTest());
    Device()->SetRenderState(D3DRS_ZWRITEENABLE, ZWrite());
    Device()->SetRenderState(D3DRS_ALPHABLENDENABLE, AlphaBlend());
    Device()->SetRenderState(D3DRS_ALPHATESTENABLE, AlphaTest());
    Device()->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    Device()->SetRenderState(D3DRS_ALPHAREF, AlphaRef());
    for (int sampler = 0; sampler < 4; ++sampler) {
        AddressModes()[sampler] = 0;
        MipLodBiases()[sampler] = 0;
        Textures()[sampler] = nullptr;
        Filters()[sampler] = 0x10000;
        g_borderColours[sampler] = 0;
        Device()->SetSamplerState(sampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        Device()->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        // NOT ORIGINAL - [Graphics] anisotropy (game/gfx/graphics_options.h): anisotropic minification on every
        // sampler at the chosen taps. (Off: the original's below.)
        if (int taps = GraphicsOptions::ActiveAnisotropy()) {
            Device()->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            Device()->SetSamplerState(sampler, D3DSAMP_MAXANISOTROPY, static_cast<DWORD>(taps));
        } else if (sampler == 0 && g_backBufferHeight <= 480) {
            // Anisotropic minification on the first sampler, at back buffers up to 480 lines high.
            Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            uint32_t anisotropy = g_maxAnisotropy;
            Device()->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, anisotropy < 3 ? anisotropy : 3);
        } else {
            Device()->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        }
        Device()->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        Device()->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
    D3DMATERIAL9 material = {};
    Device()->SetMaterial(&material);
    Device()->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    Device()->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
    Device()->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
    Device()->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
    MarkBillboardsReady();
    CreatePixelShaders();
    Device()->SetRenderState(D3DRS_FOGENABLE, Fog());
    Device()->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    Device()->SetRenderState(D3DRS_RANGEFOGENABLE, TRUE);
    Device()->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    Device()->SetRenderState(D3DRS_FOGCOLOR, FogColor());
    Device()->SetRenderState(D3DRS_FOGSTART, reinterpret_cast<uint32_t&>(g_fogStart));
    Device()->SetRenderState(D3DRS_FOGEND, reinterpret_cast<uint32_t&>(g_fogEnd));
    // (NOT ORIGINAL: or [Graphics] msaa's samples - game/gfx/graphics_options.h)
    if (g_antialiasing || GraphicsOptions::ChosenSamples()) // the antialiasing setting
        Device()->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
}

} // namespace Nx
