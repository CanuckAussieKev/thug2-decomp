// Self-tests for the objsys agent's batch OS-6: the path manager and its object groups
// (src/game/obj/path_manager.cpp). The original and ours on a stand-in memory manager over a 64 KB-aligned arena,
// with stand-in objects (real smart pointer lists) and a stand-in node graph; the whole arena compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/path_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/objsys_testkit.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

constexpr uint32_t kArenaSize = 0x20000;
constexpr uint32_t kObjects = 0x1000;     // 8 objects, 0x20 apart (vtable, smart pointer list)
constexpr uint32_t kHeapOffset = 0x2000;
constexpr uint32_t kPathNum = 0x9C91C3CA;

uint8_t* g_pmArenas[2];
uint8_t* g_pmArena;

// (both allocated at once and both mapped in either run - ArenaWord / ArenaWordOrHigh, selftest.h: a value that
// only happens to fall inside one of them maps the same way in each)
uint8_t* PmArena(bool original)
{
    for (uint8_t*& arena : g_pmArenas)
        if (!arena)
            arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    return g_pmArenas[original ? 0 : 1];
}
bool InPmArenas(uint32_t word) { return ArenaOf(g_pmArenas[0], g_pmArenas[1], kArenaSize, word) >= 0; }
uint32_t PmReloc(uint32_t word)
{
    if (InPmArenas(word))
        return ArenaWord(g_pmArenas[0], g_pmArenas[1], kArenaSize, word);
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
    manager->m_bottomUpHeap = heap;
    manager->PushContext(heap);
}

std::vector<uint32_t> ArenaImage(uint8_t* arena)
{
    std::vector<uint32_t> image(kArenaSize / 4);
    std::memcpy(image.data(), arena, kArenaSize);
    for (uint32_t& word : image)
        word = InPmArenas(word) || !OnStack(word) ? ArenaWordOrHigh(g_pmArenas[0], g_pmArenas[1], kArenaSize, word) : 0x57AC0000u;
    return image;
}

// The node graph (structure stand-ins 0x5E000000 + index; names 0x0E0E0000 + index).
struct GraphNode {
    uint32_t nodeClass = 0;
    uint32_t type = 0;
    bool hasType = false;
    std::vector<uint32_t> links;
    bool hasPath = false;
    int path = 0;
};
std::vector<GraphNode> g_nodes;
size_t g_walkAt;
std::map<uint32_t, int> g_params; // CFunc params: name -> integer

GraphNode& NodeOf(uint32_t structure) { return g_nodes[structure & 0xFFFF]; }

void StandInPathWorld(StandIns& s)
{
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Record({0x90DE0000, r.args[0]});
        return 0x5E000000u + (r.args[0] & 0xFFFF);
    });
    s.At({0x0045C1D0, AddressOf(&SkateScript::StartNodeWalk)}, 0, [](Regs& r) {
        g_walkAt = 0;
        Record({0x3A1C0000, r.args[0]});
        return 0u;
    });
    s.At({0x0045C1F0, AddressOf(&SkateScript::NextNode)}, 0, [](Regs&) {
        uint32_t node = g_walkAt < g_nodes.size() ? 0x5E000000u + static_cast<uint32_t>(g_walkAt++) : 0u;
        Record({0x3A1C0001, node});
        return node;
    });
    s.At({0x0045C200, AddressOf(&SkateScript::EndNodeWalk)}, 0, [](Regs&) {
        Record({0x3A1C0002});
        return 0u;
    });
    s.At({0x0045C020, AddressOf(static_cast<uint32_t (*)(Script::CStruct*)>(&SkateScript::GetLinkCount))}, 0,
         [](Regs& r) {
             auto count = static_cast<uint32_t>(NodeOf(r.args[0]).links.size());
             Record({0x11C40000, r.args[0], count});
             return count;
         });
    s.At({0x0045C420, AddressOf(static_cast<Script::CStruct* (*)(Script::CStruct*, int)>(
                          &SkateScript::GetLinkedNode))},
         0, [](Regs& r) {
             uint32_t node = 0x5E000000u + NodeOf(r.args[0]).links[r.args[1]];
             Record({0x11C40002, r.args[0], r.args[1], node});
             return node;
         });
    s.At({0x00476950, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(
                          &Script::CStruct::GetChecksum))},
         12, [](Regs& r) {
             GraphNode& n = NodeOf(r.ecx);
             Record({0x6E7C0000, r.ecx, r.args[0]});
             if (r.args[0] == 0x12B4E660) {
                 *reinterpret_cast<uint32_t*>(r.args[1]) = n.nodeClass;
                 return 1u;
             }
             if (r.args[0] == 0x7321A8D6 && n.hasType) {
                 *reinterpret_cast<uint32_t*>(r.args[1]) = n.type;
                 return 1u;
             }
             return 0u;
         });
    s.At({0x00476630, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(
                          &Script::CStruct::GetInteger))},
         12, [](Regs& r) {
             Record({0x6E140000, r.ecx, r.args[0], r.args[2]});
             if ((r.ecx & 0xFF000000) == 0x5E000000) {
                 GraphNode& n = NodeOf(r.ecx);
                 if (r.args[0] != kPathNum || !n.hasPath)
                     return 0u;
                 *reinterpret_cast<int*>(r.args[1]) = n.path;
                 return 1u;
             }
             auto it = g_params.find(r.args[0]);
             if (it == g_params.end())
                 return 0u;
             *reinterpret_cast<int*>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476AF0, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t)>(
                          &Script::CStruct::ContainsComponentNamed))},
         4, [](Regs& r) {
             bool has = r.args[0] == kPathNum && NodeOf(r.ecx).hasPath;
             Record({0xC0A70000, r.ecx, r.args[0], has});
             return static_cast<uint32_t>(has);
         });
    s.At({0x00477B80, AddressOf(static_cast<void (Script::CStruct::*)(uint32_t, int)>(&Script::CStruct::AddInteger))},
         8, [](Regs& r) {
             GraphNode& n = NodeOf(r.ecx);
             if (r.args[0] == kPathNum) {
                 n.hasPath = true;
                 n.path = static_cast<int>(r.args[1]);
             }
             Record({0xADD10000, r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
}

void BuildGraph(Lcg& random)
{
    g_nodes.clear();
    uint32_t count = 4 + random.Below(10);
    static const uint32_t kClasses[] = {0x11111111, 0x22222222, 0x8E6B02AD, 0x30C19600, 0x4C23A77E, 0x4C23A77E};
    for (uint32_t i = 0; i < count; ++i) {
        GraphNode n;
        n.nodeClass = random.Pick(kClasses);
        if (random.Below(2)) {
            n.hasType = true;
            n.type = random.Below(2) ? 0xCBA10FFA : 0x0000BEEF;
        }
        if (random.Below(6) == 0) {
            n.hasPath = true;
            n.path = static_cast<int>(random.Below(3));
        }
        g_nodes.push_back(n);
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t links = random.Below(4) == 0 ? 0 : 1 + random.Below(2);
        for (uint32_t k = 0; k < links; ++k)
            g_nodes[i].links.push_back(random.Below(3) == 0 ? random.Below(count) : (i + 1) % count);
    }
}

// The manager made (twice: the instance kept), allocated (groups of slots, the extra group), objects added to
// paths by their nodes' PathNum and to the extra group (until full), groups emptied and objects removed, every
// path numbered (classes skipped, the 0x4C23A77E kind with type 0xCBA10FFA, nodes without links, nodes numbered
// already; chains running into numbered nodes, loops), freed, and the script command (defaults, given sizes).
void TestPathManager()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestPathManager skipped (the memory manager is already set up)");
        return;
    }
    ObjsysTests::SaveGlobals({{0x007CC979, 1}, {0x007D1800, 4}, {0x006F2264, 4}});
    StandIns& s = *new StandIns();
    StandInPathWorld(s);
    for (uint32_t seed = 1; seed <= 30; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            ObjsysTests::ZeroGlobals();
            g_pmArena = PmArena(original);
            SetUpManager(g_pmArena);
            Lcg random(0x9A7B0000 + seed);
            BuildGraph(random);
            g_params.clear();
            // stand-in objects: a vtable word and the smart pointer list (CRefCounted's layout)
            for (uint32_t i = 0; i < 8; ++i)
                *reinterpret_cast<uint32_t*>(g_pmArena + kObjects + 0x20 * i) = 0x0B1E0000 + i;
            auto object = [](uint32_t i) { return reinterpret_cast<uint32_t>(g_pmArena + kObjects + 0x20 * i); };
            auto* manager = Fn<uint8_t*(__cdecl*)()>(original, 0x004694E0)();
            for (int i = 0; i < 4; ++i) // (as made)
                Record({PmReloc(reinterpret_cast<uint32_t*>(manager)[i])});
            Record({PmReloc(reinterpret_cast<uint32_t>(manager)),
                    PmReloc(reinterpret_cast<uint32_t>(Fn<uint8_t*(__cdecl*)()>(original, 0x004694E0)()))});
            int groups = 2 + static_cast<int>(random.Below(3));
            int size = 1 + static_cast<int>(random.Below(3));
            int extra = 1 + static_cast<int>(random.Below(4));
            Fn<void(__thiscall*)(void*, int, int, int)>(original, 0x00469960)(manager, groups, size, extra);
            for (int step = 0; step < 30; ++step) {
                uint32_t node = 0x0E0E0000 + random.Below(static_cast<uint32_t>(g_nodes.size()));
                GraphNode& n = g_nodes[node & 0xFFFF];
                auto* groupArray = *reinterpret_cast<uint8_t**>(manager);
                auto* extraGroup = *reinterpret_cast<uint8_t**>(manager + 4);
                switch (random.Below(6)) {
                case 0:
                case 1:
                    if (n.hasPath && n.path >= groups)
                        break; // (past the groups: out of bounds, as the original)
                    Record({PmReloc(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x00469620)(
                        manager, object(random.Below(8)), node))});
                    break;
                case 2:
                    Record({PmReloc(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(original, 0x00469670)(
                        manager, object(random.Below(8)), node))});
                    break;
                case 3:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00469580)(
                        random.Below(3) == 0 ? extraGroup : groupArray + 8 * random.Below(groups),
                        object(random.Below(8)));
                    break;
                case 4:
                    Fn<void(__thiscall*)(void*)>(original, 0x00469540)(
                        random.Below(3) == 0 ? extraGroup : groupArray + 8 * random.Below(groups));
                    break;
                default:
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x004695D0)(
                        groupArray + 8 * random.Below(groups), object(random.Below(8)));
                    break;
                }
            }
            // a group of its own destroyed while it holds objects (then again: nothing)
            {
                auto* group = reinterpret_cast<uint32_t*>(g_pmArena + 0x1800);
                group[0] = 0;
                group[1] = 0;
                Fn<void(__thiscall*)(void*, int)>(original, 0x004698C0)(group, 3);
                for (int k = 0; k < 2; ++k)
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x004695D0)(group, object(random.Below(8)));
                Fn<void(__thiscall*)(void*)>(original, 0x004694A0)(group);
                Fn<void(__thiscall*)(void*)>(original, 0x004694A0)(group);
            }
            std::vector<uint32_t> groupsImage = ArenaImage(g_pmArena); // (the groups and objects' lists)
            g_trace.insert(g_trace.end(), groupsImage.begin(), groupsImage.end());
            Fn<void(__thiscall*)(void*)>(original, 0x004696C0)(manager);
            for (const GraphNode& n : g_nodes)
                Record({static_cast<uint32_t>(n.hasPath), static_cast<uint32_t>(n.path)});
            groupsImage = ArenaImage(g_pmArena); // (numbering lets every group go)
            g_trace.insert(g_trace.end(), groupsImage.begin(), groupsImage.end());
            Fn<void(__thiscall*)(void*)>(original, 0x00469820)(manager);
            // freed: numbering and freeing again do nothing
            Fn<void(__thiscall*)(void*)>(original, 0x004696C0)(manager);
            Fn<void(__thiscall*)(void*)>(original, 0x00469820)(manager);
            // the script command (the instance made if needed: the first seeds start without one)
            if (seed % 2)
                GameVariable<uint32_t>(0x006F2264) = 0;
            uint32_t which = random.Below(3);
            if (which == 0) {
                // (nothing given: 200 groups of 20, an extra group of 200)
            } else if (which == 1) {
                g_params[0x0C230B04] = 1 + static_cast<int>(random.Below(3));
                g_params[0xF6A4081D] = 1 + static_cast<int>(random.Below(3));
                g_params[0x13AFFFF9] = 1 + static_cast<int>(random.Below(3));
            } else {
                g_params[0x0C230B04] = 3; // (the slot sizes from the defaults: 20, 200)
            }
            Record({Fn<uint32_t(__cdecl*)(uint32_t, uint32_t)>(original, 0x00469A80)(0x5F000000, 0) & 0xFF});
            Record({PmReloc(GameVariable<uint32_t>(0x006F2264))});
            std::vector<uint32_t> trace = g_trace;
            for (uint32_t& word : trace)
                word = PmReloc(word);
            std::vector<uint32_t> image = ArenaImage(g_pmArena);
            trace.insert(trace.end(), image.begin(), image.end());
            GameVariable<void*>(0x007D1800) = nullptr;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "path manager: groups, paths numbered, freed, AllocatePathManMemory");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestPathManager, ObjsysTests::CleanUp);

} // namespace
