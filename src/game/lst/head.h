#pragma once

#include "game/core/alloc.h"

namespace Lst {

// An intrusive list's head (Neversoft's Lst::Head<T>): a node whose data is -1, the list circular through it.
struct Head {
    Mem::ListNode m_node;

    // 0x0045A540 (Lst::Head<CObject>): `node` goes before the first node whose priority isn't higher (the
    // highest first; equal ones newest first), else at the end. (The walk checks each node's data for -1
    // before checking the node for null - a null link would crash first.)
    void AddNode(Mem::ListNode* node);
    // 0x0045AEB0 (Lst::Head<Net::MsgSeqLink>, also used by the game's net code): `node` goes before the first node
    // whose priority is higher (ascending order), else at the end - unless a node has the same priority: then
    // nothing is added and the result is false. (The same head-before-null check order as AddNode.)
    bool AddUniqueSequence(Mem::ListNode* node);

    // 0x00457350 (Lst::Head<T> of the net library's message lists): the same order searched from the end:
    // `node` goes after the last node whose priority isn't lower (equal ones oldest first), else at the
    // front. (The same data-before-null check order as AddNode.)
    void AddNodeFromTail(Mem::ListNode* node);
};

} // namespace Lst
