#pragma once

#include "game/core/arena_alloc.h"

namespace ListKit {

// An intrusive list's head (Neversoft's ListKit::Foremost<T>): a node whose data is -1, the list circular through it.
struct Foremost {
    MemSys::ListNode m_listLink;

    // 0x0045A540 (ListKit::Foremost<CEntity>): `node` goes before the first node whose priority isn't higher (the
    // highest first; equal ones newest first), else at the end. (The walk checks each node's data for -1
    // before checking the node for null - a null link would crash first.)
    void AppendVertex(MemSys::ListNode* node);
    // 0x0045AEB0 (ListKit::Foremost<NetLink::OrderedMsgNode>, also used by the game's net code): `node` goes before the first node
    // whose priority is higher (ascending order), else at the end - unless a node has the same priority: then
    // nothing is added and the result is false. (The same head-before-null check order as AppendVertex.)
    bool InsertByUniqueOrder(MemSys::ListNode* node);

    // 0x00457350 (ListKit::Foremost<T> of the net library's message lists): the same order searched from the end:
    // `node` goes after the last node whose priority isn't lower (equal ones oldest first), else at the
    // front. (The same data-before-null check order as AppendVertex.)
    void AppendNode(MemSys::ListNode* node);
};

} // namespace Lst
