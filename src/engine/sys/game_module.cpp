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

namespace modules {

namespace {

extern void** const kModuleVtable;
extern void** const kNodeVtable;
extern void** const kHeadVtable;
extern void** const kSearchVtable;

Hub*& sole() { return commands::g_module_manager; }

unsigned int g_module_references;

unsigned int& ref_tally() { return g_module_references; }

void free_memory(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

// A list node's or task's scalar deleting destructor (vtable slot 0 / slot 2), deleting it.
void unlink_list_node(memory::ListNode* node)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>(node->vtable[0])(node, 1);
}
void delete_task(void* task)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(task))[2])(task, 1);
}

} // namespace

// 0x0044F7E0
PartUnit* PartUnit::make_new()
{
    vtable_ = kModuleVtable;
    cur_phase_ = kStopped;
    locked_ = false;
    auto* node = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
    if (node) {
        node->data = this;
        node->vtable = kNodeVtable;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    list_link_ = node;
    return this;
}

// 0x0044F790
void PartUnit::destroy_body()
{
    vtable_ = kModuleVtable;
    if (list_link_)
        unlink_list_node(list_link_);
}

// 0x0044F7B0
PartUnit* PartUnit::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        free_memory(this);
    return this;
}

// 0x0044F5E0
Hub* Hub::make_new()
{
    modules_.data = reinterpret_cast<void*>(-1);
    modules_.priority = 0;
    modules_.next = &modules_;
    modules_.prev = &modules_;
    modules_.vtable = kHeadVtable;
    auto* task = static_cast<uint32_t*>(memory::alloc_zeroed(0x20));
    if (task) {
        reinterpret_cast<tasks::SchedulerTask*>(task)->make_new(-1000); // tasks::SchedulerTask
        task[0] = reinterpret_cast<uint32_t>(game_code::kTaskVtable);   // tasks::Task<Manager>
        task[6] = reinterpret_cast<uint32_t>(&tick_modules);
        task[7] = reinterpret_cast<uint32_t>(this);
    }
    task_ = task;
    command_pending_ = false;
    return this;
}

// 0x0044F680
void Hub::destroy_body()
{
    halt_every_module();
    if (task_)
        delete_task(task_);
    modules_.unlink_as(kNodeVtable); // (the head's destructor is the node's)
}

// 0x0044F3C0
void Hub::enrol_unit(PartUnit* module)
{
    memory::ListNode* node = module->list_link_;
    node->prev = modules_.prev;
    node->next = &modules_;
    modules_.prev->next = node;
    modules_.prev = node;
}

// 0x0044F3E0
void Hub::remove_module(PartUnit* module)
{
    memory::ListNode* node = module->list_link_;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// 0x0044F310
void Hub::launch_module(PartUnit* module)
{
    if (module->locked_)
        return;
    module->queued_op_ = PartUnit::kStart;
    command_pending_ = true;
}

// 0x0044F530
void Hub::halt_every_module()
{
    lists::Hunt<PartUnit> search;
    PartUnit* next = search.head_element(&modules_);
    while (PartUnit* module = next) {
        next = search.following_item();
        if (!module->locked_ && module->cur_phase_ == PartUnit::kRunning) {
            module->stop_callback();
            module->queued_op_ = PartUnit::kNone;
            module->cur_phase_ = PartUnit::kStopped;
        }
    }
}

// 0x0044F440
void __cdecl Hub::tick_modules(void* task)
{
    auto* manager = static_cast<Hub*>(static_cast<void**>(task)[7]);
    if (!manager->command_pending_)
        return;
    manager->command_pending_ = false;
    lists::Hunt<PartUnit> search;
    PartUnit* next = search.head_element(&manager->modules_);
    while (PartUnit* module = next) {
        next = search.following_item();
        switch (module->cur_phase_) {
        case PartUnit::kStopped:
            if (module->queued_op_ == PartUnit::kStart || module->queued_op_ == PartUnit::kRestart) {
                module->start_callback();
                module->cur_phase_ = PartUnit::kRunning;
                module->queued_op_ = PartUnit::kNone;
            }
            break;
        case PartUnit::kRunning:
            if (module->queued_op_ == PartUnit::kStop) {
                module->stop_callback();
                module->cur_phase_ = PartUnit::kStopped;
                module->queued_op_ = PartUnit::kNone;
            } else if (module->queued_op_ == PartUnit::kRestart) {
                module->stop_callback();
                module->start_callback();
                module->queued_op_ = PartUnit::kNone;
            }
            break;
        }
    }
}

// 0x0044F6F0
Hub* create_manager(bool create)
{
    if (!sole() && create) {
        if (void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1)) {
            sole() = static_cast<Hub*>(memory)->make_new();
            ++ref_tally();
            return sole();
        }
        sole() = nullptr;
    }
    ++ref_tally();
    return sole();
}

// 0x0044F740
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    if (Hub* manager = sole()) {
        manager->destroy_body();
        free_memory(manager);
    }
    sole() = nullptr;
    ref_tally() = 0;
}

// 0x0044F360
void ModuleListDestructors::node_destroy_body() { reinterpret_cast<memory::ListNode*>(this)->unlink_as(kNodeVtable); }

// 0x0044F380
memory::ListNode* ModuleListDestructors::node_destroy(unsigned int flags)
{
    auto* node = reinterpret_cast<memory::ListNode*>(this);
    node->unlink_as(kNodeVtable);
    if (flags & 1)
        free_memory(node);
    return node;
}

// 0x0044F410
memory::ListNode* ModuleListDestructors::head_destroy(unsigned int flags)
{
    node_destroy_body();
    if (flags & 1)
        free_memory(this);
    return reinterpret_cast<memory::ListNode*>(this);
}

// 0x0044F300
void ModuleListDestructors::search_destroy_body() { *reinterpret_cast<void***>(this) = kSearchVtable; }

// 0x0044F330
ModuleListDestructors* ModuleListDestructors::search_destroy(unsigned int flags)
{
    *reinterpret_cast<void***>(this) = kSearchVtable;
    if (flags & 1)
        free_memory(this);
    return this;
}

namespace {
static VtableSlot g_module_vtable_slots[] = {&modules::PartUnit::teardown, &_purecall, &_purecall};
void** const kModuleVtable = reinterpret_cast<void**>(g_module_vtable_slots);
static VtableSlot g_node_vtable_slots[] = {&modules::ModuleListDestructors::node_destroy};
void** const kNodeVtable = reinterpret_cast<void**>(g_node_vtable_slots);
static VtableSlot g_head_vtable_slots[] = {&modules::ModuleListDestructors::head_destroy};
void** const kHeadVtable = reinterpret_cast<void**>(g_head_vtable_slots);
static VtableSlot g_search_vtable_slots[] = {&modules::ModuleListDestructors::search_destroy};
void** const kSearchVtable = reinterpret_cast<void**>(g_search_vtable_slots);
} // namespace

} // namespace modules
