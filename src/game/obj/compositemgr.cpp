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
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/obj/objman.h"
#include "game/front/screen_element_manager.h"
#include "hook/game_code.h"

namespace Obj {

GAME_GLOBAL(uint32_t[186], g_compositeObjectMemberFunctions, 0x00683268,
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
    0, 0x0ADA4E74, 0, 0, 0, 0);

GAME_GLOBAL(uint32_t[102], g_objectMemberFunctions, 0x00683550,
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
    0, 0x57525413, 0, 0, 0, 0);

GAME_GLOBAL(int, g_compositeMgrBlockHeaderSize, 0x00658454, 16);
GAME_GLOBAL(int, g_coimInteger0, 0x0067D804, 4);
GAME_GLOBAL(int, g_coimInteger1, 0x0067D808, 2);
GAME_GLOBAL(int, g_coimA, 0x006F0D8C);
GAME_GLOBAL(int, g_coimB, 0x006F0D90);
GAME_GLOBAL(int, g_coimC, 0x006F0D98);
GAME_GLOBAL(Mem::Region*, g_coimRegion, 0x006F0DA0);
GAME_GLOBAL(Mem::Heap*, g_coimHeap, 0x006F0DA4);
GAME_GLOBAL(uint32_t, g_instanceCount, 0x006F0DB4);
GAME_GLOBAL(CBaseComponent*[82], g_componentList, 0x006F0DB8);
GAME_GLOBAL(Obj::CCompositeObjectManager*, g_compositeObjectManager, 0x006F0FB8);
GAME_GLOBAL(uint32_t, g_compositeMgrReferences, 0x006F0FBC);

namespace {

extern void** const kManagerVtable;
constexpr uint32_t kCompositeObjectType = 0xCDEB9229;
constexpr uint32_t kObjectMemberFunctionType = 0x57525413;    // the table at 0x00683550 (CObject's)
constexpr uint32_t kCompositeMemberFunctionType = 0x0ADA4E74; // the table at 0x00683268 (CCompositeObject's)

uint32_t& References() { return g_compositeMgrReferences; }

// The components of each type (index = the type's index in m_components), linked through their +0x14.
CBaseComponent*& ComponentList(uint32_t index) { return g_componentList[index]; }
// A component's type and its link to the next of its type.
uint32_t TypeOf(CBaseComponent* component) { return component->m_type; }
CBaseComponent*& NextOfType(CBaseComponent* component) { return component->mp_nextOfType; }

// A composite object's flags.
uint32_t& CompositeFlags(CCompositeObject* object) { return object->m_compositeFlags; }

// CCompositeObject's code (composite.cpp).
CCompositeObject* ConstructCompositeObject(void* memory)
{
    return static_cast<CCompositeObject*>(memory)->Construct();
}
void UpdateCompositeObject(CCompositeObject* object) { object->Update(); }
void InitFromStructure(CCompositeObject* object, Script::CStruct* nodeData) { object->InitFromStructure(nodeData); }
void CreateComponentsFromArray(CCompositeObject* object, Script::CArray* components, Script::CStruct* nodeData)
{
    object->CreateComponentsFromArray(components, nodeData);
}
void Finalize(CCompositeObject* object) // its vtable +0x20
{
    reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(object->m_vtable[0x20 / 4])(object);
}

// The task system (Tsk): a task's constructor (priority), its removal from its list, and the main loop's logic task
// list's add.
void ConstructTask(void* task, int priority) { static_cast<Tsk::BaseTask*>(task)->Construct(priority); }
void RemoveTask(void* task) { static_cast<Tsk::BaseTask*>(task)->Remove(); }
void AddLogicTask(void* task)
{
    Ass::g_mainLoopManager->m_logicTasks.AddTask(static_cast<Tsk::BaseTask*>(task));
}

// The node after `node`, or null at the head; a node's object (null for none).
Mem::ListNode* NextNode(Mem::ListNode* node)
{
    Mem::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
CCompositeObject* ObjectOf(Mem::ListNode* node) { return node ? static_cast<CCompositeObject*>(node->data) : nullptr; }

// The walk Update and Pause share (CBaseManager::ForEach's, with the work inline): `visit` once for each object,
// starting again from the front (past the objects done) whenever the list changes.
template <typename Visit>
void WalkObjects(CBaseManager* manager, Visit visit)
{
    uint32_t bit = 1;
    while (manager->m_walking & bit)
        bit <<= 1;
    manager->m_walking |= bit;
    for (Mem::ListNode* node = NextNode(&manager->m_objects); node && node->data; node = NextNode(node))
        static_cast<CObject*>(node->data)->m_visitedMask &= ~bit;
    do {
        manager->m_restartWalks &= ~bit;
        Mem::ListNode* node = NextNode(&manager->m_objects);
        CCompositeObject* object = ObjectOf(node);
        while (object) {
            Mem::ListNode* nextNode = NextNode(node);
            CCompositeObject* next = ObjectOf(nextNode);
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

CCompositeObjectManager*& CompositeObjectManager() { return g_compositeObjectManager; }

// 0x0045F6E0
CCompositeObjectManager* CCompositeObjectManager::Construct()
{
    CGeneralManager::Construct();
    m_vtable = kManagerVtable;
    auto* task = static_cast<uint32_t*>(Mem::Calloc(0x20));
    if (task) {
        ConstructTask(task, kLogicTaskPriority);
        task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
        task[6] = reinterpret_cast<uint32_t>(&CompositeObjectManagerTask);
        task[7] = reinterpret_cast<uint32_t>(this);
    }
    m_logicTask = task;
    AddLogicTask(task);
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kObjectMemberFunctionType),
                                        g_objectMemberFunctions, 0x18);
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kCompositeMemberFunctionType),
                                        g_compositeObjectMemberFunctions, 0x18);
    return this;
}

// 0x0045F200
void CCompositeObjectManager::DestroyBody()
{
    m_vtable = kManagerVtable;
    if (m_logicTask) {
        RemoveTask(m_logicTask);
        if (m_logicTask) { // (its deleting destructor, through the vtable)
            auto* task = static_cast<void***>(m_logicTask);
            reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*task)[2])(task, 1);
        }
    }
    CGeneralManager::DestroyBody();
}

// 0x0045F300
CCompositeObjectManager* CCompositeObjectManager::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0045F270
void CCompositeObjectManager::RegisterComponent(uint32_t type, ComponentCreateFunction create,
                                                ComponentRegisterFunction registerFunction)
{
    m_components[m_componentTypes].type = type;
    m_components[m_componentTypes].create = create;
    ++m_componentTypes;
    if (registerFunction)
        registerFunction();
}

// 0x0045F2A0
CBaseComponent* CCompositeObjectManager::CreateComponent(uint32_t type)
{
    for (uint32_t i = 0; i < m_componentTypes; ++i) {
        if (m_components[i].type == type)
            return m_components[i].create();
    }
    return nullptr;
}

// 0x0045F2D0
CBaseComponent* CCompositeObjectManager::GetFirstComponentByType(uint32_t type)
{
    for (uint32_t i = 0; i < m_componentTypes; ++i) {
        if (m_components[i].type == type)
            return ComponentList(i);
    }
    return nullptr;
}

// 0x0045F3E0
void CCompositeObjectManager::AddComponentByType(CBaseComponent* component)
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
void CCompositeObjectManager::RemoveComponentByType(CBaseComponent* component)
{
    uint32_t type = TypeOf(component);
    for (uint32_t i = 0; i < m_componentTypes; ++i) {
        if (m_components[i].type != type)
            continue;
        CBaseComponent* previous = ComponentList(i);
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
CCompositeObject* CCompositeObjectManager::CreateCompositeObject()
{
    void* memory = Mem::Calloc(0x108);
    CCompositeObject* object = memory ? ConstructCompositeObject(memory) : nullptr;
    object->SetType(kCompositeObjectType);
    VirtualRegisterObject(object);
    return object;
}

// 0x0045F3A0
CCompositeObject* CCompositeObjectManager::CreateCompositeObjectFromNode(Script::CArray* components,
                                                                         Script::CStruct* nodeData, bool finalize)
{
    CCompositeObject* object = CreateCompositeObject();
    InitFromStructure(object, nodeData);
    CreateComponentsFromArray(object, components, nodeData);
    if (finalize)
        Finalize(object);
    return object;
}

// 0x0045F4A0
void CCompositeObjectManager::Update()
{
    WalkObjects(this, [](CCompositeObject* object) { UpdateCompositeObject(object); });
}

// 0x0045F5D0
void CCompositeObjectManager::Pause(bool pause)
{
    WalkObjects(this, [pause](CCompositeObject* object) {
        if (pause)
            CompositeFlags(object) |= COMPOSITE_FLAG_PAUSED;
        else
            CompositeFlags(object) &= ~COMPOSITE_FLAG_PAUSED;
    });
}

// 0x0045F6D0
void __cdecl CompositeObjectManagerTask(void* task)
{
    static_cast<CCompositeObjectManager*>(static_cast<void**>(task)[7])->Update();
}

// 0x0045F7A0
CCompositeObjectManager* CreateCompositeObjectManager(bool create)
{
    if (!CompositeObjectManager() && create) {
        void* memory = Mem::Calloc(sizeof(CCompositeObjectManager), 1);
        CompositeObjectManager() = memory ? static_cast<CCompositeObjectManager*>(memory)->Construct() : nullptr;
    }
    ++References();
    return CompositeObjectManager();
}

// 0x0045F1E0
void ReleaseCompositeObjectManager()
{
    uint32_t& references = References();
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
struct InstanceNode {
    void* object; // the object (its vtable first)
    InstanceNode* next;
    InstanceNode* prev;
};
GAME_GLOBAL(InstanceNode*, g_instanceListHead, 0x006F0DB0);
InstanceNode*& InstanceListHead() { return g_instanceListHead; }
uint32_t& InstanceCount() { return g_instanceCount; }
// std::list::erase(first, last) (0x004ECC30; the result iterator in `result`).
void EraseInstances(InstanceNode** result, InstanceNode* first, InstanceNode* last)
{
    reinterpret_cast<PointerList*>(&g_instanceListHead)->Erase(reinterpret_cast<PointerListNode**>(result),
                                                           reinterpret_cast<PointerListNode*>(first),
                                                           reinterpret_cast<PointerListNode*>(last));
}

Mem::Region*& CoimRegion() { return g_coimRegion; }
Mem::Heap*& CoimHeap() { return g_coimHeap; }
int& CoimA() { return g_coimA; }
int& CoimB() { return g_coimB; }
int& CoimC() { return g_coimC; }
int& CoimInteger0() { return g_coimInteger0; } // (4 until InitCOIM)
int& CoimInteger1() { return g_coimInteger1; } // (2 until InitCOIM)

} // namespace

// 0x0045EFD0
void DestroyCoimHeap()
{
    Dbg::Printf("Destroying COIM heap\n");
    if (InstanceCount()) {
        InstanceNode* head = InstanceListHead();
        InstanceNode* node = head ? head->next : nullptr;
        if (node != head) {
            do {
                if (node->object) {
                    void** vtable = *static_cast<void***>(node->object); // (slot 1: its deleting destructor)
                    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[1])(node->object, 1);
                    node->object = nullptr;
                }
                InstanceNode* next = node->next;
                if (node != InstanceListHead()) {
                    node->prev->next = node->next;
                    node->next->prev = node->prev;
                    Mem::Manager::Get()->Free(node);
                    --InstanceCount();
                }
                node = next;
            } while (node != InstanceListHead());
        }
        head = InstanceListHead();
        InstanceNode* result;
        EraseInstances(&result, head ? head->next : nullptr, head);
    }
    if (CoimHeap()) {
        Mem::Manager::Get()->DeleteHeap(CoimHeap());
        if (CoimRegion()) // (its scalar deleting destructor, through the vtable)
            reinterpret_cast<void*(__thiscall*)(Mem::Region*, unsigned int)>(CoimRegion()->m_vtable[0])(CoimRegion(), 1);
        CoimHeap() = nullptr;
        CoimRegion() = nullptr;
    }
}

// 0x0045F0B0
bool CreateCoimHeap(uint32_t size)
{
    DestroyCoimHeap();
    if (size) {
        void* memory = Mem::Calloc(sizeof(Mem::AllocRegion));
        CoimRegion() = memory ? new (memory) Mem::AllocRegion(size) : nullptr;
        CoimHeap() = Mem::Manager::Get()->AddHeap(CoimRegion(), 1, "COIMHeap");
        CoimHeap()->m_unknown64 = true;
        CoimHeap()->m_unknown68 = CoimB() - g_compositeMgrBlockHeaderSize; // (16: a block header)
        Script::GetInteger(0xBA2C7CCB); // (the result isn't used)
    }
    return false;
}

// 0x0045F160
void InitCoim(int a, uint32_t heapSize, int b, int c)
{
    CoimA() = a;
    CoimB() = b;
    CoimInteger0() = Script::GetInteger(0xB5527B89);
    CoimInteger1() = Script::GetInteger(0xD84C26CF);
    CoimC() = c;
    CreateCoimHeap(heapSize);
    Dbg::Printf("Initializing CCompositeObjectInstanceMgr at %dk\n", heapSize >> 10);
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

REPLACE(0x0045F6E0, &Obj::CCompositeObjectManager::Construct);
REPLACE(0x0045F200, &Obj::CCompositeObjectManager::DestroyBody);
REPLACE(0x0045F300, &Obj::CCompositeObjectManager::Destroy);
REPLACE(0x0045F270, &Obj::CCompositeObjectManager::RegisterComponent);
REPLACE(0x0045F2A0, &Obj::CCompositeObjectManager::CreateComponent);
REPLACE(0x0045F2D0, &Obj::CCompositeObjectManager::GetFirstComponentByType);
REPLACE(0x0045F3E0, &Obj::CCompositeObjectManager::AddComponentByType);
REPLACE(0x0045F420, &Obj::CCompositeObjectManager::RemoveComponentByType);
REPLACE(0x0045F330, &Obj::CCompositeObjectManager::CreateCompositeObject);
REPLACE(0x0045F3A0, &Obj::CCompositeObjectManager::CreateCompositeObjectFromNode);
REPLACE(0x0045F4A0, &Obj::CCompositeObjectManager::Update);
REPLACE(0x0045F5D0, &Obj::CCompositeObjectManager::Pause);
REPLACE(0x0045F6D0, Obj::CompositeObjectManagerTask);
REPLACE(0x0045F1E0, Obj::ReleaseCompositeObjectManager);
REPLACE(0x0045EFD0, Obj::DestroyCoimHeap);
REPLACE(0x0045F7A0, Obj::CreateCompositeObjectManager);
REPLACE(0x0045F0B0, Obj::CreateCoimHeap);
REPLACE(0x0045F160, Obj::InitCoim);
REPLACE(0x0045F1C0, Obj::ResizeCoimHeap);

namespace Obj {
namespace {

GAME_VTABLE(kManagerVtable, 0x006476D4,
    &Obj::CCompositeObjectManager::Destroy,
    &Obj::CBaseManager::RegisterObject,
    &Obj::CBaseManager::UnregisterObject,
    &Obj::CBaseManager::ReassignObjectId,
    &Obj::CBaseManager::SetObjectPriority,
    &Obj::CGeneralManager::KillObject,
    &Front::CScreenElementManager::GetObjectList);

} // namespace
} // namespace Obj
