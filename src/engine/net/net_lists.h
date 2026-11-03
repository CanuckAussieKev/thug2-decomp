#pragma once

// The net library's list types (the list templates' instances): their
// destructors, and BlockedConnEntry (an address an App refuses connections from). Each type has its own vtable
// holding only its scalar deleting destructor; the linker folded identical vtables, so one may stand for a
// node type and a head type at once.

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"

namespace net {

// The handler lists a Dispatcher (wt/netC) starts with: one per message id 0..254 (lists::Foremost<NoteHandler>;
// 0x00457010 makes each, 0x00457030 unlinks it).
struct HandlerLists {
    static constexpr int kCount = 255;
    // The fields other code reads and sets.
    std::array<memory::ListNode, kCount>& lists() { return lists_; }
    [[nodiscard]] const std::array<memory::ListNode, kCount>& lists() const { return lists_; }

private:
    std::array<memory::ListNode, kCount> lists_;

public:

    // 0x00457260: the Dispatcher's destructor: each list's, last to first (the compiler's array-destructor
    // helper 0x0062382B).
    void teardown();
};

// An address an App doesn't accept connections from: a node whose data is itself (made by AppHost::block_connection;
// lists::Elem<BlockedConnEntry>, as the head of the App's banned list).
struct BlockedConnEntry : memory::ListNode {
    // The fields other code reads and sets.
    [[nodiscard]] uint32_t remote_ip() const { return remote_ip_; }
    void set_remote_ip(uint32_t value) { remote_ip_ = value; }

private:
    uint32_t remote_ip_; // +0x14

public:

    // 0x00457460: the scalar deleting destructor (0x00457070 / 0x004570D0 the node's: out of its list).
    BlockedConnEntry* destroy_banned(unsigned int flags);
};

// A walk over an App's connections (AppHost::begin_conn_walk): the current node (see lists::Hunt). The original's
// searches have a vtable holding only a destructor that resets it (lists::Hunt<Conn>'s 0x00456B80 / 0x00456FB0,
// lists::Hunt<BlockedConnEntry>'s 0x00456B90 / 0x00456FE0) - not kept.
struct ConnSearch {
    memory::ListNode* list_link_ = nullptr;
};

} // namespace net
