// Intrusive list heads (lists::Foremost<T>). See chain_start.h.

#include "engine/lists/chain_start.h"
#include "engine/core/arena_alloc.h"

namespace lists {

namespace {

// `node` linked in before `next`.
void insert_before(memory::ListNode* next, memory::ListNode* node)
{
    node->prev = next->prev;
    node->next = next;
    next->prev->next = node;
    next->prev = node;
}

// `node` linked in after `prev`.
void insert_after(memory::ListNode* prev, memory::ListNode* node)
{
    node->prev = prev;
    node->next = prev->next;
    prev->next->prev = node;
    prev->next = node;
}

} // namespace

// 0x0045A540
void Foremost::append_vertex(memory::ListNode* node)
{
    const int priority = node->priority;
    for (memory::ListNode* next = list_link_.next;; next = next->next) {
        if (next->data == memory::ListNode::head_marker() || !next)
            break;
        if (next->priority <= priority) {
            insert_before(next, node);
            return;
        }
    }
    insert_before(&list_link_, node); // at the end
}

// 0x0045AEB0
bool Foremost::insert_by_unique_order(memory::ListNode* node)
{
    const int priority = node->priority;
    for (memory::ListNode* next = list_link_.next;; next = next->next) {
        if (next->data == memory::ListNode::head_marker() || !next)
            break;
        if (next->priority == priority)
            return false;
        if (next->priority > priority) {
            insert_before(next, node);
            return true;
        }
    }
    insert_before(&list_link_, node); // at the end
    return true;
}

// 0x00457350
void Foremost::append_node(memory::ListNode* node)
{
    const int priority = node->priority;
    for (memory::ListNode* prev = list_link_.prev;; prev = prev->prev) {
        if (prev->data == memory::ListNode::head_marker() || !prev)
            break;
        if (prev->priority >= priority) {
            insert_after(prev, node);
            return;
        }
    }
    insert_after(&list_link_, node); // at the front
}

} // namespace lists
