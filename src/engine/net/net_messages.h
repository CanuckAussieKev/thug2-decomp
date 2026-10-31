#pragma once

// The net library's queued messages and their links (the types: PendingMsg / SeqQueuedMsg in net_app_view.h,
// the links in net_access_c.h; their functions in net_messages.cpp), and the lists::Elem<T> types of the message
// lists and of the Manager's app lists: each has its own vtable holding only its scalar deleting destructor.

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace net {

struct PendingMsg;
struct SeqQueuedMsg;
struct StreamDesc;

extern void** const kStreamLinkNodeVtable; // lists::Elem<BulkStreamNode>
extern void** const kMsgNormalNodeVtable;  // lists::Elem<MsgNormalLink>
extern void** const kMsgImpNodeVtable;     // lists::Elem<ReliableMsgNode>
extern void** const kMsgSeqNodeVtable;     // lists::Elem<OrderedMsgNode>
extern void** const kAppListNodeVtable;    // lists::Elem<App>
extern void** const kAppListHeadVtable;    // lists::Foremost<App>
extern void** const kMsgNormalLinkVtable;
extern void** const kMsgImpLinkVtable;
extern void** const kMsgSeqLinkVtable;
extern void** const kStreamLinkVtable;

// The queues' links (lists::Elem<T>: the node, then the link's fields).
struct MsgNormalLink {
    // 0x004555F0 (vtable 0x00647578; send time 0)
    MsgNormalLink* make_new(PendingMsg* msg);
    // 0x004557C0: the scalar deleting destructor.
    MsgNormalLink* teardown(unsigned int flags);

    memory::ListNode list_link_;
    PendingMsg* shared_msg_;
    uint32_t due_time_; // not before this time
};
struct ReliableMsgNode {
    // 0x00455620 (vtable 0x0064757C; no resends, send time 0 - the timestamps aren't set)
    ReliableMsgNode* make_new(PendingMsg* msg);
    // 0x004557F0: the scalar deleting destructor.
    ReliableMsgNode* teardown(unsigned int flags);

    memory::ListNode list_link_;
    uint32_t stamp_ms_; // when it was last sent (0: never)
    PendingMsg* shared_msg_;
    uint32_t packet_clock_; // the connection's timestamp it went out with (the ack names it)
    uint8_t retry_count_;
    uint8_t pad21[3];
    uint32_t due_time_;
};
struct OrderedMsgNode {
    // 0x00455650 (vtable 0x00647580; no resends, +0x16 and send time 0 - the group, the timestamps and the
    // sequence id aren't set)
    OrderedMsgNode* make_new(SeqQueuedMsg* msg);
    // 0x00455820: the scalar deleting destructor.
    OrderedMsgNode* teardown(unsigned int flags);

    memory::ListNode list_link_; // priority: the sequence number
    uint8_t retry_count_;
    uint8_t bundle_id_;
    uint8_t bulk_msg_; // a piece of a streamed message
    uint8_t pad17;
    uint32_t packet_clock_;
    uint32_t stamp_ms_; // when it was last sent (0: never)
    uint32_t order_index_;
    uint32_t due_time_;
    SeqQueuedMsg* shared_msg_;
};
static_assert(offsetof(ReliableMsgNode, due_time_) == 0x24 && offsetof(OrderedMsgNode, shared_msg_) == 0x28 &&
              offsetof(MsgNormalLink, due_time_) == 0x18 && sizeof(OrderedMsgNode) == 0x2C);

// A stream being received in pieces: StreamDesc (net_app.h); its link (0x18).
struct BulkStreamNode {
    // 0x00455680 (vtable 0x00647584)
    BulkStreamNode* make_new(StreamDesc* desc);
    // 0x00455850: the scalar deleting destructor.
    BulkStreamNode* teardown(unsigned int flags);

    memory::ListNode list_link_;
    StreamDesc* msg_meta_;
};
static_assert(sizeof(BulkStreamNode) == 0x18);

// The connection's queue heads (and the links' base) - lists::Elem<T> of each link type. (The destructor bodies,
// 0x00455B50-0x00455BB0, are in Conn's range: net_conn.cpp.)
struct StreamLinkNode : memory::ListNode {
    // 0x00455BB0: the destructor body: out of its list (and linked to itself).
    void destroy_body();
    // 0x00455130: the scalar deleting destructor.
    StreamLinkNode* teardown(unsigned int flags);
};
struct MsgNormalNode : memory::ListNode {
    // 0x00455B50: the destructor body.
    void destroy_body();
    // 0x00455170
    MsgNormalNode* teardown(unsigned int flags);
};
struct MsgImpNode : memory::ListNode {
    // 0x00455B70: the destructor body.
    void destroy_body();
    // 0x004551B0
    MsgImpNode* teardown(unsigned int flags);
};
struct MsgSeqNode : memory::ListNode {
    // 0x00455B90: the destructor body (also each of Conn's received queues', from its destructor).
    void destroy_body();
    // 0x004551F0
    MsgSeqNode* teardown(unsigned int flags);
};

// An App's node in its Manager's server or client list (App +0x1414), and those lists' heads.
struct AppNode : memory::ListNode {
    // 0x00455110: the destructor body: out of its list.
    void destroy_body();
    // 0x00455230: the scalar deleting destructor.
    AppNode* teardown(unsigned int flags);
    // 0x004556B0: lists::Foremost<App>'s scalar deleting destructor.
    AppNode* destroy_head(unsigned int flags);
};

} // namespace net
