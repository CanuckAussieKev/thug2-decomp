// The composite object manager and the COIM heap (compositemgr.h).

#include "game/obj/compositemgr.h"
#include "game/net/task_vtable.h"
#include "game/obj/pager_message.h"

#include <cstdint>
#include <new>

#include "game/core/alloc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/mlp.h"
#include "game/ass/assman.h"
#include "game/core/code_address.h"
#include "game/obj/objman.h"
#include "game/front/screen_element_manager.h"
#include "game/obj/cutscene_list_cfuncs.h"
#include "game/sys/startup_init.h"

namespace GameObjects {

uint32_t g_compositeObjectMemberFunctions[186]{
    0xE90AAD2D, 0x0ADA4E74, 0, 0, 0, 0,
    0xC2404947, 0x0ADA4E74, 0, 0, 0, 0,
    0x406998A5, 0x0ADA4E74, 0, 0, 0, 0,
    0x2BFFEAA3, 0x0ADA4E74, 0, 0, 0, 0,
    0xF7251A64, 0x0ADA4E74, 0, 0, 0, 0,
    0xC6F3BAA5, 0x0ADA4E74, 0, 0, 0, 0,
    0x77403AFC, 0x0ADA4E74, 0, 0, 0, 0,
    0xC1BFF0F3, 0x0ADA4E74, 0, 0, 0, 0,
    0x11FE9F71, 0x0ADA4E74, 0, 0, 0, 0,
    0xC0CAAC4A, 0x0ADA4E74, 0, 0, 0, 0,
    0x5B6634D4, 0x0ADA4E74, 0, 0, 0, 0,
    0xB60D1F35, 0x0ADA4E74, 0, 0, 0, 0,
    0xB16619AE, 0x0ADA4E74, 0, 0, 0, 0,
    0x10C1C887, 0x0ADA4E74, 0, 0, 0, 0,
    0xCE0CA665, 0x0ADA4E74, 0, 0, 0, 0,
    0xCA3C59A6, 0x0ADA4E74, 0, 0, 0, 0,
    0x28656D12, 0x0ADA4E74, 0, 0, 0, 0,
    0xFF85D4D7, 0x0ADA4E74, 0, 0, 0, 0,
    0x0168B09C, 0x0ADA4E74, 0, 0, 0, 0,
    0xB9ED09D2, 0x0ADA4E74, 0, 0, 0, 0,
    0xD96C0A20, 0x0ADA4E74, 0, 0, 0, 0,
    0x4AF57BBB, 0x0ADA4E74, 0, 0, 0, 0,
    0x2A26FFA2, 0x0ADA4E74, 0, 0, 0, 0,
    0xF8CFD515, 0x0ADA4E74, 0, 0, 0, 0,
    0x7DCC5FB9, 0x0ADA4E74, 0, 0, 0, 0,
    0xE5DF66D7, 0x0ADA4E74, 0, 0, 0, 0,
    0xDD468509, 0x0ADA4E74, 0, 0, 0, 0,
    0x37185D8A, 0x0ADA4E74, 0, 0, 0, 0,
    0x70ED3048, 0x0ADA4E74, 0, 0, 0, 0,
    0x33CAC5FF, 0x0ADA4E74, 0, 0, 0, 0,
    0, 0x0ADA4E74, 0, 0, 0, 0};

uint32_t g_objectMemberFunctions[102]{
    0x3611C136, 0x57525413, 0, 0, 0, 0,
    0x62C11CB9, 0x57525413, 0, 0, 0, 0,
    0xEEEB7220, 0x57525413, 0, 0, 0, 0,
    0xA58079EB, 0x57525413, 0, 0, 0, 0,
    0x0C7CD27A, 0x57525413, 0, 0, 0, 0,
    0xB3C262EC, 0x57525413, 0, 0, 0, 0,
    0x500EB224, 0x57525413, 0, 0, 0, 0,
    0x4BABC987, 0x57525413, 0, 0, 0, 0,
    0x53EBEE03, 0x57525413, 0, 0, 0, 0,
    0x6C2B67F9, 0x57525413, 0, 0, 0, 0,
    0xBE563426, 0x57525413, 0, 0, 0, 0,
    0xFBD89CD5, 0x57525413, 0, 0, 0, 0,
    0x23A4E5C2, 0x57525413, 0, 0, 0, 0,
    0x714937C7, 0x57525413, 0, 0, 0, 0,
    0x20D125D7, 0x57525413, 0, 0, 0, 0,
    0x3578E5A9, 0x57525413, 0, 0, 0, 0,
    0, 0x57525413, 0, 0, 0, 0};

int g_compositeMgrBlockHeaderSize{16};
int g_coimInteger0{4};
int g_coimInteger1{2};
int g_coimA;
int g_coimB;
int g_coimC;
MemSys::Region* g_coimRegion;
MemSys::Heap* g_coimHeap;
uint32_t g_instanceCount;
// (one list per registered type: up to kMaxComponentTypes, the manager pointer follows at 0x006F0FB8)
CObjectModule* g_componentList[GameObjects::CEntityHub::kMaxComponentTypes];
GameObjects::CEntityHub* g_compositeObjectManager;
uint32_t g_compositeMgrReferences;

namespace {

extern void** const kManagerVtable;
constexpr uint32_t kCompositeObjectType = 0xCDEB9229;
constexpr uint32_t kObjectMemberFunctionType = 0x57525413;    // the table at 0x00683550 (CObject's)
constexpr uint32_t kCompositeMemberFunctionType = 0x0ADA4E74; // the table at 0x00683268 (CGameEntity's)

uint32_t& RefTally() { return g_compositeMgrReferences; }

// The components of each type (index = the type's index in m_components), linked through their +0x14.
CObjectModule*& ComponentList(uint32_t index) { return g_componentList[index]; }
// A component's type and its link to the next of its type.
uint32_t TypeOf(CObjectModule* component) { return component->m_type; }
CObjectModule*& NextOfType(CObjectModule* component) { return component->mp_nextOfType; }

// A composite object's flags.
uint32_t& CompositeFlags(CGameEntity* object) { return object->m_compositeFlags; }

// CGameEntity's code (composite.cpp).
CGameEntity* ConstructCompositeObject(void* memory)
{
    return static_cast<CGameEntity*>(memory)->Construct();
}
void UpdateCompositeObject(CGameEntity* object) { object->Update(); }
void ConfigureFromData(CGameEntity* object, Scripts::CStruct* nodeData) { object->ConfigureFromData(nodeData); }
void BuildModulesFromList(CGameEntity* object, Scripts::CArray* components, Scripts::CStruct* nodeData)
{
    object->BuildModulesFromList(components, nodeData);
}
void Finalize(CGameEntity* object) // its vtable +0x20
{
    reinterpret_cast<void(__thiscall*)(CGameEntity*)>(object->m_vtable[0x20 / 4])(object);
}

// The task system (Tsk): a task's constructor (priority), its removal from its list, and the main loop's logic task
// list's add.
void ConstructTask(void* task, int priority) { static_cast<TaskList::SchedulerTask*>(task)->Construct(priority); }
void RemoveTask(void* task) { static_cast<TaskList::SchedulerTask*>(task)->Remove(); }
void RegisterLogicTask(void* task)
{
    AssetStore::g_mainLoopManager->m_logicTasks.AddTask(static_cast<TaskList::SchedulerTask*>(task));
}

// The node after `node`, or null at the head; a node's object (null for none).
MemSys::ListNode* NextNode(MemSys::ListNode* node)
{
    MemSys::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
CGameEntity* ObjectOf(MemSys::ListNode* node) { return node ? static_cast<CGameEntity*>(node->data) : nullptr; }

// The walk Update and Pause share (CObjectHubBase::ForEach's, with the work inline): `visit` once for each object,
// starting again from the front (past the objects done) whenever the list changes.
template <typename Visit>
void WalkObjects(CObjectHubBase* manager, Visit visit)
{
    uint32_t bit = 1;
    while (manager->m_walking & bit)
        bit <<= 1;
    manager->m_walking |= bit;
    for (MemSys::ListNode* node = NextNode(&manager->m_entities); node && node->data; node = NextNode(node))
        static_cast<CObject*>(node->data)->m_visitedMask &= ~bit;
    do {
        manager->m_restartWalks &= ~bit;
        MemSys::ListNode* node = NextNode(&manager->m_entities);
        CGameEntity* object = ObjectOf(node);
        while (object) {
            MemSys::ListNode* nextNode = NextNode(node);
            CGameEntity* next = ObjectOf(nextNode);
            if (!(object->m_visitedMask & bit)) {
                object->m_visitedMask |= bit;
                visit(object);
                if (manager->m_restartWalks & bit)
                    break;
            }
            node = nextNode;
            object = next;
        }
    } while (manager->m_restartWalks & bit);
    manager->m_walking &= ~bit;
}

} // namespace

CEntityHub*& CompositeObjectManager() { return g_compositeObjectManager; }

// 0x0045F6E0
CEntityHub* CEntityHub::Construct()
{
    CMiscHub::Construct();
    m_vtable = kManagerVtable;
    auto* task = static_cast<uint32_t*>(MemSys::AllocZeroed(0x20));
    if (task) {
        ConstructTask(task, kLogicTaskPriority);
        task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        task[6] = reinterpret_cast<uint32_t>(&CompositeObjectManagerTask);
        task[7] = reinterpret_cast<uint32_t>(this);
    }
    m_logicTask = task;
    RegisterLogicTask(task);
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kObjectMemberFunctionType),
                                        g_objectMemberFunctions, 0x18);
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kCompositeMemberFunctionType),
                                        g_compositeObjectMemberFunctions, 0x18);
    return this;
}

// 0x0045F200
void CEntityHub::DestroyBody()
{
    m_vtable = kManagerVtable;
    if (m_logicTask) {
        RemoveTask(m_logicTask);
        if (m_logicTask) { // (its deleting destructor, through the vtable)
            auto* task = static_cast<void***>(m_logicTask);
            reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*task)[2])(task, 1);
        }
    }
    CMiscHub::DestroyBody();
}

// 0x0045F300
CEntityHub* CEntityHub::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0045F270
void CEntityHub::RegisterComponent(uint32_t type, ComponentCreateFunction create,
                                                ComponentRegisterFunction registerFunction)
{
    m_components[m_componentTypes].type = type;
    m_components[m_componentTypes].create = create;
    ++m_componentTypes;
    if (registerFunction)
        registerFunction();
}

// 0x0045F2A0
CObjectModule* CEntityHub::CreateComponent(uint32_t type)
{
    for (uint32_t i = 0; i < m_componentTypes; ++i) {
        if (m_components[i].type == type)
            return m_components[i].create();
    }
    return nullptr;
}

// 0x0045F2D0
CObjectModule* CEntityHub::FindFirstModuleOfKind(uint32_t type)
{
    for (uint32_t i = 0; i < m_componentTypes; ++i) {
        if (m_components[i].type == type)
            return ComponentList(i);
    }
    return nullptr;
}

// 0x0045F3E0
void CEntityHub::AttachModuleOfKind(CObjectModule* component)
{
    uint32_t type = TypeOf(component);
    for (uint32_t i = 0; i < m_componentTypes; ++i) {
        if (m_components[i].type == type) {
            NextOfType(component) = ComponentList(i);
            ComponentList(i) = component;
            return;
        }
    }
}

// 0x0045F420. (A type registered twice: the lists of each of its entries are searched, in order.)
void CEntityHub::DetachModuleOfKind(CObjectModule* component)
{
    uint32_t type = TypeOf(component);
    for (uint32_t i = 0; i < m_componentTypes; ++i) {
        if (m_components[i].type != type)
            continue;
        CObjectModule* previous = ComponentList(i);
        if (previous == component) {
            ComponentList(i) = NextOfType(component);
            NextOfType(component) = nullptr;
            return;
        }
        for (; previous; previous = NextOfType(previous)) {
            if (NextOfType(previous) == component) {
                NextOfType(previous) = NextOfType(component);
                NextOfType(component) = nullptr;
                return;
            }
        }
    }
}

// 0x0045F330
CGameEntity* CEntityHub::CreateCompositeObject()
{
    void* memory = MemSys::AllocZeroed(0x108);
    CGameEntity* object = memory ? ConstructCompositeObject(memory) : nullptr;
    object->SetType(kCompositeObjectType);
    VirtualRegisterObject(object);
    return object;
}

// 0x0045F3A0
CGameEntity* CEntityHub::SpawnEntityFromNode(Scripts::CArray* components,
                                                                         Scripts::CStruct* nodeData, bool finalize)
{
    CGameEntity* object = CreateCompositeObject();
    ConfigureFromData(object, nodeData);
    BuildModulesFromList(object, components, nodeData);
    if (finalize)
        Finalize(object);
    return object;
}

// 0x0045F4A0
void CEntityHub::Update()
{
    WalkObjects(this, [](CGameEntity* object) { UpdateCompositeObject(object); });
}

// 0x0045F5D0
void CEntityHub::Pause(bool pause)
{
    WalkObjects(this, [pause](CGameEntity* object) {
        if (pause)
            CompositeFlags(object) |= COMPOSITE_FLAG_PAUSED;
        else
            CompositeFlags(object) &= ~COMPOSITE_FLAG_PAUSED;
    });
}

// 0x0045F6D0
void __cdecl CompositeObjectManagerTask(void* task)
{
    static_cast<CEntityHub*>(static_cast<void**>(task)[7])->Update();
}

// 0x0045F7A0
CEntityHub* CreateCompositeObjectManager(bool create)
{
    if (!CompositeObjectManager() && create) {
        void* memory = MemSys::AllocZeroed(sizeof(CEntityHub), 1);
        CompositeObjectManager() = memory ? static_cast<CEntityHub*>(memory)->Construct() : nullptr;
    }
    ++RefTally();
    return CompositeObjectManager();
}

// 0x0045F1E0
void ReleaseCompositeObjectManager()
{
    uint32_t& references = RefTally();
    if (references > 1) {
        --references;
        return;
    }
    CompositeObjectManager() = nullptr; // (not deleted)
    references = 0;
}

// --- the COIM heap ---

namespace {

// The instance list (a std::list of objects with a virtual deleting destructor in slot 1): its head node and size.
struct MaterializeNode {
    void* object; // the object (its vtable first)
    MaterializeNode* next;
    MaterializeNode* prev;
};
MaterializeNode* g_instanceListHead;
MaterializeNode*& InstanceListHead() { return g_instanceListHead; }
uint32_t& InstanceCount() { return g_instanceCount; }
// std::list::erase(first, last) (0x004ECC30; the result iterator in `result`).
void EraseInstances(MaterializeNode** result, MaterializeNode* first, MaterializeNode* last)
{
    reinterpret_cast<PointerList*>(&g_instanceListHead)->Erase(reinterpret_cast<PointerListNode**>(result),
                                                           reinterpret_cast<PointerListNode*>(first),
                                                           reinterpret_cast<PointerListNode*>(last));
}

MemSys::Region*& CoimRegion() { return g_coimRegion; }
MemSys::Heap*& CoimHeap() { return g_coimHeap; }
int& CoimA() { return g_coimA; }
int& CoimB() { return g_coimB; }
int& CoimC() { return g_coimC; }
int& CoimInteger0() { return g_coimInteger0; } // (4 until InitCOIM)
int& CoimInteger1() { return g_coimInteger1; } // (2 until InitCOIM)

} // namespace

// 0x0045EFD0
void DestroyCoimHeap()
{
    DebugOut::Printf("Destroying COIM heap\n");
    if (InstanceCount()) {
        MaterializeNode* head = InstanceListHead();
        MaterializeNode* node = head ? head->next : nullptr;
        if (node != head) {
            do {
                if (node->object) {
                    void** vtable = *static_cast<void***>(node->object); // (slot 1: its deleting destructor)
                    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[1])(node->object, 1);
                    node->object = nullptr;
                }
                MaterializeNode* next = node->next;
                if (node != InstanceListHead()) {
                    node->prev->next = node->next;
                    node->next->prev = node->prev;
                    MemSys::Manager::Get()->Free(node);
                    --InstanceCount();
                }
                node = next;
            } while (node != InstanceListHead());
        }
        head = InstanceListHead();
        MaterializeNode* result;
        EraseInstances(&result, head ? head->next : nullptr, head);
    }
    if (CoimHeap()) {
        MemSys::Manager::Get()->DeleteHeap(CoimHeap());
        if (CoimRegion()) // (its scalar deleting destructor, through the vtable)
            reinterpret_cast<void*(__thiscall*)(MemSys::Region*, unsigned int)>(CoimRegion()->m_vtable[0])(CoimRegion(), 1);
        CoimHeap() = nullptr;
        CoimRegion() = nullptr;
    }
}

// 0x0045F0B0
bool CreateCoimHeap(uint32_t size)
{
    DestroyCoimHeap();
    if (size) {
        void* memory = MemSys::AllocZeroed(sizeof(MemSys::MemoryArena));
        CoimRegion() = memory ? new (memory) MemSys::MemoryArena(size) : nullptr;
        CoimHeap() = MemSys::Manager::Get()->AddHeap(CoimRegion(), 1, "COIMHeap");
        CoimHeap()->m_unknown64 = true;
        CoimHeap()->m_unknown68 = CoimB() - g_compositeMgrBlockHeaderSize; // (16: a block header)
        Scripts::FetchIntValue(0xBA2C7CCB); // (the result isn't used)
    }
    return false;
}

// 0x0045F160
void InitCoim(int a, uint32_t heapSize, int b, int c)
{
    CoimA() = a;
    CoimB() = b;
    CoimInteger0() = Scripts::FetchIntValue(0xB5527B89);
    CoimInteger1() = Scripts::FetchIntValue(0xD84C26CF);
    CoimC() = c;
    CreateCoimHeap(heapSize);
    DebugOut::Printf("Initializing CCompositeObjectInstanceMgr at %dk\n", heapSize >> 10);
}

// 0x0045F1C0
void ResizeCoimHeap(uint32_t size)
{
    if (size)
        CreateCoimHeap(size);
    else
        DestroyCoimHeap();
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kManagerVtable_slots[] = {&GameObjects::CEntityHub::Destroy,
         &GameObjects::CObjectHubBase::RegisterObject,
         &GameObjects::CObjectHubBase::DropEntityRegistration,
         &GameObjects::CObjectHubBase::ReassignObjectId,
         &GameObjects::CObjectHubBase::SetEntityRank,
         &GameObjects::CMiscHub::RemoveEntity,
         &MenuSystem::CUiTree::FetchObjectSet};
void** const kManagerVtable = reinterpret_cast<void**>(kManagerVtable_slots);

} // namespace
} // namespace Obj

namespace GameObjects {

namespace {

// A list (std::list<T*>) nothing uses but its static initialiser and exit function: an empty head node.
ScriptCommands::PointerList g_spareObjectList;

} // namespace

// 0x006409F0 (a static initialiser)
void ConstructSpareObjectList() { g_spareObjectList.Construct(); }

// 0x00640A10 (a static initialiser): the instance list (its head and the count after it), empty.
void ConstructInstanceList() { reinterpret_cast<ScriptCommands::PointerList*>(&g_instanceListHead)->Construct(); }

} // namespace Obj
