// Modules and the module manager (Mdl).

#include "engine/sys/game_module.h"

#include <memory>
#include <new>

#include "engine/memory/arena_alloc.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/task_list.h"
#include "engine/lists/list_walk.h"
#include "base/core/no_destroy.h"

namespace modules {

namespace {

// The module manager, made by create_manager.
memory::HeapUnique<Hub>& sole()
{
    static core::NoDestroy<memory::HeapUnique<Hub>> manager;
    return manager.get();
}

unsigned int& ref_tally()
{
    static unsigned int module_references{};
    return module_references;
}

void free_memory(void* memory) { memory::Hub::dealloc(memory); }

} // namespace

// 0x0044F7E0
PartUnit::PartUnit() : list_link_(memory::ListNode::create(this)) {}

// 0x0044F790
PartUnit::~PartUnit()
{
    memory::ListNode::destroy(list_link_); // (0x0044F380)
}

// 0x0044F5E0
namespace {
constexpr int kTickPriority = 1000; // (the task runs first: a negative priority)
} // namespace
Hub::Hub()
{
    modules_.make_head();
    task_ = std::make_unique<tasks::Task<Hub>>(&tick_modules, this, -kTickPriority);
}

// 0x0044F680
Hub::~Hub()
{
    halt_every_module();
    task_.reset();
    modules_.uncouple(); // (0x0044F360: the head's destructor is the node's)
}

// 0x0044F3C0
void Hub::enrol_unit(PartUnit* module)
{
    memory::ListNode* node = module->list_link();
    node->set_prev(modules_.prev());
    node->set_next(&modules_);
    modules_.prev()->set_next(node);
    modules_.set_prev(node);
}

// 0x0044F3E0
void Hub::remove_module(PartUnit* module)
{
    memory::ListNode* node = module->list_link();
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
}

// 0x0044F310
void Hub::launch_module(PartUnit* module)
{
    if (module->locked()) {
        return;
    }
    module->set_queued_op(PartUnit::OrderCode::kStart);
    command_pending_ = true;
}

// 0x0044F530
void Hub::halt_every_module()
{
    lists::Hunt<PartUnit> search;
    PartUnit* next = search.head_element(&modules_);
    while (PartUnit* module = next) {
        next = search.following_item();
        if (!module->locked() && module->cur_phase() == PartUnit::State::kRunning) {
            module->stop_callback();
            module->set_queued_op(PartUnit::OrderCode::kNone);
            module->set_cur_phase(PartUnit::State::kStopped);
        }
    }
}

// 0x0044F440
void Hub::tick_modules(tasks::Task<Hub>* task)
{
    Hub* manager = task->content();
    if (!manager->command_pending_) {
        return;
    }
    manager->command_pending_ = false;
    lists::Hunt<PartUnit> search;
    PartUnit* next = search.head_element(&manager->modules_);
    while (PartUnit* module = next) {
        next = search.following_item();
        switch (module->cur_phase()) {
        case PartUnit::State::kStopped:
            if (module->queued_op() == PartUnit::OrderCode::kStart || module->queued_op() == PartUnit::OrderCode::kRestart) {
                module->start_callback();
                module->set_cur_phase(PartUnit::State::kRunning);
                module->set_queued_op(PartUnit::OrderCode::kNone);
            }
            break;
        case PartUnit::State::kRunning:
            if (module->queued_op() == PartUnit::OrderCode::kStop) {
                module->stop_callback();
                module->set_cur_phase(PartUnit::State::kStopped);
                module->set_queued_op(PartUnit::OrderCode::kNone);
            } else if (module->queued_op() == PartUnit::OrderCode::kRestart) {
                module->stop_callback();
                module->start_callback();
                module->set_queued_op(PartUnit::OrderCode::kNone);
            }
            break;
        }
    }
}

Hub* get_module_manager() { return sole().get(); }

// 0x0044F6F0
Hub* create_manager(bool create)
{
    if (!sole() && create) {
        if (void* memory = memory::alloc_zeroed(sizeof(Hub), nullptr, 1)) {
            sole() = memory::HeapUnique<Hub>(memory::construct_in<Hub>(memory));
            ++ref_tally();
            return sole().get();
        }
        sole() = nullptr;
    }
    ++ref_tally();
    return sole().get();
}

// 0x0044F740
void release_manager()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    memory::destroy_then_clear(sole()); // (its destructor, then its block given back)
    ref_tally() = 0;
}

} // namespace modules
