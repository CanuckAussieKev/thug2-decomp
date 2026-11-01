#pragma once

// The net library's queued messages and their links (the types: PendingMsg / SeqQueuedMsg in net_app_view.h,
// the links in net_access_c.h; their functions in net_messages.cpp). The links are their own list nodes (data = the
// link).

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"

namespace net {

struct PendingMsg;
struct SeqQueuedMsg;
struct StreamDesc;

// The queues' links (lists::Elem<T>: the node, then the link's fields).
struct MsgNormalLink {
    // 0x004555F0 (send time 0)
    explicit MsgNormalLink(PendingMsg* msg);
    // 0x004557C0: the scalar deleting destructor (out of its list: 0x00455B50).
    MsgNormalLink* teardown(unsigned int flags);

    memory::ListNode list_link_{};
    PendingMsg* shared_msg_;
    uint32_t due_time_{0}; // not before this time
};
struct ReliableMsgNode {
    // 0x00455620 (no resends, send time 0 - the timestamps aren't set)
    explicit ReliableMsgNode(PendingMsg* msg);
    // 0x004557F0: the scalar deleting destructor (0x00455B70).
    ReliableMsgNode* teardown(unsigned int flags);

    memory::ListNode list_link_{};
    uint32_t stamp_ms_{}; // when it was last sent (0: never)
    PendingMsg* shared_msg_;
    uint32_t packet_clock_{}; // the connection's timestamp it went out with (the ack names it)
    uint8_t retry_count_{0};
    std::array<uint8_t, 3> pad21{};
    uint32_t due_time_{0};
};
struct OrderedMsgNode {
    // 0x00455650 (no resends, +0x16 and send time 0 - the group, the timestamps and the
    // sequence id aren't set)
    explicit OrderedMsgNode(SeqQueuedMsg* msg);
    // 0x00455820: the scalar deleting destructor (0x00455B90).
    OrderedMsgNode* teardown(unsigned int flags);

    memory::ListNode list_link_{}; // priority: the sequence number
    uint8_t retry_count_{0};
    uint8_t bundle_id_{};
    uint8_t bulk_msg_{0}; // a piece of a streamed message
    uint32_t packet_clock_{};
    uint32_t stamp_ms_{}; // when it was last sent (0: never)
    uint32_t order_index_{};
    uint32_t due_time_{0};
    SeqQueuedMsg* shared_msg_;
};

// A stream being received in pieces: StreamDesc (net_app.h); its link (0x18).
struct BulkStreamNode {
    // 0x00455680
    explicit BulkStreamNode(StreamDesc* desc);
    // 0x00455850: the scalar deleting destructor (0x00455BB0).
    BulkStreamNode* teardown(unsigned int flags);

    memory::ListNode list_link_{};
    StreamDesc* msg_meta_;
};

} // namespace net
