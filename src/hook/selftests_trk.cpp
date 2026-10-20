// Self-tests for the object tracker (src/game/obj/tracker.cpp) and CRefCounted's constructor / destructor
// (src/game/obj/smtptr.cpp): the original and ours run the same sequence over stand-in scripts, objects, event
// listeners and script parameters in an arena. What they call outside the tracker (the memory manager, the pools,
// the hash table item pool, the CScript methods, the object functions, the structure getters, Dbg::Printf, the
// game's sprintf) is stood in for by recording code (written over the game's function and ours alike), so the two
// are compared call by call, then the whole arena word by word. The event's own methods (0x0045F7F0, 0x0045F9B0,
// 0x00460150) and the hash table are the real code.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
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
using Obj::CTracker;
using Script::CScript;
using Script::CStruct;

constexpr uint32_t kArenaSize = 0x40000;
// (aligned to 64K, and both mapped in either run - ArenaWord / ArenaWordOrHigh, selftest.h: a value that only
// happens to fall inside one of them maps the same way in each)
ArenaPair<kArenaSize> g_trk_storage;
uint8_t (*const g_trk_arenas)[kArenaSize] = g_trk_storage.Get();
uint32_t g_trk_base;
uint32_t g_trk_bump;     // the heap stand-in's next block
uint32_t g_trk_poolBump; // the pool stand-in's next item
bool g_trk_original;
uint32_t g_trk_alive;            // which stand-in scripts FindScriptWithUniqueId finds (bit per script)
uint32_t g_trk_spawned;          // the spawned script FindSpawnedScriptWithId finds (index + 1; 0 none)
uint32_t g_trk_listenerMarks;    // event listeners (bit per listener) that mark the event handled
int g_trk_destroyOnReceive = -1; // ReceiveEvent on script 0 destroys this script's CRefCounted
int g_trk_markOnReceive = -1;    // ReceiveEvent on this script marks the event handled (0x0045F9B0)
std::vector<Saved> g_trk_saved;

constexpr int kScripts = 12;
constexpr int kObjects = 6;
constexpr int kListeners = 3;

uint8_t* Arena() { return reinterpret_cast<uint8_t*>(g_trk_base); }
CScript* ScriptAt(int i) { return reinterpret_cast<CScript*>(Arena() + 0x800 + 0x100 * i); }
Obj::CObject* ObjectAt(int i) { return reinterpret_cast<Obj::CObject*>(Arena() + 0x1400 + 0x80 * i); }
uint32_t* ObjectWords(int i) { return reinterpret_cast<uint32_t*>(ObjectAt(i)); }
Obj::CEventListener* ListenerAt(int i) { return reinterpret_cast<Obj::CEventListener*>(Arena() + 0x1900 + 0x20 * i); }
uint32_t* ListenerWords(int i) { return reinterpret_cast<uint32_t*>(ListenerAt(i)); }
uint32_t ObjectIdOf(int i) { return ObjectWords(i)[5]; }

uint32_t Rel(uint32_t word) { return ArenaWord(g_trk_arenas, kArenaSize, word); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
// An event's contents (events live on the stack: their addresses differ).
void RecEvent(uint32_t tag, uint32_t event, std::initializer_list<uint32_t> values)
{
    auto* e = reinterpret_cast<const uint32_t*>(event);
    Rec(tag, values);
    Rec(0xE7E7, {e[0], e[1], e[2], e[3], e[4]});
}
uint32_t Bump(uint32_t size)
{
    uint32_t block = g_trk_bump;
    g_trk_bump += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(block), 0xCD, size);
    return block;
}

// --- stand-in script parameters: a list of {name, kind, value} at the CStruct's address ---
enum Kind : uint32_t { CHECKSUM = 1, ARRAY = 2, STRUCTURE = 3, FLAG = 4, FLOAT = 5, INTEGER = 6, OBJECT_ID = 7 };
struct TestParams {
    uint32_t count;
    struct {
        uint32_t name, kind, value;
    } items[10];
};
TestParams* ParamsAt(int i) { return reinterpret_cast<TestParams*>(Arena() + 0x1A00 + 0x100 * i); }
CStruct* Params(int i) { return reinterpret_cast<CStruct*>(ParamsAt(i)); }
bool Find(uint32_t params, uint32_t name, uint32_t kind, uint32_t* value)
{
    auto* p = reinterpret_cast<TestParams*>(params);
    for (uint32_t i = 0; i < p->count; ++i) {
        if (p->items[i].name == name && p->items[i].kind == kind) {
            *value = p->items[i].value;
            return true;
        }
    }
    return false;
}
void SetParams(int i, std::initializer_list<std::initializer_list<uint32_t>> items)
{
    TestParams* p = ParamsAt(i);
    std::memset(p, 0, sizeof(*p));
    for (auto& item : items) {
        auto it = item.begin();
        p->items[p->count].name = it[0];
        p->items[p->count].kind = it[1];
        p->items[p->count].value = it[2];
        ++p->count;
    }
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_trk_saved);
    g_trk_saved.clear();
}

uint32_t Getter(Regs& r, uint32_t tag, uint32_t kind)
{
    Rec(tag, {r.ecx, r.args[0], r.args[2]});
    uint32_t value;
    if (!Find(r.ecx, r.args[0], kind, &value))
        return 0;
    *reinterpret_cast<uint32_t*>(r.args[1]) = value;
    return 1;
}

void InstallStandIns(StandIns& s)
{
    // The memory manager and the pools.
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xA11, {r.args[0], r.args[1], r.args[2]});
        return Bump(r.args[0]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xA12, {r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xA13, {r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Rec(0xA14, {});
        return 0u;
    });
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Rec(0xB01, {r.ecx});
        uint32_t item = g_trk_poolBump;
        g_trk_poolBump += 0x20;
        std::memset(reinterpret_cast<void*>(item), 0xBD, 0x14);
        return item;
    });
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        Rec(0xB02, {r.ecx, r.args[0]});
        return r.args[0] >= g_trk_base + 0x4000 && r.args[0] < g_trk_base + 0x6000 ? 1u : 0u;
    });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Rec(0xB03, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BC450, AddressOf(&Mem::PoolManager::CreateItem)}, 0, [](Regs& r) {
        Rec(0xB04, {r.args[0]});
        return Bump(12);
    });
    s.At({0x005BC4A0, AddressOf(&Mem::PoolManager::FreeItem)}, 0, [](Regs& r) {
        Rec(0xB05, {r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Rec(0xDB6, {r.args[0]});
        return 0xFFFFFFFFu;
    });
    s.At({0x006234FE}, 0, StandInSprintf); // (the original's sprintf; ours uses its own C runtime)
    // The CScript functions.
    s.At({0x004734B0, AddressOf(&Script::FindScriptWithUniqueId)}, 0, [](Regs& r) {
        Rec(0x34B0, {r.args[0]});
        for (int i = 0; i < kScripts; ++i) {
            if ((g_trk_alive >> i & 1) && ScriptAt(i)->m_uniqueId == r.args[0])
                return reinterpret_cast<uint32_t>(ScriptAt(i));
        }
        return 0u;
    });
    s.At({0x00472E20, AddressOf(&Script::FindSpawnedScriptWithId)}, 0, [](Regs& r) {
        Rec(0x2E20, {r.args[0]});
        return g_trk_spawned ? reinterpret_cast<uint32_t>(ScriptAt(static_cast<int>(g_trk_spawned) - 1)) : 0u;
    });
    s.At({0x00472FD0, AddressOf(&CScript::Block)}, 4, [](Regs& r) {
        Rec(0x2FD0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004728F0, AddressOf(&CScript::StopWaiting)}, 0, [](Regs& r) {
        Rec(0x28F0, {r.ecx});
        return 0u;
    });
    s.At({0x00473800, AddressOf(&CScript::ReceiveEvent)}, 4, [](Regs& r) {
        RecEvent(0x3800, r.args[0], {r.ecx});
        if (r.ecx == reinterpret_cast<uint32_t>(ScriptAt(0)) && g_trk_destroyOnReceive >= 0) {
            Obj::CRefCounted* refCounted = &ScriptAt(g_trk_destroyOnReceive)->m_refCounted;
            if (g_trk_original)
                Original<void(__thiscall*)(Obj::CRefCounted*)>(0x00463870)(refCounted);
            else
                refCounted->Destruct();
        }
        if (g_trk_markOnReceive >= 0 && r.ecx == reinterpret_cast<uint32_t>(ScriptAt(g_trk_markOnReceive)))
            GameFunction<void(__thiscall*)(uint32_t, uint32_t, uint32_t)>(0x0045F9B0)(r.args[0], 7, 0x77777777);
        return 1u;
    });
    s.At({0x00474350, AddressOf(&Script::StopScriptsUsingObjectThatReferTo)}, 0, [](Regs& r) {
        Rec(0x4350, {r.args[0], r.args[1]});
        return 0u;
    });
    // The objects and event listeners.
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Rec(0x19F0, {r.ecx, r.args[0]});
        for (int i = 0; i < kObjects; ++i) {
            if (ObjectIdOf(i) == r.args[0])
                return reinterpret_cast<uint32_t>(ObjectAt(i));
        }
        return 0u;
    });
    s.At({0x004A1090}, 4, [](Regs& r) {
        auto* listener = reinterpret_cast<uint32_t*>(r.ecx);
        RecEvent(0x1090, r.args[0], {r.ecx, listener[6]});
        int index = static_cast<int>((r.ecx - reinterpret_cast<uint32_t>(ListenerAt(0))) / 0x20);
        if (g_trk_listenerMarks >> index & 1)
            reinterpret_cast<uint32_t*>(r.args[0])[3] |= 2;
        return 0u;
    });
    s.At({0x004AC120}, 12, [](Regs& r) {
        Rec(0xC120, {r.ecx, r.args[0], r.args[1]});
        uint32_t value;
        if (!Find(r.args[0], r.args[1], OBJECT_ID, &value))
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[2]) = value;
        return 1u;
    });
    // The structure getters.
    using GetChecksumFn = bool (CStruct::*)(uint32_t, uint32_t*, int);
    using GetArrayFn = bool (CStruct::*)(uint32_t, Script::CArray**, int);
    using GetStructureFn = bool (CStruct::*)(uint32_t, CStruct**, int);
    using GetFloatFn = bool (CStruct::*)(uint32_t, float*, int);
    using GetIntegerFn = bool (CStruct::*)(uint32_t, int*, int);
    using ContainsFlagFn = bool (CStruct::*)(uint32_t);
    s.At({0x00476950, AddressOf(static_cast<GetChecksumFn>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) { return Getter(r, 0x6950, CHECKSUM); });
    s.At({0x00476860, AddressOf(static_cast<GetArrayFn>(&CStruct::GetArray))}, 12,
         [](Regs& r) { return Getter(r, 0x6860, ARRAY); });
    s.At({0x004767C0, AddressOf(static_cast<GetStructureFn>(&CStruct::GetStructure))}, 12,
         [](Regs& r) { return Getter(r, 0x67C0, STRUCTURE); });
    s.At({0x00476D50, AddressOf(static_cast<GetFloatFn>(&CStruct::GetFloat))}, 12,
         [](Regs& r) { return Getter(r, 0x6D50, FLOAT); });
    s.At({0x00476630, AddressOf(static_cast<GetIntegerFn>(&CStruct::GetInteger))}, 12,
         [](Regs& r) { return Getter(r, 0x6630, INTEGER); });
    s.At({0x00476B40, AddressOf(static_cast<ContainsFlagFn>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x6B40, {r.ecx, r.args[0]});
        uint32_t value;
        return Find(r.ecx, r.args[0], FLAG, &value) ? 1u : 0u;
    });
    // GetNextComponent on a stand-in structure: its first component's address at +0.
    s.At({0x00476490, AddressOf(&CStruct::GetNextComponent)}, 4, [](Regs& r) {
        Rec(0x6490, {r.ecx, r.args[0]});
        if (!r.args[0])
            return *reinterpret_cast<uint32_t*>(r.ecx);
        return reinterpret_cast<uint32_t>(reinterpret_cast<Script::CComponent*>(r.args[0])->next);
    });
    // The global array of event names.
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        return g_trk_base + 0x3000;
    });
}

// The objects' vtable: +0x04 SetProperties(params), +0x14 PassEvent(event).
void* g_trk_objectVtable[8];

void TestTracker()
{
    g_trk_saved = {
        {0x006F0FC0, 8, {}},  // the tracker and its use count
        {0x006F0FC8, 16, {}}, // the listener lists' pool pair
        {0x007D1800, 4, {}},  // the memory manager
        {0x007CC979, 1, {}},  // the memory manager is ready
        {0x006F0FB8, 4, {}},  // the object manager (0x004619F0's)
        {0x00701440, 4, {}},  // the screen element manager (0x004AC120's)
    };
    SaveAll(g_trk_saved);
    auto* s = new StandIns;
    InstallStandIns(*s);
    for (void*& slot : g_trk_objectVtable)
        slot = nullptr;
    g_trk_objectVtable[1] = s->Thunk(4, [](Regs& r) {
        Rec(0x0B11, {r.ecx, r.args[0]});
        return 0u;
    });
    g_trk_objectVtable[5] = s->Thunk(4, [](Regs& r) {
        RecEvent(0x0B15, r.args[0], {r.ecx});
        return 0u;
    });

    auto run = [&](bool original) -> std::vector<uint32_t> {
        g_trace.clear();
        g_trk_original = original;
        uint8_t* arena = g_trk_arenas[original ? 0 : 1];
        std::memset(arena, 0, kArenaSize);
        g_trk_base = reinterpret_cast<uint32_t>(arena);
        g_trk_bump = g_trk_base + 0x8000;
        g_trk_poolBump = g_trk_base + 0x4000;
        g_trk_alive = 0;
        g_trk_spawned = 0;
        g_trk_listenerMarks = 0;
        g_trk_destroyOnReceive = -1;
        g_trk_markOnReceive = -1;
        for (Saved& saved : g_trk_saved)
            std::memset(reinterpret_cast<void*>(saved.address), 0, saved.size);
        GameVariable<bool>(0x007CC979) = true;
        auto* manager = reinterpret_cast<Mem::Manager*>(arena);
        manager->m_scriptHeap = reinterpret_cast<Mem::Heap*>(0x5C1F0000);
        GameVariable<void*>(0x007D1800) = manager;
        GameVariable<uint32_t>(0x006F0FB8) = 0x0B1E0000;
        GameVariable<uint32_t>(0x00701440) = 0x5C4E0000;
        auto* pool = reinterpret_cast<Mem::Pool*>(arena + 0x3800);
        auto& pools = GameVariable<Mem::PoolPair>(0x006F0FC8);

        // Scripts (ids 100 + i; scripts 0, 3, 4 and 6 run on objects 0, 1, 2 and 3; script 7 has no name).
        for (int i = 0; i < kScripts; ++i) {
            CScript* p = ScriptAt(i);
            p->m_uniqueId = 100 + i;
            p->m_scriptName = i == 7 ? 0 : 0x5C000000u + i;
        }
        // Objects: ids 0x100, 0x200, 0x300...; objects 0-2 have positions.
        static const float positions[kObjects][4] = {
            {0, 0, 0, 1}, {3, 4, 0, 1}, {10, 1, 2, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}, {0, 0, 0, 1}};
        auto* vtable = reinterpret_cast<void**>(arena + 0x1800); // (in the arena: compared relocated)
        std::memcpy(vtable, g_trk_objectVtable, sizeof(g_trk_objectVtable));
        for (int i = 0; i < kObjects; ++i) {
            uint32_t* o = ObjectWords(i);
            o[0] = reinterpret_cast<uint32_t>(vtable);
            o[2] = i < 3 ? 0x20 : 0x1F;
            o[5] = 0x100u * (i + 1);
            std::memcpy(o + 0x13, positions[i], 16);
        }
        ScriptAt(0)->m_object.mp_object = ObjectAt(0);
        ScriptAt(3)->m_object.mp_object = ObjectAt(1);
        ScriptAt(4)->m_object.mp_object = ObjectAt(2);
        ScriptAt(6)->m_object.mp_object = ObjectAt(3);
        // The event names (a string array).
        auto* names = reinterpret_cast<Script::CArray*>(arena + 0x3000);
        auto* nameStrings = reinterpret_cast<uint32_t*>(arena + 0x3010);
        const char* texts[3] = {"Ev_Alpha", "Ev_Beta", "SomeEvent"};
        for (int i = 0; i < 3; ++i) {
            std::memcpy(arena + 0x3100 + 0x20 * i, texts[i], std::strlen(texts[i]) + 1);
            nameStrings[i] = g_trk_base + 0x3100 + 0x20 * i;
        }
        names->m_type = Script::ESYMBOLTYPE_STRING;
        names->m_size = 3;
        names->m_elements = nameStrings;

        // The functions under test, original or ours.
        CTracker* t = nullptr;
        auto create = [&](bool make) {
            CTracker* result =
                original ? Original<CTracker*(__cdecl*)(bool)>(0x00463730)(make) : Obj::CreateTracker(make);
            Rec(0x3730, {reinterpret_cast<uint32_t>(result), GameVariable<uint32_t>(0x006F0FC4)});
            return result;
        };
        auto call = [&](uint32_t address, auto ours, auto... args) {
            using R = decltype((t->*ours)(args...));
            if (original)
                return Original<R(__thiscall*)(CTracker*, decltype(args)...)>(address)(t, args...);
            return (t->*ours)(args...);
        };
        auto getObject = [&](uint32_t id) {
            Rec(0x2120, {reinterpret_cast<uint32_t>(call(0x00462120, &CTracker::LookUpObject, id))});
        };
        auto addListener = [&](uint32_t type, int i) {
            call(0x00463370, &CTracker::AddListener, type, ScriptAt(i));
        };
        auto launch = [&](uint32_t type, uint32_t target, uint32_t source, CStruct* data, bool broadcast,
                          float radius) {
            bool handled = call(0x00462F00, &CTracker::LaunchEvent, type, target, source, data, broadcast, radius);
            Rec(0x2F00, {static_cast<uint32_t>(handled), static_cast<uint32_t>(t->m_launchDepth)});
        };
        auto cfunc = [&](uint32_t address, bool(__cdecl * ours)(CStruct*, CScript*), int params, CScript* script) {
            bool result = original ? Original<bool(__cdecl*)(CStruct*, CScript*)>(address)(Params(params), script)
                                   : ours(Params(params), script);
            Rec(address, {static_cast<uint32_t>(result), static_cast<uint32_t>(t->m_launchedFromScript)});
        };

        // --- create (not made without `create`; a second create only counts).
        create(false);
        t = create(true);
        create(true);

        // --- objects and aliases (ids 0x100 and 0x200 share bucket 0; alias 0x1111 is moved; the alias 0x300 goes
        // when object 0x300 comes).
        call(0x00462B70, &CTracker::LogFrame);
        call(0x00462B70, &CTracker::LogFrame);
        call(0x00462980, &CTracker::AddObject, ObjectAt(0));
        call(0x00462B70, &CTracker::LogFrame);
        call(0x00462980, &CTracker::AddObject, ObjectAt(1));
        call(0x00462A90, &CTracker::AddAlias, 0x1111u, ObjectAt(0));
        call(0x00462A90, &CTracker::AddAlias, 0x2221u, ObjectAt(1));
        call(0x00462A90, &CTracker::AddAlias, 0x1111u, ObjectAt(1));
        call(0x00462A90, &CTracker::AddAlias, 0x0331u, ObjectAt(1));
        call(0x00462A90, &CTracker::AddAlias, 0x0300u, ObjectAt(0));
        call(0x00462A90, &CTracker::AddAlias, 0x0400u, ObjectAt(4)); // (object 4 isn't tracked)
        call(0x00462980, &CTracker::AddObject, ObjectAt(2));
        for (uint32_t id : {0x100u, 0x200u, 0x300u, 0x1111u, 0x2221u, 0x0400u, 0x9999u})
            getObject(id);
        for (uint32_t alias : {0x2221u, 0x1111u, 0x100u, 0x300u})
            Rec(0x2190, {reinterpret_cast<uint32_t>(call(0x00462190, &CTracker::GetObjectByAlias, alias))});
        // Unique ids: "autoid1" is taken; then the counter wraps at 1000000.
        ObjectWords(5)[5] = Crc::GenerateCRCFromString("autoid1");
        call(0x00462980, &CTracker::AddObject, ObjectAt(5));
        for (int i = 0; i < 3; ++i)
            Rec(0x21D0, {call(0x004621D0, &CTracker::GetUniqueId), static_cast<uint32_t>(t->m_nextAutoId)});
        t->m_nextAutoId = 999999;
        Rec(0x21D0, {call(0x004621D0, &CTracker::GetUniqueId), static_cast<uint32_t>(t->m_nextAutoId)});

        // --- listeners: type 0x11 (scripts 0, 1, 2; 1 twice), 0x21 (script 1), 0x31 (script 4) and 0x41 (script 6)
        // from the pool, 0x111 (scripts 3, 5, 0: the same bucket as 0x11, chained).
        addListener(0x11, 0);
        addListener(0x11, 1);
        addListener(0x11, 1);
        addListener(0x11, 2);
        addListener(0x21, 1);
        pools.pools[0] = pool;
        addListener(0x31, 4);
        addListener(0x41, 6);
        pools.pools[0] = nullptr;
        addListener(0x111, 3);
        addListener(0x111, 5);
        addListener(0x111, 0);

        // --- broadcasts.
        // Several: script 0's handler deletes script 2, which is skipped (its smart pointer cleared).
        g_trk_destroyOnReceive = 2;
        launch(0x11, Obj::kNoObjectId, 0x100, Params(9), true, -1.0f);
        g_trk_destroyOnReceive = -1;
        // With a radius from object 0x100 (at 0,0,0): script 3's object (3,4,0) is 25 away squared, script 0's 0,
        // script 5 has no object.
        launch(0x111, 0x200, 0x100, nullptr, true, 5.0f);
        launch(0x111, 0x200, 0x100, nullptr, true, 5.01f);
        launch(0x111, 0x200, 0x100, nullptr, true, 0.0f);
        // One listener: in range (105 squared) / out of range / its object has no position / no radius.
        launch(0x31, Obj::kNoObjectId, 0x100, nullptr, true, 12.0f);
        launch(0x31, Obj::kNoObjectId, 0x100, nullptr, true, 10.0f);
        launch(0x41, Obj::kNoObjectId, 0x300, nullptr, true, 1.0f);
        launch(0x41, Obj::kNoObjectId, 0x300, nullptr, true, -0.5f);
        // Nobody listening; one that marks it handled.
        launch(0x99, Obj::kNoObjectId, 0x100, nullptr, true, -1.0f);
        g_trk_markOnReceive = 1;
        launch(0x21, Obj::kNoObjectId, 0x100, nullptr, true, -1.0f);
        g_trk_markOnReceive = -1;

        // --- targeted events: the listeners first (1 marks it handled: the target doesn't get it), then the
        // target object (by id or alias), or the spawned script with that id.
        t->mp_eventListeners = ListenerAt(0);
        ListenerWords(0)[4] = reinterpret_cast<uint32_t>(ListenerAt(1));
        ListenerWords(1)[4] = reinterpret_cast<uint32_t>(ListenerAt(2));
        for (int i = 0; i < kListeners; ++i) {
            ListenerWords(i)[5] = 1;       // registered
            ListenerWords(i)[6] = 10u * i; // busy counts
        }
        launch(0x51, 0x100, 0x200, Params(9), false, -1.0f);
        launch(0x51, 0x2221, 0x200, nullptr, false, -1.0f);
        launch(0x51, Obj::kNoObjectId, 0x200, nullptr, false, -1.0f);
        g_trk_spawned = 8;
        launch(0x52, 0xABCD, 0x200, nullptr, false, -1.0f);
        g_trk_listenerMarks = 2;
        launch(0x53, 0x100, 0x200, nullptr, false, -1.0f);
        g_trk_listenerMarks = 0;
        g_trk_markOnReceive = 7;
        launch(0x54, 0xABCD, 0x200, nullptr, false, -1.0f);
        g_trk_markOnReceive = -1;

        // --- WaitForEvent: scripts 3 (object 0x200) and 5 (none) wait for 0x61, 4 (object 0x300) for 0x62, 6 for
        // 0x63.
        g_trk_alive = 0xFFF;
        call(0x00462260, &CTracker::WaitForEvent, ScriptAt(3), 0x61u);
        call(0x00462260, &CTracker::WaitForEvent, ScriptAt(5), 0x61u);
        call(0x00462260, &CTracker::WaitForEvent, ScriptAt(4), 0x62u);
        call(0x00462260, &CTracker::WaitForEvent, ScriptAt(6), 0x63u);
        // Targeted at 0x100: script 5 (no object) goes, only it. Then at 0x200: script 3.
        launch(0x61, 0x100, 0x200, nullptr, false, -1.0f);
        launch(0x61, 0x200, 0x100, nullptr, false, -1.0f);
        // Script 4's is for 0x300: a broadcast reaches it anyway.
        launch(0x62, 0x500, 0x100, nullptr, true, -1.0f);
        // Script 6 has gone: its slot is dropped, nobody goes.
        g_trk_alive &= ~(1u << 6);
        launch(0x63, Obj::kNoObjectId, 0x100, nullptr, false, -1.0f);
        g_trk_alive = 0xFFF;
        // All 40 slots taken by live scripts: no wait. Then one's script is gone: its slot is taken.
        for (int i = 0; i < 40; ++i)
            call(0x00462260, &CTracker::WaitForEvent, ScriptAt(i % kScripts), 0x70u + i);
        call(0x00462260, &CTracker::WaitForEvent, ScriptAt(9), 0x99u);
        g_trk_alive &= ~(1u << 1);
        call(0x00462260, &CTracker::WaitForEvent, ScriptAt(9), 0x98u);
        g_trk_alive = 0xFFF;
        // A renamed object: waiters on 0x200 follow it.
        call(0x00462A20, &CTracker::RemoveObject, ObjectAt(1), 0x777u, true);
        // A removed object's aliases go (0x2221, 0x0331, 0x1111 name object 1).
        call(0x00462980, &CTracker::AddObject, ObjectAt(1));
        call(0x00462A90, &CTracker::AddAlias, 0x0441u, ObjectAt(0));
        call(0x00462A20, &CTracker::RemoveObject, ObjectAt(1), 0x888u, false);
        for (uint32_t id : {0x100u, 0x200u, 0x1111u, 0x2221u, 0x0331u, 0x0441u})
            getObject(id);
        call(0x00462590, &CTracker::RemoveAliasesOf, ObjectAt(0));
        getObject(0x0441);

        // --- removing listeners: 0x31 (from the pool) empties and goes; 0x11 keeps scripts 0 and 2.
        call(0x00463450, &CTracker::RemoveListener, 0x31u, ScriptAt(4));
        call(0x00463450, &CTracker::RemoveListener, 0x11u, ScriptAt(1));
        call(0x00463450, &CTracker::RemoveListener, 0x11u, ScriptAt(9));
        call(0x00463450, &CTracker::RemoveListener, 0x77u, ScriptAt(1));
        // Script 0 forgets everything: 0x111 keeps 3 and 5; script 1: 0x21 goes.
        call(0x00462B90, &CTracker::ForgetScript, ScriptAt(0));
        call(0x00462B90, &CTracker::ForgetScript, ScriptAt(1));
        launch(0x11, Obj::kNoObjectId, 0x100, nullptr, true, -1.0f);
        launch(0x111, Obj::kNoObjectId, 0x100, nullptr, true, -1.0f);
        call(0x00462B90, &CTracker::ForgetScript, ScriptAt(2));
        call(0x00462B90, &CTracker::ForgetScript, ScriptAt(3));
        call(0x00462B90, &CTracker::ForgetScript, ScriptAt(5));
        call(0x00462B90, &CTracker::ForgetScript, ScriptAt(6));
        call(0x00462B90, &CTracker::ForgetScript, ScriptAt(6)); // (an empty table)

        // --- event listeners out: the middle one, the first, one that isn't there, the last.
        auto removeListener = [&](int i) {
            call(0x00462000, &CTracker::RemoveEventListener, ListenerAt(i));
            Rec(0x2000, {reinterpret_cast<uint32_t>(t->mp_eventListeners)});
        };
        removeListener(1);
        removeListener(0);
        removeListener(1);
        removeListener(2);
        ListenerWords(2)[5] = 1;
        removeListener(2);

        // --- the log: entries by others, printing, and the ring wrapping.
        uint32_t fakeEvent[5] = {0xE1, 0xE2, 0xE3, 0, 0};
        auto* event = reinterpret_cast<Obj::CEvent*>(fakeEvent);
        call(0x00462B10, &CTracker::LogHandled, event, 4u, 0x5C5C0001u);
        call(0x00462B40, &CTracker::LogRead, event, 5u, 0x5C5C0002u);
        auto print = [&](bool onlyNew, int count) {
            if (original)
                Original<void(__thiscall*)(Obj::CEventLog*, bool, int)>(0x004620A0)(&t->m_log, onlyNew, count);
            else
                t->m_log.Print(onlyNew, count);
            Rec(0x20A0, {static_cast<uint32_t>(t->m_log.m_newEntries)});
        };
        print(true, 0x400);
        print(false, 3);
        for (int i = 0; i < 0x3F0; ++i)
            call(0x00462B40, &CTracker::LogRead, event, static_cast<uint32_t>(i), 0u);
        call(0x00462B70, &CTracker::LogFrame);
        call(0x00462B70, &CTracker::LogFrame);
        print(true, 0x400);
        // A frame entry given a type (none of the game's calls does that).
        call(0x00462B40, &CTracker::LogRead, event, 1u, 0u);
        if (original)
            Original<void(__thiscall*)(Obj::CEventLog*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int)>(
                0x004627C0)(&t->m_log, 5, 6, 7, 8, 9, Obj::CEventLog::FRAME);
        else
            t->m_log.AddEntry(5, 6, 7, 8, 9, Obj::CEventLog::FRAME);

        // --- the script commands.
        const uint32_t kTypeName = 0x7321A8D6, kTargetName = 0xB990D003, kSourceName = 0xA075808C;
        const uint32_t kIdName = 0x40C698AF;
        call(0x00462980, &CTracker::AddObject, ObjectAt(1));
        addListener(0x111, 4);
        addListener(0x111, 3);
        SetParams(0, {{kTypeName, CHECKSUM, 0x81}});
        g_trk_alive &= ~(1u << 2); // (a slot for it)
        cfunc(0x00462310, Obj::CFunc_WaitForEvent, 0, ScriptAt(10));
        g_trk_alive = 0xFFF;
        SetParams(1, {{kIdName, OBJECT_ID, 0x200}});
        cfunc(0x00462340, Obj::CFunc_ObjectExists, 1, nullptr);
        cfunc(0x00462440, Obj::CFunc_SetObjectProps, 1, nullptr);
        SetParams(2, {{kIdName, OBJECT_ID, 0x9999}});
        cfunc(0x00462340, Obj::CFunc_ObjectExists, 2, nullptr);
        SetParams(3, {});
        cfunc(0x00462340, Obj::CFunc_ObjectExists, 3, nullptr);
        cfunc(0x00462380, Obj::CFunc_TerminateObjectsScripts, 3, nullptr);
        cfunc(0x00462440, Obj::CFunc_SetObjectProps, 3, nullptr);
        cfunc(0x00462C40, Obj::CFunc_AssignAlias, 3, nullptr);
        // TerminateObjectsScripts: by array, by name, by nothing.
        auto* scriptsArray = reinterpret_cast<Script::CArray*>(arena + 0x3400);
        auto* scriptNames = reinterpret_cast<uint32_t*>(arena + 0x3410);
        scriptNames[0] = 0xAAA1;
        scriptNames[1] = 0xAAA2;
        scriptNames[2] = 0xAAA3;
        scriptsArray->m_type = Script::ESYMBOLTYPE_NAME;
        scriptsArray->m_size = 3;
        scriptsArray->m_elements = scriptNames;
        SetParams(4, {{kIdName, OBJECT_ID, 0x200}, {0x22E168C1, ARRAY, reinterpret_cast<uint32_t>(scriptsArray)}});
        cfunc(0x00462380, Obj::CFunc_TerminateObjectsScripts, 4, nullptr);
        SetParams(5, {{kIdName, OBJECT_ID, 0x100}, {0x6166F3AD, CHECKSUM, 0xBBB1}});
        cfunc(0x00462380, Obj::CFunc_TerminateObjectsScripts, 5, nullptr);
        cfunc(0x00462380, Obj::CFunc_TerminateObjectsScripts, 1, nullptr);
        // PrintEventLog: no count / a count.
        cfunc(0x00462480, Obj::CFunc_PrintEventLog, 3, nullptr);
        SetParams(6, {{0, INTEGER, 5}});
        cfunc(0x00462480, Obj::CFunc_PrintEventLog, 6, nullptr);
        // AssignAlias.
        SetParams(7, {{kIdName, OBJECT_ID, 0x200}, {0x1E93946B, CHECKSUM, 0x5551}});
        cfunc(0x00462C40, Obj::CFunc_AssignAlias, 7, nullptr);
        getObject(0x5551);
        // LaunchEvent: one type (target / source given, given as none, defaulting to the script's object or none),
        // with data, broadcast and radius; an array of types; a structure of types; no type.
        SetParams(8, {{kTypeName, CHECKSUM, 0x51}, {kTargetName, OBJECT_ID, 0x100}, {kSourceName, OBJECT_ID, 0x200},
                      {0x520C0C9C, STRUCTURE, reinterpret_cast<uint32_t>(Params(9))}});
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 8, ScriptAt(0));
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 8, ScriptAt(7));
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 8, nullptr);
        SetParams(10, {{kTypeName, CHECKSUM, 0x51}});
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 10, ScriptAt(0));
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 10, ScriptAt(1));
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 10, nullptr);
        SetParams(11, {{kTypeName, CHECKSUM, 0x111}, {kTargetName, OBJECT_ID, Obj::kNoObjectId},
                       {kSourceName, OBJECT_ID, Obj::kNoObjectId}, {0x640E830A, FLAG, 0},
                       {0xC48391A5, FLOAT, 0xBF800000}});
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 11, ScriptAt(0));
        float radius = 30.0f;
        std::memcpy(&ParamsAt(11)->items[4].value, &radius, 4);
        ParamsAt(11)->items[2].value = 0x300;
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 11, ScriptAt(0));
        auto* typesArray = reinterpret_cast<Script::CArray*>(arena + 0x3500);
        auto* typeValues = reinterpret_cast<uint32_t*>(arena + 0x3510);
        typeValues[0] = 0x51;
        typeValues[1] = 0x81;
        typeValues[2] = 0x52;
        typesArray->m_type = Script::ESYMBOLTYPE_NAME;
        typesArray->m_size = 3;
        typesArray->m_elements = typeValues;
        SetParams(12, {{kTypeName, ARRAY, reinterpret_cast<uint32_t>(typesArray)}, {kTargetName, OBJECT_ID, 0x200}});
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 12, ScriptAt(3));
        typesArray->m_size = 0;
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 12, ScriptAt(3));
        auto* typesStruct = reinterpret_cast<uint32_t*>(arena + 0x3600);
        auto* components = reinterpret_cast<Script::CComponent*>(arena + 0x3610);
        for (int i = 0; i < 3; ++i) {
            components[i].checksum = 0x51u + 0x60u * i;
            components[i].next = i < 2 ? &components[i + 1] : nullptr;
        }
        typesStruct[0] = reinterpret_cast<uint32_t>(&components[0]);
        SetParams(13, {{kTypeName, STRUCTURE, reinterpret_cast<uint32_t>(typesStruct)}, {0x640E830A, FLAG, 0}});
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 13, ScriptAt(1));
        typesStruct[0] = 0;
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 13, ScriptAt(1));
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 3, ScriptAt(1));
        // WaitForEvent's waiter released by the LaunchEvent command.
        call(0x00462260, &CTracker::WaitForEvent, ScriptAt(11), 0x82u);
        SetParams(14, {{kTypeName, CHECKSUM, 0x82}, {kTargetName, OBJECT_ID, Obj::kNoObjectId}});
        cfunc(0x004634D0, Obj::CFunc_LaunchEvent, 14, ScriptAt(1));

        // --- the list classes' destructors, CRefCounted, and the tracker's.
        {
            auto* node = reinterpret_cast<Obj::CScriptNode*>(arena + 0x3700);
            auto* other = reinterpret_cast<Obj::CScriptNode*>(arena + 0x3720);
            auto* head = reinterpret_cast<Obj::CScriptList*>(arena + 0x3740);
            node->mp_next = other;
            node->mp_prev = other;
            other->mp_next = node;
            other->mp_prev = node;
            if (original)
                Original<Obj::CScriptNode*(__thiscall*)(Obj::CScriptNode*, uint8_t)>(0x00462550)(node, 0);
            else
                node->ScalarDelete(0);
            head->mp_next = other;
            head->mp_prev = other;
            other->mp_next = head;
            other->mp_prev = head;
            if (original)
                Original<Obj::CScriptList*(__thiscall*)(Obj::CScriptList*, uint8_t)>(0x00462710)(head, 1);
            else
                head->ScalarDelete(1);
            auto* third = reinterpret_cast<Obj::CScriptNode*>(arena + 0x3760);
            other->mp_next = third;
            other->mp_prev = third;
            third->mp_next = other;
            third->mp_prev = other;
            if (original)
                Original<void(__thiscall*)(Obj::CScriptNode*)>(0x00462530)(other);
            else
                other->Unlink();
            third->mp_next = node;
            third->mp_prev = node;
            node->mp_next = third;
            node->mp_prev = third;
            if (original)
                Original<Obj::CScriptNode*(__thiscall*)(Obj::CScriptNode*, uint8_t)>(0x00462550)(node, 1);
            else
                node->ScalarDelete(1);
        }
        {
            // Smart pointers to one CRefCounted (one of them pointing elsewhere: cleared, not unlinked).
            using P = Obj::CSmtPtr<Obj::CRefCounted>;
            auto* object = reinterpret_cast<Obj::CRefCounted*>(arena + 0x3800);
            auto* elsewhere = reinterpret_cast<Obj::CRefCounted*>(arena + 0x3840);
            auto* pointers = reinterpret_cast<P*>(arena + 0x3880);
            auto construct = [&](Obj::CRefCounted* o) {
                Obj::CRefCounted* result =
                    original ? Original<Obj::CRefCounted*(__thiscall*)(Obj::CRefCounted*)>(0x004637B0)(o)
                             : o->Construct();
                Rec(0x37B0, {reinterpret_cast<uint32_t>(result), reinterpret_cast<uint32_t>(o->m_vtable)});
                o->m_vtable = nullptr;
            };
            std::memset(object, 0xEE, 8);
            construct(object);
            construct(elsewhere);
            for (int i = 0; i < 4; ++i)
                pointers[i].Set(i == 3 ? elsewhere : object);
            pointers[1].mp_object = elsewhere;
            // (a stale back link on the first: ClearSmartPointers follows it)
            P* stray = pointers + 5;
            stray->mp_object = object;
            stray->mp_prev = stray->mp_next = nullptr;
            object->mp_smartPointers->mp_prev = stray;
            if (original)
                Original<void(__thiscall*)(Obj::CRefCounted*)>(0x00463820)(object);
            else
                object->ClearSmartPointers();
            if (original)
                Original<Obj::CRefCounted*(__thiscall*)(Obj::CRefCounted*, uint8_t)>(0x00463880)(elsewhere, 1);
            else
                elsewhere->ScalarDelete(1);
            construct(object);
            pointers[0].Set(object);
            pointers[2].Set(object);
            object->m_vtable = nullptr;
            if (original)
                Original<void(__thiscall*)(Obj::CRefCounted*)>(0x00463870)(object);
            else
                object->Destruct();
            pointers[0].Set(object);
            if (original)
                Original<Obj::CRefCounted*(__thiscall*)(Obj::CRefCounted*, uint8_t)>(0x00463880)(object, 0);
            else
                object->ScalarDelete(0);
        }
        // The tracker's deleting destructor (what the singleton's release does), and its destructor alone.
        call(0x00462E40, &CTracker::Destruct);
        call(0x00463780, &CTracker::ScalarDelete, static_cast<uint8_t>(1));

        // The whole arena.
        std::vector<uint32_t> result = g_trace;
        std::vector<uint32_t> image(kArenaSize / 4);
        std::memcpy(image.data(), arena, kArenaSize);
        for (uint32_t& word : image) {
            word = ArenaWordOrHigh(g_trk_arenas, kArenaSize, word);
        }
        result.insert(result.end(), image.begin(), image.end());
        result.push_back(Rel(GameVariable<uint32_t>(0x006F0FC0)));
        result.push_back(GameVariable<uint32_t>(0x006F0FC4));
        g_trace.clear();
        return result;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Cleanup();
    Compare(expected, actual,
            "the tracker: create, objects / aliases / unique ids, listeners (pool and heap), LaunchEvent (broadcast: "
            "one / several / radius / a deleted listener; targeted: event listeners, object, alias, spawned script), "
            "WaitForEvent, RemoveObject, RemoveListener / ForgetScript, event listeners, the log, the script "
            "commands, the list and CRefCounted destructors");
}

} // namespace

SELFTEST(Early, TestTracker, Cleanup);
