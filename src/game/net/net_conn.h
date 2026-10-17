#pragma once

// The list types of Net::Conn's queues (Neversoft's Lst::Node<T> / Lst::Head<T> / Lst::Search<T> for the message
// links and the stream links): the destructors and constructors in Conn's address range (0x00455970-0x00456910).
// Conn itself: net_app_view.h (ConnView).

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Net {

// Lst::Node<T>'s vtables (the destructor body resets them), the heads' (the constructor sets them; their
// scalar deleting destructors are in wt/netE's range) and the searches'.
inline void** const kMsgLinkNodeVtable = reinterpret_cast<void**>(0x006474CC);    // Lst::Node<MsgLink>
inline void** const kMsgImpLinkNodeVtable = reinterpret_cast<void**>(0x006474D0); // Lst::Node<MsgImpLink>
inline void** const kMsgSeqLinkNodeVtable = reinterpret_cast<void**>(0x006474D4); // Lst::Node<MsgSeqLink>
inline void** const kStreamLinkNodeVtable = reinterpret_cast<void**>(0x006474C8); // Lst::Node<StreamLink>
inline void** const kMsgLinkHeadVtable = reinterpret_cast<void**>(0x00647578);    // Lst::Head<MsgLink>
inline void** const kMsgImpLinkHeadVtable = reinterpret_cast<void**>(0x0064757C); // Lst::Head<MsgImpLink>
inline void** const kMsgSeqLinkHeadVtable = reinterpret_cast<void**>(0x00647580); // Lst::Head<MsgSeqLink>
inline void** const kStreamLinkHeadVtable = reinterpret_cast<void**>(0x00647584); // Lst::Head<StreamLink>
inline void** const kMsgHandlerHeadVtable = reinterpret_cast<void**>(0x006475E8); // Lst::Head<MsgHandler>

// A node of one of Conn's lists: Lst::Node<T> for each link type (the vtable its destructor leaves).
template <uint32_t NodeVtable>
struct ConnListNode : Mem::ListNode {
    // The destructor body: out of its list (and linked to itself).
    void DestroyBody() { UnlinkAs(reinterpret_cast<void**>(NodeVtable)); }
};
using MsgLinkNode = ConnListNode<0x006474CC>;    // 0x00455B50
using MsgImpLinkNode = ConnListNode<0x006474D0>; // 0x00455B70
using MsgSeqLinkNode = ConnListNode<0x006474D4>; // 0x00455B90 (also each received queue's, Conn's destructor)
using StreamLinkNode = ConnListNode<0x006474C8>; // 0x00455BB0

// Lst::Head<MsgSeqLink> (Conn's sequenced queue and received queues).
struct MsgSeqLinkHead : Mem::ListNode {
    // 0x00455C60: the constructor (the received queues', through the compiler's array helper): an empty list
    // (data -1, priority 0, linked to itself).
    MsgSeqLinkHead* Construct();
};
// The same for the other heads Conn's constructor makes (inline there).
void InitConnListHead(Mem::ListNode* head, void** vtable);

// A local Lst::Search<T> over one of Conn's lists: its vtable (only a virtual destructor, which resets it).
template <uint32_t SearchVtable>
struct ConnListSearch {
    void** m_vtable;
    Mem::ListNode* m_node;

    void DestroyBody();
    // The scalar deleting destructor.
    ConnListSearch* Destroy(unsigned int flags);
};
using MsgSeqLinkSearch = ConnListSearch<0x00647564>; // 0x004559D0 / 0x00455A80
using MsgLinkSearch = ConnListSearch<0x00647568>;    // 0x004559E0 / 0x00455AB0
using MsgImpLinkSearch = ConnListSearch<0x0064756C>; // 0x004559F0 / 0x00455AE0
using StreamLinkSearch = ConnListSearch<0x00647570>; // 0x00455A00 / 0x00455B10

} // namespace Net
