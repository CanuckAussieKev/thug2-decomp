// The object managers (objman.h): CBaseManager's sorted list of objects and its re-entrant walk, CGeneralManager's
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

namespace Obj {

uint32_t g_trackerReferences;

namespace {

extern void** const kManagerVtable;
extern void** const kGeneralManagerVtable;
extern void** const kListHeadVtable;
extern void** const kObjectSearchBaseVtable;
constexpr int kFlushTaskPriority = 1000;

// The tracker's reference count.
uint32_t& TrackerReferences() { return g_trackerReferences; }

// Lst::Head<CObject>'s insert by priority (the highest first, equal ones newest first).
void AddNodeByPriority(Mem::ListNode* head, Mem::ListNode* node) { reinterpret_cast<Lst::Head*>(head)->AddNode(node); }
// Tsk::Task's constructor and the main loop's system task list's add.
void ConstructTask(void* task, int priority) { static_cast<Tsk::BaseTask*>(task)->Construct(priority); }
void AddSystemTask(void* task)
{
    Ass::g_mainLoopManager->m_systemTasks.AddTask(static_cast<Tsk::BaseTask*>(task));
}

// The node after `node`, or null at the head.
Mem::ListNode* NextNode(Mem::ListNode* node)
{
    Mem::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
CObject* ObjectOf(Mem::ListNode* node) { return node ? static_cast<CObject*>(node->data) : nullptr; }

void InitHead(Mem::ListNode* head)
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
    static_cast<CObjectNode*>(static_cast<Mem::ListNode*>(this))->DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00461BC0
CBaseManager* CBaseManager::Construct()
{
    m_vtable = kManagerVtable;
    InitHead(&m_objects);
    m_walking = 0;
    m_restartWalks = 0;
    m_reassigning = false;
    return this;
}

// 0x00461B50
void CBaseManager::DestroyBody()
{
    m_vtable = kManagerVtable;
    m_objects.UnlinkAs(g_objectNodeVtable);
}

// 0x00461B80
CBaseManager* CBaseManager::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

void CBaseManager::VirtualRegisterObject(CObject* object)
{
    reinterpret_cast<void(__thiscall*)(CBaseManager*, CObject*)>(m_vtable[1])(this, object);
}
void CBaseManager::VirtualUnregisterObject(CObject* object)
{
    reinterpret_cast<void(__thiscall*)(CBaseManager*, CObject*)>(m_vtable[2])(this, object);
}
void CBaseManager::VirtualReassignObjectId(CObject* object, uint32_t id)
{
    reinterpret_cast<void(__thiscall*)(CBaseManager*, CObject*, uint32_t)>(m_vtable[3])(this, object, id);
}
void CBaseManager::VirtualKillObject(CObject* object)
{
    reinterpret_cast<void(__thiscall*)(CBaseManager*, CObject*)>(m_vtable[5])(this, object);
}

// 0x00461A00
void CBaseManager::RegisterObject(CObject* object)
{
    CTracker* tracker = Tracker();
    object->mp_manager = this;
    AddNodeByPriority(&m_objects, &object->m_node);
    m_restartWalks = 0xFFFFFFFF;
    if (object->m_id == kUnassignedId)
        object->SetID(tracker->GetUniqueId());
    tracker->AddObject(object);
}

// 0x00461A50
void CBaseManager::UnregisterObject(CObject* object)
{
    Tracker()->RemoveObject(object, m_newId, m_reassigning);
    Mem::ListNode* node = &object->m_node;
    if (node->prev != node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = node;
        node->prev = node;
        object->mp_manager = nullptr;
        m_restartWalks = 0xFFFFFFFF;
    }
}

// 0x004619A0
void CBaseManager::ReassignObjectId(CObject* object, uint32_t id)
{
    m_newId = id;
    m_reassigning = true;
    VirtualUnregisterObject(object);
    object->m_id = id;
    VirtualRegisterObject(object);
    m_reassigning = false;
}

// 0x004619D0
void CBaseManager::SetObjectPriority(CObject* object, int priority)
{
    uint32_t id = object->m_id;
    object->m_node.priority = priority;
    VirtualReassignObjectId(object, id);
}

// 0x004619F0
CObject* CBaseManager::GetObjectByID(uint32_t id) { return Tracker()->LookUpObject(id); }

// 0x00461CC0. The next object is looked at before the callback runs; a callback that changes the list (register
// or unregister) makes the walk start again from the front, past the objects already done.
void CBaseManager::ForEach(ObjectCallback callback, void* data)
{
    uint32_t bit = 1;
    while (m_walking & bit)
        bit <<= 1;
    m_walking |= bit;
    for (Mem::ListNode* node = NextNode(&m_objects); node && node->data; node = NextNode(node))
        static_cast<CObject*>(node->data)->m_visitedMask &= ~bit;
    do {
        m_restartWalks &= ~bit;
        Mem::ListNode* node = NextNode(&m_objects);
        CObject* object = ObjectOf(node);
        while (object) {
            Mem::ListNode* nextNode = NextNode(node);
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
CGeneralManager* CGeneralManager::Construct()
{
    CBaseManager::Construct();
    m_vtable = kGeneralManagerVtable;
    InitHead(&m_deadObjects);
    auto* task = static_cast<uint32_t*>(Mem::Calloc(0x20));
    if (task) {
        ConstructTask(task, kFlushTaskPriority);
        task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        task[6] = reinterpret_cast<uint32_t>(&FlushDeadObjectsTask);
        task[7] = reinterpret_cast<uint32_t>(this);
    }
    m_flushTask = task;
    AddSystemTask(task);
    return this;
}

// 0x00461DF0
void CGeneralManager::DestroyBody()
{
    m_vtable = kGeneralManagerVtable;
    ForEach(&Unlock, nullptr);
    ForEach(&DestroyIfUnlocked, nullptr);
    if (m_flushTask) {
        auto* task = static_cast<void***>(m_flushTask);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*task)[2])(task, 1); // its deleting destructor
    }
    m_deadObjects.UnlinkAs(g_objectNodeVtable);
    m_vtable = kManagerVtable;
    m_objects.UnlinkAs(g_objectNodeVtable);
}

// 0x00461EA0
CGeneralManager* CGeneralManager::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00461B00
void CGeneralManager::KillObject(CObject* object)
{
    Mem::ListNode* node = &object->m_node;
    Mem::ListNode* head = &m_deadObjects;
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}

// 0x00461AB0
void CGeneralManager::FlushDeadObjects()
{
    Mem::ListNode* node = m_deadObjects.next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    do {
        Mem::ListNode* next = NextNode(node);
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
    static_cast<CGeneralManager*>(static_cast<void**>(task)[7])->FlushDeadObjects();
}

// 0x00461F30
CObject* CObjectSearch::FirstItemOfType(Mem::ListNode* head, uint32_t type)
{
    m_node = head;
    m_node = NextNode(head);
    CObject* object = ObjectOf(m_node);
    m_type = type;
    if (!object)
        return nullptr;
    while (object->m_type != type) {
        m_node = NextNode(m_node);
        if (!m_node)
            return nullptr;
        object = ObjectOf(m_node);
        if (!object)
            return nullptr;
    }
    return object;
}

// 0x00461ED0
CObject* CObjectSearch::NextItemOfType()
{
    m_node = NextNode(m_node);
    if (!m_node)
        return nullptr;
    CObject* object = ObjectOf(m_node);
    if (!object)
        return nullptr;
    while (object->m_type != m_type) {
        m_node = NextNode(m_node);
        if (!m_node)
            return nullptr;
        object = ObjectOf(m_node);
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
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041A850
CObjectSearch* CObjectSearch::DestroyAsBase(unsigned int flags)
{
    m_vtable = kObjectSearchBaseVtable;
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
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
    if (CTracker* tracker = Tracker()) // (its scalar deleting destructor, through the vtable)
        reinterpret_cast<void*(__thiscall*)(CTracker*, unsigned int)>(tracker->m_vtable[0])(tracker, 1);
    Tracker() = nullptr;
    references = 0;
}

// 0x00461FE0
void CTracker::RegisterEventListener(CEventListener* listener)
{
    listener->mp_nextListener = mp_eventListeners;
    mp_eventListeners = listener;
    listener->m_registered = true;
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kListHeadVtable_slots[] = {&Obj::CObjectListHead::Destroy};
void** const kListHeadVtable = reinterpret_cast<void**>(kListHeadVtable_slots);
static VtableSlot kManagerVtable_slots[] = {&Obj::CBaseManager::Destroy,
         &Obj::CBaseManager::RegisterObject,
         &Obj::CBaseManager::UnregisterObject,
         &Obj::CBaseManager::ReassignObjectId,
         &Obj::CBaseManager::SetObjectPriority,
         &_purecall,
         &_purecall};
void** const kManagerVtable = reinterpret_cast<void**>(kManagerVtable_slots);
static VtableSlot kGeneralManagerVtable_slots[] = {&Obj::CGeneralManager::Destroy,
         &Obj::CBaseManager::RegisterObject,
         &Obj::CBaseManager::UnregisterObject,
         &Obj::CBaseManager::ReassignObjectId,
         &Obj::CBaseManager::SetObjectPriority,
         &Obj::CGeneralManager::KillObject,
         &Front::CScreenElementManager::GetObjectList};
void** const kGeneralManagerVtable = reinterpret_cast<void**>(kGeneralManagerVtable_slots);

} // namespace
} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kObjectSearchBaseVtable_slots[] = {&Obj::CObjectSearch::DestroyAsBase};
void** const kObjectSearchBaseVtable = reinterpret_cast<void**>(kObjectSearchBaseVtable_slots);

} // namespace
} // namespace Obj
