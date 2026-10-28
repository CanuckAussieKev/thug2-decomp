// Self-tests for the engine's primitive (src/game/gfx/engine_primitive.cpp): the stubs, the constructor and
// destructors, readying and drawing, the lists and their sort, the state - original and ours on primitives in an arena
// with a recording vtable, the PC state call and Dbg::Printf stood in; the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/engine_primitive.h"
#include "game/gfx/pc_primitive.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Nx::CPrimitive;

void* g_primVtable[36];

int Order(const CPrimitive* a, const CPrimitive* b)
{
    return a->m_blendMode < b->m_blendMode ? -1 : a->m_blendMode == b->m_blendMode ? 0 : 1;
}

void StandInPrimitiveCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0x55u;
    });
    s.At({0x004C5E40, AddressOf(&Nx::SetPrimitiveDepthTest)}, 0, [](Regs& r) {
        Record({0x5E40, r.args[0], r.args[1], GameVariable<uint32_t>(0x00700EF8)});
        return 0u;
    });
    for (int slot = 0; slot < 36; ++slot) {
        uint16_t pop = slot == 6 ? 16 : slot == 35 || slot == 0 ? 4 : 0;
        g_primVtable[slot] = s.Thunk(pop, [slot](Regs& r) {
            Record({0xF00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            if (slot == 6)
                Record({r.args[0], r.args[1], r.args[2], r.args[3]});
            if (slot == 35) {
                Record({Relocate(r.args[0])});
                return static_cast<uint32_t>(Order(reinterpret_cast<CPrimitive*>(r.ecx),
                                                   reinterpret_cast<CPrimitive*>(r.args[0])));
            }
            return 0x77u;
        });
    }
}

uint32_t CallSlot35(void* code, void* object, void* other)
{
    uint32_t result;
    __asm {
        mov ecx, object
        push other
        call code
        mov result, eax
    }
    return result;
}

// The stubs: each called with its arguments, the message and the result (floats in ST0) recorded.
void TestPrimitiveStubs()
{
    StandIns& s = *new StandIns();
    StandInPrimitiveCalls(s);
    bool ok = true, reported = false;
    ok &= CompareTwoRuns("primitive stubs", reported, [&](bool original) {
        g_trace.clear();
        std::vector<uint32_t> trace;
        alignas(16) uint8_t object[0x40] = {};
        void* p = object;
        Fn<void(__thiscall*)(void*, const void*, int, int)>(original, 0x0049C390)(p, nullptr, 1, 2);
        Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0049C3A0)(p, 5);
        Fn<void(__thiscall*)(void*, uint32_t, int, int)>(original, 0x0049C3B0)(p, 1, 2, 3);
        Fn<void(__thiscall*)(void*)>(original, 0x0049C3C0)(p);
        Fn<void(__thiscall*)(void*)>(original, 0x0049C3D0)(p);
        Fn<void(__thiscall*)(void*, const void*, const void*, const void*, const void*)>(original, 0x0049C3E0)(
            p, nullptr, nullptr, nullptr, nullptr);
        for (uint32_t address : {0x0049C3F0u, 0x0049C430u})
            Fn<void(__thiscall*)(void*, int, float, float, float)>(original, address)(p, 1, 1.0f, 2.0f, 3.0f);
        for (uint32_t address : {0x0049C400u, 0x0049C410u, 0x0049C420u, 0x0049C440u, 0x0049C450u, 0x0049C460u,
                                 0x0049C490u, 0x0049C4A0u})
            Fn<void(__thiscall*)(void*, int, float)>(original, address)(p, 1, 1.0f);
        Fn<void(__thiscall*)(void*, int, uint32_t)>(original, 0x0049C470)(p, 1, 2);
        Fn<void(__thiscall*)(void*, int, float, float)>(original, 0x0049C480)(p, 1, 1.0f, 2.0f);
        for (uint32_t address : {0x0049C4B0u, 0x0049C4D0u, 0x0049C4F0u, 0x0049C510u, 0x0049C530u, 0x0049C550u,
                                 0x0049C590u, 0x0049C5B0u})
            trace.push_back(std::bit_cast<uint32_t>(Fn<float(__thiscall*)(void*, int)>(original, address)(p, 3)));
        trace.push_back(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x0049C570)(p, 3));
        Fn<void(__thiscall*)(void*, float, float, float)>(original, 0x0049C5D0)(p, 1.0f, 2.0f, 3.0f);
        for (uint32_t address : {0x0049C5E0u, 0x0049C5F0u, 0x0049C600u, 0x0049C620u, 0x0049C630u})
            Fn<void(__thiscall*)(void*, float)>(original, address)(p, 1.0f);
        Fn<void(__thiscall*)(void*, float, float)>(original, 0x0049C610)(p, 1.0f, 2.0f);
        trace.push_back(Fn<uint32_t(__thiscall*)(void*, const void*)>(original, 0x0049C640)(p, nullptr));
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    });
    delete &s;
    Check(ok, "primitive: the 35 platform stubs (their messages, their results)");
}

// Rounds of random steps on primitives: constructed (listed or not), readied, drawn, unlinked, destroyed, sorted
// into the drawing list and drawn; the state set; the slot-35 jump.
void TestPrimitiveLists()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x00700EF4, 16}});
    StandIns& s = *new StandIns();
    StandInPrimitiveCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 60; ++round) {
        ok &= CompareTwoRuns("primitive lists", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            GameVariable<uint32_t>(0x00700EF8) = 0x5A5A5A5Au;
            Lcg random(0x9100 + round);
            std::vector<uint32_t> trace;
            std::vector<CPrimitive*> made;
            for (int step = 0; step < 30; ++step) {
                g_trace.clear();
                switch (random.Below(9)) {
                case 0:
                case 1: { // made: listed (own 1) or not
                    auto* p = reinterpret_cast<CPrimitive*>(Bump(0x40));
                    int own = random.Below(4) ? 1 : static_cast<int>(random.Below(3)) - 1;
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x0049C660)(p, own)));
                    trace.push_back(reinterpret_cast<uint32_t>(p->m_vtable));
                    trace.push_back(p->m_blendMode);
                    p->m_vtable = g_primVtable;
                    p->m_blendMode = random.Below(5);
                    made.push_back(p);
                    break;
                }
                case 2:
                    if (!made.empty())
                        Fn<void(__thiscall*)(void*)>(original, 0x0049C6C0)(made[random.Below(made.size())]);
                    break;
                case 3:
                    if (!made.empty()) {
                        CPrimitive* p = made[random.Below(made.size())];
                        if (random.Below(2))
                            Fn<void(__thiscall*)(void*)>(original, 0x0049C6D0)(p);
                        else
                            Fn<void(__thiscall*)(void*, const void*, const void*, const void*, const void*)>(
                                original, 0x0049C6E0)(p, reinterpret_cast<void*>(1), reinterpret_cast<void*>(2),
                                                      reinterpret_cast<void*>(3), reinterpret_cast<void*>(4));
                    }
                    break;
                case 4: // destroyed (by its body or freed) - off whichever list it's on
                    if (!made.empty()) {
                        size_t i = random.Below(made.size());
                        CPrimitive* p = made[i];
                        made.erase(made.begin() + static_cast<long>(i));
                        p->m_vtable = g_primVtable;
                        if (random.Below(2))
                            Fn<void(__thiscall*)(void*)>(original, 0x0049C6A0)(p);
                        else
                            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, unsigned int)>(
                                original, 0x0049C790)(p, random.Below(2))));
                    }
                    break;
                case 5:
                    Fn<void(__cdecl*)()>(original, 0x0049C840)();
                    break;
                case 6: {
                    int state = static_cast<int>(random.Below(6));
                    Fn<void(__cdecl*)(int, int)>(original, 0x0049C700)(state, static_cast<int>(random.Next()));
                    break;
                }
                case 7:
                    if (made.size() >= 2) {
                        void* code = original ? OriginalFunction(0x0049C6F0) : reinterpret_cast<void*>(&Nx::CallVirtual35Stub);
                        trace.push_back(CallSlot35(code, made[0], made[1]));
                    }
                    break;
                default: // the new list's first linked again (a primitive moved to the front)
                    if (!made.empty()) {
                        CPrimitive* p = made[random.Below(made.size())];
                        if (p->m_ownsVertices == 1 && (p->m_flags & 0x100u) && GameVariable<CPrimitive*>(0x00700F00) == p) {
                            Fn<void(__cdecl*)(void*)>(original, 0x0049C7F0)(p);
                            Fn<void(__cdecl*)(void*)>(original, 0x0049C7D0)(p);
                        }
                    }
                    break;
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                memory.Dump(trace);
                trace.push_back(ArenaRelocate(GameVariable<uint32_t>(0x00700F00)));
                trace.push_back(ArenaRelocate(GameVariable<uint32_t>(0x00700EFC)));
            }
            DumpArena(trace);
            GameVariable<uint32_t>(0x00700F00) = 0;
            GameVariable<uint32_t>(0x00700EFC) = 0;
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "primitive: made, readied, drawn, destroyed, sorted into the drawing list, states, the slot-35 jump");
}

} // namespace

SELFTEST(Early, TestPrimitiveStubs, RemoveStandIns);
SELFTEST(Early, TestPrimitiveLists, RemoveStandIns);
