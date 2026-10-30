// The PC viewport: see pc_viewport.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include "game/gfx/pc_viewport.h"

#include <windows.h>

#include <d3d9.h>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/render_state.h"
#include "game/gfx/viewport.h"
#include "game/sys/platform_d3d.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kViewportVtable = 0x0064A848;

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
bool Radeon8500() { return GameVariable<bool>(0x00786AB5); }

void Transform(float* out, const float* v, uint32_t matrix)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const void*)>(0x005BF833)(
        out, v, &GameVariable<uint8_t>(matrix)); // (D3DXVec4Transform)
}

// The queries' states: colour writes off, no z writes, no texture, the vertex colour's shader.
void BeginQueryDraw(uint32_t fvf)
{
    SetBlendMode(0);
    SetTexture(0, nullptr);
    SetPixelShader(GameVariable<void*>(0x007CA94C), nullptr);
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
    Transform(view, v, 0x00786BD0);
    Transform(clip, view, 0x00786C10);
    if (clip[3] != 0.0f) {
        double inverse = 1.0f / static_cast<double>(clip[3]);
        clip[0] = static_cast<float>(clip[0] * inverse);
        clip[1] = static_cast<float>(clip[1] * inverse);
        clip[2] = static_cast<float>(inverse * clip[2]);
    }
    *x = static_cast<float>((static_cast<double>(clip[0]) + 1.0f) * 0.5f * GameVariable<float>(0x00649FD4));
    *y = static_cast<float>((1.0f - static_cast<double>(clip[1])) * 0.5f * GameVariable<float>(0x00649FCC));
    if (!(clip[2] < 1.0f))
        return -1.0f;
    float most = GameVariable<float>(0x0067E54C), least = GameVariable<float>(0x0070137C);
    double factor = view[2] == 0.0f ? most : -1.0f / static_cast<double>(view[2]) * GameVariable<float>(0x0067E548);
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
    auto* strip = &GameVariable<uint32_t>(0x007642F0); // (four vertices: position, colour)
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
    auto* strip = &GameVariable<Vertex>(0x007642A0);
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
    HRESULT result = Platform::GetOcclusionResult(GameVariable<uint32_t>(0x00701390), &pixels);
    if (result == S_OK) {
        GameVariable<uint8_t>(0x0067E550 + GameVariable<uint32_t>(0x00701390)) = 0;
        PopOcclusionRequest(); // (the requests moved on)
        GameVariable<uint32_t>(0x00701384) = pixels;
        return pixels;
    }
    return result == S_FALSE ? GameVariable<uint32_t>(0x00701384) : 0;
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
