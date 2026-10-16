// Self-tests for src/game/gfx/render.cpp: the originals, with PartyMod's patch bytes written in for the length of
// the test, against ours - the device and the renderer's state calls stood in by recorders.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/gfx/render.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

// Bytes written over game code for the length of a test (restored when it goes). (A copy of
// selftests_partymod.cpp's.)
struct CodePatch {
    uint8_t* at;
    std::vector<uint8_t> saved;
    CodePatch(uint32_t address, const std::vector<uint8_t>& bytes)
        : at(reinterpret_cast<uint8_t*>(address)), saved(bytes.size())
    {
        DWORD old;
        VirtualProtect(at, bytes.size(), PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(saved.data(), at, bytes.size());
        std::memcpy(at, bytes.data(), bytes.size());
        FlushInstructionCache(GetCurrentProcess(), at, bytes.size());
    }
    ~CodePatch()
    {
        std::memcpy(at, saved.data(), saved.size());
        FlushInstructionCache(GetCurrentProcess(), at, saved.size());
    }
};
std::vector<uint8_t> JumpTo(uint32_t from, const void* to)
{
    std::vector<uint8_t> bytes(5);
    bytes[0] = 0xE9;
    int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (from + 5));
    std::memcpy(&bytes[1], &relative, 4);
    return bytes;
}
std::vector<uint8_t> FloatBytes(float f)
{
    std::vector<uint8_t> bytes(4);
    std::memcpy(bytes.data(), &f, 4);
    return bytes;
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// Globals saved before a test and put back after.
struct SavedBytes {
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> blocks;
    SavedBytes(std::initializer_list<std::pair<uint32_t, uint32_t>> ranges)
    {
        for (auto& r : ranges) {
            std::vector<uint8_t> bytes(r.second);
            std::memcpy(bytes.data(), reinterpret_cast<void*>(r.first), r.second);
            blocks.push_back({r.first, bytes});
        }
    }
    ~SavedBytes()
    {
        for (auto& b : blocks)
            std::memcpy(reinterpret_cast<void*>(b.first), b.second.data(), b.second.size());
    }
};

void CompareTraces(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* name,
                   size_t minimum)
{
    for (size_t i = 0, shown = 0; i < expected.size() && i < actual.size() && shown < 6; ++i)
        if (expected[i] != actual[i]) {
            Log("  %s entry %u: original %08X ours %08X", name, static_cast<unsigned>(i), expected[i], actual[i]);
            ++shown;
        }
    if (expected.size() != actual.size())
        Log("  %s trace sizes: original %u ours %u", name, static_cast<unsigned>(expected.size()),
            static_cast<unsigned>(actual.size()));
    Check(expected == actual && expected.size() >= minimum, name);
}

std::vector<uint32_t> g_log;
void Record(std::initializer_list<uint32_t> words) { g_log.insert(g_log.end(), words); }

// --- set_camera ---------------------------------------------------------------------------------------------

long __stdcall CameraSetTransform(void*, int type, const uint32_t* m)
{
    Record({0x7F0u, static_cast<uint32_t>(type)});
    g_log.insert(g_log.end(), m, m + 16);
    return 0;
}
void __fastcall CameraFrustum(void* frustum) { Record({0xF85u, reinterpret_cast<uint32_t>(frustum)}); }

// The camera over orientations facing all four ways (and a tilted one), fields of view, aspects, zooms (1, not 1,
// NaN), far plane at infinity or not, distance clipping off / on (with a setting past 2^31) - the original with
// PartyMod's far plane (96000) written in. Every camera global and the device's transforms compared.
void TestSetCamera()
{
    SavedBytes saved({{0x00786B90, 0x164}, {0x00786E18, 0x18}, {0x007D643A, 1}, {0x007D6444, 4}, {0x007D69E0, 4}});
    CodePatch frustum(0x004CDAA0, JumpTo(0x004CDAA0, reinterpret_cast<void*>(&CameraFrustum)));
    static void* vtable[120] = {};
    vtable[0xB0 / 4] = reinterpret_cast<void*>(&CameraSetTransform);
    static void* device[2] = {vtable, nullptr};
    GameVariable<void*>(0x007D69E0) = device;
    static const Mth::Matrix orientations[5] = {
        {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}},
        {{{0, 0, -1, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}, {5, 6, 7, 1}}},
        {{{-0.6f, 0, 0.8f, 0}, {0, 1, 0, 0}, {-0.8f, 0, -0.6f, 0}, {0, 0, 0, 1}}},
        {{{0.28f, 0, -0.96f, 0}, {0.1f, 0.98f, 0.05f, 0}, {0.96f, -0.2f, -0.28f, 0.5f}, {0, 0, 0, 1}}},
        {{{0.7f, 0.1f, 0.7f, 0}, {-0.07f, 0.99f, 0.05f, 0}, {-0.71f, 0.03f, 0.7f, 0}, {0, 0, 0, 1}}},
    };
    static const Mth::Vector positions[2] = {{100.5f, -20.25f, 3000.75f, 1}, {-1234.0f, 56.0f, -0.125f, 7}};
    // (30.0071 and 30.0639: the first tangent differs if its half-angle is rounded to float first)
    const float angles[6] = {72.0f, 90.0f, 55.3f, 121.7f, 30.007099151611328f, 30.063899993896484f};
    const float aspects[3] = {4.0f / 3, 16.0f / 9, 0.75f};
    const float zooms[3] = {1.0f, 1.37f, std::numeric_limits<float>::quiet_NaN()};
    // (D3DX picks its implementation on the first call through its dispatch pointers: done before the runs)
    Mth::Matrix scratch;
    GameFunction<void*(__stdcall*)(void*, float, float, float)>(0x005C036B)(&scratch, 1, 2, 3);
    GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C)(&scratch, &scratch, &scratch);
    GameFunction<void*(__stdcall*)(void*, float)>(0x005C0509)(&scratch, 0.5f);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_log.clear();
        int n = 0;
        for (int o = 0; o < 5; ++o)
            for (int a = 0; a < 6; ++a)
                for (int s = 0; s < 3; ++s)
                    for (int z = 0; z < 3; ++z)
                        for (int infinite = 0; infinite < 2; ++infinite)
                            for (int clip = 0; clip < 3; ++clip) {
                                ++n;
                                std::memset(reinterpret_cast<void*>(0x00786B90), 0x5A + n % 7, 0x164);
                                std::memset(reinterpret_cast<void*>(0x00786E18), 0x3C + n % 5, 0x18);
                                GameVariable<bool>(0x007D643A) = clip != 0;
                                GameVariable<uint32_t>(0x007D6444) = clip == 2 ? 0x80000005u : 100 + 37 * (n % 14);
                                const Mth::Matrix* m = &orientations[o];
                                const Mth::Vector* p = &positions[n % 2];
                                if (original) {
                                    CodePatch farPlane(0x004DB35D, FloatBytes(96000.0f));
                                    Original<void(__cdecl*)(const Mth::Matrix*, const Mth::Vector*, float, float, float,
                                                            int)>(0x004DB200)(m, p, angles[a], aspects[s], zooms[z],
                                                                              infinite ? 0x7701 : 0x7700);
                                } else {
                                    Nx::SetCamera(m, p, angles[a], aspects[s], zooms[z], infinite != 0);
                                }
                                const uint32_t* globals = reinterpret_cast<const uint32_t*>(0x00786B90);
                                g_log.insert(g_log.end(), globals, globals + 0x164 / 4);
                                globals = reinterpret_cast<const uint32_t*>(0x00786E18);
                                g_log.insert(g_log.end(), globals, globals + 6);
                            }
        traces[run] = g_log;
    }
    CompareTraces(traces[0], traces[1], "gfx: set_camera (PartyMod's far plane)", 360 * 140);
}

// --- the blur pass ------------------------------------------------------------------------------------------

uint32_t g_sampler[4][8];
uint32_t g_texture_size[2][2];
void* g_texture[2];
void* g_surface[2];

long __stdcall BlurSetRenderTarget(void*, int index, void* surface) { Record({0x57u, static_cast<uint32_t>(index), reinterpret_cast<uint32_t>(surface)}); return 0; }
long __stdcall BlurSetDepth(void*, void* surface) { Record({0xD5u, reinterpret_cast<uint32_t>(surface)}); return 0; }
long __stdcall BlurClear(void*, uint32_t count, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    Record({0xC1u, count, reinterpret_cast<uint32_t>(rects), flags, color, Bits(z), stencil});
    return 0;
}
long __stdcall BlurStageState(void*, int stage, int type, int value) { Record({0x555u, static_cast<uint32_t>(stage), static_cast<uint32_t>(type), static_cast<uint32_t>(value)}); return 0; }
long __stdcall BlurGetSampler(void*, int sampler, int type, uint32_t* value)
{
    Record({0x6E7u, static_cast<uint32_t>(sampler), static_cast<uint32_t>(type)});
    *value = g_sampler[sampler][type];
    return 0;
}
long __stdcall BlurSetSampler(void*, int sampler, int type, uint32_t value)
{
    Record({0x5E7u, static_cast<uint32_t>(sampler), static_cast<uint32_t>(type), value});
    g_sampler[sampler][type] = value;
    return 0;
}
long __stdcall BlurDraw(void*, int type, unsigned int count, const uint32_t* v, unsigned int stride)
{
    Record({0xD4Au, static_cast<uint32_t>(type), count, stride});
    g_log.insert(g_log.end(), v, v + 28);
    return 0;
}
long __stdcall BlurGetLevelDesc(void* texture, unsigned int level, uint32_t* desc)
{
    int i = texture == g_texture[1];
    Record({0x1DEu, static_cast<uint32_t>(i), level});
    std::memset(desc, 0xEE, 32);
    desc[6] = g_texture_size[i][0];
    desc[7] = g_texture_size[i][1];
    return 0;
}
long __stdcall BlurGetSurfaceLevel(void* texture, unsigned int level, void** surface)
{
    int i = texture == g_texture[1];
    Record({0x5AFu, static_cast<uint32_t>(i), level, *surface == nullptr});
    *surface = g_surface[i];
    return 0;
}
unsigned long __stdcall BlurRelease(void* surface) { Record({0x2E1u, reinterpret_cast<uint32_t>(surface)}); return 0; }
void __cdecl BlurFog(int on) { Record({0xF06u, static_cast<uint32_t>(on)}); }
void __cdecl BlurState(int state, int value) { Record({0x5A7u, static_cast<uint32_t>(state), static_cast<uint32_t>(value)}); }
void __cdecl BlurTexture(int stage, void* texture, int unknown) { Record({0x7E7u, static_cast<uint32_t>(stage), reinterpret_cast<uint32_t>(texture), static_cast<uint32_t>(unknown)}); }
void __cdecl BlurBlend(int mode) { Record({0xB1Du, static_cast<uint32_t>(mode)}); }
void __cdecl BlurFormat(uint32_t fvf) { Record({0xF7Fu, fvf}); }
void __cdecl BlurShader(int a, int b) { Record({0x5AAu, static_cast<uint32_t>(a), static_cast<uint32_t>(b)}); }
void __cdecl BlurExtra() { Record({0xE47u}); }

// The blur pass: off; on with each texture current, blur amounts 0 / small / full / past 2^31, blur levels 2 and 3
// (one pass or two), letterboxed or not (PartyMod skips the adjustment), fog and distance clipping (clear colour),
// the extra pass 0x004DEB70 or not, screen and texture sizes, samplers already linear or not - the original with
// PartyMod's JMP at 0x004DF63A written in.
void TestDrawScreenBlur()
{
    SavedBytes saved({{0x007CC244, 1}, {0x00786D68, 4}, {0x00786D1C, 8}, {0x00787328, 8}, {0x00786A7C, 8},
                      {0x00786CBE, 1}, {0x00786D70, 4}, {0x00705720, 4}, {0x00786D64, 4}, {0x00786D10, 8},
                      {0x007D6436, 1}, {0x007D643A, 1}, {0x0078733C, 4}, {0x00786CC0, 4}, {0x00786EBC, 8},
                      {0x007D69E0, 4}});
    CodePatch fog(0x00490DE0, JumpTo(0x00490DE0, reinterpret_cast<void*>(&BlurFog)));
    CodePatch state(0x004DAB60, JumpTo(0x004DAB60, reinterpret_cast<void*>(&BlurState)));
    CodePatch texture(0x004DA730, JumpTo(0x004DA730, reinterpret_cast<void*>(&BlurTexture)));
    CodePatch blend(0x004DA760, JumpTo(0x004DA760, reinterpret_cast<void*>(&BlurBlend)));
    CodePatch format(0x004D9DB0, JumpTo(0x004D9DB0, reinterpret_cast<void*>(&BlurFormat)));
    CodePatch shader(0x004DAA80, JumpTo(0x004DAA80, reinterpret_cast<void*>(&BlurShader)));
    CodePatch extra(0x004DEB70, JumpTo(0x004DEB70, reinterpret_cast<void*>(&BlurExtra)));
    static void* vtable[120] = {};
    vtable[0x94 / 4] = reinterpret_cast<void*>(&BlurSetRenderTarget);
    vtable[0x9C / 4] = reinterpret_cast<void*>(&BlurSetDepth);
    vtable[0xAC / 4] = reinterpret_cast<void*>(&BlurClear);
    vtable[0x10C / 4] = reinterpret_cast<void*>(&BlurStageState);
    vtable[0x110 / 4] = reinterpret_cast<void*>(&BlurGetSampler);
    vtable[0x114 / 4] = reinterpret_cast<void*>(&BlurSetSampler);
    vtable[0x14C / 4] = reinterpret_cast<void*>(&BlurDraw);
    static void* device[2] = {vtable, nullptr};
    static void* textureVtable[20] = {};
    textureVtable[0x44 / 4] = reinterpret_cast<void*>(&BlurGetLevelDesc);
    textureVtable[0x48 / 4] = reinterpret_cast<void*>(&BlurGetSurfaceLevel);
    static void* surfaceVtable[4] = {};
    surfaceVtable[2] = reinterpret_cast<void*>(&BlurRelease);
    static void* textures[2][2] = {{textureVtable, nullptr}, {textureVtable, nullptr}};
    static void* surfaces[2][2] = {{surfaceVtable, nullptr}, {surfaceVtable, nullptr}};
    g_texture[0] = textures[0];
    g_texture[1] = textures[1];
    g_surface[0] = surfaces[0];
    g_surface[1] = surfaces[1];
    GameVariable<void*>(0x007D69E0) = device;
    GameVariable<void*>(0x00786D1C) = textures[0];
    GameVariable<void*>(0x00786D20) = textures[1];
    GameVariable<uint32_t>(0x00786D64) = 0xDE000064;
    GameVariable<uint32_t>(0x00786D10) = 0xBB000010;
    GameVariable<uint32_t>(0x00786D14) = 0xDD000014;
    GameVariable<uint32_t>(0x0078733C) = 0x00334455;
    GameVariable<uint32_t>(0x00786CC0) = 0x00667788;
    const uint32_t amounts[5] = {0, 1, 128, 255, 0x80000001u};
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_log.clear();
        GameVariable<int>(0x00786EBC) = 10;
        GameVariable<int>(0x00786EC0) = 100;
        int n = 0;
        for (int enabled = 0; enabled < 2; ++enabled)
            for (int amount = 0; amount < 5; ++amount)
                for (uint32_t level = 1; level <= 4; ++level)
                    for (int flags = 0; flags < 16; ++flags) {
                        ++n;
                        GameVariable<uint8_t>(0x007CC244) = enabled ? static_cast<uint8_t>(1 + n % 3) : 0;
                        GameVariable<int>(0x00786D68) = n % 2;
                        GameVariable<uint32_t>(0x00787328) = amounts[amount];
                        GameVariable<uint32_t>(0x0078732C) = level;
                        GameVariable<uint8_t>(0x00786CBE) = flags & 1;
                        GameVariable<uint8_t>(0x007D6436) = (flags >> 1) & 1;
                        GameVariable<uint8_t>(0x007D643A) = (flags >> 2) & 1;
                        GameVariable<uint32_t>(0x00705720) = (flags >> 3) ? 0x1234 : 0;
                        GameVariable<int>(0x00786A7C) = n % 3 == 0 ? 640 : n % 3 == 1 ? 1920 : 1023;
                        GameVariable<int>(0x00786A80) = n % 3 == 0 ? 480 : n % 3 == 1 ? 1080 : 777;
                        GameVariable<int>(0x00786D70) = 700 + n % 11;
                        g_texture_size[0][0] = 512 + (n % 5) * 3;
                        g_texture_size[0][1] = 256 + (n % 7) * 5;
                        g_texture_size[1][0] = 1024 - (n % 3);
                        g_texture_size[1][1] = 300 + n % 4;
                        for (int s = 0; s < 4; ++s) {
                            g_sampler[s][5] = (n + s) % 3;
                            g_sampler[s][6] = (n + 2 * s) % 4;
                        }
                        if (original) {
                            CodePatch partyMod(0x004DF63A, {0xEB});
                            Original<void(__cdecl*)()>(0x004DF390)();
                        } else {
                            Nx::DrawScreenBlur();
                        }
                        Record({0xE4Du, GameVariable<uint32_t>(0x00786D68), GameVariable<uint32_t>(0x00786EBC),
                                GameVariable<uint32_t>(0x00786EC0)});
                        for (int s = 0; s < 4; ++s)
                            Record({g_sampler[s][5], g_sampler[s][6]});
                    }
        traces[run] = g_log;
    }
    CompareTraces(traces[0], traces[1], "gfx: the blur pass (PartyMod's letterbox skip)", 50000);
}

} // namespace

SELFTEST(Early, TestSetCamera);
SELFTEST(Early, TestDrawScreenBlur);
