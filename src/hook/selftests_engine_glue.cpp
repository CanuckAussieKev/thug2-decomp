// Self-tests for the engine's platform calls (src/game/gfx/engine_glue.cpp, RFX-1): the original and ours against a
// recording stand-in device, stand-in allocations and constructors, the screenshot's files in a scratch folder;
// traces and the globals compared.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/device.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/shadow.h"
#include "game/gfx/texture.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

constexpr uint32_t kDevice = 0xD3D00000;
constexpr uint32_t kSurface = 0x5F500000;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

// Allocations: blocks from an arena (or null, as the test says), recorded; pointers into it relocated.
alignas(16) uint8_t g_arena[0x8000];
uint32_t g_used;
std::vector<bool> g_failAllocations; // in order (none left: succeed)
uint32_t Bump(uint32_t size)
{
    bool fail = false;
    if (!g_failAllocations.empty()) {
        fail = g_failAllocations.front();
        g_failAllocations.erase(g_failAllocations.begin());
    }
    if (fail)
        return 0;
    uint32_t at = reinterpret_cast<uint32_t>(g_arena) + g_used;
    g_used += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(at), 0xCD, size);
    return at;
}
uint32_t ArenaRelocate(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(g_arena);
    if (word >= base && word <= base + sizeof(g_arena))
        return 0xA0000000u + (word - base);
    return word;
}

void StandInMemory(StandIns& s)
{
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        return Bump(r.args[0]);
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        uint32_t at = Bump(r.args[0]);
        if (at)
            std::memset(reinterpret_cast<void*>(at), 0, r.args[0]);
        return at;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Relocate(r.ecx), Relocate(r.args[0])});
        return 0u;
    });
}

// A thiscall function stood in: records `this` and its stack arguments, returns `this`.
void StandInThis(StandIns& s, std::initializer_list<uint32_t> addresses, int args)
{
    uint32_t tag = *addresses.begin() & 0xFFFFFF;
    s.At(addresses, static_cast<uint16_t>(4 * args), [tag, args](Regs& r) {
        Record({tag, Relocate(r.ecx)});
        for (int i = 0; i < args; ++i)
            Record({Relocate(r.args[i])});
        return r.ecx;
    });
}
// A cdecl function stood in: records its arguments, returns `result`.
void StandInCdecl(StandIns& s, std::initializer_list<uint32_t> addresses, int args, uint32_t result = 0)
{
    uint32_t tag = *addresses.begin() & 0xFFFFFF;
    s.At(addresses, 0, [tag, args, result](Regs& r) {
        Record({tag});
        for (int i = 0; i < args; ++i)
            Record({Relocate(r.args[i])});
        return result;
    });
}

// The screenshot: numbered names (none, one, three files there already) and the exact name; the display mode's
// size into the surface; the file name the D3DX save gets.
void TestEngineScreenShot()
{
    char scratch[MAX_PATH], previous[MAX_PATH];
    GetTempPathA(MAX_PATH, scratch);
    strcat_s(scratch, "thug2_rfx_shots");
    CreateDirectoryA(scratch, nullptr);
    GetCurrentDirectoryA(MAX_PATH, previous);
    if (!SetCurrentDirectoryA(scratch)) {
        Log("self-test: TestEngineScreenShot skipped (no scratch folder)");
        return;
    }
    for (const char* f : {"shot000.jpg", "shot001.jpg", "shot002.jpg", "one000.jpg"}) {
        FILE* file = nullptr;
        if (fopen_s(&file, f, "wb") == 0 && file)
            std::fclose(file);
    }
    GameMemory memory({{0x00786E14, 1}, {0x007D69E0, 4}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* surface = com.Surface(kSurface);
    UseStandardRelocation(com);
    com.On(kDevice, 8, [](Regs& r) { // GetDisplayMode
        auto* mode = reinterpret_cast<D3DDISPLAYMODE*>(r.args[2]);
        mode->Width = 1234;
        mode->Height = 567;
        mode->RefreshRate = 60;
        mode->Format = D3DFMT_X8R8G8B8;
        return 0u;
    });
    com.On(kDevice, 36, [surface](Regs& r) { // CreateOffscreenPlainSurface
        *reinterpret_cast<void**>(r.args[5]) = surface;
        return 0u;
    });
    // the game's C runtime (not started offline), for the original
    s.At({0x006234FE}, 0, StandInSprintf);
    s.At({0x006241E8}, 0, [](Regs& r) {
        FILE* file = nullptr;
        fopen_s(&file, reinterpret_cast<const char*>(r.args[0]), "rb");
        return reinterpret_cast<uint32_t>(file);
    });
    s.At({0x00623E04}, 0, [](Regs& r) { return static_cast<uint32_t>(std::fclose(reinterpret_cast<FILE*>(r.args[0]))); });
    s.At({0x005C2F76}, 20, [](Regs& r) { // D3DXSaveSurfaceToFileA
        Record({0x2F76, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], Relocate(r.args[2]), r.args[3],
                r.args[4]});
        return 0u;
    });
    bool ok = true;
    for (int round = 0; round < 6; ++round) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Fill(0x5407 + round);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<bool>(0x00786E14) = round >= 4;
            const char* names[6] = {"shot", "one", "none", "shot", "shot", "x"};
            Fn<void(__cdecl*)(const char*, int)>(original, 0x004B24E0)(names[round], round);
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        if (expected != actual) {
            ok = false;
            Compare(expected, actual, "engine glue: screenshot");
        }
    }
    delete &s;
    SetCurrentDirectoryA(previous);
    Check(ok, "engine glue: screenshot names and the front buffer's copy");
}

// The frame's end: every combination of the device lost, rendering skipped, a pending screenshot (and its exact-name
// flag), the screen blur, the letterbox, 0x00786CBD and the fog colour flags.
void TestEngineFinishRendering()
{
    GameMemory memory({{0x00786D04, 1}, {0x007D1EC0, 1}, {0x00786D94, 0x80}, {0x00786E14, 1}, {0x00787328, 4},
                       {0x0078732C, 4}, {0x00786CBE, 1}, {0x00786CBD, 1}, {0x007D6436, 1}, {0x007D643A, 1},
                       {0x0078733C, 4}, {0x00786CC0, 4}, {0x00786EBC, 8}, {0x007D69E0, 4}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    UseStandardRelocation(com);
    s.At({0x0048C100, AddressOf(&Nx::ScreenShot)}, 0, [](Regs& r) {
        Record({0xC100, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    s.At({0x0044F890, AddressOf(&Nx::ClearDevice)}, 0, [](Regs& r) {
        Record({0xF890, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
        return 0u;
    });
    bool ok = true;
    for (uint32_t combo = 0; combo < 0x200; ++combo) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Fill(0xF1A0 + combo);
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint8_t>(0x00786D04) = (combo & 1) ? 1 : 0;
            GameVariable<uint8_t>(0x007D1EC0) = (combo & 2) ? 2 : 0;
            if (!(combo & 4))
                GameVariable<uint8_t>(0x00786D94) = 0;
            else
                strcpy_s(&GameVariable<char>(0x00786D94), 0x80, "pending");
            GameVariable<uint32_t>(0x00787328) = (combo & 8) ? 0x80000000u : 0;
            GameVariable<uint8_t>(0x00786CBE) = (combo & 0x10) ? 1 : 0;
            GameVariable<uint8_t>(0x00786CBD) = (combo & 0x20) ? 1 : 0;
            GameVariable<uint8_t>(0x007D6436) = (combo & 0x40) ? 1 : 0;
            GameVariable<uint8_t>(0x007D643A) = (combo & 0x80) ? 1 : 0;
            GameVariable<uint8_t>(0x00786E14) = (combo & 0x100) ? 1 : 0;
            Fn<void(__cdecl*)()>(original, 0x004B2750)();
            std::vector<uint32_t> trace = g_trace;
            memory.Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        if (expected != actual) {
            if (ok)
                Compare(expected, actual, "engine glue: FinishRendering");
            ok = false;
        }
    }
    delete &s;
    Check(ok, "engine glue: the frame's end, every flag combination");
}

// The makers: the engine start, scenes, models, geoms, sprites, textured quads and render targets - with
// allocations that work and that fail; the scene unload; the particle manager's destructor; the texture's shadow
// casters.
void TestEngineMakers()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FA7C4, 12}});
    StandIns& s = *new StandIns();
    StandInMemory(s);
    g_relocate = ArenaRelocate;
    s.At({0x004D8650, AddressOf(&Nx::InitDirect3D)}, 0, [](Regs&) {
        Record({0x8650});
        return 0u;
    });
    StandInThis(s, {0x00496770}, 0);
    StandInThis(s, {0x004CC560}, 0);
    StandInThis(s, {0x004C6480}, 0);
    StandInThis(s, {0x004C6130}, 1);
    StandInThis(s, {0x004DE300, AddressOf(&Nx::sScene::Construct)}, 0);
    StandInThis(s, {0x004C6200}, 0);
    StandInCdecl(s, {0x004DE560, AddressOf(&Nx::DeleteScene)}, 1);
    StandInThis(s, {0x004BAD90}, 0);
    StandInThis(s, {0x004B4460}, 0);
    StandInThis(s, {0x004C70E0}, 0);
    StandInThis(s, {0x0049EED0}, 1);
    StandInThis(s, {0x004C7ED0}, 0);
    StandInThis(s, {0x004E04B0, AddressOf(&Nx::sTexture::Construct)}, 0);
    StandInThis(s, {0x004C7EF0}, 1);
    StandInThis(s, {0x004E0700, AddressOf(&Nx::sTexture::CreateRenderTarget)}, 4);
    StandInThis(s, {0x004967D0}, 0);
    StandInCdecl(s, {0x004DDC90, AddressOf(&Nx::AddShadowCaster)}, 3);
    StandInCdecl(s, {0x004DB0A0, AddressOf(&Nx::RemoveShadowCaster)}, 1);
    s.At({0x004DB100, AddressOf(&Nx::AimShadowCaster)}, 0, [](Regs& r) {
        const auto* p = reinterpret_cast<const uint32_t*>(r.args[1]);
        const auto* t = reinterpret_cast<const uint32_t*>(r.args[2]);
        Record({0xB100, Relocate(r.args[0]), p[0], p[1], p[2], t[0], t[1], t[2]});
        return 0u;
    });
    alignas(16) static uint8_t textures[3][0x20];
    alignas(16) static uint8_t keyHolder[8];
    alignas(16) static float camera[0x40];
    bool ok = true;
    for (int round = 0; round < 8; ++round) {
        auto run = [&](bool original) {
            g_trace.clear();
            g_used = 0;
            std::memset(g_arena, 0, sizeof(g_arena));
            memory.Fill(0x3A1E + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            Lcg random(0x77 + round);
            g_failAllocations.clear();
            for (int i = 0; i < 16; ++i)
                g_failAllocations.push_back(round > 0 && random.Below(4) == 0);
            std::vector<uint32_t> trace;
            Fn<void(__cdecl*)()>(original, 0x004B25F0)();
            for (uint32_t address : {0x006FA7C4u, 0x006FA7C8u, 0x006FA7CCu})
                trace.push_back(ArenaRelocate(GameVariable<uint32_t>(address)));
            g_failAllocations.clear(); // (a scene and a render target: never null - the original writes through it)
            void* scene = Fn<void*(__cdecl*)(const char*, void*, int)>(original, 0x004B28C0)(
                "name", reinterpret_cast<void*>(0x1234), round & 1 ? 0x101 : 0x100);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(scene)));
            trace.push_back(Fn<uint32_t(__cdecl*)(void*)>(original, 0x004B2990)(scene) & 0xFF);
            void* target = Fn<void*(__cdecl*)(int, int, int, int)>(original, 0x004B2B60)(64 << round, 32 + round,
                                                                                         7 + round, -round);
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(target)));
            for (int i = 0; i < 8; ++i)
                g_failAllocations.push_back(round > 0 && random.Below(3) == 0);
            for (uint32_t address : {0x004B29C0u, 0x004B2A20u, 0x004B2A80u, 0x004B2AF0u})
                trace.push_back(ArenaRelocate(Fn<uint32_t(__cdecl*)(int)>(original, address)(round)));
            // the particle manager's destructor: freed with flag 1 only
            void* manager = g_arena + 0x7000;
            trace.push_back(ArenaRelocate(
                Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004B2710)(manager, round == 3 ? 0 : round == 5 ? 2 : round | 1)));
            // the shadow casters: a texture with its key at +0x10, and one through a pointer (flag 1)
            for (int i = 0; i < 3; ++i) {
                std::memset(textures[i], 0, sizeof(textures[i]));
                textures[i][8] = static_cast<uint8_t>(i == 1 ? 0x41 : 0x40 + 2 * i);
            }
            *reinterpret_cast<uint32_t*>(textures[0] + 0x10) = 0x7E700000;
            *reinterpret_cast<uint8_t**>(textures[1] + 0x10) = keyHolder;
            *reinterpret_cast<uint32_t*>(keyHolder) = 0x7E710000 + round;
            *reinterpret_cast<uint32_t*>(textures[2] + 0x10) = 0x7E720000;
            for (int i = 0; i < 0x40; ++i)
                camera[i] = static_cast<float>(random.Below(20001)) / 7.0f - 1000.0f;
            for (int i = 0; i < 3; ++i) {
                Fn<void(__cdecl*)(const uint8_t*, uint32_t, uint32_t)>(original, 0x004B2C30)(textures[i], 0x30DE0 + i,
                                                                                         0x99);
                Fn<void(__cdecl*)(const uint8_t*, const void*)>(original, 0x004B2C50)(textures[i], camera);
                Fn<void(__cdecl*)(const uint8_t*)>(original, 0x004B2D10)(textures[i]);
            }
            for (uint32_t word : g_trace)
                trace.push_back(word);
            memory.Dump(trace);
            for (uint32_t i = 0; i < sizeof(g_arena); i += 4)
                trace.push_back(ArenaRelocate(*reinterpret_cast<uint32_t*>(g_arena + i)));
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        if (expected != actual) {
            if (ok)
                Compare(expected, actual, "engine glue: makers");
            ok = false;
        }
    }
    g_failAllocations.clear();
    delete &s;
    g_relocate = nullptr;
    Check(ok, "engine glue: the engine start, scene / model / geom / sprite / quad / render target makers, shadow casters");
}

} // namespace

SELFTEST(Early, TestEngineScreenShot, RemoveStandIns);
SELFTEST(Early, TestEngineFinishRendering, RemoveStandIns);
SELFTEST(Early, TestEngineMakers, RemoveStandIns);
