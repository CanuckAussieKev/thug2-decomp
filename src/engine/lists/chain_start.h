#pragma once

#include "engine/core/arena_alloc.h"

namespace lists {

// An intrusive list's head (Neversoft's lists::Foremost<T>): a node whose data is -1, the list circular through it.
struct Foremost {
    memory::ListNode list_link_;

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

} // namespace Lst
