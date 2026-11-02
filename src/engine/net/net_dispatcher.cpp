// The network library's message dispatcher (NetLink::Router). See net_dispatcher.h.

#include "engine/net/net_dispatcher.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <iterator>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/chain_start.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app.h"
#include "engine/net/net_guards.h"
#include "engine/net/net_manager.h"
#include "formats/byte_stream.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/checked_index.h"

namespace net {

namespace {

constexpr uint32_t kFixedSizeCopy = 0x514; // what a message without a length field copies

void construct_context(InboundMsgCtx* context) { *context = InboundMsgCtx(); }

} // namespace

// 0x0045A590
NoteHandler* NoteHandler::teardown(unsigned int flags)
{
    list_link_.uncouple(); // (lists::Elem<NoteHandler>'s 0x00457030)
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0045A640
void Router::prepare_all()
{
    for (int id = 0; id < kNumMessageIds; ++id) {
        const memory::ListNode* node = link_chain(id).next;
        // (the head check comes before the null check)
        if (node->data == memory::ListNode::head_marker() || !node)
            continue;
        for (bool again = true; again; again = node) {
            const memory::ListNode* next = node->next;
            if (next->data == memory::ListNode::head_marker())
                next = nullptr;
            static_cast<NoteHandler*>(node->data)->teardown(1); // (the handler is its node)
            node = next;
        }
    }
}

// 0x0045A690
void Router::tear_down_state()
{
    for (int id = 0; id < kNumMessageIds; ++id) {
        lists::Hunt<NoteHandler> search;
        for (NoteHandler* handler = search.head_element(&link_chain(id)); handler;) {
            NoteHandler* next = search.following_item();
            handler->teardown(1);
            handler = next;
        }
    }
}

// 0x0045A5C0
NoteHandler* Router::append_listener(uint8_t id, HandlerSlotValue code, uint32_t flags, void* data, int priority)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->network_heap_);
    auto* handler = static_cast<NoteHandler*>(memory::alloc_zeroed(sizeof(NoteHandler)));
    if (handler) {
        handler->list_link_.make_node(handler, priority);
        handler->code_ = code;
        handler->content_ = data;
        handler->mode_bits_ = flags;
    }
    // (a failed allocation reaches append_vertex as null and crashes there, as in the original)
    lists::append_vertex(link_chain(id), handler ? &handler->list_link_ : nullptr);
    memory::Hub::get_pull()->leave_heap_scope();
    return handler;
}

// 0x0045A750
int Router::dispatch_msg(InboundMsgCtx* context)
{
    // Not original - what THUGPRO players send that THUG2 hasn't, checked first (TP-R8, net_guards.h).
    const int guarded = online::guards::check_received_message(context);
    if (guarded >= 0)
        return guarded;
    lists::Hunt<NoteHandler> search;
    const NoteHandler* next = nullptr;
    for (const NoteHandler* handler = search.head_element(&link_chain(context->type_code_)); handler;
         handler = next) {
        next = search.following_item();
        if ((handler->mode_bits_ & context->wire_options_) != context->wire_options_)
            continue;
        context->content_ = handler->content_;
        const int result = handler->code_(context);
        switch (result) {
        case kResultFailed:
        case kResultStop:
        case kHandlerKeep:
        case kResultMsgFreed:
            return result;
        case kResultConsumed:
            return kResultPassOn;
        default:
            break;
        }
    }
    return kResultPassOn;
}

// 0x0045A830
int Router::invoke_handlers_for_msg(Conn* conn, uint32_t flags)
{
    InboundMsgCtx context;
    std::array<uint8_t, kConnReceiveBufferSize> buffer{};
    construct_context(&context);
    context.receiver_app_ = conn->owner_app_;
    context.sender_conn_ = conn;
    context.wire_options_ = flags;
    context.body_ = buffer.data();
    context.body_size_ = 0;
    void* metrics = &conn->receive_metrics_;

    // (the messages one after another: net_wire.h)
    util::ByteReader packet(std::span<const uint8_t>(conn->incoming_.filled()));
    for (; packet.ok() && packet.remaining() > 0; packet.skip(context.body_size_)) {
        context.type_code_ = packet.u8();
        Hub* table = fetch_hub(conn->owner_app_);
        const bool has_length = !(message_flags(table, context.type_code_) & kMsgNoLength);
        if (has_length) {
            context.body_size_ = packet.u16();
            // NOT ORIGINAL: a length past the packet's end ends it (the original copied that much into its buffer)
            if (!packet.ok() || context.body_size_ > packet.remaining())
                break;
        } // (else the length stays what the previous message had)
        const uint8_t* p = packet.cursor();
        if (context.type_code_ == kMsgSequenced) // counted by the message inside
            add_message_metric(metrics, *std::next(p, 5), context.body_size_);
        if (has_length) {
            if (context.body_size_)
                std::memcpy(buffer.data(), p, context.body_size_);
        } else {
            // a whole buffer's worth, whatever the message's size (NOT ORIGINAL: no further than the packet's end -
            // the original read on past the connection's receive buffer)
            const size_t copy = packet.remaining() < kFixedSizeCopy ? packet.remaining() : kFixedSizeCopy;
            std::memcpy(buffer.data(), p, copy);
        }
        if (is_outsider(conn))
            context.wire_options_ |= kPacketForeign;
        const int result = dispatch_msg(&context);
        if (result == kResultFailed || result == kResultStop) {
            conn->incoming_.used = 0;
            return kResultStop;
        }
        if (result == kResultMsgFreed)
            return kResultMsgFreed; // (the connection is gone)
        if (context.type_code_ != kMsgSequenced)
            add_message_metric(metrics, context.type_code_, context.body_size_);
        // (handlers may have changed the length: the timestamp and ack handlers set it)
    }

    if (context.acknowledge_) {
        for (int i = 0; i < context.num_sequences_to_ack_; ++i) {
            NetMessageInfo desc;
            construct_msg_desc(&desc);
            auto sequence = static_cast<uint16_t>(core::at(context.sequences_to_ack_, i));
            desc.content_ = &sequence;
            desc.byte_count_ = sizeof(sequence);
            desc.ident_ = kMsgAck;
            put_on_outbox(context.receiver_app_, context.sender_conn_->hdl_, &desc);
        }
    }
    conn->incoming_.used = 0;
    return kResultPassOn;
}

} // namespace net
