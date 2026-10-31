// Light glows (Nx engine, PC): see glow.h.

#include "game/gfx/device.h"
#include "game/gfx/glow.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>

#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/lst/hashtable.h"
#include "game/sys/platform_d3d.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/shader_slots.h"
#include "game/sys/startup_init.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }
GAME_GLOBAL(Lst::HashTable, g_glowTable, 0x007CA9D8, Lst::HashTable::Unconstructed{}); // (the exe makes it with HashTable(8) at start-up)
Lst::HashTable& Glows() { return g_glowTable; }
GAME_GLOBAL(uint8_t[0x400], g_glowQueriesInUse, 0x007BA520); // (a byte per query: DrawGlows looks through 0x400)
GAME_GLOBAL(float[16], g_glowQuad, 0x007CA9F0);

// The occlusion queries in use (a byte each).
uint8_t* QueriesInUse() { return g_glowQueriesInUse; }
// The quad worked out for the last glow (four positions and colours; nothing draws it).
float* GlowQuad() { return g_glowQuad; }

// The number of pixels a query saw: 0 when it has the answer, 1 when not yet.
int QueryResult(uint32_t query, uint32_t* pixels)
{
    return Platform::GetOcclusionResult(query, reinterpret_cast<DWORD*>(pixels));
}

sGlow* NextGlow()
{
    Lst::HashTable& table = Glows();
    if (!table.IterateNext() || !table.m_iterItem)
        return nullptr;
    return static_cast<sGlow*>(table.m_iterItem->value);
}

// The glow's quad of half-size `size` facing the camera (across `u`, up `v`).
void SetQuad(const sGlow* glow, const float* u, const float* v, float size, uint32_t colour)
{
    float* q = GlowQuad();
    auto* colours = reinterpret_cast<uint32_t*>(q);
    for (int i = 0; i < 3; ++i) {
        double across = static_cast<double>(u[i]) * size;
        double up = static_cast<double>(v[i]) * size;
        q[i] = static_cast<float>((glow->position[i] - across) - up);
        q[4 + i] = static_cast<float>((glow->position[i] - across) + up);
        q[8 + i] = static_cast<float>((up + across) + glow->position[i]);
        q[12 + i] = static_cast<float>((across + glow->position[i]) - up);
    }
    colours[3] = colour;
    colours[7] = colour;
    colours[11] = colour;
    colours[15] = colour;
}

} // namespace

// 0x004DCC70
void __cdecl UpdateGlows(bool queries)
{
    // The camera's across and up directions from the view matrix's third column (a, b, c): u = (b*0 - c, c*0 - a*0,
    // a - b*0) and v = u x (a, b, c), each then normalised.
    double a = g_viewMatrix.row[0].z;
    double b = g_viewMatrix.row[1].z;
    double c = g_viewMatrix.row[2].z;
    float u[3], v[3];
    u[0] = static_cast<float>(b * 0.0f - c);
    u[1] = static_cast<float>(c * 0.0f - a * 0.0f);
    u[2] = static_cast<float>(a - b * 0.0f);
    v[0] = static_cast<float>(u[1] * c - u[2] * b);
    v[1] = static_cast<float>(u[2] * a - c * u[0]);
    v[2] = static_cast<float>(b * u[0] - u[1] * a);
    for (float* w : {u, v}) {
        double length = std::sqrt((static_cast<double>(w[2]) * w[2] + static_cast<double>(w[1]) * w[1]) +
                                  static_cast<double>(w[0]) * w[0]);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            for (int i = 0; i < 3; ++i)
                w[i] = static_cast<float>(w[i] * inverse);
        }
    }
    Lst::HashTable& table = Glows();
    table.m_iterBucket = -1;
    table.m_iterItem = nullptr;
    sGlow* glow = NextGlow();

    if (queries) {
        // Each glow gets an occlusion query (up to three outstanding).
        Device()->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        SetBlendMode(0);
        SetTexture(0, nullptr);
        SetPixelShader(nullptr, nullptr);
        Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        Device()->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        for (; glow; glow = NextGlow()) {
            if (glow->queryCount >= 3)
                continue;
            uint32_t query = 0;
            for (; query < 0x400; ++query) {
                if (!QueriesInUse()[query]) {
                    QueriesInUse()[query] = 1;
                    break;
                }
            }
            if (query == 0x400)
                query = 0x3FF; // (none free: the last, not marked)
            glow->queries[glow->queryCount++] = query;
            if (query > 0)
                SetQuad(glow, u, v, glow->querySize, 0xFFFFFFFF);
        }
        Device()->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        Device()->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        return;
    }

    // Each glow grows towards its size while its queries see it, and fades otherwise.
    SetBlendMode(5);
    SetPixelShader(g_pixelShaders[kPixelShader94C], nullptr);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    for (; glow; glow = NextGlow()) {
        bool seen = false;
        if (!g_radeon8500) { // (not on a Radeon 8500)
            uint32_t pixels;
            int result = QueryResult(glow->queries[0], &pixels);
            if (result == 0) {
                QueriesInUse()[glow->queries[0]] = 0;
                glow->queries[0] = glow->queries[1];
                glow->queries[1] = glow->queries[2];
                glow->queries[2] = glow->queries[3];
                --glow->queryCount;
                glow->queries[3] = 0;
                glow->pixels = pixels;
                seen = pixels != 0;
            } else if (result == 1) {
                seen = glow->pixels != 0;
            }
        }
        if (seen)
            glow->size = static_cast<float>(
                (static_cast<double>(glow->maxSize) - glow->size) * glow->rate + glow->size);
        else
            glow->size = static_cast<float>(glow->size - static_cast<double>(glow->rate) * glow->size);
        if (glow->size > 0.0f)
            SetQuad(glow, u, v, glow->size, 0x40FFFFFF);
    }
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

} // namespace Nx

REPLACE(0x004DCC70, Nx::UpdateGlows);

namespace Nx {

// 0x00640E10 (a static initialiser): the glows' table, 256 buckets.
void ConstructGlowTable() { new (&g_glowTable) Lst::HashTable(8); }

} // namespace Nx
