// The object tracker: objects by id and alias, the scripts listening for each event type, WaitForEvent, the
// event log, and LaunchEvent - how an event is delivered (docs/script-runtime.md, section 11). See tracker.h.

#include "engine/objects/tracker.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "frontend/screen_element_manager.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/objects/compositemgr.h"
#include "game/park_editing/piece_menu.h"
#include "engine/core/code_address.h"
#include "engine/gfx/weather.h"

namespace GameObjects {

GameObjects::CEventWaitBoard* g_tracker;
MemSys::PoolPair g_scriptListPools;

namespace {

// Vtables.
extern void** const kScriptNodeVtable;
extern void** const kScriptListVtable;
extern void** const kTrackerVtable;

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
MemSys::PoolPair& ScriptListPools() { return g_scriptListPools; }

// --- the objects ---
uint32_t& ObjectId(CEntity* object) { return object->m_ident; }
// The tracker's aliases of it (compared as a signed count by the original).
bool HasAliases(const CEntity* object) { return static_cast<int>(object->m_aliasCount) > 0; }
bool HasPosition(const CEntity* object) { return (object->m_objectFlags & OBJECT_FLAG_HAS_POSITION) != 0; }
// The position: CGameEntity's m_atPoint (+0x4C, just after CEntity's; read on any object).
const float* Position(CEntity* object) { return &static_cast<CGameEntity*>(object)->m_atPoint.x; }
// Its vtable +0x04: take the properties in `params` (SetObjectProps). +0x14: an event targeted at it.
void ChangeTraits(CEntity* object, Scripts::CStruct* params)
{
    void** vtable = *reinterpret_cast<void***>(object);
    reinterpret_cast<void(__thiscall*)(CEntity*, Scripts::CStruct*)>(vtable[1])(object, params);
}
void ForwardTrigger(CEntity* object, CSignal* event)
{
    void** vtable = *reinterpret_cast<void***>(object);
    reinterpret_cast<void(__thiscall*)(CEntity*, CSignal*)>(vtable[5])(object, event);
}
// The object with this id (the object manager 0x006F0FB8's LookUpEntity).
CEntity* FindObjectById(uint32_t id) { return reinterpret_cast<CObjectHubBase*>(g_compositeRegistry)->LookUpEntity(id); }
// 0x004AC120: the object id named by `name` in `params` (the screen element manager 0x00701440; also takes names
// of screen elements and aliases). False if there is none.
bool GetObjectId(Scripts::CStruct* params, uint32_t name, uint32_t* id)
{
    return reinterpret_cast<MenuSystem::CUiTree*>(MenuSystem::g_uiElementHub)->ResolveId(params, name, id);
}

// 0x004A1090: give an event listener the event (its vtable +0x04, the subclass's filter).
void ListenerReceive(CSignalReceiver* listener, CSignal* event)
{
    reinterpret_cast<void(__thiscall*)(CSignalReceiver*, CSignal*)>(&Engine3D::VirtualSlot1Jump)(listener, event);
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
ListKit::KeyMap* NewHashTable(int bits)
{
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    return memory ? new (memory) ListKit::KeyMap(bits) : nullptr;
}
void DeleteHashTable(ListKit::KeyMap* table)
{
    if (table) {
        table->~KeyMap();
        MemSys::Dealloc(table);
    }
}

void PushScriptHeap() { MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_scriptHeap); }

// Delete an empty listener list (its key `type` already gone from the table... or about to be).
void DeleteScriptList(CScriptList* list)
{
    list->Uncouple(); // (the head's destructor, inlined)
    CScriptList::operator delete(list);
}

} // namespace

// --- the list of scripts ---------------------------------------------------------------------------------

// 0x00462530
void CScriptNode::Uncouple()
{
    m_vtable = kScriptNodeVtable;
    m_preceding->m_following = m_following;
    m_following->m_preceding = m_preceding;
    m_following = this;
    m_preceding = this;
}

// 0x00462550
CScriptNode* CScriptNode::ScalarDelete(uint8_t flags)
{
    Uncouple(); // (inlined in the original)
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00462710
CScriptList* CScriptList::ScalarDelete(uint8_t flags)
{
    Uncouple();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00462620
void CScriptList::AppendOne(Scripts::CScript* script)
{
    for (CScriptNode* node = m_following->IsHead() ? nullptr : m_following; node;) {
        if (node->m_payload == script)
            return;
        node = node->m_following;
        if (node->IsHead())
            break;
    }
    PushScriptHeap();
    auto* node = static_cast<CScriptNode*>(MemSys::AllocZeroed(sizeof(CScriptNode)));
    if (node) {
        node->m_payload = script;
        node->m_vtable = kScriptNodeVtable;
        node->m_rank = 0;
        node->m_following = node;
        node->m_preceding = node;
    }
    MemSys::LeaveHeapScope();
    // At the end (no null check).
    node->m_preceding = m_preceding;
    node->m_following = this;
    m_preceding->m_following = node;
    m_preceding = node;
}

// 0x004626B0
void CScriptList::DropOut(Scripts::CScript* script)
{
    for (CScriptNode* node = m_following->IsHead() ? nullptr : m_following; node;) {
        if (node->m_payload == script) {
            node->m_preceding->m_following = node->m_following;
            node->m_following->m_preceding = node->m_preceding;
            node->m_following = node;
            node->m_preceding = node;
            // delete node (virtual)
            reinterpret_cast<CScriptNode*(__thiscall*)(CScriptNode*, uint8_t)>(node->m_vtable[0])(node, 1);
            return;
        }
        node = node->m_following;
        if (node->IsHead())
            return;
    }
}

void* CScriptList::operator new(size_t size) { return ScriptListPools().Carve(static_cast<unsigned int>(size)); }

// 0x004624D0
void CScriptList::operator delete(void* list) { ScriptListPools().Dealloc(list); }

// --- the event log ---------------------------------------------------------------------------------------

// 0x004627C0
void CSignalTrace::AddRow(uint32_t type, uint32_t target, uint32_t source, uint32_t script, uint32_t how, int kind)
{
    if (!m_namesPtr) {
        m_namesPtr = NewHashTable(8);
        Scripts::CArray* names = Scripts::FetchList(kEventNames);
        for (int i = 0; i < static_cast<int>(names->m_extent); ++i) {
            char* name = m_names[i];
            const char* string = names->FindInlineText(i);
            std::memcpy(name, string, std::strlen(string) + 1);
            uint32_t checksum = Checksums::HashFromText(names->FindInlineText(i));
            m_namesPtr->StoreEntry(checksum, name);
        }
    }
    int last = m_ordinal - 1;
    if (last < 0)
        last = kLogSize - 1;
    if (kind == FRAME) {
        if (m_entries[last].kind == FRAME) {
            ++m_entries[last].type;
            return;
        }
    } else if (kind == HANDLED) {
        --m_layer;
    }
    TableRow& entry = m_entries[m_ordinal];
    entry.kind = kind;
    entry.type = kind == FRAME ? 0 : type;
    entry.target = target;
    entry.source = source;
    entry.script = script;
    entry.how = how;
    entry.depth = m_layer;
    if (++m_ordinal == kLogSize) {
        m_ordinal = 0;
        m_hasLooped = true;
    }
    if (kind == LAUNCHED)
        ++m_layer;
    ++m_addedSinceSweep;
}

// 0x004620A0
void CSignalTrace::EchoOut(bool onlyNew, int /*count*/)
{
    const char* const line = "========================================================\n"; // "=====...\n"
    int index = 0;
    int count = m_ordinal;
    if (m_hasLooped) {
        index = m_ordinal;
        count = kLogSize;
    }
    DebugOut::Printf(line);
    DebugOut::Printf(onlyNew ? "PRINTING EVENT LOG (ONLY NEW ENTRIES)\n\n"
                        : "PRINTING EVENT LOG\n\n");
    // Each entry from the oldest (their printing is compiled out of this build).
    for (; count > 0; --count) {
        if (++index == kLogSize)
            index = 0;
    }
    DebugOut::Printf(line);
    m_addedSinceSweep = 0;
}

// 0x00462CB0
void CSignalTrace::Teardown() { DeleteHashTable(m_namesPtr); }

// --- the tracker -----------------------------------------------------------------------------------------

// 0x00462CF0
CEventWaitBoard* CEventWaitBoard::MakeNew()
{
    m_vtable = kTrackerVtable;
    m_log.m_ordinal = 0;
    m_log.m_hasLooped = false;
    m_log.m_namesPtr = nullptr;
    m_log.m_layer = 0;
    m_log.m_addedSinceSweep = 0;
    m_entityList = NewHashTable(8);
    m_aliases = NewHashTable(4);
    m_listeners = NewHashTable(8);
    m_nextAutoId = 0;
    m_eventListeners = nullptr;
    unknown18 = false;
    m_launchDepth = 0;
    m_launchedFromScript = 0;
    for (SWaiter& waiter : m_waiters)
        waiter.type = 0;
    unknownB218 = false;
    return this;
}

// 0x00462E40 (the listener table isn't freed - an original leak)
void CEventWaitBoard::Destruct()
{
    m_vtable = kTrackerVtable;
    DeleteHashTable(m_entityList);
    m_log.Teardown();
}

// 0x00463780
CEventWaitBoard* CEventWaitBoard::ScalarDelete(uint8_t flags)
{
    Destruct();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00462120
CEntity* CEventWaitBoard::LookUpObject(uint32_t id)
{
    if (auto* object = static_cast<CEntity*>(m_entityList->FetchEntry(id)))
        return object;
    return static_cast<CEntity*>(m_aliases->FetchEntry(id, true));
}

// 0x00462190
CEntity* CEventWaitBoard::LookUpByNickname(uint32_t alias) { return static_cast<CEntity*>(m_aliases->FetchEntry(alias)); }

// 0x004621D0
uint32_t CEventWaitBoard::FetchDistinctId()
{
    uint32_t checksum;
    do {
        char name[64];
        std::snprintf(name, sizeof(name), "autoid%d", m_nextAutoId++); // "autoid%d"
        if (m_nextAutoId >= 1000000)
            m_nextAutoId = 0;
        checksum = Checksums::HashFromText(name);
    } while (m_entityList->FetchEntry(checksum));
    return checksum;
}

// 0x00462980
void CEventWaitBoard::AddObject(CEntity* object)
{
    if (auto* aliased = static_cast<CEntity*>(m_aliases->FetchEntry(ObjectId(object)))) {
        if (HasAliases(aliased))
            --aliased->m_aliasCount;
        m_aliases->EraseEntry(ObjectId(object));
    }
    m_entityList->StoreEntry(ObjectId(object), object);
    m_log.AddRow(0, ObjectId(object), kNoObjectId, 0, 0, CSignalTrace::OBJECT_ADDED);
}

// 0x00462A20
void CEventWaitBoard::RemoveObject(CEntity* object, uint32_t freshId, bool renamed)
{
    m_entityList->EraseEntry(ObjectId(object));
    if (renamed) {
        for (SWaiter& waiter : m_waiters) {
            if (waiter.objectId == ObjectId(object))
                waiter.objectId = freshId;
        }
    } else {
        RemoveAliasesOf(object);
    }
    m_log.AddRow(0, ObjectId(object), kNoObjectId, 0, 0, CSignalTrace::OBJECT_REMOVED);
}

// 0x00462590: from the start of the table again after each one removed.
void CEventWaitBoard::RemoveAliasesOf(CEntity* object)
{
    ListKit::KeyMap* table = m_aliases;
    for (;;) {
        table->m_iterBucket = -1;
        table->m_iterItem = nullptr;
        if (!table->StepIterator() || !table->m_iterItem)
            return;
        uint32_t alias = table->m_iterItem->key;
        auto* aliased = static_cast<CEntity*>(table->m_iterItem->value);
        for (;;) {
            if (!aliased)
                return;
            if (ObjectId(aliased) == ObjectId(object))
                break;
            if (!table->StepIterator() || !table->m_iterItem)
                return;
            alias = table->m_iterItem->key;
            aliased = static_cast<CEntity*>(table->m_iterItem->value);
        }
        if (HasAliases(aliased))
            --aliased->m_aliasCount;
        table->EraseEntry(alias);
    }
}

// 0x00462A90 (`object` isn't checked for null)
void CEventWaitBoard::RegisterNickname(uint32_t alias, CEntity* object)
{
    if (auto* aliased = static_cast<CEntity*>(m_aliases->FetchEntry(alias))) {
        if (HasAliases(aliased))
            --aliased->m_aliasCount;
        m_aliases->EraseEntry(alias);
    }
    ++object->m_aliasCount;
    m_aliases->StoreEntry(alias, object);
}

// 0x00462000
void CEventWaitBoard::RemoveEventListener(CSignalReceiver* listener)
{
    CSignalReceiver* previous = nullptr;
    for (CSignalReceiver* other = m_eventListeners; other; other = other->m_nextSubscriber) {
        if (other == listener) {
            if (previous)
                previous->m_nextSubscriber = listener->m_nextSubscriber;
            else
                m_eventListeners = listener->m_nextSubscriber;
            break;
        }
        previous = other;
    }
    listener->m_onBoard = false;
}

// 0x00463370
void CEventWaitBoard::AddListener(uint32_t type, Scripts::CScript* script)
{
    PushScriptHeap();
    auto* list = static_cast<CScriptList*>(m_listeners->FetchEntry(type));
    if (!list) {
        list = static_cast<CScriptList*>(CScriptList::operator new(sizeof(CScriptList)));
        if (list) {
            list->m_payload = reinterpret_cast<Scripts::CScript*>(-1);
            list->m_rank = 0;
            list->m_following = list;
            list->m_preceding = list;
            list->m_vtable = kScriptListVtable;
        }
        m_listeners->StoreEntry(type, list); // (null too, if there was no memory - then Add crashes)
    }
    list->AppendOne(script);
    MemSys::LeaveHeapScope();
}

// 0x00463450
void CEventWaitBoard::RemoveListener(uint32_t type, Scripts::CScript* script)
{
    auto* list = static_cast<CScriptList*>(m_listeners->FetchEntry(type));
    if (!list)
        return;
    list->DropOut(script);
    if (list->IsVacant()) {
        m_listeners->EraseEntry(type);
        DeleteScriptList(list);
    }
}

// 0x00462B90: walking the table, one item ahead (the current one may be removed).
void CEventWaitBoard::ForgetScript(Scripts::CScript* script)
{
    ListKit::KeyMap* table = m_listeners;
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    if (!table->StepIterator() || !table->m_iterItem)
        return;
    uint32_t type = table->m_iterItem->key;
    auto* list = static_cast<CScriptList*>(table->m_iterItem->value);
    uint32_t nextType = type;
    while (list) {
        CScriptList* next = nullptr;
        if (table->StepIterator() && table->m_iterItem) {
            nextType = table->m_iterItem->key;
            next = static_cast<CScriptList*>(table->m_iterItem->value);
        }
        list->DropOut(script);
        if (list->IsVacant()) {
            table->EraseEntry(type);
            DeleteScriptList(list);
        }
        list = next;
        type = nextType;
    }
}

// 0x00462260
void CEventWaitBoard::WaitForEvent(Scripts::CScript* script, uint32_t type)
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
            if (!Scripts::FindScriptWithUniqueId(m_waiters[i].scriptId)) {
                slot = i;
                break;
            }
        }
        if (slot < 0)
            return;
    }
    SWaiter& waiter = m_waiters[slot];
    waiter.scriptId = script->m_uniqueId;
    waiter.objectId = script->m_entity ? ObjectId(script->m_entity.Pull()) : 0;
    waiter.type = type;
    script->GateOff(0);
}

// 0x00462F00
bool CEventWaitBoard::LaunchEvent(uint32_t type, uint32_t target, uint32_t source, Scripts::CStruct* data, bool broadcast,
                           float radius)
{
    ++m_launchDepth;
    m_log.AddRow(type, target, source, static_cast<uint32_t>(m_launchedFromScript), 0, CSignalTrace::LAUNCHED);
    m_launchedFromScript = 0;
    CSignal event;
    event.MakeNew(type, source, target, data);

    if (!broadcast) {
        // Every event listener, then the target: an object, or else the spawned script with that id.
        for (CSignalReceiver* listener = m_eventListeners; listener; listener = listener->m_nextSubscriber) {
            ++listener->m_busy;
            ListenerReceive(listener, &event);
            --listener->m_busy;
        }
        uint32_t eventTarget = event.m_aimPoint;
        if (eventTarget != kNoObjectId && !(event.m_flagBits & CSignal::FLAG_HANDLED)) {
            if (CEntity* object = LookUpObject(eventTarget)) {
                ForwardTrigger(object, &event);
            } else {
                Scripts::CScript* script = Scripts::LookUpSpawnedScript(eventTarget);
                if (!script)
                    m_log.EchoOut(true, kLogSize);
                script->ReceiveEvent(&event); // (null when there's no such script - an original crash)
            }
        }
    } else if (auto* list = static_cast<CScriptList*>(m_listeners->FetchEntry(type, true))) {
        // Every script listening for the type, in the order they started listening; with a radius (>= 0), only
        // those whose object is nearer the source than that (scripts without an object, or whose object has no
        // position, always get it).
        float sourcePosition[4];
        if (radius >= 0.0f) {
            const float* position = Position(FindObjectById(source)); // (not checked for null)
            std::memcpy(sourcePosition, position, sizeof(sourcePosition));
        }
        auto inRange = [&](Scripts::CScript* script) {
            if (!(radius >= 0.0f))
                return true;
            CEntity* object = script->m_entity.Pull();
            if (!object || !HasPosition(object))
                return true;
            double distance = DistanceSquared(sourcePosition, Position(object));
            return static_cast<double>(radius) * radius > distance;
        };
        int count = 0;
        CScriptNode* first = list->m_following;
        if (!first->IsHead()) {
            for (CScriptNode* node = first; node;) {
                node = node->m_following;
                ++count;
                if (node->IsHead())
                    break;
            }
        }
        if (count == 1) {
            Scripts::CScript* script = first->m_payload;
            if (inRange(script))
                script->ReceiveEvent(&event);
        } else {
            // Several (or none): first a smart pointer to each, so that one deleted by an earlier one's handler
            // is skipped.
            using Pointer = CSharedRef<CSharedBase>;
            auto* scripts = static_cast<Scripts::CScript**>(MemSys::Alloc(static_cast<unsigned int>(count) * 4));
            Pointer* pointers = nullptr;
            if (auto* block = static_cast<uint32_t*>(MemSys::Alloc(static_cast<unsigned int>(count) * sizeof(Pointer) + 4))) {
                block[0] = static_cast<uint32_t>(count);
                pointers = reinterpret_cast<Pointer*>(block + 1);
                for (int i = 0; i < count; ++i)
                    new (&pointers[i]) Pointer();
            }
            CScriptNode* node = list->m_following->IsHead() ? nullptr : list->m_following;
            for (int i = 0; node; ++i) {
                scripts[i] = node->m_payload;
                pointers[i].AssignValue(&node->m_payload->m_sharedBase);
                node = node->m_following;
                if (node->IsHead())
                    break;
            }
            for (int i = 0; i < count; ++i) {
                if (pointers[i] && inRange(scripts[i]))
                    scripts[i]->ReceiveEvent(&event);
            }
            if (pointers) {
                uint32_t* block = reinterpret_cast<uint32_t*>(pointers) - 1;
                for (int i = static_cast<int>(block[0]) - 1; i >= 0; --i)
                    pointers[i].Release(); // (the destructors, last first)
                MemSys::Dealloc(block);
            }
            MemSys::Dealloc(scripts);
        }
    }

    // The first WaitForEvent waiter for this type whose object is the target (or that has no object, or the
    // event has no target or is broadcast) goes on; waiters whose script has gone are dropped on the way.
    for (SWaiter& waiter : m_waiters) {
        if (waiter.type == type &&
            (waiter.objectId == target || waiter.objectId == 0 || target == kNoObjectId || broadcast)) {
            waiter.type = 0;
            if (Scripts::CScript* script = Scripts::FindScriptWithUniqueId(waiter.scriptId)) {
                script->StopWaiting();
                event.SetEventHandled(2, script->m_scriptTitle);
                break;
            }
        }
    }
    if (!(event.m_flagBits & CSignal::FLAG_HANDLED))
        m_log.AddRow(event.m_kindTag, event.m_aimPoint, event.m_fromWhere, 0, 3, CSignalTrace::HANDLED);
    --m_launchDepth;
    bool handled = (event.m_flagBits & CSignal::FLAG_HANDLED) != 0;
    event.Teardown();
    return handled;
}

// 0x00462B10
void CEventWaitBoard::LogHandled(CSignal* event, uint32_t how, uint32_t script)
{
    m_log.AddRow(event->m_kindTag, event->m_aimPoint, event->m_fromWhere, script, how, CSignalTrace::HANDLED);
}

// 0x00462B40
void CEventWaitBoard::LogRead(CSignal* event, uint32_t how, uint32_t script)
{
    m_log.AddRow(event->m_kindTag, event->m_aimPoint, event->m_fromWhere, script, how, CSignalTrace::READ);
}

// 0x00462B70
void CEventWaitBoard::LogFrame() { m_log.AddRow(0, 0, 0, 0, 0, CSignalTrace::FRAME); }

// 0x00463730
CEventWaitBoard* CreateTracker(bool create)
{
    CEventWaitBoard*& tracker = Tracker();
    if (!tracker && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CEventWaitBoard), nullptr, 1);
        tracker = memory ? static_cast<CEventWaitBoard*>(memory)->MakeNew() : nullptr;
    }
    ++g_trackerReferences;
    return tracker;
}

// --- the script commands ---------------------------------------------------------------------------------

// 0x00462310: WaitForEvent type=<type>
bool __cdecl CFunc_WaitForEvent(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t type = 0; // (uninitialised in the original when there's no type)
    params->ReadHashValue(kType, &type, 0);
    Tracker()->WaitForEvent(script, type);
    return true;
}

// 0x00462340: ObjectExists id=<object>
bool __cdecl CFunc_ObjectExists(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t id;
    if (!GetObjectId(params, kId, &id))
        return false;
    return Tracker()->LookUpObject(id) != nullptr;
}

// 0x00462380: TerminateObjectsScripts id=<object> (scripts=[names] | script_name=<name>): stop the object's
// scripts that refer to these names (no name: 0).
bool __cdecl CFunc_TerminateObjectsScripts(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t id;
    if (!GetObjectId(params, kId, &id))
        return false;
    CEntity* object = Tracker()->LookUpObject(id);
    Scripts::CArray* scripts;
    if (params->FetchList(kScripts, &scripts, 0)) {
        for (uint32_t i = 0; i < scripts->m_extent; ++i)
            Scripts::StopScriptsUsingObjectThatReferTo(object, scripts->FetchPart(i));
    } else {
        uint32_t name = 0;
        params->ReadHashValue(kScriptName, &name, 0);
        Scripts::StopScriptsUsingObjectThatReferTo(object, name);
    }
    return true;
}

// 0x00462440: SetObjectProps id=<object> ... (the object takes the rest; not checked for null)
bool __cdecl CFunc_SetObjectProps(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t id;
    if (!GetObjectId(params, kId, &id))
        return false;
    ChangeTraits(Tracker()->LookUpObject(id), params);
    return true;
}

// 0x00462480: PrintEventLog [<count>] (with a count: the whole header, not "only new entries")
bool __cdecl CFunc_PrintEventLog(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    bool onlyNew = true;
    int count = kLogSize;
    if (params->FetchIntValue(0u, &count, 0))
        onlyNew = false;
    Tracker()->m_log.EchoOut(onlyNew, count);
    return true;
}

// 0x00462C40: AssignAlias id=<object> alias=<name>
bool __cdecl CFunc_AssignAlias(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t id;
    if (!GetObjectId(params, kId, &id))
        return false;
    CEventWaitBoard* tracker = Tracker();
    CEntity* object = tracker->LookUpObject(id);
    uint32_t alias = 0; // (uninitialised in the original when there's no alias)
    params->ReadHashValue(kAlias, &alias, 1);
    tracker->RegisterNickname(alias, object);
    return true;
}

// 0x004634D0: LaunchEvent / FireEvent type=<type> | type=[types] | type={types} [target=<id>] [source=<id>]
// [data={...}] [broadcast] [radius=<r>]. Target and source default to the script's object (none without one).
bool __cdecl CFunc_LaunchEvent(Scripts::CStruct* params, Scripts::CScript* script)
{
    auto ownObject = [&]() {
        return script && script->m_entity ? ObjectId(script->m_entity.Pull()) : kNoObjectId;
    };
    uint32_t target;
    if (!GetObjectId(params, kTarget, &target))
        target = ownObject();
    uint32_t source;
    if (!GetObjectId(params, kSource, &source))
        source = ownObject();
    Scripts::CStruct* data = nullptr;
    params->ReadSubStruct(kData, &data, 0);
    bool broadcast = params->HasBareFlag(kBroadcast);
    float radius = -1.0f;
    params->FetchFloat(kRadius, &radius, 0);
    CEventWaitBoard* tracker = Tracker();
    // Whether this launch comes from a script with a name (only for the log).
    auto fromScript = [&]() { return script && script->m_scriptTitle ? 1 : 0; };

    uint32_t type;
    if (params->ReadHashValue(kType, &type, 0)) {
        tracker->m_launchedFromScript = fromScript();
        tracker->LaunchEvent(type, target, source, data, broadcast, radius);
        return true;
    }
    Scripts::CArray* types;
    if (params->FetchList(kType, &types, 0)) {
        uint32_t size = types->m_extent;
        for (uint32_t i = 0; i < size; ++i) {
            tracker->m_launchedFromScript = fromScript();
            tracker->LaunchEvent(types->FetchPart(i), target, source, data, broadcast, radius);
        }
        return true;
    }
    Scripts::CStruct* structure;
    if (params->ReadSubStruct(kType, &structure, 0)) {
        for (Scripts::CComponent* component = structure->StepToEntry(nullptr); component;
             component = structure->StepToEntry(component)) {
            tracker->m_launchedFromScript = fromScript();
            tracker->LaunchEvent(component->checksum, target, source, data, broadcast, radius);
        }
    }
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kScriptNodeVtable_slots[] = {&GameObjects::CScriptNode::ScalarDelete};
void** const kScriptNodeVtable = reinterpret_cast<void**>(kScriptNodeVtable_slots);
static VtableSlot kScriptListVtable_slots[] = {&GameObjects::CScriptList::ScalarDelete};
void** const kScriptListVtable = reinterpret_cast<void**>(kScriptListVtable_slots);
static VtableSlot kTrackerVtable_slots[] = {&GameObjects::CEventWaitBoard::ScalarDelete};
void** const kTrackerVtable = reinterpret_cast<void**>(kTrackerVtable_slots);

} // namespace
} // namespace Obj
