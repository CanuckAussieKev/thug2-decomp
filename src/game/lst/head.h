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
};

} // namespace Lst
