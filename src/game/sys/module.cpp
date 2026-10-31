// Modules and the module manager (Mdl).

#include "game/sys/module.h"
#include "game/net/task_vtable.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/lst/search.h"
#include "game/core/code_address.h"
#include "game/script/world_cfuncs.h"

namespace Mdl {

namespace {

extern void** const kModuleVtable;
extern void** const kNodeVtable;
extern void** const kHeadVtable;
extern void** const kSearchVtable;

Manager*& Instance() { return CFuncs::g_moduleManager; }

unsigned int g_moduleReferences;

unsigned int& References() { return g_moduleReferences; }

void FreeMemory(void* memory) { Mem::Manager::Get()->Free(memory); }

// A list node's or task's scalar deleting destructor (vtable slot 0 / slot 2), deleting it.
void DeleteNode(Mem::ListNode* node) { reinterpret_cast<void(__thiscall*)(void*, unsigned int)>(node->vtable[0])(node, 1); }
void DeleteTask(void* task)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(task))[2])(task, 1);
}

} // namespace

// 0x0044F7E0
Module* Module::Construct()
{
    m_vtable = kModuleVtable;
    m_state = kStopped;
    m_locked = false;
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (node) {
        node->data = this;
        node->vtable = kNodeVtable;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    m_node = node;
    return this;
}

// 0x0044F790
void Module::DestroyBody()
{
    m_vtable = kModuleVtable;
    if (m_node)
        DeleteNode(m_node);
}

// 0x0044F7B0
Module* Module::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        FreeMemory(this);
    return this;
}

// 0x0044F5E0
Manager* Manager::Construct()
{
    m_modules.data = reinterpret_cast<void*>(-1);
    m_modules.priority = 0;
    m_modules.next = &m_modules;
    m_modules.prev = &m_modules;
    m_modules.vtable = kHeadVtable;
    auto* task = static_cast<uint32_t*>(Mem::Calloc(0x20));
    if (task) {
        reinterpret_cast<Tsk::BaseTask*>(task)->Construct(-1000);             // Tsk::BaseTask
        task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);                                                // Tsk::Task<Manager>
        task[6] = reinterpret_cast<uint32_t>(&ProcessModules);
        task[7] = reinterpret_cast<uint32_t>(this);
    }
    m_task = task;
    m_commandPending = false;
    return this;
}

// 0x0044F680
void Manager::DestroyBody()
{
    StopAllModules();
    if (m_task)
        DeleteTask(m_task);
    m_modules.UnlinkAs(kNodeVtable); // (the head's destructor is the node's)
}

// 0x0044F3C0
void Manager::RegisterModule(Module* module)
{
    Mem::ListNode* node = module->m_node;
    node->prev = m_modules.prev;
    node->next = &m_modules;
    m_modules.prev->next = node;
    m_modules.prev = node;
}

// 0x0044F3E0
void Manager::UnregisterModule(Module* module)
{
    Mem::ListNode* node = module->m_node;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// 0x0044F310
void Manager::StartModule(Module* module)
{
    if (module->m_locked)
        return;
    module->m_command = Module::kStart;
    m_commandPending = true;
}

// 0x0044F530
void Manager::StopAllModules()
{
    Lst::Search<Module> search;
    Module* next = search.FirstItem(&m_modules);
    while (Module* module = next) {
        next = search.NextItem();
        if (!module->m_locked && module->m_state == Module::kRunning) {
            module->StopCallback();
            module->m_command = Module::kNone;
            module->m_state = Module::kStopped;
        }
    }
}

// 0x0044F440
void __cdecl Manager::ProcessModules(void* task)
{
    auto* manager = static_cast<Manager*>(static_cast<void**>(task)[7]);
    if (!manager->m_commandPending)
        return;
    manager->m_commandPending = false;
    Lst::Search<Module> search;
    Module* next = search.FirstItem(&manager->m_modules);
    while (Module* module = next) {
        next = search.NextItem();
        switch (module->m_state) {
        case Module::kStopped:
            if (module->m_command == Module::kStart || module->m_command == Module::kRestart) {
                module->StartCallback();
                module->m_state = Module::kRunning;
                module->m_command = Module::kNone;
            }
            break;
        case Module::kRunning:
            if (module->m_command == Module::kStop) {
                module->StopCallback();
                module->m_state = Module::kStopped;
                module->m_command = Module::kNone;
            } else if (module->m_command == Module::kRestart) {
                module->StopCallback();
                module->StartCallback();
                module->m_command = Module::kNone;
            }
            break;
        }
    }
}

// 0x0044F6F0
Manager* CreateManager(bool create)
{
    if (!Instance() && create) {
        if (void* memory = Mem::Calloc(sizeof(Manager), nullptr, 1)) {
            Instance() = static_cast<Manager*>(memory)->Construct();
            ++References();
            return Instance();
        }
        Instance() = nullptr;
    }
    ++References();
    return Instance();
}

// 0x0044F740
void ReleaseManager()
{
    if (References() > 1) {
        --References();
        return;
    }
    if (Manager* manager = Instance()) {
        manager->DestroyBody();
        FreeMemory(manager);
    }
    Instance() = nullptr;
    References() = 0;
}

// 0x0044F360
void ModuleListDestructors::NodeDestroyBody() { reinterpret_cast<Mem::ListNode*>(this)->UnlinkAs(kNodeVtable); }

// 0x0044F380
Mem::ListNode* ModuleListDestructors::NodeDestroy(unsigned int flags)
{
    auto* node = reinterpret_cast<Mem::ListNode*>(this);
    node->UnlinkAs(kNodeVtable);
    if (flags & 1)
        FreeMemory(node);
    return node;
}

// 0x0044F410
Mem::ListNode* ModuleListDestructors::HeadDestroy(unsigned int flags)
{
    NodeDestroyBody();
    if (flags & 1)
        FreeMemory(this);
    return reinterpret_cast<Mem::ListNode*>(this);
}

// 0x0044F300
void ModuleListDestructors::SearchDestroyBody() { *reinterpret_cast<void***>(this) = kSearchVtable; }

// 0x0044F330
ModuleListDestructors* ModuleListDestructors::SearchDestroy(unsigned int flags)
{
    *reinterpret_cast<void***>(this) = kSearchVtable;
    if (flags & 1)
        FreeMemory(this);
    return this;
}

namespace {
static VtableSlot kModuleVtable_slots[] = {&Mdl::Module::Destroy, &_purecall,
                &_purecall};
void** const kModuleVtable = reinterpret_cast<void**>(kModuleVtable_slots);
static VtableSlot kNodeVtable_slots[] = {&Mdl::ModuleListDestructors::NodeDestroy};
void** const kNodeVtable = reinterpret_cast<void**>(kNodeVtable_slots);
static VtableSlot kHeadVtable_slots[] = {&Mdl::ModuleListDestructors::HeadDestroy};
void** const kHeadVtable = reinterpret_cast<void**>(kHeadVtable_slots);
static VtableSlot kSearchVtable_slots[] = {&Mdl::ModuleListDestructors::SearchDestroy};
void** const kSearchVtable = reinterpret_cast<void**>(kSearchVtable_slots);
} // namespace

} // namespace Mdl
