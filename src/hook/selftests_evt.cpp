// Self-tests for events and the event handler table (src/game/obj/event.cpp, 0x0045F820-0x00460792). The original
// and ours run the same sequence on identical stand-in tables, scripts and objects in an arena; everything they
// call outside (the tracker, the memory manager, the structure functions, the smart pointers, CScript's
// Interrupt / SetScript / Update, SpawnScript, the debug output) is stood in for by recording code written over
// the game's function and ours alike, so the two are compared call by call and then the arena word by word.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/obj/event.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CEvent;
using Obj::CEventHandlerTable;
using Obj::CEventListener;
using Script::CScript;
using Script::CStruct;
using Entry = CEventHandlerTable::Entry;

constexpr uint32_t kArenaSize = 0x10000;
alignas(16) uint8_t g_evt_arenas[2][kArenaSize];
uint32_t g_evt_base;
uint32_t g_evt_bump;
bool g_evt_original;
std::vector<Saved> g_evt_saved;
// What the stand-in Interrupt does besides recording (a handler changing the table, killing the script...).
std::function<void(CScript*, uint32_t)> g_evt_onInterrupt;
uint32_t g_evt_spawned; // the script the stand-in SpawnScript returns

uint32_t Rel(uint32_t word)
{
    return word >= g_evt_base && word <= g_evt_base + kArenaSize ? 0xA0000000u + (word - g_evt_base) : word;
}
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
uint32_t Bump(uint32_t size)
{
    uint32_t block = g_evt_bump;
    g_evt_bump += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(block), 0xCD, size);
    return block;
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_evt_saved);
    g_evt_saved.clear();
    g_evt_onInterrupt = nullptr;
}

// A stand-in handler structure (AddHandlers): the CStruct (its m_head the first component), then what the
// stand-in getters answer: +0x08 params (0: none), +0x0C has a group, +0x10 the group, +0x14 exception.
struct FakeHandler {
    CStruct structure;
    uint32_t params;
    uint32_t hasGroup;
    uint32_t group;
    uint32_t exception;
    Script::CComponent components[2];
};

const char* const kNames[4] = {"name zero", "name one", "name two", "name three"};

void InstallStandIns(StandIns& s)
{
    using Mem::Manager;
    s.At({0x005BAFC0, AddressOf(&Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xA11, {r.args[0], r.args[1], r.args[2]});
        return Bump(r.args[0]);
    });
    s.At({0x005BABA0, AddressOf(&Manager::Free)}, 4, [](Regs& r) {
        Rec(0xA15, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xA13, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Manager::PopContext)}, 0, [](Regs& r) {
        Rec(0xA14, {r.ecx});
        return 0u;
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Rec(0x5701, {r.ecx});
        return r.ecx;
    });
    s.At({0x00478AB0, AddressOf(&CStruct::InitCopy)}, 4, [](Regs& r) {
        Rec(0x5709, {r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Rec(0x5702, {r.ecx});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Rec(0x5703, {r.args[0]});
        return 0u;
    });
    s.At({0x00478480, AddressOf(&CStruct::AppendStructure)}, 4, [](Regs& r) {
        Rec(0x5704, {r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Rec(0x5708, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00476490, AddressOf(&CStruct::GetNextComponent)}, 4, [](Regs& r) {
        Rec(0x5706, {r.ecx, r.args[0]});
        auto* structure = reinterpret_cast<CStruct*>(r.ecx);
        auto* component = reinterpret_cast<Script::CComponent*>(r.args[0]);
        return reinterpret_cast<uint32_t>(component ? component->next : structure->m_head);
    });
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
         [](Regs& r) {
             Rec(0x570A, {r.ecx, r.args[0], r.args[2]});
             auto* handler = reinterpret_cast<FakeHandler*>(r.ecx);
             if (!handler->params)
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = handler->params;
             return 1u;
         });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Rec(0x570B, {r.ecx, r.args[0], r.args[2]});
             auto* handler = reinterpret_cast<FakeHandler*>(r.ecx);
             if (!handler->hasGroup)
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = handler->group;
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Rec(0x570C, {r.ecx, r.args[0]});
        // (an AL with more bits than 0/1: only bit 0 counts)
        return reinterpret_cast<FakeHandler*>(r.ecx)->exception ? 0xFFFFFF01u : 0xFFFFFF00u;
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Rec(0x5300, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Rec(0x5301, {r.ecx, r.args[0]});
        return 0u;
    });
    // The tracker.
    s.At({0x00463370}, 8, [](Regs& r) {
        Rec(0x7A01, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00463450}, 8, [](Regs& r) {
        Rec(0x7A02, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00461FE0}, 4, [](Regs& r) {
        Rec(0x7A03, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00462000}, 4, [](Regs& r) {
        Rec(0x7A04, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00462B10}, 12, [](Regs& r) {
        Rec(0x7A05, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // The script.
    s.At({0x00475750, AddressOf(&CScript::Interrupt)}, 8, [](Regs& r) {
        Rec(0x1E7, {r.ecx, r.args[0], r.args[1]});
        if (g_evt_onInterrupt)
            g_evt_onInterrupt(reinterpret_cast<CScript*>(r.ecx), r.args[0]);
        return 5u;
    });
    s.At({0x004747A0, AddressOf(static_cast<void (CScript::*)(uint32_t, CStruct*, Obj::CObject*)>(&CScript::SetScript))},
         12, [](Regs& r) {
             Rec(0x5E7, {r.ecx, r.args[0], r.args[1], r.args[2]});
             return 0u;
         });
    s.At({0x004750B0, AddressOf(&CScript::Update)}, 0, [](Regs& r) {
        Rec(0x0BDA, {r.ecx});
        return 2u;
    });
    using SpawnFn = CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t, bool, bool, bool, bool);
    s.At({0x00475420, AddressOf(static_cast<SpawnFn>(&Script::SpawnScript))}, 0, [](Regs& r) {
        Rec(0x5BA, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                    r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
        return g_evt_spawned;
    });
    // Debug output: the strings are compared, not their addresses.
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        const char* format = reinterpret_cast<const char*>(r.args[0]);
        g_trace.push_back(0xDB6);
        g_trace.push_back(Hash(format));
        if (std::strstr(format, "%s"))
            g_trace.push_back(Hash(reinterpret_cast<const char*>(r.args[1])));
        return 0x1000u + static_cast<uint32_t>(g_trace.size());
    });
    s.At({0x0046D110, AddressOf(&Script::FindChecksumNameOrUnknown)}, 0, [](Regs& r) {
        Rec(0x4A3E, {r.args[0]});
        return reinterpret_cast<uint32_t>(kNames[r.args[0] & 3]);
    });
}

// The functions under test: the game's (original run) or ours.
uint32_t Pick(uint32_t original, uint32_t ours) { return g_evt_original ? original : ours; }
template <typename F>
F Fn(uint32_t original, uint32_t ours)
{
    return reinterpret_cast<F>(Pick(original, ours));
}

using Table = CEventHandlerTable;
Table* Construct(Table* t) { return Fn<Table*(__thiscall*)(Table*)>(0x0045F820, AddressOf(&Table::Construct))(t); }
void Destroy(Table* t) { Fn<void(__thiscall*)(Table*)>(0x0045FB70, AddressOf(&Table::Destroy))(t); }
void Add(Table* t, CScript* script, uint32_t type, uint32_t name, uint32_t group, bool exception, CStruct* params)
{
    Fn<void(__thiscall*)(Table*, CScript*, uint32_t, uint32_t, uint32_t, bool, CStruct*)>(
        0x0045FBC0, AddressOf(&Table::AddHandler))(t, script, type, name, group, exception, params);
}
void AddArray(Table* t, Script::CArray* array, bool replace)
{
    Fn<void(__thiscall*)(Table*, Script::CArray*, bool)>(0x004601A0, AddressOf(&Table::AddHandlers))(t, array, replace);
}
void RemoveType(Table* t, uint32_t type)
{
    Fn<void(__thiscall*)(Table*, uint32_t)>(0x0045FDF0, AddressOf(&Table::RemoveType))(t, type);
}
void RemoveGroup(Table* t, uint32_t group)
{
    Fn<void(__thiscall*)(Table*, uint32_t)>(
        0x0045FE60, AddressOf(static_cast<void (Table::*)(uint32_t)>(&Table::RemoveGroup)))(t, group);
}
void RemoveGroup(Table* t, CScript* script, uint32_t group)
{
    Fn<void(__thiscall*)(Table*, CScript*, uint32_t)>(
        0x004600E0, AddressOf(static_cast<void (Table::*)(CScript*, uint32_t)>(&Table::RemoveGroup)))(t, script, group);
}
void MarkGroup(Table* t, uint32_t group)
{
    Fn<void(__thiscall*)(Table*, uint32_t)>(0x0045F840, AddressOf(&Table::MarkGroup))(t, group);
}
void RemoveMarked(Table* t, CScript* script)
{
    Fn<void(__thiscall*)(Table*, CScript*)>(0x0045FED0, AddressOf(&Table::RemoveMarked))(t, script);
}
void Compact(Table* t) { Fn<void(__thiscall*)(Table*)>(0x0045FF50, AddressOf(&Table::Compact))(t); }
void Register(Table* t, CScript* script)
{
    Fn<void(__thiscall*)(Table*, CScript*)>(0x0045FA10, AddressOf(&Table::RegisterWithTracker))(t, script);
}
void Unregister(Table* t, CScript* script)
{
    Fn<void(__thiscall*)(Table*, CScript*)>(0x0045FA60, AddressOf(&Table::UnregisterWithTracker))(t, script);
}
void Pass(Table* t, CEvent* event, CScript* script)
{
    Fn<void(__thiscall*)(Table*, CEvent*, CScript*)>(0x00460470, AddressOf(&Table::PassEvent))(t, event, script);
}
int Print(Table* t) { return Fn<int(__cdecl*)(Table*)>(0x0045F880, AddressOf(&Table::PrintTable))(t); }

void TestEvents()
{
    g_evt_saved = {
        {0x006F0FC0, 4, {}},  // the tracker
        {0x007D1800, 4, {}},  // the memory manager
        {0x007CC979, 1, {}},  // the memory manager is ready
        {0x006F3614, 16, {}}, // the CStruct pools
    };
    SaveAll(g_evt_saved);
    auto* s = new StandIns;
    InstallStandIns(*s);

    auto run = [&](bool original) -> std::vector<uint32_t> {
        g_evt_original = original;
        g_trace.clear();
        uint8_t* arena = g_evt_arenas[original ? 0 : 1];
        std::memset(arena, 0, kArenaSize);
        std::memset(arena + 0x8000, 0xCD, 0x100); // dirty memory for the constructors
        g_evt_base = reinterpret_cast<uint32_t>(arena);
        g_evt_bump = g_evt_base + 0x9000;
        for (Saved& saved : g_evt_saved)
            std::memset(reinterpret_cast<void*>(saved.address), 0, saved.size);
        GameVariable<bool>(0x007CC979) = true;
        auto* manager = reinterpret_cast<Mem::Manager*>(arena + 0x6000);
        manager->m_frontEndHeap = reinterpret_cast<Mem::Heap*>(0xF0E70000);
        GameVariable<void*>(0x007D1800) = manager;
        GameVariable<uint32_t>(0x006F0FC0) = 0x7AC4E400;

        auto* script = reinterpret_cast<CScript*>(arena + 0x1000);
        auto* script2 = reinterpret_cast<CScript*>(arena + 0x1100);
        g_evt_spawned = g_evt_base + 0x1200;
        auto* objectPlain = arena + 0x1400; // CObject: +0x08 its flags
        auto* objectSpawning = arena + 0x1440;
        reinterpret_cast<uint32_t*>(objectPlain)[2] = 0xFFFEFFFF;
        reinterpret_cast<uint32_t*>(objectSpawning)[2] = 0x00010000;
        auto structure = [&](int i) { return reinterpret_cast<CStruct*>(arena + 0x1800 + 0x10 * i); };
        auto table = [&](int i) { return reinterpret_cast<Table*>(arena + 0x8000 + 0x10 * i); };
        auto entry = [&](Table* t, int i) -> Entry& { return t->m_entries[i]; };
        auto dump = [&](Table* t) {
            Rec(0xD0, {reinterpret_cast<uint32_t>(t->m_entries), static_cast<uint32_t>(t->m_count), t->m_alive,
                       t->m_changed, static_cast<uint32_t>(t->m_inUse)});
            for (int i = 0; i < t->m_count; ++i) {
                Entry& e = t->m_entries[i];
                Rec(0xD1, {e.type, e.script, e.group, reinterpret_cast<uint32_t>(e.params), e.flags});
            }
        };

        // Construct (on dirty memory).
        Table* t = table(0);
        Rec(0xC0, {reinterpret_cast<uint32_t>(Construct(t))});
        dump(t);
        Print(nullptr);
        Print(t);

        // AddHandler: the first, then new ones at the end, a live type again (no tracker), a type removed by
        // group (tracker), a removed slot reused.
        Add(t, script, 0xA, 0x5A, 0x61, false, structure(0));
        Add(t, script, 0xB, 0x5B, 0x61, true, nullptr);
        Add(t, script, 0xC, 0x5C, 0x62, false, structure(1));
        Add(t, script, 0xD, 0x5D, 0x63, true, structure(2));
        dump(t);
        Add(t, script, 0xA, 0x5E, 0x62, true, structure(3));
        dump(t);
        RemoveGroup(t, 0x62);
        dump(t);
        Add(t, script, 0xC, 0x5F, 0x64, false, nullptr);
        RemoveType(t, 0xB);
        dump(t);
        Add(t, script2, 0xE, 0x60, 0x61, false, structure(4));
        Add(t, script2, 0xF, 0x70, 0x65, false, nullptr); // (no removed slot left: grows)
        dump(t);
        Print(t);

        // Tracker registration (types 0 and scripts 0 / removed skipped as the original does).
        Add(t, script, 0x17, 0x77, 0x66, false, nullptr);
        RemoveGroup(t, 0x66); // (removed, type kept)
        entry(t, 2).script = 0;
        Register(t, script);
        Unregister(t, script2);
        entry(t, 2).script = 0x5F;

        // Mark / remove marked / compact.
        Add(t, script, 0x10, 0x71, 0x61, false, nullptr);
        RemoveType(t, 0x10); // (removed: not marked)
        MarkGroup(t, 0x61);
        dump(t);
        RemoveType(t, 0xE); // (its mark stays, and it's removed: type 0)
        RemoveMarked(t, script);
        dump(t);
        t->m_changed = false;
        Compact(t);
        dump(t);
        Compact(t); // nothing removed
        dump(t);
        RemoveGroup(t, script, 0x64);
        RemoveGroup(t, script2, 0x99);
        dump(t);
        Compact(t);
        dump(t);
        RemoveGroup(t, script, Obj::kAllGroups);
        dump(t);
        Compact(t); // everything removed: the array goes
        dump(t);
        Compact(t); // no array
        dump(t);
        RemoveGroup(t, Obj::kAllGroups);

        // AddHandlers.
        auto* handlers = reinterpret_cast<FakeHandler*>(arena + 0x2000);
        auto handler = [&](int i, uint32_t type, uint32_t name, uint32_t params, bool hasGroup, uint32_t group,
                           bool exception) {
            FakeHandler& h = handlers[i];
            h.structure.m_head = &h.components[0];
            h.components[0].checksum = type;
            h.components[0].next = &h.components[1];
            h.components[1].checksum = name;
            h.params = params;
            h.hasGroup = hasGroup;
            h.group = group;
            h.exception = exception;
            return reinterpret_cast<uint32_t>(&h);
        };
        auto* elements = reinterpret_cast<uint32_t*>(arena + 0x2380);
        elements[0] = handler(0, 0x11, 0x81, reinterpret_cast<uint32_t>(structure(5)), true, 0x71, false);
        elements[1] = handler(1, 0x12, 0x82, 0, false, 0, true);
        elements[2] = handler(2, 0x13, 0x83, 0, true, 0x72, true);
        elements[3] = handler(3, 0x12, 0x84, reinterpret_cast<uint32_t>(structure(6)), false, 0, false);
        auto* arrays = reinterpret_cast<Script::CArray*>(arena + 0x2400);
        arrays[0].m_type = Script::ESYMBOLTYPE_STRUCTURE;
        arrays[0].m_size = 3;
        arrays[0].m_elements = elements;
        arrays[1].m_type = Script::ESYMBOLTYPE_STRUCTURE;
        arrays[1].m_size = 2;
        arrays[1].m_elements = elements + 2;
        arrays[2].m_size = 0;
        Table* u = table(1);
        Construct(u);
        AddArray(u, &arrays[0], false); // onto no array
        dump(u);
        Add(u, script, 0x14, 0x85, 0x73, false, structure(7));
        AddArray(u, &arrays[1], true); // replacing 0x12 and 0x13: in front
        dump(u);
        AddArray(u, &arrays[0], false);
        dump(u);
        AddArray(u, &arrays[2], false); // nothing added: a new array all the same
        dump(u);
        AddArray(u, &arrays[2], true); // empty and replace: everything goes
        dump(u);
        AddArray(u, &arrays[2], true); // (no array)
        dump(u);
        AddArray(u, &arrays[1], false);
        Destroy(u);
        Table* w = table(2);
        Construct(w);
        Destroy(w); // no array

        // PassEvent.
        auto* events = reinterpret_cast<CEvent*>(arena + 0x2800);
        CEvent& event = events[0];
        using ConstructFn = CEvent*(__thiscall*)(CEvent*, uint32_t, uint32_t, uint32_t, CStruct*);
        std::memset(events, 0xCD, 0x80);
        Rec(0xE4, {reinterpret_cast<uint32_t>(Fn<ConstructFn>(0x0045F7F0, AddressOf(&CEvent::Construct))(
                      &events[1], 0x77, 0x1D1, 0x1D2, structure(15)))});
        event.m_type = 0x21;
        event.m_target = 0x1D1;
        event.m_source = 0x1D2;
        event.m_flags = 0x10;
        event.mp_data = nullptr;
        Table* p = table(3);
        Construct(p);
        Add(p, script, 0x21, 0x91, 0x61, false, structure(8));
        Add(p, script, 0x22, 0x92, 0x61, false, nullptr);
        Add(p, script, 0x23, 0x93, 0x61, false, nullptr);
        Add(p, script, 0x24, 0x94, 0x62, true, structure(9));
        p->m_entries[1].type = 0x21;
        p->m_entries[2].type = 0x21;
        p->m_entries[2].script = Obj::kNullScript; // removed: skipped
        Pass(p, &event, script); // two interrupts (the second without params)
        dump(p);
        Rec(0xE0, {event.m_flags});
        p->m_changed = true;
        event.mp_data = structure(10);
        event.m_flags = 0;
        Pass(p, &event, script); // merged parameters; m_changed stays as it was
        dump(p);
        Rec(0xE0, {event.m_flags});
        event.m_type = 0x55;
        Pass(p, &event, script); // nothing matches
        dump(p);
        // An object: one without the flag (interrupt), one with it (a spawned script on it).
        script->m_object.mp_object = reinterpret_cast<Obj::CObject*>(objectPlain);
        event.m_type = 0x21;
        event.mp_data = nullptr;
        Pass(p, &event, script);
        script->m_object.mp_object = reinterpret_cast<Obj::CObject*>(objectSpawning);
        reinterpret_cast<CScript*>(g_evt_spawned)->m_object.mp_object = reinterpret_cast<Obj::CObject*>(objectPlain);
        Pass(p, &event, script);
        event.mp_data = structure(11);
        reinterpret_cast<CScript*>(g_evt_spawned)->m_object.mp_object = nullptr;
        Pass(p, &event, script);
        dump(p);
        // Exceptions: without an OnExceptionRun script, with one, and one that removes the handler.
        event.m_type = 0x24;
        event.mp_data = nullptr;
        Pass(p, &event, script);
        script->m_onExceptionScript = 0xE5C;
        Pass(p, &event, script);
        Rec(0xE1, {script->m_onExceptionScript});
        script->m_onExceptionScript = 0xE5D;
        event.mp_data = structure(12);
        g_evt_onInterrupt = [&](CScript*, uint32_t name) {
            if (name == 0xE5D)
                RemoveType(p, 0x24);
        };
        Pass(p, &event, script);
        dump(p);
        script->m_object.mp_object = nullptr;
        // A handler that adds a handler: the walk stops after it (m_changed).
        Add(p, script, 0x24, 0x95, 0x61, false, nullptr);
        Add(p, script, 0x25, 0x96, 0x61, false, nullptr);
        p->m_entries[2].type = 0x21;
        p->m_entries[3].type = 0x21;
        event.m_type = 0x21;
        event.mp_data = nullptr;
        g_evt_onInterrupt = [&](CScript* who, uint32_t name) {
            if (name == 0x91)
                Add(p, who, 0x26, 0x97, 0x61, false, nullptr);
        };
        Pass(p, &event, script);
        dump(p);
        // A handler that removes others (no stop: removal doesn't set m_changed).
        p->m_changed = false;
        g_evt_onInterrupt = [&](CScript* who, uint32_t name) {
            if (name == 0x91)
                RemoveGroup(p, who, 0x61);
        };
        Pass(p, &event, script);
        dump(p);
        // Nested: a handler passes another event to the same table.
        Table* q = table(4);
        Construct(q);
        Add(q, script, 0x31, 0xA1, 0x61, false, nullptr);
        Add(q, script, 0x32, 0xA2, 0x61, false, nullptr);
        Add(q, script, 0x31, 0xA3, 0x61, false, nullptr); // (replaces A1's entry)
        Add(q, script, 0x33, 0xA4, 0x61, false, nullptr);
        q->m_entries[2].type = 0x31;
        q->m_changed = false;
        CEvent& inner = events[1];
        inner.m_type = 0x32;
        event.m_type = 0x31;
        g_evt_onInterrupt = [&](CScript* who, uint32_t name) {
            Rec(0xE2, {static_cast<uint32_t>(q->m_inUse)});
            if (name == 0xA3)
                Pass(q, &inner, who);
        };
        Pass(q, &event, script);
        dump(q);
        // The script dies in a handler: nested (only the count goes down), then outermost (the table is freed).
        q->m_inUse = 1;
        g_evt_onInterrupt = [&](CScript*, uint32_t name) {
            if (name == 0xA3)
                q->m_alive = false;
        };
        Pass(q, &event, script);
        dump(q);
        q->m_alive = true;
        q->m_inUse = 0;
        Pass(q, &event, script);
        dump(q);
        g_evt_onInterrupt = nullptr;
        Table* r = table(6);
        Construct(r);
        Add(r, script, 0x41, 0xB1, 0x61, false, structure(14));
        Add(r, script, 0x42, 0xB2, 0x62, false, nullptr);
        Add(r, script, 0x43, 0xB3, 0x63, false, structure(15));
        RemoveGroup(r, 0x62);
        dump(r);
        RemoveGroup(r, Obj::kAllGroups);
        dump(r);
        Table* z = table(5);
        Construct(z);
        Pass(z, &event, script); // no entries
        dump(z);

        // CEvent.
        using MarkFn = void(__thiscall*)(CEvent*, uint32_t, uint32_t);
        Fn<MarkFn>(0x0045F9B0, AddressOf(&CEvent::MarkHandled))(&events[2], 0x1D5, 0x5C5);
        Rec(0xE3, {events[2].m_flags});
        using EventFn = void(__thiscall*)(CEvent*);
        events[3].m_flags = 0xFB; // doesn't own its data
        events[3].mp_data = structure(13);
        Fn<EventFn>(0x00460150, AddressOf(&CEvent::Destroy))(&events[3]);
        events[3].m_flags = 0x04;
        Fn<EventFn>(0x00460150, AddressOf(&CEvent::Destroy))(&events[3]);
        events[3].mp_data = nullptr;
        Fn<EventFn>(0x00460150, AddressOf(&CEvent::Destroy))(&events[3]);

        // CEventListener.
        auto* listener = reinterpret_cast<CEventListener*>(arena + 0x8100);
        std::memset(listener, 0xCD, 0x40);
        using ListenerFn = CEventListener*(__thiscall*)(CEventListener*);
        using RegisterFn = void(__thiscall*)(CEventListener*, Obj::CObject*);
        using DeleteFn = CEventListener*(__thiscall*)(CEventListener*, unsigned int);
        Rec(0xF0, {reinterpret_cast<uint32_t>(
                      Fn<ListenerFn>(0x0045FAA0, AddressOf(&CEventListener::Construct))(listener))});
        auto registerWith = Fn<RegisterFn>(0x0045FB30, AddressOf(&CEventListener::RegisterWithTracker));
        registerWith(listener, reinterpret_cast<Obj::CObject*>(objectPlain));
        registerWith(listener, reinterpret_cast<Obj::CObject*>(objectSpawning));
        registerWith(listener, nullptr);
        registerWith(listener, reinterpret_cast<Obj::CObject*>(objectPlain));
        Fn<void(__thiscall*)(CEventListener*)>(0x0045FAC0, AddressOf(&CEventListener::Destroy))(listener);
        auto deleting = Fn<DeleteFn>(0x00460170, AddressOf(&CEventListener::DeletingDestructor));
        listener->m_vtable = nullptr;
        listener->mp_object.mp_object = reinterpret_cast<Obj::CObject*>(objectSpawning);
        Rec(0xF1, {reinterpret_cast<uint32_t>(deleting(listener, 0))});
        listener->mp_object.mp_object = nullptr;
        Rec(0xF1, {reinterpret_cast<uint32_t>(deleting(listener, 0xFFFFFFF1))});
        Rec(0xF2, {reinterpret_cast<uint32_t>(deleting(listener, 2))});

        // The whole arena.
        std::vector<uint32_t> result = g_trace;
        std::vector<uint32_t> image(kArenaSize / 4);
        std::memcpy(image.data(), arena, kArenaSize);
        for (uint32_t& word : image) {
            word = Rel(word);
            uint32_t high = word >> 16;
            if (high >= (g_evt_base >> 16) && high <= ((g_evt_base + kArenaSize) >> 16))
                word = (word & 0xFFFF) | ((0xA000u + high - (g_evt_base >> 16)) << 16);
        }
        result.insert(result.end(), image.begin(), image.end());
        g_trace.clear();
        return result;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Cleanup();
    Compare(expected, actual,
            "events: the handler table (construct, add, add array, remove by type / group / mark, compact, tracker "
            "registration, print, destroy), PassEvent (interrupts, merged data, spawned on a flagged object, "
            "exceptions with OnExceptionRun, changes and deaths during a handler, nesting), CEvent, CEventListener");
}

} // namespace

SELFTEST(Early, TestEvents, Cleanup);
