// Self-tests for the renderer's cached device state and the device's start-up objects (appmain batch 1:
// src/game/gfx/render_state.cpp, device.cpp): the original and ours run on identical game memory against a
// recording stand-in Direct3D device (every method call is traced with its arguments), and their traces and the
// memory they leave are compared.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "game/gfx/device.h"
#include "game/gfx/render_state.h"
#include "game/gfx/text.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

constexpr uint32_t kDevice = 0xD3D00000;
constexpr uint32_t kD3D = 0xD3900000;

// The renderer's globals (the start-up flags, present parameters, caches, constants, fog...).
GameMemory* RendererMemory()
{
    return new GameMemory({{0x00786630, 0xD30}, {0x007CA940, 0x80}, {0x007D69DC, 8}, {0x007D6434, 4},
                           {0x0067DA40, 1}, {0x007CEFA0, 4}, {0x007D643A, 1}, {0x007D6A28, 4}});
}

// The device in 0x007D69E0 for the run.
IDirect3DDevice9*& DeviceSlot() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

// --- the cached render state ------------------------------------------------------------------------------------

// Long random sequences of every setter, from random caches (made of the values the setters are given, so both
// "already set" and "changed" happen), with the capability flags and locks changing.
void TestRenderStateCache()
{
    std::unique_ptr<GameMemory> memory(RendererMemory());
    StandIns& s = *new StandIns(); // (deleted at the end, or by RemoveStandIns after a crash)
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    UseStandardRelocation(com);
    static const uint32_t kValues[] = {0,       1,       2,          3,          7,       0x10000,    0x10001,
                                       0x20000, 0x20002, 0x30000,    0x10002,    0x20001, 0xFFFFFFFF, 0x80000000,
                                       0x2C,    0xFFFF,  0xFFFF0000, 0x00010003, 0x40,    0x12345678};
    static const uint32_t kModes[] = {0,    1,    2,    3,    4,    5,    6,    7,    8,    9,    10,   11,
                                      12,   13,   14,   15,   16,   17,   18,   0x40, 0x41, 0x7F, 0x80, 0xFF};
    static const uint32_t kAlphas[] = {0, 0x40000000, 0x7F000000, 0x80000000, 0xFF000000, 0x01000000};
    void* shaders[8] = {reinterpret_cast<void*>(0x5ADE0001), reinterpret_cast<void*>(0x5ADE0002),
                        reinterpret_cast<void*>(0x5ADE0003), reinterpret_cast<void*>(0x5ADE0004),
                        reinterpret_cast<void*>(0x5ADE0005), reinterpret_cast<void*>(0x5ADE0006),
                        reinterpret_cast<void*>(0x5ADE0007), nullptr};
    float material[0x14];

    auto run = [&](bool original, int variant) {
        g_trace.clear();
        memory->Fill(0x5EED0000 + variant);
        DeviceSlot() = reinterpret_cast<IDirect3DDevice9*>(device);
        Lcg random(0xC0FFEE00 + variant);
        // The caches: values the setters get.
        for (uint32_t address = 0x00786E30; address < 0x00786EC8; address += 4)
            GameVariable<uint32_t>(address) = random.Pick(kValues);
        GameVariable<uint32_t>(0x00786E30) = random.Pick(kModes) | random.Pick(kAlphas);
        GameVariable<uint32_t>(0x00787338) = random.Below(2);
        GameVariable<uint8_t>(0x00787348) = static_cast<uint8_t>(random.Below(3));
        // The shader handles: distinct, or (variant 1) all the same - the order of the tests matters then.
        for (int i = 0; i < 9; ++i)
            GameVariable<void*>(0x007CA944 + 4 * i) = variant == 1 ? nullptr : shaders[i % 7];
        if (variant == 3) { // 0x007CA944-58 the same handle: the first test that matches wins
            for (int i = 0; i < 9; ++i)
                GameVariable<void*>(0x007CA944 + 4 * i) = shaders[0];
            GameVariable<void*>(0x007CA964) = shaders[1];
            GameVariable<void*>(0x007CA95C) = shaders[2];
        }
        Originals guard(original, {0x004DAB60, 0x004D9DB0, 0x004DA730, 0x004DA760, 0x004D9DE0, 0x004DAA10,
                                   0x004DAA80, 0x004DAAF0, 0x004DA0E0, 0x004DB620});
        for (int step = 0; step < 4000; ++step) {
            switch (random.Below(48)) {
            case 0: GameVariable<bool>(0x00786AC0) = random.Below(2) != 0; break;
            case 1: GameVariable<bool>(0x00786AC1) = random.Below(2) != 0; break;
            case 2: GameVariable<bool>(0x00786AB8) = random.Below(2) != 0; break;
            case 3: GameVariable<uint32_t>(0x00786F34) = random.Below(3) ? 0 : random.Pick(kModes) | random.Pick(kAlphas); break;
            case 4: GameVariable<uint32_t>(0x00786F28) = random.Below(4) == 0; break;
            case 5: GameVariable<uint32_t>(0x00786EC4) = random.Below(4) == 0; break;
            case 6: GameVariable<uint32_t>(0x0078733C) = random.Next(); break;
            case 7:
                Fn<void(__cdecl*)(uint32_t)>(original, 0x004D9DB0)(random.Pick(kValues));
                break;
            case 8:
                Fn<void(__cdecl*)(int, void*)>(original, 0x004DA730)(random.Below(4), shaders[random.Below(8)]);
                break;
            case 9:
            case 10:
            case 11:
            case 12: {
                uint32_t mode = random.Pick(kModes) | random.Pick(kAlphas);
                if (random.Below(8) == 0)
                    mode = random.Next() << 4;
                Fn<void(__cdecl*)(uint32_t)>(original, 0x004DA760)(mode);
                break;
            }
            case 13:
                Fn<void(__cdecl*)(void*, uint32_t)>(original, 0x004D9DE0)(shaders[random.Below(8)], random.Next());
                break;
            case 14: {
                for (float& f : material)
                    f = static_cast<float>(static_cast<int>(random.Below(4000)) - 1000) / 997.0f;
                bool none = random.Below(4) == 0;
                Fn<void(__cdecl*)(void*, const void*)>(original, 0x004DAA10)(shaders[random.Below(8)],
                                                                             none ? nullptr : material);
                break;
            }
            case 15: {
                for (float& f : material)
                    f = static_cast<float>(random.Below(1000)) / 999.0f;
                bool none = random.Below(4) == 0;
                Fn<void(__cdecl*)(void*, const void*)>(original, 0x004DAA80)(shaders[random.Below(8)],
                                                                             none ? nullptr : material);
                break;
            }
            case 16:
                Fn<void(__cdecl*)(void*, uint32_t)>(original, 0x004DAAF0)(shaders[random.Below(8)], random.Below(6));
                break;
            case 17: {
                GameVariable<uint32_t>(0x007CA978) = random.Next();
                float f = Fn<float(__cdecl*)()>(original, 0x004DA0E0)();
                Record({0xF10A7, std::bit_cast<uint32_t>(f)});
                break;
            }
            case 18: {
                float m[16];
                for (float& f : m)
                    f = static_cast<float>(random.Next() & 0xFFFF);
                Fn<void(__cdecl*)(const float*)>(original, 0x004DB620)(random.Below(3) ? m : nullptr);
                Record({GameVariable<uint32_t>(0x007CA968)});
                break;
            }
            default: { // the state setter, most of the time
                int state = static_cast<int>(random.Below(0x28)) - 1;
                Fn<void(__cdecl*)(int, uint32_t)>(original, 0x004DAB60)(state, random.Pick(kValues));
                break;
            }
            }
        }
        std::vector<uint32_t> trace = g_trace;
        memory->Dump(trace);
        return trace;
    };
    for (int variant = 0; variant < 4; ++variant) {
        std::vector<uint32_t> expected = run(true, variant);
        std::vector<uint32_t> actual = run(false, variant);
        Compare(expected, actual, "appmain: the render state cache (states, blend modes, textures, shaders)");
    }
    g_relocate = nullptr;
    delete &s;
}

// ResetRenderState then ApplyRenderState, at back buffers of 480 and 481 lines, anisotropy 2 / 3 / 16,
// antialiasing on / off (the two callees stood in).
void TestRenderStateDefaults()
{
    std::unique_ptr<GameMemory> memory(RendererMemory());
    StandIns& s = *new StandIns(); // (deleted at the end, or by RemoveStandIns after a crash)
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    UseStandardRelocation(com);
    com.On(kDevice, kSetMaterial, [](Regs& r) {
        const auto* m = reinterpret_cast<const uint32_t*>(r.args[1]);
        for (int i = 0; i < 17; ++i)
            Record({m[i]});
        return 0u;
    });
    s.At({0x004CD940}, 0, [](Regs&) { Record({0xD940}); return 0u; });
    s.At({0x004DD7F0}, 0, [](Regs&) { Record({0xD7F0}); return 0u; });
    struct Case {
        int height;
        uint32_t anisotropy;
        bool antialias;
        bool reset;
    };
    static const Case kCases[] = {{480, 2, false, true}, {481, 3, true, true}, {200, 16, true, false},
                                  {1024, 0, false, false}, {-5, 7, false, true}};
    for (const Case& c : kCases) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0xDEFA0000 + c.height);
            DeviceSlot() = reinterpret_cast<IDirect3DDevice9*>(device);
            GameVariable<int>(0x00786D70) = c.height;
            GameVariable<uint32_t>(0x00786ABC) = c.anisotropy;
            GameVariable<bool>(0x007D6434) = c.antialias;
            Originals guard(original, {0x004D7D70, 0x004D80D0});
            if (c.reset)
                Fn<void(__cdecl*)()>(original, 0x004D7D70)();
            Fn<void(__cdecl*)()>(original, 0x004D80D0)();
            std::vector<uint32_t> trace = g_trace;
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the render state defaults and sending them to the device");
    }
    g_relocate = nullptr;
    delete &s;
}

// --- the device's start-up -----------------------------------------------------------------------------------

std::vector<const char*> g_soundDevices;

// CreateDevice: hardware / software vertices x multisampling, failing or not; adapters with "8500" in the
// description or not, Intel 915G or other ids; sound devices listed with the Realtek one last, not last, a name
// over 200 characters (cut, unterminated) and exactly 200.
void TestCreateDevice()
{
    std::unique_ptr<GameMemory> memory(RendererMemory());
    StandIns& s = *new StandIns(); // (deleted at the end, or by RemoveStandIns after a crash)
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    FakeCom* d3d = com.Direct3D(kD3D);
    UseStandardRelocation(com);
    static std::string longName(230, 'x'), name200(200, 'y');
    for (size_t i = 0; i < longName.size(); ++i)
        longName[i] = static_cast<char>('a' + i % 26);
    struct Case {
        bool hardware, multisample;
        HRESULT result;
        const char* description;
        uint32_t vendor, id;
        std::vector<const char*> sound;
    };
    const Case cases[] = {
        {true, false, D3D_OK, "RADEON 8500 SERIES", 0x8086, 0x2582, {"Primary", "Realtek HD Audio rear output"}},
        {false, true, D3DERR_NOTAVAILABLE, "GeForce 6800", 0x8086, 0x2582, {"Realtek HD Audio rear output"}},
        {true, true, D3D_OK, "Intel 915G", 0x8086, 0x2583, {"Realtek HD Audio rear output"}},
        {false, false, D3D_OK, "Intel 915G", 0x10DE, 0x2582, {"Realtek HD Audio rear output"}},
        {true, false, D3D_OK, "Intel 915G 8500", 0x8086, 0x2582, {"Realtek HD Audio rear output", "Other"}},
        {true, false, D3D_OK, "x", 0x8086, 0x2582, {"Realtek HD Audio rear output2"}},
        {true, false, E_FAIL, "", 0, 0, {longName.c_str()}},
        {false, true, D3D_OK, "850", 1, 2, {name200.c_str()}},
        {true, true, D3D_OK, "85008500", 3, 4, {}},
    };
    const Case* current = nullptr;
    com.On(kD3D, 16, [&](Regs& r) {
        *reinterpret_cast<FakeCom**>(r.args[6]) = device;
        auto* pp = reinterpret_cast<const uint32_t*>(r.args[5]);
        for (int i = 0; i < 14; ++i)
            Record({pp[i]});
        return static_cast<uint32_t>(current->result);
    });
    com.On(kD3D, 5, [&](Regs& r) {
        auto* id = reinterpret_cast<D3DADAPTER_IDENTIFIER9*>(r.args[3]);
        Record({HashBytes(reinterpret_cast<const uint8_t*>(id), sizeof(*id))}); // (zeroed first)
        std::memcpy(id->Description, current->description, std::strlen(current->description) + 1);
        id->VendorId = current->vendor;
        id->DeviceId = current->id;
        return 0u;
    });
    void* enumerate = s.Thunk(8, [&](Regs& r) {
        Record({0xD50, r.args[1]});
        auto callback = reinterpret_cast<BOOL(CALLBACK*)(GUID*, const char*, const char*, void*)>(r.args[0]);
        for (const char* name : current->sound)
            Record({static_cast<uint32_t>(callback(nullptr, name, "module", reinterpret_cast<void*>(r.args[1])))});
        return 0u;
    });
    s.Bytes(0x0064502C, Dword(reinterpret_cast<uint32_t>(enumerate)));
    for (const Case& c : cases) {
        current = &c;
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0x0DE71CE0 + static_cast<uint32_t>(&c - cases));
            DeviceSlot() = nullptr;
            GameVariable<FakeCom*>(0x007D69DC) = d3d;
            HRESULT result = Fn<HRESULT(__cdecl*)(int, int, bool, bool)>(original, 0x004D7C80)(
                640 + static_cast<int>(&c - cases), 480, c.hardware, c.multisample);
            std::vector<uint32_t> trace = g_trace;
            trace.push_back(static_cast<uint32_t>(result));
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: CreateDevice (and the sound device callback)");
    }
    g_relocate = nullptr;
    delete &s;
}

// CreateRenderTargets (and ApplyRenderState after it) with capabilities that set every flag / none, depth
// textures or not, D16 or D24 depth, sizes that don't divide; ReleaseRenderTargets after, with and without the
// depth texture target.
void TestRenderTargets()
{
    std::unique_ptr<GameMemory> memory(RendererMemory());
    StandIns& s = *new StandIns(); // (deleted at the end, or by RemoveStandIns after a crash)
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    UseStandardRelocation(com);
    std::vector<FakeCom*> textures, surfaces;
    for (uint32_t i = 0; i < 12; ++i) {
        textures.push_back(com.Texture(0x7E000000 + (i << 12)));
        surfaces.push_back(com.Surface(0x5F000000 + (i << 12)));
    }
    size_t nextTexture = 0, nextSurface = 0;
    bool noSurfaces = false;
    bool firstSurfaceNull = false;
    int levels = 0;
    D3DCAPS9 caps = {};
    com.On(kDevice, kCreateTexture, [&](Regs& r) {
        *reinterpret_cast<FakeCom**>(r.args[7]) = textures[nextTexture++ % textures.size()];
        return 0u;
    });
    for (uint32_t i = 0; i < 12; ++i) {
        com.On(0x7E000000 + (i << 12), 18, [&](Regs& r) {
            bool none = noSurfaces || (firstSurfaceNull && levels == 0);
            ++levels;
            *reinterpret_cast<FakeCom**>(r.args[2]) = none ? nullptr : surfaces[nextSurface++ % surfaces.size()];
            return 0u;
        });
        com.On(0x5F000000 + (i << 12), 12, [i](Regs& r) {
            auto* desc = reinterpret_cast<D3DSURFACE_DESC*>(r.args[1]);
            std::memset(desc, 0, sizeof(*desc));
            desc->Format = static_cast<D3DFORMAT>(0x40 + i);
            return 0u;
        });
    }
    auto giveSurface = [&](int argument) {
        return [&, argument](Regs& r) {
            *reinterpret_cast<FakeCom**>(r.args[argument]) = surfaces[nextSurface++ % surfaces.size()];
            return 0u;
        };
    };
    com.On(kDevice, kCreateDepthStencilSurface, giveSurface(7));
    com.On(kDevice, kCreateRenderTarget, giveSurface(7));
    com.On(kDevice, kGetRenderTarget, giveSurface(2));
    com.On(kDevice, kGetDepthStencilSurface, giveSurface(1));
    com.On(kDevice, kGetDeviceCaps, [&](Regs& r) {
        std::memcpy(reinterpret_cast<void*>(r.args[1]), &caps, sizeof(caps));
        return 0u;
    });
    for (uint32_t address : {0x005F4620u, 0x005F4690u}) {
        s.At({address, Resolve(OursAt(address))}, 0, [address](Regs& r) { Record({address, r.args[0]}); return 0u; });
    }
    for (uint32_t address : {0x005F4650u, 0x005F4670u, 0x005F52D0u, 0x004CD940u, 0x004DD7F0u}) {
        uint32_t ours = OursAt(address) ? Resolve(OursAt(address)) : address; // (0x004CD940 isn't ours yet)
        s.At({address, ours}, 0, [address](Regs&) { Record({address}); return 0u; });
    }
    struct Case {
        uint32_t width, height, depthFormat;
        bool depthTextures;
        uint32_t psVersion, devCaps2, addressCaps, anisotropy, srcBlend, destBlend;
        bool noSurfaces;
        bool firstSurfaceNull;
    };
    static const Case kCases[] = {
        {1280, 1024, D3DFMT_D24X8, true, 0xFFFF0101, 1, 8, 16, 0x2000, 0x2000, false, false},
        {1023, 767, D3DFMT_D16, false, 0xFFFF0100, 2, 7, 1, 0x2000, 0x1FFF, false, false},
        {641, 479, D3DFMT_D24S8, true, 0xFFFF0200, 0xFFFFFFFF, 0xFFFFFFF7, 0, 0x1FFF, 0x2000, true, false},
        {17, 3, D3DFMT_D16, false, 0xFFFE0300, 0, 0, 2, 0xFFFFFFFF, 0xFFFFFFFF, false, false},
        {800, 600, D3DFMT_D24X8, false, 0xFFFF0101, 0, 8, 4, 0x2000, 0x2000, false, true},
    };
    for (const Case& c : kCases) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0x7A26E700 + c.width);
            nextTexture = nextSurface = 0;
            noSurfaces = c.noSurfaces;
            firstSurfaceNull = c.firstSurfaceNull;
            levels = 0;
            DeviceSlot() = reinterpret_cast<IDirect3DDevice9*>(device);
            D3DPRESENT_PARAMETERS& pp = GameVariable<D3DPRESENT_PARAMETERS>(0x00786A7C);
            pp.BackBufferWidth = c.width;
            pp.BackBufferHeight = c.height;
            pp.AutoDepthStencilFormat = static_cast<D3DFORMAT>(c.depthFormat);
            GameVariable<bool>(0x00786AB9) = c.depthTextures;
            GameVariable<void*>(0x00786D18) = nullptr;
            for (uint32_t flag : {0x00786AB8u, 0x00786AB6u, 0x00786AC0u})
                GameVariable<uint8_t>(flag) = 0;
            caps = {};
            caps.PixelShaderVersion = c.psVersion;
            caps.DevCaps2 = c.devCaps2;
            caps.TextureAddressCaps = c.addressCaps;
            caps.MaxAnisotropy = c.anisotropy;
            caps.SrcBlendCaps = c.srcBlend;
            caps.DestBlendCaps = c.destBlend;
            Originals guard(original, {0x004D8420, 0x004D80D0, 0x004D7AF0});
            Fn<void(__cdecl*)()>(original, 0x004D8420)();
            std::vector<uint32_t> trace = g_trace;
            memory->Dump(trace);
            g_trace.clear();
            if (c.noSurfaces || c.firstSurfaceNull) // (the original would release the null surfaces)
                return trace;
            Fn<void(__cdecl*)()>(original, 0x004D7AF0)();
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: CreateRenderTargets / ReleaseRenderTargets");
    }
    g_relocate = nullptr;
    delete &s;
}

// LoadGamma: the key missing (0.25 each), present with all three values, with some missing.
void TestLoadGamma()
{
    StandIns& s = *new StandIns(); // (deleted at the end, or by RemoveStandIns after a crash)
    LSTATUS openResult = ERROR_SUCCESS;
    int queries = 0;
    int missing = -1;
    StandInApi(s, 0x00645004, reinterpret_cast<const void*>(&RegOpenKeyExA), 20, [&](Regs& r) {
        Record({0x0BE4, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], r.args[3]});
        if (openResult == ERROR_SUCCESS)
            *reinterpret_cast<HKEY*>(r.args[4]) = reinterpret_cast<HKEY>(0x4E7A0001);
        return static_cast<uint32_t>(openResult);
    });
    StandInApi(s, 0x00645008, reinterpret_cast<const void*>(&RegQueryValueExA), 24, [&](Regs& r) {
        auto* size = reinterpret_cast<DWORD*>(r.args[5]);
        Record({0x0BE5, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], r.args[3], *size});
        int index = queries++;
        *size = 2 + index; // (what's left in the size: the next query must set it again)
        if (index == missing)
            return static_cast<uint32_t>(ERROR_FILE_NOT_FOUND);
        float value = 0.5f + 0.125f * index;
        std::memcpy(reinterpret_cast<void*>(r.args[4]), &value, 4);
        return 0u;
    });
    s.At({0x004D07B0}, 0, [](Regs& r) { Record({0x07B0, r.args[0], r.args[1], r.args[2]}); return 0u; });
    for (int c = 0; c < 5; ++c) {
        auto run = [&](bool original) {
            g_trace.clear();
            openResult = c == 0 ? ERROR_FILE_NOT_FOUND : ERROR_SUCCESS;
            queries = 0;
            missing = c - 2;
            Fn<void(__cdecl*)()>(original, 0x004D7B60)();
            return g_trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: LoadGamma");
    }
    delete &s;
}

// ShowDiscError: the timer killed or not there, the device made if missing, each language - run for some frames
// and ended from the stand-in Present.
int g_presents;
int g_frames;
void TestShowDiscError()
{
    std::unique_ptr<GameMemory> memory(RendererMemory());
    StandIns& s = *new StandIns(); // (deleted at the end, or by RemoveStandIns after a crash)
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    UseStandardRelocation(com);
    com.On(kDevice, kPresent, [](Regs& r) {
        if (++g_presents > g_frames)
            r.ret = EscapeTarget();
        return 0u;
    });
    void* kill = s.Thunk(4, [](Regs& r) { Record({0x7111, r.args[0]}); return 0u; });
    s.Bytes(0x00645268, Dword(reinterpret_cast<uint32_t>(kill)));
    s.At({0x004D8650, AddressOf(&Nx::InitDirect3D)}, 0, [](Regs&) { Record({0x8650}); return 0u; });
    auto textFields = [](uint32_t tag, Regs& r) {
        const auto* t = reinterpret_cast<const Nx::SText*>(r.ecx);
        const auto* words = reinterpret_cast<const uint32_t*>(r.ecx);
        Record({tag, words[1], words[2], words[3], std::bit_cast<uint32_t>(t->z), reinterpret_cast<uint32_t>(t->font),
                reinterpret_cast<uint32_t>(t->effects), Hash(t->text), std::bit_cast<uint32_t>(t->x),
                std::bit_cast<uint32_t>(t->y), std::bit_cast<uint32_t>(t->xscale), std::bit_cast<uint32_t>(t->yscale),
                t->color, t->fixedColor});
    };
    s.At({0x004CEED0, AddressOf(&Nx::SText::Construct)}, 4, [](Regs& r) {
        std::memset(reinterpret_cast<void*>(r.ecx), 0x3C, sizeof(Nx::SText));
        Record({0xEED0, r.args[0]});
        return r.ecx;
    });
    s.At({0x004CEF00, AddressOf(&Nx::SText::ResetVertices)}, 0, [&](Regs& r) { textFields(0xEF00, r); return 0u; });
    s.At({0x004CFD00, AddressOf(&Nx::SText::Draw)}, 0, [&](Regs& r) { textFields(0xFD00, r); return 0u; });
    s.At({0x004CEF10, AddressOf(&Nx::SText::DrawVertices)}, 0, [&](Regs& r) { textFields(0xEF10, r); return 0u; });
    struct Case {
        uint32_t timer;
        bool deviceMade;
        int language;
        int frames;
    };
    static const Case kCases[] = {{0x77, true, 0, 3}, {0, false, 3, 2}, {5, true, 5, 1}, {0, true, 4, 0}};
    for (const Case& c : kCases) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0xD15C0000 + c.language);
            DeviceSlot() = reinterpret_cast<IDirect3DDevice9*>(device);
            GameVariable<uint32_t>(0x00786D08) = c.timer;
            GameVariable<void*>(0x00786D0C) = c.deviceMade ? device : nullptr;
            GameVariable<int>(0x007CEFA0) = c.language;
            GameVariable<uint32_t>(0x00786D90) = 0xF0E70000;
            g_presents = 0;
            g_frames = c.frames;
            {
                Originals guard(original, {0x004D89C0, 0x004DA730, 0x004DA760, 0x004DAB60});
                CallEscapable(Code(original, 0x004D89C0));
            }
            std::vector<uint32_t> trace = g_trace;
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: ShowDiscError");
    }
    g_relocate = nullptr;
    delete &s;
}

} // namespace

SELFTEST(Early, TestRenderStateCache, RemoveStandIns);
SELFTEST(Early, TestRenderStateDefaults, RemoveStandIns);
SELFTEST(Early, TestCreateDevice, RemoveStandIns);
SELFTEST(Early, TestRenderTargets, RemoveStandIns);
SELFTEST(Early, TestLoadGamma, RemoveStandIns);
SELFTEST(Early, TestShowDiscError, RemoveStandIns);
