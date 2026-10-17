// The composite object manager and the COIM heap (compositemgr.h).

#include "game/obj/compositemgr.h"

#include <cstdint>
#include <new>

#include "game/core/alloc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kManagerVtable = reinterpret_cast<void**>(0x006476D4);
void** const kTaskVtable = reinterpret_cast<void**>(0x006476F0); // Tsk::Task<CCompositeObjectManager>
constexpr uint32_t kCompositeObjectType = 0xCDEB9229;
constexpr uint32_t kObjectMemberFunctionType = 0x57525413;    // the table at 0x00683550 (CObject's)
constexpr uint32_t kCompositeMemberFunctionType = 0x0ADA4E74; // the table at 0x00683268 (CCompositeObject's)

uint32_t& References() { return GameVariable<uint32_t>(0x006F0FBC); }

// The components of each type (index = the type's index in m_components), linked through their +0x14.
CBaseComponent*& ComponentList(uint32_t index) { return GameVariable<CBaseComponent*>(0x006F0DB8 + 4 * index); }
// A component's type (+0x04) and its link to the next of its type (+0x14).
uint32_t TypeOf(CBaseComponent* component) { return reinterpret_cast<uint32_t*>(component)[1]; }
CBaseComponent*& NextOfType(CBaseComponent* component) { return reinterpret_cast<CBaseComponent**>(component)[5]; }

// A composite object's +0x104 flags.
uint32_t& CompositeFlags(CCompositeObject* object)
{
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(object) + 0x104);
}

// CCompositeObject's code (wt/cobj's range, not rewritten here).
CCompositeObject* ConstructCompositeObject(void* memory) // -> 0x0045E510 (wt/cobj)
{
    return GameFunction<CCompositeObject*(__thiscall*)(void*)>(0x0045E510)(memory);
}
void UpdateCompositeObject(CCompositeObject* object) // -> 0x0045D920 (wt/cobj)
{
    GameFunction<void(__thiscall*)(CCompositeObject*)>(0x0045D920)(object);
}
void InitFromStructure(CCompositeObject* object, Script::CStruct* nodeData) // -> 0x0045DAD0 (wt/cobj)
{
    GameFunction<void(__thiscall*)(CCompositeObject*, Script::CStruct*)>(0x0045DAD0)(object, nodeData);
}
void CreateComponentsFromArray(CCompositeObject* object, Script::CArray* components,
                               Script::CStruct* nodeData) // -> 0x0045DA90 (wt/cobj)
{
    GameFunction<void(__thiscall*)(CCompositeObject*, Script::CArray*, Script::CStruct*)>(0x0045DA90)(
        object, components, nodeData);
}
void Finalize(CCompositeObject* object) // its vtable +0x20
{
    reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(object->m_vtable[0x20 / 4])(object);
}

// The task system (Tsk, not rewritten yet): a task's constructor (priority), its removal from its list, and the
// main loop's logic task list's add.
void ConstructTask(void* task, int priority) { GameFunction<void*(__thiscall*)(void*, int)>(0x00406890)(task, priority); }
void RemoveTask(void* task) { GameFunction<void(__thiscall*)(void*)>(0x004068F0)(task); }
void AddLogicTask(void* task)
{
    auto* mlp = GameVariable<uint8_t*>(0x00696D68); // Mlp::Manager: m_logicTasks at +0x04
    GameFunction<void(__thiscall*)(void*, void*)>(0x00406C90)(mlp + 0x04, task);
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

CCompositeObjectManager*& CompositeObjectManager() { return GameVariable<CCompositeObjectManager*>(0x006F0FB8); }

// 0x0045F6E0
CCompositeObjectManager* CCompositeObjectManager::Construct()
{
    CGeneralManager::Construct();
    m_vtable = kManagerVtable;
    auto* task = static_cast<uint32_t*>(Mem::Calloc(0x20));
    if (task) {
        ConstructTask(task, kLogicTaskPriority);
        task[0] = reinterpret_cast<uint32_t>(kTaskVtable);
        task[6] = reinterpret_cast<uint32_t>(&CompositeObjectManagerTask);
        task[7] = reinterpret_cast<uint32_t>(this);
    }
    m_logicTask = task;
    AddLogicTask(task);
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kObjectMemberFunctionType),
                                        reinterpret_cast<const uint32_t*>(0x00683550), 0x18);
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kCompositeMemberFunctionType),
                                        reinterpret_cast<const uint32_t*>(0x00683268), 0x18);
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
InstanceNode*& InstanceListHead() { return GameVariable<InstanceNode*>(0x006F0DB0); }
uint32_t& InstanceCount() { return GameVariable<uint32_t>(0x006F0DB4); }
// std::list::erase(first, last) (0x004ECC30; the result iterator in `result`).
void EraseInstances(InstanceNode** result, InstanceNode* first, InstanceNode* last)
{
    GameFunction<InstanceNode**(__thiscall*)(void*, InstanceNode**, InstanceNode*, InstanceNode*)>(0x004ECC30)(
        reinterpret_cast<void*>(0x006F0DB0), result, first, last);
}

Mem::Region*& CoimRegion() { return GameVariable<Mem::Region*>(0x006F0DA0); }
Mem::Heap*& CoimHeap() { return GameVariable<Mem::Heap*>(0x006F0DA4); }
int& CoimA() { return GameVariable<int>(0x006F0D8C); }
int& CoimB() { return GameVariable<int>(0x006F0D90); }
int& CoimC() { return GameVariable<int>(0x006F0D98); }
int& CoimInteger0() { return GameVariable<int>(0x0067D804); } // (4 until InitCOIM)
int& CoimInteger1() { return GameVariable<int>(0x0067D808); } // (2 until InitCOIM)

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
        CoimHeap()->m_unknown68 = CoimB() - GameVariable<int>(0x00658454); // (16: a block header)
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
REPLACE(0x0045F0B0, Obj::CreateCoimHeap);
REPLACE(0x0045F160, Obj::InitCoim);
REPLACE(0x0045F1C0, Obj::ResizeCoimHeap);
