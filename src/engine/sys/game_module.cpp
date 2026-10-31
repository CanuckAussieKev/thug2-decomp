// Modules and the module manager (Mdl).

#include "engine/sys/game_module.h"
#include "engine/net/task_vtable.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/lists/list_walk.h"
#include "engine/core/code_address.h"
#include "engine/script/world_cfuncs.h"

namespace GameModules {

namespace {

extern void** const kModuleVtable;
extern void** const kNodeVtable;
extern void** const kHeadVtable;
extern void** const kSearchVtable;

Hub*& Sole() { return ScriptCommands::g_moduleManager; }

unsigned int g_moduleReferences;

unsigned int& RefTally() { return g_moduleReferences; }

void FreeMemory(void* memory) { MemSys::Hub::Pull()->Dealloc(memory); }

// A list node's or task's scalar deleting destructor (vtable slot 0 / slot 2), deleting it.
void UnlinkListNode(MemSys::ListNode* node) { reinterpret_cast<void(__thiscall*)(void*, unsigned int)>(node->vtable[0])(node, 1); }
void DeleteTask(void* task)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(task))[2])(task, 1);
}

} // namespace

// 0x0044F7E0
PartUnit* PartUnit::MakeNew()
{
    m_vtable = kModuleVtable;
    m_curPhase = kStopped;
    m_locked = false;
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
    if (node) {
        node->data = this;
        node->vtable = kNodeVtable;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    m_listLink = node;
    return this;
}

// 0x0044F790
void PartUnit::DestroyBody()
{
    m_vtable = kModuleVtable;
    if (m_listLink)
        UnlinkListNode(m_listLink);
}

// 0x0044F7B0
PartUnit* PartUnit::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        FreeMemory(this);
    return this;
}

// 0x0044F5E0
Hub* Hub::MakeNew()
{
    m_modules.data = reinterpret_cast<void*>(-1);
    m_modules.priority = 0;
    m_modules.next = &m_modules;
    m_modules.prev = &m_modules;
    m_modules.vtable = kHeadVtable;
    auto* task = static_cast<uint32_t*>(MemSys::AllocZeroed(0x20));
    if (task) {
        reinterpret_cast<Scheduling::SchedulerTask*>(task)->MakeNew(-1000);             // Scheduling::SchedulerTask
        task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);                                                // Scheduling::Task<Manager>
        task[6] = reinterpret_cast<uint32_t>(&TickModules);
        task[7] = reinterpret_cast<uint32_t>(this);
    }
    m_task = task;
    m_commandPending = false;
    return this;
}

// 0x0044F680
void Hub::DestroyBody()
{
    HaltEveryModule();
    if (m_task)
        DeleteTask(m_task);
    m_modules.UnlinkAs(kNodeVtable); // (the head's destructor is the node's)
}

// 0x0044F3C0
void Hub::EnrolUnit(PartUnit* module)
{
    MemSys::ListNode* node = module->m_listLink;
    node->prev = m_modules.prev;
    node->next = &m_modules;
    m_modules.prev->next = node;
    m_modules.prev = node;
}

// 0x0044F3E0
void Hub::RemoveModule(PartUnit* module)
{
    MemSys::ListNode* node = module->m_listLink;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// 0x0044F310
void Hub::LaunchModule(PartUnit* module)
{
    if (module->m_locked)
        return;
    module->m_queuedOp = PartUnit::kStart;
    m_commandPending = true;
}

// 0x0044F530
void Hub::HaltEveryModule()
{
    ListKit::Hunt<PartUnit> search;
    PartUnit* next = search.HeadElement(&m_modules);
    while (PartUnit* module = next) {
        next = search.FollowingItem();
        if (!module->m_locked && module->m_curPhase == PartUnit::kRunning) {
            module->StopCallback();
            module->m_queuedOp = PartUnit::kNone;
            module->m_curPhase = PartUnit::kStopped;
        }
    }
}

// 0x0044F440
void __cdecl Hub::TickModules(void* task)
{
    auto* manager = static_cast<Hub*>(static_cast<void**>(task)[7]);
    if (!manager->m_commandPending)
        return;
    manager->m_commandPending = false;
    ListKit::Hunt<PartUnit> search;
    PartUnit* next = search.HeadElement(&manager->m_modules);
    while (PartUnit* module = next) {
        next = search.FollowingItem();
        switch (module->m_curPhase) {
        case PartUnit::kStopped:
            if (module->m_queuedOp == PartUnit::kStart || module->m_queuedOp == PartUnit::kRestart) {
                module->StartCallback();
                module->m_curPhase = PartUnit::kRunning;
                module->m_queuedOp = PartUnit::kNone;
            }
            break;
        case PartUnit::kRunning:
            if (module->m_queuedOp == PartUnit::kStop) {
                module->StopCallback();
                module->m_curPhase = PartUnit::kStopped;
                module->m_queuedOp = PartUnit::kNone;
            } else if (module->m_queuedOp == PartUnit::kRestart) {
                module->StopCallback();
                module->StartCallback();
                module->m_queuedOp = PartUnit::kNone;
            }
            break;
        }
    }
}

// 0x0044F6F0
Hub* CreateManager(bool create)
{
    if (!Sole() && create) {
        if (void* memory = MemSys::AllocZeroed(sizeof(Hub), nullptr, 1)) {
            Sole() = static_cast<Hub*>(memory)->MakeNew();
            ++RefTally();
            return Sole();
        }
        Sole() = nullptr;
    }
    ++RefTally();
    return Sole();
}

// 0x0044F740
void ReleaseManager()
{
    if (RefTally() > 1) {
        --RefTally();
        return;
    }
    if (Hub* manager = Sole()) {
        manager->DestroyBody();
        FreeMemory(manager);
    }
    Sole() = nullptr;
    RefTally() = 0;
}

// 0x0044F360
void ModuleListDestructors::NodeDestroyBody() { reinterpret_cast<MemSys::ListNode*>(this)->UnlinkAs(kNodeVtable); }

// 0x0044F380
MemSys::ListNode* ModuleListDestructors::NodeDestroy(unsigned int flags)
{
    auto* node = reinterpret_cast<MemSys::ListNode*>(this);
    node->UnlinkAs(kNodeVtable);
    if (flags & 1)
        FreeMemory(node);
    return node;
}

// 0x0044F410
MemSys::ListNode* ModuleListDestructors::HeadDestroy(unsigned int flags)
{
    NodeDestroyBody();
    if (flags & 1)
        FreeMemory(this);
    return reinterpret_cast<MemSys::ListNode*>(this);
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
static VtableSlot kModuleVtable_slots[] = {&GameModules::PartUnit::Teardown, &_purecall,
                &_purecall};
void** const kModuleVtable = reinterpret_cast<void**>(kModuleVtable_slots);
static VtableSlot kNodeVtable_slots[] = {&GameModules::ModuleListDestructors::NodeDestroy};
void** const kNodeVtable = reinterpret_cast<void**>(kNodeVtable_slots);
static VtableSlot kHeadVtable_slots[] = {&GameModules::ModuleListDestructors::HeadDestroy};
void** const kHeadVtable = reinterpret_cast<void**>(kHeadVtable_slots);
static VtableSlot kSearchVtable_slots[] = {&GameModules::ModuleListDestructors::SearchDestroy};
void** const kSearchVtable = reinterpret_cast<void**>(kSearchVtable_slots);
} // namespace

} // namespace Mdl
