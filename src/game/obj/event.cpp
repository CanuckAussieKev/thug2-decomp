// Events, event listeners and the scripts' event handler tables (0x0045F7F0-0x00460792). See event.h and
// docs/script-runtime.md section 11 (how an event reaches a script's handlers).

#include <cstdint>

#include "game/obj/event.h"

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"

namespace GameObjects {

namespace {

extern void** const kEventListenerVtable;

using Scripts::CScript;
using Scripts::CStruct;

// Handler structure components (AddHandlers): `params`, `group`, `exception`.
constexpr uint32_t kParamsName = 0x7031F10C;
constexpr uint32_t kGroupName = 0x923FBB3A;
constexpr uint32_t kExceptionFlag = 0x80367192;

// The tracker (0x006F0FC0, tracker.h): which scripts listen for which event types, the event listeners, the
// event log.
void TrackerAddListener(uint32_t type, CScript* script) { Tracker()->AddListener(type, script); }
void TrackerRemoveListener(uint32_t type, CScript* script) { Tracker()->RemoveListener(type, script); }
void TrackerRegisterListener(CSignalReceiver* listener)
{
    Tracker()->SubscribeReceiver(listener);
}
void TrackerUnregisterListener(CSignalReceiver* listener) { Tracker()->RemoveEventListener(listener); }
void TrackerLogEventHandled(CSignal* event, uint32_t how, uint32_t script) { Tracker()->LogHandled(event, how, script); }
void TrackerLogEventRead(CSignal* event, uint32_t how, uint32_t script) { Tracker()->LogRead(event, how, script); }

// `new CStruct` / `new CStruct(*source)` (null if the pool is full) and `delete structure`.
CStruct* NewEmptyStruct()
{
    CStruct* structure = Scripts::NewStruct();
    return structure ? structure->Init() : nullptr;
}
CStruct* NewStructCopy(CStruct* source)
{
    CStruct* structure = Scripts::NewStruct();
    return structure ? structure->InitCopy(source) : nullptr;
}
void FreeStruct(CStruct* structure)
{
    structure->Clear();
    Scripts::DeleteStruct(structure);
}
// An entry's parameters go (and the pointer is cleared).
void FreeParams(CSignalRouteTable::Entry& entry)
{
    if (entry.params) {
        FreeStruct(entry.params);
        entry.params = nullptr;
    }
}

} // namespace

// 0x0045F7F0
CSignal* CSignal::Construct(uint32_t type, uint32_t source, uint32_t target, CStruct* data)
{
    m_type = type;
    m_source = source;
    m_aimPoint = target;
    m_flags = 0;
    m_payload = data;
    return this;
}

// 0x0045F980
void CSignal::SetEventConsumed(uint32_t how, uint32_t script)
{
    TrackerLogEventRead(this, how, script);
    m_flags |= FLAG_READ;
}

// 0x0045F9B0
void CSignal::SetEventHandled(uint32_t how, uint32_t script)
{
    TrackerLogEventHandled(this, how, script);
    m_flags |= FLAG_HANDLED;
}

// 0x0045F9E0
int __cdecl EventController(CSignal* event)
{
    int controller = 0;
    if (event->m_payload)
        event->m_payload->FetchIntValue(0xB30D9965, &controller, 0); // "controller"
    return controller;
}

// 0x00460150
void CSignal::Destroy()
{
    if ((m_flags & FLAG_OWNS_DATA) && m_payload)
        FreeStruct(m_payload);
}

// 0x0045FAA0
CSignalReceiver* CSignalReceiver::Construct()
{
    m_vtable = kEventListenerVtable;
    m_ownerObject.m_preceding = nullptr;
    m_ownerObject.m_following = nullptr;
    m_ownerObject.m_ownerObject = nullptr;
    m_onBoard = false;
    m_busy = 0;
    return this;
}

// 0x0045FAC0
void CSignalReceiver::Destroy()
{
    m_vtable = kEventListenerVtable;
    TrackerUnregisterListener(this);
    m_ownerObject.Release();
}

// 0x00460170
CSignalReceiver* CSignalReceiver::DeletingDestructor(unsigned int flags)
{
    Destroy();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0045FB30
void CSignalReceiver::EnterWaitBoard(CObject* object)
{
    m_ownerObject.Set(object);
    TrackerRegisterListener(this);
}

// 0x0045F820
CSignalRouteTable* CSignalRouteTable::Construct()
{
    m_count = 0;
    m_entries = nullptr;
    m_alive = true;
    m_inUse = 0;
    m_changed = false;
    return this;
}

// 0x0045FB70
void CSignalRouteTable::Destroy()
{
    if (!m_entries)
        return;
    for (int i = 0; i < m_count; ++i) {
        if (CStruct* params = m_entries[i].params)
            FreeStruct(params);
    }
    MemSys::Manager::Get()->Free(m_entries);
}

// 0x0045F840
void CSignalRouteTable::MarkGroup(uint32_t group)
{
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = m_entries[i];
        if (entry.group == group && entry.script != kNullScript)
            entry.flags |= Entry::FLAG_MARKED;
    }
}

// 0x0045F880
int CSignalRouteTable::PrintTable(CSignalRouteTable* table)
{
    DebugOut::Printf("====================================\n");
    DebugOut::Printf("Event Handler Table:\n");
    if (!table) {
        DebugOut::Printf("  Emtpy\n");
    } else {
        for (int i = 0; i < table->m_count; ++i) {
            Entry& entry = table->m_entries[i];
            DebugOut::Printf("------------------------------------\n");
            DebugOut::Printf("  Type     : %s\n", Scripts::FindChecksumNameOrUnknown(entry.type));
            DebugOut::Printf("  Script   : %s\n", Scripts::FindChecksumNameOrUnknown(entry.script));
            DebugOut::Printf("  Group    : %s\n", Scripts::FindChecksumNameOrUnknown(entry.group));
            DebugOut::Printf("  Exception: %s\n", (entry.flags & Entry::FLAG_EXCEPTION) ? "yes" : "no");
            if (entry.params) {
                DebugOut::Printf("  Params   :\n");
                Scripts::DumpToLog(entry.params, 13);
            }
        }
    }
    return DebugOut::Printf("====================================\n");
}

// 0x0045FA10
void CSignalRouteTable::EnterWaitBoard(CScript* script)
{
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = m_entries[i];
        if (entry.type && entry.script && entry.script != kNullScript)
            TrackerAddListener(entry.type, script);
    }
}

// 0x0045FA60 (removed entries included, as long as they still have a type)
void CSignalRouteTable::UnregisterWithTracker(CScript* script)
{
    for (int i = 0; i < m_count; ++i) {
        if (uint32_t type = m_entries[i].type)
            TrackerRemoveListener(type, script);
    }
}

// 0x0045FBC0
void CSignalRouteTable::AddHandler(CScript* script, uint32_t type, uint32_t scriptName, uint32_t group,
                                    bool exception, CStruct* params)
{
    bool tellTracker = true;
    Entry* entry = nullptr;
    if (!m_entries) {
        m_count = 1;
        m_entries = static_cast<Entry*>(MemSys::New(sizeof(Entry)));
        entry = m_entries;
    } else {
        // The entry of that type (live or removed by group): reused; the tracker already knows a live one.
        for (int i = 0; i < m_count; ++i) {
            if (m_entries[i].type == type) {
                entry = &m_entries[i];
                if (entry->params)
                    FreeStruct(entry->params); // (the pointer is overwritten below)
                tellTracker = entry->script == kNullScript;
                break;
            }
        }
        // Else the first removed entry.
        if (!entry) {
            for (int i = 0; i < m_count; ++i) {
                if (m_entries[i].script == kNullScript) {
                    entry = &m_entries[i];
                    break;
                }
            }
        }
        // Else a bigger array (on the current heap), the new entry at the end.
        if (!entry) {
            Entry* old = m_entries;
            m_entries = static_cast<Entry*>(MemSys::New((m_count + 1) * sizeof(Entry)));
            for (int i = 0; i < m_count; ++i)
                m_entries[i] = old[i];
            MemSys::Manager::Get()->Free(old);
            entry = &m_entries[m_count++];
        }
    }
    entry->flags = static_cast<uint8_t>((entry->flags & ~(Entry::FLAG_EXCEPTION | Entry::FLAG_MARKED)) |
                                        (static_cast<uint8_t>(exception) & Entry::FLAG_EXCEPTION));
    entry->group = group;
    entry->params = params ? NewStructCopy(params) : nullptr;
    entry->script = scriptName;
    entry->type = type;
    m_changed = true;
    if (tellTracker)
        TrackerAddListener(type, script);
}

// 0x0045FDF0
void CSignalRouteTable::RemoveType(uint32_t type)
{
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = m_entries[i];
        if (entry.type == type) {
            entry.type = 0;
            entry.script = kNullScript;
            FreeParams(entry);
        }
    }
}

// 0x0045FE60
void CSignalRouteTable::RemoveGroup(uint32_t group)
{
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = m_entries[i];
        if (entry.group == group || group == kAllGroups) {
            entry.script = kNullScript;
            FreeParams(entry);
        }
    }
}

// 0x004600E0 (already removed entries are told to the tracker again)
void CSignalRouteTable::RemoveGroup(CScript* script, uint32_t group)
{
    Entry* entries = m_entries;
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = entries[i];
        if (entry.group == group || group == kAllGroups) {
            if (entry.type)
                TrackerRemoveListener(entry.type, script);
            entry.script = kNullScript;
            FreeParams(entry);
        }
    }
}

// 0x0045FED0 (the mark stays)
void CSignalRouteTable::RemoveMarked(CScript* script)
{
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = m_entries[i];
        if (entry.flags & Entry::FLAG_MARKED) {
            if (entry.type)
                TrackerRemoveListener(entry.type, script);
            entry.script = kNullScript;
            FreeParams(entry);
        }
    }
}

// 0x0045FF50
void CSignalRouteTable::Compact()
{
    if (!m_entries || m_count <= 0)
        return;
    int removed = 0;
    for (int i = 0; i < m_count; ++i) {
        if (m_entries[i].script == kNullScript)
            ++removed;
    }
    if (!removed)
        return;
    int live = m_count - removed;
    m_changed = true;
    if (live == 0) {
        for (int i = 0; i < m_count; ++i)
            FreeParams(m_entries[i]);
        MemSys::Manager::Get()->Free(m_entries);
        m_entries = nullptr;
        m_count = 0;
        return;
    }
    MemSys::Manager::Get()->EnterHeapScope(MemSys::Manager::Get()->m_frontEndHeap);
    auto* entries = static_cast<Entry*>(MemSys::New(live * sizeof(Entry)));
    MemSys::Manager::Get()->LeaveHeapScope();
    Entry* out = entries;
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = m_entries[i];
        if (entry.script != kNullScript)
            *out++ = entry;
        else
            FreeParams(entry);
    }
    MemSys::Manager::Get()->Free(m_entries);
    m_count = live;
    m_entries = entries;
}

// 0x004601A0
void CSignalRouteTable::AddHandlers(Scripts::CArray* handlers, bool replace)
{
    int count = static_cast<int>(handlers->m_size);
    if (replace && count == 0) {
        // An empty array: every handler goes at once (the tracker isn't told; the caller has unregistered).
        if (!m_entries)
            return;
        for (int i = 0; i < m_count; ++i) {
            if (CStruct* params = m_entries[i].params)
                FreeStruct(params);
        }
        MemSys::Manager::Get()->Free(m_entries);
        m_entries = nullptr;
        m_count = 0;
        m_changed = true;
        return;
    }
    MemSys::Manager::Get()->EnterHeapScope(MemSys::Manager::Get()->m_frontEndHeap);
    auto* entries = static_cast<Entry*>(MemSys::New((m_count + count) * sizeof(Entry)));
    int used = 0;
    for (int i = 0; i < count; ++i) {
        auto* handler = reinterpret_cast<CStruct*>(handlers->GetElement(static_cast<uint32_t>(i)));
        Scripts::CComponent* type = handler->StepToEntry(nullptr);
        Scripts::CComponent* script = handler->StepToEntry(type);
        if (replace)
            RemoveType(type->checksum);
        Entry& entry = entries[used++];
        CStruct* params;
        entry.params = handler->ReadSubStruct(kParamsName, &params, 0) ? NewStructCopy(params) : nullptr;
        if (!handler->ReadHashValue(kGroupName, &entry.group, 0))
            entry.group = kDefaultGroup;
        entry.flags = static_cast<uint8_t>((entry.flags & ~(Entry::FLAG_EXCEPTION | Entry::FLAG_MARKED)) |
                                           (handler->HasBareFlag(kExceptionFlag) & Entry::FLAG_EXCEPTION));
        entry.type = type->checksum;
        entry.script = script->checksum;
        m_changed = true;
    }
    // The old entries after the new ones.
    if (m_entries) {
        for (int i = 0; i < m_count; ++i)
            entries[used++] = m_entries[i];
    }
    MemSys::Manager::Get()->Free(m_entries);
    m_entries = entries;
    m_count = used;
    MemSys::Manager::Get()->LeaveHeapScope();
}

// 0x00460470
void CSignalRouteTable::PassEvent(CSignal* event, CScript* script)
{
    bool wasChanged = m_changed;
    m_changed = false;
    ++m_inUse;
    // (Read once: if a handler adds entries the array moves, m_changed is set and the loop stops after it.)
    Entry* entry = m_entries;
    for (int i = 0; i < m_count; ++i, ++entry) {
        if (entry->type == event->m_type && entry->script != kNullScript) {
            // The event's data is added over the handler's own parameters, in a structure of its own.
            CStruct* merged = nullptr;
            CStruct* params;
            if (event->m_payload) {
                merged = NewEmptyStruct();
                if (entry->params)
                    merged->MergeStructure(entry->params);
                merged->MergeStructure(event->m_payload);
                params = merged;
            } else {
                params = entry->params;
            }

            if (entry->flags & Entry::FLAG_EXCEPTION) {
                // An exception: first the script's OnExceptionRun script (once) as an interrupt, given an
                // unnamed checksum 1 (the exception bit); then the handler replaces what the script runs.
                if (script->GetOnExceptionScript()) {
                    CStruct* exceptionParams = NewEmptyStruct();
                    exceptionParams->AddComponentOfType(0, Scripts::QBVAL_NAME, entry->flags & Entry::FLAG_EXCEPTION);
                    uint32_t onException = script->GetOnExceptionScript();
                    script->SetOnExceptionScript(0);
                    script->Interrupt(onException, exceptionParams);
                    if (exceptionParams)
                        FreeStruct(exceptionParams);
                }
                // (The entry may have been removed by the OnExceptionRun script - or its array freed [bug].)
                if (entry->script != kNullScript) {
                    script->BindScript(entry->script, params, script->m_object.Get());
                    script->Update();
                }
            } else if (CObject* object = script->m_object.Get();
                       object && (object->m_objectFlags & GameObjects::OBJECT_FLAG_SPAWN_HANDLERS)) {
                // A new spawned script on the object, run at once (a C function as handler returns no script:
                // null is used [bug: crash]).
                CScript* spawned = Scripts::SpawnScript(entry->script, params, 0, nullptr, 0, 0, false, false, false,
                                                       false);
                spawned->m_object.Set(script->m_object.Get());
                spawned->Update();
            } else {
                script->Interrupt(entry->script, params);
            }

            if (merged)
                FreeStruct(merged);
            // (entry->script is read again: a stale entry if the handler changed the table [bug, harmless].)
            event->SetEventHandled(0, entry->script);
        }
        if (!m_alive) {
            // The script was destroyed by a handler: the last PassEvent out frees the table.
            if (--m_inUse == 0) {
                Destroy();
                MemSys::Manager::Get()->Free(this);
            }
            return;
        }
        if (m_changed) {
            --m_inUse;
            return;
        }
    }
    m_changed = wasChanged;
    --m_inUse;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kEventListenerVtable_slots[] = {&GameObjects::CSignalReceiver::DeletingDestructor,
         &_purecall};
void** const kEventListenerVtable = reinterpret_cast<void**>(kEventListenerVtable_slots);

} // namespace
} // namespace Obj
