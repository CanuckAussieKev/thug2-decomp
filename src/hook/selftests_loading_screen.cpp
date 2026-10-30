// Self-tests for the loading screen (src/game/gfx/loading_screen.cpp, RFX-6): the original and ours with a recording
// stand-in device, the lock / timer imports stood in, the render-state calls, the picture's holder and its load
// stood in; traces and the globals compared.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/loading_bar.h"
#include "game/gfx/loading_screen.h"
#include "game/gfx/render_state.h"
#include "game/gfx/engine.h"
#include "game/gfx/pc_lights.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

constexpr uint32_t kDevice = 0xD3D00000;
bool g_loads;           // what the picture's load answers
void* g_holderVtable[1]; // the pictures' (slot 0: destroy, recorded)

void StandInScreenCalls(StandIns& s)
{
    StandInMemory(s);
    s.Bytes(0x006450BC, Dword(reinterpret_cast<uint32_t>(s.Thunk(8, [](Regs& r) {
        Record({0x50BC, r.args[0], r.args[1]});
        return 0u;
    }))));
    s.Bytes(0x006450C0, Dword(reinterpret_cast<uint32_t>(s.Thunk(4, [](Regs& r) {
        Record({0x50C0, r.args[0]});
        return 1u;
    }))));
    s.Bytes(0x00645264, Dword(reinterpret_cast<uint32_t>(s.Thunk(20, [](Regs& r) {
        // (the callback: the game's address in the original, ours in ours - the same function)
        bool frame = r.args[2] == 0x004B6FF0 || r.args[2] == AddressOf(&Nx::DrawLoadingScreenFrame) ||
                     reinterpret_cast<void*>(r.args[2]) == reinterpret_cast<void*>(&Nx::DrawLoadingScreenFrame);
        Record({0x5264, r.args[0], r.args[1], frame ? 1u : r.args[2], r.args[3], r.args[4]});
        return 0x7133u;
    }))));
    s.Bytes(0x00645268, Dword(reinterpret_cast<uint32_t>(s.Thunk(4, [](Regs& r) {
        Record({0x5268, r.args[0]});
        return 0u;
    }))));
    s.At({0x004DAB60, AddressOf(&Nx::SetState)}, 0, [](Regs& r) {
        Record({0xAB60, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004D9DB0, AddressOf(&Nx::SetVertexFormat)}, 0, [](Regs& r) {
        Record({0x9DB0, r.args[0]});
        return 0u;
    });
    s.At({0x004DA730, AddressOf(&Nx::SetTexture)}, 0, [](Regs& r) {
        Record({0xA730, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004DA760, AddressOf(&Nx::SetBlendMode)}, 0, [](Regs& r) {
        Record({0xA760, r.args[0]});
        return 0u;
    });
    s.At({0x004DAA80, AddressOf(&Nx::SetPixelShader)}, 0, [](Regs& r) {
        Record({0xAA80, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004B6B80, AddressOf(&Nx::UpdateLoadingBarColors)}, 0, [](Regs&) {
        Record({0x6B80});
        return 0u;
    });
    s.At({0x004C7ED0}, 0, [](Regs& r) { // the picture's holder: its texture at +0x10 (directly)
        Record({0x7ED0, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 0x14);
        *reinterpret_cast<void***>(r.ecx) = g_holderVtable;
        uint32_t texture = Bump(0x20);
        *reinterpret_cast<uint32_t*>(texture + 0x10) = 0x7E700000;
        *reinterpret_cast<uint32_t*>(r.ecx + 0x10) = texture;
        return r.ecx;
    });
    g_holderVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xDE57, Relocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x0049F220}, 12, [](Regs& r) {
        Record({0xF220, Relocate(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2]});
        return g_loads ? 0x101u : 0x100u;
    });
    StandInCdecl(s, {0x0048C080, AddressOf(&Nx::DeleteModelLights)}, 1);
    s.At({0x006234FE}, 0, StandInSprintf); // (the game's C runtime, for the original)
}

// The picture drawn (none, direct, through a pointer), the screen hidden (with and without the timer), the sprite
// freed, the bar started (with a timer already, and not), the screen shown kept / black / with a picture - the same
// name and another, each language, loading and not.
void TestLoadingScreen()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x007052A0, 0x18}, {0x00786D04, 8},
                       {0x00787338, 4}, {0x0067E958, 0x1C}, {0x007CEFA0, 4}, {0x00786D6C, 8}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    StandInScreenCalls(s);
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    com.On(kDevice, kGetSamplerState, [](Regs& r) {
        *reinterpret_cast<DWORD*>(r.args[3]) = 0x3A3;
        return 0u;
    });
    com.On(kDevice, kDrawPrimitiveUP, [](Regs& r) {
        Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), 4 * r.args[4])});
        return 0u;
    });
    bool ok = true, reported = false;
    for (int round = 0; round < 12; ++round) {
        ok &= CompareTwoRuns("loading screen", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x10AD + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<int>(0x00786D6C) = 640 + 16 * round;
            GameVariable<int>(0x00786D70) = 480 + 9 * round;
            GameVariable<int>(0x007CEFA0) = round % 3 == 0 ? 3 : round % 3 == 1 ? 5 : 1;
            // the picture's name: a short string in place
            uint8_t* name = &GameVariable<uint8_t>(0x0067E958);
            std::memset(name, 0, 0x1C);
            strcpy_s(reinterpret_cast<char*>(name + 4), 16, round & 1 ? "loading" : "other");
            *reinterpret_cast<uint32_t*>(name + 0x14) = round & 1 ? 7 : 5;
            *reinterpret_cast<uint32_t*>(name + 0x18) = 15;
            std::vector<uint32_t> trace;
            // a picture there (round 0: none; round 2: its texture through a pointer)
            uint8_t* holder = round == 0 ? nullptr : reinterpret_cast<uint8_t*>(Bump(0x14));
            if (holder) {
                std::memset(holder, 0, 0x14);
                *reinterpret_cast<void***>(holder) = g_holderVtable;
                uint32_t texture = Bump(0x20);
                *reinterpret_cast<uint32_t*>(texture + 0x10) = 0x7E710000 + round;
                if (round == 2) {
                    uint32_t pointer = Bump(4);
                    *reinterpret_cast<uint32_t*>(pointer) = texture;
                    *reinterpret_cast<uint32_t*>(holder + 0x10) = pointer;
                    holder[8] = 1;
                } else {
                    *reinterpret_cast<uint32_t*>(holder + 0x10) = texture;
                    holder[8] = 2;
                }
            }
            GameVariable<uint8_t*>(0x007052B4) = holder;
            Fn<void(__cdecl*)()>(original, 0x004B6CD0)();
            GameVariable<uint32_t>(0x00786D08) = round & 2 ? 0x55 : 0;
            Fn<void(__cdecl*)()>(original, 0x004B6F10)();
            trace.push_back(GameVariable<uint32_t>(0x00786D08));
            GameVariable<uint32_t>(0x007052B0) = round & 4 ? 0x5B : 0;
            Fn<void(__cdecl*)()>(original, 0x004B6F70)();
            GameVariable<uint32_t>(0x00786D08) = round & 8 ? 0x66 : 0;
            Fn<void(__cdecl*)(float)>(original, 0x004B7400)(1.5f + 0.37f * round);
            g_loads = round != 5;
            int mode = round % 4; // kept, black, the same picture, another
            Fn<void(__cdecl*)(const char*, int, int)>(original, 0x004B78B0)(
                mode == 3 ? "nextpic" : round & 1 ? "loading" : "other", mode == 0 ? 0x101 : 0x100,
                mode == 1 ? 0x201 : 0x200);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "loading screen: the picture, hiding, the sprite, the bar's start, showing");
}

} // namespace

SELFTEST(Early, TestLoadingScreen, RemoveStandIns);
