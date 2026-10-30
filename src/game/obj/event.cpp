// Events, event listeners and the scripts' event handler tables (0x0045F7F0-0x00460792). See event.h and
// docs/script-runtime.md section 11 (how an event reaches a script's handlers).

#include "game/obj/event.h"

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

using Script::CScript;
using Script::CStruct;

// Handler structure components (AddHandlers): `params`, `group`, `exception`.
constexpr uint32_t kParamsName = 0x7031F10C;
constexpr uint32_t kGroupName = 0x923FBB3A;
constexpr uint32_t kExceptionFlag = 0x80367192;


// The tracker (0x006F0FC0, tracker.h): which scripts listen for which event types, the event listeners, the
// event log.
void TrackerAddListener(uint32_t type, CScript* script) { Tracker()->AddListener(type, script); }
void TrackerRemoveListener(uint32_t type, CScript* script) { Tracker()->RemoveListener(type, script); }
void TrackerRegisterListener(CEventListener* listener)
{
    Tracker()->RegisterEventListener(listener);
}
void TrackerUnregisterListener(CEventListener* listener) { Tracker()->RemoveEventListener(listener); }
void TrackerLogEventHandled(CEvent* event, uint32_t how, uint32_t script) { Tracker()->LogHandled(event, how, script); }
void TrackerLogEventRead(CEvent* event, uint32_t how, uint32_t script) { Tracker()->LogRead(event, how, script); }

// `new CStruct` / `new CStruct(*source)` (null if the pool is full) and `delete structure`.
CStruct* NewEmptyStruct()
{
    CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
CStruct* NewStructCopy(CStruct* source)
{
    CStruct* structure = Script::NewStruct();
    return structure ? structure->InitCopy(source) : nullptr;
}
void FreeStruct(CStruct* structure)
{
    structure->Clear();
    Script::DeleteStruct(structure);
}
// An entry's parameters go (and the pointer is cleared).
void FreeParams(CEventHandlerTable::Entry& entry)
{
    if (entry.params) {
        FreeStruct(entry.params);
        entry.params = nullptr;
    }
}

} // namespace

// 0x0045F7F0
CEvent* CEvent::Construct(uint32_t type, uint32_t source, uint32_t target, CStruct* data)
{
    m_type = type;
    m_source = source;
    m_target = target;
    m_flags = 0;
    mp_data = data;
    return this;
}

// 0x0045F980
void CEvent::MarkRead(uint32_t how, uint32_t script)
{
    TrackerLogEventRead(this, how, script);
    m_flags |= FLAG_READ;
}

// 0x0045F9B0
void CEvent::MarkHandled(uint32_t how, uint32_t script)
{
    TrackerLogEventHandled(this, how, script);
    m_flags |= FLAG_HANDLED;
}

// 0x0045F9E0
int __cdecl EventController(CEvent* event)
{
    int controller = 0;
    if (event->mp_data)
        event->mp_data->GetInteger(0xB30D9965, &controller, 0); // "controller"
    return controller;
}

// 0x00460150
void CEvent::Destroy()
{
    if ((m_flags & FLAG_OWNS_DATA) && mp_data)
        FreeStruct(mp_data);
}

// 0x0045FAA0
CEventListener* CEventListener::Construct()
{
    m_vtable = reinterpret_cast<void**>(kVtable);
    mp_object.mp_prev = nullptr;
    mp_object.mp_next = nullptr;
    mp_object.mp_object = nullptr;
    m_registered = false;
    m_busy = 0;
    return this;
}

// 0x0045FAC0
void CEventListener::Destroy()
{
    m_vtable = reinterpret_cast<void**>(kVtable);
    TrackerUnregisterListener(this);
    mp_object.Release();
}

// 0x00460170
CEventListener* CEventListener::DeletingDestructor(unsigned int flags)
{
    Destroy();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0045FB30
void CEventListener::RegisterWithTracker(CObject* object)
{
    mp_object.Set(object);
    TrackerRegisterListener(this);
}

// 0x0045F820
CEventHandlerTable* CEventHandlerTable::Construct()
{
    m_count = 0;
    m_entries = nullptr;
    m_alive = true;
    m_inUse = 0;
    m_changed = false;
    return this;
}

// 0x0045FB70
void CEventHandlerTable::Destroy()
{
    if (!m_entries)
        return;
    for (int i = 0; i < m_count; ++i) {
        if (CStruct* params = m_entries[i].params)
            FreeStruct(params);
    }
    Mem::Manager::Get()->Free(m_entries);
}

// 0x0045F840
void CEventHandlerTable::MarkGroup(uint32_t group)
{
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = m_entries[i];
        if (entry.group == group && entry.script != kNullScript)
            entry.flags |= Entry::FLAG_MARKED;
    }
}

// 0x0045F880
int CEventHandlerTable::PrintTable(CEventHandlerTable* table)
{
    Dbg::Printf("====================================\n");
    Dbg::Printf("Event Handler Table:\n");
    if (!table) {
        Dbg::Printf("  Emtpy\n");
    } else {
        for (int i = 0; i < table->m_count; ++i) {
            Entry& entry = table->m_entries[i];
            Dbg::Printf("------------------------------------\n");
            Dbg::Printf("  Type     : %s\n", Script::FindChecksumNameOrUnknown(entry.type));
            Dbg::Printf("  Script   : %s\n", Script::FindChecksumNameOrUnknown(entry.script));
            Dbg::Printf("  Group    : %s\n", Script::FindChecksumNameOrUnknown(entry.group));
            Dbg::Printf("  Exception: %s\n", (entry.flags & Entry::FLAG_EXCEPTION) ? "yes" : "no");
            if (entry.params) {
                Dbg::Printf("  Params   :\n");
                Script::PrintContents(entry.params, 13);
            }
        }
    }
    return Dbg::Printf("====================================\n");
}

// 0x0045FA10
void CEventHandlerTable::RegisterWithTracker(CScript* script)
{
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = m_entries[i];
        if (entry.type && entry.script && entry.script != kNullScript)
            TrackerAddListener(entry.type, script);
    }
}

// 0x0045FA60 (removed entries included, as long as they still have a type)
void CEventHandlerTable::UnregisterWithTracker(CScript* script)
{
    for (int i = 0; i < m_count; ++i) {
        if (uint32_t type = m_entries[i].type)
            TrackerRemoveListener(type, script);
    }
}

// 0x0045FBC0
void CEventHandlerTable::AddHandler(CScript* script, uint32_t type, uint32_t scriptName, uint32_t group,
                                    bool exception, CStruct* params)
{
    bool tellTracker = true;
    Entry* entry = nullptr;
    if (!m_entries) {
        m_count = 1;
        m_entries = static_cast<Entry*>(Mem::New(sizeof(Entry)));
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
            m_entries = static_cast<Entry*>(Mem::New((m_count + 1) * sizeof(Entry)));
            for (int i = 0; i < m_count; ++i)
                m_entries[i] = old[i];
            Mem::Manager::Get()->Free(old);
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
void CEventHandlerTable::RemoveType(uint32_t type)
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
void CEventHandlerTable::RemoveGroup(uint32_t group)
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
void CEventHandlerTable::RemoveGroup(CScript* script, uint32_t group)
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
void CEventHandlerTable::RemoveMarked(CScript* script)
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
void CEventHandlerTable::Compact()
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
        Mem::Manager::Get()->Free(m_entries);
        m_entries = nullptr;
        m_count = 0;
        return;
    }
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_frontEndHeap);
    auto* entries = static_cast<Entry*>(Mem::New(live * sizeof(Entry)));
    Mem::Manager::Get()->PopContext();
    Entry* out = entries;
    for (int i = 0; i < m_count; ++i) {
        Entry& entry = m_entries[i];
        if (entry.script != kNullScript)
            *out++ = entry;
        else
            FreeParams(entry);
    }
    Mem::Manager::Get()->Free(m_entries);
    m_count = live;
    m_entries = entries;
}

// 0x004601A0
void CEventHandlerTable::AddHandlers(Script::CArray* handlers, bool replace)
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
        Mem::Manager::Get()->Free(m_entries);
        m_entries = nullptr;
        m_count = 0;
        m_changed = true;
        return;
    }
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_frontEndHeap);
    auto* entries = static_cast<Entry*>(Mem::New((m_count + count) * sizeof(Entry)));
    int used = 0;
    for (int i = 0; i < count; ++i) {
        auto* handler = reinterpret_cast<CStruct*>(handlers->GetElement(static_cast<uint32_t>(i)));
        Script::CComponent* type = handler->GetNextComponent(nullptr);
        Script::CComponent* script = handler->GetNextComponent(type);
        if (replace)
            RemoveType(type->checksum);
        Entry& entry = entries[used++];
        CStruct* params;
        entry.params = handler->GetStructure(kParamsName, &params, 0) ? NewStructCopy(params) : nullptr;
        if (!handler->GetChecksum(kGroupName, &entry.group, 0))
            entry.group = kDefaultGroup;
        entry.flags = static_cast<uint8_t>((entry.flags & ~(Entry::FLAG_EXCEPTION | Entry::FLAG_MARKED)) |
                                           (handler->ContainsFlag(kExceptionFlag) & Entry::FLAG_EXCEPTION));
        entry.type = type->checksum;
        entry.script = script->checksum;
        m_changed = true;
    }
    // The old entries after the new ones.
    if (m_entries) {
        for (int i = 0; i < m_count; ++i)
            entries[used++] = m_entries[i];
    }
    Mem::Manager::Get()->Free(m_entries);
    m_entries = entries;
    m_count = used;
    Mem::Manager::Get()->PopContext();
}

// 0x00460470
void CEventHandlerTable::PassEvent(CEvent* event, CScript* script)
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
            if (event->mp_data) {
                merged = NewEmptyStruct();
                if (entry->params)
                    merged->AppendStructure(entry->params);
                merged->AppendStructure(event->mp_data);
                params = merged;
            } else {
                params = entry->params;
            }

            if (entry->flags & Entry::FLAG_EXCEPTION) {
                // An exception: first the script's OnExceptionRun script (once) as an interrupt, given an
                // unnamed checksum 1 (the exception bit); then the handler replaces what the script runs.
                if (script->GetOnExceptionScript()) {
                    CStruct* exceptionParams = NewEmptyStruct();
                    exceptionParams->AddComponentOfType(0, Script::ESYMBOLTYPE_NAME, entry->flags & Entry::FLAG_EXCEPTION);
                    uint32_t onException = script->GetOnExceptionScript();
                    script->SetOnExceptionScript(0);
                    script->Interrupt(onException, exceptionParams);
                    if (exceptionParams)
                        FreeStruct(exceptionParams);
                }
                // (The entry may have been removed by the OnExceptionRun script - or its array freed [bug].)
                if (entry->script != kNullScript) {
                    script->SetScript(entry->script, params, script->m_object.Get());
                    script->Update();
                }
            } else if (CObject* object = script->m_object.Get();
                       object && (object->m_objectFlags & Obj::OBJECT_FLAG_SPAWN_HANDLERS)) {
                // A new spawned script on the object, run at once (a C function as handler returns no script:
                // null is used [bug: crash]).
                CScript* spawned = Script::SpawnScript(entry->script, params, 0, nullptr, 0, 0, false, false, false,
                                                       false);
                spawned->m_object.Set(script->m_object.Get());
                spawned->Update();
            } else {
                script->Interrupt(entry->script, params);
            }

            if (merged)
                FreeStruct(merged);
            // (entry->script is read again: a stale entry if the handler changed the table [bug, harmless].)
            event->MarkHandled(0, entry->script);
        }
        if (!m_alive) {
            // The script was destroyed by a handler: the last PassEvent out frees the table.
            if (--m_inUse == 0) {
                Destroy();
                Mem::Manager::Get()->Free(this);
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

REPLACE(0x0045F7F0, &Obj::CEvent::Construct);
REPLACE(0x0045F980, &Obj::CEvent::MarkRead);
REPLACE(0x0045F9B0, &Obj::CEvent::MarkHandled);
REPLACE(0x0045F9E0, Obj::EventController);
REPLACE(0x00460150, &Obj::CEvent::Destroy);
REPLACE(0x0045FAA0, &Obj::CEventListener::Construct);
REPLACE(0x0045FAC0, &Obj::CEventListener::Destroy);
REPLACE(0x00460170, &Obj::CEventListener::DeletingDestructor);
REPLACE(0x0045FB30, &Obj::CEventListener::RegisterWithTracker);
REPLACE(0x0045F820, &Obj::CEventHandlerTable::Construct);
REPLACE(0x0045FB70, &Obj::CEventHandlerTable::Destroy);
REPLACE(0x0045F840, &Obj::CEventHandlerTable::MarkGroup);
REPLACE(0x0045F880, Obj::CEventHandlerTable::PrintTable);
REPLACE(0x0045FA10, &Obj::CEventHandlerTable::RegisterWithTracker);
REPLACE(0x0045FA60, &Obj::CEventHandlerTable::UnregisterWithTracker);
REPLACE(0x0045FBC0, &Obj::CEventHandlerTable::AddHandler);
REPLACE(0x0045FDF0, &Obj::CEventHandlerTable::RemoveType);
REPLACE(0x0045FE60, static_cast<void (Obj::CEventHandlerTable::*)(uint32_t)>(&Obj::CEventHandlerTable::RemoveGroup));
REPLACE(0x004600E0, static_cast<void (Obj::CEventHandlerTable::*)(Script::CScript*, uint32_t)>(
                        &Obj::CEventHandlerTable::RemoveGroup));
REPLACE(0x0045FED0, &Obj::CEventHandlerTable::RemoveMarked);
REPLACE(0x0045FF50, &Obj::CEventHandlerTable::Compact);
REPLACE(0x004601A0, &Obj::CEventHandlerTable::AddHandlers);
REPLACE(0x00460470, &Obj::CEventHandlerTable::PassEvent);
