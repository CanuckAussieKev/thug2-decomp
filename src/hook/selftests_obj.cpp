// Self-tests for the object base and its managers (src/game/obj/object.cpp, objman.cpp): the original and ours run
// the same calls on identical stand-in objects, managers and structures in an arena. Everything they call outside
// the batch (the structure functions, the memory manager, the CScript methods and the script manager, the smart
// pointers, CRefCounted, the tracker, the task system, the objects' and managers' virtual functions) is stood in
// for by recording code (written over the game's function and ours alike), so the two are compared call by call,
// return value by return value, then the whole arena word by word.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/smtptr.h"
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
using Obj::CBaseManager;
using Obj::CGeneralManager;
using Obj::CObject;
using Script::CArray;
using Script::CComponent;
using Script::CScript;
using Script::CStruct;

constexpr uint32_t kArenaSize = 0x10000;
alignas(16) uint8_t g_obj_arenas[2][kArenaSize];
uint32_t g_obj_base;   // the current run's arena
uint32_t g_obj_bump;   // the stand-in allocator's next block
bool g_obj_original;   // this run calls the originals
bool g_obj_allocFail;  // the stand-in allocators return null
bool g_obj_killDuringEvent; // the stand-in ReceiveEvent clears the last smart pointer added
uint32_t g_obj_lastPointer;  // the last smart pointer added
uint32_t g_obj_found;        // what the stand-in tracker lookup returns (arena-relative or 0)
uint32_t g_obj_nextId;       // the stand-in tracker's next unique id
std::vector<Saved> g_obj_saved;

// The stand-in answers: (kind, structure, name) -> value (arena-relative for pointers).
std::map<std::tuple<char, uint32_t, uint32_t>, uint32_t> g_obj_answers;

// (both arenas, the same for both runs: a value that only happens to fall inside one of them maps the same way in
// each - ArenaWord, selftest.h)
bool InArena(uint32_t word) { return word - reinterpret_cast<uint32_t>(g_obj_arenas) <= 2 * kArenaSize; }
uint32_t Rel(uint32_t word) { return ArenaWord(g_obj_arenas, kArenaSize, word); }
uint32_t Rel(const void* p) { return Rel(reinterpret_cast<uint32_t>(p)); }
uint32_t Abs(uint32_t value) { return (value & 0xFFFF0000u) == 0xA0000000u ? g_obj_base + (value & 0xFFFF) : value; }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
uint32_t Bump(uint32_t size, uint8_t fill)
{
    uint32_t block = g_obj_bump;
    g_obj_bump += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(block), fill, size);
    return block;
}
void Answer(char kind, const void* structure, uint32_t name, uint32_t value)
{
    g_obj_answers[{kind, Rel(structure), name}] = Rel(value);
}
void Answer(char kind, const void* structure, uint32_t name, const void* value)
{
    Answer(kind, structure, name, reinterpret_cast<uint32_t>(value));
}
bool Lookup(char kind, uint32_t structure, uint32_t name, uint32_t& value)
{
    auto it = g_obj_answers.find({kind, Rel(structure), Rel(name)});
    if (it == g_obj_answers.end())
        return false;
    value = Abs(it->second);
    return true;
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_obj_saved);
    g_obj_saved.clear();
}

// Recording thunks for virtual functions (made once; the same in both runs).
void* g_obj_objectVtable[8];
void* g_obj_managerVtable[8];
void* g_obj_taskVtable[4];
void* g_obj_trackerVtable[2];
bool g_obj_forwardManager; // the manager's slots 1-2 run the run's own Register / Unregister after recording

using Fn1 = void(__thiscall*)(void*, void*);
template <typename F>
void* Code(uint32_t address, F ours)
{
    return g_obj_original ? reinterpret_cast<void*>(address) : reinterpret_cast<void*>(AddressOf(ours));
}

void InstallStandIns(StandIns& s)
{
    // --- the structure functions ---
    auto getter = [&](uint32_t address, uint32_t ours, char kind) {
        s.At({address, ours}, 12, [kind](Regs& r) {
            Rec(0x6E70000u + static_cast<uint8_t>(kind), {r.ecx, r.args[0], r.args[2]});
            uint32_t value;
            if (!Lookup(kind, r.ecx, r.args[0], value))
                return 0u;
            *reinterpret_cast<uint32_t*>(r.args[1]) = value;
            return 1u;
        });
    };
    getter(0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum)), 'K');
    getter(0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CArray**, int)>(&CStruct::GetArray)), 'A');
    getter(0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger)), 'I');
    getter(0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure)), 'S');
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6F1A, {r.ecx, r.args[0]});
        uint32_t value;
        return Lookup('F', r.ecx, r.args[0], value) ? value : 0u;
    });
    s.At({0x00476420, AddressOf(&CStruct::IsEmpty)}, 0, [](Regs& r) {
        Rec(0x6E37, {r.ecx});
        uint32_t value;
        return Lookup('E', r.ecx, 0, value) ? value : 1u;
    });
    s.At({0x00476A90, AddressOf(&CStruct::FindComponent)}, 4, [](Regs& r) {
        Rec(0x6F17, {r.ecx, r.args[0]});
        uint32_t value;
        return Lookup('C', r.ecx, r.args[0], value) ? value : 0u;
    });
    s.At({0x00476490, AddressOf(&CStruct::GetNextComponent)}, 4, [](Regs& r) {
        Rec(0x6E87, {r.ecx, r.args[0]});
        uint32_t value;
        return Lookup('N', r.ecx, r.args[0], value) ? value : 0u;
    });
    auto record = [&](std::initializer_list<uint32_t> addresses, uint16_t pop, uint32_t tag, int args, bool ecx,
                      uint32_t result) {
        s.At(addresses, pop, [=](Regs& r) {
            g_trace.push_back(tag);
            if (ecx)
                g_trace.push_back(Rel(r.ecx));
            for (int i = 0; i < args; ++i)
                g_trace.push_back(Rel(r.args[i]));
            return result == 0xEC ? r.ecx : result;
        });
    };
    record({0x004776B0, AddressOf(static_cast<void (CStruct::*)(uint32_t)>(&CStruct::RemoveComponent))}, 4, 0x6E01, 1, true, 0);
    record({0x00477860, AddressOf(&CStruct::RemoveFlag)}, 4, 0x6E02, 1, true, 0);
    record({0x00477130, AddressOf(&CStruct::Clear)}, 0, 0x6E03, 0, true, 0);
    record({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, 0x6E04, 1, false, 0);
    record({0x00477920, AddressOf(&CStruct::AddComponent)}, 4, 0x6E05, 1, true, 0);
    record({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, 0x6E06, 1, true, 0);
    record({0x004781B0, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructurePointer))}, 8, 0x6E07, 2, true, 0);
    record({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8, 0x6E08, 2, true, 0);
    record({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8, 0x6E09, 2, true, 0);
    record({0x00476410, AddressOf(&CStruct::Init)}, 0, 0x6E0A, 0, true, 0xEC);
    record({0x004773E0, AddressOf(&Script::CopyComponent)}, 0, 0x6E0B, 3, false, 0);
    // A component's constructor: the original's 0x0046D140 (this in ECX), ours ConstructComponent (cdecl).
    s.At({0x0046D140}, 0, [](Regs& r) {
        Rec(0x6E0C, {r.ecx});
        return r.ecx;
    });
    s.At({AddressOf(&Script::ConstructComponent)}, 0, [](Regs& r) {
        Rec(0x6E0C, {r.args[0]});
        return r.args[0];
    });
    s.At({0x0046CC30, AddressOf(&CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0x6EA0, {r.ecx, r.args[0]});
        return 0xE0000000u + ((Rel(r.ecx) & 0xFFF) << 8) + r.args[0];
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Rec(0x6EA1, {r.args[0]});
        return 31 + (r.args[0] & 7); // (bit numbers from 32 up too)
    });
    // --- the memory manager ---
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xA11, {r.args[0], r.args[1], r.args[2]});
        return g_obj_allocFail ? 0u : Bump(r.args[0], 0xCD);
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0xA15, {r.args[0]});
        return g_obj_allocFail ? 0u : Bump(r.args[0], 0);
    });
    record({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, 0xA13, 1, true, 0);
    record({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, 0xA14, 0, true, 0);
    record({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, 0xA16, 1, true, 0);
    // --- the CScript methods and the script manager ---
    s.At({0x004738E0, AddressOf(&CScript::Construct)}, 0, [](Regs& r) {
        Rec(0xC500, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, sizeof(CScript));
        return r.ecx;
    });
    record({0x00474420, AddressOf(&CScript::Destroy)}, 0, 0xC501, 0, true, 0);
    record({0x004499B0, AddressOf(static_cast<void (*)(void*)>(&CScript::operator delete))}, 0, 0xC502, 1, false, 0);
    record({0x004747A0, AddressOf(static_cast<void (CScript::*)(uint32_t, CStruct*, Obj::CObject*)>(&CScript::SetScript))},
           12, 0xC503, 3, true, 0);
    record({0x00472940, AddressOf(&CScript::GotScript)}, 0, 0xC504, 0, true, 1);
    record({0x00475750, AddressOf(&CScript::Interrupt)}, 8, 0xC505, 2, true, 5);
    s.At({0x00473800, AddressOf(&CScript::ReceiveEvent)}, 4, [](Regs& r) {
        Rec(0xC506, {r.ecx, r.args[0]});
        if (g_obj_killDuringEvent)
            *reinterpret_cast<uint32_t*>(g_obj_lastPointer) = 0; // the object went: the smart pointer is cleared
        return 1u;
    });
    record({0x00472FD0, AddressOf(&CScript::Block)}, 4, 0xC507, 1, true, 0);
    s.At({0x004734E0, AddressOf(&CScript::SetEventHandlers)}, 8, [](Regs& r) {
        Rec(0xC508, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    record({0x004750B0, AddressOf(&CScript::Update)}, 0, 0xC509, 0, true, 2);
    record({0x00472F90, AddressOf(&CScript::SetSpawnerFile)}, 4, 0xC50A, 1, true, 0);
    record({0x00472F00, AddressOf(&CScript::ClearOnExitScripts)}, 0, 0xC50B, 0, true, 0);
    record({0x00473690, AddressOf(&CScript::ClearEventHandler)}, 4, 0xC50C, 1, true, 0);
    record({0x00473BE0, AddressOf(&CScript::ObjectDestroyed)}, 4, 0xC50D, 1, true, 0);
    using SpawnFn = CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t, bool, bool, bool, bool);
    s.At({0x00475420, AddressOf(static_cast<SpawnFn>(&Script::SpawnScript))}, 0, [](Regs& r) {
        Rec(0xC5A0, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                     r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
        uint32_t script = Bump(sizeof(CScript), 0);
        return script;
    });
    s.At({0x00473420, AddressOf(&Script::SendSpawnScriptMessage)}, 0, [](Regs& r) {
        Rec(0xC5A1, {r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
        return 0u;
    });
    record({0x00474190, AddressOf(&Script::KillSpawnedScriptsWithObjectAndName)}, 0, 0xC5A2, 3, false, 0);
    record({0x004740D0, AddressOf(&Script::KillSpawnedScriptsWithObjectAndId)}, 0, 0xC5A3, 3, false, 0);
    record({0x004756F0, AddressOf(&Script::StopAllScriptsUsingThisObject)}, 0, 0xC5A4, 1, false, 0);
    record({0x00472E00, AddressOf(&Script::GetSpawnId)}, 0, 0xC5A5, 1, false, 0x5BA41D);
    // --- smart pointers (a pointer on the stack is recorded as 0x57AC: the runs' stacks differ) ---
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Rec(0x5300, {r.ecx, InArena(r.args[0]) ? r.args[0] : 0x57ACu});
        g_obj_lastPointer = r.args[0];
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Rec(0x5301, {r.ecx, InArena(r.args[0]) ? r.args[0] : 0x57ACu});
        return 0u;
    });
    // CRefCounted (wt/trk): constructor, destructor, pointers cleared.
    s.At({0x004637B0, AddressOf(&Obj::CRefCounted::Construct)}, 0, [](Regs& r) {
        Rec(0x5310, {r.ecx});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0x006478E4;
        reinterpret_cast<uint32_t*>(r.ecx)[1] = 0;
        return r.ecx;
    });
    record({0x00463870, AddressOf(&Obj::CRefCounted::Destruct)}, 0, 0x5311, 0, true, 0);
    record({0x00463820, AddressOf(&Obj::CRefCounted::ClearSmartPointers)}, 0, 0x5312, 0, true, 0);
    // --- the tracker (wt/trk) ---
    s.At({0x00462120, AddressOf(&Obj::CTracker::LookUpObject)}, 4, [](Regs& r) {
        Rec(0x7A00, {r.ecx, r.args[0]});
        return Abs(g_obj_found);
    });
    s.At({0x00462F00, AddressOf(&Obj::CTracker::LaunchEvent)}, 0x18, [](Regs& r) {
        Rec(0x7A01, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4] & 0xFF, r.args[5]});
        return 0u;
    });
    s.At({0x004621D0, AddressOf(&Obj::CTracker::GetUniqueId)}, 0, [](Regs& r) {
        Rec(0x7A02, {r.ecx});
        return g_obj_nextId++;
    });
    record({0x00462980, AddressOf(&Obj::CTracker::AddObject)}, 4, 0x7A03, 1, true, 0);
    s.At({0x00462A20, AddressOf(&Obj::CTracker::RemoveObject)}, 12, [](Regs& r) {
        Rec(0x7A04, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // --- tasks ---
    record({0x00406890, AddressOf(&Tsk::BaseTask::Construct)}, 4, 0x7A50, 1, true, 0xEC);
    record({0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x7A51, 1, true, 0);

    // --- virtual functions ---
    for (void*& slot : g_obj_objectVtable)
        slot = nullptr;
    g_obj_objectVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0x0B00, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_obj_objectVtable[1] = s.Thunk(4, [](Regs& r) {
        Rec(0x0B01, {r.ecx, r.args[0]});
        return 0u;
    });
    g_obj_objectVtable[6] = s.Thunk(0, [](Regs& r) {
        Rec(0x0B06, {r.ecx});
        return 0u;
    });
    for (void*& slot : g_obj_managerVtable)
        slot = nullptr;
    g_obj_managerVtable[1] = s.Thunk(4, [](Regs& r) {
        Rec(0x3A01, {r.ecx, r.args[0], reinterpret_cast<CBaseManager*>(r.ecx)->m_reassigning});
        if (g_obj_forwardManager)
            reinterpret_cast<Fn1>(Code(0x00461A00, &CBaseManager::RegisterObject))(reinterpret_cast<void*>(r.ecx),
                                                                                  reinterpret_cast<void*>(r.args[0]));
        return 0u;
    });
    g_obj_managerVtable[2] = s.Thunk(4, [](Regs& r) {
        Rec(0x3A02, {r.ecx, r.args[0]});
        if (g_obj_forwardManager)
            reinterpret_cast<Fn1>(Code(0x00461A50, &CBaseManager::UnregisterObject))(
                reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]));
        else
            reinterpret_cast<CObject*>(r.args[0])->mp_manager = nullptr; // (as unregistering does)
        return 0u;
    });
    g_obj_managerVtable[3] = s.Thunk(8, [](Regs& r) {
        Rec(0x3A03, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    g_obj_managerVtable[5] = s.Thunk(4, [](Regs& r) {
        Rec(0x3A05, {r.ecx, r.args[0]});
        return 0u;
    });
    for (void*& slot : g_obj_taskVtable)
        slot = nullptr;
    g_obj_taskVtable[2] = s.Thunk(4, [](Regs& r) {
        Rec(0x7A5D, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_obj_trackerVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0x7AD0, {r.ecx, r.args[0]});
        return r.ecx;
    });
}

void SaveGlobals()
{
    g_obj_saved = {
        {0x006F0FC0, 8, {}},  // the tracker and its references
        {0x007D1800, 4, {}},  // the memory manager
        {0x007CC979, 1, {}},  // the memory manager is ready
        {0x006F3614, 16, {}}, // the CStruct pools
        {0x006F2338, 16, {}}, // the component pools
        {0x006F3548, 16, {}}, // the CScript pools
        {0x00696D68, 4, {}},  // the main loop's task manager
    };
    SaveAll(g_obj_saved);
}

// One run's set-up: the arena (dirty from +0x8000, where the stand-in allocator hands out blocks), the globals.
uint8_t* BeginRun(bool original)
{
    g_obj_original = original;
    g_trace.clear();
    g_obj_answers.clear();
    uint8_t* arena = g_obj_arenas[original ? 0 : 1];
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + 0x8000, 0xDD, kArenaSize - 0x8000);
    g_obj_base = reinterpret_cast<uint32_t>(arena);
    g_obj_bump = g_obj_base + 0x8000;
    g_obj_allocFail = false;
    g_obj_killDuringEvent = false;
    g_obj_lastPointer = 0;
    g_obj_found = 0;
    g_obj_nextId = 0x1D000;
    g_obj_forwardManager = false;
    for (Saved& saved : g_obj_saved)
        std::memset(reinterpret_cast<void*>(saved.address), 0, saved.size);
    GameVariable<bool>(0x007CC979) = true;
    auto* manager = reinterpret_cast<Mem::Manager*>(arena + 0x6000);
    manager->m_scriptHeap = reinterpret_cast<Mem::Heap*>(0x5C1F0000);
    manager->m_frontEndHeap = reinterpret_cast<Mem::Heap*>(0xF0E70000);
    GameVariable<void*>(0x007D1800) = manager;
    GameVariable<uint32_t>(0x006F0FC0) = 0x7AC4E400;
    GameVariable<uint8_t*>(0x00696D68) = arena + 0x7000; // (its system task list at +0x7C)
    return arena;
}

// The whole arena, pointers arena-relative, our code pointers as the original's.
std::vector<uint32_t> EndRun(uint8_t* arena)
{
    std::vector<uint32_t> result = g_trace;
    std::vector<uint32_t> image(kArenaSize / 4);
    std::memcpy(image.data(), arena, kArenaSize);
    const uint32_t task = AddressOf(&Obj::FlushDeadObjectsTask);
    const uint32_t taskPointer = reinterpret_cast<uint32_t>(&Obj::FlushDeadObjectsTask);
    for (uint32_t& word : image) {
        if (word == task || word == taskPointer)
            word = 0x00461BB0;
        word = Rel(word);
    }
    result.insert(result.end(), image.begin(), image.end());
    return result;
}

// Calls: the original at `address`, or ours.
template <typename R, typename F, typename... A>
R Call(uint32_t address, F ours, void* self, A... args)
{
    void* code = g_obj_original ? reinterpret_cast<void*>(address) : reinterpret_cast<void*>(AddressOf(ours));
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(code)(self, args...);
}
template <typename R, typename F, typename... A>
R CallC(uint32_t address, F ours, A... args)
{
    void* code = g_obj_original ? reinterpret_cast<void*>(address) : reinterpret_cast<void*>(AddressOf(ours));
    return reinterpret_cast<R(__cdecl*)(A...)>(code)(args...);
}
void Result(uint32_t value) { Rec(0x2E5, {value}); }

// A stand-in object in the arena: vtable, its node linked to itself, the fields set.
CObject* MakeObject(uint8_t* arena, int i, uint32_t id, uint32_t type, int priority)
{
    auto* object = reinterpret_cast<CObject*>(arena + 0x100 + 0x60 * i);
    object->m_vtable = g_obj_objectVtable;
    object->mp_smartPointers = nullptr;
    object->m_objectFlags = 0;
    object->mp_manager = nullptr;
    object->m_script = nullptr;
    object->m_id = id;
    object->m_type = type;
    object->m_visitedMask = 0;
    object->mp_tags = nullptr;
    object->m_flags = 0x00F0F001u;
    object->m_node.vtable = reinterpret_cast<void**>(0x006477E4);
    object->m_node.data = object;
    object->m_node.priority = priority;
    object->m_node.next = &object->m_node;
    object->m_node.prev = &object->m_node;
    object->m_aliasCount = 0;
    object->m_ref.m_vtable = reinterpret_cast<void**>(0x006477E0);
    object->m_ref.mp_smartPointers = nullptr;
    object->m_ref.mp_object = object;
    return object;
}
CStruct* Struct(uint8_t* arena, int i) { return reinterpret_cast<CStruct*>(arena + 0x1800 + 0x10 * i); }
CArray* Array(uint8_t* arena, int i, uint32_t size)
{
    auto* array = reinterpret_cast<CArray*>(arena + 0x1C00 + 0x10 * i);
    array->m_size = size;
    return array;
}
CScript* Script(uint8_t* arena, int i) { return reinterpret_cast<CScript*>(arena + 0x2000 + 0x100 * i); }

constexpr uint32_t kName = 0xA1DC81F9, kId = 0x40C698AF, kStructure = 0x90FEC815, kTags = 0x90436BD9,
                   kRemoveTags = 0x8225D629, kEventHandlers = 0x475BF03C, kReplaceHandlers = 0x3F93A1EF,
                   kParams = 0x7031F10C, kPauseWithObject = 0x8757D0BB, kAll = 0xC4E78E22, kClear = 0x1A4E0EF9,
                   kNoSpawnerFile = 0x01917DF2;

using P = CStruct*;
using S = CScript*;

// --- CObject: construction, id / type / tags, flags, the member functions, the vtable functions ---
std::vector<uint32_t> RunObjectTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    auto* manager = reinterpret_cast<CBaseManager*>(arena + 0x3000);
    manager->m_vtable = g_obj_managerVtable;

    // Construct over dirty memory.
    auto* a = reinterpret_cast<CObject*>(arena + 0x100);
    std::memset(a, 0xCD, 0x60);
    Result(Rel(Call<CObject*>(0x00460B80, &CObject::Construct, a)));
    Result(a->m_id);
    Result(a->m_type);
    a->m_vtable = g_obj_objectVtable;

    // SetID: no id yet; an id but no manager; an id and a manager. SetType.
    Call<void>(0x00461930, &CObject::SetID, a, 0x1111u);
    Call<void>(0x00461930, &CObject::SetID, a, 0x2222u);
    a->mp_manager = manager;
    Call<void>(0x00461930, &CObject::SetID, a, 0x3333u);
    a->m_id = Obj::kUnassignedId;
    Call<void>(0x00461930, &CObject::SetID, a, 0x4444u);
    Call<void>(0x00461970, &CObject::SetType, a, 0x7E7Eu);
    CStruct* tags = a->mp_tags;
    Call<void>(0x004611E0, &CObject::CreateTagsIfNeeded, a);
    Call<void>(0x004616B0, &CObject::SetIntegerTag, a, 0x11u, -5);
    Call<void>(0x004616D0, &CObject::SetChecksumTag, a, 0x12u, 0xC4C4u);
    Call<void>(0x004616F0, &CObject::SetTagsFromStruct, a, Struct(arena, 1));
    // Tags made by each setter (none yet).
    CObject* b = MakeObject(arena, 1, 0xB, 1, 0);
    Call<void>(0x004616B0, &CObject::SetIntegerTag, b, 0x21u, 7);
    b->mp_tags = nullptr;
    Call<void>(0x004616D0, &CObject::SetChecksumTag, b, 0x22u, 8u);
    b->mp_tags = nullptr;
    Call<void>(0x004616F0, &CObject::SetTagsFromStruct, b, Struct(arena, 2));
    b->mp_tags = nullptr;
    Call<void>(0x00461970, &CObject::SetType, b, 0x31u);
    // Allocation failure: no tags (the setter crashes in both: not called).
    b->mp_tags = nullptr;
    g_obj_allocFail = true;
    Call<void>(0x004611E0, &CObject::CreateTagsIfNeeded, b);
    g_obj_allocFail = false;
    Result(Rel(b->mp_tags));

    // GetTag: no tags; found; no unnamed checksum (the pointer is the name); no such tag.
    P params = Struct(arena, 3), dest = Struct(arena, 4);
    b->mp_tags = nullptr;
    Result(Call<bool>(0x00460CA0, &CObject::GetTag, b, params, dest));
    Answer('K', params, 0, 0x55u);
    Answer('C', tags, 0x55, arena + 0x1F00);
    Result(Call<bool>(0x00460CA0, &CObject::GetTag, a, params, dest));
    P params2 = Struct(arena, 5);
    Answer('C', tags, Rel(params2), arena + 0x1F10);
    Result(Call<bool>(0x00460CA0, &CObject::GetTag, a, params2, dest));
    P params3 = Struct(arena, 6);
    Answer('K', params3, 0, 0x56u);
    Result(Call<bool>(0x00460CA0, &CObject::GetTag, a, params3, dest));
    // GetTags: no tags; three names (the middle one found), the last first; none.
    CArray* names3 = Array(arena, 0, 3);
    Result(Call<bool>(0x00460D90, &CObject::GetTags, b, names3, dest));
    Answer('C', tags, 0xE0000000u + ((Rel(names3) & 0xFFF) << 8) + 1, arena + 0x1F20);
    Result(Call<bool>(0x00460D90, &CObject::GetTags, a, names3, dest));
    Result(Call<bool>(0x00460D90, &CObject::GetTags, a, Array(arena, 1, 0), dest));

    // GetFlagMask: an array (bit numbers from the names' global integers); an empty array; an integer; nothing.
    P flagsArray = Struct(arena, 7), flagsEmpty = Struct(arena, 8), flagsInt = Struct(arena, 9),
      flagsNone = Struct(arena, 10), flagsSmall = Struct(arena, 11);
    Answer('A', flagsArray, 0, Array(arena, 2, 3));
    Answer('A', flagsEmpty, 0, Array(arena, 3, 0));
    Answer('I', flagsEmpty, 0, 4u);
    Answer('I', flagsInt, 0, 35u);
    Answer('I', flagsSmall, 0, 3u);
    for (P p : {flagsArray, flagsEmpty, flagsInt, flagsNone, flagsSmall})
        Result(Call<uint32_t>(0x00460960, &CObject::GetFlagMask, a, p, Script(arena, 0)));

    // The flag member functions.
    S caller = Script(arena, 0);
    caller->m_params = Struct(arena, 12);
    auto flags = [&](uint32_t address, auto ours, P p) {
        Result(Call<bool>(address, ours, a, p, caller));
        Result(a->m_flags);
    };
    a->m_flags = 0x00F0F001u;
    flags(0x004610C0, &CObject::Obj_SetFlag, flagsNone);
    flags(0x004610C0, &CObject::Obj_SetFlag, flagsInt);
    flags(0x00461040, &CObject::Obj_FlagNotSet, flagsNone);
    flags(0x00461040, &CObject::Obj_FlagNotSet, flagsInt);
    flags(0x00461040, &CObject::Obj_FlagNotSet, flagsSmall);
    flags(0x004610F0, &CObject::Obj_FlagSet, flagsNone);
    flags(0x004610F0, &CObject::Obj_FlagSet, flagsSmall);
    flags(0x004610F0, &CObject::Obj_FlagSet, flagsInt);
    Answer('F', flagsInt, kClear, 1u);
    flags(0x004610F0, &CObject::Obj_FlagSet, flagsInt);
    flags(0x004610F0, &CObject::Obj_FlagSet, flagsInt);
    flags(0x00461040, &CObject::Obj_FlagNotSet, flagsArray);
    flags(0x004610F0, &CObject::Obj_FlagSet, flagsArray);
    flags(0x004610C0, &CObject::Obj_SetFlag, flagsArray);
    flags(0x00461070, &CObject::Obj_ClearFlag, flagsNone);
    flags(0x00461070, &CObject::Obj_ClearFlag, flagsSmall);
    Answer('F', flagsSmall, kAll, 1u);
    flags(0x00461070, &CObject::Obj_ClearFlag, flagsSmall);
    // Visible / Invisible, GetId.
    a->m_objectFlags = 0x3;
    Result(Call<bool>(0x004608E0, &CObject::Obj_Invisible, a, params, caller));
    Result(a->m_objectFlags);
    Result(Call<bool>(0x004608D0, &CObject::Obj_Visible, a, params, caller));
    Result(a->m_objectFlags);
    Result(Call<bool>(0x00460940, &CObject::Obj_GetId, a, params, caller));

    // GetTags (structure= / not, with tags / not), GetSingleTag, the named tags (an array / none), SetTags.
    P gt = Struct(arena, 13);
    Answer('K', gt, kStructure, 0x5757u);
    Result(Call<bool>(0x00460EB0, &CObject::Obj_GetTags, a, gt, caller));
    Result(Call<bool>(0x00460EB0, &CObject::Obj_GetTags, b, gt, caller));
    Result(Call<bool>(0x00460EB0, &CObject::Obj_GetTags, a, params, caller));
    Result(Call<bool>(0x00460EB0, &CObject::Obj_GetTags, b, params, caller));
    Result(Call<bool>(0x00460FB0, &CObject::Obj_GetSingleTag, a, params, caller));
    P named = Struct(arena, 14);
    Answer('A', named, 0, Array(arena, 4, 2));
    Result(Call<bool>(0x00460FC0, &CObject::Obj_GetNamedTags, a, named, caller));
    P notNamed = Struct(arena, 15);
    reinterpret_cast<uint32_t*>(notNamed)[1] = 2; // (taken for an array: its size)
    Result(Call<bool>(0x00460FC0, &CObject::Obj_GetNamedTags, a, notNamed, caller));
    Result(Call<bool>(0x004617C0, &CObject::Obj_SetTags, a, params, caller));
    Result(Call<bool>(0x004617C0, &CObject::Obj_SetTags, b, params, caller));
    b->mp_tags = nullptr;

    // The small tag functions: with tags (found / not), without.
    Answer('I', tags, 0x71, 0x17u);
    Answer('K', tags, 0x72, 0x27u);
    Answer('F', tags, 0x73, 1u);
    for (CObject* o : {a, b}) {
        uint32_t value = 0xBAD;
        for (uint32_t name : {0x71u, 0x72u, 0x73u}) {
            value = 0xBAD;
            Result(Call<bool>(0x004607C0, &CObject::GetIntegerTag, o, name, reinterpret_cast<int*>(&value)));
            Result(value);
            value = 0xBAD;
            Result(Call<bool>(0x004607E0, &CObject::GetChecksumTag, o, name, &value));
            Result(value);
            Result(Call<bool>(0x00460800, &CObject::ContainsTagFlag, o, name));
        }
        Call<void>(0x004607B0, &CObject::RemoveTagFlag, o, 0x74u);
        Call<void>(0x00460820, &CObject::CopyTagsTo, o, dest);
    }

    // RemoveTags: no tags; two names, tags left; two names, none left; an empty array, none left; no array.
    Call<void>(0x00460C20, &CObject::RemoveTags, b, Array(arena, 5, 2));
    Answer('E', tags, 0, 0u);
    Call<void>(0x00460C20, &CObject::RemoveTags, a, Array(arena, 5, 2));
    Result(Rel(a->mp_tags));
    Answer('E', tags, 0, 1u);
    Call<void>(0x00460C20, &CObject::RemoveTags, a, Array(arena, 6, 2));
    Result(Rel(a->mp_tags));
    a->mp_tags = tags;
    Call<void>(0x00460C20, &CObject::RemoveTags, a, Array(arena, 7, 0));
    Result(Rel(a->mp_tags));
    a->mp_tags = tags;
    Answer('E', tags, 0, 0u);
    Call<void>(0x00460C20, &CObject::RemoveTags, a, static_cast<CArray*>(nullptr));
    Result(Rel(a->mp_tags));
    // Obj_RemoveTags: tags=; the unnamed array; neither (every tag).
    P rt1 = Struct(arena, 16), rt2 = Struct(arena, 17), rt3 = Struct(arena, 18);
    Answer('A', rt1, kTags, Array(arena, 8, 1));
    Answer('A', rt1, 0, Array(arena, 9, 1));
    Answer('A', rt2, 0, Array(arena, 10, 1));
    for (P p : {rt1, rt2, rt3}) {
        a->mp_tags = tags;
        Result(Call<bool>(0x00460FF0, &CObject::Obj_RemoveTags, a, p, caller));
        Result(Rel(a->mp_tags));
    }

    // KillSpawnedScript: by name; by id; neither.
    P ks1 = Struct(arena, 19), ks2 = Struct(arena, 20), ks3 = Struct(arena, 21);
    Answer('K', ks1, kName, 0xAA01u);
    Answer('K', ks1, kId, 0xAA02u);
    Answer('K', ks2, kId, 0xAA03u);
    for (P p : {ks1, ks2, ks3})
        Result(Call<bool>(0x00460850, &CObject::Obj_KillSpawnedScript, a, p, caller));

    // SetProperties: everything (no script yet: one is made); nothing; replace_handlers only with handlers.
    P props = Struct(arena, 22);
    Answer('F', props, kReplaceHandlers, 1u);
    Answer('A', props, kEventHandlers, Array(arena, 11, 1));
    Answer('S', props, kTags, Struct(arena, 23));
    Answer('A', props, kRemoveTags, Array(arena, 12, 1));
    a->mp_tags = tags;
    Call<void>(0x00461710, &CObject::SetProperties, a, props);
    Result(Rel(a->m_script));
    Call<void>(0x00461710, &CObject::SetProperties, a, Struct(arena, 24));
    P props2 = Struct(arena, 25);
    Answer('A', props2, kEventHandlers, Array(arena, 13, 1));
    Call<void>(0x00461710, &CObject::SetProperties, a, props2);
    Answer('S', props2, kTags, Struct(arena, 26));
    Call<void>(0x00461710, &CObject::SetProperties, b, props2);

    // CallMemberFunction: SetProps, Die, another name.
    for (uint32_t name : {0x6C63C7C5u, 0xC6870028u, 0x12345678u})
        Result(Call<bool>(0x004608F0,
                          static_cast<bool (CObject::*)(uint32_t, CStruct*, CScript*)>(&CObject::CallMemberFunction),
                          a, name, params, caller));

    // Unlock, DestroyIfUnlocked (free and member), FindObject, the events.
    b->m_objectFlags = 0x7;
    CallC<void>(0x00460AC0, &Obj::Unlock, b, reinterpret_cast<void*>(0x1234));
    Result(b->m_objectFlags);
    CallC<void>(0x00460AA0, &Obj::DestroyIfUnlocked, b, static_cast<void*>(nullptr));
    b->m_objectFlags = 0x2;
    CallC<void>(0x00460AA0, &Obj::DestroyIfUnlocked, b, static_cast<void*>(nullptr));
    Call<void>(0x00460930, &CObject::DestroyIfUnlocked, b);
    b->m_objectFlags = 0x5;
    Call<void>(0x00460930, &CObject::DestroyIfUnlocked, b);
    g_obj_found = Rel(b);
    Result(Rel(CallC<CObject*>(0x00460AD0, &Obj::FindObject, 0xF1F1u)));
    Call<void>(0x004609F0, &CObject::SelfEvent, a, 0xE7E7u, params);
    Call<void>(0x00460A20, &CObject::BroadcastEvent, a, 0xE8E8u, params, 5.0f);

    // GetComponent: a composite object's list (types 1, 2, 3): found first / later / not.
    auto* composite = reinterpret_cast<Obj::CCompositeObject*>(arena + 0x4000);
    auto* components = reinterpret_cast<uint32_t*>(arena + 0x4200);
    composite->mp_components = reinterpret_cast<Obj::CBaseComponent*>(components);
    for (int i = 0; i < 3; ++i) {
        components[i * 8 + 1] = static_cast<uint32_t>(i + 1);
        components[i * 8 + 4] = i < 2 ? reinterpret_cast<uint32_t>(components + (i + 1) * 8) : 0;
    }
    for (uint32_t type : {1u, 3u, 4u})
        Result(Rel(Call<Obj::CBaseComponent*>(0x0045DB90, &Obj::CCompositeObject::GetComponent, composite, type)));
    composite->mp_components = nullptr;
    Result(Rel(Call<Obj::CBaseComponent*>(0x0045DB90, &Obj::CCompositeObject::GetComponent, composite, 1u)));

    // The second smart-pointer target and the node: destructors.
    CObject* c = MakeObject(arena, 2, 0xC, 1, 0);
    Call<void>(0x00460920, &Obj::CObjectRef::DestroyBody, &c->m_ref);
    Result(Rel(Call<Obj::CObjectRef*>(0x00460AF0, &Obj::CObjectRef::Destroy, &c->m_ref, 0u)));
    Result(Rel(Call<Obj::CObjectRef*>(0x00460AF0, &Obj::CObjectRef::Destroy, &c->m_ref, 1u)));
    CObject* d = MakeObject(arena, 3, 0xD, 1, 0);
    CObject* e = MakeObject(arena, 4, 0xE, 1, 0);
    // (d and e in a ring with a head at +0x4400)
    auto* head = reinterpret_cast<Mem::ListNode*>(arena + 0x4400);
    *head = {reinterpret_cast<void**>(0x00647814), reinterpret_cast<void*>(-1), 0, &d->m_node, &e->m_node};
    d->m_node.prev = head;
    d->m_node.next = &e->m_node;
    e->m_node.prev = &d->m_node;
    e->m_node.next = head;
    Call<void>(0x00460B20, &Obj::CObjectNode::DestroyBody, &d->m_node);
    Result(Rel(Call<Obj::CObjectNode*>(0x00460B40, &Obj::CObjectNode::Destroy, &e->m_node, 3u)));
    return EndRun(arena);
}

// --- CObject: its script, spawning, events, dying and being destroyed ---
std::vector<uint32_t> RunObjectScriptTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    auto* manager = reinterpret_cast<CBaseManager*>(arena + 0x3000);
    manager->m_vtable = g_obj_managerVtable;
    S caller = Script(arena, 0);
    caller->m_params = Struct(arena, 1);
    P params = Struct(arena, 2);

    CObject* a = MakeObject(arena, 0, 0xA, 1, 0);
    // CreateScriptIfNeeded (made; there already); SwitchScript; CallScript.
    Call<void>(0x00461290, &CObject::CreateScriptIfNeeded, a);
    Call<void>(0x00461290, &CObject::CreateScriptIfNeeded, a);
    Result(Rel(Call<S>(0x00461350, &CObject::SwitchScript, a, 0x5E5Eu, params)));
    CObject* b = MakeObject(arena, 1, 0xB, 1, 0);
    Result(Rel(Call<S>(0x00461350, &CObject::SwitchScript, b, 0x5F5Fu, params)));
    CObject* c = MakeObject(arena, 2, 0xC, 1, 0);
    Result(Call<uint32_t>(0x00461500, &CObject::CallScript, c, 0xCA11u, params));
    Result(Rel(c->m_script));
    Result(Call<uint32_t>(0x00461500, &CObject::CallScript, c, 0xCA12u, params));

    // Spawning.
    Result(Rel(Call<S>(0x00461410, &CObject::SpawnScript, a, 0x5A01u, params, 0x1D1Du, true)));
    Result(Rel(Call<S>(0x00461410, &CObject::SpawnScript, a, 0x5A02u, params, 0x1D1Eu, false)));
    Result(Rel(Call<S>(0x00461480, &CObject::SpawnAndRunScript, a, 0x5A03u, 7, true, true, params)));
    Result(Rel(Call<S>(0x00461480, &CObject::SpawnAndRunScript, a, 0x5A04u, -1, false, false, params)));
    // Obj_SpawnScript: no components; a first component that isn't a name; a name with id=, params=,
    // PauseWithObject and the spawner-file flag; a bare name.
    P sp1 = Struct(arena, 3), sp2 = Struct(arena, 4), sp3 = Struct(arena, 5), sp4 = Struct(arena, 6);
    auto* comps = reinterpret_cast<CComponent*>(arena + 0x1E00);
    comps[0].typeAndFlag = 0x1B; // a name, with the flag bit
    comps[0].checksum = 0x5A05;
    comps[1].typeAndFlag = 0x02; // an integer
    comps[1].checksum = 0x5A06;
    comps[2].typeAndFlag = 0x1A;
    comps[2].checksum = 0x5A07;
    Answer('N', sp2, 0, &comps[1]);
    Answer('N', sp3, 0, &comps[0]);
    Answer('K', sp3, kId, 0x1D1Fu);
    Answer('S', sp3, kParams, Struct(arena, 7));
    Answer('F', sp3, kPauseWithObject, 1u);
    Answer('F', sp3, kNoSpawnerFile, 1u);
    Answer('N', sp4, 0, &comps[2]);
    for (P p : {sp1, sp2, sp3, sp4})
        Result(Call<bool>(0x004617E0, &CObject::Obj_SpawnScript, a, p, caller));
    // Obj_SwitchScript: a name and params=; a name; none.
    P sw1 = Struct(arena, 8), sw2 = Struct(arena, 9), sw3 = Struct(arena, 10);
    Answer('K', sw1, 0, 0x5B01u);
    Answer('S', sw1, kParams, Struct(arena, 11));
    Answer('K', sw2, 0, 0x5B02u);
    for (P p : {sw1, sw2, sw3})
        Result(Call<bool>(0x004618A0, &CObject::Obj_SwitchScript, a, p, caller));

    // ClearEventHandler (a script / none), Obj_LeaveScript.
    Call<void>(0x004607A0, &CObject::ClearEventHandler, a, 0xE1E1u);
    Call<void>(0x004607A0, &CObject::ClearEventHandler, MakeObject(arena, 3, 0xD, 1, 0), 0xE2E2u);
    Result(Call<bool>(0x00460840, &CObject::Obj_LeaveScript, a, params, caller));

    // PassEventToScript: no script; a script; a script whose object goes during the event.
    CObject* d = MakeObject(arena, 3, 0xD, 1, 0);
    auto* event = reinterpret_cast<Obj::CEvent*>(arena + 0x1F00);
    Result(Call<bool>(0x00461140, &CObject::PassEventToScript, d, event));
    Result(Call<bool>(0x00461140, &CObject::PassEventToScript, a, event));
    g_obj_killDuringEvent = true;
    Result(Call<bool>(0x00461140, &CObject::PassEventToScript, a, event));
    g_obj_killDuringEvent = false;

    // MarkAsDead: with a script (and the manager forgetting itself on unregister); without.
    a->mp_manager = manager;
    a->m_objectFlags = 0x6;
    Call<void>(0x00460A50, &CObject::MarkAsDead, a);
    Result(a->m_objectFlags);
    Result(Rel(a->mp_manager));
    d->mp_manager = manager;
    Call<void>(0x00460A50, &CObject::MarkAsDead, d);
    Result(d->m_objectFlags);

    // Destroyed: with tags, a manager and a script; with nothing; the deleting destructor (keep / free).
    CObject* e = MakeObject(arena, 4, 0xE, 1, 0);
    e->mp_tags = Struct(arena, 12);
    e->mp_manager = manager;
    e->m_script = Script(arena, 1);
    Call<void>(0x004615D0, &CObject::DestroyBody, e);
    Result(Rel(e->mp_tags));
    Result(Rel(e->m_script));
    CObject* f = MakeObject(arena, 5, 0xF, 1, 0);
    Call<void>(0x004615D0, &CObject::DestroyBody, f);
    CObject* g = MakeObject(arena, 6, 0x10, 1, 0);
    Result(Rel(Call<CObject*>(0x00461900, &CObject::Destroy, g, 0u)));
    CObject* h = MakeObject(arena, 7, 0x11, 1, 0);
    Result(Rel(Call<CObject*>(0x00461900, &CObject::Destroy, h, 1u)));
    return EndRun(arena);
}

// --- the managers ---
// The walk's test callback: records the object; on chosen objects it changes the list or walks again.
CBaseManager* g_obj_walkManager;
CObject* g_obj_walkAdd;    // registered when the walk reaches g_obj_walkTrigger
CObject* g_obj_walkRemove; // unregistered then
CObject* g_obj_walkTrigger;
CObject* g_obj_walkNested; // a nested walk starts here
CObject* g_obj_walkUnlinkAt;   // here the next object's node is taken out by hand (no restart)
CObject* g_obj_walkUnlinkNext;
void __cdecl WalkInner(CObject* object, void* data)
{
    Rec(0x3A1, {reinterpret_cast<uint32_t>(object), reinterpret_cast<uint32_t>(data), object->m_visitedMask});
}
void __cdecl Walk(CObject* object, void* data)
{
    Rec(0x3A0, {reinterpret_cast<uint32_t>(object), reinterpret_cast<uint32_t>(data), object->m_visitedMask,
                g_obj_walkManager->m_walking, g_obj_walkManager->m_restartWalks});
    if (object == g_obj_walkTrigger) {
        g_obj_walkTrigger = nullptr;
        if (g_obj_walkRemove)
            g_obj_walkManager->VirtualUnregisterObject(g_obj_walkRemove);
        if (g_obj_walkAdd)
            g_obj_walkManager->VirtualRegisterObject(g_obj_walkAdd);
    }
    if (object == g_obj_walkUnlinkAt) {
        g_obj_walkUnlinkAt = nullptr;
        Mem::ListNode* node = &g_obj_walkUnlinkNext->m_node;
        node->prev->next = node->next;
        node->next->prev = node->prev; // (its own links kept: the walk goes on from it)
    }
    if (object == g_obj_walkNested) {
        g_obj_walkNested = nullptr;
        Call<void>(0x00461CC0, &CBaseManager::ForEach, g_obj_walkManager, &WalkInner, reinterpret_cast<void*>(0x77));
    }
}

std::vector<uint32_t> RunManagerTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    g_obj_forwardManager = true;
    g_obj_walkAdd = g_obj_walkRemove = g_obj_walkTrigger = g_obj_walkNested = nullptr;
    g_obj_walkUnlinkAt = g_obj_walkUnlinkNext = nullptr;
    // Construct over dirty memory: the base manager, the general one (and without a task).
    auto* base = reinterpret_cast<CBaseManager*>(arena + 0x3000);
    std::memset(base, 0xCD, 0x40);
    Result(Rel(Call<CBaseManager*>(0x00461BC0, &CBaseManager::Construct, base)));
    auto* general = reinterpret_cast<CGeneralManager*>(arena + 0x3100);
    std::memset(general, 0xCD, 0x40);
    Result(Rel(Call<CGeneralManager*>(0x00461BF0, &CGeneralManager::Construct, general)));
    auto* general2 = reinterpret_cast<CGeneralManager*>(arena + 0x3200);
    std::memset(general2, 0xCD, 0x40);
    g_obj_allocFail = true;
    Result(Rel(Call<CGeneralManager*>(0x00461BF0, &CGeneralManager::Construct, general2)));
    g_obj_allocFail = false;
    general->m_vtable = g_obj_managerVtable;
    general2->m_vtable = g_obj_managerVtable;
    general->m_newId = 0x9999;

    // Register objects (priorities 5, 9, 5, 1, 9; two without an id).
    CObject* objects[8];
    const int priorities[8] = {5, 9, 5, 1, 9, 3, 7, 2};
    for (int i = 0; i < 8; ++i)
        objects[i] = MakeObject(arena, i, i == 1 || i == 6 ? Obj::kUnassignedId : 0x100u + i, i % 3, priorities[i]);
    for (int i = 0; i < 5; ++i)
        Call<void>(0x00461A00, &CBaseManager::RegisterObject, general, objects[i]);
    // Reassign an id, move one by priority, look one up.
    Call<void>(0x004619A0, &CBaseManager::ReassignObjectId, general, objects[2], 0x202u);
    Call<void>(0x004619D0, &CBaseManager::SetObjectPriority, general, objects[3], 8);
    g_obj_found = Rel(objects[4]);
    Result(Rel(Call<CObject*>(0x004619F0, &CBaseManager::GetObjectByID, general, 0x104u)));
    // Unregister one that isn't in a list.
    Call<void>(0x00461A50, &CBaseManager::UnregisterObject, general, objects[7]);

    // Walks: plain; changing the list on the way (restart); nested; on an empty list.
    g_obj_walkManager = general;
    Call<void>(0x00461CC0, &CBaseManager::ForEach, general, &Walk, reinterpret_cast<void*>(0x11));
    g_obj_walkTrigger = objects[2];
    g_obj_walkRemove = objects[4];
    g_obj_walkAdd = objects[5];
    g_obj_walkNested = objects[0];
    Call<void>(0x00461CC0, &CBaseManager::ForEach, general, &Walk, reinterpret_cast<void*>(0x22));
    general->m_walking = 0x5; // (walks in progress elsewhere: this one gets bit 1)
    g_obj_walkTrigger = objects[1];
    g_obj_walkRemove = nullptr;
    g_obj_walkAdd = objects[6];
    Call<void>(0x00461CC0, &CBaseManager::ForEach, general, &Walk, reinterpret_cast<void*>(0x33));
    Result(general->m_walking);
    Call<void>(0x00461CC0, &CBaseManager::ForEach, base, &Walk, reinterpret_cast<void*>(0x44));
    // The next object taken out by hand during the callback: it was fetched before, so it's still visited.
    CObject* firstObject = static_cast<CObject*>(general->m_objects.next->data);
    g_obj_walkUnlinkAt = firstObject;
    g_obj_walkUnlinkNext = static_cast<CObject*>(general->m_objects.next->next->data);
    Call<void>(0x00461CC0, &CBaseManager::ForEach, general, &Walk, reinterpret_cast<void*>(0x55));
    Call<void>(0x00461A00, &CBaseManager::RegisterObject, general, g_obj_walkUnlinkNext);

    // Searches by type (0, 1, 2; one with no objects of it), and a node without an object ending one.
    Obj::CObjectSearch search = {};
    auto* listHead = static_cast<Mem::ListNode*>(&general->m_objects);
    for (uint32_t type : {0u, 1u, 2u, 7u}) {
        CObject* found = Call<CObject*>(0x00461F30, &Obj::CObjectSearch::FirstItemOfType, &search, listHead, type);
        Result(Rel(found));
        for (int guard = 0; found && guard < 10; ++guard) {
            found = Call<CObject*>(0x00461ED0, &Obj::CObjectSearch::NextItemOfType, &search);
            Result(Rel(found));
        }
        Result(Rel(search.m_node));
    }
    objects[6]->m_node.data = nullptr;
    for (uint32_t type : {0u, 2u}) {
        CObject* found = Call<CObject*>(0x00461F30, &Obj::CObjectSearch::FirstItemOfType, &search, listHead, type);
        Result(Rel(found));
        for (int guard = 0; found && guard < 10; ++guard) {
            found = Call<CObject*>(0x00461ED0, &Obj::CObjectSearch::NextItemOfType, &search);
            Result(Rel(found));
        }
    }
    objects[6]->m_node.data = objects[6];

    // Dead objects: none to flush; two killed (the second goes first), flushed by the task.
    Call<void>(0x00461AB0, &CGeneralManager::FlushDeadObjects, general);
    Call<void>(0x00461A50, &CBaseManager::UnregisterObject, general, objects[0]);
    Call<void>(0x00461B00, &CGeneralManager::KillObject, general, objects[0]);
    Call<void>(0x00461A50, &CBaseManager::UnregisterObject, general, objects[3]);
    Call<void>(0x00461B00, &CGeneralManager::KillObject, general, objects[3]);
    uint32_t task[8] = {};
    task[7] = reinterpret_cast<uint32_t>(general);
    CallC<void>(0x00461BB0, &Obj::FlushDeadObjectsTask, static_cast<void*>(task));
    Result(Rel(general->m_deadObjects.next));
    // A node without an object is skipped.
    Call<void>(0x00461B00, &CGeneralManager::KillObject, general, objects[7]);
    objects[7]->m_node.data = nullptr;
    Call<void>(0x00461AB0, &CGeneralManager::FlushDeadObjects, general);
    objects[7]->m_node.data = objects[7];

    // Destruction: the general manager (unlocks, deletes the unlocked: all; its task); the base manager; a
    // list head on its own.
    objects[1]->m_objectFlags = 0x2;
    general->m_flushTask = arena + 0x5000;
    *reinterpret_cast<void***>(arena + 0x5000) = g_obj_taskVtable;
    Result(Rel(Call<CGeneralManager*>(0x00461EA0, &CGeneralManager::Destroy, general, 0u)));
    Call<void>(0x00461DF0, &CGeneralManager::DestroyBody, general2);
    Result(Rel(Call<CBaseManager*>(0x00461B80, &CBaseManager::Destroy, base, 1u)));
    Call<void>(0x00461B50, &CBaseManager::DestroyBody, general2);
    auto* lone = reinterpret_cast<Obj::CObjectListHead*>(arena + 0x5100);
    lone->vtable = reinterpret_cast<void**>(0x00647814);
    lone->data = reinterpret_cast<void*>(-1);
    lone->priority = 0;
    lone->next = &objects[7]->m_node;
    lone->prev = &objects[7]->m_node;
    objects[7]->m_node.next = lone;
    objects[7]->m_node.prev = lone;
    Result(Rel(Call<Obj::CObjectListHead*>(0x00461B20, &Obj::CObjectListHead::Destroy, lone, 1u)));

    // The tracker's references: dropped; the last (deleted); none.
    auto* tracker = reinterpret_cast<uint32_t*>(arena + 0x5200);
    tracker[0] = reinterpret_cast<uint32_t>(g_obj_trackerVtable);
    for (uint32_t references : {3u, 1u, 0u}) {
        GameVariable<uint32_t>(0x006F0FC0) = references ? reinterpret_cast<uint32_t>(tracker) : 0;
        GameVariable<uint32_t>(0x006F0FC4) = references;
        CallC<void>(0x00461FA0, &Obj::ReleaseTracker);
        Result(Rel(GameVariable<uint32_t>(0x006F0FC0)));
        Result(GameVariable<uint32_t>(0x006F0FC4));
    }
    // An event listener registered (twice: the list).
    auto* listeners = reinterpret_cast<Obj::CTracker*>(arena + 0x5300);
    listeners->mp_eventListeners = reinterpret_cast<Obj::CEventListener*>(arena + 0x5400);
    auto* listener1 = reinterpret_cast<Obj::CEventListener*>(arena + 0x5500);
    auto* listener2 = reinterpret_cast<Obj::CEventListener*>(arena + 0x5520);
    Call<void>(0x00461FE0, &Obj::CTracker::RegisterEventListener, listeners, listener1);
    Call<void>(0x00461FE0, &Obj::CTracker::RegisterEventListener, listeners, listener2);
    return EndRun(arena);
}

void RunPair(std::vector<uint32_t> (*run)(bool), const char* what)
{
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

void TestObjects()
{
    SaveGlobals();
    auto* s = new StandIns;
    InstallStandIns(*s);
    RunPair(RunObjectTest, "CObject: construction, id / type / tags, flags, member functions");
    RunPair(RunObjectScriptTest, "CObject: its script, spawning, events, MarkAsDead, destruction");
    RunPair(RunManagerTest, "CBaseManager / CGeneralManager: register, walks, searches, dead list, destruction");
    Cleanup();
}

} // namespace

SELFTEST(Early, TestObjects, Cleanup);
