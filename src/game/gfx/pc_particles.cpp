// The PC particle systems: see pc_particles.h.

#include "game/gfx/pc_particles.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/material.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/particles.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "game/gfx/texture_dict.h"
#include "game/sys/platform_d3d.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

GAME_GLOBAL(uint32_t, g_particleRandomB, 0x0067E974, 0x12B9B0A1);
GAME_GLOBAL(uint32_t, g_particleRandomC, 0x0067E978, 0xAA2FB3F);
GAME_GLOBAL(uint32_t, g_particleRandomA, 0x00705608);

namespace {

constexpr uint32_t kSystemVtable = 0x0064A4F0;
constexpr float kRandomScale = 1.0f / 16384.0f; // 0x006487C8

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
IDirect3DIndexBuffer9* QuadIndices() { return GameVariable<IDirect3DIndexBuffer9*>(0x007D69E8); }
int Rand() { return GameFunction<int(__cdecl*)()>(0x00623BA3)(); } // (the game's C runtime's)

// _ftol: truncated to a 64-bit integer, its low half.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The materials' code (0x004D1A90-0x004D3390).
uint8_t* ConstructMaterial(void* memory) { return reinterpret_cast<uint8_t*>(static_cast<sMaterial*>(memory)->Construct()); }
void DestroyMaterial(uint8_t* material) { reinterpret_cast<sMaterial*>(material)->DestroyBody(); }
void SetMaterialStates(uint8_t* material) { reinterpret_cast<sMaterial*>(material)->SubmitStates(); }
void SetMaterialPass(uint8_t* material, int pass, int flags)
{
    reinterpret_cast<sMaterial*>(material)->SubmitPass(pass, reinterpret_cast<const float*>(static_cast<intptr_t>(flags)));
}
// The skater's texture dictionary's texture by checksum, its key (+0x10, through one more pointer with flag 1 of +0x08).
const uint8_t* FindTexture(uint32_t checksum)
{
    return reinterpret_cast<const uint8_t*>(static_cast<CTexDict*>(GameVariable<void*>(0x00700F20))->GetTexture(checksum));
}
uint32_t TextureKey(const uint8_t* texture)
{
    const uint32_t* key = reinterpret_cast<const uint32_t*>(texture + 0x10);
    return texture[8] & 1 ? *reinterpret_cast<const uint32_t*>(*key) : *key;
}

// The random sequence the particles' spreads come from (three words; four numbers a particle).
uint32_t& RandomA() { return g_particleRandomA; }
uint32_t& RandomB() { return g_particleRandomB; }
uint32_t& RandomC() { return g_particleRandomC; }
// A step: a' = b a + c, b' = (b ^ a') + (a' >> 4), c' = c + (a' >> 3) - 0x10101010 (arithmetic shifts).
void RandomStep(uint32_t& a, uint32_t& b, uint32_t& c)
{
    a = b * a + c;
    b = (b ^ a) + static_cast<uint32_t>(static_cast<int32_t>(a) >> 4);
    c = c + static_cast<uint32_t>(static_cast<int32_t>(a) >> 3) - 0x10101010u;
}
// The number a step makes: 1 to 2 (a's low 16 bits, their top 14).
double RandomScale(uint32_t a)
{
    int value = static_cast<int32_t>((a & 0xFFFF) << 14) >> 16;
    return static_cast<double>(value) * kRandomScale + 1.0f;
}

sParticleBurst* Next(const CPcParticleSystem* system, sParticleBurst* burst)
{
    ++burst;
    return burst == system->bursts + system->capacity ? system->bursts : burst;
}

// A quadratic through three points (x, y, z, w) at 0, `mid` and `end` (or a straight line from the first to the
// last), with the original's roundings. Two copies of it (the spread's and the path's) keep different products
// unrounded; `spreadRounding` picks the spread's.
struct sFit {
    double bx; // (unrounded: the path uses it so)
    float by, bz, bw;
    float c[4];
};
sFit Fit(const float* p0, const float* p1, const float* p2, float end, float mid, bool useMid, bool spreadRounding)
{
    sFit fit{};
    double dx2 = static_cast<double>(p2[0]) - p0[0];
    double dy2 = static_cast<double>(p2[1]) - p0[1];
    float z2 = static_cast<float>(static_cast<double>(p2[2]) - p0[2]);
    float w2 = static_cast<float>(static_cast<double>(p2[3]) - p0[3]);
    if (!useMid) {
        float inverse = static_cast<float>(1.0f / static_cast<double>(end));
        fit.bx = dx2 * inverse;
        fit.by = static_cast<float>(static_cast<double>(static_cast<float>(dy2)) * inverse);
        fit.bz = static_cast<float>(static_cast<double>(z2) * inverse);
        fit.bw = static_cast<float>(static_cast<double>(w2) * inverse);
        return fit;
    }
    float dx2f = static_cast<float>(dx2), dy2f = static_cast<float>(dy2);
    float mid2 = static_cast<float>(static_cast<double>(mid) * mid);
    float dx2Mid2 = static_cast<float>(dx2 * mid2);
    double dy2Mid2 = dy2 * mid2;
    double z2Mid2 = static_cast<double>(z2) * mid2;
    float w2Mid2 = static_cast<float>(static_cast<double>(w2) * mid2);
    double dx1 = static_cast<double>(p1[0]) - p0[0];
    double dy1 = static_cast<double>(p1[1]) - p0[1];
    float z1 = static_cast<float>(static_cast<double>(p1[2]) - p0[2]);
    float w1 = static_cast<float>(static_cast<double>(p1[3]) - p0[3]);
    float dx1f = static_cast<float>(dx1), dy1f = static_cast<float>(dy1);
    float end2 = static_cast<float>(static_cast<double>(end) * end);
    float dx1End2 = static_cast<float>(dx1 * end2);
    double dy1End2 = dy1 * end2;
    float z1End2 = static_cast<float>(static_cast<double>(z1) * end2);
    float w1End2 = static_cast<float>(static_cast<double>(w1) * end2);
    float ax = static_cast<float>(static_cast<double>(dx1End2) - dx2Mid2);
    float ay = static_cast<float>(dy1End2 - dy2Mid2);
    double az = static_cast<double>(z1End2) - z2Mid2;
    float aw = static_cast<float>(static_cast<double>(w1End2) - w2Mid2);
    float inverse = static_cast<float>(1.0f / ((static_cast<double>(end) - mid) * end * mid));
    fit.bx = static_cast<double>(ax) * inverse;
    fit.by = static_cast<float>(static_cast<double>(ay) * inverse);
    fit.bz = static_cast<float>(az * inverse);
    fit.bw = static_cast<float>(static_cast<double>(aw) * inverse);
    float dx1End = static_cast<float>(static_cast<double>(dx1f) * end);
    double dy1End = static_cast<double>(dy1f) * end;
    double z1End = static_cast<double>(z1) * end;
    float w1End = static_cast<float>(static_cast<double>(w1) * end);
    float dx2Mid = static_cast<float>(static_cast<double>(dx2f) * mid);
    float w2Mid = static_cast<float>(static_cast<double>(w2) * mid);
    float cx = static_cast<float>(static_cast<double>(dx2Mid) - dx1End), cy;
    double cz;
    if (spreadRounding) {
        cy = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dy2f) * mid)) - dy1End);
        cz = static_cast<double>(z2) * mid - z1End;
    } else {
        cy = static_cast<float>(static_cast<double>(dy2f) * mid - dy1End);
        cz = static_cast<double>(static_cast<float>(static_cast<double>(z2) * mid)) - z1End;
    }
    float cw = static_cast<float>(static_cast<double>(w2Mid) - w1End);
    fit.c[0] = static_cast<float>(static_cast<double>(cx) * inverse);
    fit.c[1] = static_cast<float>(static_cast<double>(cy) * inverse);
    fit.c[2] = static_cast<float>(cz * inverse);
    fit.c[3] = static_cast<float>(static_cast<double>(cw) * inverse);
    return fit;
}

float MidTime(const CPcParticleSystem* system)
{
    return static_cast<float>(static_cast<double>(system->midPercent) * system->lifetime * 0.01f);
}

// The camera's across and up directions from the view matrix's third column (a, b, c): r = (a, b, c) x (0, 1, 0)
// and u = r x (a, b, c), each normalised when it has a length.
void CameraAxes(float r[3], float u[3])
{
    double a = GameVariable<float>(0x00786BD8);
    double b = GameVariable<float>(0x00786BE8);
    double c = GameVariable<float>(0x00786BF8);
    double b0 = b * 0.0f;
    r[0] = static_cast<float>(b0 - c);
    r[1] = static_cast<float>(c * 0.0f - a * 0.0f);
    r[2] = static_cast<float>(a - b0);
    u[0] = static_cast<float>(r[1] * c - r[2] * b);
    u[1] = static_cast<float>(r[2] * a - c * r[0]);
    u[2] = static_cast<float>(b * r[0] - r[1] * a);
    for (float* w : {r, u}) {
        double length = std::sqrt(static_cast<double>(w[0]) * w[0] + static_cast<double>(w[2]) * w[2] +
                                  static_cast<double>(w[1]) * w[1]);
        if (length > 0.0f) {
            double inverse = 1.0f / length;
            for (int i = 0; i < 3; ++i)
                w[i] = static_cast<float>(w[i] * inverse);
        }
    }
}

// A particle's quad vertex: position, colour (ARGB), texture coordinates.
struct sParticleVertex {
    float x, y, z;
    uint32_t colour;
    float u, v;
};
static_assert(sizeof(sParticleVertex) == 0x18, "layout must match the original");

} // namespace

// 0x004BB040
void sParticleBurst::Skip(int particles)
{
    uint32_t a = seed[0], b = seed[1], c = seed[2];
    RandomA() = a;
    RandomB() = b;
    RandomC() = c;
    int steps = particles * 4;
    if (steps > 0) {
        for (; steps; --steps)
            RandomStep(a, b, c);
        RandomC() = c;
        RandomB() = b;
        RandomA() = a;
    }
    seed[2] = c;
    seed[0] = a;
    seed[1] = b;
}

// 0x004BCE90
CPcParticleSystem* __fastcall CPcParticleSystem::Create(void*)
{
    void* memory = Mem::Calloc(sizeof(CPcParticleSystem));
    if (!memory)
        return nullptr;
    auto* system = reinterpret_cast<CPcParticleSystem*>(static_cast<CParticle*>(memory)->Construct());
    system->vtable = reinterpret_cast<void**>(kSystemVtable);
    return system;
}

// 0x004BCE80
void CPcParticleSystem::DestroyBody() { EngineDestroyBody(); }

// 0x004BCE50
CPcParticleSystem* CPcParticleSystem::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004BC250
void CPcParticleSystem::Initialise()
{
    rate = static_cast<float>(static_cast<double>(rate) * 0.75f); // (0x0064A4EC)
    capacity = 5;
    live = 0;
    bursts = static_cast<sParticleBurst*>(Mem::New(0x8C));
    newest = bursts + capacity - 1;
    oldest = bursts;
    emitting = false;

    void* memory = Mem::New(0x110);
    material = memory ? ConstructMaterial(memory) : nullptr;
    std::memset(material, 0, 0x110);
    *reinterpret_cast<uint32_t*>(material + 0x20) = 0x44; // (its flags)
    *reinterpret_cast<uint32_t*>(material + 0x08) = 1;    // (a pass)
    material[0x10] = 1;
    material[0x0D] = 1;
    for (int k = 0; k < 3; ++k)
        *reinterpret_cast<float*>(material + 0x40 + 4 * k) = 0.5f;
    *reinterpret_cast<float*>(material + 0x4C) = static_cast<float>(static_cast<double>(alphaCutOff) * (1.0f / 128.0f));
    *reinterpret_cast<uint32_t*>(material + 0x90) = 0x10001;
    *reinterpret_cast<uint32_t*>(material + 0xC0) = 0x10001;
    *reinterpret_cast<uint32_t*>(material + 0x80) = BlendModeFromName(blendMode);
    *reinterpret_cast<uint32_t*>(material + 0x30) = 0;
    if (const uint8_t* found = FindTexture(texture))
        *reinterpret_cast<uint32_t*>(material + 0x30) = TextureKey(found);

    // The spread: the boxes and their radii.
    float start[4] = {boxes[0].x, boxes[0].y, boxes[0].z, radii[0]};
    float middle[4] = {boxes[1].x, boxes[1].y, boxes[1].z, radii[1]};
    float last[4] = {boxes[2].x, boxes[2].y, boxes[2].z, radii[2]};
    sFit fit = Fit(start, middle, last, lifetime, MidTime(this), useMid, true);
    spread.a = {start[0], start[1], start[2], start[3]};
    spread.b = {static_cast<float>(fit.bx), fit.by, fit.bz, fit.bw};
    spread.c = {fit.c[0], fit.c[1], fit.c[2], fit.c[3]};
    // The path (the original works it out here as well, then again in UpdatePath to the same values).
    UpdatePath();
    std::memset(&matrix, 0, sizeof(matrix));
    matrix.row[0].x = matrix.row[1].y = matrix.row[2].z = matrix.row[3].w = 1.0f;
}

// 0x004BBD20
void CPcParticleSystem::UpdatePath()
{
    float start[4] = {positions[0].x, positions[0].y, positions[0].z, sizes[0]};
    float middle[4] = {positions[1].x, positions[1].y, positions[1].z, sizes[1]};
    float last[4] = {positions[2].x, positions[2].y, positions[2].z, sizes[2]};
    sFit fit = Fit(start, middle, last, lifetime, MidTime(this), useMid, false);
    // Less 1.5 times the spread: its x and w products unrounded, its y and z rounded first.
    auto less = [](double value, float spreadValue, bool rounded) {
        double offset = static_cast<double>(spreadValue) * 1.5f;
        return static_cast<float>(value - (rounded ? static_cast<double>(static_cast<float>(offset)) : offset));
    };
    path.a = {less(start[0], spread.a.x, false), less(start[1], spread.a.y, true), less(start[2], spread.a.z, true),
              less(start[3], spread.a.w, false)};
    path.b = {less(fit.bx, spread.b.x, false), less(fit.by, spread.b.y, true), less(fit.bz, spread.b.z, true),
              less(fit.bw, spread.b.w, false)};
    path.c = {less(fit.c[0], spread.c.x, false), less(fit.c[1], spread.c.y, true), less(fit.c[2], spread.c.z, true),
              less(fit.c[3], spread.c.w, false)};
}

// 0x004BC240
void CPcParticleSystem::Update()
{
    if (localPositions)
        UpdatePath();
}

// 0x004BB0B0
bool CPcParticleSystem::IsEmitting() { return live > 0; }

// 0x004BCE10
void CPcParticleSystem::Release()
{
    if (bursts)
        Mem::Manager::Get()->Free(bursts);
    if (uint8_t* own = material) {
        DestroyMaterial(own);
        Mem::Manager::Get()->Free(own);
    }
}

// 0x004BB0C0
void CPcParticleSystem::Render(bool advance)
{
    // A new burst when the rate changed (or stopping when it's 0).
    if (rate == 0.0f || (emitting && rate == newest->rate)) {
        emitting = !(rate == 0.0f);
    } else if (live < capacity) {
        ++live;
        newest = Next(this, newest);
        newest->rate = rate;
        newest->period = static_cast<float>(1.0f / static_cast<double>(rate));
        newest->age = 0.0f;
        newest->count = 0;
        newest->seed[0] = static_cast<uint32_t>(Rand());
        newest->seed[1] = 0x12B9B0A1;
        newest->seed[2] = 0x0AA2FB3F;
        emitting = true;
    } else {
        emitting = false;
    }
    if (live == 0)
        return;
    if (advance) {
        sParticleBurst* burst = oldest;
        for (int i = 0; i < live; ++i) {
            burst->age = static_cast<float>(Tmr::GetSlomoFrameSeconds() + burst->age);
            burst = Next(this, burst);
        }
    }
    if (emitting)
        newest->count = Truncate(static_cast<double>(newest->age) * newest->rate + 1.0f);
    sParticleBurst* first = oldest;
    if (first->age > lifetime) {
        int gone = Truncate((static_cast<double>(first->age) - lifetime) * first->rate + 1.0f);
        first->count -= gone;
        if (oldest->count <= 0 && (live != 1 || !emitting)) {
            --live;
            oldest = Next(this, oldest);
            if (live == 0)
                return;
        } else {
            sParticleBurst* burst = oldest;
            burst->age = static_cast<float>(burst->age - static_cast<double>(gone) * burst->period);
            oldest->Skip(gone);
        }
    }
    if (!localPositions) {
        Mth::Vector centre = {bounds.x, bounds.y, bounds.z, 0.0f};
        bool visible = cullOutside ? SphereOutsideView(&centre, bounds.w) != 0 : SphereInView(&centre, bounds.w);
        if (!visible)
            return;
    }

    SetMaterialStates(material);
    SetMaterialPass(material, 0, 0);
    float r[3], u[3];
    CameraAxes(r, u);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    SetPixelShader(GameVariable<void*>(0x007CA964), nullptr);
    float colourTable[3][4];
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 4; ++k)
            colourTable[i][k] = static_cast<float>(static_cast<double>(colours[i][k]) * (1.0f / 255.0f));

    sParticleBurst* burst = oldest;
    for (int b = 0; b < live; ++b, ++burst) {
        if (burst == bursts + capacity)
            burst = bursts;
        float colourMid = static_cast<float>(static_cast<double>(colourMidPercent) * 0.01f * lifetime);
        float age = burst->age;
        RandomA() = burst->seed[0];
        RandomB() = burst->seed[1];
        RandomC() = burst->seed[2];
        int remaining = burst->count;
        int index = 0;
        while (remaining != 0) {
            IDirect3DVertexBuffer9* buffer = nullptr;
            uint32_t firstVertex = 0, room = 0;
            auto* vertex = static_cast<sParticleVertex*>(
                Platform::LockDynamicVertices(remaining * 4, sizeof(sParticleVertex), &buffer, &firstVertex, &room));
            room >>= 2;
            if (room > 0x1000)
                room = 0x1000;
            uint32_t quads = 0;
            if (index < burst->count) {
                double t = age;
                for (;;) {
                    float t2 = static_cast<float>(t * t);
                    uint32_t a = RandomA(), bb = RandomB(), c = RandomC();
                    RandomStep(a, bb, c);
                    double s1 = RandomScale(a);
                    RandomStep(a, bb, c);
                    float s2 = static_cast<float>(RandomScale(a));
                    RandomStep(a, bb, c);
                    double s3 = RandomScale(a);
                    RandomStep(a, bb, c);
                    float s4 = static_cast<float>(RandomScale(a));
                    RandomA() = a;
                    RandomB() = bb;
                    RandomC() = c;

                    // The colour: start to end, or through the mid.
                    const float* from = colourTable[0];
                    const float* to = colourTable[2];
                    double fraction;
                    if (colourUseMid) {
                        if (t > colourMid) {
                            fraction = (t - colourMid) / (static_cast<double>(lifetime) - colourMid);
                            from = colourTable[1];
                        } else {
                            fraction = t / colourMid;
                            to = colourTable[1];
                        }
                    } else {
                        fraction = t / lifetime;
                    }
                    auto channel = [&](int k) {
                        return static_cast<uint32_t>(
                                   Truncate(((static_cast<double>(to[k]) - from[k]) * fraction + from[k]) * 255.0f)) &
                               0xFF;
                    };
                    uint32_t alpha = channel(3), red = channel(0), green = channel(1), blue = channel(2);
                    uint32_t colour = alpha << 24 | red << 16 | green << 8 | blue;

                    // The spread at t, times its random numbers.
                    float spreadX = static_cast<float>(
                        static_cast<double>(static_cast<float>(t * spread.b.x + spread.a.x)) + static_cast<double>(t2) * spread.c.x);
                    double spreadY = t * spread.b.y + spread.a.y + t2 * static_cast<double>(spread.c.y);
                    double spreadZ =
                        static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(t * spread.b.z)) + spread.a.z)) +
                        static_cast<float>(t2 * static_cast<double>(spread.c.z));
                    float spreadW = static_cast<float>(
                        static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(t * spread.b.w)) + spread.a.w)) +
                        static_cast<float>(t2 * static_cast<double>(spread.c.w)));
                    float offsetX = static_cast<float>(spreadX * s1);
                    float offsetY = static_cast<float>(spreadY * s2);
                    double offsetZ = spreadZ * s3;
                    double offsetW = static_cast<double>(spreadW) * s4;
                    // The path at t.
                    float pathX = static_cast<float>(
                        static_cast<double>(static_cast<float>(t * path.b.x + path.a.x)) + static_cast<double>(t2) * path.c.x);
                    double pathY = t * path.b.y + path.a.y + t2 * static_cast<double>(path.c.y);
                    double pathZ =
                        static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(t * path.b.z)) + path.a.z)) +
                        static_cast<float>(t2 * static_cast<double>(path.c.z));
                    float pathW = static_cast<float>(
                        static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(t * path.b.w)) + path.a.w)) +
                        static_cast<float>(t2 * static_cast<double>(path.c.w)));
                    float centreX = static_cast<float>(static_cast<double>(pathX) + offsetX);
                    float centreY = static_cast<float>(pathY + offsetY);
                    float centreZ = static_cast<float>(pathZ + offsetZ);
                    double size = static_cast<double>(pathW) + offsetW;

                    // The quad: (centre - r - ... ) corners, its texture 0..1.
                    double acrossX = r[0] * size;
                    float acrossY = static_cast<float>(r[1] * size), acrossZ = static_cast<float>(r[2] * size);
                    float upX = static_cast<float>(u[0] * size), upY = static_cast<float>(u[1] * size),
                          upZ = static_cast<float>(u[2] * size);
                    float leftX = static_cast<float>(centreX - acrossX);
                    float leftY = static_cast<float>(static_cast<double>(centreY) - acrossY);
                    float leftZ = static_cast<float>(static_cast<double>(centreZ) - acrossZ);
                    vertex[0] = {static_cast<float>(static_cast<double>(upX) + leftX),
                                 static_cast<float>(static_cast<double>(upY) + leftY),
                                 static_cast<float>(static_cast<double>(upZ) + leftZ), colour, 0.0f, 0.0f};
                    vertex[1] = {static_cast<float>(static_cast<double>(centreX) + upX + acrossX),
                                 static_cast<float>(static_cast<double>(centreY) + upY + acrossY),
                                 static_cast<float>(static_cast<double>(centreZ) + upZ + acrossZ), colour, 1.0f, 0.0f};
                    vertex[2] = {static_cast<float>(static_cast<double>(centreX) + acrossX - upX),
                                 static_cast<float>(static_cast<double>(centreY) + acrossY - upY),
                                 static_cast<float>(static_cast<double>(centreZ) + acrossZ - upZ), colour, 1.0f, 1.0f};
                    vertex[3] = {static_cast<float>(static_cast<double>(leftX) - upX),
                                 static_cast<float>(static_cast<double>(leftY) - upY),
                                 static_cast<float>(static_cast<double>(leftZ) - upZ), colour, 0.0f, 1.0f};
                    vertex += 4;
                    t = t - burst->period;
                    ++quads;
                    ++index;
                    if (quads >= room - 1 || index >= burst->count)
                        break;
                }
                age = static_cast<float>(t);
            }
            remaining -= static_cast<int>(quads);
            Platform::UnlockDynamicVertices(quads * 4, sizeof(sParticleVertex));
            Device()->SetStreamSource(0, buffer, 0, sizeof(sParticleVertex));
            Device()->SetIndices(QuadIndices());
            Device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(firstVertex), 0, quads * 4, 0, quads * 2);
        }
    }
}

} // namespace Nx

REPLACE(0x004BB040, &Nx::sParticleBurst::Skip);
REPLACE(0x004BCE90, Nx::CPcParticleSystem::Create);
REPLACE(0x004BCE80, &Nx::CPcParticleSystem::DestroyBody);
REPLACE(0x004BCE50, &Nx::CPcParticleSystem::Destroy);
REPLACE(0x004BC250, &Nx::CPcParticleSystem::Initialise);
REPLACE(0x004BBD20, &Nx::CPcParticleSystem::UpdatePath);
REPLACE(0x004BC240, &Nx::CPcParticleSystem::Update);
REPLACE(0x004BB0B0, &Nx::CPcParticleSystem::IsEmitting);
REPLACE(0x004BCE10, &Nx::CPcParticleSystem::Release);
REPLACE(0x004BB0C0, &Nx::CPcParticleSystem::Render);
