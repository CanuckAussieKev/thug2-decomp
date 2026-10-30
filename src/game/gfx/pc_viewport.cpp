// The PC viewport: see pc_viewport.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include "game/gfx/pc_viewport.h"

#include <windows.h>

#include <d3d9.h>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/gfx/viewport.h"
#include "game/sys/platform_d3d.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/device.h"
#include "game/gfx/glow.h"

namespace Nx {

GAME_GLOBAL(uint8_t[1024], g_occlusionSlots, 0x0067E550, 1);
GAME_GLOBAL(uint32_t, g_occlusionPixels, 0x00701384);
GAME_GLOBAL(float[20], g_queryRectStrip, 0x007642A0);
GAME_GLOBAL(uint32_t[16], g_queryPointStrip, 0x007642F0);

GAME_GLOBAL(float, g_virtualScreenHeight, 0x00649FCC, 448.0f);
GAME_GLOBAL(float, g_virtualScreenWidth, 0x00649FD4, 640.0f);
GAME_GLOBAL(float, g_scale2DIn3DNumerator, 0x0067E548, 300.0f);
GAME_GLOBAL(float, g_scale2DIn3DMax, 0x0067E54C, 3.0f);
GAME_GLOBAL(float, g_scale2DIn3DMin, 0x0070137C);

namespace {

constexpr uint32_t kViewportVtable = 0x0064A848;

IDirect3DDevice9* Device() { return g_d3dDevice; }
bool Radeon8500() { return g_radeon8500; }

void Transform(float* out, const float* v, const void* matrix)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const void*)>(0x005BF833)(
        out, v, matrix); // (D3DXVec4Transform)
}

// The queries' states: colour writes off, no z writes, no texture, the vertex colour's shader.
void BeginQueryDraw(uint32_t fvf)
{
    SetBlendMode(0);
    SetTexture(0, nullptr);
    SetPixelShader(g_vertexColourPixelShader, nullptr);
    SetVertexFormat(fvf);
    Platform::BeginOcclusionQuery();
}
void EndQueryDraw(uint8_t query)
{
    Platform::EndOcclusionQuery(query);
    Device()->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    SetState(kZWrite, 1);
}

} // namespace

// 0x004CB860
CPcViewport* __cdecl CreateViewport(uint32_t a1, uint32_t a2)
{
    void* memory = Mem::New(sizeof(CPcViewport));
    return memory ? static_cast<CPcViewport*>(memory)->Construct(a1, a2) : nullptr;
}

// 0x004CB8E0
CPcViewport* CPcViewport::Construct(uint32_t a1, uint32_t a2)
{
    reinterpret_cast<CViewport*>(this)->CViewport::Construct(reinterpret_cast<const float*>(static_cast<uintptr_t>(a1)),
                                                              reinterpret_cast<Gfx::Camera*>(static_cast<uintptr_t>(a2)));
    vtable = reinterpret_cast<void**>(kViewportVtable);
    return this;
}

// 0x004CB900
void CPcViewport::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kViewportVtable);
    reinterpret_cast<CViewport*>(this)->CViewport::DestroyBody();
}

// 0x004CB910
CPcViewport* CPcViewport::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004CB940
float CPcViewport::ProjectToScreen(const Mth::Vector* point, float* x, float* y, int* depth)
{
    float v[4] = {point->x, point->y, point->z, 1.0f};
    float view[4], clip[4];
    Transform(view, v, &g_viewMatrix);
    Transform(clip, view, &g_projectionMatrix);
    if (clip[3] != 0.0f) {
        double inverse = 1.0f / static_cast<double>(clip[3]);
        clip[0] = static_cast<float>(clip[0] * inverse);
        clip[1] = static_cast<float>(clip[1] * inverse);
        clip[2] = static_cast<float>(inverse * clip[2]);
    }
    *x = static_cast<float>((static_cast<double>(clip[0]) + 1.0f) * 0.5f * g_virtualScreenWidth);
    *y = static_cast<float>((1.0f - static_cast<double>(clip[1])) * 0.5f * g_virtualScreenHeight);
    if (!(clip[2] < 1.0f))
        return -1.0f;
    float most = g_scale2DIn3DMax, least = g_scale2DIn3DMin;
    double factor = view[2] == 0.0f ? most : -1.0f / static_cast<double>(view[2]) * g_scale2DIn3DNumerator;
    if (factor > most)
        factor = most;
    else if (factor < least)
        factor = least;
    *depth = static_cast<int>(static_cast<double>(clip[2]) * 65536.0f);
    return static_cast<float>(factor);
}

// 0x004CBC00
uint8_t CPcViewport::QueryPoint(const Mth::Vector* point, int /*unused*/, uint8_t query)
{
    if (Radeon8500() || !query)
        return 0;
    SetState(kZWrite, 0);
    Device()->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    BeginQueryDraw(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    auto* strip = g_queryPointStrip; // (four vertices: position, colour)
    for (int k = 0; k < 4; ++k) {
        strip[4 * k] = reinterpret_cast<const uint32_t*>(point)[0];
        strip[4 * k + 1] = reinterpret_cast<const uint32_t*>(point)[1];
        strip[4 * k + 2] = reinterpret_cast<const uint32_t*>(point)[2];
        strip[4 * k + 3] = 0xFFFFFFFF;
    }
    Device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, strip, 0x10);
    EndQueryDraw(query);
    return query;
}

// 0x004CBA90
uint8_t CPcViewport::QueryRect(float x0, float y0, float x1, float y1, uint32_t z, uint8_t query)
{
    if (Radeon8500() || !query)
        return 0;
    Device()->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    SetState(kZWrite, 0);
    BeginQueryDraw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    struct Vertex {
        float x, y, z, rhw;
        uint32_t colour;
    };
    auto* strip = reinterpret_cast<Vertex*>(g_queryRectStrip);
    float depth = static_cast<float>(static_cast<double>(z));
    strip[0] = {x0, y0, depth, 1.0f, 0xFFFFFFFF};
    strip[1] = {x0, y1, depth, 1.0f, 0xFFFFFFFF};
    strip[2] = {x1, y0, depth, 1.0f, 0xFFFFFFFF};
    strip[3] = {x1, y1, depth, 1.0f, 0xFFFFFFFF};
    Device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, strip, 0x14);
    EndQueryDraw(query);
    return query;
}

// 0x004CBD40
uint32_t __cdecl OcclusionPixels()
{
    if (Radeon8500())
        return 0;
    DWORD pixels;
    HRESULT result = Platform::GetOcclusionResult(g_occlusionRequests[0].slot, &pixels);
    if (result == S_OK) {
        g_occlusionSlots[g_occlusionRequests[0].slot] = 0;
        PopOcclusionRequest(); // (the requests moved on)
        g_occlusionPixels = pixels;
        return pixels;
    }
    return result == S_FALSE ? g_occlusionPixels : 0;
}

} // namespace Nx

REPLACE(0x004CB860, Nx::CreateViewport);
REPLACE(0x004CB8E0, &Nx::CPcViewport::Construct);
REPLACE(0x004CB900, &Nx::CPcViewport::DestroyBody);
REPLACE(0x004CB910, &Nx::CPcViewport::Destroy);
REPLACE(0x004CB940, &Nx::CPcViewport::ProjectToScreen);
REPLACE(0x004CBC00, &Nx::CPcViewport::QueryPoint);
REPLACE(0x004CBA90, &Nx::CPcViewport::QueryRect);
REPLACE(0x004CBD40, Nx::OcclusionPixels);
