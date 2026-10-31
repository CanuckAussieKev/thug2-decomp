// The network library's message dispatcher (NetLink::Router). See net_dispatcher.h.

#include "engine/net/net_dispatcher.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app_messages.h"
#include "engine/net/net_guards.h"
#include "engine/core/code_address.h"

namespace net {

static VtableSlot g_msg_handler_head_vtable_slots[] = {&net::NoteHandler::teardown};
void** const kMsgHandlerHeadVtable = reinterpret_cast<void**>(g_msg_handler_head_vtable_slots);

namespace {

static VtableSlot g_msg_handler_search_vtable_slots[] = {&net::MsgHandlerSearch::teardown};
void** const kMsgHandlerSearchVtable = reinterpret_cast<void**>(g_msg_handler_search_vtable_slots);
constexpr uint32_t kFixedSizeCopy = 0x514; // what a message without a length field copies

void construct_context(InboundMsgCtx* context) { context->make_new(); }

} // namespace

// 0x0045A590
NoteHandler* NoteHandler::teardown(unsigned int flags)
{
    static_cast<MsgHandlerNode*>(&list_link_)->destroy_body(); // (lists::Elem<NoteHandler>: unlinks it)
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0045A500
void MsgHandlerSearch::destroy_body() { vtable_ = kMsgHandlerSearchVtable; }

// 0x0045A510
MsgHandlerSearch* MsgHandlerSearch::teardown(unsigned int flags)
{
    vtable_ = kMsgHandlerSearchVtable;
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0045A640
void Router::prepare_all()
{
    for (int id = 0; id < kNumMessageIds; ++id) {
        memory::ListNode* node = link_chain(id).list_link_.next;
        // (the head check comes before the null check)
        if (node->data == reinterpret_cast<void*>(-1) || !node)
            continue;
        do {
            memory::ListNode* next = node->next;
            if (next->data == reinterpret_cast<void*>(-1))
                next = nullptr;
            unlink_list_node(node);
            node = next;
        } while (node);
    }
}

// 0x0045A690
void Router::tear_down_state()
{
    for (int id = 0; id < kNumMessageIds; ++id) {
        lists::Hunt<NoteHandler> search;
        for (NoteHandler* handler = search.head_element(&link_chain(id).list_link_); handler;) {
            NoteHandler* next = search.following_item();
            unlink_list_node(handler);
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
        handler->list_link_.vtable = kMsgHandlerHeadVtable;
        handler->list_link_.data = handler;
        handler->list_link_.priority = priority;
        handler->list_link_.next = &handler->list_link_;
        handler->list_link_.prev = &handler->list_link_;
        handler->code_ = code;
        handler->content_ = data;
        handler->mode_bits_ = flags;
    }
    // (a failed allocation reaches append_vertex as null and crashes there, as in the original)
    link_chain(id).append_vertex(reinterpret_cast<memory::ListNode*>(handler));
    memory::Hub::get_pull()->leave_heap_scope();
    return handler;
}

// 0x0045A750
int Router::dispatch_msg(InboundMsgCtx* context)
{
    // Not original - what THUGPRO players send that THUG2 hasn't, checked first (TP-R8, net_guards.h).
    int guarded = online::guards::check_received_message(context);
    if (guarded >= 0)
        return guarded;
    lists::Hunt<NoteHandler> search;
    NoteHandler* next;
    for (NoteHandler* handler = search.head_element(&link_chain(context->type_code_).list_link_); handler; handler = next) {
        next = search.following_item();
        if ((handler->mode_bits_ & context->wire_options_) != context->wire_options_)
            continue;
        context->content_ = handler->content_;
        int result = handler->code_(context);
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
    char buffer[kConnReceiveBufferSize];
    construct_context(&context);
    context.receiver_app_ = conn->owner_app_;
    context.sender_conn_ = conn;
    context.wire_options_ = flags;
    context.body_ = buffer;
    context.body_size_ = 0;
    char*& read_buffer = conn->read_buffer_;
    char*& read_ptr = conn->read_ptr_;
    void* metrics = &conn->receive_metrics_;

    for (char* p = read_buffer; p < read_ptr; p += context.body_size_) {
        context.type_code_ = static_cast<uint8_t>(*p++);
        uint8_t* table = fetch_hub(conn->owner_app_);
        bool has_length = !(message_flags(table, context.type_code_) & kMsgNoLength);
        if (has_length) {
            context.body_size_ = *reinterpret_cast<uint16_t*>(p);
            p += 2;
        } // (else the length stays what the previous message had)
        if (context.type_code_ == kMsgSequenced) // counted by the message inside
            add_message_metric(metrics, static_cast<uint8_t>(p[5]), context.body_size_);
        if (has_length) {
            if (context.body_size_)
                std::memcpy(buffer, p, context.body_size_);
        } else {
            std::memcpy(buffer, p, kFixedSizeCopy); // a whole buffer's worth, whatever the message's size
        }
        if (is_outsider(conn))
            context.wire_options_ |= kPacketForeign;
        int result = dispatch_msg(&context);
        if (result == kResultFailed || result == kResultStop) {
            read_ptr = read_buffer;
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
            uint16_t sequence = static_cast<uint16_t>(context.sequences_to_ack_[i]);
            desc.content_ = &sequence;
            desc.byte_count_ = sizeof(sequence);
            desc.ident_ = kMsgAck;
            put_on_outbox(context.receiver_app_, context.sender_conn_->hdl_, &desc);
        }
    }
    read_ptr = read_buffer;
    return kResultPassOn;
}

} // namespace Net
