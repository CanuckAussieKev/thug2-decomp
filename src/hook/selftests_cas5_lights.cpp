// Self-tests for the engine's imposter manager and model lights (src/game/gfx/engine_imposters.cpp,
// engine_model_lights.cpp): original and ours on objects in an arena with recording vtables, the hash table's
// iteration real, its other calls and the memory manager stood in; the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/engine_imposters.h"
#include "game/gfx/engine_model_lights.h"
#include "game/gfx/primitive_states.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;

void* g_groupVtable[8];
void* g_lightsVtable[13];
std::vector<float> g_distances;

float LightFloat(Lcg& random)
{
    switch (random.Below(9)) {
    case 0:
        return std::numeric_limits<float>::quiet_NaN();
    case 1:
        return 0.5f;
    case 2:
        return 0.0f;
    case 3:
        return 1.0f;
    default:
        return static_cast<float>(static_cast<int>(random.Below(3000)) - 1000) / 1000.0f;
    }
}

void StandInLightCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x0049FD30, AddressOf(&Lst::HashTable::FlushAllItems)}, 0, [](Regs& r) {
        Record({0xFD30, Relocate(r.ecx)});
        return 0u;
    });
    s.At({0x004B60B0, AddressOf(&Nx::SetPrimitiveStates)}, 0, [](Regs&) {
        Record({0x60B0});
        return 0u;
    });
    s.At({0x004B5FF0, AddressOf(&Nx::ResetPrimitiveStates)}, 0, [](Regs&) {
        Record({0x5FF0});
        return 0u;
    });
    for (int slot = 0; slot < 8; ++slot) {
        g_groupVtable[slot] = s.Thunk(slot == 0 ? 4 : 0, [slot](Regs& r) {
            Record({0xA00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            if (slot == 0)
                Record({r.args[0]});
            return slot == 3 ? (r.ecx >> 4) & 1 : 0x77u;
        });
    }
    // slot 5 returns a float: a thunk of its own (x87).
    g_groupVtable[5] = s.Thunk(0, [](Regs& r) {
        Record({0xA05, Relocate(r.ecx)});
        return 0u;
    });
    static const uint16_t pops[13] = {4, 4, 8, 8, 4, 4, 8, 8, 8, 4, 8, 0, 4};
    for (int slot = 0; slot < 13; ++slot) {
        g_lightsVtable[slot] = s.Thunk(pops[slot], [slot](Regs& r) {
            Record({0xB00u + static_cast<uint32_t>(slot), Relocate(r.ecx)});
            for (int i = 0; i < pops[slot] / 4; ++i)
                Record({r.args[i]});
            return 0x100u * static_cast<uint32_t>(slot) + 1;
        });
    }
}

// The distance (slot 5): returned in ST0 from the list the test sets.
uint32_t g_distanceIndex;
float __fastcall GroupDistance(void* /*group*/)
{
    float d = g_distanceIndex < g_distances.size() ? g_distances[g_distanceIndex] : 0.0f;
    ++g_distanceIndex;
    Record({0xA05, std::bit_cast<uint32_t>(d)});
    return d;
}

// The imposter manager over a hand-built table of groups; the model lights with the light manager's ranges.
void TestEngineImposters()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInLightCalls(s);
    g_groupVtable[5] = reinterpret_cast<void*>(&GroupDistance); // (thiscall with no arguments: ECX is the group)
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("imposters", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x4400 + round);
            std::vector<uint32_t> trace;
            auto* manager = reinterpret_cast<Nx::CImposterManager*>(Bump(8));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048E3E0)(manager)));
            trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(manager->mp_groups)));
            trace.push_back(static_cast<uint32_t>(manager->m_maxPerFrame));
            // The table: 4 buckets, 0-7 groups (some chained).
            auto* table = reinterpret_cast<Lst::HashTable*>(Bump(sizeof(Lst::HashTable)));
            std::memset(table, 0, sizeof(Lst::HashTable));
            table->m_bits = 2;
            table->m_buckets = reinterpret_cast<Lst::HashTable::Item*>(Bump(4 * sizeof(Lst::HashTable::Item)));
            std::memset(table->m_buckets, 0, 4 * sizeof(Lst::HashTable::Item));
            for (int i = static_cast<int>(random.Below(8)); i > 0; --i) {
                auto* group = reinterpret_cast<uint8_t*>(Bump(0x60));
                *reinterpret_cast<void***>(group) = g_groupVtable;
                *reinterpret_cast<float*>(group + 4) = LightFloat(random);
                *reinterpret_cast<int*>(group + 0x58) = static_cast<int>(random.Below(5));
                group[0x5C] = static_cast<uint8_t>(random.Below(3));
                Lst::HashTable::Item* bucket = &table->m_buckets[random.Below(4)];
                if (bucket->value) {
                    auto* item = reinterpret_cast<Lst::HashTable::Item*>(Bump(sizeof(Lst::HashTable::Item)));
                    *item = {random.Next(), group, bucket->next};
                    bucket->next = item;
                } else {
                    *bucket = {random.Next(), group, nullptr};
                }
            }
            manager->mp_groups = table;
            manager->m_maxPerFrame = static_cast<int>(random.Below(4));
            for (int step = 0; step < 4; ++step) {
                g_trace.clear();
                g_distances.clear();
                for (int i = 0; i < 8; ++i)
                    g_distances.push_back(LightFloat(random));
                g_distanceIndex = 0;
                switch (random.Below(5)) {
                case 0:
                    Fn<void(__thiscall*)(void*)>(original, 0x0048E370)(manager);
                    break;
                case 4:
                    if (step == 3)
                        Fn<void(__thiscall*)(void*)>(original, 0x0048E260)(manager);
                    break;
                default:
                    Fn<void(__thiscall*)(void*)>(original, 0x0048E2D0)(manager);
                    break;
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "imposters: made, updated (near / far / NaN, the per-frame limit), drawn, all deleted");
}

void TestEngineModelLights()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006FAAE0, 4}, {0x0067DCA4, 16},
                                          {0x006FAACC, 0x14}});
    StandIns& s = *new StandIns();
    StandInLightCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("model lights", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x4500 + round);
            std::vector<uint32_t> trace;
            uint32_t count = random.Below(5);
            GameVariable<uint32_t>(0x006FAAE0) = count;
            GameVariable<float>(0x0067DCA4) = LightFloat(random) * 2.0f;
            GameVariable<float>(0x0067DCA8) = LightFloat(random) * 2.0f;
            auto* dark = reinterpret_cast<float*>(Bump(32));
            auto* bright = reinterpret_cast<float*>(Bump(32));
            for (int i = 0; i < 8; ++i) {
                dark[i] = LightFloat(random) * 3.0f;
                bright[i] = LightFloat(random) * 3.0f;
            }
            GameVariable<float*>(0x0067DCAC) = dark;
            GameVariable<float*>(0x0067DCB0) = bright;
            for (int i = 0; i < 4; ++i)
                GameVariable<uint32_t>(0x006FAACC + 4 * i) = random.Next();
            if (round & 1)
                GameVariable<uint32_t>(0x006FAADC) = 1 | (random.Next() << 1);
            auto* lights = reinterpret_cast<Nx::CModelLights*>(Bump(0x20));
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048E5B0)(lights)));
            trace.push_back(reinterpret_cast<uint32_t>(lights->m_vtable));
            trace.push_back(std::bit_cast<uint32_t>(lights->m_scale));
            for (uint32_t i = 0; i < count; ++i)
                trace.push_back(std::bit_cast<uint32_t>(lights->mp_intensities[i]));
            lights->m_vtable = g_lightsVtable;
            auto* out = reinterpret_cast<uint32_t*>(Bump(16));
            for (int step = 0; step < 14; ++step) {
                g_trace.clear();
                switch (random.Below(8)) {
                case 0:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0048E450)(lights, 0x7C000000) & 0xFF);
                    break;
                case 1:
                    trace.push_back(
                        Fn<uint32_t(__thiscall*)(void*, int, uint32_t)>(original, 0x0048E480)(lights, 2, 0x7C000000) & 0xFF);
                    break;
                case 2:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0048E4D0)(lights, random.Next()) & 0xFF);
                    break;
                case 3:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, float, float)>(original, 0x0048E830)(
                                        lights, LightFloat(random), LightFloat(random) * 0.3f) &
                                    0xFF);
                    break;
                case 4: { // near the target: overshooting it, or equal (a negative step)
                    float b = lights->m_brightness;
                    float target = random.Below(4) ? b + static_cast<float>(static_cast<int>(random.Below(400)) - 200) / 1000.0f : b;
                    float stepSize = random.Below(5) ? static_cast<float>(random.Below(300)) / 1000.0f : -0.1f;
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, float, float)>(original, 0x0048E830)(lights, target, stepSize) & 0xFF);
                    trace.push_back(std::bit_cast<uint32_t>(lights->m_brightness));
                    break;
                }
                case 5:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, float)>(original, 0x0048E8A0)(lights, LightFloat(random)) & 0xFF);
                    break;
                case 6:
                    lights->m_brightness = LightFloat(random);
                    Fn<void(__thiscall*)(void*)>(original, 0x0048E660)(lights);
                    break;
                default: { // the stubs
                    void* p = lights;
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x0048E4E0)(p, 1) & 0xFF);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, int, int)>(original, 0x0048E500)(p, 1, 2) & 0xFF);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, int, int)>(original, 0x0048E520)(p, 1, 2) & 0xFF);
                    out[0] = 0xFFFFFFFF;
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x0048E730)(p, out)));
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x0048E760)(p, 1));
                    out[1] = 0xFFFFFFFF;
                    trace.push_back(
                        ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, void*, int)>(original, 0x0048E7C0)(p, out + 1, 2)));
                    Fn<void(__thiscall*)(void*, int, int)>(original, 0x0048E5A0)(p, 1, 2);
                    Fn<void(__thiscall*)(void*, int, int)>(original, 0x0048E540)(p, 1, 2);
                    Fn<void(__thiscall*)(void*, int)>(original, 0x0048E550)(p, 1);
                    Fn<void(__thiscall*)(void*, int, int)>(original, 0x0048E560)(p, 1, 2);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0048E570)(p) & 0xFF);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, int)>(original, 0x0048E580)(p, 1) & 0xFF);
                    break;
                }
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            if (random.Below(2))
                Fn<void(__thiscall*)(void*)>(original, 0x0048E630)(lights);
            else
                trace.push_back(ArenaRelocate(
                    Fn<uint32_t(__thiscall*)(void*, unsigned int)>(original, 0x0048E7F0)(lights, random.Below(4))));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "model lights: made, brightness set / moved / scaled (ranges, NaN, ties), colours, the stubs, destroyed");
}

} // namespace

SELFTEST(Early, TestEngineImposters, RemoveStandIns);
SELFTEST(Early, TestEngineModelLights, RemoveStandIns);
