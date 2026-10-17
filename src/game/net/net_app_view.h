#pragma once

// The messages a Net::Conn's queues share (QueuedMsg, SeqQueuedMsg) and the queue links' common part. Net::App's
// methods 0x00456910-0x00457EB0 are defined in net_app_view.cpp (App: net_app.h; Conn: net_conn.h).

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Net {

// A message's contents as queued for sending. Several connections' queues can share one: a reference count.
// Two kinds: those of the normal and important queues (constructor 0x00454FA0) and those of the sequenced
// queue (0x00455040, 0x10 bytes); the destructor 0x004550F0 (both) frees the data.
struct QueuedMsg {
    uint8_t m_id;      // +0x00
    uint16_t m_length; // +0x02
    char* m_data;      // +0x04
    int m_refCount;    // +0x08
};
struct SeqQueuedMsg {
    uint8_t m_id;      // +0x00
    uint16_t m_length; // +0x02
    char* m_data;      // +0x04
    uint8_t m_byte8;   // +0x08 (the constructor's 4th argument)
    int m_refCount;    // +0x0C
};
static_assert(offsetof(QueuedMsg, m_refCount) == 0x08 && offsetof(SeqQueuedMsg, m_data) == 0x04 &&
                  offsetof(SeqQueuedMsg, m_refCount) == 0x0C && sizeof(SeqQueuedMsg) == 0x10,
              "layout must match the original");

// The links that queue a QueuedMsg on a connection: list nodes (data = the link itself), deleted through
// their vtable's scalar deleting destructor.
struct MsgLink : Mem::ListNode {
    // +0x14...: depends on the queue (see Conn).
    uint8_t m_fields[0x18];

    template <typename T>
    T& At(uint32_t offset)
    {
        return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) + offset);
    }
};

// 0x00456B30: a call through the object's vtable slot 5 (App::Deinit for an App) - a jump, its arguments left
// for the callee: the linker folded the identical non-virtual wrappers of several classes into this one.
void VirtualSlot5Jump();

} // namespace Net
