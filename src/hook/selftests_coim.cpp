// Self-tests for the composite object manager and the COIM heap (src/game/obj/compositemgr.cpp): the original and
// ours run the same calls on identical stand-in managers, objects and components in an arena. What they call
// outside the range (CGeneralManager, CCompositeObject's constructor / update / set-up, SetType, the task system,
// the memory manager, the script integers, Dbg::Printf, the member function tables) is stood in for by recording
// code (over the game's function and ours alike), so the two are compared call by call, then the arena and the
// globals word by word.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CBaseComponent;
using Obj::CCompositeObject;
using Obj::CCompositeObjectManager;
using Obj::CObject;

constexpr uint32_t kArenaSize = 0x10000;
alignas(16) uint8_t g_coim_arenas[2][kArenaSize];
uint32_t g_coim_base;
uint32_t g_coim_bump;
bool g_coim_original;
bool g_coim_allocFail;
std::vector<Saved> g_coim_saved;

// (both arenas, the same for both runs: a value that only happens to fall inside one of them maps the same way in
// each - ArenaWord, selftest.h)
bool InArena(uint32_t word) { return word - reinterpret_cast<uint32_t>(g_coim_arenas) <= 2 * kArenaSize; }
uint32_t Rel(uint32_t word) { return ArenaWord(g_coim_arenas, kArenaSize, word); }
uint32_t Rel(const void* p) { return Rel(reinterpret_cast<uint32_t>(p)); }
void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
void Result(uint32_t value) { Rec(0x2E5, {value}); }
uint32_t Bump(uint32_t size, uint8_t fill)
{
    uint32_t block = g_coim_bump;
    g_coim_bump += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(block), fill, size);
    return block;
}

void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_coim_saved);
    g_coim_saved.clear();
}

// --- the objects' update: what it does to the walk ---
CCompositeObjectManager* g_coim_manager;
CCompositeObject* g_coim_unlinkAt;   // here g_coim_unlinkWhat is unregistered (out of the list, walks restart)
CCompositeObject* g_coim_unlinkWhat;
CCompositeObject* g_coim_linkAt;     // here g_coim_linkWhat is registered (at the front, walks restart)
CCompositeObject* g_coim_linkWhat;
CCompositeObject* g_coim_nestedAt;   // here an Update starts inside the walk
CCompositeObject* g_coim_pauseAt;    // here a Pause(true) starts inside the walk
CCompositeObject* g_coim_quietUnlinkAt; // here the next object is unlinked without a restart

template <typename F>
void* Code(uint32_t address, F ours)
{
    return g_coim_original ? reinterpret_cast<void*>(address) : reinterpret_cast<void*>(AddressOf(ours));
}
template <typename R, typename F, typename... A>
R Call(uint32_t address, F ours, void* self, A... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(Code(address, ours))(self, args...);
}
template <typename R, typename F, typename... A>
R CallC(uint32_t address, F ours, A... args)
{
    return reinterpret_cast<R(__cdecl*)(A...)>(Code(address, ours))(args...);
}

void Unlink(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}
void LinkFront(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}
void LinkBack(Mem::ListNode* head, Mem::ListNode* node)
{
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

uint32_t& Flags104(void* object) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(object) + 0x104); }

uint32_t OnUpdate(Regs& r)
{
    auto* object = reinterpret_cast<CCompositeObject*>(r.ecx);
    Rec(0xD920, {r.ecx, object->m_visitedMask, g_coim_manager->m_walking, g_coim_manager->m_restartWalks});
    if (object == g_coim_unlinkAt) {
        g_coim_unlinkAt = nullptr;
        Unlink(&g_coim_unlinkWhat->m_node);
        g_coim_manager->m_restartWalks = 0xFFFFFFFF;
    }
    if (object == g_coim_linkAt) {
        g_coim_linkAt = nullptr;
        LinkFront(&g_coim_manager->m_objects, &g_coim_linkWhat->m_node);
        g_coim_manager->m_restartWalks = 0xFFFFFFFF;
    }
    if (object == g_coim_quietUnlinkAt) {
        g_coim_quietUnlinkAt = nullptr;
        Mem::ListNode* next = object->m_node.next;
        next->prev->next = next->next; // (its own links kept: the walk goes on from it)
        next->next->prev = next->prev;
    }
    if (object == g_coim_nestedAt) {
        g_coim_nestedAt = nullptr;
        Call<void>(0x0045F4A0, &CCompositeObjectManager::Update, g_coim_manager);
    }
    if (object == g_coim_pauseAt) {
        g_coim_pauseAt = nullptr;
        Call<void>(0x0045F5D0, &CCompositeObjectManager::Pause, g_coim_manager, true);
    }
    return 0;
}

// The component makers and register functions handed to RegisterComponent.
CBaseComponent* __cdecl Create1()
{
    Rec(0xC1, {});
    return reinterpret_cast<CBaseComponent*>(0xC0C00001);
}
CBaseComponent* __cdecl Create2()
{
    Rec(0xC2, {});
    return reinterpret_cast<CBaseComponent*>(0xC0C00002);
}
CBaseComponent* __cdecl Create3()
{
    Rec(0xC3, {});
    return nullptr;
}
void __cdecl Register1() { Rec(0xE1, {}); }
void __cdecl Register2() { Rec(0xE2, {}); }

void* g_coim_objectVtable[12];
void* g_coim_managerVtable[8];
void* g_coim_taskVtable[4];
void* g_coim_instanceVtable[2];
void* g_coim_regionVtable[2];

void InstallStandIns(StandIns& s)
{
    auto record = [&](std::initializer_list<uint32_t> addresses, uint16_t pop, uint32_t tag, int args, bool ecx,
                      uint32_t result) {
        s.At(addresses, pop, [=](Regs& r) {
            g_trace.push_back(tag);
            if (ecx)
                g_trace.push_back(Rel(r.ecx));
            for (int i = 0; i < args; ++i)
                g_trace.push_back(Rel(r.args[i]));
            return result == 0xEC ? r.ecx : result;
        });
    };
    // The memory manager.
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0xA15, {r.args[0]});
        return g_coim_allocFail ? 0u : Bump(r.args[0], 0);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) { // (New: the region's memory)
        Rec(0xA17, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0x0C000000u;
    });
    record({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, 0xA16, 1, true, 0);
    s.At({0x005BB4E0, AddressOf(&Mem::Manager::AddHeap)}, 12, [](Regs& r) {
        Rec(0xA18, {r.ecx, r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2]))});
        return Bump(sizeof(Mem::Heap), 0xEE);
    });
    record({0x005BABC0, AddressOf(&Mem::Manager::DeleteHeap)}, 4, 0xA19, 1, true, 0);
    // The script integers, Dbg::Printf, the member function tables.
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Rec(0x6EA1, {r.args[0]});
        return 0x100u + (r.args[0] & 0xFF);
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        const char* format = reinterpret_cast<const char*>(r.args[0]);
        Rec(0xDB6, {Hash(format), std::strchr(format, '%') ? r.args[1] : 0u}); // (no argument: stack garbage)
        return 0u;
    });
    record({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, 0x46F0, 3, false, 0);
    // CGeneralManager (objman.cpp): constructor and destructor body.
    record({0x00461BF0, AddressOf(&Obj::CGeneralManager::Construct)}, 0, 0x1BF0, 0, true, 0xEC);
    record({0x00461DF0, AddressOf(&Obj::CGeneralManager::DestroyBody)}, 0, 0x1DF0, 0, true, 0);
    // CObject::SetType; CCompositeObject's constructor, update and set-up (wt/cobj).
    record({0x00461970, AddressOf(&CObject::SetType)}, 4, 0x1970, 1, true, 0);
    s.At({0x0045E510, AddressOf(&Obj::CCompositeObject::Construct)}, 0, [](Regs& r) {
        Rec(0xE510, {r.ecx});
        *reinterpret_cast<void***>(r.ecx) = g_coim_objectVtable;
        return r.ecx;
    });
    s.At({0x0045D920, AddressOf(&Obj::CCompositeObject::Update)}, 0, OnUpdate);
    record({0x0045DAD0, AddressOf(&Obj::CCompositeObject::InitFromStructure)}, 4, 0xDAD0, 1, true, 0);
    record({0x0045DA90, AddressOf(&Obj::CCompositeObject::CreateComponentsFromArray)}, 8, 0xDA90, 2, true, 0);
    // The task system.
    record({0x00406890}, 4, 0x6890, 1, true, 0xEC);
    record({0x00406C90}, 4, 0x6C90, 1, true, 0);
    record({0x004068F0}, 0, 0x68F0, 0, true, 0);
    // Virtual functions.
    for (void*& slot : g_coim_objectVtable)
        slot = nullptr;
    g_coim_objectVtable[8] = s.Thunk(0, [](Regs& r) {
        Rec(0x0B08, {r.ecx});
        return 0u;
    });
    for (void*& slot : g_coim_managerVtable)
        slot = nullptr;
    g_coim_managerVtable[1] = s.Thunk(4, [](Regs& r) {
        Rec(0x3A01, {r.ecx, r.args[0]});
        return 0u;
    });
    g_coim_taskVtable[2] = s.Thunk(4, [](Regs& r) {
        Rec(0x7A5D, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_coim_instanceVtable[1] = s.Thunk(4, [](Regs& r) {
        Rec(0x1A57, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_coim_regionVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0x8E60, {r.ecx, r.args[0]});
        return r.ecx;
    });
}

void SaveGlobals()
{
    g_coim_saved = {
        {0x007D1800, 4, {}},     // the memory manager
        {0x007CC979, 1, {}},     // the memory manager is ready
        {0x00696D68, 4, {}},     // the main loop's task manager
        {0x006F0D8C, 0x22C, {}}, // the COIM globals, the component lists, the singleton and its references
        {0x0067D804, 8, {}},     // the COIM script integers
    };
    SaveAll(g_coim_saved);
}

uint8_t* BeginRun(bool original)
{
    g_coim_original = original;
    g_trace.clear();
    uint8_t* arena = g_coim_arenas[original ? 0 : 1];
    std::memset(arena, 0, kArenaSize);
    std::memset(arena + 0x8000, 0xDD, kArenaSize - 0x8000);
    g_coim_base = reinterpret_cast<uint32_t>(arena);
    g_coim_bump = g_coim_base + 0x8000;
    g_coim_allocFail = false;
    g_coim_unlinkAt = g_coim_unlinkWhat = g_coim_linkAt = g_coim_linkWhat = nullptr;
    g_coim_nestedAt = g_coim_pauseAt = g_coim_quietUnlinkAt = nullptr;
    for (Saved& saved : g_coim_saved)
        std::memset(reinterpret_cast<void*>(saved.address), 0, saved.size);
    GameVariable<void*>(0x007D1800) = arena + 0x7000;
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<uint8_t*>(0x00696D68) = arena + 0x7400;
    return arena;
}

std::vector<uint32_t> EndRun(uint8_t* arena)
{
    std::vector<uint32_t> result = g_trace;
    std::vector<uint32_t> image(kArenaSize / 4);
    std::memcpy(image.data(), arena, kArenaSize);
    const uint32_t task = AddressOf(&Obj::CompositeObjectManagerTask);
    const uint32_t taskPointer = reinterpret_cast<uint32_t>(&Obj::CompositeObjectManagerTask);
    for (uint32_t& word : image) {
        if (word == task || word == taskPointer)
            word = 0x0045F6D0;
        word = Rel(word);
    }
    result.insert(result.end(), image.begin(), image.end());
    for (const Saved& saved : g_coim_saved) {
        if (saved.address == 0x007D1800 || saved.address == 0x007CC979 || saved.address == 0x00696D68)
            continue;
        for (uint32_t offset = 0; offset < saved.size; offset += 4)
            result.push_back(Rel(GameVariable<uint32_t>(saved.address + offset)));
    }
    return result;
}

// A stand-in composite object in the arena (0x110 bytes each).
CCompositeObject* MakeObject(uint8_t* arena, int i, int priority)
{
    auto* object = reinterpret_cast<CCompositeObject*>(arena + 0x1000 + 0x110 * i);
    std::memset(object, 0, 0x110);
    object->m_vtable = g_coim_objectVtable;
    object->m_id = 0x0B000 + i;
    object->m_visitedMask = 0xF0F0F0F0u ^ (i * 0x11);
    object->m_node.vtable = reinterpret_cast<void**>(0x006477E4);
    object->m_node.data = object;
    object->m_node.priority = priority;
    object->m_node.next = &object->m_node;
    object->m_node.prev = &object->m_node;
    Flags104(object) = 0x30 + (i & 3);
    return object;
}
// A manager in the arena with its object list's head set up (CGeneralManager's constructor is stood in for).
CCompositeObjectManager* MakeManager(uint8_t* arena, uint32_t offset)
{
    auto* manager = reinterpret_cast<CCompositeObjectManager*>(arena + offset);
    manager->m_vtable = g_coim_managerVtable;
    Mem::ListNode* head = &manager->m_objects;
    head->vtable = reinterpret_cast<void**>(0x00647814);
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    manager->m_walking = 0;
    manager->m_restartWalks = 0;
    return manager;
}

// --- the manager: construction, component types and lists, making objects, destruction ---
std::vector<uint32_t> RunManagerTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    // Construct over dirty memory, then with no memory for the task.
    auto* manager = reinterpret_cast<CCompositeObjectManager*>(arena + 0x100);
    std::memset(manager, 0xCD, sizeof(CCompositeObjectManager));
    Result(Rel(Call<CCompositeObjectManager*>(0x0045F6E0, &CCompositeObjectManager::Construct, manager)));
    auto* manager2 = reinterpret_cast<CCompositeObjectManager*>(arena + 0x600);
    std::memset(manager2, 0xCD, 0x48);
    g_coim_allocFail = true;
    Result(Rel(Call<CCompositeObjectManager*>(0x0045F6E0, &CCompositeObjectManager::Construct, manager2)));
    g_coim_allocFail = false;
    // The task's code: the manager's Update (an empty list).
    MakeManager(arena, 0x600);
    g_coim_manager = manager2;
    LinkBack(&manager2->m_objects, &MakeObject(arena, 9, 0)->m_node);
    uint32_t task[8] = {};
    task[7] = reinterpret_cast<uint32_t>(manager2);
    CallC<void>(0x0045F6D0, &Obj::CompositeObjectManagerTask, static_cast<void*>(task));
    Result(manager2->m_walking);

    // Component types (manager zeroed as the singleton is): with and without a register function; 0x30 twice.
    manager = MakeManager(arena, 0x100);
    manager->m_componentTypes = 0;
    using RegisterFn = void(__thiscall*)(void*, uint32_t, Obj::ComponentCreateFunction, Obj::ComponentRegisterFunction);
    auto reg = [&](uint32_t type, Obj::ComponentCreateFunction create, Obj::ComponentRegisterFunction registerFn) {
        reinterpret_cast<RegisterFn>(Code(0x0045F270, &CCompositeObjectManager::RegisterComponent))(manager, type,
                                                                                                   create, registerFn);
    };
    reg(0x10, &Create1, &Register1);
    reg(0x20, &Create2, nullptr);
    reg(0x30, &Create3, &Register2);
    reg(0x40, &Create1, nullptr);
    reg(0x30, &Create2, nullptr);
    Result(manager->m_componentTypes);
    for (uint32_t type : {0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0u})
        Result(Call<uint32_t>(0x0045F2A0, &CCompositeObjectManager::CreateComponent, manager, type));

    // Component lists: components with types at +4 (0x50: not registered).
    uint32_t* components[9];
    const uint32_t types[9] = {0x10, 0x20, 0x10, 0x10, 0x30, 0x50, 0x20, 0x40, 0x30};
    for (int i = 0; i < 9; ++i) {
        components[i] = reinterpret_cast<uint32_t*>(arena + 0x3000 + 0x20 * i);
        components[i][1] = types[i];
        components[i][5] = 0xBADBAD00 + i;
    }
    auto first = [&](uint32_t type) {
        Result(Rel(Call<uint32_t>(0x0045F2D0, &CCompositeObjectManager::GetFirstComponentByType, manager, type)));
    };
    auto add = [&](int i) {
        Call<void>(0x0045F3E0, &CCompositeObjectManager::AddComponentByType, manager, components[i]);
    };
    auto remove = [&](int i) {
        Call<void>(0x0045F420, &CCompositeObjectManager::RemoveComponentByType, manager, components[i]);
    };
    for (uint32_t type : {0x10u, 0x20u, 0x50u})
        first(type);
    add(8); // (a 0x30 before component 4: 4 links to it)
    for (int i = 0; i < 8; ++i)
        add(i);
    for (uint32_t i = 0; i < 6; ++i)
        Result(Rel(GameVariable<uint32_t>(0x006F0DB8 + 4 * i))); // (the second 0x30 entry's list too)
    for (uint32_t type : {0x10u, 0x20u, 0x30u, 0x40u, 0x50u})
        first(type);
    remove(2); // the middle of 0x10's list (3, 2, 0)
    Result(components[2][5]);
    remove(3); // its head
    remove(5); // a type not registered
    remove(2); // no longer in the list
    remove(0); // the last one
    remove(4); // 0x30's only one
    // A 0x30 component in the second 0x30 entry's list: found there.
    GameVariable<uint32_t*>(0x006F0DB8 + 4 * 4) = components[4];
    components[4][5] = reinterpret_cast<uint32_t>(components[2]);
    components[2][1] = 0x30;
    components[2][5] = 0;
    remove(2);
    remove(1); // 0x20: the tail (6, 1)
    remove(7);
    for (uint32_t type : {0x10u, 0x20u, 0x30u, 0x40u})
        first(type);

    // Making composite objects: plain; from a node with and without Finalize; no memory (SetType on null).
    Result(Rel(Call<CCompositeObject*>(0x0045F330, &CCompositeObjectManager::CreateCompositeObject, manager)));
    auto* nodeData = reinterpret_cast<Script::CStruct*>(arena + 0x3800);
    auto* componentArray = reinterpret_cast<Script::CArray*>(arena + 0x3900);
    Result(Rel(Call<CCompositeObject*>(0x0045F3A0, &CCompositeObjectManager::CreateCompositeObjectFromNode, manager,
                                       componentArray, nodeData, true)));
    Result(Rel(Call<CCompositeObject*>(0x0045F3A0, &CCompositeObjectManager::CreateCompositeObjectFromNode, manager,
                                       componentArray, nodeData, false)));
    g_coim_allocFail = true;
    Result(Rel(Call<CCompositeObject*>(0x0045F330, &CCompositeObjectManager::CreateCompositeObject, manager)));
    g_coim_allocFail = false;

    // Destruction: with a task (removed, deleted), without; the scalar destructor with and without freeing.
    manager->m_logicTask = arena + 0x3A00;
    *reinterpret_cast<void***>(arena + 0x3A00) = g_coim_taskVtable;
    Call<void>(0x0045F200, &CCompositeObjectManager::DestroyBody, manager);
    Result(Rel(manager->m_vtable));
    manager->m_logicTask = nullptr;
    Result(Rel(Call<CCompositeObjectManager*>(0x0045F300, &CCompositeObjectManager::Destroy, manager, 1u)));
    manager->m_logicTask = arena + 0x3A00;
    Result(Rel(Call<CCompositeObjectManager*>(0x0045F300, &CCompositeObjectManager::Destroy, manager, 2u)));

    // The singleton's references: 5 -> 4, 2 -> 1, 1 -> gone, 0 -> gone.
    for (uint32_t references : {5u, 2u, 1u, 0u}) {
        GameVariable<CCompositeObjectManager*>(0x006F0FB8) = manager;
        GameVariable<uint32_t>(0x006F0FBC) = references;
        CallC<void>(0x0045F1E0, &Obj::ReleaseCompositeObjectManager);
        Result(Rel(GameVariable<uint32_t>(0x006F0FB8)));
        Result(GameVariable<uint32_t>(0x006F0FBC));
    }
    return EndRun(arena);
}

// --- the walks: Update and Pause ---
std::vector<uint32_t> RunWalkTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    CCompositeObjectManager* manager = MakeManager(arena, 0x100);
    g_coim_manager = manager;
    // Objects in list order (by priority, as the manager keeps them).
    CCompositeObject* objects[8];
    for (int i = 0; i < 8; ++i)
        objects[i] = MakeObject(arena, i, 10 - i);
    for (int i = 0; i < 5; ++i)
        LinkBack(&manager->m_objects, &objects[i]->m_node);
    auto update = [&] {
        Call<void>(0x0045F4A0, &CCompositeObjectManager::Update, manager);
        Result(manager->m_walking);
        Result(manager->m_restartWalks);
    };
    auto pause = [&](bool on) {
        Call<void>(0x0045F5D0, &CCompositeObjectManager::Pause, manager, on);
        Result(manager->m_walking);
        for (CCompositeObject* object : objects)
            Result(Flags104(object));
    };
    // Plain; walks in progress elsewhere (bits 0 and 2: this one gets bit 1).
    update();
    manager->m_walking = 0x5;
    update();
    manager->m_walking = 0;
    // An object unregistered and another registered on the way (restarts); a nested Update; a nested Pause.
    g_coim_unlinkAt = objects[1];
    g_coim_unlinkWhat = objects[3];
    g_coim_linkAt = objects[2];
    g_coim_linkWhat = objects[5];
    g_coim_nestedAt = objects[4];
    update();
    g_coim_pauseAt = objects[0];
    update();
    // The next object taken out by hand during an update: fetched before, so still updated.
    g_coim_quietUnlinkAt = static_cast<CCompositeObject*>(manager->m_objects.next->data);
    update();
    LinkBack(&manager->m_objects, &objects[1]->m_node);
    LinkBack(&manager->m_objects, &objects[3]->m_node);
    // A node without an object ends the walk.
    LinkBack(&manager->m_objects, &objects[6]->m_node);
    LinkBack(&manager->m_objects, &objects[7]->m_node);
    objects[6]->m_node.data = nullptr;
    update();
    pause(true);
    objects[6]->m_node.data = objects[6];
    // Pause and unpause; Pause with walks in progress; an empty list.
    pause(false);
    pause(true);
    manager->m_walking = 0x3;
    pause(false);
    manager->m_walking = 0;
    CCompositeObjectManager* empty = MakeManager(arena, 0x600);
    Call<void>(0x0045F5D0, &CCompositeObjectManager::Pause, empty, true);
    Call<void>(0x0045F4A0, &CCompositeObjectManager::Update, empty);
    Result(empty->m_walking);
    return EndRun(arena);
}

// --- the COIM heap ---
std::vector<uint32_t> RunCoimTest(bool original)
{
    uint8_t* arena = BeginRun(original);
    // Init: the parameters kept, the integers read, a heap of 0x12345 bytes.
    CallC<void>(0x0045F160, &Obj::InitCoim, 0x1111, 0x12345u, 0x2222, 0x3333);
    auto* region = GameVariable<Mem::Region*>(0x006F0DA0);
    Result(Rel(region));
    if (region)
        region->m_vtable = g_coim_regionVtable; // (so that deleting it is recorded)
    // An instance list (head + 3 nodes, one without an object): destroyed with the heap by a resize to 0x800.
    struct Node {
        void* object;
        Node* next;
        Node* prev;
    };
    auto* nodes = reinterpret_cast<Node*>(arena + 0x2000);
    void** instances[3];
    for (int i = 0; i < 3; ++i) {
        instances[i] = reinterpret_cast<void**>(arena + 0x2100 + 0x10 * i);
        instances[i][0] = g_coim_instanceVtable;
    }
    auto build = [&](int count) {
        for (int i = 0; i <= count; ++i) {
            nodes[i].object = i == 0 || i == 2 ? nullptr : instances[i - 1];
            nodes[i].next = &nodes[(i + 1) % (count + 1)];
            nodes[i].prev = &nodes[(i + count) % (count + 1)];
        }
        GameVariable<Node*>(0x006F0DB0) = nodes;
        GameVariable<uint32_t>(0x006F0DB4) = count;
    };
    build(3);
    CallC<void>(0x0045F1C0, &Obj::ResizeCoimHeap, 0x800u);
    region = GameVariable<Mem::Region*>(0x006F0DA0);
    if (region)
        region->m_vtable = g_coim_regionVtable;
    // Resize to 0: destroyed only (an empty list: a count of 0 skips it).
    GameVariable<uint32_t>(0x006F0DB4) = 0;
    CallC<void>(0x0045F1C0, &Obj::ResizeCoimHeap, 0u);
    Result(GameVariable<uint32_t>(0x006F0DA0));
    Result(GameVariable<uint32_t>(0x006F0DA4));
    // Destroy with nothing: no heap; a count but no head.
    CallC<void>(0x0045EFD0, &Obj::DestroyCoimHeap);
    GameVariable<Node*>(0x006F0DB0) = nullptr;
    GameVariable<uint32_t>(0x006F0DB4) = 2;
    CallC<void>(0x0045EFD0, &Obj::DestroyCoimHeap);
    // Create directly: 0 (destroy only), then a size; a heap but no region (the region's memory ran out).
    build(1);
    Result(CallC<uint32_t>(0x0045F0B0, &Obj::CreateCoimHeap, 0u) & 0xFF);
    Result(CallC<uint32_t>(0x0045F0B0, &Obj::CreateCoimHeap, 0x4000u) & 0xFF);
    GameVariable<uint32_t>(0x006F0DA0) = 0;
    CallC<void>(0x0045EFD0, &Obj::DestroyCoimHeap);
    return EndRun(arena);
}

void RunPair(std::vector<uint32_t> (*run)(bool), const char* what)
{
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

void TestCompositeObjectManager()
{
    SaveGlobals();
    auto* s = new StandIns;
    InstallStandIns(*s);
    RunPair(RunManagerTest, "CCompositeObjectManager: construction, component types and lists, objects, release");
    RunPair(RunWalkTest, "CCompositeObjectManager: Update and Pause walks (restarts, nesting)");
    RunPair(RunCoimTest, "COIM heap: init, resize, destroy (the instance list)");
    Cleanup();
}

} // namespace

SELFTEST(Early, TestCompositeObjectManager, Cleanup);
