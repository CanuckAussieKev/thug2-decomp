#pragma once

// The network library's message dispatcher (NetLink::Router, an App's +0x0C): handler lists per message id,
// and the context a handler gets.

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/lists/chain_start.h"
#include "engine/net/net_lists.h"
#include "engine/core/checked_index.h"

namespace net {

struct AppHost;
struct Conn;

// What a handler returns.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): values fixed by a file, a script, the wire or the SDK, used as integers
enum HandlerResult : uint8_t {
    kResultFailed = 0,   // stop: the rest of the packet is dropped
    kResultPassOn = 1,   // on to the next handler
    kResultStop = 2,     // stop: the rest of the packet is dropped
    kResultMsgFreed = 3, // stop: the connection is gone (nothing of it may be touched)
    kResultConsumed = 4, // no more handlers for this message; the packet goes on
    kHandlerKeep = 5,    // no more handlers; a streamed message's buffers are kept
};

// Packet flags (InboundMsgCtx::wire_options_; a handler gets a message when its flags include them all).
constexpr uint32_t kPacketForeign = 0x1;          // from an address that isn't a connection
constexpr uint32_t kPacketLate = 0x2;             // its timestamp is older than the newest
constexpr uint32_t kPacketChecksumMismatch = 0x4; // a stream whose checksum doesn't match

// What a handler gets (0xA4 bytes, constructor 0x00454A20).
struct InboundMsgCtx {
    // 0x00454A20 (net_descs.cpp): every field zeroed.
    InboundMsgCtx();

    // The fields other code reads and sets.
    [[nodiscard]] uint8_t* body() const { return body_; }
    void set_body(uint8_t* value) { body_ = value; }
    [[nodiscard]] uint8_t type_code() const { return type_code_; }
    void set_type_code(uint8_t value) { type_code_ = value; }
    [[nodiscard]] uint32_t body_size() const { return body_size_; }
    void set_body_size(uint32_t value) { body_size_ = value; }
    [[nodiscard]] AppHost* receiver_app() const { return receiver_app_; }
    void set_receiver_app(AppHost* value) { receiver_app_ = value; }
    [[nodiscard]] Conn* sender_conn() const { return sender_conn_; }
    void set_sender_conn(Conn* value) { sender_conn_ = value; }
    uint32_t& wire_options() { return wire_options_; }
    [[nodiscard]] uint32_t wire_options() const { return wire_options_; }
    [[nodiscard]] void* content() const { return content_; }
    void set_content(void* value) { content_ = value; }
    std::array<uint32_t, 32>& sequences_to_ack() { return sequences_to_ack_; }
    [[nodiscard]] const std::array<uint32_t, 32>& sequences_to_ack() const { return sequences_to_ack_; }
    int& num_sequences_to_ack() { return num_sequences_to_ack_; }
    [[nodiscard]] int num_sequences_to_ack() const { return num_sequences_to_ack_; }
    [[nodiscard]] bool acknowledge() const { return acknowledge_; }
    void set_acknowledge(bool value) { acknowledge_ = value; }

private:
    uint8_t* body_{nullptr}; // the message (its body_size_ bytes; read through wire/)
    uint8_t type_code_{0};
    std::array<uint8_t, 3> spare_slot5_{};
    uint32_t body_size_{0};
    AppHost* receiver_app_{nullptr};
    Conn* sender_conn_{nullptr};
    uint32_t wire_options_{0};
    void* content_{nullptr};          // the handler's own data (NoteHandler::content_)
    std::array<uint32_t, 32> sequences_to_ack_{}; // the timestamps to acknowledge after the packet (low 16 bits)
    int num_sequences_to_ack_{0};
    bool acknowledge_{false}; // send those acks
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(InboundMsgCtx, sequences_to_ack_) == 0x1C &&
                      offsetof(InboundMsgCtx, num_sequences_to_ack_) == 0x9C && sizeof(InboundMsgCtx) == 0xA4);
    }
};

using HandlerSlotValue = int (*)(InboundMsgCtx* context);

// A message handler (0x20 bytes, from the network heap): an lists::Elem<NoteHandler> (data: itself, priority: the
// order in its list) with the code, its data and the packet flags it takes.
struct NoteHandler {
    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    [[nodiscard]] const HandlerSlotValue& code() const { return code_; }
    void set_code(const HandlerSlotValue& value) { code_ = value; }
    [[nodiscard]] void* content() const { return content_; }
    void set_content(void* value) { content_ = value; }
    [[nodiscard]] uint32_t mode_bits() const { return mode_bits_; }
    void set_mode_bits(uint32_t value) { mode_bits_ = value; }

private:
    memory::ListNode list_link_;
    HandlerSlotValue code_;
    void* content_;
    uint32_t mode_bits_;

public:

    // 0x0045A590 (scalar deleting destructor: the node unlinks itself)
    NoteHandler* teardown(unsigned int flags);
};

// (lists::Hunt<NoteHandler>'s vtable, 0x006475E4, holds only a destructor that resets it: 0x0045A500 / 0x0045A510.)

// (Its handler lists, HandlerLists, and its destructor 0x00457260 are in net_lists.h.)
class Router : public HandlerLists {
public:
    static constexpr int kNumMessageIds = HandlerLists::kCount;

    memory::ListNode& link_chain(int id) { return core::at(lists(), id); }

    // 0x0045A640: every handler deleted (each list walked node by node).
    void prepare_all();
    // 0x0045A690: every handler deleted (walked by item).
    void tear_down_state();
    // 0x0045A5C0: a handler for message `id`, in the network heap.
    NoteHandler* append_listener(uint8_t id, HandlerSlotValue code, uint32_t flags, void* data, int priority);
    // 0x0045A750: the message's handlers, in order, until one stops; HandlerResult (4 comes back as 1).
    int dispatch_msg(InboundMsgCtx* context);
    // 0x0045A830: every message in the connection's read buffer, then the acks they asked for. 1, or 2 (a
    // handler stopped the packet), or 3 (the connection was destroyed).
    int invoke_handlers_for_msg(Conn* conn, uint32_t flags);
};

} // namespace net
