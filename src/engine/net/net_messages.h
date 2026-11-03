#pragma once

// The net library's queued messages and their links (the types: PendingMsg / SeqQueuedMsg in net_app_view.h,
// the links in net_access_c.h; their functions in net_messages.cpp). The links are their own list nodes (data = the
// link).

#include "wire/message_ids.h"
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

    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    [[nodiscard]] PendingMsg* shared_msg() const { return shared_msg_; }
    [[nodiscard]] uint32_t due_time() const { return due_time_; }
    void set_due_time(uint32_t value) { due_time_ = value; }

private:
    memory::ListNode list_link_{};
    PendingMsg* shared_msg_;
    uint32_t due_time_{0}; // not before this time
};
struct ReliableMsgNode {
    // 0x00455620 (no resends, send time 0 - the timestamps aren't set)
    explicit ReliableMsgNode(PendingMsg* msg);
    // 0x004557F0: the scalar deleting destructor (0x00455B70).
    ReliableMsgNode* teardown(unsigned int flags);

    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    [[nodiscard]] uint32_t stamp_ms() const { return stamp_ms_; }
    void set_stamp_ms(uint32_t value) { stamp_ms_ = value; }
    [[nodiscard]] PendingMsg* shared_msg() const { return shared_msg_; }
    [[nodiscard]] uint32_t packet_clock() const { return packet_clock_; }
    void set_packet_clock(uint32_t value) { packet_clock_ = value; }
    uint8_t& retry_count() { return retry_count_; }
    [[nodiscard]] uint8_t retry_count() const { return retry_count_; }
    [[nodiscard]] uint32_t due_time() const { return due_time_; }
    void set_due_time(uint32_t value) { due_time_ = value; }

private:
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

    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    uint8_t& retry_count() { return retry_count_; }
    [[nodiscard]] uint8_t retry_count() const { return retry_count_; }
    [[nodiscard]] wire::MessageGroup bundle_id() const { return bundle_id_; }
    void set_bundle_id(wire::MessageGroup value) { bundle_id_ = value; }
    [[nodiscard]] uint8_t bulk_msg() const { return bulk_msg_; }
    void set_bulk_msg(uint8_t value) { bulk_msg_ = value; }
    [[nodiscard]] uint32_t packet_clock() const { return packet_clock_; }
    void set_packet_clock(uint32_t value) { packet_clock_ = value; }
    [[nodiscard]] uint32_t stamp_ms() const { return stamp_ms_; }
    void set_stamp_ms(uint32_t value) { stamp_ms_ = value; }
    [[nodiscard]] uint32_t order_index() const { return order_index_; }
    void set_order_index(uint32_t value) { order_index_ = value; }
    [[nodiscard]] uint32_t due_time() const { return due_time_; }
    void set_due_time(uint32_t value) { due_time_ = value; }
    [[nodiscard]] SeqQueuedMsg* shared_msg() const { return shared_msg_; }

private:
    memory::ListNode list_link_{}; // priority: the sequence number
    uint8_t retry_count_{0};
    wire::MessageGroup bundle_id_{};
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

    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    [[nodiscard]] StreamDesc* msg_meta() const { return msg_meta_; }

private:
    memory::ListNode list_link_{};
    StreamDesc* msg_meta_;
};

} // namespace net
