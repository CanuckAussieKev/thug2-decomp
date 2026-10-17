// Self-tests for the script manager (src/game/script/cscript_manager.cpp, group D of the CScript batch): each
// original against ours, over the same stand-in scripts linked into the script list. Everything they call is
// stood in for by recording code (the CScript methods of the other groups, the event handler table, the tracker,
// symbols, pools, the memory manager, the network), so the two are compared call by call, then the list.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/obj/event.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CScript;
using Script::CStruct;
using Script::SReturnAddress;

constexpr int kBlocks = 40;
alignas(16) uint8_t g_blocks[kBlocks][0x100];  // stand-in scripts (0xF4 bytes each)
SReturnAddress g_levels[kBlocks][4];          // call stacks deeper than the inline two
alignas(16) uint8_t g_structs[12][16];
alignas(16) uint8_t g_objects[4][0x40];       // +0 vtable, +0x14 id, +0x20 "runs scripts" answer
void* g_objectVtable[8];
alignas(16) uint8_t g_tables[kBlocks][16];    // event handler tables
alignas(16) uint8_t g_manager[0x600];
alignas(16) uint8_t g_net[0x40];
alignas(16) uint8_t g_client[0x10];
alignas(16) uint8_t g_tracker[0x10];
Script::CSymbolTableEntry g_symbols[8];
Mem::Pool* const kScriptPool = reinterpret_cast<Mem::Pool*>(0x00001110); // never dereferenced (stood in)
Mem::Pool* const kStructPool = reinterpret_cast<Mem::Pool*>(0x00001120);

CScript* Block(int i) { return reinterpret_cast<CScript*>(g_blocks[i]); }
Obj::CObject* Object(int i) { return i < 0 ? nullptr : reinterpret_cast<Obj::CObject*>(g_objects[i]); }
CStruct* Struct(int i) { return i < 0 ? nullptr : reinterpret_cast<CStruct*>(g_structs[i]); }
template <typename T, size_t N, size_t M>
bool Inside(uint32_t p, T (&array)[N][M], uint32_t& index)
{
    auto base = reinterpret_cast<uint32_t>(array);
    if (p < base || p >= base + sizeof(array))
        return false;
    index = (p - base) / sizeof(array[0]);
    return true;
}
uint32_t Id(uint32_t p)
{
    uint32_t i;
    if (Inside(p, g_blocks, i))
        return 0xB000 + i + (p - reinterpret_cast<uint32_t>(g_blocks[i])) * 0x100;
    if (Inside(p, g_structs, i))
        return 0x5700 + i;
    if (Inside(p, g_objects, i))
        return 0x0B00 + i;
    if (Inside(p, g_tables, i))
        return 0x7AB0 + i;
    if (Inside(p, g_levels, i))
        return 0x1E00 + i;
    if (p == reinterpret_cast<uint32_t>(g_tracker))
        return 0x7AC;
    if (p == reinterpret_cast<uint32_t>(g_client))
        return 0xC11;
    if (p == reinterpret_cast<uint32_t>(g_net))
        return 0x9E7;
    if (p == reinterpret_cast<uint32_t>(g_manager))
        return 0x3A9;
    return p;
}
template <typename T>
uint32_t Id(T* p) { return Id(reinterpret_cast<uint32_t>(p)); }

bool g_original;
std::vector<int> g_results[kBlocks]; // Update's answers, in turn (then 2)
int g_updates[kBlocks];
std::function<void(int)> g_onUpdate;  // run inside Update (block index)
int g_nextBlock;                     // the allocation stand-ins hand out blocks from here
int g_nextStruct;

// The globals the manager reads or writes.
std::vector<Saved> SavedGlobals()
{
    return {{0x006F3538, 4, {}}, {0x006F353C, 4, {}}, {0x006F3540, 4, {}}, {0x006F3544, 2, {}}, {0x006F352C, 1, {}},
            {0x006F3530, 4, {}}, {0x006F3548, 16, {}}, {0x006F3614, 16, {}}, {0x007CC979, 1, {}}, {0x007D1800, 4, {}},
            {0x007CCE14, 4, {}}, {0x006F0FC0, 4, {}}};
}

// The CScript methods of the other groups, the event table and the tracker (ReportStopped's calls).
void StandInScriptMethods(StandIns& s)
{
    s.At({0x004750B0, AddressOf(&Script::CScript::Update)}, 0, [](Regs& r) { // Update
        uint32_t i;
        Inside(r.ecx, g_blocks, i);
        int call = g_updates[i]++;
        Record({0x0BDA, Id(r.ecx)});
        if (g_onUpdate)
            g_onUpdate(static_cast<int>(i));
        return static_cast<uint32_t>(call < static_cast<int>(g_results[i].size()) ? g_results[i][call] : 2);
    });
    s.At({0x00474420, AddressOf(&Script::CScript::Destroy)}, 0, [](Regs& r) { // Destroy: leaves the list (the block stays as it is)
        auto* script = reinterpret_cast<CScript*>(r.ecx);
        Record({0xDE57, Id(r.ecx)});
        if (script->mp_previous)
            script->mp_previous->mp_next = script->mp_next;
        else
            Script::ScriptListHead() = script->mp_next;
        if (script->mp_next)
            script->mp_next->mp_previous = script->mp_previous;
        --Script::ScriptCount();
        return 0u;
    });
    s.At({0x004499B0, AddressOf(static_cast<void (*)(void*)>(&Script::CScript::operator delete))}, 0, [](Regs& r) { Record({0xDE1E, Id(r.args[0])}); return 0u; }); // operator delete
    s.At({0x00473A80, AddressOf(&Script::CScript::ClearScript)}, 0, [](Regs& r) { // ClearScript
        Record({0xC1EA, Id(r.ecx)});
        reinterpret_cast<CScript*>(r.ecx)->m_pc = nullptr;
        return 0u;
    });
    s.At({0x0045FA60, AddressOf(&Obj::CEventHandlerTable::UnregisterWithTracker)}, 4, [](Regs& r) { Record({0xFA60, Id(r.ecx), Id(r.args[0])}); return 0u; });
    s.At({0x0045FE60, AddressOf(static_cast<void (Obj::CEventHandlerTable::*)(uint32_t)>(&Obj::CEventHandlerTable::RemoveGroup))}, 4, [](Regs& r) { Record({0xFE60, Id(r.ecx), r.args[0]}); return 0u; });
    s.At({0x00462B90, AddressOf(&Obj::CTracker::ForgetScript)}, 4, [](Regs& r) { Record({0x2B90, Id(r.ecx), Id(r.args[0])}); return 0u; });
    // Objects: vtable +0x0C (a member function by name), +0x1C (whether its scripts run).
    for (void*& entry : g_objectVtable)
        entry = nullptr;
    g_objectVtable[3] = s.Thunk(12, [](Regs& r) {
        Record({0x0B03, Id(r.ecx), r.args[0], Id(r.args[1]), Id(r.args[2])});
        return 1u;
    });
    g_objectVtable[7] = s.Thunk(0, [](Regs& r) {
        Record({0x0B07, Id(r.ecx)});
        return static_cast<uint32_t>(reinterpret_cast<uint8_t*>(r.ecx)[0x20]);
    });
}

void ResetObjects()
{
    std::memset(g_objects, 0, sizeof(g_objects));
    for (int i = 0; i < 4; ++i) {
        *reinterpret_cast<void***>(g_objects[i]) = g_objectVtable;
        *reinterpret_cast<uint32_t*>(g_objects[i] + 0x14) = 0x1D00 + i;
        g_objects[i][0x20] = i != 1; // object 1 is paused
    }
}

// One stand-in script on the list.
struct Spec {
    uint8_t flags;
    bool running;
    uint32_t name, spawnId, uniqueId, file;
    int object;
    std::vector<std::pair<uint32_t, int>> levels; // name, object
    bool handlers;
    uint32_t callback;
    int callbackParams;
};
constexpr uint32_t A = 0xAAAA0001, B = 0xBBBB0002, C = 0xCCCC0003, D = 0xDDDD0004, E = 0xEEEE0005;
constexpr uint32_t F1 = 0xF1F10001, F2 = 0xF2F20002;
std::vector<Spec> StandardList()
{
    return {
        {0x01, true, A, 7, 100, F1, 0, {}, true, 0, -1},
        {0x00, true, A, 7, 101, F1, 0, {{B, 1}}, false, 0, -1},
        {0x13, true, B, 8, 102, F2, -1, {{A, 0}}, true, 0, -1},
        {0x05, true, C, 7, 103, 0, 1, {{B, -1}, {C, 0}}, false, 0x3333, 3},
        {0x09, true, A, 9, 7, F1, 0, {}, false, 0x4444, 4},
        {0x0D, true, D, 0, 104, 7, 2, {}, true, 0, -1},
        {0x01, false, C, 7, 105, F2, 1, {{A, 2}}, false, 0x6666, -1},
        {0x14, true, B, 0x77, 106, 0, -1, {}, false, 0, -1},
        {0x19, true, A, 8, 107, F1, -1, {{D, 0}, {B, 1}, {A, -1}}, true, 0, -1},
        {0x01, false, E, 9, 108, F2, 2, {}, false, 0x9999, 9},
        {0x00, false, E, 0, 109, 0, -1, {}, false, 0, -1},
        {0x00, false, D, 0, 110, 0, 3, {}, true, 0, -1},
    };
}
void Build(const std::vector<Spec>& specs)
{
    std::memset(g_blocks, 0xCD, sizeof(g_blocks));
    std::memset(g_levels, 0, sizeof(g_levels));
    CScript* previous = nullptr;
    for (size_t i = 0; i < specs.size(); ++i) {
        const Spec& spec = specs[i];
        CScript* s = Block(static_cast<int>(i));
        std::memset(s, 0, sizeof(CScript));
        s->mp_previous = previous;
        if (previous)
            previous->mp_next = s;
        else
            Script::ScriptListHead() = s;
        previous = s;
        s->m_pc = spec.running ? reinterpret_cast<const uint8_t*>(0x00C0DE00 + i) : nullptr;
        s->m_spawnFlags = spec.flags;
        s->m_scriptName = spec.name;
        s->m_spawnId = spec.spawnId;
        s->m_uniqueId = spec.uniqueId;
        s->m_spawnerFile = spec.file;
        s->m_object.mp_object = Object(spec.object);
        s->m_returnAddresses = spec.levels.size() > 2 ? g_levels[i] : s->m_inlineReturnAddresses;
        s->m_numReturnAddresses = static_cast<int>(spec.levels.size());
        for (size_t l = 0; l < spec.levels.size(); ++l) {
            s->m_returnAddresses[l].mScriptName = spec.levels[l].first;
            s->m_returnAddresses[l].mpObject = Object(spec.levels[l].second);
        }
        s->m_eventHandlers = spec.handlers ? reinterpret_cast<Obj::CEventHandlerTable*>(g_tables[i]) : nullptr;
        s->m_callbackScript = spec.callback;
        s->m_callbackParams = Struct(spec.callbackParams);
    }
    if (previous)
        previous->mp_next = nullptr;
    else
        Script::ScriptListHead() = nullptr;
    Script::ScriptCount() = static_cast<int>(specs.size());
}
// The list from the head, every block's state and the manager's globals.
void RecordList()
{
    int n = 0;
    for (CScript* s = Script::ScriptListHead(); s && n < 40; s = s->mp_next, ++n)
        Record({0x1157, Id(s), Id(s->mp_previous)});
    for (int i = 0; i < kBlocks; ++i) {
        CScript* s = Block(i);
        Record({Id(s->mp_next), Id(s->m_pc), s->m_spawnFlags, s->m_spawnId, static_cast<uint32_t>(s->m_node),
                s->m_scriptName, s->m_callbackScript, Id(s->m_callbackParams), Id(s->m_params),
                Id(s->m_object.mp_object), s->m_uniqueId});
    }
    Record({static_cast<uint32_t>(Script::ScriptCount()), Script::UpdatingSpawnedScripts(),
            Script::DeleteSpawnedScriptsPending(), Script::OnePerFrameReleased(), Script::NextScriptUniqueId()});
}

void BeginRun(bool original)
{
    g_original = original;
    g_trace.clear();
    g_onUpdate = nullptr;
    for (auto& results : g_results)
        results.clear();
    std::memset(g_updates, 0, sizeof(g_updates));
    ResetObjects();
    std::memset(g_manager, 0, sizeof(g_manager));
    *reinterpret_cast<uint32_t*>(g_manager + 0x124) = 0x5C4EA900;
    GameVariable<void*>(0x007D1800) = g_manager;
    GameVariable<void*>(0x007CCE14) = g_net;
    GameVariable<void*>(0x006F0FC0) = g_tracker;
    GameVariable<bool>(0x007CC979) = true;
    Script::UpdatingSpawnedScripts() = false;
    Script::DeleteSpawnedScriptsPending() = false;
    Script::OnePerFrameReleased() = true;
    Script::CurrentScriptVar() = nullptr;
    Script::NextScriptUniqueId() = 500;
}

// --- the walks ---------------------------------------------------------------------------------------------

using Walk = std::function<void(bool original)>;

std::vector<uint32_t> RunWalks(bool original, const std::vector<Walk>& walks)
{
    BeginRun(original);
    for (const Walk& walk : walks) {
        Build(StandardList());
        g_onUpdate = nullptr;
        walk(original);
        RecordList();
    }
    return g_trace;
}

template <typename Fn>
Fn Orig(uint32_t address) { return Original<Fn>(address); }

// The kill / stop / pause / lookup walks and DeleteSpawnedScripts / DeleteFinishedScripts / KillSpawnedScript
// over a list of 12 scripts (spawned or not, paused, pause-with-object, not session specific, tracker-blocked,
// with or without event handler tables, objects at the top and at deeper levels, a call stack of 3 on the heap),
// every match field tried for a hit and a miss.
void TestScriptManagerWalks()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    using VoidU = void(__cdecl*)(uint32_t);
    using ObjU = void(__cdecl*)(Obj::CObject*, uint32_t);
    using ObjUS = void(__cdecl*)(Obj::CObject*, uint32_t, CScript*);
    std::vector<Walk> walks;
    for (uint32_t name : {A, B, C, D, E, 0x12345678u, 0u})
        walks.push_back([=](bool o) { (o ? Orig<VoidU>(0x00473FA0) : Script::KillSpawnedScriptsThatReferTo)(name); });
    for (uint32_t id : {7u, 8u, 9u, 0u, F1, F2, 100u, 107u, 108u, 0x77u, 0x999u})
        walks.push_back([=](bool o) { (o ? Orig<VoidU>(0x00474040) : Script::KillSpawnedScriptsWithId)(id); });
    struct ObjectCase {
        int object;
        uint32_t value;
        int except;
    };
    for (ObjectCase c : std::vector<ObjectCase>{{0, 7, -1}, {0, 7, 0}, {1, 7, -1}, {-1, 8, -1}, {2, 0, -1}, {0, 8, 2},
                                                 {1, 9, -1}, {0, 9, 4}, {2, 7, -1}, {3, 0, -1}, {-1, 0x77, -1}}) {
        walks.push_back([=](bool o) {
            (o ? Orig<ObjUS>(0x004740D0) : Script::KillSpawnedScriptsWithObjectAndId)(
                Object(c.object), c.value, c.except < 0 ? nullptr : Block(c.except));
        });
    }
    for (ObjectCase c : std::vector<ObjectCase>{{0, A, -1}, {0, A, 0}, {1, C, -1}, {-1, B, -1}, {2, D, -1}, {0, B, -1},
                                                 {1, A, -1}, {2, C, 6}, {2, C, -1}, {0, D, -1}, {3, D, -1}}) {
        walks.push_back([=](bool o) {
            (o ? Orig<ObjUS>(0x00474190) : Script::KillSpawnedScriptsWithObjectAndName)(
                Object(c.object), c.value, c.except < 0 ? nullptr : Block(c.except));
        });
    }
    walks.push_back([](bool o) { (o ? Orig<void(__cdecl*)()>(0x00474250) : Script::StopAllScripts)(); });
    for (ObjectCase c : std::vector<ObjectCase>{{0, 0, 0}, {0, A, 0}, {1, B, 0}, {-1, 0, 0}, {-1, B, 0}, {2, C, 0},
                                                 {1, C, 0}, {3, 0, 0}, {2, E, 0}, {0, 0x999, 0}}) {
        walks.push_back([=](bool o) { (o ? Orig<ObjU>(0x004742B0) : Script::StopScriptsUsingObject)(Object(c.object), c.value); });
        walks.push_back([=](bool o) {
            (o ? Orig<ObjU>(0x00474350) : Script::StopScriptsUsingObjectThatReferTo)(Object(c.object), c.value);
        });
    }
    for (int object : {0, -1, 2, 1})
        walks.push_back([=](bool o) {
            (o ? Orig<void(__cdecl*)(Obj::CObject*)>(0x004756F0) : Script::StopAllScriptsUsingThisObject)(Object(object));
        });
    for (bool pause : {true, false})
        walks.push_back([=](bool o) { (o ? Orig<void(__cdecl*)(bool)>(0x00472D80) : Script::PauseSpawnedScripts)(pause); });
    for (int block : {3, 5, 7, 0, -1})
        walks.push_back([=](bool o) {
            (o ? Orig<void(__cdecl*)(CScript*)>(0x00472DC0) : Script::UnpauseSpawnedScript)(block < 0 ? nullptr : Block(block));
        });
    for (int block : {0, 1, 2, 8, 7, -1})
        walks.push_back([=](bool o) {
            Record({(o ? Orig<uint32_t(__cdecl*)(CScript*)>(0x00472E00) : Script::GetSpawnId)(block < 0 ? nullptr : Block(block))});
        });
    for (uint32_t id : {7u, 8u, 9u, 0u, 0x77u, 0x999u})
        walks.push_back([=](bool o) {
            Record({Id((o ? Orig<CScript*(__cdecl*)(uint32_t)>(0x00472E20) : Script::FindSpawnedScriptWithId)(id))});
        });
    for (bool updating : {false, true})
        walks.push_back([=](bool o) {
            Script::UpdatingSpawnedScripts() = updating;
            (o ? Orig<void(__cdecl*)()>(0x004753C0) : Script::DeleteSpawnedScripts)();
            Script::UpdatingSpawnedScripts() = false;
        });
    walks.push_back([](bool o) { (o ? Orig<void(__cdecl*)()>(0x00475700) : Script::DeleteFinishedScripts)(); });
    for (int block : {0, 1, 2, 10, -1})
        walks.push_back([=](bool o) {
            (o ? Orig<void(__cdecl*)(CScript*)>(0x004756C0) : Script::KillSpawnedScript)(block < 0 ? nullptr : Block(block));
        });
    // An empty list.
    walks.push_back([](bool o) {
        Script::ScriptListHead() = nullptr;
        (o ? Orig<void(__cdecl*)()>(0x00474250) : Script::StopAllScripts)();
        (o ? Orig<VoidU>(0x00474040) : Script::KillSpawnedScriptsWithId)(7);
        Record({Id((o ? Orig<CScript*(__cdecl*)(uint32_t)>(0x00472E20) : Script::FindSpawnedScriptWithId)(7))});
        (o ? Orig<void(__cdecl*)()>(0x004753C0) : Script::DeleteSpawnedScripts)();
        (o ? Orig<void(__cdecl*)(bool)>(0x00472D80) : Script::PauseSpawnedScripts)(true);
    });

    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInScriptMethods(*s);
        traces[0] = RunWalks(true, walks);
        traces[1] = RunWalks(false, walks);
        RemoveStandIns();
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "script manager walks (kill/stop/pause/find/delete)");
}

// UpdateSpawnedScripts over the list, with Update's answers (finished, waiting, blocked, interrupt) and what an
// Update may do meanwhile: spawn a script, stop or kill another one, ask for DeleteSpawnedScripts.
void TestUpdateSpawnedScripts()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    auto results = [](std::initializer_list<std::pair<int, int>> answers) {
        for (auto [block, answer] : answers)
            g_results[block] = {answer};
    };
    std::vector<std::function<void(bool)>> cases;
    // Plain answers: finished with and without callbacks, paused with and without code, objects paused or not.
    cases.push_back([&](bool) { results({{0, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {8, 0}, {9, 0}}); });
    cases.push_back([&](bool) { results({{0, 2}, {2, 1}, {4, 5}, {6, 0}, {8, 2}, {9, 0}}); });
    cases.push_back([&](bool) {
        results({{3, 0}, {4, 0}, {5, 0}, {6, 0}, {8, 0}});
        Block(3)->m_pc = nullptr;         // paused but finished: updated
        g_objects[0][0x20] = 0;           // block 4's object paused
        Block(5)->m_spawnFlags = 0x09;    // pause with object 2 (runs)
        Block(8)->m_object.mp_object = Object(1); // pause with object 1 (paused)
    });
    // Updates that change the list.
    cases.push_back([&](bool o) {
        results({{0, 0}, {2, 0}, {4, 0}, {6, 0}, {9, 0}, {20, 0}});
        g_onUpdate = [o](int block) {
            if (block == 0) { // spawns a script (at the head: not reached this frame)
                CScript* s = Block(20);
                std::memset(s, 0, sizeof(CScript));
                s->m_spawnFlags = 1;
                s->m_pc = reinterpret_cast<const uint8_t*>(0xC0DE20);
                s->mp_next = Script::ScriptListHead();
                Script::ScriptListHead()->mp_previous = s;
                Script::ScriptListHead() = s;
                ++Script::ScriptCount();
            } else if (block == 2) {
                (o ? Orig<void(__cdecl*)()>(0x004753C0) : Script::DeleteSpawnedScripts)(); // deferred
            } else if (block == 4) {
                Block(6)->m_pc = nullptr; // stopped: finished at its turn
                Block(5)->m_spawnFlags &= ~Script::SCRIPT_FLAG_PAUSED;
            }
        };
    });
    cases.push_back([&](bool o) {
        results({{0, 2}, {2, 2}, {4, 2}, {8, 0}});
        g_onUpdate = [o](int block) {
            if (block == 2) // kills the next spawned script outright (read after "freeing")
                (o ? Orig<void(__cdecl*)(CScript*)>(0x004756C0) : Script::KillSpawnedScript)(Block(3));
            if (block == 4) // kills an earlier one
                (o ? Orig<void(__cdecl*)(CScript*)>(0x004756C0) : Script::KillSpawnedScript)(Block(0));
        };
    });
    // The non-spawned script after the cursor becomes spawned during its Update: already skipped (the next spawned
    // one was picked before the Update).
    cases.push_back([&](bool) {
        results({{0, 2}, {1, 0}, {2, 2}});
        g_onUpdate = [](int block) {
            if (block == 0)
                Block(1)->m_spawnFlags |= Script::SCRIPT_FLAG_SPAWNED;
        };
    });
    // The head isn't spawned, the tail is; nothing spawned at all.
    cases.push_back([&](bool) {
        std::vector<Spec> list = StandardList();
        list.erase(list.begin());
        list.push_back({0x01, true, A, 1, 1, 0, -1, {}, false, 0x7777, 7});
        Build(list);
        results({{0, 0}, {10, 2}, {11, 0}});
    });
    cases.push_back([&](bool) {
        Build({{0x00, true, A, 1, 1, 0, -1, {}, false, 0, -1}, {0x10, false, B, 1, 1, 0, -1, {}, false, 0x11, -1}});
    });
    cases.push_back([&](bool) { Build({}); });
    // Already asked to delete (left from before): done at the end.
    cases.push_back([&](bool) { Script::DeleteSpawnedScriptsPending() = true; results({{0, 0}}); });

    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInScriptMethods(*s);
        s->At({0x00475790,
               AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(&Script::RunScript))},
              0, [](Regs& r) {
                  Record({0x5C41, r.args[0], Id(r.args[1]), Id(r.args[2]), r.args[3], r.args[4],
                          Script::UpdatingSpawnedScripts()});
                  return 0u;
              });
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            BeginRun(original);
            for (auto& setUp : cases) {
                Build(StandardList());
                g_onUpdate = nullptr;
                for (auto& r : g_results)
                    r.clear();
                std::memset(g_updates, 0, sizeof(g_updates));
                ResetObjects();
                Script::DeleteSpawnedScriptsPending() = false;
                Script::OnePerFrameReleased() = true;
                setUp(original);
                (original ? Orig<void(__cdecl*)()>(0x00475920) : Script::UpdateSpawnedScripts)();
                RecordList();
            }
            traces[run] = g_trace;
        }
        RemoveStandIns();
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "UpdateSpawnedScripts");
}

// --- spawning and running ----------------------------------------------------------------------------------

constexpr uint32_t kScript = 0x5C000001, kScriptNoParams = 0x5C000002, kCFunction = 0xCF000003,
                   kMember = 0x3E000004, kMemberByName = 0x3E000005, kInteger = 0x10000006, kMissing = 0x0BAD0007,
                   kCallback = 0xCB000008;
uint32_t g_scriptString, g_runString;
bool g_inNetGame;
int g_writeSize;
Script::CMemberFunctionEntry g_entry = {0x1234, 0x5678};

void SetUpSymbols(StandIns& s)
{
    std::memset(g_symbols, 0, sizeof(g_symbols));
    g_scriptString = Crc::GenerateCRCFromString("csd_spawn_me");
    g_runString = Crc::GenerateCRCFromString("csd_run_me");
    struct Def {
        uint32_t name;
        Script::ESymbolType type;
        uint32_t value;
    };
    void* cfunction = s.Thunk(0, [](Regs& r) {
        Record({0xCF, Id(r.args[0]), Id(r.args[1])});
        return 1u;
    });
    Def defs[] = {{kScript, Script::ESYMBOLTYPE_QSCRIPT, 0x00C0DE00},
                  {kScriptNoParams, Script::ESYMBOLTYPE_QSCRIPT, 0x00C0DE01},
                  {kCFunction, Script::ESYMBOLTYPE_CFUNCTION, reinterpret_cast<uint32_t>(cfunction)},
                  {kMember, Script::ESYMBOLTYPE_MEMBERFUNCTION, reinterpret_cast<uint32_t>(&g_entry)},
                  {kMemberByName, Script::ESYMBOLTYPE_MEMBERFUNCTION, 0},
                  {kInteger, Script::ESYMBOLTYPE_INTEGER, 5},
                  {g_scriptString, Script::ESYMBOLTYPE_QSCRIPT, 0x00C0DE02},
                  {g_runString, Script::ESYMBOLTYPE_QSCRIPT, 0x00C0DE03}};
    for (int i = 0; i < 8; ++i) {
        g_symbols[i].name = defs[i].name;
        g_symbols[i].type = defs[i].type;
        g_symbols[i].raw = defs[i].value;
    }
    s.At({0x00478CF0, AddressOf(&Script::LookUpSymbol)}, 0, [](Regs& r) {
        Record({0x5E1, r.args[0]});
        for (auto& symbol : g_symbols)
            if (symbol.name == r.args[0])
                return reinterpret_cast<uint32_t>(&symbol);
        return 0u;
    });
}

// Allocations: script blocks and structures (the pools stood in for, and the heap when there's no pool).
void StandInAllocation(StandIns& s)
{
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA11C, r.ecx});
        if (r.ecx == reinterpret_cast<uint32_t>(kScriptPool))
            return g_nextBlock < kBlocks ? reinterpret_cast<uint32_t>(Block(g_nextBlock++)) : 0u;
        return g_nextStruct < 12 ? reinterpret_cast<uint32_t>(Struct(g_nextStruct++)) : 0u;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA110, Id(r.ecx), r.args[0], r.args[1], r.args[2]});
        if (r.args[0] == sizeof(CScript))
            return g_nextBlock < kBlocks ? reinterpret_cast<uint32_t>(Block(g_nextBlock++)) : 0u;
        return g_nextStruct < 12 ? reinterpret_cast<uint32_t>(Struct(g_nextStruct++)) : 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0x9054, Id(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0x90B, Id(r.ecx)});
        return 0u;
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x1417, Id(r.ecx)});
        return r.ecx;
    });
    s.At({0x00478480, AddressOf(&CStruct::AppendStructure)}, 4, [](Regs& r) {
        Record({0xA99E, Id(r.ecx), Id(r.args[0])});
        return r.ecx;
    });
}

// SetScript (group A): recorded; the script gets code, and parameters for kScript; flags scribbled for kCallback
// (SpawnScript must keep bits 4-7 and clear 1-3).
void StandInSetScript(StandIns& s)
{
    s.At({0x00474540, AddressOf(static_cast<void (Script::CScript::*)(uint32_t, const uint8_t*, Script::CStruct*, Obj::CObject*)>(&Script::CScript::SetScript))}, 16, [](Regs& r) {
        Record({0x5E75, Id(r.ecx), r.args[0], r.args[1], Id(r.args[2]), Id(r.args[3]), Id(Script::ScriptListHead())});
        auto* script = reinterpret_cast<CScript*>(r.ecx);
        script->m_pc = reinterpret_cast<const uint8_t*>(0x00C0DE10);
        script->m_scriptName = r.args[0];
        if (r.args[0] == kScript)
            script->m_params = Struct(11);
        if (r.args[0] == kCallback)
            script->m_spawnFlags = 0xFE;
        return 0u;
    });
}

void StandInNet(StandIns& s, bool messagesToo)
{
    s.At({0x0044E4F0}, 0, [](Regs& r) {
        Record({0x4E7, Id(r.ecx)});
        return static_cast<uint32_t>(g_inNetGame);
    });
    if (messagesToo) {
        s.At({0x00473350, AddressOf(&Script::SendRunScriptMessage)}, 0, [](Regs& r) {
            Record({0x3350, r.args[0], Id(r.args[1]), Id(r.args[2])});
            return 0u;
        });
        s.At({0x00473420, AddressOf(&Script::SendSpawnScriptMessage)}, 0, [](Regs& r) {
            Record({0x3420, r.args[0], Id(r.args[1]), r.args[2], r.args[3] & 0xFF});
            return 0u;
        });
    }
    s.At({0x00479E20}, 0, [](Regs& r) {
        Record({0x9E20, Id(r.args[0]), r.args[2], r.args[3], r.args[4]});
        auto* buffer = reinterpret_cast<uint8_t*>(r.args[1]);
        for (int i = 0; i < g_writeSize; ++i)
            buffer[i] = static_cast<uint8_t>(0x40 + i * 7);
        return static_cast<uint32_t>(g_writeSize);
    });
    s.At({0x00532EA0}, 4, [](Regs& r) {
        Record({0x2EA0, Id(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_client);
    });
    s.At({0x00459E50}, 4, [](Regs& r) {
        uint8_t desc[0x1D];
        std::memcpy(desc, reinterpret_cast<void*>(r.args[0]), sizeof(desc));
        auto* data = *reinterpret_cast<uint8_t**>(desc + 4);
        std::memset(desc + 4, 0, 4);
        std::memset(desc + 0x12, 0, 2); // (the constructor leaves these as they were)
        uint16_t length;
        std::memcpy(&length, desc + 2, 2);
        // (the spawn message's last 3 bytes are whatever was on the original's stack)
        size_t hashed = desc[0] == 0x49 ? 13 : length;
        Record({0x9E50, Id(r.ecx), HashBytes(desc, sizeof(desc)), HashBytes(data, hashed), *reinterpret_cast<uint32_t*>(data),
                *reinterpret_cast<uint32_t*>(data + 4)});
        return 0u;
    });
}

void ResetSpawnRun(bool poolsPresent)
{
    std::memset(g_blocks, 0xCD, sizeof(g_blocks));
    std::memset(g_structs, 0xCD, sizeof(g_structs));
    g_nextBlock = 12;
    g_nextStruct = 4;
    for (auto& r : g_results)
        r.clear();
    std::memset(g_updates, 0, sizeof(g_updates));
    auto& scripts = GameVariable<Mem::PoolPair>(0x006F3548);
    auto& structs = GameVariable<Mem::PoolPair>(0x006F3614);
    scripts = {1, 0, {nullptr, poolsPresent ? kScriptPool : nullptr}};
    structs = {0, 0, {poolsPresent ? kStructPool : nullptr, nullptr}};
}

// SpawnScript (both), RunScript (both), RunScriptOnce, ScriptExists: scripts, C functions, member functions (by
// table entry and by name), other symbols and missing names; callbacks with and without parameters; the network
// flag with and without a network game; the pools present and absent; a list with scripts on it already.
void TestSpawnAndRunScript()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    using Spawn = CScript*(__cdecl*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t, bool, bool, bool, bool);
    using SpawnString = CScript*(__cdecl*)(const char*, CStruct*, uint32_t, CStruct*, int, uint32_t, bool, bool, bool, bool);
    using Run = void(__cdecl*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*);
    using RunString = void(__cdecl*)(const char*, CStruct*, Obj::CObject*, bool);
    using RunOnce = void(__cdecl*)(uint32_t, CStruct*, Obj::CObject*, CStruct*);
    Spawn spawnOurs = &Script::SpawnScript;
    SpawnString spawnStringOurs = &Script::SpawnScript;
    Run runOurs = &Script::RunScript;
    RunString runStringOurs = &Script::RunScript;

    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInScriptMethods(*s);
        SetUpSymbols(*s);
        StandInAllocation(*s);
        StandInSetScript(*s);
        StandInNet(*s, true);
        s->At({0x00472980, AddressOf(static_cast<bool (*)(Script::CMemberFunctionEntry*, Script::CStruct*, Obj::CObject*, Script::CScript*)>(&Script::CallMemberFunction))}, 0, [](Regs& r) {
            Record({0x2980, r.args[0] == reinterpret_cast<uint32_t>(&g_entry), Id(r.args[1]), Id(r.args[2]), r.args[3]});
            return 1u;
        });
        for (int run = 0; run < 2; ++run) {
            bool o = run == 0;
            BeginRun(o);
            Spawn spawn = o ? Orig<Spawn>(0x00475420) : spawnOurs;
            SpawnString spawnString = o ? Orig<SpawnString>(0x00475670) : spawnStringOurs;
            Run runScript = o ? Orig<Run>(0x00475790) : runOurs;
            RunString runString = o ? Orig<RunString>(0x004758F0) : runStringOurs;
            RunOnce once = o ? Orig<RunOnce>(0x004752F0) : &Script::RunScriptOnce;
            auto exists = o ? Orig<bool(__cdecl*)(uint32_t)>(0x00472DE0) : &Script::ScriptExists;
            for (bool pools : {true, false}) {
                for (int list = 0; list < 2; ++list) {
                    ResetSpawnRun(pools);
                    if (list)
                        Build({{0x01, true, A, 1, 1, 0, -1, {}, false, 0, -1}, {0x00, true, B, 2, 2, 0, 0, {}, false, 0, -1}});
                    else
                        Build({});
                    Script::NextScriptUniqueId() = 500;
                    g_inNetGame = false;
                    Record({Id(spawn(kScript, Struct(0), 0, Struct(1), 3, 7, false, false, false, false))});
                    Record({Id(spawn(kScript, nullptr, kCallback, Struct(1), -1, 9, true, false, true, true))});
                    Record({Id(spawn(kCallback, Struct(2), kCallback, nullptr, 0x40, 0, false, true, false, true))});
                    g_inNetGame = true;
                    Record({Id(spawn(kScriptNoParams, Struct(0), kCallback, Struct(3), 5, 0x99, true, true, true, false))});
                    Record({Id(spawn(kScriptNoParams, Struct(0), 0, nullptr, 6, 0x98, true, false, false, false))});
                    Record({Id(spawn(kCFunction, Struct(2), kCallback, Struct(1), 4, 3, true, true, true, true))});
                    Record({Id(spawn(kMissing, Struct(2), 0, nullptr, 1, 2, false, false, false, false))});
                    Record({Id(spawn(kInteger, nullptr, 0, nullptr, 1, 2, false, false, false, false))});
                    Record({Id(spawn(kMember, nullptr, 0, nullptr, 1, 2, false, false, false, false))});
                    Record({Id(spawnString("csd_spawn_me", Struct(3), kCallback, Struct(0), 8, 0x55, true, false, true, false))});
                    RecordList();

                    g_results[g_nextBlock] = {2, 1, 5, 2, 0};
                    runScript(kScript, Struct(0), Object(0), false, "ignored");
                    g_results[g_nextBlock] = {0};
                    runScript(kScriptNoParams, nullptr, nullptr, true, nullptr);
                    g_inNetGame = false;
                    runScript(kCFunction, Struct(1), Object(1), true, nullptr);
                    runScript(kMember, Struct(2), Object(0), false, nullptr);
                    runScript(kMemberByName, Struct(3), Object(2), true, nullptr);
                    runScript(kInteger, Struct(3), Object(2), true, nullptr);
                    runScript(kMissing, Struct(3), Object(2), true, nullptr);
                    g_results[g_nextBlock] = {1, 0};
                    runString("csd_run_me", Struct(2), Object(3), true);
                    runString("csd_not_there", Struct(2), Object(3), true);
                    RecordList();

                    g_results[g_nextBlock] = {0};
                    once(kScript, Struct(0), Object(0), Struct(2));
                    g_results[g_nextBlock] = {2};
                    once(kScriptNoParams, nullptr, nullptr, Struct(1));
                    for (uint32_t name : {kScript, kCFunction, kMember, kInteger, kMissing, g_runString})
                        Record({exists(name)});
                    RecordList();
                }
            }
            traces[run] = g_trace;
        }
        RemoveStandIns();
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "SpawnScript / RunScript / RunScriptOnce / ScriptExists");
}

// The RunScript (0x48) and SpawnScript (0x49) network messages: in and out of a network game, with and without an
// object, parameter buffers of different sizes.
void TestScriptNetMessages()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    using RunMessage = void(__cdecl*)(uint32_t, CStruct*, Obj::CObject*);
    using SpawnMessage = void(__cdecl*)(uint32_t, Obj::CObject*, int, bool);
    std::vector<uint32_t> traces[2];
    {
        auto* s = new StandIns;
        StandInNet(*s, false);
        for (int run = 0; run < 2; ++run) {
            bool o = run == 0;
            BeginRun(o);
            RunMessage runMessage = o ? Orig<RunMessage>(0x00473350) : &Script::SendRunScriptMessage;
            SpawnMessage spawnMessage = o ? Orig<SpawnMessage>(0x00473420) : &Script::SendSpawnScriptMessage;
            for (bool net : {false, true}) {
                g_inNetGame = net;
                for (int size : {0, 1, 0x13, 0x3F8}) {
                    g_writeSize = size;
                    runMessage(0x12340000u + size, Struct(size & 3), Object(size & 1 ? 2 : -1));
                }
                runMessage(0xFEDC, nullptr, Object(1));
                spawnMessage(0xABCD, Object(0), 3, true);
                spawnMessage(0xABCE, nullptr, -1, false);
                spawnMessage(0xABCF, Object(3), 0x7FFF0000, true);
            }
            traces[run] = g_trace;
        }
        RemoveStandIns();
    }
    RestoreAll(saved);
    Compare(traces[0], traces[1], "the RunScript / SpawnScript network messages");
}

} // namespace

SELFTEST(Early, TestScriptManagerWalks, RemoveStandIns);
SELFTEST(Early, TestUpdateSpawnedScripts, RemoveStandIns);
SELFTEST(Early, TestSpawnAndRunScript, RemoveStandIns);
SELFTEST(Early, TestScriptNetMessages, RemoveStandIns);
