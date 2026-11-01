// Tasks, task lists and task list stacks (Tsk).

#include <cstdint>
#include <new>

#include "engine/core/task_list.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/chain_start.h"

namespace tasks {

namespace {

void* const kHeadData = reinterpret_cast<void*>(-1);

// A node's links taken out of its list and pointed at itself.
void uncouple(memory::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}
// After `head`: the first node of its list.
void insert_front(memory::ListNode* head, memory::ListNode* node)
{
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}
// The node after `node` (null at the head) - lists::Hunt's step.
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next;
    return next->data == kHeadData ? nullptr : next;
}
} // namespace

// 0x00406890
SchedulerTask::SchedulerTask(int priority)
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    list_link_ = memory::ListNode::create(this, priority);
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00406930
SchedulerTask::~SchedulerTask()
{
    drop_out();                            // (inlined)
    memory::ListNode::destroy(list_link_); // (0x00406860: out of the list, freed)
}

// 0x004068F0
void SchedulerTask::drop_out()
{
    if (!sequence_)
        return;
    ++sequence_->lock_;
    sequence_->mark_changed();
    uncouple(list_link_);
    --sequence_->lock_;
    sequence_ = nullptr;
}

// 0x00406850
bool SchedulerTask::is_on_task_list() const { return list_link_->prev != list_link_; }

// 0x00406A50
LinkChain::LinkChain() { head_.make_head(); }

// 0x00406A20
LinkChain::~LinkChain() { head_.uncouple(); }

// 0x004069B0
void LinkChain::mark_changed() { altered_ = true; }

// 0x00406A80
void LinkChain::queue_job(SchedulerTask* task)
{
    ++lock_;
    reinterpret_cast<lists::Foremost*>(&head_)->append_vertex(task->list_link_);
    task->sequence_ = this;
    task->pass_count_ = pass_count_ - 1;
    altered_ = true;
    --lock_;
}

// 0x00406AC0
void LinkChain::work_on(void* /*unused*/, uint32_t mask)
{
    if (lock_ != 0)
        return;
    ++pass_count_;
    do {
        memory::ListNode* node = get_next_node(&head_);
        SchedulerTask* task = node ? static_cast<SchedulerTask*>(node->data) : nullptr;
        altered_ = false;
        while (task) {
            // (the next task is found before this one runs: it may take itself out)
            memory::ListNode* next_node = get_next_node(node);
            SchedulerTask* next = next_node ? static_cast<SchedulerTask*>(next_node->data) : nullptr;
            if (task->disabled_ == 0 && task->pass_count_ != pass_count_) {
                task->pass_count_ = pass_count_;
                if ((mask & task->skip_bits_) == 0)
                    task->run();
            }
            if (altered_)
                break;
            node = next_node;
            task = next;
        }
    } while (altered_);
}

// 0x00406BB0
void LinkChain::clear_job_list()
{
    ++lock_;
    for (;;) {
        memory::ListNode* node = get_next_node(&head_);
        if (!node || !node->data)
            break;
        static_cast<SchedulerTask*>(node->data)->drop_out();
        altered_ = true;
    }
    --lock_;
}

// 0x00406D60
// NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
StackList::StackList() { list_link_ = memory::ListNode::create(this); }
// NOLINTEND(cppcoreguidelines-prefer-member-initializer)

// 0x00406C30 (then the list's own destructor)
StackList::~StackList() { memory::ListNode::destroy(list_link_); } // (0x00406D20)

// 0x00406E80
PileUp::PileUp()
{
    lists_.make_head();
    popped_lists_.make_head();
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set twice, as the original does
    pushed_ = false;
    just_popped_ = false;
    stack_on();
    ongoing_ = top_side_;
    pushed_ = false;
    just_popped_ = false;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x004070E0
PileUp::~PileUp()
{
    // (the original counts the lists each time round)
    for (;;) {
        memory::ListNode* first = lists_.next;
        if (first->data == kHeadData || !first)
            break;
        stack_off();
    }
    delete_popped_lists();
    popped_lists_.uncouple();
    lists_.uncouple();
}

// 0x00406DE0
void PileUp::stack_on()
{
    void* memory = memory::alloc_zeroed(sizeof(StackList));
    auto* list = memory ? new (memory) StackList : nullptr;
    top_side_ = list;
    insert_front(&lists_, list->list_link_); // (a failed allocation crashes here, as in the original)
    pushed_ = true;
}

// 0x00406FB0
void PileUp::stack_off()
{
    StackList* old = top_side_;
    memory::ListNode* first = get_next_node(&lists_);
    if (ongoing_ == old) {
        // The current list becomes the one below (an empty stack would crash here, as in the original).
        memory::ListNode* below = get_next_node(first);
        ongoing_ = below ? static_cast<StackList*>(below->data) : nullptr;
    } else {
        pushed_ = false;
    }
    if (lock_ == 0) {
        if (old) {
            old->~StackList();
            memory::Hub::get_pull()->dealloc(old);
        }
    } else {
        uncouple(old->list_link_);
        insert_front(&popped_lists_, old->list_link_);
    }
    top_side_ = ongoing_;
    just_popped_ = true;
}

// 0x00406C90
void PileUp::queue_job(SchedulerTask* task) { ongoing_->queue_job(task); }

// 0x00406CB0
void PileUp::clear_job_list() { (pushed_ ? top_side_ : ongoing_)->clear_job_list(); }

// 0x004070A0
void PileUp::work_on(void* unused)
{
    ++lock_;
    ongoing_ = top_side_;
    top_side_->work_on(unused, skip_bits_);
    --lock_;
    delete_popped_lists();
    pushed_ = false;
    just_popped_ = false;
}

// 0x00406F10
void PileUp::delete_popped_lists()
{
    for (;;) {
        memory::ListNode* node = get_next_node(&popped_lists_);
        if (!node || !node->data)
            break;
        auto* list = static_cast<StackList*>(node->data);
        list->~StackList(); // (inlined; deleting the node takes it out of the popped lists)
        memory::Hub::get_pull()->dealloc(list);
    }
}

} // namespace tasks
