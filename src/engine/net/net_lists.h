#pragma once

// The net library's list types (the list templates' instances): their
// destructors, and BlockedConnEntry (an address an App refuses connections from). Each type has its own vtable
// holding only its scalar deleting destructor; the linker folded identical vtables, so one may stand for a
// node type and a head type at once.

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace net {

extern void** const kConnNodeVtable;         // lists::Elem<Conn>
extern void** const kConnHeadVtable;         // lists::Foremost<Conn>
extern void** const kBannedConnNodeVtable;   // lists::Elem<BlockedConnEntry>
extern void** const kBannedConnVtable;       // BlockedConnEntry, lists::Foremost<BlockedConnEntry>
extern void** const kMsgHandlerNodeVtable;   // lists::Elem<NoteHandler>
extern void** const kMsgHandlerHeadVtable;   // lists::Foremost<NoteHandler>
extern void** const kConnSearchVtable;       // lists::Hunt<Conn>
extern void** const kBannedConnSearchVtable; // lists::Hunt<BlockedConnEntry>

// A node (or head) of a Dispatcher's handler lists (lists::Elem<NoteHandler>).
struct MsgHandlerNode : memory::ListNode {
    // 0x00457010: a head's constructor (lists::Foremost<NoteHandler>, vtable 0x006475E8): an empty list.
    MsgHandlerNode* construct_head();
    // 0x00457030: the destructor body: out of its list (and linked to itself).
    void destroy_body();
    // 0x00457090: the scalar deleting destructor.
    MsgHandlerNode* teardown(unsigned int flags);
};

// The handler lists a Dispatcher (wt/netC) starts with: one per message id 0..254.
struct HandlerLists {
    static constexpr int kCount = 255;
    MsgHandlerNode lists_[kCount];

    // 0x00457260: the Dispatcher's destructor: each list's, last to first (the compiler's array-destructor
    // helper 0x0062382B).
    void teardown();
};
static_assert(sizeof(HandlerLists) == 0x13EC, "layout must match the original");

// A connection's node in its App's connection list, and that list's head (lists::Elem<Conn> / lists::Foremost<Conn>).
struct ConnNode : memory::ListNode {
    // 0x00457050: the destructor body.
    void destroy_body();
    // 0x004573A0: the head's scalar deleting destructor.
    ConnNode* destroy_head(unsigned int flags);
    // 0x00455BD0: the node's scalar deleting destructor (vtable 0x00647574).
    ConnNode* teardown(unsigned int flags);
};

// lists::Elem<BlockedConnEntry> (the head of an App's banned list is one too).
struct BannedConnNode : memory::ListNode {
    // 0x00457070: the destructor body.
    void destroy_body();
    // 0x004570D0: the scalar deleting destructor.
    BannedConnNode* teardown(unsigned int flags);
};

// An address an App doesn't accept connections from: a node whose data is itself (made by AppHost::block_connection).
struct BlockedConnEntry : BannedConnNode {
    uint32_t remote_ip_; // +0x14

    // 0x00457460: the scalar deleting destructor (BlockedConnEntry's and lists::Foremost<BlockedConnEntry>'s).
    BlockedConnEntry* destroy_banned(unsigned int flags);
};
static_assert(offsetof(BlockedConnEntry, remote_ip_) == 0x14 && sizeof(BlockedConnEntry) == 0x18,
              "layout must match the original");

// A list search as the game lays it out: the vtable (its virtual destructor), then the current node (see
// lists::Hunt). The destructors only reset the vtable.
struct ConnSearch {
    void** vtable_;
    memory::ListNode* list_link_;

    // 0x00456B80
    void destroy_body();
    // 0x00456FB0 (scalar deleting)
    ConnSearch* teardown(unsigned int flags);
};
struct BannedConnSearch {
    void** vtable_;
    memory::ListNode* list_link_;

    // 0x00456B90
    void destroy_body();
    // 0x00456FE0 (scalar deleting)
    BannedConnSearch* teardown(unsigned int flags);
};

} // namespace net
