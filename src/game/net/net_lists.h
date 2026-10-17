#pragma once

// The net library's list types (Neversoft's Lst::Node<T> / Lst::Head<T> / Lst::Search<T> instances): their
// destructors, and BannedConn (an address an App refuses connections from). Each type has its own vtable
// holding only its scalar deleting destructor; the linker folded identical vtables, so one may stand for a
// node type and a head type at once.

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Net {

inline void** const kConnNodeVtable = reinterpret_cast<void**>(0x00647574);       // Lst::Node<Conn>
inline void** const kConnHeadVtable = reinterpret_cast<void**>(0x006475AC);       // Lst::Head<Conn>
inline void** const kBannedConnNodeVtable = reinterpret_cast<void**>(0x006475A4); // Lst::Node<BannedConn>
inline void** const kBannedConnVtable = reinterpret_cast<void**>(0x006475B0);     // BannedConn, Lst::Head<BannedConn>
inline void** const kMsgHandlerNodeVtable = reinterpret_cast<void**>(0x006475A8); // Lst::Node<MsgHandler>
inline void** const kConnSearchVtable = reinterpret_cast<void**>(0x0064759C);       // Lst::Search<Conn>
inline void** const kBannedConnSearchVtable = reinterpret_cast<void**>(0x006475A0); // Lst::Search<BannedConn>

// A node (or head) of a Dispatcher's handler lists (Lst::Node<MsgHandler>).
struct MsgHandlerNode : Mem::ListNode {
    // 0x00457010: a head's constructor (Lst::Head<MsgHandler>, vtable 0x006475E8): an empty list.
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

// A connection's node in its App's connection list, and that list's head (Lst::Node<Conn> / Lst::Head<Conn>).
struct ConnNode : Mem::ListNode {
    // 0x00457050: the destructor body.
    void DestroyBody();
    // 0x004573A0: the head's scalar deleting destructor.
    ConnNode* DestroyHead(unsigned int flags);
    // 0x00455BD0: the node's scalar deleting destructor (vtable 0x00647574).
    ConnNode* Destroy(unsigned int flags);
};

// Lst::Node<BannedConn> (the head of an App's banned list is one too).
struct BannedConnNode : Mem::ListNode {
    // 0x00457070: the destructor body.
    void DestroyBody();
    // 0x004570D0: the scalar deleting destructor.
    BannedConnNode* Destroy(unsigned int flags);
};

// An address an App doesn't accept connections from: a node whose data is itself (made by App::BanConnection).
struct BannedConn : BannedConnNode {
    uint32_t m_ip; // +0x14

    // 0x00457460: the scalar deleting destructor (BannedConn's and Lst::Head<BannedConn>'s).
    BannedConn* DestroyBanned(unsigned int flags);
};
static_assert(offsetof(BannedConn, m_ip) == 0x14 && sizeof(BannedConn) == 0x18, "layout must match the original");

// A list search as the game lays it out: the vtable (its virtual destructor), then the current node (see
// Lst::Search). The destructors only reset the vtable.
struct ConnSearch {
    void** m_vtable;
    Mem::ListNode* m_node;

    // 0x00456B80
    void DestroyBody();
    // 0x00456FB0 (scalar deleting)
    ConnSearch* Destroy(unsigned int flags);
};
struct BannedConnSearch {
    void** m_vtable;
    Mem::ListNode* m_node;

    // 0x00456B90
    void DestroyBody();
    // 0x00456FE0 (scalar deleting)
    BannedConnSearch* Destroy(unsigned int flags);
};

} // namespace Net
