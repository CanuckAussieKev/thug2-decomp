#pragma once

#include "engine/memory/arena_alloc.h"

namespace lists {

// An intrusive list's head: a node whose data is -1, the list circular through it.
struct Foremost {
    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }

private:
    memory::ListNode list_link_; // (the original head's size: kept where heads are embedded)

public:

    // 0x0045A540 (lists::Foremost<Entity>): `node` goes before the first node whose priority isn't higher (the
    // highest first; equal ones newest first), else at the end. (The walk checks each node's data for -1
    // before checking the node for null - a null link would crash first.)
    void append_vertex(memory::ListNode* node);
    // 0x0045AEB0 (lists::Foremost<NetLink::OrderedMsgNode>, also used by the game's net code): `node` goes before the first node
    // whose priority is higher (ascending order), else at the end - unless a node has the same priority: then
    // nothing is added and the result is false. (The same head-before-null check order as append_vertex.)
    bool insert_by_unique_order(memory::ListNode* node);

    // 0x00457350 (lists::Foremost<T> of the net library's message lists): the same order searched from the end:
    // `node` goes after the last node whose priority isn't lower (equal ones oldest first), else at the
    // front. (The same data-before-null check order as append_vertex.)
    void append_node(memory::ListNode* node);
};

// The same for a list whose head is a plain node (a head embedded in another record).
void append_vertex(memory::ListNode& head, memory::ListNode* node);
bool insert_by_unique_order(memory::ListNode& head, memory::ListNode* node);
void append_node(memory::ListNode& head, memory::ListNode* node);
// 0x0055D990 (lists::Foremost<T>::fetch_entry): the node at `index`, or null past the end. (Each node's data is
// checked for -1 before the node for null, as the original.)
memory::ListNode* node_at(memory::ListNode& head, int index);
// 0x00588B70: every node after the head taken out of the list (each linked to itself; none freed).
void unlink_all(memory::ListNode& head);

// 0x0057A2F0 (lists::Foremost<T>'s, shared by the game's lists): every item of the list at `head` deleted in turn by
// `destroy(item)`, which takes it out of the list (each next node is fetched first). (The original deleted each node
// through its table; the caller says how its items go.)
template <typename Item, typename Destroy>
void delete_each(memory::ListNode* head, Destroy destroy)
{
    const memory::ListNode* node = head->next();
    if (node->is_head()) {
        return;
    }
    while (node) {
        const memory::ListNode* next = node->next();
        const memory::ListNode* following = next->is_head() ? nullptr : next;
        destroy(static_cast<Item*>(node->data()));
        node = following;
    }
}

} // namespace lists
