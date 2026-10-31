#pragma once

// The messages a NetLink::Conn's queues share (PendingMsg, SeqQueuedMsg) and the queue links' common part. NetLink::AppHost's
// methods 0x00456910-0x00457EB0 are defined in net_app_view.cpp (App: net_app.h; Conn: net_conn.h).

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace net {

// A message's contents as queued for sending. Several connections' queues can share one: a reference count.
// Two kinds: those of the normal and important queues (constructor 0x00454FA0) and those of the sequenced
// queue (0x00455040, 0x10 bytes); the destructor 0x004550F0 (both) frees the data.
struct PendingMsg {
    // 0x00454FA0: a copy of `data` (`length` bytes; none if 0). With a compression method (1 RLE, 2 LZSS) the
    // copy is compressed if that makes it smaller: the id becomes 0x12 (compressed) and the length shrinks.
    PendingMsg* make_new(uint8_t id, uint16_t length, void* data, uint8_t compression);
    // 0x004550F0 (SeqQueuedMsg's too): the data freed.
    void destroy_body();

    uint8_t ident_;       // +0x00
    uint16_t byte_count_; // +0x02
    char* content_;       // +0x04
    int share_count_;     // +0x08
};
struct SeqQueuedMsg {
    // 0x00455040: as PendingMsg's, with the +0x08 byte.
    SeqQueuedMsg* make_new(uint8_t id, uint16_t length, void* data, uint8_t byte8, uint8_t compression);
    // (0x004550F0: PendingMsg::destroy_body - the data is at the same place)
    void destroy_body() { reinterpret_cast<PendingMsg*>(this)->destroy_body(); }

    uint8_t ident_;       // +0x00
    uint16_t byte_count_; // +0x02
    char* content_;       // +0x04
    uint8_t byte8_;       // +0x08 (the constructor's 4th argument)
    int share_count_;     // +0x0C
};
static_assert(offsetof(PendingMsg, share_count_) == 0x08 && offsetof(SeqQueuedMsg, content_) == 0x04 &&
                  offsetof(SeqQueuedMsg, share_count_) == 0x0C && sizeof(SeqQueuedMsg) == 0x10,
              "layout must match the original");

// Any of the links that queue a message on a connection (MsgNormalLink / ReliableMsgNode / OrderedMsgNode,
// net_messages.h): list nodes (data = the link itself), deleted through their vtable's scalar deleting destructor.
struct TimedMsgNode : memory::PaddedListNode {
    uint8_t fields_[0x18]; // +0x14...: depends on the queue
};

// 0x00456B30: a call through the object's vtable slot 5 (AppHost::tear_down_state for an App) - a jump, its arguments left
// for the callee: the linker folded the identical non-virtual wrappers of several classes into this one.

} // namespace net
