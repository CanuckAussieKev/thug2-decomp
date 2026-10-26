// Self-tests for the objsys agent's batch OS-4: the sound emitter nodes and the follow trail
// (src/game/obj/emitter.cpp). The original and ours on a stand-in memory manager over a 64 KB-aligned arena, with
// the node parameters, sectors and objects stood in for; the hash table, box and vector code is the real code.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/emitter.h"
#include "game/obj/object.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

constexpr uint32_t kArenaSize = 0x20000;
constexpr uint32_t kWorld = 0x1000;      // sectors (0x100 apart: +0x20 box), objects (+0x4C position)
constexpr uint32_t kHeapOffset = 0x4000;

uint8_t* g_emArenas[2];
uint8_t* g_emArena;

// (both allocated at once and both mapped in either run - ArenaWord / ArenaWordOrHigh, selftest.h: a value that
// only happens to fall inside one of them maps the same way in each)
uint8_t* EmArena(bool original)
{
    for (uint8_t*& arena : g_emArenas)
        if (!arena)
            arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    return g_emArenas[original ? 0 : 1];
}
bool InEmArenas(uint32_t word) { return ArenaOf(g_emArenas[0], g_emArenas[1], kArenaSize, word) >= 0; }
uint32_t EmReloc(uint32_t word)
{
    if (InEmArenas(word))
        return ArenaWord(g_emArenas[0], g_emArenas[1], kArenaSize, word);
    return OnStack(word) ? 0x57AC0000u : word;
}

void SetUpManager(uint8_t* arena)
{
    using Mem::Heap;
    using Mem::Manager;
    using Mem::Region;
    std::memset(arena, 0, kHeapOffset);
    std::memset(arena + kHeapOffset, 0xCD, kArenaSize - kHeapOffset);
    GameVariable<bool>(0x007CC979) = true;
    auto* manager = reinterpret_cast<Manager*>(arena);
    GameVariable<void*>(0x007D1800) = manager;
    auto* region = new (arena + 0x600) Region(arena + kHeapOffset, arena + kArenaSize);
    auto* heap = new (arena + 0x620) Heap(region, 1, "test");
    manager->m_heaps[0] = heap;
    manager->m_heapCount = 1;
    manager->PushContext(heap);
}

std::vector<uint32_t> ArenaImage(uint8_t* arena)
{
    std::vector<uint32_t> image(kArenaSize / 4);
    std::memcpy(image.data(), arena, kArenaSize);
    for (uint32_t& word : image)
        word = InEmArenas(word) || !OnStack(word) ? ArenaWordOrHigh(g_emArenas[0], g_emArenas[1], kArenaSize, word) : 0x57AC0000u;
    return image;
}

// Node parameters by structure (stand-in address = 0x5A000000 + node index).
struct Params {
    std::map<uint32_t, uint32_t> values;
    Mth::Vector pos;
};
std::map<uint32_t, Params> g_params;
std::map<uint32_t, uint32_t> g_nodeOf; // node name -> structure
Lcg g_emAnswers(1);

void StandInEmitterWorld(StandIns& s)
{
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        uint32_t node = g_nodeOf.count(r.args[0]) ? g_nodeOf[r.args[0]] : 0x5AFFFFFF;
        Record({0x90DE0000, r.args[0], r.args[1] & 0xFF, node});
        return node;
    });
    auto get = [](Regs& r) {
        Params& p = g_params[r.ecx];
        auto it = p.values.find(r.args[0]);
        Record({0x6E7C0000, r.ecx, r.args[0], r.args[2], it != p.values.end()});
        if (it == p.values.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476950, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(
                          &Script::CStruct::GetChecksum))},
         12, get);
    s.At({0x004766D0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, float*, int)>(
                          &Script::CStruct::GetFloat))},
         12, get);
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))},
         0, [](Regs& r) {
             *reinterpret_cast<Mth::Vector*>(r.args[1]) = g_params[r.args[0]].pos;
             Record({0x9050000, r.args[0], EmReloc(r.args[1])});
             return 1u;
         });
    s.At({0x0048C510}, 0, [](Regs& r) {
        uint32_t sector = 0;
        if (g_emAnswers.Below(4) != 0)
            sector = reinterpret_cast<uint32_t>(g_emArena + kWorld + 0x100 * g_emAnswers.Below(4));
        Record({0x5EC70000, r.args[0], EmReloc(sector)});
        return sector;
    });
    s.At({0x0049E6A0}, 0, [](Regs& r) {
        Record({0x5EC70001, EmReloc(r.ecx)});
        return r.ecx + 0x20;
    });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        uint32_t object = r.args[0] < 4 ? reinterpret_cast<uint32_t>(g_emArena + kWorld + 0x400 + 0x100 * r.args[0])
                                        : 0u;
        Record({0xF1DD0000, r.args[0], EmReloc(object)});
        return object;
    });
}

Mth::Vector RandomPoint(Lcg& random, float spread)
{
    Mth::Vector p;
    p.x = (static_cast<float>(random.Below(2001)) - 1000.0f) * spread / 1000.0f;
    p.y = (static_cast<float>(random.Below(2001)) - 1000.0f) * spread / 1000.0f;
    p.z = (static_cast<float>(random.Below(2001)) - 1000.0f) * spread / 1000.0f;
    static const float kW[] = {1.0f, 1.0f, 0.5f, 0.3f, 1.7f, 0.123f};
    p.w = random.Pick(kW); // (w values whose products round)
    return p;
}

void BuildWorld(Lcg& random)
{
    for (uint32_t i = 0; i < 4; ++i) {
        auto* box = reinterpret_cast<float*>(g_emArena + kWorld + 0x100 * i + 0x20);
        for (int k = 0; k < 3; ++k) {
            box[k] = static_cast<float>(random.Below(200)) - 100.0f;
            box[4 + k] = box[k] + 10.0f + static_cast<float>(random.Below(150));
        }
        box[3] = 1.0f;
        box[7] = 1.0f;
    }
}

// The emitters: the table made / emptied, nodes added from parameters of every kind (each shape name and none,
// sectors or not, radius or not, renamed by name=), lookups, activation by name, the inside test and nearest point
// for each shape (points inside and out), the clean-up.
void TestEmitters()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestEmitters skipped (the memory manager is already set up)");
        return;
    }
    GameMemory memory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006F2260, 4}});
    StandIns& s = *new StandIns();
    StandInEmitterWorld(s);
    static const uint32_t kShapes[] = {0xE460ABDE, 0x03F1E5A7, 0xAA069978, 0x12345678, 0};
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            memory.Zero();
            g_emArena = EmArena(original);
            SetUpManager(g_emArena);
            Lcg random(0xE3170000 + seed);
            g_emAnswers = Lcg(0xE3A00000 + seed);
            BuildWorld(random);
            g_params.clear();
            g_nodeOf.clear();
            Fn<void(__cdecl*)()>(original, 0x004683B0)();
            std::vector<uint32_t> names;
            uint32_t count = 2 + random.Below(8);
            for (uint32_t k = 0; k < count; ++k) {
                // (names in different buckets: no chained items)
                uint32_t name = 0x0E000000 + 0x100 * random.Below(16) + k;
                uint32_t structure = 0x5A000000 + k;
                Params& p = g_params[structure];
                if (random.Below(4) == 0) // (renamed: sometimes to another node's new name)
                    p.values[0xA1DC81F9] = 0x0F000000 + random.Below(3);
                if (uint32_t shape = random.Pick(kShapes))
                    p.values[0x7321A8D6] = shape;
                if (random.Below(4) != 0)
                    p.values[0xC48391A5] = Bits(static_cast<float>(5 + random.Below(60)) + 0.5f);
                if (random.Below(3) != 0)
                    p.values[0x2CA8A299] = 0x5C000000 + k;
                p.pos = RandomPoint(random, 100.0f);
                p.pos.w = 1.0f;
                g_nodeOf[name] = structure;
                names.push_back(name);
                Fn<void(__cdecl*)(uint32_t, uint32_t)>(original, 0x00468450)(name, random.Below(2));
            }
            for (int step = 0; step < 30; ++step) {
                uint32_t name = random.Below(5) == 0 ? 0x77777777u : names[random.Below(count)];
                if (random.Below(4) == 0)
                    name = 0x0F000000 + random.Below(3); // (the new names, some shared)
                switch (random.Below(4)) {
                case 0: {
                    static const int kActive[] = {0, 1, 2};
                    Fn<void(__cdecl*)(uint32_t, int)>(original, 0x00468340)(name, random.Pick(kActive));
                    break;
                }
                case 1:
                    Record({EmReloc(reinterpret_cast<uint32_t>(
                        Fn<void*(__cdecl*)(uint32_t)>(original, 0x004680E0)(name)))});
                    break;
                default: {
                    auto* node = static_cast<uint8_t*>(Fn<void*(__cdecl*)(uint32_t)>(original, 0x004680E0)(name));
                    if (!node)
                        break;
                    int shape = *reinterpret_cast<int*>(node + 0x28);
                    Mth::Vector p = RandomPoint(random, 120.0f);
                    if (random.Below(2)) {
                        auto* pos = reinterpret_cast<float*>(node + 4);
                        p.x = pos[0] + static_cast<float>(random.Below(81)) - 40.0f;
                        p.y = pos[1] + static_cast<float>(random.Below(81)) - 40.0f;
                        p.z = pos[2] + static_cast<float>(random.Below(81)) - 40.0f;
                        if (random.Below(3) == 0) // (the sphere's exact edge)
                            p = {pos[0] + *reinterpret_cast<float*>(node + 0x2C), pos[1], pos[2], 1.0f};
                    }
                    bool inside = Fn<bool(__thiscall*)(void*, const Mth::Vector*)>(original, 0x00468120)(node, &p);
                    Record({0x1251DE00u | static_cast<uint32_t>(inside)});
                    if (shape == 1 && !inside)
                        break; // (no nearest point: uninitialised locals)
                    Mth::Vector out{};
                    auto* result = Fn<Mth::Vector*(__thiscall*)(void*, Mth::Vector*, const Mth::Vector*)>(
                        original, 0x004681A0)(node, &out, &p);
                    Record({EmReloc(reinterpret_cast<uint32_t>(result)), Bits(out.x), Bits(out.y), Bits(out.z),
                            Bits(out.w)});
                    break;
                }
                }
            }
            // emptied (the table stays), the nodes freed; then Init on the existing table empties it again
            Fn<void(__cdecl*)()>(original, seed % 2 ? 0x004682B0 : 0x004683B0)();
            Record({EmReloc(GameVariable<uint32_t>(0x006F2260))});
            std::vector<uint32_t> trace = g_trace;
            for (uint32_t& word : trace)
                word = EmReloc(word);
            std::vector<uint32_t> image = ArenaImage(g_emArena);
            trace.insert(trace.end(), image.begin(), image.end());
            GameVariable<void*>(0x007D1800) = nullptr;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "emitters: add, look up, activate, inside, nearest point, clean-up");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    // no table: the clean-up does nothing
    GameVariable<uint32_t>(0x006F2260) = 0;
    Original<void(__cdecl*)()>(0x004682B0)();
    Obj::Emitter_Cleanup();
    RemoveStandIns();
}
SELFTEST(Early, TestEmitters, RemoveStandIns);

// The follow trail: made and reset (dirty memory), updated as the followed object moves (missing object, steps
// under and over 40 units, past 64 points: the ring wraps), and the point and facing back along it for distances
// within a segment, across several, past the whole trail, zero; both facing modes; zero-length segments.
void TestFollowTrail()
{
    StandIns& s = *new StandIns();
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        uint32_t object = 0;
        if (r.args[0] != 0x99 && g_emAnswers.Below(8) != 0)
            object = reinterpret_cast<uint32_t>(g_emArena + kWorld + 0x400);
        Record({0xF1DD0000, r.args[0], EmReloc(object)});
        return object;
    });
    // the vector maths the same in both runs (rewritten and tested before)
    for (uint32_t address : {0x004023F0u, 0x00402550u}) {
        void* ours = OursAt(address);
        Check(ours != nullptr, "follow trail: our maths found");
        if (ours)
            s.Bytes(address, Branch(0xE9, address, ours));
    }
    alignas(16) static uint8_t trail[0x430];
    alignas(16) static float matrix[16];
    for (uint32_t seed = 1; seed <= 30; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            g_emArena = EmArena(true);
            std::memset(g_emArena + kWorld, 0, 0x1000);
            Lcg random(0x7A110000 + seed);
            g_emAnswers = Lcg(0x7A1A0000 + seed);
            std::memset(trail, 0xCD, sizeof(trail));
            Record({EmReloc(reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*)>(original, 0x00468AC0)(trail)))});
            if (seed % 3 == 0) {
                std::memset(trail + 0x400, 0xCD, 0x30);
                Fn<void(__thiscall*)(void*)>(original, 0x004685D0)(trail);
            }
            for (uint32_t i = 0x400; i < 0x424; i += 4)
                Record({*reinterpret_cast<uint32_t*>(trail + i)});
            *reinterpret_cast<uint32_t*>(trail + 0x408) = seed % 7 == 0 ? 0x99 : 5;
            trail[0x420] = static_cast<uint8_t>(seed % 2);
            auto* object = reinterpret_cast<float*>(g_emArena + kWorld + 0x400 + 0x4C);
            float x = 0, y = 0, z = 0;
            uint32_t updates = 10 + random.Below(250); // (past 64 points: the ring wraps)
            for (uint32_t i = 0; i < updates; ++i) {
                if (random.Below(6) == 0) {
                    x += random.Below(2) ? 40.0f : 40.5f; // (exactly the spacing, and just over)
                } else {
                    float step = static_cast<float>(random.Below(3) == 0 ? random.Below(40) : 20 + random.Below(60));
                    x += step * (static_cast<float>(random.Below(3)) - 1.0f);
                    y += static_cast<float>(random.Below(5)) - 2.0f;
                    z += step * (static_cast<float>(random.Below(3)) - 1.0f);
                }
                object[0] = x;
                object[1] = y;
                object[2] = z;
                object[3] = random.Below(4) == 0 ? 0.5f : 1.0f;
                Fn<void(__thiscall*)(void*)>(original, 0x00468610)(trail);
                if (random.Below(4) == 0) {
                    static const float kDistances[] = {0.0f, 10.0f, 39.5f, 100.0f, 250.0f, 1000.0f, 50000.0f};
                    *reinterpret_cast<float*>(trail + 0x41C) = random.Pick(kDistances);
                    for (int k = 0; k < 16; ++k)
                        matrix[k] = (static_cast<float>(random.Below(201)) - 100.0f) / 100.0f;
                    float out[4] = {7, 7, 7, 7};
                    Fn<void(__thiscall*)(void*, float*, float*)>(original, 0x00468730)(trail, out, matrix);
                    Record({Bits(out[0]), Bits(out[1]), Bits(out[2]), Bits(out[3])});
                    for (float m : matrix)
                        Record({Bits(m)});
                }
            }
            for (uint32_t i = 0; i < sizeof(trail); i += 4)
                Record({*reinterpret_cast<uint32_t*>(trail + i)});
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "follow trail: updates, positions and facings back along it");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    RemoveStandIns();
}
SELFTEST(Early, TestFollowTrail, RemoveStandIns);

} // namespace
