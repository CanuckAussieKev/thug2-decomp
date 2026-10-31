// Tasks, task lists and task list stacks (Tsk).

#include <cstdint>

#include "engine/core/task_list.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/chain_start.h"
#include "engine/core/code_address.h"

namespace tasks {

namespace {

extern void** const kTaskNodeVtable;      // lists::Elem<SchedulerTask>
extern void** const kStackListNodeVtable; // lists::Elem<StackList>
extern void** const kStackListHeadVtable; // lists::Foremost<StackList>

void* const kHeadData = reinterpret_cast<void*>(-1);

// A node's links taken out of its list and pointed at itself (the vtable kept).
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
void setup_list_sentinel(memory::ListNode* head, void** vtable)
{
    head->data = kHeadData;
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}
// A node's scalar deleting destructor (vtable slot 0), with 1: unlinked and freed.
void unlink_list_node(memory::ListNode* node)
{
    reinterpret_cast<void(__thiscall*)(memory::ListNode*, int)>(node->vtable[0])(node, 1);
}

// The destructors behind the nodes', heads' and local searches' vtables.
struct TaskNode : memory::ListNode {
    // 0x00406A00
    void destroy_body() { unlink_as(kTaskNodeVtable); }
    // 0x00406860 (scalar deleting)
    TaskNode* teardown(uint8_t flags)
    {
        destroy_body();
        if (flags & 1)
            memory::Hub::get_pull()->dealloc(this);
        return this;
    }
};
struct StackListNode : memory::ListNode {
    // 0x00406D00
    void destroy_body() { unlink_as(kStackListNodeVtable); }
    // 0x00406D20 (scalar deleting; the body inlined)
    StackListNode* teardown(uint8_t flags)
    {
        destroy_body();
        if (flags & 1)
            memory::Hub::get_pull()->dealloc(this);
        return this;
    }
    // 0x00406E50: lists::Foremost<StackList>'s scalar deleting destructor (the node's body).
    StackListNode* destroy_head(uint8_t flags)
    {
        destroy_body();
        if (flags & 1)
            memory::Hub::get_pull()->dealloc(this);
        return this;
    }
};

} // namespace

// 0x00406890
SchedulerTask::SchedulerTask(int priority)
{
    disabled_ = 0;
    sequence_ = nullptr;
    pass_count_ = 0;
    skip_bits_ = 0;
    auto* node = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
    if (node) {
        node->data = this;
        node->vtable = kTaskNodeVtable;
        node->priority = priority;
        node->next = node;
        node->prev = node;
    }
    list_link_ = node;
}

// 0x00406930
SchedulerTask::~SchedulerTask()
{
    drop_out(); // (inlined)
    if (list_link_)
        unlink_list_node(list_link_);
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
LinkChain* LinkChain::make_new()
{
    lock_ = 0;
    setup_list_sentinel(&head_, kTaskNodeVtable);
    pass_count_ = 0;
    altered_ = false;
    return this;
}

// 0x00406A20
void LinkChain::destruct() { head_.unlink_as(kTaskNodeVtable); }

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
StackList* StackList::make_new()
{
    LinkChain::make_new();
    auto* node = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
    if (node) {
        node->data = this;
        node->vtable = kStackListNodeVtable;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    list_link_ = node;
    return this;
}

// 0x00406C30
void StackList::destruct()
{
    if (list_link_)
        unlink_list_node(list_link_);
    LinkChain::destruct();
}

// 0x00406E80
PileUp* PileUp::make_new()
{
    lock_ = 0;
    setup_list_sentinel(&lists_, kStackListHeadVtable);
    setup_list_sentinel(&popped_lists_, kStackListHeadVtable);
    pushed_ = false;
    just_popped_ = false;
    stack_on();
    ongoing_ = top_side_;
    pushed_ = false;
    just_popped_ = false;
    return this;
}

// 0x004070E0
void PileUp::destruct()
{
    // (the original counts the lists each time round)
    for (;;) {
        memory::ListNode* first = lists_.next;
        if (first->data == kHeadData || !first)
            break;
        stack_off();
    }
    delete_popped_lists();
    popped_lists_.unlink_as(kStackListNodeVtable);
    lists_.unlink_as(kStackListNodeVtable);
}

// 0x00406DE0
void PileUp::stack_on()
{
    void* memory = memory::alloc_zeroed(sizeof(StackList));
    auto* list = memory ? static_cast<StackList*>(memory)->make_new() : nullptr;
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
            old->destruct();
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
        list->destruct(); // (inlined; deleting the node takes it out of the popped lists)
        memory::Hub::get_pull()->dealloc(list);
    }
}

namespace {
static VtableSlot g_task_node_vtable_slots[] = {&tasks::TaskNode::teardown};
void** const kTaskNodeVtable = reinterpret_cast<void**>(g_task_node_vtable_slots);
static VtableSlot g_stack_list_node_vtable_slots[] = {&tasks::StackListNode::teardown};
void** const kStackListNodeVtable = reinterpret_cast<void**>(g_stack_list_node_vtable_slots);
static VtableSlot g_stack_list_head_vtable_slots[] = {&tasks::StackListNode::destroy_head};
void** const kStackListHeadVtable = reinterpret_cast<void**>(g_stack_list_head_vtable_slots);
} // namespace

} // namespace tasks
