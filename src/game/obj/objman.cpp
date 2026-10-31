// The object managers (objman.h): CObjectHubBase's sorted list of objects and its re-entrant walk, CMiscHub's
// dead list, the search by type, and two pieces of the object tracker.

#include "game/obj/objman.h"
#include "game/net/task_vtable.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/lst/head.h"
#include "game/sys/mlp.h"
#include "game/ass/assman.h"
#include "game/core/code_address.h"
#include "game/front/screen_element_manager.h"
#include "game/obj/object.h"

namespace GameObjects {

uint32_t g_trackerReferences;

namespace {

extern void** const kManagerVtable;
extern void** const kGeneralManagerVtable;
extern void** const kListHeadVtable;
extern void** const kObjectSearchBaseVtable;
constexpr int kFlushTaskPriority = 1000;

// The tracker's reference count.
uint32_t& TrackerReferences() { return g_trackerReferences; }

// ListKit::Head<CObject>'s insert by priority (the highest first, equal ones newest first).
void AddNodeByPriority(MemSys::ListNode* head, MemSys::ListNode* node) { reinterpret_cast<ListKit::Head*>(head)->AddNode(node); }
// TaskList::Task's constructor and the main loop's system task list's add.
void ConstructTask(void* task, int priority) { static_cast<TaskList::SchedulerTask*>(task)->Construct(priority); }
void QueueEngineJob(void* task)
{
    AssetStore::g_mainLoopManager->m_systemTasks.AddTask(static_cast<TaskList::SchedulerTask*>(task));
}

// The node after `node`, or null at the head.
MemSys::ListNode* NextNode(MemSys::ListNode* node)
{
    MemSys::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
CObject* ObjectOf(MemSys::ListNode* node) { return node ? static_cast<CObject*>(node->data) : nullptr; }

void SetupListSentinel(MemSys::ListNode* head)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = kListHeadVtable;
}

} // namespace

// 0x00461B20
CObjectListHead* CObjectListHead::Destroy(unsigned int flags)
{
    static_cast<CObjectNode*>(static_cast<MemSys::ListNode*>(this))->DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00461BC0
CObjectHubBase* CObjectHubBase::Construct()
{
    m_vtable = kManagerVtable;
    SetupListSentinel(&m_entities);
    m_walking = 0;
    m_restartWalks = 0;
    m_reassigning = false;
    return this;
}

// 0x00461B50
void CObjectHubBase::DestroyBody()
{
    m_vtable = kManagerVtable;
    m_entities.UnlinkAs(g_objectNodeVtable);
}

// 0x00461B80
CObjectHubBase* CObjectHubBase::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

void CObjectHubBase::VirtualRegisterObject(CObject* object)
{
    reinterpret_cast<void(__thiscall*)(CObjectHubBase*, CObject*)>(m_vtable[1])(this, object);
}
void CObjectHubBase::VirtualUnregisterObject(CObject* object)
{
    reinterpret_cast<void(__thiscall*)(CObjectHubBase*, CObject*)>(m_vtable[2])(this, object);
}
void CObjectHubBase::VirtualReassignObjectId(CObject* object, uint32_t id)
{
    reinterpret_cast<void(__thiscall*)(CObjectHubBase*, CObject*, uint32_t)>(m_vtable[3])(this, object, id);
}
void CObjectHubBase::VirtualKillObject(CObject* object)
{
    reinterpret_cast<void(__thiscall*)(CObjectHubBase*, CObject*)>(m_vtable[5])(this, object);
}

// 0x00461A00
void CObjectHubBase::RegisterObject(CObject* object)
{
    CEventWaitBoard* tracker = Tracker();
    object->m_parkOwner = this;
    AddNodeByPriority(&m_entities, &object->m_listLink);
    m_restartWalks = 0xFFFFFFFF;
    if (object->m_id == kUnassignedId)
        object->SetID(tracker->GetUniqueId());
    tracker->AddObject(object);
}

// 0x00461A50
void CObjectHubBase::DropEntityRegistration(CObject* object)
{
    Tracker()->RemoveObject(object, m_newId, m_reassigning);
    MemSys::ListNode* node = &object->m_listLink;
    if (node->prev != node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = node;
        node->prev = node;
        object->m_parkOwner = nullptr;
        m_restartWalks = 0xFFFFFFFF;
    }
}

// 0x004619A0
void CObjectHubBase::ReassignObjectId(CObject* object, uint32_t id)
{
    m_newId = id;
    m_reassigning = true;
    VirtualUnregisterObject(object);
    object->m_id = id;
    VirtualRegisterObject(object);
    m_reassigning = false;
}

// 0x004619D0
void CObjectHubBase::SetEntityRank(CObject* object, int priority)
{
    uint32_t id = object->m_id;
    object->m_listLink.priority = priority;
    VirtualReassignObjectId(object, id);
}

// 0x004619F0
CObject* CObjectHubBase::LookUpEntity(uint32_t id) { return Tracker()->LookUpObject(id); }

// 0x00461CC0. The next object is looked at before the callback runs; a callback that changes the list (register
// or unregister) makes the walk start again from the front, past the objects already done.
void CObjectHubBase::ForEach(ObjectCallback callback, void* data)
{
    uint32_t bit = 1;
    while (m_walking & bit)
        bit <<= 1;
    m_walking |= bit;
    for (MemSys::ListNode* node = NextNode(&m_entities); node && node->data; node = NextNode(node))
        static_cast<CObject*>(node->data)->m_visitedMask &= ~bit;
    do {
        m_restartWalks &= ~bit;
        MemSys::ListNode* node = NextNode(&m_entities);
        CObject* object = ObjectOf(node);
        while (object) {
            MemSys::ListNode* nextNode = NextNode(node);
            CObject* next = ObjectOf(nextNode);
            if (!(object->m_visitedMask & bit)) {
                object->m_visitedMask |= bit;
                callback(object, data);
                if (m_restartWalks & bit)
                    break;
            }
            node = nextNode;
            object = next;
        }
    } while (m_restartWalks & bit);
    m_walking &= ~bit;
}

// 0x00461BF0
CMiscHub* CMiscHub::Construct()
{
    CObjectHubBase::Construct();
    m_vtable = kGeneralManagerVtable;
    SetupListSentinel(&m_deadObjects);
    auto* task = static_cast<uint32_t*>(MemSys::AllocZeroed(0x20));
    if (task) {
        ConstructTask(task, kFlushTaskPriority);
        task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        task[6] = reinterpret_cast<uint32_t>(&FlushDeadObjectsTask);
        task[7] = reinterpret_cast<uint32_t>(this);
    }
    m_flushTask = task;
    QueueEngineJob(task);
    return this;
}

// 0x00461DF0
void CMiscHub::DestroyBody()
{
    m_vtable = kGeneralManagerVtable;
    ForEach(&Unlock, nullptr);
    ForEach(&DisposeWhenFree, nullptr);
    if (m_flushTask) {
        auto* task = static_cast<void***>(m_flushTask);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*task)[2])(task, 1); // its deleting destructor
    }
    m_deadObjects.UnlinkAs(g_objectNodeVtable);
    m_vtable = kManagerVtable;
    m_entities.UnlinkAs(g_objectNodeVtable);
}

// 0x00461EA0
CMiscHub* CMiscHub::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00461B00
void CMiscHub::RemoveEntity(CObject* object)
{
    MemSys::ListNode* node = &object->m_listLink;
    MemSys::ListNode* head = &m_deadObjects;
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}

// 0x00461AB0
void CMiscHub::FlushDeadObjects()
{
    MemSys::ListNode* node = m_deadObjects.next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    do {
        MemSys::ListNode* next = NextNode(node);
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = node;
        node->prev = node;
        if (auto* object = static_cast<CObject*>(node->data))
            object->VirtualDestroy(1);
        node = next;
    } while (node);
}

// 0x00461BB0
void __cdecl FlushDeadObjectsTask(void* task)
{
    static_cast<CMiscHub*>(static_cast<void**>(task)[7])->FlushDeadObjects();
}

// 0x00461F30
CObject* CObjectSearch::FirstItemOfType(MemSys::ListNode* head, uint32_t type)
{
    m_listLink = head;
    m_listLink = NextNode(head);
    CObject* object = ObjectOf(m_listLink);
    m_type = type;
    if (!object)
        return nullptr;
    while (object->m_type != type) {
        m_listLink = NextNode(m_listLink);
        if (!m_listLink)
            return nullptr;
        object = ObjectOf(m_listLink);
        if (!object)
            return nullptr;
    }
    return object;
}

// 0x00461ED0
CObject* CObjectSearch::NextItemOfType()
{
    m_listLink = NextNode(m_listLink);
    if (!m_listLink)
        return nullptr;
    CObject* object = ObjectOf(m_listLink);
    if (!object)
        return nullptr;
    while (object->m_type != m_type) {
        m_listLink = NextNode(m_listLink);
        if (!m_listLink)
            return nullptr;
        object = ObjectOf(m_listLink);
        if (!object)
            return nullptr;
    }
    return object;
}

// 0x0041AA90
void CObjectSearch::DestroyBody() { m_vtable = kObjectSearchBaseVtable; }

// 0x0041AAA0
CObjectSearch* CObjectSearch::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0041A850
CObjectSearch* CObjectSearch::DestroyAsBase(unsigned int flags)
{
    m_vtable = kObjectSearchBaseVtable;
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00461FA0
void ReleaseTracker()
{
    uint32_t& references = TrackerReferences();
    if (references > 1) {
        --references;
        return;
    }
    if (CEventWaitBoard* tracker = Tracker()) // (its scalar deleting destructor, through the vtable)
        reinterpret_cast<void*(__thiscall*)(CEventWaitBoard*, unsigned int)>(tracker->m_vtable[0])(tracker, 1);
    Tracker() = nullptr;
    references = 0;
}

// 0x00461FE0
void CEventWaitBoard::SubscribeReceiver(CSignalReceiver* listener)
{
    listener->mp_nextListener = mp_eventListeners;
    mp_eventListeners = listener;
    listener->m_onBoard = true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kListHeadVtable_slots[] = {&GameObjects::CObjectListHead::Destroy};
void** const kListHeadVtable = reinterpret_cast<void**>(kListHeadVtable_slots);
static VtableSlot kManagerVtable_slots[] = {&GameObjects::CObjectHubBase::Destroy,
         &GameObjects::CObjectHubBase::RegisterObject,
         &GameObjects::CObjectHubBase::DropEntityRegistration,
         &GameObjects::CObjectHubBase::ReassignObjectId,
         &GameObjects::CObjectHubBase::SetEntityRank,
         &_purecall,
         &_purecall};
void** const kManagerVtable = reinterpret_cast<void**>(kManagerVtable_slots);
static VtableSlot kGeneralManagerVtable_slots[] = {&GameObjects::CMiscHub::Destroy,
         &GameObjects::CObjectHubBase::RegisterObject,
         &GameObjects::CObjectHubBase::DropEntityRegistration,
         &GameObjects::CObjectHubBase::ReassignObjectId,
         &GameObjects::CObjectHubBase::SetEntityRank,
         &GameObjects::CMiscHub::RemoveEntity,
         &MenuSystem::CUiTree::FetchObjectSet};
void** const kGeneralManagerVtable = reinterpret_cast<void**>(kGeneralManagerVtable_slots);

} // namespace
} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kObjectSearchBaseVtable_slots[] = {&GameObjects::CObjectSearch::DestroyAsBase};
void** const kObjectSearchBaseVtable = reinterpret_cast<void**>(kObjectSearchBaseVtable_slots);

} // namespace
} // namespace Obj
