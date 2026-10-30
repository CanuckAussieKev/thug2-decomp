// Texture splats on PC and the projection's geometry helpers: see texture_splats.h. The maths follows the original's
// x87 code: double precision where it keeps values in registers, floats where it stores them.

#include "game/gfx/texture_splats.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine_mesh.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/pc_scene.h"
#include "game/gfx/scene_lights.h"
#include "game/gfx/render_state.h"
#include "game/lst/hashtable.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/device.h"

namespace Nx {

GAME_GLOBAL(sVertexStack, g_vertexStack, 0x006FC4A0);
GAME_GLOBAL(uint8_t[128], g_splatMiddle20, 0x007052C0);
GAME_GLOBAL(uint8_t[128], g_splatMiddle12, 0x00705340);
GAME_GLOBAL(uint8_t[128], g_splatMiddle01, 0x007053C0);
GAME_GLOBAL(uint8_t[128], g_splatVertex0, 0x00705440);
GAME_GLOBAL(uint8_t[128], g_splatVertex1, 0x007054C0);
GAME_GLOBAL(uint8_t[128], g_splatVertex2, 0x00705540);

GAME_GLOBAL(float, g_splitAreaLimit, 0x0067DDF0, 82944.0f);

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }
IDirect3DDevice9* DrawDevice() { return g_drawDevice; } // (the same device, kept twice)
Lst::HashTable* Splats() { return g_splatSets; }

// The table's first value, and the next one (null when there are no more).
sTextureSplats* NextSplats(Lst::HashTable* table)
{
    if (!table->IterateNext() || !table->m_iterItem)
        return nullptr;
    return static_cast<sTextureSplats*>(table->m_iterItem->value);
}
sTextureSplats* FirstSplats(Lst::HashTable* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    return NextSplats(table);
}

// RGBA bytes as ARGB.
uint32_t ToArgb(uint32_t rgba)
{
    uint8_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF, a = rgba >> 24;
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

// The vertex stack (0x006FC4A0: +0 what's on it, +4 a vertex's size; not rewritten yet): popped into / pushed from.
uint8_t* VertexStack() { return reinterpret_cast<uint8_t*>(&g_vertexStack); }
uint32_t VertexSize() { return g_vertexStack.vertexSize; }
void PopVertex(uint8_t* out) { reinterpret_cast<sVertexStack*>(VertexStack())->Pop(out); }
void PushVertex(uint8_t* vertex) { reinterpret_cast<sVertexStack*>(VertexStack())->Push(vertex); }

// A byte channel's midpoint as the original computes it: a + (b - a) / 2, the division rounding towards 0.
uint8_t ByteMidpoint(uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + (static_cast<int>(b) - a) / 2); }
float Midpoint(float a, float b) { return static_cast<float>((static_cast<double>(b) - a) * 0.5 + a); }

} // namespace

// 0x004B7F30
void __cdecl CollapseSplatTriangle(sTextureSplats* splats, int index)
{
    uint8_t(*triangle)[0x18] = splats->vertices[index];
    std::memcpy(triangle[1], triangle[0], 0x18);
    std::memcpy(triangle[2], triangle[0], 0x18);
}

// 0x004B87B0
void __cdecl LightTextureSplats(sTextureSplats* splats)
{
    for (int i = 0; i <= splats->last; ++i) {
        uint8_t(*triangle)[0x18] = splats->vertices[i];
        uint32_t* base = splats->baseColors[i];
        // (the lit colours come back over `colors`, which first holds the base colours)
        uint32_t colors[3];
        std::memcpy(colors, base, 12);
        float points[3][4];
        for (auto& point : points)
            std::memcpy(point, triangle[0], 12);
        LightVertexColours(base, colors, reinterpret_cast<const Mth::Vector*>(points), nullptr, 3);
        for (int k = 0; k < 3; ++k)
            *reinterpret_cast<uint32_t*>(triangle[k] + 0xC) = ToArgb(colors[k]);
    }
}

// 0x004B9200
void __cdecl KillAllTextureSplats()
{
    sTextureSplats* splats = FirstSplats(Splats());
    while (splats) {
        if (uint8_t* material = splats->material) {
            reinterpret_cast<sMaterial*>(material)->DestroyBody(); // (the destructor's body)
            Mem::Manager::Get()->Free(material);
        }
        sTextureSplats* next = NextSplats(Splats());
        uint32_t key = reinterpret_cast<uint32_t>(splats);
        Splats()->FlushItem(key);
        Mem::Manager::Get()->Free(splats);
        splats = next;
    }
}

// 0x004B92A0
void __cdecl KillTextureSplatsInBox(const float* box)
{
    sTextureSplats* splats = FirstSplats(Splats());
    while (splats) {
        sTextureSplats* next = NextSplats(Splats());
        for (int i = 0; i < 0x100; ++i) {
            const float* p = reinterpret_cast<const float*>(splats->vertices[i][0]);
            if (p[0] >= box[0] && p[1] >= box[1] && p[2] >= box[2] && p[0] <= box[4] && p[1] <= box[5] &&
                p[2] <= box[6])
                splats->alive[i] = 0;
        }
        splats = next;
    }
}

// 0x004B9380
void __cdecl DrawTextureSplats()
{
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    Device()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
    DWORD minFilter;
    Device()->GetSamplerState(0, D3DSAMP_MINFILTER, &minFilter);
    Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    for (sTextureSplats* splats = FirstSplats(Splats()); splats; splats = NextSplats(Splats())) {
        if (splats->last < 0)
            continue;
        for (int j = 0; j <= splats->last; ++j) {
            double factor = 1.0;
            if (!(0.0f == splats->correctionA[j] - splats->correctionB[j])) {
                double r = static_cast<double>(splats->correctionA[j]) / splats->correctionB[j];
                double steps = splats->correctionSteps[j];
                double t = (steps - static_cast<int>(splats->alive[j])) / steps;
                factor = 1.0 / (t * (1.0 - r) + r);
            }
            auto* uv = reinterpret_cast<float*>(splats->vertices[0][0] + 0x18 * j + 0x10);
            uv[0] = static_cast<float>((static_cast<double>(splats->uvs[j][0]) - 0.5f) * factor + 0.5f);
            uv[1] = static_cast<float>((static_cast<double>(splats->uvs[j][1]) - 0.5f) * factor + 0.5f);
        }
        reinterpret_cast<sMaterial*>(splats->material)->Submit(); // (the material's set-up)
        SetPixelShader(GameVariable<void*>(0x007CA964), splats->material);
        DrawDevice()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, splats->last + 1, splats->vertices, 0x18);
    }
    Device()->SetSamplerState(0, D3DSAMP_MINFILTER, minFilter);
}

// 0x004B7FA0
bool __cdecl SegmentsIntersect(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy)
{
    // (the stored differences are floats; the rest stays in registers)
    float abx = static_cast<float>(static_cast<double>(bx) - ax);
    float aby = static_cast<float>(static_cast<double>(by) - ay);
    double dcx = static_cast<double>(cx) - dx;
    double dcy = static_cast<double>(cy) - dy;
    float cax = static_cast<float>(static_cast<double>(ax) - cx);
    float cay = static_cast<float>(static_cast<double>(ay) - cy);
    float along = static_cast<float>(cax * dcy - cay * dcx);
    float denominator = static_cast<float>(aby * dcx - dcy * abx);
    double other = static_cast<double>(cay) * abx - static_cast<double>(cax) * aby;
    if (denominator > 0.0f) {
        if (along < 0.0f || along > denominator)
            return false;
        return !(other < 0.0 || other > denominator);
    }
    if (along > 0.0f || along < denominator)
        return false;
    return !(other > 0.0 || other < denominator);
}

// 0x004B88D0
bool __cdecl TriangleOverlapsScreen(float x0, float y0, float x1, float y1, float x2, float y2)
{
    if ((x0 < -1.0f && x1 < -1.0f && x2 < -1.0f) || (x0 > 1.0f && x1 > 1.0f && x2 > 1.0f) ||
        (y0 < -1.0f && y1 < -1.0f && y2 < -1.0f) || (y0 > 1.0f && y1 > 1.0f && y2 > 1.0f))
        return false;
    // the square's edges against the triangle's
    const float corners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    for (int i = 0; i < 4; ++i) {
        const float* a = corners[i];
        const float* b = corners[(i + 1) % 4];
        if (SegmentsIntersect(x0, y0, x1, y1, a[0], a[1], b[0], b[1]) ||
            SegmentsIntersect(x1, y1, x2, y2, a[0], a[1], b[0], b[1]) ||
            SegmentsIntersect(x2, y2, x0, y0, a[0], a[1], b[0], b[1]))
            return true;
    }
    // a vertex inside the square
    const float vertices[3][2] = {{x0, y0}, {x1, y1}, {x2, y2}};
    for (const auto& v : vertices) {
        if (v[0] >= -1.0f && v[0] <= 1.0f && v[1] >= -1.0f && v[1] <= 1.0f)
            return true;
    }
    // a corner inside the triangle: on the inner side of each edge (as the original rounds)
    double e0 = static_cast<double>(x2) - x1, f0 = static_cast<double>(y2) - y1;
    float side0 = static_cast<float>((static_cast<double>(y0) - y1) * e0 - f0 * (static_cast<double>(x0) - x1));
    float e0f = static_cast<float>(e0), f0f = static_cast<float>(f0);
    for (const auto& corner : corners) {
        double px = static_cast<double>(corner[0]) - x1, py = static_cast<double>(corner[1]) - y1;
        if (!((py * e0f - f0f * px) * side0 >= 0.0))
            continue;
        px = static_cast<double>(corner[0]) - x2;
        py = static_cast<double>(corner[1]) - y2;
        double e1 = static_cast<double>(x0) - x2, f1 = static_cast<double>(y0) - y2;
        float e1f = static_cast<float>(e1), f1f = static_cast<float>(f1);
        float g1 = static_cast<float>(static_cast<double>(x1) - x2), h1 = static_cast<float>(static_cast<double>(y1) - y2);
        if (!((py * e1 - f1f * px) * (static_cast<double>(h1) * e1f - f1 * g1) >= 0.0))
            continue;
        px = static_cast<double>(corner[0]) - x0;
        py = static_cast<double>(corner[1]) - y0;
        double e2 = static_cast<double>(x1) - x0, f2 = static_cast<double>(y1) - y0;
        float e2f = static_cast<float>(e2), f2f = static_cast<float>(f2);
        float g2 = static_cast<float>(static_cast<double>(x2) - x0), h2 = static_cast<float>(static_cast<double>(y2) - y0);
        if ((py * e2 - f2f * px) * (static_cast<double>(h2) * e2f - f2 * g2) >= 0.0)
            return true;
    }
    return false;
}

// 0x004B8220
bool __cdecl SplitNextTriangle(uint8_t** out, const sSplitFormat* format)
{
    if (!reinterpret_cast<uint32_t&>(g_vertexStack))
        return false;
    uint8_t* v2 = g_splatVertex2; // (the three popped, last pushed first)
    uint8_t* v1 = g_splatVertex1;
    uint8_t* v0 = g_splatVertex0;
    PopVertex(v2);
    PopVertex(v1);
    PopVertex(v0);
    auto* p0 = reinterpret_cast<float*>(v0);
    auto* p1 = reinterpret_cast<float*>(v1);
    auto* p2 = reinterpret_cast<float*>(v2);
    // the edges from v0 and their cross product (as the original: some parts rounded to floats on the way)
    double ax = static_cast<double>(p1[0]) - p0[0];
    double ay = static_cast<double>(p1[1]) - p0[1];
    double az = static_cast<double>(p1[2]) - p0[2];
    float ayf = static_cast<float>(ay), azf = static_cast<float>(az);
    float bxf = static_cast<float>(static_cast<double>(p2[0]) - p0[0]);
    float byf = static_cast<float>(static_cast<double>(p2[1]) - p0[1]);
    double bz = static_cast<double>(p2[2]) - p0[2];
    float crossX = static_cast<float>(bz * ay - byf * az);
    float crossY = static_cast<float>(az * bxf - bz * ax);
    double crossZ = ax * byf - ay * bxf;
    double lengthSquared = crossZ * crossZ + static_cast<double>(crossY) * crossY + static_cast<double>(crossX) * crossX;
    uint32_t size = VertexSize();
    if (!(lengthSquared > g_splitAreaLimit)) {
        for (uint8_t* v : {v0, v1, v2}) {
            std::memcpy(*out, v, size);
            *out += size;
        }
        return true;
    }
    // the midpoints of the edges: v0-v1, v1-v2, v2-v0
    uint8_t* m01 = g_splatMiddle01;
    uint8_t* m12 = g_splatMiddle12;
    uint8_t* m20 = g_splatMiddle20;
    std::memcpy(m01, v0, size);
    auto* q01 = reinterpret_cast<float*>(m01);
    q01[0] = static_cast<float>(ax * 0.5 + p0[0]);
    std::memcpy(m12, v1, size);
    q01[1] = static_cast<float>(static_cast<double>(ayf) * 0.5 + p0[1]);
    std::memcpy(m20, v2, size);
    q01[2] = static_cast<float>(static_cast<double>(azf) * 0.5 + p0[2]);
    auto* q12 = reinterpret_cast<float*>(m12);
    auto* q20 = reinterpret_cast<float*>(m20);
    for (int k = 0; k < 3; ++k)
        q12[k] = Midpoint(p1[k], p2[k]);
    for (int k = 0; k < 3; ++k)
        q20[k] = Midpoint(p2[k], p0[k]);
    if (uint8_t c = format->colorOffset) {
        for (int k = 0; k < 4; ++k) {
            m01[c + k] = ByteMidpoint(v0[c + k], v1[c + k]);
            m12[c + k] = ByteMidpoint(v1[c + k], v2[c + k]);
            m20[c + k] = ByteMidpoint(v2[c + k], v0[c + k]);
        }
    }
    if (uint8_t t = format->uvOffset) {
        for (int k = 0; k < 2; ++k) {
            auto at = [&](uint8_t* v) { return reinterpret_cast<float*>(v + t) + k; };
            *at(m01) = Midpoint(*at(v0), *at(v1));
            *at(m12) = Midpoint(*at(v1), *at(v2));
            *at(m20) = Midpoint(*at(v2), *at(v0));
        }
    }
    for (uint8_t* v : {v0, m01, m20, m01, v1, m12, m01, m12, m20, m20, m12, v2})
        PushVertex(v);
    return true;
}

namespace {

int Rand() { return GameFunction<int(__cdecl*)()>(0x00623BA3)(); } // (the game's C runtime's)
const float* At(const uint8_t* vertex) { return reinterpret_cast<const float*>(vertex); }

// The x87's sine and cosine (as the original: FSIN / FCOS of the unrounded value).
double Sine(double a)
{
    double result;
    __asm {
        fld a
        fsin
        fstp result
    }
    return result;
}
double Cosine(double a)
{
    double result;
    __asm {
        fld a
        fcos
        fstp result
    }
    return result;
}

// D3DX (linked library code).
void TransformCoord(float* out, const float* v, const Mth::Matrix* m)
{
    GameFunction<void*(__stdcall*)(float*, const float*, const Mth::Matrix*)>(0x005BF2F5)(out, v, m);
}
void Normalize3(float* v) { GameFunction<void*(__stdcall*)(float*, const float*)>(0x005BEF2D)(v, v); }
void LookAt(Mth::Matrix* out, const float* eye, const float* at, const float* up)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const float*, const float*, const float*)>(0x005C0985)(out, eye, at, up);
}
void Ortho(Mth::Matrix* out, float width, float height, float nearZ, float farZ)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, float, float, float, float)>(0x005C0B39)(out, width, height, nearZ,
                                                                                         farZ);
}
void Multiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

// A set's oldest triangle (a free one first: 0x00490CD0, the engine's).
int OldestTriangle(sTextureSplats* splats)
{
    return reinterpret_cast<sSplatSlots*>(splats)->OldestSlot(0x100);
}
// The lifetime in milliseconds (the C runtime's _ftol: truncated).
int Milliseconds(float seconds) { return static_cast<int>(static_cast<double>(seconds) * 1000.0f); }
// A vertex colour as the splats keep it: opaque-ish (alpha 0x80).
uint32_t SplatColor(uint32_t color) { return (color & 0xFFFFFF) | 0x80000000; }

// The splat vertex layout: position, colour (+0x0C), u, v (+0x10).
float* Uv(uint8_t* vertex) { return reinterpret_cast<float*>(vertex + 0x10); }
uint32_t& Color(uint8_t* vertex) { return *reinterpret_cast<uint32_t*>(vertex + 0xC); }

} // namespace

// 0x004B9A60
sTextureSplats* __cdecl GetTextureSplats(uint32_t texture)
{
    for (sTextureSplats* splats = FirstSplats(Splats()); splats; splats = NextSplats(Splats())) {
        if (*reinterpret_cast<uint32_t*>(splats->material + 0x30) == texture)
            return splats;
    }
    void* memory = Mem::New(0x110);
    auto* material = memory ? reinterpret_cast<uint8_t*>(static_cast<sMaterial*>(memory)->Construct()) : nullptr;
    // (no check: a failed allocation writes through null, as the original)
    *reinterpret_cast<uint32_t*>(material + 0x20) = texture ? 4 : 0; // (the pass's flags)
    int a = Rand();
    int b = Rand();
    *reinterpret_cast<int*>(material) = a * b; // a random checksum
    *reinterpret_cast<uint32_t*>(material + 0x30) = texture;
    *reinterpret_cast<uint32_t*>(material + 0x08) = 1; // one pass
    material[0x0D] = 1;
    material[0x11] = 0x14;
    *reinterpret_cast<uint32_t*>(material + 0x80) = 5; // its blend mode
    for (int k = 0; k < 3; ++k)
        reinterpret_cast<float*>(material + 0x40)[k] = 0.5f; // its colour
    *reinterpret_cast<uint32_t*>(material + 0x90) = 0x20002;
    *reinterpret_cast<uint32_t*>(material + 0xE0) = 0;
    material[0x10] = 1;
    auto* splats = static_cast<sTextureSplats*>(Mem::New(sizeof(sTextureSplats)));
    splats->last = 0;
    splats->material = material;
    std::memset(splats->alive, 0, sizeof(splats->alive));
    for (int v = 0; v < 0x300; ++v) {
        uint8_t* vertex = splats->vertices[0][0] + 0x18 * v;
        std::memset(vertex, 0, 12);
        Color(vertex) = 0x80808080;
    }
    uint32_t key = reinterpret_cast<uint32_t>(splats);
    Splats()->PutItem(key, splats);
    return splats;
}

// 0x004B9C30
sTextureSplats* __cdecl CreateTextureSplat(void** sectors, int /*unused*/, sSplatParams* params)
{
    float* start = params->start;
    float* end = params->end;
    // the ray's direction (normalised when it has a length; the parts stored are floats) and length
    double dz = static_cast<double>(end[2]) - start[2];
    float dyf = static_cast<float>(static_cast<double>(end[1]) - start[1]);
    float dxf = static_cast<float>(static_cast<double>(end[0]) - start[0]);
    float direction[3] = {dxf, dyf, static_cast<float>(dz)};
    double lengthSquared = static_cast<double>(dxf) * dxf + static_cast<double>(dyf) * dyf + dz * dz;
    float length = static_cast<float>(std::sqrt(lengthSquared));
    double norm = std::sqrt(lengthSquared);
    if (norm > 0.0) {
        double inverse = 1.0 / norm;
        direction[0] = static_cast<float>(dxf * inverse);
        direction[1] = static_cast<float>(dyf * inverse);
        direction[2] = static_cast<float>(dz * inverse);
    }
    float cone = static_cast<float>(Cosine(params->coneAngle));
    // the projection: looking along the ray, the texture's up from `up` (the ray moved by it, and the height its
    // distance), else about y (turned by the angle - or a random one - when the ray is steep)
    float up[4];
    Mth::Matrix view, projection, combined;
    if (params->up) {
        const float* from = params->up;
        double az = static_cast<double>(start[2]) - from[2];
        double ay = static_cast<double>(start[1]) - from[1];
        up[0] = static_cast<float>(static_cast<double>(start[0]) - from[0]);
        up[1] = static_cast<float>(ay);
        up[2] = static_cast<float>(az);
        up[3] = 1.0f;
        float height = static_cast<float>(std::sqrt(static_cast<double>(up[0]) * up[0] + ay * ay + az * az));
        start[0] = static_cast<float>(static_cast<double>(start[0]) - up[0]);
        start[1] = static_cast<float>(static_cast<double>(start[1]) - ay);
        start[2] = static_cast<float>(static_cast<double>(start[2]) - az);
        start[3] = static_cast<float>(static_cast<double>(start[3]) - 1.0f);
        end[0] = static_cast<float>(static_cast<double>(end[0]) - up[0]);
        end[1] = static_cast<float>(static_cast<double>(end[1]) - ay);
        end[2] = static_cast<float>(static_cast<double>(end[2]) - az);
        end[3] = static_cast<float>(static_cast<double>(end[3]) - 1.0f);
        reinterpret_cast<Mth::Vector*>(up)->Normalize();
        LookAt(&view, start, end, up);
        Ortho(&projection, params->size, height, 0.1f, length);
    } else {
        if (std::fabs(direction[1]) > 0.5f) {
            double a = params->angle;
            if (0.0f == params->angle)
                a = Rand() * static_cast<double>(std::bit_cast<float>(0x40C90FDBu)) *
                    std::bit_cast<float>(0x38000100u); // (2 pi, 1 / 32767)
            up[0] = static_cast<float>(Sine(a));
            up[1] = 0.0f;
            up[2] = static_cast<float>(Cosine(a));
        } else {
            up[0] = 0.0f;
            up[1] = 1.0f;
            up[2] = 0.0f;
        }
        up[3] = 1.0f;
        LookAt(&view, start, end, up);
        Ortho(&projection, params->size, params->size, 0.1f, length);
    }
    Multiply(&combined, &view, &projection);

    sTextureSplats* splats = nullptr;
    uint8_t* splatVertices = nullptr;
    int slot = 0;
    for (; *sectors; ++sectors) {
        auto* geom = static_cast<uint8_t*>(static_cast<CPcSector*>(*sectors)->geom); // (the sector's geom)
        if (!geom)
            continue;
        auto** meshes = *reinterpret_cast<sMesh***>(geom + 0x3C);
        uint32_t meshCount = *reinterpret_cast<uint32_t*>(geom + 0x40);
        for (uint32_t m = 0; m < meshCount; ++m) {
            sMesh* mesh = meshes[m];
            if (mesh->flags & 2)
                continue;
            uint8_t* vertices;
            mesh->vertexBuffers[0]->Lock(0, 0, &vertices, 0x10);
            uint16_t* indices;
            mesh->indexBuffers[0]->Lock(0, 0, &indices, 0x10);
            uint32_t a = indices[0], b = indices[1];
            for (uint32_t strip = 2; strip < mesh->indexCounts[0]; ++strip) {
                uint32_t c = indices[strip];
                uint32_t ia = a, ib = b, ic = c;
                a = b;
                b = c;
                if (ia == ib || ia == ic || ib == ic)
                    continue;
                uint32_t stride = mesh->stride;
                float t0[3], t1[3], t2[3];
                TransformCoord(t0, At(vertices + stride * ia), &combined);
                TransformCoord(t1, At(vertices + stride * ib), &combined);
                TransformCoord(t2, At(vertices + stride * ic), &combined);
                if ((t0[2] < -1.0f && t1[2] < -1.0f && t2[2] < -1.0f) || (t0[2] > 1.0f && t1[2] > 1.0f && t2[2] > 1.0f))
                    continue;
                if (!TriangleOverlapsScreen(t0[0], t0[1], t1[0], t1[1], t2[0], t2[1]))
                    continue;
                if (!splatVertices) {
                    uint8_t* holder = params->texture;
                    uint8_t* key = *reinterpret_cast<uint8_t**>(holder + 0x10);
                    if (holder[8] & 1)
                        key = *reinterpret_cast<uint8_t**>(key);
                    splats = GetTextureSplats(reinterpret_cast<uint32_t>(key));
                    splatVertices = splats->vertices[0][0];
                }
                slot = OldestTriangle(splats);
                uint8_t* triangle = splatVertices + slot * 0x48;
                uint8_t* v0 = triangle;
                uint8_t* v1 = triangle + 0x18;
                uint8_t* v2 = triangle + 0x30;
                std::memcpy(v0, vertices + stride * ia, 12);
                std::memcpy(v1, vertices + stride * ib, 12);
                std::memcpy(v2, vertices + stride * ic, 12);
                // the face's normal against the ray (as the original rounds it)
                {
                    const float* p0 = At(v0);
                    const float* p1 = At(v1);
                    const float* p2 = At(v2);
                    double ez2 = static_cast<double>(p2[2]) - p0[2], ey2 = static_cast<double>(p2[1]) - p0[1];
                    double ex2 = static_cast<double>(p2[0]) - p0[0], ez1 = static_cast<double>(p1[2]) - p0[2];
                    float ey1f = static_cast<float>(static_cast<double>(p1[1]) - p0[1]);
                    float ex1f = static_cast<float>(static_cast<double>(p1[0]) - p0[0]);
                    float normal[3] = {static_cast<float>(ez1 * ey2 - ey1f * ez2),
                                       static_cast<float>(ex1f * ez2 - ez1 * ex2),
                                       static_cast<float>(ey1f * ex2 - ex1f * ey2)};
                    Normalize3(normal);
                    double facing = static_cast<double>(normal[2]) * direction[2] +
                                    static_cast<double>(normal[1]) * direction[1] +
                                    static_cast<double>(normal[0]) * direction[0];
                    double absolute = std::fabs(facing);
                    if (absolute < cone || absolute == cone)
                        continue;
                }
                splats->alive[slot] = params->lifetime < 0.0f || params->lifetime != params->lifetime
                                          ? 0x7FFFFFFF
                                          : Milliseconds(params->lifetime);
                // texture coordinates from the projection (0-1 over the square)
                const float* t[3] = {t0, t1, t2};
                uint8_t* v[3] = {v0, v1, v2};
                for (int k = 0; k < 3; ++k) {
                    Uv(v[k])[0] = static_cast<float>((static_cast<double>(t[k][0]) + 1.0f) * 0.5f);
                    Uv(v[k])[1] = static_cast<float>((static_cast<double>(t[k][1]) + 1.0f) * 0.5f);
                }
                for (int k = 0; k < 3; ++k) {
                    splats->uvs[3 * slot + k][0] = Uv(v[k])[0];
                    splats->uvs[3 * slot + k][1] = Uv(v[k])[1];
                }
                const uint32_t index[3] = {ia, ib, ic};
                for (int k = 0; k < 3; ++k)
                    Color(v[k]) = SplatColor(*reinterpret_cast<uint32_t*>(vertices + stride * index[k] +
                                                                          mesh->unknown1C[0]));
                // how fine to split it: a 128th of its cross product's square
                {
                    const float* p0 = At(v0);
                    const float* p1 = At(v1);
                    const float* p2 = At(v2);
                    double ez1 = static_cast<double>(p1[2]) - p0[2], ey1 = static_cast<double>(p1[1]) - p0[1];
                    double ex1 = static_cast<double>(p1[0]) - p0[0];
                    float ez2f = static_cast<float>(static_cast<double>(p2[2]) - p0[2]);
                    float ey2f = static_cast<float>(static_cast<double>(p2[1]) - p0[1]);
                    double ex2 = static_cast<double>(p2[0]) - p0[0];
                    float cz = static_cast<float>(ey2f * ex1 - ex2 * ey1);
                    float cy = static_cast<float>(ex2 * ez1 - ez2f * ex1);
                    double cx = ez2f * ey1 - ey2f * ez1;
                    g_splitAreaLimit = static_cast<float>(
                        (cx * cx + static_cast<double>(cy) * cy + static_cast<double>(cz) * cz) * (1.0 / 128.0));
                }
                reinterpret_cast<sVertexStack*>(VertexStack())->Construct(); // (emptied)
                g_vertexStack.vertexSize = 0x18;
                PushVertex(v0);
                PushVertex(v1);
                PushVertex(v2);
                auto* pieces = static_cast<uint8_t*>(Mem::New(0x2000));
                std::memset(pieces, 0, 0x2000);
                void* memory = Mem::New(0xD8);
                auto* format = memory ? reinterpret_cast<uint8_t*>(static_cast<sMesh*>(memory)->Construct()) : nullptr;
                format[0x1C] = 0x0C;
                format[0x1E] = 0x10;
                uint8_t* cursor = pieces;
                while (SplitNextTriangle(&cursor, reinterpret_cast<const sSplitFormat*>(format)))
                    ;
                // the pieces inside the projection, each into a triangle of the set
                bool kept = false;
                for (uint8_t* piece = pieces; piece != cursor; piece += 0x48) {
                    auto twice = [&](int offset) {
                        double value = *reinterpret_cast<const float*>(piece + offset);
                        return static_cast<float>(value + value - 1.0f);
                    };
                    if (!TriangleOverlapsScreen(twice(0x10), twice(0x14), twice(0x28), twice(0x2C), twice(0x40),
                                                twice(0x44)))
                        continue;
                    kept = true;
                    splats->alive[slot] = Milliseconds(params->lifetime);
                    splats->correctionSteps[slot] = Milliseconds(params->lifetime);
                    uint8_t* into = splatVertices + slot * 0x48;
                    for (int k = 0; k < 3; ++k) {
                        std::memcpy(into + 0x18 * k, piece + 0x18 * k, 12);
                        std::memcpy(Uv(into + 0x18 * k), Uv(piece + 0x18 * k), 8);
                    }
                    for (int k = 0; k < 3; ++k) {
                        splats->uvs[3 * slot + k][0] = Uv(into + 0x18 * k)[0];
                        splats->uvs[3 * slot + k][1] = Uv(into + 0x18 * k)[1];
                    }
                    for (int k = 0; k < 3; ++k)
                        Color(into + 0x18 * k) = SplatColor(Color(piece + 0x18 * k));
                    // the base colours: grey from each vertex colour's first byte
                    for (int k = 0; k < 3; ++k) {
                        uint8_t grey = into[0x18 * k + 0xC];
                        splats->baseColors[slot][k] = 0x80000000u | (grey << 16) | (grey << 8) | grey;
                    }
                    splats->correctionA[slot] = params->depth;
                    splats->correctionB[slot] = params->size;
                    slot = OldestTriangle(splats);
                }
                if (!kept) {
                    splats->alive[slot] = 0;
                    CollapseSplatTriangle(splats, slot);
                }
                reinterpret_cast<sMesh*>(format)->DestroyBody(); // (the format mesh's destructor body)
                Mem::Manager::Get()->Free(format);
                Mem::Manager::Get()->Free(pieces);
            }
        }
    }
    return splats;
}

} // namespace Nx

REPLACE(0x004B9A60, Nx::GetTextureSplats);
REPLACE(0x004B9C30, Nx::CreateTextureSplat);
REPLACE(0x004B7F30, Nx::CollapseSplatTriangle);
REPLACE(0x004B87B0, Nx::LightTextureSplats);
REPLACE(0x004B9200, Nx::KillAllTextureSplats);
REPLACE(0x004B92A0, Nx::KillTextureSplatsInBox);
REPLACE(0x004B9380, Nx::DrawTextureSplats);
REPLACE(0x004B7FA0, Nx::SegmentsIntersect);
REPLACE(0x004B88D0, Nx::TriangleOverlapsScreen);
REPLACE_SOURCE_ONLY(0x004B8E30, Nx::TriangleOverlapsScreen); // a jump to 0x004B88D0
REPLACE(0x004B8220, Nx::SplitNextTriangle);
REPLACE_SOURCE_ONLY(0x004B8232, Nx::SplitNextTriangle); // (the rest of 0x004B8220, Ghidra's split)
