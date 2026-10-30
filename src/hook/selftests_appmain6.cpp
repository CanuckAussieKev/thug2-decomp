// Self-tests for the full-screen effects and the 2D draw list (appmain batch 6: src/game/gfx/screen_fx.cpp,
// draw2d.cpp, the SSprite methods in sprite.cpp): the original and ours against a recording stand-in device and
// screen-copy textures; every device call with its arguments (vertex data hashed) and the memory compared.

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "game/core/manager.h"
#include "game/gfx/draw2d.h"
#include "game/gfx/screen_fx.h"
#include "game/gfx/sprite.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/engine.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

constexpr uint32_t kDevice = 0xD3D00000;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

// Nine screen copies (textures with a surface each), sizes given per test.
struct Screens {
    StandIns& s;
    FakeComSet com;
    FakeCom* device;
    FakeCom* textures[10];
    FakeCom* surfaces[10];
    uint32_t widths[10], heights[10];
    Screens() : s(*new StandIns()), com(&s)
    {
        device = com.Device(kDevice);
        for (uint32_t i = 0; i < 10; ++i) {
            uint32_t tag = 0x7E000000 + (i << 16);
            textures[i] = com.Texture(tag);
            surfaces[i] = com.Surface(0x5F000000 + (i << 16));
            com.On(tag, 18, [this, i](Regs& r) { // GetSurfaceLevel
                *reinterpret_cast<FakeCom**>(r.args[2]) = surfaces[i];
                return 0u;
            });
            com.On(tag, 17, [this, i](Regs& r) { // GetLevelDesc
                auto* desc = reinterpret_cast<D3DSURFACE_DESC*>(r.args[2]);
                std::memset(desc, 0, sizeof(*desc));
                desc->Width = widths[i];
                desc->Height = heights[i];
                return 0u;
            });
        }
        com.On(kDevice, kDrawPrimitiveUP, [](Regs& r) {
            Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), r.args[2] * 4 * r.args[4] / 2)});
            return 0u;
        });
        com.On(kDevice, kSetPixelShaderConstantF, [](Regs& r) {
            Record({HashBytes(reinterpret_cast<const uint8_t*>(r.args[2]), 16 * r.args[3])});
            return 0u;
        });
        UseStandardRelocation(com);
        s.At({0x00490DE0, AddressOf(&Nx::Engine::EnableFog)}, 0, [](Regs& r) { Record({0x0DE0, r.args[0] & 0xFF}); return 0u; }); // SetFog
        s.At({0x004B6070}, 0, [this](Regs& r) { Record({0x6070, Relocate(r.args[0]), Relocate(r.args[1])}); return 0u; });
    }
    ~Screens()
    {
        g_relocate = nullptr;
        delete &s;
    }
};

GameMemory* EffectsMemory()
{
    return new GameMemory({{0x00786D0C, 4}, {0x00786D1C, 0x24}, {0x00786D64, 8}, {0x00786E30, 0x110}, {0x00787328, 4},
                           {0x00787338, 0x14}, {0x007CA940, 0x28}, {0x007CC244, 1}, {0x00705720, 4},
                           {0x00786AB8, 1}, {0x007D6436, 1}, {0x007D643A, 1}, {0x00786CC0, 4},
                           {0x0067E97C, 8}, {0x0067E98C, 4}, {0x0067EF68, 1}, {0x0067EF88, 0x20},
                           {0x00786D6C, 8}, {0x007D69E0, 4}});
}

// The quad on its own (textured with odd sizes and offsets, flat, with and without the shader, fog on / off), then
// BeginScreenEffects / CopyScreenQuarter / Bloom in every state: blur amounts 2 / 3, bloom on / off, pixel shaders
// or not, the clear colour both ways, the composite on / off, weights over 255 / 128.
void TestScreenEffects()
{
    std::unique_ptr<GameMemory> memory(EffectsMemory());
    Screens screens;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0x5CFF0000 + seed);
            Lcg random(0x5CFE0000 + seed);
            for (int i = 0; i < 10; ++i) {
                screens.widths[i] = 1 + random.Below(1300);
                screens.heights[i] = 1 + random.Below(1000);
            }
            screens.widths[1] = 0x80000010; // (over 2^31: converted unsigned)
            GameVariable<FakeCom*>(0x007D69E0) = screens.device;
            GameVariable<FakeCom*>(0x00786D0C) = screens.device;
            for (int i = 0; i < 9; ++i)
                (&GameVariable<FakeCom*>(0x00786D1C))[i] = screens.textures[i];
            GameVariable<FakeCom*>(0x00786D64) = screens.surfaces[9];
            GameVariable<int>(0x00786D68) = static_cast<int>(random.Below(2));
            GameVariable<uint32_t>(0x00787338) = seed & 1;
            GameVariable<uint32_t>(0x00787328) = 1 + seed % 3;
            GameVariable<uint32_t>(0x00705720) = seed % 4 != 1;
            GameVariable<bool>(0x00786AB8) = seed != 5;
            GameVariable<bool>(0x007D6436) = (seed & 2) != 0;
            GameVariable<bool>(0x007D643A) = (seed & 4) != 0;
            GameVariable<bool>(0x0067EF68) = seed != 3;
            GameVariable<int>(0x00786D6C) = 640 + 16 * static_cast<int>(seed); // (screen sizes the half texel shows on)
            GameVariable<int>(0x00786D70) = 480 - 8 * static_cast<int>(seed);
            for (int tap = 0; tap < 4; ++tap) {
                GameVariable<float>(0x0067EF88 + 4 * tap) = static_cast<float>(random.Below(400)) / 99.0f;
                GameVariable<float>(0x0067EF98 + 4 * tap) = static_cast<float>(static_cast<int>(random.Below(9)) - 4) * 0.37f;
            }
            Originals guard(original, {0x004DE5E0, 0x004DEA80, 0x004DE980, 0x004DEB70, 0x004DA760, 0x004DAB60,
                                       0x004DA730, 0x004DAA80, 0x004DAA10, 0x004D9DE0, 0x004D9DB0});
            using Quad = void(__cdecl*)(uint32_t, void*, void*, float, float, uint32_t, bool);
            for (int q = 0; q < 6; ++q) {
                float du = static_cast<float>(static_cast<int>(random.Below(2001)) - 1000) / 333.0f;
                float dv = static_cast<float>(static_cast<int>(random.Below(2001)) - 1000) / 333.0f;
                Fn<Quad>(original, 0x004DE5E0)(random.Below(18), screens.textures[random.Below(9)],
                                               q == 4 ? nullptr : screens.textures[random.Below(9)], du, dv,
                                               random.Next(), random.Below(2) != 0);
            }
            Fn<void(__cdecl*)()>(original, 0x004DEA80)();
            Fn<void(__cdecl*)()>(original, 0x004DE980)();
            Fn<void(__cdecl*)()>(original, 0x004DEB70)();
            std::vector<uint32_t> trace = g_trace;
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the screen effects (quad, begin, quarter copy, bloom)");
    }
}

// The draw list: items constructed shown and hidden with random (also equal and not-a-number) priorities,
// moved by SetPriority / SetZ, hidden and shown, removed (also items not in it), destroyed; sprites
// constructed, begun / ended, destroyed; Draw2D over the list with stand-in draw methods.
struct Item {
    void** vtable;
    uint8_t rest[0x40];
};
void TestDrawList()
{
    std::unique_ptr<GameMemory> memory(EffectsMemory());
    Screens screens;
    GameMemory list({{0x007CC248, 8}, {0x007D1800, 4}});
    static Item items[12];
    alignas(16) static uint8_t spriteTexture[0x20];
    *reinterpret_cast<FakeCom**>(spriteTexture + 0x10) = screens.textures[5];
    static void* vtable[4];
    auto base = reinterpret_cast<uint32_t>(items);
    auto index = [base](uint32_t p) { return p >= base && p < base + sizeof(items) ? (p - base) / sizeof(Item) : 0xFFu; };
    for (int slot = 1; slot < 4; ++slot) {
        vtable[slot] = screens.s.Thunk(0, [slot, index](Regs& r) {
            Record({0xD2D0u + slot, static_cast<uint32_t>(index(r.ecx))});
            return 0u;
        });
    }
    screens.s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [index](Regs& r) {
        Record({0xABA0, static_cast<uint32_t>(index(r.args[0]))});
        return 0u;
    });
    static const float kPriorities[] = {0.0f, 1.0f, 1.0f, -2.5f, 3.0f, 100.0f, -0.0f,
                                        std::numeric_limits<float>::quiet_NaN()};
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory->Fill(0xD2D00000 + seed);
            GameVariable<FakeCom*>(0x007D69E0) = screens.device;
            GameVariable<uint32_t>(0x007CC248) = 0;
            GameVariable<uint32_t>(0x007CC24C) = seed & 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A3A0000;
            std::memset(items, 0x5A, sizeof(items));
            Lcg random(0xD2D10000 + seed);
            std::vector<uint32_t> trace;
            Originals guard(original, {0x004DF900, 0x004DF950, 0x004DFA70, 0x004DFAA0, 0x004DFC90, 0x004DFAE0,
                                       0x004DFB20, 0x004DFB70, 0x004DFB90, 0x004DFC50, 0x004DFC80, 0x004DFCC0,
                                       0x004DF990, 0x004DFA40, 0x004DA760, 0x004DAB60, 0x004DA730, 0x004DAA80,
                                       0x004DAA10, 0x004D9DE0, 0x004D9DB0});
            for (int i = 0; i < 12; ++i) {
                if (i % 4 == 3) {
                    Fn<void*(__thiscall*)(void*, float)>(original, 0x004DFC50)(&items[i], random.Pick(kPriorities));
                    *reinterpret_cast<uint8_t**>(items[i].rest + 0x10) = random.Below(2) ? spriteTexture : nullptr;
                } else {
                    Fn<void*(__thiscall*)(void*, float, bool)>(original, 0x004DFA70)(&items[i], random.Pick(kPriorities),
                                                                                    random.Below(4) == 0);
                }
                items[i].vtable = vtable;
            }
            for (int step = 0; step < 300; ++step) {
                Item* item = &items[random.Below(12)];
                switch (random.Below(7)) {
                case 0: Fn<void(__thiscall*)(void*, float)>(original, 0x004DFAE0)(item, random.Pick(kPriorities)); break;
                case 1: Fn<void(__thiscall*)(void*, float)>(original, 0x004DFB20)(item, random.Pick(kPriorities)); break;
                case 2: Fn<void(__thiscall*)(void*, bool)>(original, 0x004DFB70)(item, random.Below(2) != 0); break;
                case 3: Fn<void(__thiscall*)(void*)>(original, 0x004DF950)(item); break;
                case 4:
                    if (!reinterpret_cast<uint8_t*>(item)[8]) // (shown items are in the list: inserting twice loops)
                        break;
                    Fn<void(__thiscall*)(void*)>(original, 0x004DF900)(item);
                    reinterpret_cast<uint8_t*>(item)[8] = 0;
                    break;
                case 5: Fn<void(__cdecl*)()>(original, 0x004DFB90)(); break;
                default:
                    if ((item - items) % 4 == 3) {
                        Fn<void(__thiscall*)(void*)>(original, 0x004DF990)(item);
                        Fn<void(__thiscall*)(void*)>(original, 0x004DFA40)(item);
                    }
                    break;
                }
                for (Item& each : items)
                    each.vtable = vtable;
            }
            // Draw2D with nothing above z 0 (some exactly 0, -0, not a number), with the z test from the last frame
            // on and off, and with a listed item marked hidden.
            for (int pass = 0; pass < 4; ++pass) {
                static const float kZs[] = {0.0f, -0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()};
                for (Item& each : items)
                    *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(&each) + 0x10) = kZs[(&each - items + pass) % 4];
                if (pass == 2 && GameVariable<uint32_t>(0x007CC248))
                    reinterpret_cast<uint8_t*>(GameVariable<uint32_t>(0x007CC248))[8] = 1;
                GameVariable<uint32_t>(0x007CC24C) = pass & 1;
                GameVariable<uint8_t>(0x00786E52) = static_cast<uint8_t>((pass >> 1) ^ 1);
                Fn<void(__cdecl*)()>(original, 0x004DFB90)();
                trace.push_back(GameVariable<uint32_t>(0x007CC24C));
                trace.push_back(GameVariable<uint8_t>(0x00786E52));
            }
            for (int i = 0; i < 12; ++i) {
                uint32_t address = i % 4 == 3 ? (i % 8 == 3 ? 0x004DFCC0 : 0x004DFC80) : (i % 2 ? 0x004DFC90 : 0x004DFAA0);
                if (address == 0x004DFCC0 || address == 0x004DFC90)
                    trace.push_back(static_cast<uint32_t>(index(reinterpret_cast<uint32_t>(
                        Fn<void*(__thiscall*)(void*, unsigned int)>(original, address)(&items[i], i & 1)))));
                else
                    Fn<void(__thiscall*)(void*)>(original, address)(&items[i]);
            }
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            for (Item& each : items) {
                const auto* words = reinterpret_cast<const uint32_t*>(&each);
                for (int w = 0; w < 6; ++w) {
                    uint32_t word = words[w];
                    trace.push_back(index(word) != 0xFF ? 0xA0000000 + index(word) : word);
                }
            }
            trace.push_back(static_cast<uint32_t>(index(GameVariable<uint32_t>(0x007CC248))));
            trace.push_back(GameVariable<uint32_t>(0x007CC24C));
            memory->Dump(trace);
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "appmain: the 2D draw list and sprites");
    }
}

} // namespace

SELFTEST(Early, TestScreenEffects, RemoveStandIns);
SELFTEST(Early, TestDrawList, RemoveStandIns);
