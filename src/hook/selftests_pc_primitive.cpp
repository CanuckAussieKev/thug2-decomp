// Self-tests for the PC primitive drawer (src/game/gfx/pc_primitive.cpp, RFX-13): the original and ours over
// stand-in drawers in every vertex layout and type, a recording stand-in device and the dynamic vertex buffer, the
// engine's constructor / destructor, the material's code and rand() stood in.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/pc_primitive.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "game/sys/platform_d3d.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPcPrimitive;

constexpr uint32_t kDevice = 0xD3D00000;

float RandomFloat(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(100000)) / 99999.0f;
}

uint32_t g_rand;

template <typename F>
F Slot(bool original, uint32_t address)
{
    return Fn<F>(original, address);
}

void TestPcPrimitives()
{
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007D69E0, 4}, {0x007D69E8, 4}, {0x007CA964, 4},
                       {0x00786EBC, 8}, {0x00705620, 0x2400}});
    StandIns& s = *new StandIns();
    FakeComSet com(&s);
    FakeCom* device = com.Device(kDevice);
    StandInMemory(s);
    s.At({0x00623BA3}, 0, [](Regs&) {
        g_rand = g_rand * 1103515245u + 12345u;
        return (g_rand >> 16) & 0x7FFF;
    });
    s.At({0x0049C660}, 4, [](Regs& r) {
        Record({0xC660, Relocate(r.ecx), r.args[0]});
        *reinterpret_cast<uint32_t*>(r.ecx + 0x10) = r.args[0];
        return r.ecx;
    });
    StandInThis(s, {0x0049C6A0}, 0);
    s.At({0x004D1A90}, 0, [](Regs& r) {
        Record({0x1A90, Relocate(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x5C, 0x110);
        return r.ecx;
    });
    StandInThis(s, {0x004D2BD0}, 0);
    StandInThis(s, {0x004D1F00}, 0);
    s.At({0x004D9CF0, AddressOf(&Nx::BlendModeFromName)}, 0, [](Regs& r) {
        Record({0x9CF0, r.args[0]});
        return r.args[0] * 2654435761u;
    });
    s.At({0x004DAA80, AddressOf(&Nx::SetPixelShader)}, 0, [](Regs& r) {
        Record({0xAA80, r.args[0], Relocate(r.args[1])});
        return 0u;
    });
    s.At({0x004D9DB0, AddressOf(&Nx::SetVertexFormat)}, 0, [](Regs& r) {
        Record({0x9DB0, r.args[0]});
        return 0u;
    });
    s.At({0x005F4740, AddressOf(&Platform::LockDynamicVertices)}, 0, [](Regs& r) {
        Record({0x4740, r.args[0], r.args[1]});
        *reinterpret_cast<uint32_t*>(r.args[2]) = 0xB0FF0000;
        *reinterpret_cast<uint32_t*>(r.args[3]) = 77;
        if (r.args[4])
            *reinterpret_cast<uint32_t*>(r.args[4]) = 5000;
        return Bump(r.args[0] * r.args[1] + 16);
    });
    s.At({0x005F4800, AddressOf(&Platform::UnlockDynamicVertices)}, 0, [](Regs& r) {
        Record({0x4800, r.args[0], r.args[1]});
        return 0u;
    });
    UseStandardRelocation(com);
    auto standard = g_relocate;
    g_relocate = [standard](uint32_t word) { return ArenaRelocate(standard(word)); };
    bool ok = true, reported = false;
    static const uint32_t kLayouts[] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int round = 0; round < 32; ++round) {
        ok &= CompareTwoRuns("primitives", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Fill(0x9100 + round);
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<FakeCom*>(0x007D69E0) = device;
            GameVariable<uint32_t>(0x007D69E8) = 0x1D8E0000;
            g_rand = 0x55 + round;
            Lcg random(0x91000 + round);
            auto* p = reinterpret_cast<CPcPrimitive*>(Bump(sizeof(CPcPrimitive)));
            int own = round & 1;
            Slot<void*(__thiscall*)(void*, int)>(original, 0x004C5230)(p, own);
            std::vector<uint32_t> trace(reinterpret_cast<uint32_t*>(p), reinterpret_cast<uint32_t*>(p) + 0x56);
            p->blendMode = random.Next();
            uint32_t layout = kLayouts[round % 8];
            int type = static_cast<int>(random.Below(6));
            int count = 1 + static_cast<int>(random.Below(30));
            if (round == 17)
                g_failAllocations = {true};
            Slot<void(__thiscall*)(void*, uint32_t, int, int)>(original, 0x004C5F00)(p, layout, type, count);
            if (!p->vertices) {
                p->vertices = reinterpret_cast<uint8_t*>(Bump(0x24 * count + 16));
                p->stride = 0x24;
            }
            // a texture (key through a pointer sometimes, 0 sometimes)
            uint8_t* texture = reinterpret_cast<uint8_t*>(Bump(0x20));
            std::memset(texture, 0, 0x20);
            uint32_t key = round % 3 == 0 ? 0 : random.Next();
            if (round & 4) {
                auto* k = reinterpret_cast<uint32_t*>(Bump(4));
                *k = key;
                *reinterpret_cast<uint32_t**>(texture + 0x10) = k;
                texture[8] = 1;
            } else {
                *reinterpret_cast<uint32_t*>(texture + 0x10) = key;
            }
            if (round & 2)
                Slot<void(__thiscall*)(void*, const uint8_t*, int, int)>(original, 0x004C5A90)(p, texture, 3, 4);
            Slot<void(__thiscall*)(void*, uint32_t)>(original, 0x004C5310)(p, random.Next());
            Slot<void(__thiscall*)(void*)>(original, 0x004C5320)(p);
            for (int i = 0; i < count; ++i) {
                Slot<void(__thiscall*)(void*, int, float, float, float)>(original, 0x004C5B20)(
                    p, i, RandomFloat(random, -9, 9), RandomFloat(random, -9, 9), RandomFloat(random, -9, 9));
                Slot<void(__thiscall*)(void*, int, uint32_t)>(original, 0x004C56E0)(p, i, random.Next());
                Slot<void(__thiscall*)(void*, int, float, float)>(original, 0x004C5760)(p, i, RandomFloat(random, 0, 1),
                                                                                       RandomFloat(random, 0, 1));
                if (random.Below(3) == 0) {
                    Slot<void(__thiscall*)(void*, int, float)>(original, 0x004C5B60)(p, i, RandomFloat(random, -9, 9));
                    Slot<void(__thiscall*)(void*, int, float)>(original, 0x004C5B80)(p, i, RandomFloat(random, -9, 9));
                    Slot<void(__thiscall*)(void*, int, float)>(original, 0x004C5BA0)(p, i, RandomFloat(random, -9, 9));
                    Slot<void(__thiscall*)(void*, int, float)>(original, 0x004C57C0)(p, i, RandomFloat(random, 0, 1));
                    Slot<void(__thiscall*)(void*, int, float)>(original, 0x004C5800)(p, i, RandomFloat(random, 0, 1));
                }
                if (p->vertexFormat == 0x152) {
                    Slot<void(__thiscall*)(void*, int, float)>(original, 0x004C5BF0)(p, i, RandomFloat(random, -1, 1));
                    Slot<void(__thiscall*)(void*, int, float)>(original, 0x004C5C10)(p, i, RandomFloat(random, -1, 1));
                    Slot<void(__thiscall*)(void*, int, float)>(original, 0x004C5C30)(p, i, RandomFloat(random, -1, 1));
                    Slot<void(__thiscall*)(void*, int, float, float, float)>(original, 0x004C5BC0)(
                        p, static_cast<int>(random.Below(count)), 1.5f, 2.5f, 3.5f);
                }
            }
            Slot<void(__thiscall*)(void*, float, float, float)>(original, 0x004C5D10)(p, 0.25f, -1.5f, 3.0f);
            Slot<void(__thiscall*)(void*, float)>(original, 0x004C5D80)(p, 0.125f);
            Slot<void(__thiscall*)(void*, float)>(original, 0x004C5DC0)(p, -0.5f);
            Slot<void(__thiscall*)(void*, float)>(original, 0x004C5E00)(p, 7.0f);
            Slot<void(__thiscall*)(void*, float, float)>(original, 0x004C5910)(p, 0.1f, 0.2f);
            Slot<void(__thiscall*)(void*, float)>(original, 0x004C5980)(p, 0.3f);
            Slot<void(__thiscall*)(void*, float)>(original, 0x004C59D0)(p, 0.4f);
            for (int i = 0; i < count; ++i) {
                for (uint32_t getter : {0x004C5C50u, 0x004C5C70u, 0x004C5C90u, 0x004C5890u, 0x004C58D0u})
                    trace.push_back(std::bit_cast<uint32_t>(Slot<float(__thiscall*)(void*, int)>(original, getter)(p, i)));
                trace.push_back(Slot<uint32_t(__thiscall*)(void*, int)>(original, 0x004C5840)(p, i));
                if (p->vertexFormat == 0x152)
                    for (uint32_t getter : {0x004C5CB0u, 0x004C5CD0u, 0x004C5CF0u})
                        trace.push_back(
                            std::bit_cast<uint32_t>(Slot<float(__thiscall*)(void*, int)>(original, getter)(p, i)));
            }
            Slot<void(__thiscall*)(void*)>(original, 0x004C5370)(p);
            // drawn from arrays (through the vtable: the original's or ours, as the drawer's own vtable says)
            if (round % 4 == 3) {
                std::vector<float> positions(3 * count), uvs(2 * count), normals(3 * count);
                std::vector<uint32_t> colours(count);
                for (float& f : positions)
                    f = RandomFloat(random, -5, 5);
                for (float& f : uvs)
                    f = RandomFloat(random, 0, 1);
                for (float& f : normals)
                    f = RandomFloat(random, -1, 1);
                for (uint32_t& c : colours)
                    c = random.Next();
                Slot<void(__thiscall*)(void*, const float*, const uint32_t*, const float*, const float*)>(original,
                                                                                                            0x004C55D0)(
                    p, positions.data(), round & 8 ? colours.data() : nullptr, uvs.data(),
                    p->vertexFormat == 0x152 ? normals.data() : nullptr);
                trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(0x00705620), count * p->stride));
                p->vertices = reinterpret_cast<uint8_t*>(Bump(4)); // (not to free the game's buffer)
            }
            CPcPrimitive other{};
            other.type = static_cast<int>(random.Below(6));
            trace.push_back(Slot<int(__thiscall*)(void*, const void*)>(original, 0x004C5340)(p, &other));
            Fn<void(__cdecl*)(int, int)>(original, 0x004C5E40)(round % 3 == 2 ? 2 : 1, static_cast<int>(random.Below(6)));
            trace.push_back(ArenaRelocate(Slot<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x004C5ED0)(
                p, round & 16 ? 1 : 0)));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    g_relocate = nullptr;
    Check(ok, "pc primitives: made, set up, vertices set / read / moved, drawn, destroyed");
}

} // namespace

SELFTEST(Early, TestPcPrimitives, RemoveStandIns);
