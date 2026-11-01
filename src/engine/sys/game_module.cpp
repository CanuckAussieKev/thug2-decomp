// Modules and the module manager (Mdl).

#include "engine/sys/game_module.h"

#include <memory>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/lists/list_walk.h"
#include "engine/script/world_cfuncs.h"

namespace modules {

namespace {

Hub*& sole() { return commands::g_module_manager; }

unsigned int g_module_references;

unsigned int& ref_tally() { return g_module_references; }

void free_memory(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

} // namespace

// 0x0044F7E0
PartUnit::PartUnit()
{
    cur_phase_ = kStopped;
    locked_ = false;
    list_link_ = memory::ListNode::create(this);
}

// 0x0044F790
PartUnit::~PartUnit()
{
    memory::ListNode::destroy(list_link_); // (0x0044F380)
}

// 0x0044F5E0
Hub* Hub::make_new()
{
    modules_.make_head();
    task_ = std::make_unique<tasks::Task<Hub>>(&tick_modules, this, -1000);
    command_pending_ = false;
    return this;
}

// 0x0044F680
void Hub::destroy_body()
{
    halt_every_module();
    task_.reset();
    modules_.uncouple(); // (0x0044F360: the head's destructor is the node's)
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
void __cdecl Hub::tick_modules(tasks::Task<Hub>* task)
{
    Hub* manager = task->content_;
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

} // namespace modules
