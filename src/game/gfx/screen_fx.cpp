// Full-screen effects: see screen_fx.h.

#include "game/gfx/screen_fx.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

#include "game/gfx/graphics_options.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/pc_scene.h"
#include "game/gfx/primitive_states.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/gfx/settings.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/shader_slots.h"
#include "game/gfx/device.h"
#include "game/gfx/engine_glue.h"

namespace Nx {

GAME_GLOBAL(bool, g_screenCopyInUse, 0x007CC244);
GAME_GLOBAL(bool, g_blurSecondPass, 0x0067EF68, 1);
GAME_GLOBAL(float[4], g_blurTapWeights, 0x0067EF88, 0.16666667f, 0.33333334f, 0.33333334f, 0.16666667f);
GAME_GLOBAL(float[4], g_blurTapOffsets, 0x0067EF98, -0.022857143f, -0.005714286f, 0.005714286f, 0.022857143f);

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }
IDirect3DDevice9* DeviceCopy() { return g_drawDevice; } // (the same device)
IDirect3DTexture9* ScreenCopy(int index) { return g_screenTextures[index]; }
IDirect3DTexture9* CurrentScreenCopy() { return ScreenCopy(g_screenCopyIndex); }
IDirect3DSurface9* DepthSurface() { return g_depthSurface; }
bool& ScreenCopyInUse() { return g_screenCopyInUse; }
void* Shader(uint32_t address) { return GameVariable<void*>(address); }

// Fog on or off (not rewritten yet; the request kept, the device's state following).
void SetFog(bool on) { Engine::EnableFog(on); }

// The renderer's draw counters.
void CountDraw(int triangles)
{
    g_drawnTriangles += triangles;
    ++g_drawCalls;
}

// An unsigned size as the x87 loads it (FILD, plus 2^32 when the top bit is set).
double Unsigned(uint32_t value) { return static_cast<double>(value); }

void SetTarget(IDirect3DTexture9* texture, IDirect3DSurface9* depth)
{
    // NOT ORIGINAL - [Graphics] msaa (game/gfx/graphics_options.h): a full-size screen copy drawn through the
    // multisampled scene surface.
    if (GraphicsOptions::SetSceneTarget(texture))
        return;
    IDirect3DSurface9* surface = nullptr;
    texture->GetSurfaceLevel(0, &surface);
    Device()->SetRenderTarget(0, surface);
    Device()->SetDepthStencilSurface(depth);
    surface->Release();
}

struct TexturedVertex {
    float x, y, z, rhw;
    uint32_t colour;
    float u, v;
};
struct FlatVertex {
    float x, y, z, rhw;
    uint32_t colour;
};

// The blur's four taps (texel offsets and weights) at 0x0067EF98 / 0x0067EF88.
float TapOffset(int tap) { return g_blurTapOffsets[tap]; }
float TapWeight(int tap) { return g_blurTapWeights[tap]; }

// A grey of 0-255 at half alpha: 0x80 then the value three times (from _ftol: a bigger value spills over).
uint32_t TapColour(float weight)
{
    uint32_t w = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(weight) * 128.0)); // 0x00647B2C
    return (((w | 0xFFFF8000) << 8 | w) << 8) | w;
}

} // namespace

// 0x004DE5E0
void __cdecl DrawScreenQuad(uint32_t blend, void* target, void* source, float du, float dv, uint32_t colour,
                            bool shader)
{
    bool fogWasOn = g_fogEnabled != 0;
    SetFog(false);
    auto* targetTexture = static_cast<IDirect3DTexture9*>(target);
    SetTarget(targetTexture, nullptr);
    D3DSURFACE_DESC targetDesc;
    targetTexture->GetLevelDesc(0, &targetDesc);
    SetTexture(0, nullptr);
    DeviceCopy()->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    DeviceCopy()->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    SetState(kAddressMode0, 0x10001);
    SetState(kAlphaRef, 0);
    float width = static_cast<float>(Unsigned(targetDesc.Width));
    float height = static_cast<float>(Unsigned(targetDesc.Height));
    SetBlendMode(blend);
    if (source) {
        auto* sourceTexture = static_cast<IDirect3DTexture9*>(source);
        D3DSURFACE_DESC sourceDesc;
        sourceTexture->GetLevelDesc(0, &sourceDesc); // (not used: half a texel of the target)
        float halfX = static_cast<float>(0.5 / Unsigned(targetDesc.Width));
        float halfY = static_cast<float>(0.5 / Unsigned(targetDesc.Height));
        SetTexture(0, source);
        if (shader)
            SetPixelShader(Shader(kPixelShader950), nullptr);
        SetVertexFormat(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        float right = static_cast<float>(static_cast<double>(du) + 1.0);
        float u0 = static_cast<float>(static_cast<double>(halfX) + du);
        float v0 = static_cast<float>(static_cast<double>(halfY) + dv);
        float u1 = static_cast<float>(static_cast<double>(right) + halfX);
        float v1 = static_cast<float>((static_cast<double>(dv) + 1.0) + halfY);
        TexturedVertex quad[4] = {
            {0.0f, 0.0f, 0.0f, 1.0f, colour, u0, v0},
            {width, 0.0f, 0.0f, 1.0f, colour, u1, v0},
            {0.0f, height, 0.0f, 1.0f, colour, u0, v1},
            {width, height, 0.0f, 1.0f, colour, u1, v1},
        };
        DeviceCopy()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TexturedVertex));
    } else {
        SetPixelShader(Shader(kPixelShader94C), nullptr);
        // (the vertex format is left as it was)
        FlatVertex quad[4] = {
            {0.0f, 0.0f, 0.0f, 1.0f, colour},
            {width, 0.0f, 0.0f, 1.0f, colour},
            {0.0f, height, 0.0f, 1.0f, colour},
            {width, height, 0.0f, 1.0f, colour},
        };
        DeviceCopy()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(FlatVertex));
    }
    CountDraw(2);
    SetFog(fogWasOn);
}

// 0x004DEA80
void __cdecl BeginScreenEffects()
{
    ScreenCopyInUse() = false;
    IDirect3DSurface9* surface = nullptr;
    if (g_screenBlur > 2) { // the motion blur
        CurrentScreenCopy()->GetSurfaceLevel(0, &surface);
        // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, game/gfx/graphics_options.h)
        if (!GraphicsOptions::SetSceneTarget(CurrentScreenCopy())) {
            Device()->SetRenderTarget(0, surface);
            Device()->SetDepthStencilSurface(DepthSurface());
        }
    } else {
        if (!g_bloomOn || !g_shadersAvailable) // no bloom (or no pixel shaders)
            return;
        CurrentScreenCopy()->GetSurfaceLevel(0, &surface);
        // (NOT ORIGINAL: or the multisampled scene surface - [Graphics] msaa, game/gfx/graphics_options.h)
        if (!GraphicsOptions::SetSceneTarget(CurrentScreenCopy()))
            SetRenderTargetAndDepth(surface, DepthSurface());
    }
    // The clear colour: the fog's with distance clipping and fog on (0x007D643A, 0x007D6436), else 0x00786CC0.
    uint32_t colour = (g_fogSetting && g_distanceClipping)
                          ? g_fogColour
                          : g_camera.backgroundColour;
    Device()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colour, 1.0f, 0);
    surface->Release();
    ScreenCopyInUse() = true;
}

// 0x004DE980
void __cdecl CopyScreenQuarter()
{
    if (!ScreenCopyInUse())
        return;
    IDirect3DTexture9* source = CurrentScreenCopy();
    IDirect3DTexture9* quarter = ScreenCopy(2);
    // NOT ORIGINAL - [Graphics] msaa (game/gfx/graphics_options.h): the frame so far resolved into the copy first.
    GraphicsOptions::ResolveSceneTarget(source);
    SetState(kDepthBias, 0);
    SetState(kLighting, 0);
    SetState(kZWrite, 0);
    SetState(kZTest, 0);
    SetState(kAlphaTest, 0);
    SetState(kAlphaBlend, 0);
    SetState(kCullMode, D3DCULL_NONE);
    DrawScreenQuad(0, quarter, source, 0.0f, 0.0f, 0x80808080, true);
    SetTarget(CurrentScreenCopy(), DepthSurface());
    SetState(kZWrite, 1);
    SetState(kZTest, 1);
    SetState(kAlphaTest, 1);
    SetState(kAlphaBlend, 1);
}

// 0x004DEB70
void __cdecl Bloom()
{
    if (!g_shadersAvailable)
        return;
    SetState(kZWrite, 0);
    SetState(kZTest, 0);
    SetState(kAlphaTest, 0);
    SetState(kAlphaBlend, 0);
    IDirect3DTexture9* quarter = ScreenCopy(2);
    IDirect3DTexture9* bright = ScreenCopy(3);
    SetPixelShader(Shader(kPixelShader948), nullptr); // the cut-off
    // Constant 0: the cut-off colour (bytes / 128) and 0.5.
    const uint8_t* cutoff = reinterpret_cast<uint8_t*>(&g_bloomCutOff);
    float constant[4] = {static_cast<float>(cutoff[0] * (1.0 / 128)), static_cast<float>(cutoff[1] * (1.0 / 128)),
                         static_cast<float>(cutoff[2] * (1.0 / 128)), 0.5f};
    Device()->SetPixelShaderConstantF(0, constant, 1);
    DrawScreenQuad(0, bright, quarter, 0.0f, 0.0f, 0x80808080, false);
    // Blur: vertically into the eighth, back horizontally; then the same at a sixteenth.
    IDirect3DTexture9* a = ScreenCopy(3);
    IDirect3DTexture9* b = ScreenCopy(4);
    SetState(kAlphaBlend, 1);
    D3DSURFACE_DESC unused;
    b->GetLevelDesc(0, &unused);
    for (int tap = 0; tap < 4; ++tap)
        DrawScreenQuad(tap > 0, b, a, 0.0f, TapOffset(tap), TapColour(TapWeight(tap)), true);
    b = ScreenCopy(4);
    a = ScreenCopy(3);
    for (int tap = 0; tap < 4; ++tap)
        DrawScreenQuad(tap > 0, a, b, TapOffset(tap), 0.0f, TapColour(TapWeight(tap)), true);
    DrawScreenQuad(0, ScreenCopy(7), ScreenCopy(3), 0.0f, 0.0f, 0x80808080, true);
    b = ScreenCopy(8);
    a = ScreenCopy(7);
    b->GetLevelDesc(0, &unused);
    for (int tap = 0; tap < 4; ++tap)
        DrawScreenQuad(tap > 0, b, a, 0.0f, TapOffset(tap), TapColour(TapWeight(tap)), true);
    b = ScreenCopy(8);
    a = ScreenCopy(7);
    for (int tap = 0; tap < 4; ++tap)
        DrawScreenQuad(tap > 0, a, b, TapOffset(tap), 0.0f, TapColour(TapWeight(tap)), true);
    SetTarget(CurrentScreenCopy(), DepthSurface());
    if (!g_blurSecondPass)
        return;
    // Both blurred copies added over the frame.
    SetState(kZWrite, 0);
    SetState(kZTest, 0);
    SetState(kAlphaTest, 0);
    SetState(kAlphaBlend, 1);
    SetState(kAlphaRef, 0);
    SetState(kAddressMode0, 0x10001);
    SetPixelShader(Shader(kPixelShader950), nullptr);
    SetVertexFormat(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    SetBlendMode(1);
    for (int pass = 0; pass < 2; ++pass) {
        IDirect3DTexture9* blurred = pass ? ScreenCopy(7) : ScreenCopy(3);
        D3DSURFACE_DESC desc;
        CurrentScreenCopy()->GetLevelDesc(0, &desc);
        double halfX = 0.5 / Unsigned(desc.Width);
        double halfY = 0.5 / Unsigned(desc.Height);
        uint32_t alpha = pass ? g_bloomAlphaSecond : g_bloomAlphaFirst;
        float halfXFloat = static_cast<float>(halfX), halfYFloat = static_cast<float>(halfY);
        float width = static_cast<float>(g_backBufferWidth);
        float height = static_cast<float>(g_backBufferHeight);
        uint32_t colour = alpha << 24 | 0x808080;
        // (as the original builds them: z 1, the second vertex's u 1, the last vertex half a texel short of the bottom)
        TexturedVertex quad[4] = {
            {0.0f, 0.0f, 1.0f, 1.0f, colour, halfXFloat, halfYFloat},
            {width, 0.0f, 1.0f, 1.0f, colour, 1.0f, halfYFloat},
            {0.0f, height, 1.0f, 1.0f, colour, halfXFloat, static_cast<float>(static_cast<double>(halfYFloat) + 1.0)},
            {width, static_cast<float>(static_cast<double>(height) - halfYFloat), 1.0f, 1.0f, colour,
             static_cast<float>(halfX + 1.0), static_cast<float>(static_cast<double>(halfYFloat) + 1.0)},
        };
        SetTexture(0, nullptr);
        SetTexture(0, blurred);
        SetTexture(1, nullptr);
        SetTexture(2, nullptr);
        SetTexture(3, nullptr);
        DeviceCopy()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TexturedVertex));
        CountDraw(2);
    }
    SetState(kAlphaTest, 1);
    SetState(kAlphaBlend, 1);
    SetState(kZWrite, 1);
    SetState(kZTest, 1);
    for (int stage = 0; stage < 4; ++stage)
        SetTexture(stage, nullptr);
    SetPixelShader(nullptr, nullptr);
}

} // namespace Nx

REPLACE(0x004DE5E0, Nx::DrawScreenQuad);
REPLACE(0x004DEA80, Nx::BeginScreenEffects);
REPLACE(0x004DE980, Nx::CopyScreenQuarter);
REPLACE(0x004DEB70, Nx::Bloom);
