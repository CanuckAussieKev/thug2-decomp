#pragma once

// The net library's list types (Neversoft's ListKit::Node<T> / ListKit::Head<T> / ListKit::Search<T> instances): their
// destructors, and BlockedConnEntry (an address an App refuses connections from). Each type has its own vtable
// holding only its scalar deleting destructor; the linker folded identical vtables, so one may stand for a
// node type and a head type at once.

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace NetLink {

extern void** const kConnNodeVtable;       // ListKit::Node<Conn>
extern void** const kConnHeadVtable;       // ListKit::Head<Conn>
extern void** const kBannedConnNodeVtable; // ListKit::Node<BlockedConnEntry>
extern void** const kBannedConnVtable;     // BlockedConnEntry, ListKit::Head<BlockedConnEntry>
extern void** const kMsgHandlerNodeVtable; // ListKit::Node<MsgHandler>
extern void** const kMsgHandlerHeadVtable; // ListKit::Head<MsgHandler>
extern void** const kConnSearchVtable;       // ListKit::Search<Conn>
extern void** const kBannedConnSearchVtable; // ListKit::Search<BlockedConnEntry>

// A node (or head) of a Dispatcher's handler lists (ListKit::Node<MsgHandler>).
struct MsgHandlerNode : MemSys::ListNode {
    // 0x00457010: a head's constructor (ListKit::Head<MsgHandler>, vtable 0x006475E8): an empty list.
    MsgHandlerNode* ConstructHead();
    // 0x00457030: the destructor body: out of its list (and linked to itself).
    void DestroyBody();
    // 0x00457090: the scalar deleting destructor.
    MsgHandlerNode* Destroy(unsigned int flags);
};

// The handler lists a Dispatcher (wt/netC) starts with: one per message id 0..254.
struct HandlerLists {
    static constexpr int kCount = 255;
    MsgHandlerNode m_lists[kCount];

    // 0x00457260: the Dispatcher's destructor: each list's, last to first (the compiler's array-destructor
    // helper 0x0062382B).
    void Destroy();
};
static_assert(sizeof(HandlerLists) == 0x13EC, "layout must match the original");

// A connection's node in its App's connection list, and that list's head (ListKit::Node<Conn> / ListKit::Head<Conn>).
struct ConnNode : MemSys::ListNode {
    // 0x00457050: the destructor body.
    void DestroyBody();
    // 0x004573A0: the head's scalar deleting destructor.
    ConnNode* DestroyHead(unsigned int flags);
    // 0x00455BD0: the node's scalar deleting destructor (vtable 0x00647574).
    ConnNode* Destroy(unsigned int flags);
};

// ListKit::Node<BlockedConnEntry> (the head of an App's banned list is one too).
struct BannedConnNode : MemSys::ListNode {
    // 0x00457070: the destructor body.
    void DestroyBody();
    // 0x004570D0: the scalar deleting destructor.
    BannedConnNode* Destroy(unsigned int flags);
};

// An address an App doesn't accept connections from: a node whose data is itself (made by App::BlockConnection).
struct BlockedConnEntry : BannedConnNode {
    uint32_t m_hostAddr; // +0x14

    // 0x00457460: the scalar deleting destructor (BlockedConnEntry's and ListKit::Head<BlockedConnEntry>'s).
    BlockedConnEntry* DestroyBanned(unsigned int flags);
};
static_assert(offsetof(BlockedConnEntry, m_hostAddr) == 0x14 && sizeof(BlockedConnEntry) == 0x18, "layout must match the original");

// A list search as the game lays it out: the vtable (its virtual destructor), then the current node (see
// ListKit::Search). The destructors only reset the vtable.
struct ConnSearch {
    void** m_vtable;
    MemSys::ListNode* m_listLink;

    // 0x00456B80
    void DestroyBody();
    // 0x00456FB0 (scalar deleting)
    ConnSearch* Destroy(unsigned int flags);
};
struct BannedConnSearch {
    void** m_vtable;
    MemSys::ListNode* m_listLink;

    // 0x00456B90
    void DestroyBody();
    // 0x00456FE0 (scalar deleting)
    BannedConnSearch* Destroy(unsigned int flags);
};

} // namespace Net
