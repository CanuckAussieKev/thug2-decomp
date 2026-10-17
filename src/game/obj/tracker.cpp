// The object tracker: objects by id and alias, the scripts listening for each event type, WaitForEvent, the
// event log, and LaunchEvent - how an event is delivered (docs/script-runtime.md, section 11). See tracker.h.

#include "game/obj/tracker.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

// Vtables.
void** const kScriptNodeVtable = reinterpret_cast<void**>(0x006478D8);
void** const kScriptListVtable = reinterpret_cast<void**>(0x006478DC);
void** const kTrackerVtable = reinterpret_cast<void**>(0x006478E0);

// Names.
constexpr uint32_t kId = 0x40C698AF;         // id
constexpr uint32_t kAlias = 0x1E93946B;      // alias
constexpr uint32_t kType = 0x7321A8D6;       // type
constexpr uint32_t kTarget = 0xB990D003;     // target
constexpr uint32_t kSource = 0xA075808C;     // source
constexpr uint32_t kData = 0x520C0C9C;       // data
constexpr uint32_t kBroadcast = 0x640E830A;  // broadcast
constexpr uint32_t kRadius = 0xC48391A5;     // radius
constexpr uint32_t kScripts = 0x22E168C1;    // scripts
constexpr uint32_t kScriptName = 0x6166F3AD; // script_name
constexpr uint32_t kEventNames = 0x08114F90; // the global array of event names (its name isn't known)

constexpr int kLogSize = 0x400;
constexpr int kWaiters = 40;

// The pool pair of the listener lists (CScriptList).
Mem::PoolPair& ScriptListPools() { return GameVariable<Mem::PoolPair>(0x006F0FC8); }

// --- the objects (Obj::CObject; not rewritten yet) ---
uint8_t* Bytes(CObject* object) { return reinterpret_cast<uint8_t*>(object); }
uint32_t& ObjectId(CObject* object) { return *reinterpret_cast<uint32_t*>(Bytes(object) + 0x14); }
int& AliasCount(CObject* object) { return *reinterpret_cast<int*>(Bytes(object) + 0x3C); }
// +0x08 bit 0x20: the object has a position (+0x4C).
bool HasPosition(CObject* object) { return (Bytes(object)[0x08] & 0x20) != 0; }
const float* Position(CObject* object) { return reinterpret_cast<const float*>(Bytes(object) + 0x4C); }
// Its vtable +0x04: take the properties in `params` (SetObjectProps). +0x14: an event targeted at it.
void SetProperties(CObject* object, Script::CStruct* params)
{
    void** vtable = *reinterpret_cast<void***>(object);
    reinterpret_cast<void(__thiscall*)(CObject*, Script::CStruct*)>(vtable[1])(object, params);
}
void PassEvent(CObject* object, CEvent* event)
{
    void** vtable = *reinterpret_cast<void***>(object);
    reinterpret_cast<void(__thiscall*)(CObject*, CEvent*)>(vtable[5])(object, event);
}
// 0x004619F0: the object with this id (the object manager 0x006F0FB8). -> wt/obj
CObject* FindObjectById(uint32_t id)
{
    return GameFunction<CObject*(__thiscall*)(void*, uint32_t)>(0x004619F0)(GameVariable<void*>(0x006F0FB8), id);
}
// 0x004AC120: the object id named by `name` in `params` (the screen element manager 0x00701440; also takes names
// of screen elements and aliases). False if there is none.
bool GetObjectId(Script::CStruct* params, uint32_t name, uint32_t* id)
{
    return GameFunction<bool(__thiscall*)(void*, Script::CStruct*, uint32_t, uint32_t*)>(0x004AC120)(
        GameVariable<void*>(0x00701440), params, name, id);
}

// --- the event listeners (Obj::CEventListener; not rewritten yet) ---
uint8_t* Bytes(CEventListener* listener) { return reinterpret_cast<uint8_t*>(listener); }
CEventListener*& NextListener(CEventListener* listener)
{
    return *reinterpret_cast<CEventListener**>(Bytes(listener) + 0x10);
}
bool& Registered(CEventListener* listener) { return *reinterpret_cast<bool*>(Bytes(listener) + 0x14); }
int& Busy(CEventListener* listener) { return *reinterpret_cast<int*>(Bytes(listener) + 0x18); }
// 0x004A1090: give it the event (its vtable +0x04).
void ListenerReceive(CEventListener* listener, CEvent* event)
{
    GameFunction<void(__thiscall*)(CEventListener*, CEvent*)>(0x004A1090)(listener, event);
}

// --- the event (Obj::CEvent, 0x14 bytes; its methods are wt/evt's) ---
struct SEvent {
    uint32_t type;
    uint32_t source;
    uint32_t target;
    uint32_t flags; // bit 1: handled; bit 2: owns its data
    Script::CStruct* data;
};
constexpr uint32_t EVENT_HANDLED = 0x2;
CEvent* AsEvent(SEvent* event) { return reinterpret_cast<CEvent*>(event); }
// 0x0045F7F0: the constructor (flags 0). -> wt/evt
void ConstructEvent(SEvent* event, uint32_t type, uint32_t source, uint32_t target, Script::CStruct* data)
{
    GameFunction<SEvent*(__thiscall*)(SEvent*, uint32_t, uint32_t, uint32_t, Script::CStruct*)>(0x0045F7F0)(
        event, type, source, target, data);
}
// 0x00460150: the destructor (frees the data if it owns it). -> wt/evt
void DestroyEvent(SEvent* event) { GameFunction<void(__thiscall*)(SEvent*)>(0x00460150)(event); }
// 0x0045F9B0: mark it handled by `script` (logged through LogHandled). -> wt/evt
void MarkHandled(SEvent* event, uint32_t how, uint32_t script)
{
    GameFunction<void(__thiscall*)(SEvent*, uint32_t, uint32_t)>(0x0045F9B0)(event, how, script);
}

// 0x0041A820: the squared distance between two points (x87: (dx*dx + dz*dz) + dy*dy, not rounded to float).
double DistanceSquared(const float* a, const float* b)
{
    double dx = static_cast<double>(a[0]) - b[0];
    double dy = static_cast<double>(a[1]) - b[1];
    double dz = static_cast<double>(a[2]) - b[2];
    return dx * dx + dz * dz + dy * dy;
}

// A new hash table of 2^bits buckets (null if there's no memory).
Lst::HashTable* NewHashTable(int bits)
{
    void* memory = Mem::New(sizeof(Lst::HashTable));
    return memory ? new (memory) Lst::HashTable(bits) : nullptr;
}
void DeleteHashTable(Lst::HashTable* table)
{
    if (table) {
        table->~HashTable();
        Mem::Free(table);
    }
}

void PushScriptHeap() { Mem::PushContext(Mem::Manager::Get()->m_scriptHeap); }

// Delete an empty listener list (its key `type` already gone from the table... or about to be).
void DeleteScriptList(CScriptList* list)
{
    list->Unlink(); // (the head's destructor, inlined)
    CScriptList::operator delete(list);
}

} // namespace

// --- the list of scripts ---------------------------------------------------------------------------------

// 0x00462530
void CScriptNode::Unlink()
{
    m_vtable = kScriptNodeVtable;
    mp_prev->mp_next = mp_next;
    mp_next->mp_prev = mp_prev;
    mp_next = this;
    mp_prev = this;
}

// 0x00462550
CScriptNode* CScriptNode::ScalarDelete(uint8_t flags)
{
    Unlink(); // (inlined in the original)
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00462710
CScriptList* CScriptList::ScalarDelete(uint8_t flags)
{
    Unlink();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00462620
void CScriptList::Add(Script::CScript* script)
{
    for (CScriptNode* node = mp_next->IsHead() ? nullptr : mp_next; node;) {
        if (node->mp_data == script)
            return;
        node = node->mp_next;
        if (node->IsHead())
            break;
    }
    PushScriptHeap();
    auto* node = static_cast<CScriptNode*>(Mem::Calloc(sizeof(CScriptNode)));
    if (node) {
        node->mp_data = script;
        node->m_vtable = kScriptNodeVtable;
        node->m_priority = 0;
        node->mp_next = node;
        node->mp_prev = node;
    }
    Mem::PopContext();
    // At the end (no null check).
    node->mp_prev = mp_prev;
    node->mp_next = this;
    mp_prev->mp_next = node;
    mp_prev = node;
}

// 0x004626B0
void CScriptList::Remove(Script::CScript* script)
{
    for (CScriptNode* node = mp_next->IsHead() ? nullptr : mp_next; node;) {
        if (node->mp_data == script) {
            node->mp_prev->mp_next = node->mp_next;
            node->mp_next->mp_prev = node->mp_prev;
            node->mp_next = node;
            node->mp_prev = node;
            // delete node (virtual)
            reinterpret_cast<CScriptNode*(__thiscall*)(CScriptNode*, uint8_t)>(node->m_vtable[0])(node, 1);
            return;
        }
        node = node->mp_next;
        if (node->IsHead())
            return;
    }
}

void* CScriptList::operator new(size_t size) { return ScriptListPools().Allocate(static_cast<unsigned int>(size)); }

// 0x004624D0
void CScriptList::operator delete(void* list) { ScriptListPools().Free(list); }

// --- the event log ---------------------------------------------------------------------------------------

// 0x004627C0
void CEventLog::AddEntry(uint32_t type, uint32_t target, uint32_t source, uint32_t script, uint32_t how, int kind)
{
    if (!mp_names) {
        mp_names = NewHashTable(8);
        Script::CArray* names = Script::GetArray(kEventNames);
        for (int i = 0; i < static_cast<int>(names->m_size); ++i) {
            char* name = m_names[i];
            const char* string = names->GetLocalString(i);
            std::memcpy(name, string, std::strlen(string) + 1);
            uint32_t checksum = Crc::GenerateCRCFromString(names->GetLocalString(i));
            mp_names->PutItem(checksum, name);
        }
    }
    int last = m_index - 1;
    if (last < 0)
        last = kLogSize - 1;
    if (kind == FRAME) {
        if (m_entries[last].kind == FRAME) {
            ++m_entries[last].type;
            return;
        }
    } else if (kind == HANDLED) {
        --m_depth;
    }
    Entry& entry = m_entries[m_index];
    entry.kind = kind;
    entry.type = kind == FRAME ? 0 : type;
    entry.target = target;
    entry.source = source;
    entry.script = script;
    entry.how = how;
    entry.depth = m_depth;
    if (++m_index == kLogSize) {
        m_index = 0;
        m_wrapped = true;
    }
    if (kind == LAUNCHED)
        ++m_depth;
    ++m_newEntries;
}

// 0x004620A0
void CEventLog::Print(bool onlyNew, int /*count*/)
{
    const char* const line = reinterpret_cast<const char*>(0x00647890); // "=====...\n"
    int index = 0;
    int count = m_index;
    if (m_wrapped) {
        index = m_index;
        count = kLogSize;
    }
    Dbg::Printf(line);
    Dbg::Printf(reinterpret_cast<const char*>(onlyNew ? 0x00647868   // "PRINTING EVENT LOG (ONLY NEW ENTRIES)\n\n"
                                                      : 0x00647850)); // "PRINTING EVENT LOG\n\n"
    // Each entry from the oldest (their printing is compiled out of this build).
    for (; count > 0; --count) {
        if (++index == kLogSize)
            index = 0;
    }
    Dbg::Printf(line);
    m_newEntries = 0;
}

// 0x00462CB0
void CEventLog::Destroy() { DeleteHashTable(mp_names); }

// --- the tracker -----------------------------------------------------------------------------------------

// 0x00462CF0
CTracker* CTracker::Construct()
{
    m_vtable = kTrackerVtable;
    m_log.m_index = 0;
    m_log.m_wrapped = false;
    m_log.mp_names = nullptr;
    m_log.m_depth = 0;
    m_log.m_newEntries = 0;
    mp_objects = NewHashTable(8);
    mp_aliases = NewHashTable(4);
    mp_listeners = NewHashTable(8);
    m_nextAutoId = 0;
    mp_eventListeners = nullptr;
    unknown18 = false;
    m_launchDepth = 0;
    m_launchedFromScript = 0;
    for (SWaiter& waiter : m_waiters)
        waiter.type = 0;
    unknownB218 = false;
    return this;
}

// 0x00462E40 (the listener table isn't freed - an original leak)
void CTracker::Destruct()
{
    m_vtable = kTrackerVtable;
    DeleteHashTable(mp_objects);
    m_log.Destroy();
}

// 0x00463780
CTracker* CTracker::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00462120
CObject* CTracker::LookUpObject(uint32_t id)
{
    if (auto* object = static_cast<CObject*>(mp_objects->GetItem(id)))
        return object;
    return static_cast<CObject*>(mp_aliases->GetItem(id, true));
}

// 0x00462190
CObject* CTracker::GetObjectByAlias(uint32_t alias) { return static_cast<CObject*>(mp_aliases->GetItem(alias)); }

// 0x004621D0
uint32_t CTracker::GetUniqueId()
{
    uint32_t checksum;
    do {
        char name[64];
        std::snprintf(name, sizeof(name), reinterpret_cast<const char*>(0x006478CC), m_nextAutoId++); // "autoid%d"
        if (m_nextAutoId >= 1000000)
            m_nextAutoId = 0;
        checksum = Crc::GenerateCRCFromString(name);
    } while (mp_objects->GetItem(checksum));
    return checksum;
}

// 0x00462980
void CTracker::AddObject(CObject* object)
{
    if (auto* aliased = static_cast<CObject*>(mp_aliases->GetItem(ObjectId(object)))) {
        if (AliasCount(aliased) > 0)
            --AliasCount(aliased);
        mp_aliases->FlushItem(ObjectId(object));
    }
    mp_objects->PutItem(ObjectId(object), object);
    m_log.AddEntry(0, ObjectId(object), kNoObjectId, 0, 0, CEventLog::OBJECT_ADDED);
}

// 0x00462A20
void CTracker::RemoveObject(CObject* object, uint32_t newId, bool renamed)
{
    mp_objects->FlushItem(ObjectId(object));
    if (renamed) {
        for (SWaiter& waiter : m_waiters) {
            if (waiter.objectId == ObjectId(object))
                waiter.objectId = newId;
        }
    } else {
        RemoveAliasesOf(object);
    }
    m_log.AddEntry(0, ObjectId(object), kNoObjectId, 0, 0, CEventLog::OBJECT_REMOVED);
}

// 0x00462590: from the start of the table again after each one removed.
void CTracker::RemoveAliasesOf(CObject* object)
{
    Lst::HashTable* table = mp_aliases;
    for (;;) {
        table->m_iterBucket = -1;
        table->m_iterItem = nullptr;
        if (!table->IterateNext() || !table->m_iterItem)
            return;
        uint32_t alias = table->m_iterItem->key;
        auto* aliased = static_cast<CObject*>(table->m_iterItem->value);
        for (;;) {
            if (!aliased)
                return;
            if (ObjectId(aliased) == ObjectId(object))
                break;
            if (!table->IterateNext() || !table->m_iterItem)
                return;
            alias = table->m_iterItem->key;
            aliased = static_cast<CObject*>(table->m_iterItem->value);
        }
        if (AliasCount(aliased) > 0)
            --AliasCount(aliased);
        table->FlushItem(alias);
    }
}

// 0x00462A90 (`object` isn't checked for null)
void CTracker::AddAlias(uint32_t alias, CObject* object)
{
    if (auto* aliased = static_cast<CObject*>(mp_aliases->GetItem(alias))) {
        if (AliasCount(aliased) > 0)
            --AliasCount(aliased);
        mp_aliases->FlushItem(alias);
    }
    ++AliasCount(object);
    mp_aliases->PutItem(alias, object);
}

// 0x00462000
void CTracker::RemoveEventListener(CEventListener* listener)
{
    CEventListener* previous = nullptr;
    for (CEventListener* other = mp_eventListeners; other; other = NextListener(other)) {
        if (other == listener) {
            if (previous)
                NextListener(previous) = NextListener(listener);
            else
                mp_eventListeners = NextListener(listener);
            break;
        }
        previous = other;
    }
    Registered(listener) = false;
}

// 0x00463370
void CTracker::AddListener(uint32_t type, Script::CScript* script)
{
    PushScriptHeap();
    auto* list = static_cast<CScriptList*>(mp_listeners->GetItem(type));
    if (!list) {
        list = static_cast<CScriptList*>(CScriptList::operator new(sizeof(CScriptList)));
        if (list) {
            list->mp_data = reinterpret_cast<Script::CScript*>(-1);
            list->m_priority = 0;
            list->mp_next = list;
            list->mp_prev = list;
            list->m_vtable = kScriptListVtable;
        }
        mp_listeners->PutItem(type, list); // (null too, if there was no memory - then Add crashes)
    }
    list->Add(script);
    Mem::PopContext();
}

// 0x00463450
void CTracker::RemoveListener(uint32_t type, Script::CScript* script)
{
    auto* list = static_cast<CScriptList*>(mp_listeners->GetItem(type));
    if (!list)
        return;
    list->Remove(script);
    if (list->IsEmpty()) {
        mp_listeners->FlushItem(type);
        DeleteScriptList(list);
    }
}

// 0x00462B90: walking the table, one item ahead (the current one may be removed).
void CTracker::ForgetScript(Script::CScript* script)
{
    Lst::HashTable* table = mp_listeners;
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    if (!table->IterateNext() || !table->m_iterItem)
        return;
    uint32_t type = table->m_iterItem->key;
    auto* list = static_cast<CScriptList*>(table->m_iterItem->value);
    uint32_t nextType = type;
    while (list) {
        CScriptList* next = nullptr;
        if (table->IterateNext() && table->m_iterItem) {
            nextType = table->m_iterItem->key;
            next = static_cast<CScriptList*>(table->m_iterItem->value);
        }
        list->Remove(script);
        if (list->IsEmpty()) {
            table->FlushItem(type);
            DeleteScriptList(list);
        }
        list = next;
        type = nextType;
    }
}

// 0x00462260
void CTracker::WaitForEvent(Script::CScript* script, uint32_t type)
{
    int slot = -1;
    for (int i = 0; i < kWaiters; ++i) {
        if (m_waiters[i].type == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        // A slot whose script has gone.
        for (int i = 0; i < kWaiters; ++i) {
            if (!Script::FindScriptWithUniqueId(m_waiters[i].scriptId)) {
                slot = i;
                break;
            }
        }
        if (slot < 0)
            return;
    }
    SWaiter& waiter = m_waiters[slot];
    waiter.scriptId = script->m_uniqueId;
    waiter.objectId = script->m_object ? ObjectId(script->m_object.Get()) : 0;
    waiter.type = type;
    script->Block(0);
}

// 0x00462F00
bool CTracker::LaunchEvent(uint32_t type, uint32_t target, uint32_t source, Script::CStruct* data, bool broadcast,
                           float radius)
{
    ++m_launchDepth;
    m_log.AddEntry(type, target, source, static_cast<uint32_t>(m_launchedFromScript), 0, CEventLog::LAUNCHED);
    m_launchedFromScript = 0;
    SEvent event;
    ConstructEvent(&event, type, source, target, data);

    if (!broadcast) {
        // Every event listener, then the target: an object, or else the spawned script with that id.
        for (CEventListener* listener = mp_eventListeners; listener; listener = NextListener(listener)) {
            ++Busy(listener);
            ListenerReceive(listener, AsEvent(&event));
            --Busy(listener);
        }
        uint32_t eventTarget = event.target;
        if (eventTarget != kNoObjectId && !(event.flags & EVENT_HANDLED)) {
            if (CObject* object = LookUpObject(eventTarget)) {
                PassEvent(object, AsEvent(&event));
            } else {
                Script::CScript* script = Script::FindSpawnedScriptWithId(eventTarget);
                if (!script)
                    m_log.Print(true, kLogSize);
                script->ReceiveEvent(AsEvent(&event)); // (null when there's no such script - an original crash)
            }
        }
    } else if (auto* list = static_cast<CScriptList*>(mp_listeners->GetItem(type, true))) {
        // Every script listening for the type, in the order they started listening; with a radius (>= 0), only
        // those whose object is nearer the source than that (scripts without an object, or whose object has no
        // position, always get it).
        float sourcePosition[4];
        if (radius >= 0.0f) {
            const float* position = Position(FindObjectById(source)); // (not checked for null)
            std::memcpy(sourcePosition, position, sizeof(sourcePosition));
        }
        auto inRange = [&](Script::CScript* script) {
            if (!(radius >= 0.0f))
                return true;
            CObject* object = script->m_object.Get();
            if (!object || !HasPosition(object))
                return true;
            double distance = DistanceSquared(sourcePosition, Position(object));
            return static_cast<double>(radius) * radius > distance;
        };
        int count = 0;
        CScriptNode* first = list->mp_next;
        if (!first->IsHead()) {
            for (CScriptNode* node = first; node;) {
                node = node->mp_next;
                ++count;
                if (node->IsHead())
                    break;
            }
        }
        if (count == 1) {
            Script::CScript* script = first->mp_data;
            if (inRange(script))
                script->ReceiveEvent(AsEvent(&event));
        } else {
            // Several (or none): first a smart pointer to each, so that one deleted by an earlier one's handler
            // is skipped.
            using Pointer = CSmtPtr<CRefCounted>;
            auto* scripts = static_cast<Script::CScript**>(Mem::New(static_cast<unsigned int>(count) * 4));
            Pointer* pointers = nullptr;
            if (auto* block = static_cast<uint32_t*>(Mem::New(static_cast<unsigned int>(count) * sizeof(Pointer) + 4))) {
                block[0] = static_cast<uint32_t>(count);
                pointers = reinterpret_cast<Pointer*>(block + 1);
                for (int i = 0; i < count; ++i)
                    new (&pointers[i]) Pointer();
            }
            CScriptNode* node = list->mp_next->IsHead() ? nullptr : list->mp_next;
            for (int i = 0; node; ++i) {
                scripts[i] = node->mp_data;
                pointers[i].Set(&node->mp_data->m_refCounted);
                node = node->mp_next;
                if (node->IsHead())
                    break;
            }
            for (int i = 0; i < count; ++i) {
                if (pointers[i] && inRange(scripts[i]))
                    scripts[i]->ReceiveEvent(AsEvent(&event));
            }
            if (pointers) {
                uint32_t* block = reinterpret_cast<uint32_t*>(pointers) - 1;
                for (int i = static_cast<int>(block[0]) - 1; i >= 0; --i)
                    pointers[i].Release(); // (the destructors, last first)
                Mem::Free(block);
            }
            Mem::Free(scripts);
        }
    }

    // The first WaitForEvent waiter for this type whose object is the target (or that has no object, or the
    // event has no target or is broadcast) goes on; waiters whose script has gone are dropped on the way.
    for (SWaiter& waiter : m_waiters) {
        if (waiter.type == type &&
            (waiter.objectId == target || waiter.objectId == 0 || target == kNoObjectId || broadcast)) {
            waiter.type = 0;
            if (Script::CScript* script = Script::FindScriptWithUniqueId(waiter.scriptId)) {
                script->StopWaiting();
                MarkHandled(&event, 2, script->m_scriptName);
                break;
            }
        }
    }
    if (!(event.flags & EVENT_HANDLED))
        m_log.AddEntry(event.type, event.target, event.source, 0, 3, CEventLog::HANDLED);
    --m_launchDepth;
    bool handled = (event.flags & EVENT_HANDLED) != 0;
    DestroyEvent(&event);
    return handled;
}

// 0x00462B10
void CTracker::LogHandled(CEvent* event, uint32_t how, uint32_t script)
{
    auto* e = reinterpret_cast<SEvent*>(event);
    m_log.AddEntry(e->type, e->target, e->source, script, how, CEventLog::HANDLED);
}

// 0x00462B40
void CTracker::LogRead(CEvent* event, uint32_t how, uint32_t script)
{
    auto* e = reinterpret_cast<SEvent*>(event);
    m_log.AddEntry(e->type, e->target, e->source, script, how, CEventLog::READ);
}

// 0x00462B70
void CTracker::LogFrame() { m_log.AddEntry(0, 0, 0, 0, 0, CEventLog::FRAME); }

// 0x00463730
CTracker* CreateTracker(bool create)
{
    CTracker*& tracker = Tracker();
    if (!tracker && create) {
        void* memory = Mem::Calloc(sizeof(CTracker), nullptr, 1);
        tracker = memory ? static_cast<CTracker*>(memory)->Construct() : nullptr;
    }
    ++GameVariable<int>(0x006F0FC4);
    return tracker;
}

// --- the script commands ---------------------------------------------------------------------------------

// 0x00462310: WaitForEvent type=<type>
bool __cdecl CFunc_WaitForEvent(Script::CStruct* params, Script::CScript* script)
{
    uint32_t type = 0; // (uninitialised in the original when there's no type)
    params->GetChecksum(kType, &type, 0);
    Tracker()->WaitForEvent(script, type);
    return true;
}

// 0x00462340: ObjectExists id=<object>
bool __cdecl CFunc_ObjectExists(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t id;
    if (!GetObjectId(params, kId, &id))
        return false;
    return Tracker()->LookUpObject(id) != nullptr;
}

// 0x00462380: TerminateObjectsScripts id=<object> (scripts=[names] | script_name=<name>): stop the object's
// scripts that refer to these names (no name: 0).
bool __cdecl CFunc_TerminateObjectsScripts(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t id;
    if (!GetObjectId(params, kId, &id))
        return false;
    CObject* object = Tracker()->LookUpObject(id);
    Script::CArray* scripts;
    if (params->GetArray(kScripts, &scripts, 0)) {
        for (uint32_t i = 0; i < scripts->m_size; ++i)
            Script::StopScriptsUsingObjectThatReferTo(object, scripts->GetElement(i));
    } else {
        uint32_t name = 0;
        params->GetChecksum(kScriptName, &name, 0);
        Script::StopScriptsUsingObjectThatReferTo(object, name);
    }
    return true;
}

// 0x00462440: SetObjectProps id=<object> ... (the object takes the rest; not checked for null)
bool __cdecl CFunc_SetObjectProps(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t id;
    if (!GetObjectId(params, kId, &id))
        return false;
    SetProperties(Tracker()->LookUpObject(id), params);
    return true;
}

// 0x00462480: PrintEventLog [<count>] (with a count: the whole header, not "only new entries")
bool __cdecl CFunc_PrintEventLog(Script::CStruct* params, Script::CScript* /*script*/)
{
    bool onlyNew = true;
    int count = kLogSize;
    if (params->GetInteger(0u, &count, 0))
        onlyNew = false;
    Tracker()->m_log.Print(onlyNew, count);
    return true;
}

// 0x00462C40: AssignAlias id=<object> alias=<name>
bool __cdecl CFunc_AssignAlias(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t id;
    if (!GetObjectId(params, kId, &id))
        return false;
    CTracker* tracker = Tracker();
    CObject* object = tracker->LookUpObject(id);
    uint32_t alias = 0; // (uninitialised in the original when there's no alias)
    params->GetChecksum(kAlias, &alias, 1);
    tracker->AddAlias(alias, object);
    return true;
}

// 0x004634D0: LaunchEvent / FireEvent type=<type> | type=[types] | type={types} [target=<id>] [source=<id>]
// [data={...}] [broadcast] [radius=<r>]. Target and source default to the script's object (none without one).
bool __cdecl CFunc_LaunchEvent(Script::CStruct* params, Script::CScript* script)
{
    auto ownObject = [&]() {
        return script && script->m_object ? ObjectId(script->m_object.Get()) : kNoObjectId;
    };
    uint32_t target;
    if (!GetObjectId(params, kTarget, &target))
        target = ownObject();
    uint32_t source;
    if (!GetObjectId(params, kSource, &source))
        source = ownObject();
    Script::CStruct* data = nullptr;
    params->GetStructure(kData, &data, 0);
    bool broadcast = params->ContainsFlag(kBroadcast);
    float radius = -1.0f;
    params->GetFloat(kRadius, &radius, 0);
    CTracker* tracker = Tracker();
    // Whether this launch comes from a script with a name (only for the log).
    auto fromScript = [&]() { return script && script->m_scriptName ? 1 : 0; };

    uint32_t type;
    if (params->GetChecksum(kType, &type, 0)) {
        tracker->m_launchedFromScript = fromScript();
        tracker->LaunchEvent(type, target, source, data, broadcast, radius);
        return true;
    }
    Script::CArray* types;
    if (params->GetArray(kType, &types, 0)) {
        uint32_t size = types->m_size;
        for (uint32_t i = 0; i < size; ++i) {
            tracker->m_launchedFromScript = fromScript();
            tracker->LaunchEvent(types->GetElement(i), target, source, data, broadcast, radius);
        }
        return true;
    }
    Script::CStruct* structure;
    if (params->GetStructure(kType, &structure, 0)) {
        for (Script::CComponent* component = structure->GetNextComponent(nullptr); component;
             component = structure->GetNextComponent(component)) {
            tracker->m_launchedFromScript = fromScript();
            tracker->LaunchEvent(component->checksum, target, source, data, broadcast, radius);
        }
    }
    return true;
}

} // namespace Obj

REPLACE(0x00462000, &Obj::CTracker::RemoveEventListener);
REPLACE(0x004620A0, &Obj::CEventLog::Print);
REPLACE(0x00462120, &Obj::CTracker::LookUpObject);
REPLACE(0x00462190, &Obj::CTracker::GetObjectByAlias);
REPLACE(0x004621D0, &Obj::CTracker::GetUniqueId);
REPLACE(0x00462260, &Obj::CTracker::WaitForEvent);
REPLACE(0x00462310, Obj::CFunc_WaitForEvent);
REPLACE(0x00462340, Obj::CFunc_ObjectExists);
REPLACE(0x00462380, Obj::CFunc_TerminateObjectsScripts);
REPLACE(0x00462440, Obj::CFunc_SetObjectProps);
REPLACE(0x00462480, Obj::CFunc_PrintEventLog);
REPLACE(0x004624D0, static_cast<void (*)(void*)>(&Obj::CScriptList::operator delete));
REPLACE(0x00462530, &Obj::CScriptNode::Unlink);
REPLACE(0x00462550, &Obj::CScriptNode::ScalarDelete);
REPLACE(0x00462590, &Obj::CTracker::RemoveAliasesOf);
REPLACE(0x00462620, &Obj::CScriptList::Add);
REPLACE(0x004626B0, &Obj::CScriptList::Remove);
REPLACE(0x00462710, &Obj::CScriptList::ScalarDelete);
REPLACE(0x004627C0, &Obj::CEventLog::AddEntry);
REPLACE(0x00462980, &Obj::CTracker::AddObject);
REPLACE(0x00462A20, &Obj::CTracker::RemoveObject);
REPLACE(0x00462A90, &Obj::CTracker::AddAlias);
REPLACE(0x00462B10, &Obj::CTracker::LogHandled);
REPLACE(0x00462B40, &Obj::CTracker::LogRead);
REPLACE(0x00462B70, &Obj::CTracker::LogFrame);
REPLACE(0x00462B90, &Obj::CTracker::ForgetScript);
REPLACE(0x00462C40, Obj::CFunc_AssignAlias);
REPLACE(0x00462CB0, &Obj::CEventLog::Destroy);
REPLACE(0x00462CF0, &Obj::CTracker::Construct);
REPLACE(0x00462E40, &Obj::CTracker::Destruct);
REPLACE(0x00462F00, &Obj::CTracker::LaunchEvent);
REPLACE(0x00463370, &Obj::CTracker::AddListener);
REPLACE(0x00463450, &Obj::CTracker::RemoveListener);
REPLACE(0x004634D0, Obj::CFunc_LaunchEvent);
REPLACE(0x00463730, Obj::CreateTracker);
REPLACE(0x00463780, &Obj::CTracker::ScalarDelete);
