// Intrusive list heads (lists::Foremost<T>). See chain_start.h.

#include "engine/lists/chain_start.h"
#include "engine/memory/arena_alloc.h"

namespace lists {

namespace {

// `node` linked in before `next`.
void insert_before(memory::ListNode* next, memory::ListNode* node)
{
    node->set_prev(next->prev());
    node->set_next(next);
    next->prev()->set_next(node);
    next->set_prev(node);
}

// `node` linked in after `prev`.
void insert_after(memory::ListNode* prev, memory::ListNode* node)
{
    node->set_prev(prev);
    node->set_next(prev->next());
    prev->next()->set_prev(node);
    prev->set_next(node);
}

} // namespace

// 0x0045A540
void append_vertex(memory::ListNode& head, memory::ListNode* node)
{
    const int priority = node->priority();
    for (memory::ListNode* next = head.next();; next = next->next()) {
        if (next->data() == memory::ListNode::head_marker() || !next) {
            break;
        }
        if (next->priority() <= priority) {
            insert_before(next, node);
            return;
        }
    }
    insert_before(&head, node); // at the end
}

// 0x0045AEB0
bool insert_by_unique_order(memory::ListNode& head, memory::ListNode* node)
{
    const int priority = node->priority();
    for (memory::ListNode* next = head.next();; next = next->next()) {
        if (next->data() == memory::ListNode::head_marker() || !next) {
            break;
        }
        if (next->priority() == priority) {
            return false;
        }
        if (next->priority() > priority) {
            insert_before(next, node);
            return true;
        }
    }
    insert_before(&head, node); // at the end
    return true;
}

// 0x00457350
void append_node(memory::ListNode& head, memory::ListNode* node)
{
    const int priority = node->priority();
    for (memory::ListNode* prev = head.prev();; prev = prev->prev()) {
        if (prev->data() == memory::ListNode::head_marker() || !prev) {
            break;
        }
        if (prev->priority() >= priority) {
            insert_after(prev, node);
            return;
        }
    }
    insert_after(&head, node); // at the front
}

// 0x0055D990
memory::ListNode* node_at(memory::ListNode& head, int index)
{
    memory::ListNode* node = head.next();
    if (node->data() == memory::ListNode::head_marker() || !node) {
        return nullptr;
    }
    while ((index--) != 0) {
        node = node->next();
        if (node->data() == memory::ListNode::head_marker() || !node) {
            return nullptr;
        }
    }
    return node;
}

// 0x00588B70
void unlink_all(memory::ListNode& head)
{
    // (The original reads the first node's data before checking the node for null.)
    memory::ListNode* node = head.next();
    if (node->data() == memory::ListNode::head_marker() || !node) {
        return;
    }
    while (node) {
        memory::ListNode* following = node->next();
        node->prev()->set_next(following);
        following->set_prev(node->prev());
        node->set_next(node);
        node->set_prev(node);
        node = following->data() == memory::ListNode::head_marker() ? nullptr : following;
    }
}

void Foremost::append_vertex(memory::ListNode* node) { lists::append_vertex(list_link_, node); }
bool Foremost::insert_by_unique_order(memory::ListNode* node) { return lists::insert_by_unique_order(list_link_, node); }
void Foremost::append_node(memory::ListNode* node) { lists::append_node(list_link_, node); }

} // namespace lists
