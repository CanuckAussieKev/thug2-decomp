// The PC flexible particle systems: see pc_flexible_particles.h.

#include "game/gfx/pc_flexible_particles.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/material.h"
#include "game/gfx/particle_system.h"
#include "game/gfx/particle_types.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "game/mth/x87_math.h"
#include "game/sys/platform_d3d.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
IDirect3DIndexBuffer9* QuadIndices() { return GameVariable<IDirect3DIndexBuffer9*>(0x007D69E8); }
void* DefaultPixelShader() { return GameVariable<void*>(0x007CA964); }
// The frame's draw counts: draws, triangles.
uint32_t& DrawCalls() { return GameVariable<uint32_t>(0x00786EBC); }
uint32_t& DrawnTriangles() { return GameVariable<uint32_t>(0x00786EC0); }

// _ftol: truncated to a 64-bit integer, its low half.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

void SetMaterial(uint8_t* material) { reinterpret_cast<sMaterial*>(material)->Submit(); } // (its set-up)

// The camera's across and up directions from the view matrix's third column (a, b, c): r = (a, b, c) x (0, 1, 0)
// and u = r x (a, b, c), then normalised (Mth::Vector::Normalize).
void CameraAxes(Mth::Vector& r, Mth::Vector& u)
{
    double a = GameVariable<float>(0x00786BD8);
    double b = GameVariable<float>(0x00786BE8);
    double c = GameVariable<float>(0x00786BF8);
    double b0 = b * 0.0f;
    r.x = static_cast<float>(b0 - c);
    r.y = static_cast<float>(c * 0.0f - a * 0.0f);
    r.z = static_cast<float>(a - b0);
    r.w = 0.0f;
    u.x = static_cast<float>(r.y * c - r.z * b);
    u.y = static_cast<float>(r.z * a - c * r.x);
    u.z = static_cast<float>(b * r.x - r.y * a);
    u.w = 0.0f;
    r.Normalize();
    u.Normalize();
}

// A quad's vertex: position, colour, texture coordinates.
struct sQuadVertex {
    float x, y, z;
    uint32_t colour;
    float u, v;
};
static_assert(sizeof(sQuadVertex) == 0x18, "layout must match the original");

uint8_t ClampedByte(double value)
{
    if (value < 0.0f)
        value = 0.0f;
    else if (value > 255.0f)
        value = 255.0f;
    return static_cast<uint8_t>(Truncate(value));
}

} // namespace

// 0x004BD470
uint32_t* CPcFlexibleParticles::LerpColour(uint32_t* out, const uint8_t* from, const uint8_t* to, float t)
{
    double red = static_cast<double>(colourScale[0]) * brightness;
    double green = static_cast<double>(colourScale[1]) * brightness;
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(from[k] + Truncate(static_cast<double>(to[k] - from[k]) * t));
    };
    uint8_t b = lerp(2), g = lerp(1), r = lerp(0), a = lerp(3);
    double blue = static_cast<double>(colourScale[2]) * brightness;
    uint8_t bytes[4] = {ClampedByte(blue * b), ClampedByte(green * g), ClampedByte(red * r), a};
    std::memcpy(out, bytes, 4);
    return out;
}

void CPcFlexibleParticles::EmitForFrame(float& accumulator)
{
    if (!(emitRate > 0.0f))
        return;
    double owed = static_cast<double>(emitRate) * (1.0f / 60.0f) + accumulator; // (0x006463B8)
    accumulator = static_cast<float>(owed);
    if (owed < 1.0f || owed != owed)
        return;
    reinterpret_cast<CParticleSystem*>(this)->Emit(Truncate(owed));
    accumulator = static_cast<float>(accumulator - static_cast<double>(Truncate(accumulator)));
}

CPcFlexibleParticles::sColourSpan CPcFlexibleParticles::ColourSpan(float life) const
{
    sColourSpan span{startColour, endColour, life};
    if (colourMid >= 0.0f) {
        if (life < colourMid) {
            span.t = static_cast<float>(static_cast<double>(life) / colourMid);
            span.to = midColour;
        } else {
            span.t = static_cast<float>((static_cast<double>(life) - colourMid) / (1.0f - static_cast<double>(colourMid)));
            span.from = midColour;
        }
    }
    return span;
}

uint32_t CPcFlexibleParticles::Colour(uint32_t from, uint32_t to, float t)
{
    uint32_t colour;
    LerpColour(&colour, reinterpret_cast<const uint8_t*>(&from), reinterpret_cast<const uint8_t*>(&to), t);
    return colour;
}

// The engine class's destructor body: the call each PC class ends with.
template <typename EngineClass>
void __cdecl DestroyEngine(CPcFlexibleParticles* particles)
{
    reinterpret_cast<EngineClass*>(particles)->EngineClass::DestroyBody();
}

void CPcFlexibleParticles::DestroyParts(uint32_t primary, uint32_t secondary, sSimpleMaterial* material,
                                        void(__cdecl* engineDestructor)(CPcFlexibleParticles*))
{
    vtable = reinterpret_cast<void**>(primary);
    secondaryVtable = reinterpret_cast<void**>(secondary);
    if (material) {
        material->DestroyBody();
        Mem::Manager::Get()->Free(material);
    }
    engineDestructor(this);
}

namespace {

// What a system's constructor is given: (name, max, texture, blend, alpha, segments, unknown), and some also the
// history; the shaded class only the first five; the engine's model class its model name instead of the texture.
struct sConstructArgs {
    uint32_t name;
    int maxParticles;
    uint32_t texture;
    uint32_t blendMode;
    int fixedAlpha;
    int segments;
    float unknown;
    int history;
    const char* model;
};
template <typename T>
void* MakePlain(void* memory, const sConstructArgs& a)
{
    return static_cast<T*>(memory)->Construct(a.name, a.maxParticles, a.texture, a.blendMode, a.fixedAlpha, a.segments,
                                              a.unknown);
}
template <typename T>
void* MakeWithHistory(void* memory, const sConstructArgs& a)
{
    return static_cast<T*>(memory)->Construct(a.name, a.maxParticles, a.texture, a.blendMode, a.fixedAlpha, a.segments,
                                              a.unknown, a.history);
}
template <typename T>
void* MakeFive(void* memory, const sConstructArgs& a)
{
    return static_cast<T*>(memory)->Construct(a.name, a.maxParticles, a.texture, a.blendMode, a.fixedAlpha);
}
void* MakeModel(void* memory, const sConstructArgs& a)
{
    return static_cast<CModelParticles*>(memory)->Construct(a.name, a.maxParticles, a.model, a.blendMode, a.fixedAlpha,
                                                            a.segments, a.unknown, a.history);
}

} // namespace

// 0x004BCEF0
void* __cdecl CreateFlexibleParticles(uint32_t name, uint32_t type, int maxParticles, int, uint32_t texture,
                                      uint32_t blendMode, int fixedAlpha, int segments, float unknown, int history,
                                      const char* model)
{
    struct sType {
        uint32_t type;
        uint32_t size;
        void* (*construct)(void*, const sConstructArgs&); // the class's constructor
    };
    static constexpr sType kTypes[] = {
        {0x15834EEA, 0x150, &MakePlain<CPcGlowParticles>},             // glow
        {0x0EE6FC5B, 0x148, &MakeWithHistory<CPcRibbonParticles>},     // ribbon
        {0x097CB7A9, 0x150, &MakePlain<CPcSmoothStarParticles>},       // smoothstar
        {0x286A8D26, 0x148, &MakeModel},                               // (the engine's model class)
        {0x3624A5EB, 0x150, &MakePlain<CPcStarParticles>},             // star
        {0x2EEB4B09, 0x148, &MakePlain<CPcLineParticles>},             // line
        {0x3D56A7A6, 0x144, &MakeWithHistory<CPcFlatRibbonParticles>}, // (a flat ribbon)
        {0x8ADDAC1F, 0x148, &MakePlain<CPcSmoothParticles>},           // smooth
        {0x7EC7252D, 0x150, &MakeWithHistory<CPcGlowRibbonTrailParticles>}, // glowribbontrail
        {0x3F109FCC, 0x140, &MakeWithHistory<CPcSmoothRibbonParticles>}, // smoothribbon
        {0xC4D5A4CB, 0x148, &MakeWithHistory<CPcRibbonTrailParticles>}, // ribbontrail
        {0xF4D8D486, 0x138, &MakeFive<CPcShadedParticles>},            // shaded
        {0xDEDFC057, 0x148, &MakePlain<CPcNewFlatParticles>},          // newflat
    };
    const sType* chosen = &kTypes[12]; // (any other type: newflat)
    for (const sType& t : kTypes)
        if (t.type == type)
            chosen = &t;
    void* memory = Mem::Manager::Get()->AllocateZeroed(chosen->size, g_allocAssertFlag, nullptr);
    if (!memory)
        return nullptr;
    const sConstructArgs args{name, maxParticles, texture, blendMode, fixedAlpha, segments, unknown, history, model};
    return chosen->construct(memory, args);
}

// --- newflat ---

// 0x004BD3A0
CPcNewFlatParticles* CPcNewFlatParticles::Construct(uint32_t name, int maxParticles, uint32_t texture,
                                                    uint32_t blendMode, int fixedAlpha, int segments, float unknown)
{
    reinterpret_cast<CNewFlatParticles*>(this)->CNewFlatParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segments, unknown, 0);
    vtable = reinterpret_cast<void**>(0x0064A514);
    secondaryVtable = reinterpret_cast<void**>(0x0064A510);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 1, texture, blendMode,
                                                                          fixedAlpha, 0, 0)
                      : nullptr;
    return this;
}

// 0x004BD610
void CPcNewFlatParticles::DestroyBody() { DestroyParts(0x0064A514, 0x0064A510, material, &DestroyEngine<CNewFlatParticles>); }

// 0x004BDC40
CPcNewFlatParticles* CPcNewFlatParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004BD390: the same through the vtable at +0x0C.
static CPcNewFlatParticles* __fastcall DestroyNewFlatSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcNewFlatParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004BD680
void CPcNewFlatParticles::Render()
{
    if (count > 0) {
        Mth::Vector r, u;
        CameraAxes(r, u);
        SetMaterial(material->material);
        SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        SetPixelShader(DefaultPixelShader(), nullptr);
        const float* place = positions;
        sFlexibleParticle* particle = particles;
        int remaining = count;
        int index = 0;
        while (remaining != 0) {
            IDirect3DVertexBuffer9* buffer = nullptr;
            uint32_t firstVertex = 0, room = 0;
            auto* vertex = static_cast<sQuadVertex*>(
                Platform::LockDynamicVertices(count * 4, sizeof(sQuadVertex), &buffer, &firstVertex, &room));
            uint32_t quads = 0;
            if (index < count) {
                for (;;) {
                    float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                    double across = (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life +
                                    particle->startSize[0];
                    double up = (static_cast<double>(particle->endSize[1]) - particle->startSize[1]) * life +
                                particle->startSize[1];
                    float cx, cy, cz;
                    if (emitterIndependent) {
                        cx = place[0], cy = place[1], cz = place[2];
                    } else {
                        cx = static_cast<float>(static_cast<double>(position.x) + place[0]);
                        cy = static_cast<float>(static_cast<double>(place[1]) + position.y);
                        cz = static_cast<float>(static_cast<double>(place[2]) + position.z);
                    }
                    float rx = static_cast<float>(r.x * across), ry = static_cast<float>(r.y * across),
                          rz = static_cast<float>(r.z * across);
                    float ux = static_cast<float>(u.x * up), uy = static_cast<float>(u.y * up),
                          uz = static_cast<float>(u.z * up);

                    sColourSpan span = ColourSpan(life);
                    uint32_t colour = Colour(*span.from, *span.to, span.t);

                    double dx = static_cast<double>(cx) - rx, dy = static_cast<double>(cy) - ry,
                           dz = static_cast<double>(cz) - rz;
                    float fdx = static_cast<float>(dx), fdy = static_cast<float>(dy), fdz = static_cast<float>(dz);
                    double sz = static_cast<double>(rz) + cz;
                    float fsx = static_cast<float>(static_cast<double>(rx) + cx),
                          fsy = static_cast<float>(static_cast<double>(ry) + cy), fsz = static_cast<float>(sz);
                    vertex[0] = {static_cast<float>(static_cast<double>(fdx) + ux),
                                 static_cast<float>(static_cast<double>(fdy) + uy), static_cast<float>(dz + uz), colour,
                                 0.0f, 0.0f};
                    vertex[1] = {static_cast<float>(static_cast<double>(fsx) + ux),
                                 static_cast<float>(static_cast<double>(fsy) + uy), static_cast<float>(sz + uz), colour,
                                 1.0f, 0.0f};
                    vertex[2] = {static_cast<float>(static_cast<double>(fsx) - ux),
                                 static_cast<float>(static_cast<double>(fsy) - uy),
                                 static_cast<float>(static_cast<double>(fsz) - uz), colour, 1.0f, 1.0f};
                    vertex[3] = {static_cast<float>(static_cast<double>(fdx) - ux),
                                 static_cast<float>(static_cast<double>(fdy) - uy),
                                 static_cast<float>(static_cast<double>(fdz) - uz), colour, 0.0f, 1.0f};
                    vertex += 4;
                    ++quads;
                    if (quads >= room - 4)
                        break;
                    ++particle;
                    ++index;
                    place += 3;
                    if (index >= count)
                        break;
                }
            }
            remaining -= static_cast<int>(quads);
            Platform::UnlockDynamicVertices(quads * 4, sizeof(sQuadVertex));
            if (quads >= room - 4)
                break; // (full: the original stops here, undrawn)
            Device()->SetStreamSource(0, buffer, 0, sizeof(sQuadVertex));
            Device()->SetIndices(QuadIndices());
            Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, quads * 4, 0,
                                           quads * 2);
            ++DrawCalls();
            DrawnTriangles() += quads * 2;
        }
    }
    EmitForFrame(emitted);
}

// --- the flat ribbon (0x3D56A7A6) ---

namespace {

// A ribbon's vertex: position, colour.
struct sTrailVertex {
    float x, y, z;
    uint32_t colour;
};
static_assert(sizeof(sTrailVertex) == 0x10, "layout must match the original");

// A particle's position (`index` in the positions), the emitter's added unless emitterIndependent.
void PlaceOf(const CPcFlexibleParticles* system, int index, float out[3])
{
    const float* p = system->positions + 3 * index;
    if (system->emitterIndependent) {
        out[0] = p[0], out[1] = p[1], out[2] = p[2];
    } else {
        out[0] = static_cast<float>(static_cast<double>(system->position.x) + p[0]);
        out[1] = static_cast<float>(static_cast<double>(p[1]) + system->position.y);
        out[2] = static_cast<float>(static_cast<double>(p[2]) + system->position.z);
    }
}

// Across the path from p0 to p1 and the camera's direction (a, b, c): (p1 - p0) x (a, b, c), normalised when it has
// a length. The ribbon's first segment rounds every component first; the later ones keep z unrounded.
void Across(const float p0[3], const float p1[3], double a, double b, double c, bool first, float n[3])
{
    double dx = static_cast<double>(p1[0]) - p0[0], dy = static_cast<double>(p1[1]) - p0[1],
           dz = static_cast<double>(p1[2]) - p0[2];
    n[0] = static_cast<float>(c * dy - b * dz);
    n[1] = static_cast<float>(a * dz - c * dx);
    double z = b * dx - a * dy;
    n[2] = static_cast<float>(z);
    if (first)
        z = n[2];
    double length = std::sqrt(static_cast<double>(n[0]) * n[0] + z * z + static_cast<double>(n[1]) * n[1]);
    if (length > 0.0f) {
        double inverse = 1.0f / length;
        n[0] = static_cast<float>(n[0] * inverse);
        n[1] = static_cast<float>(n[1] * inverse);
        n[2] = static_cast<float>(inverse * z);
    }
}

} // namespace

// 0x004BDC80
CPcFlatRibbonParticles* CPcFlatRibbonParticles::Construct(uint32_t name, int maxParticles, uint32_t texture,
                                                          uint32_t blendMode, int fixedAlpha, int segmentCount,
                                                          float ribbonWidth, int history)
{
    reinterpret_cast<CFlatRibbonParticles*>(this)->CFlatRibbonParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, ribbonWidth, history);
    vtable = reinterpret_cast<void**>(0x0064A528);
    secondaryVtable = reinterpret_cast<void**>(0x0064A524);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 0xB, texture, blendMode,
                                                                          fixedAlpha, segmentCount, history)
                      : nullptr;
    width = ribbonWidth;
    return this;
}

// 0x004BDD60
void CPcFlatRibbonParticles::DestroyBody() { DestroyParts(0x0064A528, 0x0064A524, material, &DestroyEngine<CFlatRibbonParticles>); }

// 0x004BECD0
CPcFlatRibbonParticles* CPcFlatRibbonParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004BDC70: the same through the vtable at +0x0C.
static CPcFlatRibbonParticles* __fastcall DestroyFlatRibbonSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcFlatRibbonParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004BDDD0
void CPcFlatRibbonParticles::Render()
{
    if (count <= 0)
        return;
    double a = GameVariable<float>(0x00786BD8);
    double b = GameVariable<float>(0x00786BE8);
    double c = GameVariable<float>(0x00786BF8);
    Mth::Vector r, u;
    CameraAxes(r, u);
    uint32_t particleBytes = (segments - 1) * 16;
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    SetPixelShader(GameVariable<void*>(0x007CA95C), material->material);

    // The ribbons.
    sFlexibleParticle* particle = particles;
    int remaining = (segments - 1) * count * 4;
    int index = 0;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<sTrailVertex*>(
            Platform::LockDynamicVertices(remaining, sizeof(sTrailVertex), &buffer, &firstVertex, &room));
        if (index < count) {
            for (uint32_t bytes = particleBytes; bytes <= 0x7FF; bytes += particleBytes) {
                float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                float p0[3], p1[3], n[3];
                PlaceOf(this, index, p0);
                PlaceOf(this, historyStride + index, p1);
                Across(p0, p1, a, b, c, true, n);
                float w = static_cast<float>(
                    (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life + particle->startSize[0]);
                sColourSpan span = ColourSpan(life);
                const uint32_t* from = span.from;
                const uint32_t* to = span.to;
                uint32_t previousColour = Colour(*from++, *to++, span.t);

                // The head end's edge points.
                double n0w = static_cast<double>(n[0]) * w, n1w = static_cast<double>(w) * n[1],
                       n2w = static_cast<double>(w) * n[2];
                float o0 = static_cast<float>(static_cast<double>(static_cast<float>(n0w)) * width);
                float o1 = static_cast<float>(width * n1w);
                float previousA[3] = {static_cast<float>(static_cast<double>(o0) + p0[0]),
                                      static_cast<float>(static_cast<double>(o1) + p0[1]),
                                      static_cast<float>(width * n2w + p0[2])};
                float previousB[3] = {
                    static_cast<float>(static_cast<double>(p0[0]) - o0), static_cast<float>(static_cast<double>(p0[1]) - o1),
                    static_cast<float>(p0[2] - width * static_cast<double>(static_cast<float>(n2w)))};

                for (int k = 1; k < segments; ++k) {
                    uint32_t colour = Colour(*from++, *to++, span.t);
                    if (k > 1) {
                        PlaceOf(this, historyStride * k + index, p1);
                        Across(p0, p1, a, b, c, false, n);
                    }
                    n0w = static_cast<double>(n[0]) * w;
                    n1w = static_cast<double>(w) * n[1];
                    n2w = static_cast<double>(w) * n[2];
                    o0 = static_cast<float>(static_cast<double>(static_cast<float>(n0w)) * width);
                    o1 = static_cast<float>(width * n1w);
                    float nextB[3] = {static_cast<float>(static_cast<double>(p1[0]) - o0),
                                      static_cast<float>(static_cast<double>(p1[1]) - o1),
                                      static_cast<float>(p1[2] - width * n2w)};
                    float o2 = static_cast<float>(width * static_cast<double>(static_cast<float>(n2w)));
                    float nextA[3] = {static_cast<float>(static_cast<double>(o0) + p1[0]),
                                      static_cast<float>(static_cast<double>(o1) + p1[1]),
                                      static_cast<float>(static_cast<double>(o2) + p1[2])};
                    vertex[0] = {previousA[0], previousA[1], previousA[2], previousColour};
                    vertex[1] = {previousB[0], previousB[1], previousB[2], previousColour};
                    vertex[2] = {nextA[0], nextA[1], nextA[2], colour};
                    vertex[3] = {nextB[0], nextB[1], nextB[2], colour};
                    vertex += 4;
                    std::memcpy(previousA, nextA, sizeof(nextA));
                    std::memcpy(previousB, nextB, sizeof(nextB));
                    std::memcpy(p0, p1, sizeof(p1));
                    previousColour = colour;
                    triangles += 2;
                    vertices += 4;
                    if (vertices >= room - 4)
                        break;
                }
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                if (index >= count)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, sizeof(sTrailVertex));
        if (vertices >= room - 4)
            break;
        Device()->SetStreamSource(0, buffer, 0, sizeof(sTrailVertex));
        Device()->SetIndices(QuadIndices());
        Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, vertices, 0, triangles);
        ++DrawCalls();
        DrawnTriangles() += triangles;
    }

    // The heads.
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    SetPixelShader(DefaultPixelShader(), nullptr);
    particle = particles;
    remaining = count * 4;
    index = 0;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<sQuadVertex*>(
            Platform::LockDynamicVertices(remaining, sizeof(sQuadVertex), &buffer, &firstVertex, &room));
        if (index < count) {
            for (uint32_t bytes = sizeof(sQuadVertex); bytes <= 0x7FF; bytes += sizeof(sQuadVertex)) {
                float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                double size = ((static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life +
                               particle->startSize[0]) *
                              width * 5.0f; // (0x006469AC)
                float r0 = static_cast<float>(r.x * size), r1 = static_cast<float>(r.y * size),
                      r2 = static_cast<float>(r.z * size);
                float u0 = static_cast<float>(u.x * size), u1 = static_cast<float>(u.y * size);
                double u2 = u.z * size;
                const float* p = positions + 3 * index;
                double px, py;
                float pz;
                if (emitterIndependent) {
                    px = p[0], py = p[1], pz = p[2];
                } else {
                    px = static_cast<double>(position.x) + p[0];
                    py = static_cast<double>(p[1]) + position.y;
                    pz = static_cast<float>(static_cast<double>(p[2]) + position.z);
                }
                double e0 = px + r0;
                float e1 = static_cast<float>(py + r1), e2 = static_cast<float>(static_cast<double>(pz) + r2);
                float g0 = static_cast<float>(px - r0);
                double g1 = py - r1, g2 = static_cast<double>(pz) - r2;
                float g1f = static_cast<float>(g1);
                sColourSpan span = ColourSpan(life);
                uint32_t colour = Colour(*span.from, *span.to, span.t);
                vertex[0] = {static_cast<float>(e0 - u0), static_cast<float>(static_cast<double>(e1) - u1),
                             static_cast<float>(e2 - u2), colour, 0.0f, 0.0f};
                vertex[1] = {static_cast<float>(static_cast<double>(g0) - u0),
                             static_cast<float>(static_cast<double>(g1f) - u1), static_cast<float>(g2 - u2), colour,
                             1.0f, 0.0f};
                vertex[2] = {static_cast<float>(static_cast<double>(g0) + u0),
                             static_cast<float>(static_cast<double>(g1f) + u1), static_cast<float>(g2 + u2), colour,
                             1.0f, 1.0f};
                vertex[3] = {static_cast<float>(static_cast<double>(static_cast<float>(e0)) + u0),
                             static_cast<float>(static_cast<double>(e1) + u1), static_cast<float>(e2 + u2), colour,
                             0.0f, 1.0f};
                vertex += 4;
                vertices += 4;
                triangles += 2;
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                if (index >= count)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, sizeof(sQuadVertex));
        if (vertices >= room - 4)
            break;
        Device()->SetStreamSource(0, buffer, 0, sizeof(sQuadVertex));
        Device()->SetIndices(QuadIndices());
        Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, vertices, 0, triangles);
        ++DrawCalls();
        DrawnTriangles() += triangles;
    }
}

// --- glow ---

// 0x004BED10
CPcGlowParticles* CPcGlowParticles::Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                              int fixedAlpha, int segmentCount, float unknown)
{
    reinterpret_cast<CGlowParticles*>(this)->CGlowParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, unknown, 0);
    vtable = reinterpret_cast<void**>(0x0064A540);
    secondaryVtable = reinterpret_cast<void**>(0x0064A53C);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 4, texture, blendMode,
                                                                          fixedAlpha, segmentCount, 0)
                      : nullptr;
    return this;
}

// 0x004BEDE0
void CPcGlowParticles::DestroyBody() { DestroyParts(0x0064A540, 0x0064A53C, material, &DestroyEngine<CGlowParticles>); }

// 0x004BF6E0
CPcGlowParticles* CPcGlowParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004BED00: the same through the vtable at +0x0C.
static CPcGlowParticles* __fastcall DestroyGlowSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcGlowParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004BEE50
void CPcGlowParticles::Render()
{
    if (count <= 0)
        return;
    Mth::Vector r, u;
    CameraAxes(r, u);
    uint32_t particleWords = segments * 9 * 4; // (nine vertices of four words a segment)
    uint32_t remaining = count * particleWords;
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    SetPixelShader(GameVariable<void*>(0x007CA95C), material->material);
    sFlexibleParticle* particle = particles;
    const float* place = positions;
    int index = 0;
    sTrailVertex buffer[0x200];
    while (remaining > 0) {
        sTrailVertex* vertex = buffer;
        uint32_t words = 0;
        int total = count;
        for (uint32_t budget = particleWords; index < total && budget <= 0x7FF; budget += particleWords) {
            float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            double across = (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life +
                            particle->startSize[0];
            double up = (static_cast<double>(particle->endSize[1]) - particle->startSize[1]) * life +
                        particle->startSize[1];
            float p[3];
            if (emitterIndependent) {
                p[0] = place[0], p[1] = place[1], p[2] = place[2];
            } else {
                p[0] = static_cast<float>(static_cast<double>(position.x) + place[0]);
                p[1] = static_cast<float>(static_cast<double>(place[1]) + position.y);
                p[2] = static_cast<float>(static_cast<double>(place[2]) + position.z);
            }
            float rx = static_cast<float>(r.x * across), ry = static_cast<float>(r.y * across),
                  rz = static_cast<float>(r.z * across);
            float ux = static_cast<float>(u.x * up), uy = static_cast<float>(u.y * up), uz = static_cast<float>(u.z * up);
            // The centre's, the edge's and the outer ring's colours (each its own of the three colour arrays').
            sColourSpan span = ColourSpan(life);
            uint32_t colours[3];
            for (int k = 0; k < 3; ++k)
                colours[k] = Colour(span.from[k], span.to[k], span.t);
            double w = glowWidth;

            // The rings' points at angle 0: outer (r sin, u cos, times the width), inner.
            double s = X87::Sin(0.0), c = X87::Cos(0.0);
            float outer[3], inner[3];
            {
                double ry_s = static_cast<float>(ry * s), rz_s = static_cast<float>(rz * s);
                outer[0] = static_cast<float>(rx * s * w + p[0]);
                outer[1] = static_cast<float>(ry_s * w + p[1]);
                outer[2] = static_cast<float>(static_cast<double>(static_cast<float>(rz_s * w)) + p[2]);
                double uy_c = static_cast<float>(uy * c), uz_c = static_cast<float>(uz * c);
                outer[0] = static_cast<float>(ux * c * w + outer[0]);
                outer[1] = static_cast<float>(uy_c * w + outer[1]);
                outer[2] = static_cast<float>(static_cast<double>(static_cast<float>(uz_c * w)) + outer[2]);
                inner[0] = static_cast<float>(rx * s + p[0]);
                inner[1] = static_cast<float>(static_cast<double>(static_cast<float>(ry * s)) + p[1]);
                inner[2] = static_cast<float>(static_cast<double>(static_cast<float>(rz * s)) + p[2]);
                inner[0] = static_cast<float>(ux * c + inner[0]);
                inner[1] = static_cast<float>(static_cast<double>(static_cast<float>(uy * c)) + inner[1]);
                inner[2] = static_cast<float>(static_cast<double>(static_cast<float>(uz * c)) + inner[2]);
            }
            float step = static_cast<float>(6.28318548f / static_cast<double>(segments)); // (0x0064545C)
            for (int i = 1; i <= segments; ++i) {
                double angle = static_cast<double>(i) * step;
                double si = X87::Sin(angle), ci = X87::Cos(angle);
                float nextOuter[3], nextInner[3];
                double rsw = rx * si * w;
                double r1 = static_cast<double>(static_cast<float>(ry * si)) * w;
                double r2 = static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(rz * si)) * w)) + p[2];
                float cosine = static_cast<float>(ci);
                double usw = ux * ci * w;
                double u1 = static_cast<double>(static_cast<float>(uy * ci)) * w;
                float u2 = static_cast<float>(static_cast<double>(static_cast<float>(uz * ci)) * w);
                nextOuter[0] = static_cast<float>(usw + static_cast<float>(rsw + p[0]));
                nextOuter[1] = static_cast<float>(u1 + (r1 + p[1]));
                nextOuter[2] = static_cast<float>(u2 + r2);
                double x = rx * si + p[0];
                double y = static_cast<double>(static_cast<float>(ry * si)) + p[1];
                float z = static_cast<float>(static_cast<double>(static_cast<float>(rz * si)) + p[2]);
                nextInner[0] = static_cast<float>(ux * static_cast<double>(cosine) + x);
                nextInner[1] = static_cast<float>(static_cast<double>(static_cast<float>(uy * static_cast<double>(cosine))) + y);
                nextInner[2] = static_cast<float>(static_cast<double>(static_cast<float>(uz * static_cast<double>(cosine))) + z);
                vertex[0] = {p[0], p[1], p[2], colours[0]};
                vertex[1] = {outer[0], outer[1], outer[2], colours[1]};
                vertex[2] = {nextOuter[0], nextOuter[1], nextOuter[2], colours[1]};
                vertex[3] = {outer[0], outer[1], outer[2], colours[1]};
                vertex[4] = {nextOuter[0], nextOuter[1], nextOuter[2], colours[1]};
                vertex[5] = {inner[0], inner[1], inner[2], colours[2]};
                vertex[6] = {nextOuter[0], nextOuter[1], nextOuter[2], colours[1]};
                vertex[7] = {inner[0], inner[1], inner[2], colours[2]};
                vertex[8] = {nextInner[0], nextInner[1], nextInner[2], colours[2]};
                vertex += 9;
                std::memcpy(outer, nextOuter, sizeof(outer));
                std::memcpy(inner, nextInner, sizeof(inner));
            }
            words += particleWords;
            remaining -= particleWords;
            ++index;
            place += 3;
            ++particle;
        }
        uint32_t triangles = (words >> 4) / 3; // (the original's count: a quarter of what it wrote)
        if (!GameVariable<bool>(0x007D1EC0)) { // (rendering not skipped)
            DrawnTriangles() += triangles;
            ++DrawCalls();
            Device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, buffer, sizeof(sTrailVertex));
        }
    }
}

// --- line ---

// 0x004C09F0
CPcLineParticles* CPcLineParticles::Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                              int fixedAlpha, int segmentCount, float unknown)
{
    reinterpret_cast<CLineParticles*>(this)->CLineParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, unknown, 0);
    vtable = reinterpret_cast<void**>(0x0064A570);
    secondaryVtable = reinterpret_cast<void**>(0x0064A56C);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 0, texture, blendMode,
                                                                          fixedAlpha, 0, 0)
                      : nullptr;
    return this;
}

// 0x004C0AC0
void CPcLineParticles::DestroyBody() { DestroyParts(0x0064A570, 0x0064A56C, material, &DestroyEngine<CLineParticles>); }

// 0x004C0E70
CPcLineParticles* CPcLineParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C09E0: the same through the vtable at +0x0C.
static CPcLineParticles* __fastcall DestroyLineSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcLineParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004C0B30
void CPcLineParticles::Render()
{
    if (count <= 0)
        return;
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    SetPixelShader(GameVariable<void*>(0x007CA95C), material->material);
    int remaining = count * 2;
    int index = 0;
    sFlexibleParticle* particle = particles;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, lines = 0;
        auto* vertex = static_cast<sTrailVertex*>(
            Platform::LockDynamicVertices(count * 2, sizeof(sTrailVertex), &buffer, &firstVertex, &room));
        while (index < count) {
            float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            float p0[3], p1[3];
            PlaceOf(this, index, p0);
            PlaceOf(this, historyStride + index, p1);
            sColourSpan span = ColourSpan(life);
            uint32_t colours[2];
            for (int k = 0; k < 2; ++k)
                colours[k] = Colour(span.from[k], span.to[k], span.t);
            vertex[0] = {p0[0], p0[1], p0[2], colours[0]};
            vertex[1] = {p1[0], p1[1], p1[2], colours[1]};
            vertex += 2;
            vertices += 2;
            ++lines;
            if (vertices >= room - 2)
                break;
            ++index;
            ++particle;
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, sizeof(sTrailVertex));
        if (vertices >= room - 2)
            break;
        Device()->SetStreamSource(0, buffer, 0, sizeof(sTrailVertex));
        Device()->DrawPrimitive(D3DPT_LINELIST, firstVertex, lines);
        ++DrawCalls();
        DrawnTriangles() += lines;
    }
}

// --- ribbon ---

// 0x004C0EB0
CPcRibbonParticles* CPcRibbonParticles::Construct(uint32_t name, int maxParticles, uint32_t texture,
                                                  uint32_t blendMode, int fixedAlpha, int segmentCount, float unknown,
                                                  int history)
{
    reinterpret_cast<CRibbonParticles*>(this)->CRibbonParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, unknown, history);
    vtable = reinterpret_cast<void**>(0x0064A584);
    secondaryVtable = reinterpret_cast<void**>(0x0064A580);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 7, texture, blendMode,
                                                                          fixedAlpha, segmentCount, 0)
                      : nullptr;
    return this;
}

// 0x004C0F80
void CPcRibbonParticles::DestroyBody() { DestroyParts(0x0064A584, 0x0064A580, material, &DestroyEngine<CRibbonParticles>); }

// 0x004C16E0
CPcRibbonParticles* CPcRibbonParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C0EA0: the same through the vtable at +0x0C.
static CPcRibbonParticles* __fastcall DestroyRibbonSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcRibbonParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004C0FF0
void CPcRibbonParticles::Render()
{
    if (count <= 0)
        return;
    double a = GameVariable<float>(0x00786BD8);
    double b = GameVariable<float>(0x00786BE8);
    double c = GameVariable<float>(0x00786BF8);
    bool textured = *reinterpret_cast<const uint32_t*>(material->material + 0x30) != 0;
    uint32_t stride = textured ? sizeof(sQuadVertex) : sizeof(sTrailVertex);
    SetMaterial(material->material);
    if (textured) {
        SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        SetPixelShader(DefaultPixelShader(), nullptr);
    } else {
        SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        SetPixelShader(GameVariable<void*>(0x007CA95C), material->material);
    }
    sFlexibleParticle* particle = particles;
    const float* head = positions;
    const float* tail = positions + 3 * (segments - 1) * historyStride;
    int index = 0;
    int remaining = count * 4;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* out = static_cast<uint8_t*>(
            Platform::LockDynamicVertices(remaining, stride, &buffer, &firstVertex, &room));
        while (index < count) {
            float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            double hx, hy;
            float hz, t[3];
            if (emitterIndependent) {
                hx = head[0], hy = head[1], hz = head[2];
                t[0] = tail[0], t[1] = tail[1], t[2] = tail[2];
            } else {
                hx = static_cast<double>(position.x) + head[0];
                hy = static_cast<double>(head[1]) + position.y;
                hz = static_cast<float>(static_cast<double>(head[2]) + position.z);
                t[0] = static_cast<float>(static_cast<double>(position.x) + tail[0]);
                t[1] = static_cast<float>(static_cast<double>(tail[1]) + position.y);
                t[2] = static_cast<float>(static_cast<double>(tail[2]) + position.z);
            }
            // Across the ribbon: (tail - head) x the camera's direction, normalised when it has a length.
            float dx = static_cast<float>(t[0] - hx), dy = static_cast<float>(t[1] - hy),
                  dz = static_cast<float>(static_cast<double>(t[2]) - hz);
            double n0 = dy * c - dz * b;
            double n1 = dz * a - c * dx;
            float n2 = static_cast<float>(b * dx - dy * a);
            double length = std::sqrt(n0 * n0 + static_cast<double>(n2) * n2 + n1 * n1);
            if (length > 0.0f) {
                double inverse = 1.0f / length;
                n0 *= inverse;
                n1 *= inverse;
                n2 = static_cast<float>(n2 * inverse);
            }
            float w = static_cast<float>(
                (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life + particle->startSize[0]);
            double n0w = n0 * w, n1w = n1 * w, n2w = static_cast<double>(n2) * w;
            float f0 = static_cast<float>(n0w), f1 = static_cast<float>(n1w), f2 = static_cast<float>(n2w);
            float corners[4][3] = {
                {static_cast<float>(f0 + hx), static_cast<float>(f1 + hy), static_cast<float>(n2w + hz)},
                {static_cast<float>(hx - f0), static_cast<float>(hy - f1), static_cast<float>(static_cast<double>(hz) - f2)},
                {static_cast<float>(static_cast<double>(t[0]) - f0), static_cast<float>(static_cast<double>(t[1]) - f1),
                 static_cast<float>(static_cast<double>(t[2]) - f2)},
                {static_cast<float>(static_cast<double>(f0) + t[0]), static_cast<float>(static_cast<double>(f1) + t[1]),
                 static_cast<float>(static_cast<double>(f2) + t[2])}};
            sColourSpan span = ColourSpan(life);
            uint32_t colours[2];
            for (int k = 0; k < 2; ++k)
                colours[k] = Colour(span.from[k], span.to[k], span.t);
            static constexpr float kUv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            for (int k = 0; k < 4; ++k) {
                uint32_t colour = colours[k / 2];
                if (textured) {
                    reinterpret_cast<sQuadVertex*>(out)[k] = {corners[k][0], corners[k][1], corners[k][2], colour,
                                                              kUv[k][0], kUv[k][1]};
                } else {
                    reinterpret_cast<sTrailVertex*>(out)[k] = {corners[k][0], corners[k][1], corners[k][2], colour};
                }
            }
            out += 4 * stride;
            vertices += 4;
            triangles += 2;
            if (vertices >= room - 4)
                break;
            head += 3;
            tail += 3;
            ++index;
            ++particle;
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, stride);
        if (vertices >= room - 4)
            break;
        Device()->SetStreamSource(0, buffer, 0, stride);
        Device()->SetIndices(QuadIndices());
        Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, vertices, 0, triangles);
        ++DrawCalls();
        DrawnTriangles() += triangles;
    }
}

// --- ribbontrail ---

// The colour of a ribbon trail's head end (inline in its Render): LerpColour's channels, the red, green and blue
// scaled by the brightness alone, not clamped (each the low byte).
uint32_t CPcRibbonTrailParticles::HeadColour(uint32_t from, uint32_t to, float t)
{
    const auto* f = reinterpret_cast<const uint8_t*>(&from);
    const auto* g = reinterpret_cast<const uint8_t*>(&to);
    auto lerp = [&](int k) { return static_cast<uint8_t>(f[k] + Truncate(static_cast<double>(g[k] - f[k]) * t)); };
    uint8_t bytes[4] = {lerp(2), lerp(1), lerp(0), lerp(3)};
    for (int k = 0; k < 3; ++k)
        bytes[k] = static_cast<uint8_t>(Truncate(static_cast<double>(bytes[k]) * brightness));
    uint32_t colour;
    std::memcpy(&colour, bytes, 4);
    return colour;
}

// 0x004C1720
CPcRibbonTrailParticles* CPcRibbonTrailParticles::Construct(uint32_t name, int maxParticles, uint32_t texture,
                                                            uint32_t blendMode, int fixedAlpha, int segmentCount,
                                                            float unknown, int history)
{
    reinterpret_cast<CRibbonTrailParticles*>(this)->CRibbonTrailParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, unknown, history);
    vtable = reinterpret_cast<void**>(0x0064A598);
    secondaryVtable = reinterpret_cast<void**>(0x0064A594);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 9, texture, blendMode,
                                                                          fixedAlpha, segmentCount, history)
                      : nullptr;
    return this;
}

// 0x004C17F0
void CPcRibbonTrailParticles::DestroyBody() { DestroyParts(0x0064A598, 0x0064A594, material, &DestroyEngine<CRibbonTrailParticles>); }

// 0x004C2160
CPcRibbonTrailParticles* CPcRibbonTrailParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C1710: the same through the vtable at +0x0C.
static CPcRibbonTrailParticles* __fastcall DestroyRibbonTrailSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcRibbonTrailParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004C1860
void CPcRibbonTrailParticles::Render()
{
    if (count <= 0)
        return;
    double a = GameVariable<float>(0x00786BD8);
    double b = GameVariable<float>(0x00786BE8);
    double c = GameVariable<float>(0x00786BF8);
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    SetPixelShader(GameVariable<void*>(0x007CA95C), material->material);
    sFlexibleParticle* particle = particles;
    int remaining = (segments - 1) * count * 4;
    int index = 0;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<sTrailVertex*>(
            Platform::LockDynamicVertices(remaining, sizeof(sTrailVertex), &buffer, &firstVertex, &room));
        while (index < count) {
            float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            float p0[3], p1[3], n[3];
            PlaceOf(this, index, p0);
            PlaceOf(this, historyStride + index, p1);
            {
                double dx = static_cast<double>(p1[0]) - p0[0], dy = static_cast<double>(p1[1]) - p0[1],
                       dz = static_cast<double>(p1[2]) - p0[2];
                n[0] = static_cast<float>(dy * c - dz * b);
                n[1] = static_cast<float>(dz * a - c * dx);
                n[2] = static_cast<float>(b * dx - dy * a);
                double length = std::sqrt(static_cast<double>(n[2]) * n[2] + static_cast<double>(n[1]) * n[1] +
                                          static_cast<double>(n[0]) * n[0]);
                if (length > 0.0f) {
                    double inverse = 1.0f / length;
                    for (float& f : n)
                        f = static_cast<float>(f * inverse);
                }
            }
            float w = static_cast<float>(
                (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life + particle->startSize[0]);
            sColourSpan span = ColourSpan(life);
            const uint32_t* from = span.from;
            const uint32_t* to = span.to;
            uint32_t previousColour = HeadColour(*from++, *to++, span.t);
            double n0w = static_cast<double>(n[0]) * w, n1w = static_cast<double>(n[1]) * w,
                   n2w = static_cast<double>(n[2]) * w;
            float f0 = static_cast<float>(n0w), f1 = static_cast<float>(n1w), f2 = static_cast<float>(n2w);
            float previousPlus[3] = {static_cast<float>(static_cast<double>(f0) + p0[0]),
                                     static_cast<float>(static_cast<double>(f1) + p0[1]),
                                     static_cast<float>(n2w + p0[2])};
            float previousMinus[3] = {static_cast<float>(static_cast<double>(p0[0]) - f0),
                                      static_cast<float>(static_cast<double>(p0[1]) - f1),
                                      static_cast<float>(static_cast<double>(p0[2]) - f2)};
            for (int k = 1; k < segments; ++k) {
                uint32_t colour = Colour(*from++, *to++, span.t);
                if (k > 1) {
                    PlaceOf(this, historyStride * k + index, p1);
                    double dx = static_cast<double>(p1[0]) - p0[0], dy = static_cast<double>(p1[1]) - p0[1],
                           dz = static_cast<double>(p1[2]) - p0[2];
                    n[0] = static_cast<float>(dy * c - dz * b);
                    n[1] = static_cast<float>(dz * a - c * dx);
                    double z = b * dx - dy * a;
                    n[2] = static_cast<float>(z);
                    double length =
                        std::sqrt(z * z + static_cast<double>(n[1]) * n[1] + static_cast<double>(n[0]) * n[0]);
                    if (length > 0.0f) {
                        double inverse = 1.0f / length;
                        n[0] = static_cast<float>(n[0] * inverse);
                        n[1] = static_cast<float>(n[1] * inverse);
                        n[2] = static_cast<float>(z * inverse);
                    }
                }
                n0w = static_cast<double>(n[0]) * w;
                n1w = static_cast<double>(n[1]) * w;
                n2w = static_cast<double>(n[2]) * w;
                f0 = static_cast<float>(n0w), f1 = static_cast<float>(n1w), f2 = static_cast<float>(n2w);
                float plus[3] = {static_cast<float>(static_cast<double>(f0) + p1[0]), static_cast<float>(n1w + p1[1]),
                                 static_cast<float>(n2w + p1[2])};
                float minus[3] = {static_cast<float>(static_cast<double>(p1[0]) - f0),
                                  static_cast<float>(static_cast<double>(p1[1]) - f1),
                                  static_cast<float>(static_cast<double>(p1[2]) - f2)};
                vertex[0] = {previousPlus[0], previousPlus[1], previousPlus[2], previousColour};
                vertex[1] = {previousMinus[0], previousMinus[1], previousMinus[2], previousColour};
                vertex[2] = {minus[0], minus[1], minus[2], colour};
                vertex[3] = {plus[0], plus[1], plus[2], colour};
                vertex += 4;
                previousColour = colour;
                std::memcpy(p0, p1, sizeof(p1));
                std::memcpy(previousPlus, plus, sizeof(plus));
                std::memcpy(previousMinus, minus, sizeof(minus));
                triangles += 2;
                vertices += 4;
                if (vertices >= room - 4)
                    break;
            }
            if (vertices >= room - 4)
                break;
            ++index;
            ++particle;
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, sizeof(sTrailVertex));
        if (vertices >= room - 4)
            break;
        Device()->SetStreamSource(0, buffer, 0, sizeof(sTrailVertex));
        Device()->SetIndices(QuadIndices());
        Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, vertices, 0, triangles);
        ++DrawCalls();
        DrawnTriangles() += triangles;
    }
}

// --- shaded ---

void CPcFlexibleParticles::AlignedAxes(Mth::Vector& r, Mth::Vector& u) const
{
    double ax = alignTo.x, ay = alignTo.y, az = alignTo.z;
    double p = 0.0f, q = 1.0f; // (up: y - or x when alignTo is y itself)
    double dot = az * 0.0f + ax * 0.0f + ay;
    if (1.0f == dot)
        p = 1.0f, q = 0.0f;
    r.x = static_cast<float>(ay * 0.0f - az * q);
    r.y = static_cast<float>(az * p - ax * 0.0f);
    r.z = static_cast<float>(q * ax - ay * p);
    r.w = 0.0f;
    u.x = static_cast<float>(r.y * az - r.z * ay);
    u.y = static_cast<float>(r.z * ax - az * r.x);
    u.z = static_cast<float>(ay * r.x - r.y * ax);
    u.w = 0.0f;
    r.Normalize();
    u.Normalize();
}

// 0x004C21A0
CPcShadedParticles* CPcShadedParticles::Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                                  int fixedAlpha)
{
    reinterpret_cast<CShadedParticles*>(this)->CShadedParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, 0, 0, 0);
    vtable = reinterpret_cast<void**>(0x0064A5AC);
    secondaryVtable = reinterpret_cast<void**>(0x0064A5A8);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 2, texture, blendMode,
                                                                          fixedAlpha, 0, 0)
                      : nullptr;
    return this;
}

// 0x004C2260
void CPcShadedParticles::DestroyBody() { DestroyParts(0x0064A5AC, 0x0064A5A8, material, &DestroyEngine<CShadedParticles>); }

// 0x004C2AF0
CPcShadedParticles* CPcShadedParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C2190: the same through the vtable at +0x0C.
static CPcShadedParticles* __fastcall DestroyShadedSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcShadedParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004C22D0
void CPcShadedParticles::Render()
{
    if (count <= 0)
        return;
    Mth::Vector r, u;
    if (aligned)
        AlignedAxes(r, u);
    else
        CameraAxes(r, u);
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    SetPixelShader(material->shader, nullptr);
    sFlexibleParticle* particle = particles;
    const float* place = positions;
    int index = 0;
    int remaining = count * 4;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<sQuadVertex*>(
            Platform::LockDynamicVertices(remaining, sizeof(sQuadVertex), &buffer, &firstVertex, &room));
        if (index < count) {
            for (uint32_t bytes = sizeof(sQuadVertex); bytes <= 0x7FF; bytes += sizeof(sQuadVertex)) {
                float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                double across = (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life +
                                particle->startSize[0];
                double up = (static_cast<double>(particle->endSize[1]) - particle->startSize[1]) * life +
                            particle->startSize[1];
                float p[3];
                if (emitterIndependent) {
                    p[0] = place[0], p[1] = place[1], p[2] = place[2];
                } else {
                    p[0] = static_cast<float>(static_cast<double>(position.x) + place[0]);
                    p[1] = static_cast<float>(static_cast<double>(place[1]) + position.y);
                    p[2] = static_cast<float>(static_cast<double>(place[2]) + position.z);
                }
                float rx = static_cast<float>(r.x * across), ry = static_cast<float>(r.y * across),
                      rz = static_cast<float>(r.z * across);
                float ux = static_cast<float>(u.x * up), uy = static_cast<float>(u.y * up),
                      uz = static_cast<float>(u.z * up);
                // The corners' colours (each its own of the colour arrays').
                sColourSpan span = ColourSpan(life);
                uint32_t colours[4];
                for (int k = 0; k < 4; ++k)
                    colours[k] = Colour(span.from[k], span.to[k], span.t);
                // The texture: all of it, or (UseRandomUVQuadrants) a quarter by the particle's index.
                float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
                if (flags & 0x4000) {
                    switch (index % 4) {
                    case 0: u0 = 0.5f, v0 = 0.5f; break;
                    case 1: v1 = 0.5f, u0 = 0.5f; break;
                    case 2: v1 = 0.5f, u1 = 0.5f; break;
                    case 3: u1 = 0.5f, v0 = 0.5f; break;
                    default: break;
                    }
                }
                double e0 = static_cast<double>(rx) + p[0], e1 = static_cast<double>(ry) + p[1],
                       e2 = static_cast<double>(rz) + p[2];
                float e0f = static_cast<float>(e0), e1f = static_cast<float>(e1), e2f = static_cast<float>(e2);
                double g0 = static_cast<double>(p[0]) - rx, g1 = static_cast<double>(p[1]) - ry,
                       g2 = static_cast<double>(p[2]) - rz;
                float g0f = static_cast<float>(g0), g1f = static_cast<float>(g1), g2f = static_cast<float>(g2);
                vertex[0] = {static_cast<float>(static_cast<double>(e0f) - ux),
                             static_cast<float>(static_cast<double>(e1f) - uy), static_cast<float>(e2 - uz), colours[0],
                             u0, v0};
                vertex[1] = {static_cast<float>(static_cast<double>(g0f) - ux),
                             static_cast<float>(static_cast<double>(g1f) - uy), static_cast<float>(g2 - uz), colours[1],
                             u1, v0};
                vertex[2] = {static_cast<float>(static_cast<double>(g0f) + ux),
                             static_cast<float>(static_cast<double>(g1f) + uy),
                             static_cast<float>(static_cast<double>(g2f) + uz), colours[2], u1, v1};
                vertex[3] = {static_cast<float>(static_cast<double>(e0f) + ux),
                             static_cast<float>(static_cast<double>(e1f) + uy),
                             static_cast<float>(static_cast<double>(e2f) + uz), colours[3], u0, v1};
                vertex += 4;
                vertices += 4;
                triangles += 2;
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                place += 3;
                if (index >= count)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, sizeof(sQuadVertex));
        if (vertices >= room - 4)
            break;
        Device()->SetStreamSource(0, buffer, 0, sizeof(sQuadVertex));
        Device()->SetIndices(QuadIndices());
        Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, vertices, 0, triangles);
        ++DrawCalls();
        DrawnTriangles() += triangles;
    }
}

// --- smooth ---

// 0x004C2B30
CPcSmoothParticles* CPcSmoothParticles::Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                                  int fixedAlpha, int segmentCount, float unknown)
{
    reinterpret_cast<CSmoothParticles*>(this)->CSmoothParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, unknown, 0);
    vtable = reinterpret_cast<void**>(0x0064A5C0);
    secondaryVtable = reinterpret_cast<void**>(0x0064A5BC);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 3, texture, blendMode,
                                                                          fixedAlpha, 0, 0)
                      : nullptr;
    return this;
}

// 0x004C2C00
void CPcSmoothParticles::DestroyBody() { DestroyParts(0x0064A5C0, 0x0064A5BC, material, &DestroyEngine<CSmoothParticles>); }

// 0x004C3500
CPcSmoothParticles* CPcSmoothParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C2B20: the same through the vtable at +0x0C.
static CPcSmoothParticles* __fastcall DestroySmoothSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcSmoothParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004C2C70
void CPcSmoothParticles::Render()
{
    if (count <= 0)
        return;
    Mth::Vector r, u;
    if (aligned)
        AlignedAxes(r, u);
    else
        CameraAxes(r, u);
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    SetPixelShader(DefaultPixelShader(), nullptr);
    sFlexibleParticle* particle = particles;
    const float* place = positions;
    int index = 0;
    int remaining = count * 12;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<sQuadVertex*>(
            Platform::LockDynamicVertices(remaining, sizeof(sQuadVertex), &buffer, &firstVertex, &room));
        while (index < count) {
            float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            double across = (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life +
                            particle->startSize[0];
            float up = static_cast<float>(
                (static_cast<double>(particle->endSize[1]) - particle->startSize[1]) * life + particle->startSize[1]);
            float p[3];
            if (emitterIndependent) {
                p[0] = place[0], p[1] = place[1], p[2] = place[2];
            } else {
                p[0] = static_cast<float>(static_cast<double>(position.x) + place[0]);
                p[1] = static_cast<float>(static_cast<double>(place[1]) + position.y);
                p[2] = static_cast<float>(static_cast<double>(place[2]) + position.z);
            }
            float rx = static_cast<float>(r.x * across), ry = static_cast<float>(r.y * across),
                  rz = static_cast<float>(r.z * across);
            double ux = static_cast<double>(u.x) * up;
            float uy = static_cast<float>(static_cast<double>(u.y) * up),
                  uz = static_cast<float>(static_cast<double>(u.z) * up);
            double g0 = static_cast<double>(p[0]) - rx, g1 = static_cast<double>(p[1]) - ry,
                   g2 = static_cast<double>(p[2]) - rz;
            float g0f = static_cast<float>(g0), g1f = static_cast<float>(g1), g2f = static_cast<float>(g2);
            double e0 = static_cast<double>(rx) + p[0], e1 = static_cast<double>(ry) + p[1];
            float e0f = static_cast<float>(e0), e1f = static_cast<float>(e1),
                  e2f = static_cast<float>(static_cast<double>(rz) + p[2]);
            // The four corners (each with a triangle to the centre).
            float k[4][3] = {
                {static_cast<float>(g0f + ux), static_cast<float>(static_cast<double>(g1f) + uy),
                 static_cast<float>(g2 + uz)},
                {static_cast<float>(e0f + ux), static_cast<float>(static_cast<double>(e1f) + uy),
                 static_cast<float>(static_cast<double>(e2f) + uz)},
                {static_cast<float>(e0f - ux), static_cast<float>(static_cast<double>(e1f) - uy),
                 static_cast<float>(static_cast<double>(e2f) - uz)},
                {static_cast<float>(g0f - ux), static_cast<float>(static_cast<double>(g1f) - uy),
                 static_cast<float>(static_cast<double>(g2f) - uz)}};
            static constexpr float kUv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            // The centre's colour and the edge's.
            sColourSpan span = ColourSpan(life);
            uint32_t colours[2];
            for (int i = 0; i < 2; ++i)
                colours[i] = Colour(span.from[i], span.to[i], span.t);
            for (int corner = 0; corner < 4; ++corner) {
                int next = (corner + 1) % 4;
                vertex[0] = {p[0], p[1], p[2], colours[0], 0.0f, 0.0f};
                vertex[1] = {k[corner][0], k[corner][1], k[corner][2], colours[1], kUv[corner][0], kUv[corner][1]};
                vertex[2] = {k[next][0], k[next][1], k[next][2], colours[1], kUv[next][0], kUv[next][1]};
                vertex += 3;
            }
            vertices += 12;
            triangles += 4;
            if (vertices >= room - 12)
                break;
            ++particle;
            ++index;
            place += 3;
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, sizeof(sQuadVertex));
        if (vertices >= room - 12)
            break;
        Device()->SetStreamSource(0, buffer, 0, sizeof(sQuadVertex));
        Device()->DrawPrimitive(D3DPT_TRIANGLELIST, firstVertex, triangles);
        ++DrawCalls();
        DrawnTriangles() += triangles;
    }
}

// --- smoothribbon ---

// 0x004C3550
CPcSmoothRibbonParticles* CPcSmoothRibbonParticles::Construct(uint32_t name, int maxParticles, uint32_t texture,
                                                              uint32_t blendMode, int fixedAlpha, int segmentCount,
                                                              float unknown, int history)
{
    reinterpret_cast<CSmoothRibbonParticles*>(this)->CSmoothRibbonParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, unknown, history);
    vtable = reinterpret_cast<void**>(0x0064A5D4);
    secondaryVtable = reinterpret_cast<void**>(0x0064A5D0);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 8, texture, blendMode,
                                                                          fixedAlpha, 0, 0)
                      : nullptr;
    return this;
}

// 0x004C3620
void CPcSmoothRibbonParticles::DestroyBody() { DestroyParts(0x0064A5D4, 0x0064A5D0, material, &DestroyEngine<CSmoothRibbonParticles>); }

// 0x004C3D40
CPcSmoothRibbonParticles* CPcSmoothRibbonParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C3540: the same through the vtable at +0x0C.
static CPcSmoothRibbonParticles* __fastcall DestroySmoothRibbonSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcSmoothRibbonParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004C3530
int CPcSmoothRibbonParticles::GetSegments() { return segments; }

// 0x004C3690
void CPcSmoothRibbonParticles::Render()
{
    if (count <= 0)
        return;
    double a = GameVariable<float>(0x00786BD8);
    double b = GameVariable<float>(0x00786BE8);
    double c = GameVariable<float>(0x00786BF8);
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    SetPixelShader(GameVariable<void*>(0x007CA95C), material->material);
    const float* head = positions;
    const float* tail = positions + 3 * (segments - 1) * historyStride;
    sFlexibleParticle* particle = particles;
    int index = 0;
    int remaining = count * 8;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<sTrailVertex*>(
            Platform::LockDynamicVertices(remaining, sizeof(sTrailVertex), &buffer, &firstVertex, &room));
        if (index < count) {
            for (uint32_t bytes = 0x20; bytes <= 0x7FF; bytes += 0x20) {
                float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                float h[3], t[3];
                if (emitterIndependent) {
                    h[0] = head[0], h[1] = head[1], h[2] = head[2];
                    t[0] = tail[0], t[1] = tail[1], t[2] = tail[2];
                } else {
                    for (int k = 0; k < 3; ++k) {
                        h[k] = static_cast<float>(static_cast<double>(head[k]) + (&position.x)[k]);
                        t[k] = static_cast<float>(static_cast<double>(tail[k]) + (&position.x)[k]);
                    }
                }
                float dx = static_cast<float>(static_cast<double>(t[0]) - h[0]),
                      dy = static_cast<float>(static_cast<double>(t[1]) - h[1]),
                      dz = static_cast<float>(static_cast<double>(t[2]) - h[2]);
                double n0 = dy * c - dz * b;
                double n1 = dz * a - c * dx;
                float n2 = static_cast<float>(b * dx - dy * a);
                double length = std::sqrt(n0 * n0 + static_cast<double>(n2) * n2 + n1 * n1);
                if (length > 0.0f) {
                    double inverse = 1.0f / length;
                    n0 *= inverse;
                    n1 *= inverse;
                    n2 = static_cast<float>(n2 * inverse);
                }
                float w = static_cast<float>((static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life +
                                             particle->startSize[0]);
                double n0w = n0 * w, n1w = n1 * w, n2w = static_cast<double>(n2) * w;
                float f0 = static_cast<float>(n0w), f1 = static_cast<float>(n1w), f2 = static_cast<float>(n2w);
                float plusHead[3] = {static_cast<float>(static_cast<double>(f0) + h[0]),
                                     static_cast<float>(static_cast<double>(f1) + h[1]), static_cast<float>(n2w + h[2])};
                float minusHead[3] = {static_cast<float>(static_cast<double>(h[0]) - f0),
                                      static_cast<float>(static_cast<double>(h[1]) - f1),
                                      static_cast<float>(static_cast<double>(h[2]) - f2)};
                float minusTail[3] = {static_cast<float>(static_cast<double>(t[0]) - f0),
                                      static_cast<float>(static_cast<double>(t[1]) - f1),
                                      static_cast<float>(static_cast<double>(t[2]) - f2)};
                float plusTail[3] = {static_cast<float>(static_cast<double>(f0) + t[0]),
                                     static_cast<float>(static_cast<double>(f1) + t[1]), static_cast<float>(n2w + t[2])};
                sColourSpan span = ColourSpan(life);
                uint32_t colours[4];
                for (int k = 0; k < 4; ++k)
                    colours[k] = Colour(span.from[k], span.to[k], span.t);
                // Two quads along the middle: to the plus side and to the minus side.
                vertex[0] = {h[0], h[1], h[2], colours[0]};
                vertex[1] = {t[0], t[1], t[2], colours[3]};
                vertex[2] = {plusTail[0], plusTail[1], plusTail[2], colours[2]};
                vertex[3] = {plusHead[0], plusHead[1], plusHead[2], colours[1]};
                vertex[4] = {h[0], h[1], h[2], colours[0]};
                vertex[5] = {t[0], t[1], t[2], colours[3]};
                vertex[6] = {minusTail[0], minusTail[1], minusTail[2], colours[2]};
                vertex[7] = {minusHead[0], minusHead[1], minusHead[2], colours[1]};
                vertex += 8;
                vertices += 8;
                triangles += 4;
                if (vertices >= room - 8)
                    break;
                ++particle;
                ++index;
                head += 3;
                tail += 3;
                if (index >= count)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, sizeof(sTrailVertex));
        if (vertices >= room - 8)
            break;
        Device()->SetStreamSource(0, buffer, 0, sizeof(sTrailVertex));
        Device()->SetIndices(QuadIndices());
        Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, vertices, 0, triangles);
        ++DrawCalls();
        DrawnTriangles() += triangles;
    }
}

// --- smoothstar ---

// 0x004C3D80
CPcSmoothStarParticles* CPcSmoothStarParticles::Construct(uint32_t name, int maxParticles, uint32_t texture,
                                                          uint32_t blendMode, int fixedAlpha, int segmentCount,
                                                          float unknown)
{
    reinterpret_cast<CSmoothStarParticles*>(this)->CSmoothStarParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, unknown, 0);
    vtable = reinterpret_cast<void**>(0x0064A5EC);
    secondaryVtable = reinterpret_cast<void**>(0x0064A5E8);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 6, texture, blendMode,
                                                                          fixedAlpha, segmentCount, 0)
                      : nullptr;
    return this;
}

// 0x004C3E50
void CPcSmoothStarParticles::DestroyBody() { DestroyParts(0x0064A5EC, 0x0064A5E8, material, &DestroyEngine<CSmoothStarParticles>); }

// 0x004C4880
CPcSmoothStarParticles* CPcSmoothStarParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C3D70: the same through the vtable at +0x0C.
static CPcSmoothStarParticles* __fastcall DestroySmoothStarSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcSmoothStarParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004C3EC0
void CPcSmoothStarParticles::Render()
{
    if (count <= 0)
        return;
    Mth::Vector r, u;
    CameraAxes(r, u);
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    SetPixelShader(GameVariable<void*>(0x007CA95C), material->material);
    sFlexibleParticle* particle = particles;
    const float* place = positions;
    int index = 0;
    int remaining = segments * count * 8;
    constexpr float kDegrees = 0.0174532924f; // (0x00645464)
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<sTrailVertex*>(
            Platform::LockDynamicVertices(remaining, sizeof(sTrailVertex), &buffer, &firstVertex, &room));
        if (index < count) {
            float sin0 = static_cast<float>(X87::Sin(0.0)), cos0 = static_cast<float>(X87::Cos(0.0));
            for (;;) {
                float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                double across = (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life +
                                particle->startSize[0];
                double upSize = (static_cast<double>(particle->endSize[1]) - particle->startSize[1]) * life +
                                particle->startSize[1];
                float p[3];
                if (emitterIndependent) {
                    p[0] = place[0], p[1] = place[1], p[2] = place[2];
                } else {
                    p[0] = static_cast<float>(static_cast<double>(position.x) + place[0]);
                    p[1] = static_cast<float>(static_cast<double>(place[1]) + position.y);
                    p[2] = static_cast<float>(static_cast<double>(place[2]) + position.z);
                }
                float rv[3] = {static_cast<float>(r.x * across), static_cast<float>(r.y * across),
                               static_cast<float>(r.z * across)};
                float uv[3] = {static_cast<float>(u.x * upSize), static_cast<float>(u.y * upSize),
                               static_cast<float>(u.z * upSize)};
                // The centre's, the points' and the edge's colours.
                sColourSpan span = ColourSpan(life);
                uint32_t colours[3];
                for (int k = 0; k < 3; ++k)
                    colours[k] = Colour(span.from[k], span.to[k], span.t);
                double w = starWidth;
                // The outer point at angle 0 (the previous one for the first segment).
                float outer[3];
                {
                    double x = static_cast<double>(rv[0]) * sin0;
                    double y = static_cast<double>(rv[1]) * sin0;
                    float z = static_cast<float>(static_cast<double>(rv[2]) * sin0);
                    float ox = static_cast<float>(static_cast<double>(static_cast<float>(x * w)) + p[0]);
                    float oy = static_cast<float>(y * w + p[1]);
                    double oz = static_cast<double>(z) * w + p[2];
                    double ux = static_cast<double>(cos0) * uv[0];
                    double uy = static_cast<double>(uv[1]) * cos0;
                    float uz = static_cast<float>(static_cast<double>(uv[2]) * cos0);
                    outer[0] = static_cast<float>(static_cast<double>(static_cast<float>(ux * w)) + ox);
                    outer[1] = static_cast<float>(uy * w + oy);
                    outer[2] = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(uz) * w)) + oz);
                }
                for (int i = 0, k = 1; i < segments; ++i, k += 2) {
                    float step = static_cast<float>(360.0f / (static_cast<double>(segments) + segments));
                    // The point between this segment's two outer points, out at the star's width.
                    double angle1 = static_cast<double>(k) * step * kDegrees;
                    double s1 = X87::Sin(angle1), c1 = X87::Cos(angle1);
                    float s1f = static_cast<float>(s1);
                    float tip[3];
                    {
                        double x = rv[0] * s1 * w;
                        double y = static_cast<double>(static_cast<float>(rv[1] * s1)) * w;
                        float z = static_cast<float>(static_cast<double>(static_cast<float>(rv[2] * s1)) * w);
                        tip[0] = static_cast<float>(x + p[0]);
                        tip[1] = static_cast<float>(y + p[1]);
                        tip[2] = static_cast<float>(static_cast<double>(z) + p[2]);
                        double ux = uv[0] * c1 * w;
                        double uy = static_cast<double>(static_cast<float>(uv[1] * c1)) * w;
                        float uz = static_cast<float>(static_cast<double>(static_cast<float>(uv[2] * c1)) * w);
                        tip[0] = static_cast<float>(ux + tip[0]);
                        tip[1] = static_cast<float>(uy + tip[1]);
                        tip[2] = static_cast<float>(static_cast<double>(uz) + tip[2]);
                    }
                    // The next outer point.
                    double angle2 = static_cast<double>(k + 1) * step * kDegrees;
                    double s2 = X87::Sin(angle2), c2 = X87::Cos(angle2);
                    float next[3];
                    double nextY;
                    {
                        double x = rv[0] * s2 * w;
                        double y = static_cast<double>(static_cast<float>(rv[1] * s2)) * w;
                        float z = static_cast<float>(static_cast<double>(static_cast<float>(rv[2] * s2)) * w);
                        float nx = static_cast<float>(x + p[0]);
                        double ny = y + p[1];
                        float nz = static_cast<float>(static_cast<double>(z) + p[2]);
                        double ux = uv[0] * c2 * w;
                        float uy = static_cast<float>(static_cast<double>(static_cast<float>(uv[1] * c2)) * w);
                        float uz = static_cast<float>(static_cast<double>(static_cast<float>(uv[2] * c2)) * w);
                        next[0] = static_cast<float>(ux + nx);
                        nextY = static_cast<double>(uy) + ny;
                        next[1] = static_cast<float>(nextY);
                        next[2] = static_cast<float>(static_cast<double>(uz) + nz);
                    }
                    // The inner point at angle 1 (unscaled).
                    float inner[3];
                    {
                        double x = static_cast<double>(rv[0]) * s1f + p[0];
                        float ry = static_cast<float>(static_cast<double>(rv[1]) * s1f);
                        float rz = static_cast<float>(static_cast<double>(rv[2]) * s1f);
                        float iy = static_cast<float>(static_cast<double>(ry) + p[1]);
                        float iz = static_cast<float>(static_cast<double>(rz) + p[2]);
                        float ux = static_cast<float>(uv[0] * c1), uy = static_cast<float>(uv[1] * c1),
                              uz = static_cast<float>(uv[2] * c1);
                        inner[0] = static_cast<float>(static_cast<double>(ux) + x);
                        inner[1] = static_cast<float>(static_cast<double>(uy) + iy);
                        inner[2] = static_cast<float>(static_cast<double>(uz) + iz);
                    }
                    vertex[0] = {tip[0], tip[1], tip[2], colours[1]};
                    vertex[1] = {p[0], p[1], p[2], colours[0]};
                    vertex[2] = {next[0], next[1], next[2], colours[2]};
                    vertex[3] = {inner[0], inner[1], inner[2], colours[2]};
                    vertex[4] = {tip[0], tip[1], tip[2], colours[1]};
                    vertex[5] = {p[0], p[1], p[2], colours[0]};
                    vertex[6] = {outer[0], outer[1], outer[2], colours[2]};
                    vertex[7] = {inner[0], inner[1], inner[2], colours[2]};
                    vertex += 8;
                    std::memcpy(outer, next, sizeof(next));
                    vertices += 8;
                    triangles += 4;
                    if (vertices >= room - 8)
                        break;
                }
                if (vertices >= room - 8)
                    break;
                ++index;
                ++particle;
                place += 3;
                if (index >= count)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, sizeof(sTrailVertex));
        if (vertices >= room - 8)
            break;
        Device()->SetStreamSource(0, buffer, 0, sizeof(sTrailVertex));
        Device()->SetIndices(QuadIndices());
        Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, vertices, 0, triangles);
        ++DrawCalls();
        DrawnTriangles() += triangles;
    }
}

// --- star ---

// 0x004C48C0
CPcStarParticles* CPcStarParticles::Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                              int fixedAlpha, int segmentCount, float unknown)
{
    reinterpret_cast<CStarParticles*>(this)->CStarParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, unknown, 0);
    vtable = reinterpret_cast<void**>(0x0064A604);
    secondaryVtable = reinterpret_cast<void**>(0x0064A600);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 5, texture, blendMode,
                                                                          fixedAlpha, segmentCount, 0)
                      : nullptr;
    return this;
}

// 0x004C4990
void CPcStarParticles::DestroyBody() { DestroyParts(0x0064A604, 0x0064A600, material, &DestroyEngine<CStarParticles>); }

// 0x004C5200
CPcStarParticles* CPcStarParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004C48B0: the same through the vtable at +0x0C.
static CPcStarParticles* __fastcall DestroyStarSecondary(uint8_t* secondary, int, unsigned int freeFlags)
{
    return reinterpret_cast<CPcStarParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004C4A00
void CPcStarParticles::Render()
{
    if (count <= 0)
        return;
    Mth::Vector r, u;
    CameraAxes(r, u);
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    SetPixelShader(GameVariable<void*>(0x007CA95C), material->material);
    sFlexibleParticle* particle = particles;
    const float* place = positions;
    int index = 0;
    int remaining = segments * count * 4;
    constexpr float kDegrees = 0.0174532924f; // (0x00645464)
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t firstVertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<sTrailVertex*>(
            Platform::LockDynamicVertices(remaining, sizeof(sTrailVertex), &buffer, &firstVertex, &room));
        if (index < count) {
            float sin0 = static_cast<float>(X87::Sin(0.0)), cos0 = static_cast<float>(X87::Cos(0.0));
            for (;;) {
                float life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                double across = (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life +
                                particle->startSize[0];
                double upSize = (static_cast<double>(particle->endSize[1]) - particle->startSize[1]) * life +
                                particle->startSize[1];
                float p[3];
                if (emitterIndependent) {
                    p[0] = place[0], p[1] = place[1], p[2] = place[2];
                } else {
                    p[0] = static_cast<float>(static_cast<double>(position.x) + place[0]);
                    p[1] = static_cast<float>(static_cast<double>(place[1]) + position.y);
                    p[2] = static_cast<float>(static_cast<double>(place[2]) + position.z);
                }
                float rv[3] = {static_cast<float>(r.x * across), static_cast<float>(r.y * across),
                               static_cast<float>(r.z * across)};
                float uv[3] = {static_cast<float>(u.x * upSize), static_cast<float>(u.y * upSize),
                               static_cast<float>(u.z * upSize)};
                sColourSpan span = ColourSpan(life);
                uint32_t colours[3];
                for (int k = 0; k < 3; ++k)
                    colours[k] = Colour(span.from[k], span.to[k], span.t);
                double w = starWidth;
                // The outer point at angle 0 (the previous one for the first segment).
                float outer[3];
                {
                    double x = static_cast<double>(rv[0]) * sin0;
                    double y = static_cast<double>(rv[1]) * sin0;
                    float z = static_cast<float>(static_cast<double>(rv[2]) * sin0);
                    float ox = static_cast<float>(static_cast<double>(static_cast<float>(x * w)) + p[0]);
                    float oy = static_cast<float>(y * w + p[1]);
                    double oz = static_cast<double>(z) * w + p[2];
                    double ux = static_cast<double>(cos0) * uv[0];
                    double uy = static_cast<double>(uv[1]) * cos0;
                    float uz = static_cast<float>(static_cast<double>(uv[2]) * cos0);
                    outer[0] = static_cast<float>(static_cast<double>(static_cast<float>(ux * w)) + ox);
                    outer[1] = static_cast<float>(uy * w + oy);
                    outer[2] = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(uz) * w)) + oz);
                }
                for (int k = 1; k <= segments; ++k) {
                    // The next outer point (out at the width) and the inner one between (unscaled).
                    double angleA = 360.0f / static_cast<double>(segments) * k * kDegrees;
                    double sA = X87::Sin(angleA), cA = X87::Cos(angleA);
                    float next[3];
                    {
                        double x = rv[0] * sA * w;
                        double y = static_cast<double>(static_cast<float>(rv[1] * sA)) * w;
                        float z = static_cast<float>(static_cast<double>(static_cast<float>(rv[2] * sA)) * w);
                        float nx = static_cast<float>(x + p[0]);
                        double ny = y + p[1];
                        float nz = static_cast<float>(static_cast<double>(z) + p[2]);
                        double ux = uv[0] * cA * w;
                        float uy = static_cast<float>(static_cast<double>(static_cast<float>(uv[1] * cA)) * w);
                        float uz = static_cast<float>(static_cast<double>(static_cast<float>(uv[2] * cA)) * w);
                        next[0] = static_cast<float>(ux + nx);
                        next[1] = static_cast<float>(static_cast<double>(uy) + ny);
                        next[2] = static_cast<float>(static_cast<double>(uz) + nz);
                    }
                    double angleB = 360.0f / (static_cast<double>(segments) + segments) * (2 * k - 1) * kDegrees;
                    double sB = X87::Sin(angleB), cB = X87::Cos(angleB);
                    float inner[3];
                    {
                        double x = rv[0] * sB + p[0];
                        float iy = static_cast<float>(static_cast<double>(static_cast<float>(rv[1] * sB)) + p[1]);
                        float iz = static_cast<float>(static_cast<double>(static_cast<float>(rv[2] * sB)) + p[2]);
                        float ux = static_cast<float>(uv[0] * cB), uy = static_cast<float>(uv[1] * cB),
                              uz = static_cast<float>(uv[2] * cB);
                        inner[0] = static_cast<float>(static_cast<double>(ux) + x);
                        inner[1] = static_cast<float>(static_cast<double>(uy) + iy);
                        inner[2] = static_cast<float>(static_cast<double>(uz) + iz);
                    }
                    vertex[0] = {p[0], p[1], p[2], colours[0]};
                    vertex[1] = {outer[0], outer[1], outer[2], colours[1]};
                    vertex[2] = {inner[0], inner[1], inner[2], colours[2]};
                    vertex[3] = {next[0], next[1], next[2], colours[1]};
                    vertex += 4;
                    std::memcpy(outer, next, sizeof(next));
                    vertices += 4;
                    triangles += 2;
                    if (vertices >= room - 4)
                        break;
                }
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                place += 3;
                if (index >= count)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        Platform::UnlockDynamicVertices(vertices, sizeof(sTrailVertex));
        if (vertices >= room - 4)
            break;
        Device()->SetStreamSource(0, buffer, 0, sizeof(sTrailVertex));
        Device()->SetIndices(QuadIndices());
        Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, vertices, 0, triangles);
        ++DrawCalls();
        DrawnTriangles() += triangles;
    }
}

// --- glowribbontrail ---

// The colour `t` of the way from `from` to `to` in 4096ths (RGBA bytes in, BGRA out), not scaled.
uint32_t CPcGlowRibbonTrailParticles::FineColour(uint32_t from, uint32_t to, double t)
{
    const auto* f = reinterpret_cast<const uint8_t*>(&from);
    const auto* g = reinterpret_cast<const uint8_t*>(&to);
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(Truncate((static_cast<double>(g[k] - f[k]) * t + (f[k] << 12)) * (1.0f / 4096.0f)));
    };
    uint8_t bytes[4] = {lerp(2), lerp(1), lerp(0), lerp(3)};
    uint32_t colour;
    std::memcpy(&colour, bytes, 4);
    return colour;
}

// 0x004BF720
CPcGlowRibbonTrailParticles* CPcGlowRibbonTrailParticles::Construct(uint32_t name, int maxParticles,
                                                                    uint32_t texture, uint32_t blendMode,
                                                                    int fixedAlpha, int segmentCount, float unknown,
                                                                    int history)
{
    reinterpret_cast<CGlowRibbonTrailParticles*>(this)->CGlowRibbonTrailParticles::Construct(name, maxParticles, texture, blendMode, fixedAlpha, segmentCount, unknown, history);
    vtable = reinterpret_cast<void**>(0x0064A558);
    secondaryVtable = reinterpret_cast<void**>(0x0064A554);
    void* memory = Mem::New(sizeof(sSimpleMaterial));
    material = memory ? static_cast<sSimpleMaterial*>(memory)->Construct(maxParticles, 10, texture, blendMode,
                                                                          fixedAlpha, segmentCount, history)
                      : nullptr;
    return this;
}

// 0x004C0940
void CPcGlowRibbonTrailParticles::DestroyBody() { DestroyParts(0x0064A558, 0x0064A554, material, &DestroyEngine<CGlowRibbonTrailParticles>); }

// 0x004C09B0
CPcGlowRibbonTrailParticles* CPcGlowRibbonTrailParticles::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004BF710: the same through the vtable at +0x0C.
static CPcGlowRibbonTrailParticles* __fastcall DestroyGlowRibbonTrailSecondary(uint8_t* secondary, int,
                                                                              unsigned int freeFlags)
{
    return reinterpret_cast<CPcGlowRibbonTrailParticles*>(secondary - 0xC)->Destroy(freeFlags);
}

// 0x004BF7F0
void CPcGlowRibbonTrailParticles::Render()
{
    if (count <= 0)
        return;
    double a = GameVariable<float>(0x00786BD8);
    double b = GameVariable<float>(0x00786BE8);
    double c = GameVariable<float>(0x00786BF8);
    // The camera's across and up directions, not normalised.
    double b0 = b * 0.0f;
    float r[3] = {static_cast<float>(b0 - c), static_cast<float>(c * 0.0f - a * 0.0f), static_cast<float>(a - b0)};
    float u[3] = {static_cast<float>(r[1] * c - r[2] * b), static_cast<float>(a * r[2] - r[0] * c),
                  static_cast<float>(r[0] * b - a * r[1])};
    uint32_t perParticle = segments * 9 * 4 + (historyLength - 1) * 3 * 8;
    uint32_t remaining = count * perParticle;
    SetMaterial(material->material);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    SetPixelShader(GameVariable<void*>(0x007CA95C), material->material);
    IDirect3DVertexBuffer9* buffer = nullptr;
    uint32_t firstVertex = 0, room = 0;
    uint32_t totalVertices = (segments * 9 + (historyLength * 3 - 3) * 2) * count;
    uint32_t totalTriangles = (segments * 3 - 2 + historyLength * 2) * count;
    auto* vertex = static_cast<sTrailVertex*>(
        Platform::LockDynamicVertices(totalVertices, sizeof(sTrailVertex), &buffer, &firstVertex, &room));
    sFlexibleParticle* particle = particles;
    int index = 0;
    while (remaining > 0) {
        for (; index < count; ++index, ++particle) {
            double life = static_cast<double>(particle->age) / particle->lifetime;
            const float* p = positions + 3 * index;
            float p0[3] = {p[0], p[1], p[2]}, p1[3];
            const float* q = positions + 3 * (historyStride + index);
            p1[0] = q[0], p1[1] = q[1], p1[2] = q[2];
            // Across the trail's first segment, normalised (unrounded).
            double n[3];
            {
                float dx = static_cast<float>(static_cast<double>(p1[0]) - p0[0]),
                      dy = static_cast<float>(static_cast<double>(p1[1]) - p0[1]),
                      dz = static_cast<float>(static_cast<double>(p1[2]) - p0[2]);
                n[0] = dy * c - dz * b;
                n[1] = a * dz - dx * c;
                n[2] = dx * b - a * dy;
                double length = std::sqrt(n[0] * n[0] + n[2] * n[2] + n[1] * n[1]);
                if (length > 0.0f) {
                    double inverse = 1.0f / length;
                    n[0] *= inverse;
                    n[1] *= inverse;
                    n[2] *= inverse;
                }
            }
            float size0 = static_cast<float>(
                (static_cast<double>(particle->endSize[0]) - particle->startSize[0]) * life + particle->startSize[0]);
            float size1 = static_cast<float>(
                (static_cast<double>(particle->endSize[1]) - particle->startSize[1]) * life + particle->startSize[1]);
            // The colours (unrounded fraction): the trail's from index 3 on.
            const uint32_t* from = startColour;
            const uint32_t* to = endColour;
            double t = life;
            if (colourMid >= 0.0f) {
                if (life < colourMid) {
                    t = life / colourMid;
                    to = midColour;
                } else {
                    t = (life - colourMid) / (1.0f - static_cast<double>(colourMid));
                    from = midColour;
                }
            }
            uint32_t previousColour = FineColour(from[3], to[3], t);
            from += 4;
            to += 4;
            double w = width;
            // The head end's edge points.
            float previousA[3], previousB[3];
            {
                double n0s = n[0] * size0, n1s = n[1] * size0;
                float f0 = static_cast<float>(n0s), f2 = static_cast<float>(n[2] * size0);
                float o0 = static_cast<float>(static_cast<double>(f0) * w), o1 = static_cast<float>(w * n1s),
                      o2 = static_cast<float>(w * static_cast<double>(f2));
                previousA[0] = static_cast<float>(static_cast<double>(o0) + p0[0]);
                previousA[1] = static_cast<float>(static_cast<double>(o1) + p0[1]);
                previousA[2] = static_cast<float>(static_cast<double>(o2) + p0[2]);
                previousB[0] = static_cast<float>(static_cast<double>(p0[0]) - o0);
                previousB[1] = static_cast<float>(static_cast<double>(p0[1]) - o1);
                previousB[2] = static_cast<float>(static_cast<double>(p0[2]) - o2);
            }
            for (int k = 1; k < historyLength; ++k) {
                uint32_t colour = FineColour(*from++, *to++, t);
                if (k > 1) {
                    const float* next = positions + 3 * (historyStride * k + index);
                    p1[0] = next[0], p1[1] = next[1], p1[2] = next[2];
                    double dx = static_cast<double>(p1[0]) - p0[0], dy = static_cast<double>(p1[1]) - p0[1],
                           dz = static_cast<double>(p1[2]) - p0[2];
                    float m0 = static_cast<float>(dy * c - dz * b);
                    float m1 = static_cast<float>(a * dz - dx * c);
                    float m2 = static_cast<float>(dx * b - a * dy);
                    double length = std::sqrt(static_cast<double>(m2) * m2 + static_cast<double>(m1) * m1 +
                                              static_cast<double>(m0) * m0);
                    if (length > 0.0f) {
                        float inverse = static_cast<float>(1.0f / static_cast<double>(static_cast<float>(length)));
                        n[0] = static_cast<double>(m0) * inverse;
                        n[1] = static_cast<double>(inverse) * m1;
                        n[2] = static_cast<double>(inverse) * m2;
                    } else {
                        n[0] = m0, n[1] = m1, n[2] = m2;
                    }
                }
                double n0s = n[0] * size0, n1s = n[1] * size0;
                float f0 = static_cast<float>(n0s), f2 = static_cast<float>(n[2] * size0);
                float o0 = static_cast<float>(static_cast<double>(f0) * w), o1 = static_cast<float>(w * n1s),
                      o2 = static_cast<float>(w * static_cast<double>(f2));
                float nextC[3] = {static_cast<float>(static_cast<double>(o0) + p1[0]),
                                  static_cast<float>(static_cast<double>(o1) + p1[1]),
                                  static_cast<float>(w * static_cast<double>(f2) + p1[2])};
                float nextD[3] = {static_cast<float>(static_cast<double>(p1[0]) - o0),
                                  static_cast<float>(static_cast<double>(p1[1]) - o1),
                                  static_cast<float>(static_cast<double>(p1[2]) - o2)};
                vertex[0] = {previousA[0], previousA[1], previousA[2], previousColour};
                vertex[1] = {previousB[0], previousB[1], previousB[2], previousColour};
                vertex[2] = {nextC[0], nextC[1], nextC[2], colour};
                vertex[3] = {previousB[0], previousB[1], previousB[2], previousColour};
                vertex[4] = {nextD[0], nextD[1], nextD[2], colour};
                vertex[5] = {nextC[0], nextC[1], nextC[2], colour};
                vertex += 6;
                std::memcpy(previousA, nextC, sizeof(nextC));
                std::memcpy(previousB, nextD, sizeof(nextD));
                std::memcpy(p0, p1, sizeof(p1));
                previousColour = colour;
            }

            // The glow about the head: its colours from the colour fraction gone through the selection again.
            std::memcpy(p0, p, sizeof(p0));
            float rr[3] = {static_cast<float>(static_cast<double>(r[0]) * size0),
                           static_cast<float>(static_cast<double>(r[1]) * size0),
                           static_cast<float>(static_cast<double>(r[2]) * size0)};
            float uu[3] = {static_cast<float>(static_cast<double>(u[0]) * size1),
                           static_cast<float>(static_cast<double>(u[1]) * size1),
                           static_cast<float>(static_cast<double>(u[2]) * size1)};
            const uint32_t* glowFrom = startColour;
            const uint32_t* glowTo = endColour;
            if (colourMid >= 0.0f) {
                if (t < colourMid) {
                    t = t / colourMid;
                    glowTo = midColour;
                } else {
                    t = (t - colourMid) / (1.0f - static_cast<double>(colourMid));
                    glowFrom = midColour;
                }
            }
            uint32_t glow[3];
            for (int k = 0; k < 3; ++k)
                glow[k] = FineColour(glowFrom[k], glowTo[k], t);
            float outer[3], inner[3];
            {
                double r0 = static_cast<double>(rr[0]) * 0.0f, r1 = static_cast<double>(rr[1]) * 0.0f,
                       r2 = static_cast<double>(rr[2]) * 0.0f;
                double x = static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(r0)) * w)) + p0[0];
                outer[1] = static_cast<float>(static_cast<double>(static_cast<float>(w * r1)) + p0[1]);
                outer[2] = static_cast<float>(w * r2 + p0[2]);
                outer[0] = static_cast<float>(x + static_cast<float>(static_cast<double>(uu[0]) * w));
                outer[1] = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(uu[1]) * w)) + outer[1]);
                outer[2] = static_cast<float>(static_cast<double>(static_cast<float>(w * uu[2])) + outer[2]);
                inner[0] = static_cast<float>(static_cast<double>(p0[0]) + static_cast<float>(r0));
                inner[1] = static_cast<float>(r1 + p0[1]);
                double z = static_cast<double>(static_cast<float>(r2)) + p0[2];
                inner[0] = static_cast<float>(static_cast<double>(uu[0]) + inner[0]);
                inner[1] = static_cast<float>(static_cast<double>(uu[1]) + inner[1]);
                inner[2] = static_cast<float>(static_cast<double>(uu[2]) + z);
            }
            for (int i = 1; i <= segments; ++i) {
                double angle = static_cast<double>(i) * glowStep * 0.0174532924f;
                float s = static_cast<float>(X87::Sin(angle)), cs = static_cast<float>(X87::Cos(angle));
                double r0s = static_cast<double>(rr[0]) * s, r1s = static_cast<double>(s) * rr[1],
                       r2s = static_cast<double>(s) * rr[2];
                float nextOuter[3], nextInner[3];
                double x = static_cast<double>(p0[0]) + static_cast<float>(static_cast<double>(static_cast<float>(r0s)) * w);
                nextOuter[1] = static_cast<float>(static_cast<double>(static_cast<float>(r1s * w)) + p0[1]);
                nextOuter[2] = static_cast<float>(w * r2s + p0[2]);
                double u0c = static_cast<double>(uu[0]) * cs, u1c = static_cast<double>(cs) * uu[1];
                float u2c = static_cast<float>(static_cast<double>(cs) * uu[2]);
                nextOuter[0] = static_cast<float>(x + static_cast<float>(u0c * w));
                nextOuter[1] = static_cast<float>(static_cast<double>(static_cast<float>(u1c * w)) + nextOuter[1]);
                nextOuter[2] = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(u2c) * w)) + nextOuter[2]);
                float ix = static_cast<float>(static_cast<double>(p0[0]) + static_cast<float>(r0s));
                float iy = static_cast<float>(r1s + p0[1]);
                double iz = static_cast<double>(static_cast<float>(r2s)) + p0[2];
                nextInner[0] = static_cast<float>(static_cast<double>(static_cast<float>(u0c)) + ix);
                nextInner[1] = static_cast<float>(static_cast<double>(static_cast<float>(u1c)) + iy);
                nextInner[2] = static_cast<float>(static_cast<double>(u2c) + iz);
                vertex[0] = {p0[0], p0[1], p0[2], glow[0]};
                vertex[1] = {outer[0], outer[1], outer[2], glow[1]};
                vertex[2] = {nextOuter[0], nextOuter[1], nextOuter[2], glow[1]};
                vertex[3] = {outer[0], outer[1], outer[2], glow[1]};
                vertex[4] = {nextOuter[0], nextOuter[1], nextOuter[2], glow[1]};
                vertex[5] = {inner[0], inner[1], inner[2], glow[2]};
                vertex[6] = {nextOuter[0], nextOuter[1], nextOuter[2], glow[1]};
                vertex[7] = {inner[0], inner[1], inner[2], glow[2]};
                vertex[8] = {nextInner[0], nextInner[1], nextInner[2], glow[2]};
                vertex += 9;
                std::memcpy(outer, nextOuter, sizeof(outer));
                std::memcpy(inner, nextInner, sizeof(inner));
            }
            remaining -= perParticle;
        }
    }
    Device()->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    Platform::UnlockDynamicVertices(totalVertices, sizeof(sTrailVertex));
    Device()->SetStreamSource(0, buffer, 0, sizeof(sTrailVertex));
    Device()->DrawPrimitive(D3DPT_TRIANGLELIST, firstVertex, totalTriangles);
    Device()->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    ++DrawCalls();
    DrawnTriangles() += totalTriangles;
}

} // namespace Nx

REPLACE(0x004BD470, &Nx::CPcFlexibleParticles::LerpColour);
REPLACE(0x004BCEF0, Nx::CreateFlexibleParticles);
REPLACE(0x004BD3A0, &Nx::CPcNewFlatParticles::Construct);
REPLACE(0x004BD610, &Nx::CPcNewFlatParticles::DestroyBody);
REPLACE(0x004BDC40, &Nx::CPcNewFlatParticles::Destroy);
REPLACE(0x004BD390, Nx::DestroyNewFlatSecondary);
REPLACE(0x004BD680, &Nx::CPcNewFlatParticles::Render);
REPLACE(0x004BDC80, &Nx::CPcFlatRibbonParticles::Construct);
REPLACE(0x004BDD60, &Nx::CPcFlatRibbonParticles::DestroyBody);
REPLACE(0x004BECD0, &Nx::CPcFlatRibbonParticles::Destroy);
REPLACE(0x004BDC70, Nx::DestroyFlatRibbonSecondary);
REPLACE(0x004BDDD0, &Nx::CPcFlatRibbonParticles::Render);
REPLACE(0x004BED10, &Nx::CPcGlowParticles::Construct);
REPLACE(0x004BEDE0, &Nx::CPcGlowParticles::DestroyBody);
REPLACE(0x004BF6E0, &Nx::CPcGlowParticles::Destroy);
REPLACE(0x004BED00, Nx::DestroyGlowSecondary);
REPLACE(0x004BEE50, &Nx::CPcGlowParticles::Render);
REPLACE(0x004C09F0, &Nx::CPcLineParticles::Construct);
REPLACE(0x004C0AC0, &Nx::CPcLineParticles::DestroyBody);
REPLACE(0x004C0E70, &Nx::CPcLineParticles::Destroy);
REPLACE(0x004C09E0, Nx::DestroyLineSecondary);
REPLACE(0x004C0B30, &Nx::CPcLineParticles::Render);
REPLACE(0x004C0EB0, &Nx::CPcRibbonParticles::Construct);
REPLACE(0x004C0F80, &Nx::CPcRibbonParticles::DestroyBody);
REPLACE(0x004C16E0, &Nx::CPcRibbonParticles::Destroy);
REPLACE(0x004C0EA0, Nx::DestroyRibbonSecondary);
REPLACE(0x004C0FF0, &Nx::CPcRibbonParticles::Render);
REPLACE(0x004C1720, &Nx::CPcRibbonTrailParticles::Construct);
REPLACE(0x004C17F0, &Nx::CPcRibbonTrailParticles::DestroyBody);
REPLACE(0x004C2160, &Nx::CPcRibbonTrailParticles::Destroy);
REPLACE(0x004C1710, Nx::DestroyRibbonTrailSecondary);
REPLACE(0x004C1860, &Nx::CPcRibbonTrailParticles::Render);
REPLACE(0x004C21A0, &Nx::CPcShadedParticles::Construct);
REPLACE(0x004C2260, &Nx::CPcShadedParticles::DestroyBody);
REPLACE(0x004C2AF0, &Nx::CPcShadedParticles::Destroy);
REPLACE(0x004C2190, Nx::DestroyShadedSecondary);
REPLACE(0x004C22D0, &Nx::CPcShadedParticles::Render);
REPLACE(0x004C2B30, &Nx::CPcSmoothParticles::Construct);
REPLACE(0x004C2C00, &Nx::CPcSmoothParticles::DestroyBody);
REPLACE(0x004C3500, &Nx::CPcSmoothParticles::Destroy);
REPLACE(0x004C2B20, Nx::DestroySmoothSecondary);
REPLACE(0x004C2C70, &Nx::CPcSmoothParticles::Render);
REPLACE(0x004C3550, &Nx::CPcSmoothRibbonParticles::Construct);
REPLACE(0x004C3620, &Nx::CPcSmoothRibbonParticles::DestroyBody);
REPLACE(0x004C3D40, &Nx::CPcSmoothRibbonParticles::Destroy);
REPLACE(0x004C3540, Nx::DestroySmoothRibbonSecondary);
REPLACE(0x004C3690, &Nx::CPcSmoothRibbonParticles::Render);
REPLACE(0x004C3D80, &Nx::CPcSmoothStarParticles::Construct);
REPLACE(0x004C3E50, &Nx::CPcSmoothStarParticles::DestroyBody);
REPLACE(0x004C4880, &Nx::CPcSmoothStarParticles::Destroy);
REPLACE(0x004C3D70, Nx::DestroySmoothStarSecondary);
REPLACE(0x004C3EC0, &Nx::CPcSmoothStarParticles::Render);
REPLACE(0x004C48C0, &Nx::CPcStarParticles::Construct);
REPLACE(0x004C4990, &Nx::CPcStarParticles::DestroyBody);
REPLACE(0x004C5200, &Nx::CPcStarParticles::Destroy);
REPLACE(0x004C48B0, Nx::DestroyStarSecondary);
REPLACE(0x004C4A00, &Nx::CPcStarParticles::Render);
REPLACE(0x004C3530, &Nx::CPcSmoothRibbonParticles::GetSegments);
REPLACE(0x004BF720, &Nx::CPcGlowRibbonTrailParticles::Construct);
REPLACE(0x004C0940, &Nx::CPcGlowRibbonTrailParticles::DestroyBody);
REPLACE(0x004C09B0, &Nx::CPcGlowRibbonTrailParticles::Destroy);
REPLACE(0x004BF710, Nx::DestroyGlowRibbonTrailSecondary);
REPLACE(0x004BF7F0, &Nx::CPcGlowRibbonTrailParticles::Render);
