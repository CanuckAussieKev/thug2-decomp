// NetLink::AppHost's socket I/O, connection list and message queues. See net_app.h.

// NOLINTNEXTLINE(bugprone-reserved-identifier, readability-identifier-naming): Winsock's own macro
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "engine/net/net_app.h"
#include "engine/core/task_list.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <array>
#include <memory>
#include <span>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app_view.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_messages.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/span_copy.h"
#include "platform/sockets.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/net/net_address.h"

namespace net {

namespace {

// (The message ids and handler results: net_access_c.h, net_dispatcher.h.)

// Router::append_listener's packet-flags masks (3: foreign packets too) and the priority Init gives its handlers.
constexpr uint32_t kHandleFlags2 = 2;
constexpr uint32_t kHandleFlags3 = 3;
constexpr uint32_t kHighPriority = 0x80;

// (NetLink::InboundMsgCtx: net_dispatcher.h)

void dealloc(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

// A message link deleted (every link type's scalar deleting destructor: out of its list, freed).
void delete_link(TimedMsgNode* link) { memory::ListNode::destroy(link); }
void delete_link(OrderedMsgNode* link) { memory::ListNode::destroy(&link->list_link()); }

// Conn's methods (net_conn.cpp)
uint32_t transmit_period(Conn* conn) { return conn->get_transmit_period(); }
bool send_now(Conn* conn) { return conn->is_send_forced(); }
// twice the average latency, at least 500 ms (1000 ms with status bit 2)
uint32_t retransmit_limit(Conn* conn) { return conn->get_retry_delay(); }
void delete_conn(Conn* conn)
{
    if (conn) {
        conn->~Conn();
        dealloc(conn);
    }
}

// PendingMsg's destructor (frees the data)
void delete_msg(void* msg)
{
    static_cast<PendingMsg*>(msg)->~PendingMsg();
    dealloc(msg);
}

// The Dispatcher (net_dispatcher.h) the App's handler lists are.
void dispatcher_init(HandlerLists* dispatcher) { core::down_cast<Router>(dispatcher)->prepare_all(); }
void dispatcher_deinit(HandlerLists* dispatcher) { core::down_cast<Router>(dispatcher)->tear_down_state(); }
void append_listener(HandlerLists* dispatcher, uint32_t id, HandlerSlotValue handler, uint32_t flags, uint32_t priority)
{
    core::down_cast<Router>(dispatcher)
        ->append_listener(static_cast<uint8_t>(id), handler, flags, nullptr, static_cast<int>(priority));
}
int DispatchMessage(HandlerLists* dispatcher, InboundMsgCtx* context)
{
    return core::down_cast<Router>(dispatcher)->dispatch_msg(context);
}

uint16_t name_hash(const uint8_t* data, int length)
{
    const uint32_t crc = checksums::generate_crc_from_buffer(data, length);
    return static_cast<uint16_t>(crc + (crc >> 16));
}

// `node` out of its list (and linked to itself; its vtable stays).
void drop_out(memory::ListNode* node)
{
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
}

} // namespace

// 0x00456910
void AppHost::encrypt(const uint8_t* data, uint8_t* out, int length, int* out_length)
{
    uint16_t checksum = name_hash(data, length);
    std::memcpy(out, &checksum, sizeof(checksum));
    const std::span<const uint8_t> in(data, static_cast<size_t>(length));
    const std::span<uint8_t> sealed(out, static_cast<size_t>(length) + 2);
    for (size_t i = 0; i < in.size(); ++i)
        core::at(sealed, 2 + i) = core::at(in, i) ^ static_cast<uint8_t>(checksum);
    *out_length = length + 2;
}

// 0x00456CC0
bool AppHost::decrypt(std::span<const uint8_t> packet, std::span<uint8_t> out)
{
    int length = static_cast<int>(packet.size());
    if (length < 3)
        return false;
    if (!(flag_bits_ & kSecure)) {
        core::copy_into(packet, out);
        return true;
    }
    uint16_t checksum = 0;
    std::memcpy(&checksum, packet.data(), sizeof(checksum));
    length -= 2;
    // (a longer packet overruns the buffer, as in the original: here it ends the game with a report)
    std::array<uint8_t, kMaxPacketSize> buffer{};
    const std::span<uint8_t> body = std::span<uint8_t>(buffer).first(static_cast<size_t>(length));
    core::copy_into(packet.subspan(2), body);
    for (int i = 0; i < length; ++i)
        core::at(buffer, i) ^= static_cast<uint8_t>(checksum);
    if (name_hash(buffer.data(), length) != checksum)
        return false;
    core::copy_into(std::span<const uint8_t>(body), out);
    return true;
}

// 0x00456A90
void AppHost::raise_fault()
{
    std::array<char, 0x400> message{};
    text::format_to(message, "%s: Error %d\n", label_.data(), WSAGetLastError());
    OutputDebugStringA(message.data());
}

// 0x00456B00
bool AppHost::is_send_due(Conn* conn)
{
    const uint32_t interval = transmit_period(conn);
    if (clock_ - conn->last_transmit_at() >= interval)
        return true;
    return send_now(conn);
}

// 0x00456B40
void AppHost::link_duplicate_conns(Conn* first, Conn* second)
{
    first->set_alias_conn(second);
    second->set_alias_conn(first);
}

// 0x00456BA0
bool AppHost::get_bind(uint32_t /*address*/, uint16_t port)
{
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    // (The original asks the manager for its +0x128 value and sets the address to INADDR_ANY unless it's 1 - it's
    // INADDR_ANY already either way.)
    if (supervisor_->get_net_link_kind() != Hub::kConnectionModem)
        local.sin_addr.s_addr = INADDR_ANY;
    if (sockets::bind_to(sock_, local) < 0) {
        if (WSAGetLastError() != WSAEADDRINUSE) {
            raise_fault();
            return false;
        }
        raise_fault();
        local.sin_port = htons(0);
        if (sockets::bind_to(sock_, local) < 0) {
            raise_fault();
            return false;
        }
    }
    int length = sizeof(sockaddr_in);
    sockaddr_in bound{};
    if (sockets::name_of(sock_, &bound, &length) < 0) {
        raise_fault();
        return false;
    }
    std::memcpy(local_address_.data(), &bound, sizeof(bound));
    return true;
}

// 0x00456D90
gsl::owner<SeqQueuedMsg*> AppHost::new_queued_msg(uint8_t id, uint16_t length, const void* data, uint8_t arg4, uint8_t arg5)
{
    void* memory = memory::alloc(sizeof(SeqQueuedMsg));
    if (!memory)
        return nullptr;
    // SeqQueuedMsg's constructor (copies the data, compressing it if `arg5`)
    return memory::construct_in<SeqQueuedMsg>(memory, id, length, data, SeqQueuedMsg::Options{.byte8 = arg4, .compression = arg5});
}

// 0x00456E20
bool AppHost::transmit_to(net::Address address, const uint8_t* data, int length, int flags)
{
    const auto ip = address.ip;
    const auto port = address.port;
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = ip;
    int result = 0;
    if (flag_bits_ & kSecure) {
        encrypt(data, scrambled_.data(), length, &length);
        result = sockets::send_bytes_to(sock_, scrambled_.data(), length, flags, to);
    } else {
        result = sockets::send_bytes_to(sock_, data, length, flags, to);
    }
    if (result == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
            return false;
    }
    return true;
}

// 0x00456F10
bool AppHost::post_out(const uint8_t* data, int length, int flags)
{
    if (!linked_up_)
        return false;
    if (flag_bits_ & kSecure) {
        encrypt(data, scrambled_.data(), length, &length);
        return sockets::send_bytes(sock_, scrambled_.data(), length, flags) != SOCKET_ERROR;
    }
    return sockets::send_bytes(sock_, data, length, flags) != SOCKET_ERROR;
}

// 0x00457110
bool AppHost::prepare_all()
{
    if (!(flag_bits_ & kLocal)) {
        const SOCKET made = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sock_ = static_cast<uint32_t>(made);
        constexpr auto kNoSocket = INVALID_SOCKET; // (winsock's macro: a C cast of ~0)
        if (made == kNoSocket) {
            raise_fault();
            return false;
        }
        u_long on = 1;
        if (ioctlsocket(sock_, FIONBIO, &on) == SOCKET_ERROR) {
            raise_fault();
            return false;
        }
        if ((flag_bits_ & kBroadcast) && sockets::set_option(sock_, SOL_SOCKET, SO_BROADCAST, &on,
                                                    sizeof(on)) == SOCKET_ERROR) {
            raise_fault();
            return false;
        }
    }
    open_links_ = 0;
    outbound_byte_total_ = 0;
    inbound_byte_total_ = 0;
    skipped_stamps_ = 0;
    stale_arrivals_ = 0;
    repeat_arrivals_ = 0;
    frame_count_ = 0;
    dispatcher_init(&dispatcher_);
    // the handlers (net_app_messages.cpp)
    append_listener(&dispatcher_, kMsgAck, AppHost::on_receipt_msg, kHandleFlags3, kHighPriority);
    append_listener(&dispatcher_, kMsgSequenced, AppHost::on_ordered_msgs, kHandleFlags2, kHighPriority);
    append_listener(&dispatcher_, kMsgCompressed, AppHost::handle_compressed_messages, kHandleFlags3, kHighPriority);
    append_listener(&dispatcher_, kMsgBulkBegin, AppHost::drain_stream_msgs, kHandleFlags2, kHighPriority);
    append_listener(&dispatcher_, kMsgBulkChunk, AppHost::drain_stream_msgs, kHandleFlags2, kHighPriority);
    append_listener(&dispatcher_, kMsgLatencyTest, AppHost::on_ping_probe_msg, kHandleFlags2, kHighPriority);
    append_listener(&dispatcher_, kMsgLatencyResponse, AppHost::on_ping_reply_msg, kHandleFlags2, kHighPriority);
    return true;
}

// 0x00457280
bool AppHost::deliver_to(AppHost::BulkHead head, const void* data, net::Address address, int flags)
{
    const auto id = head.msg_id_;
    const auto length = head.size;
    const auto ip = address.ip;
    const auto port = address.port;
    std::array<uint8_t, kMaxPacketSize> packet{};
    core::at(packet, 0) = id;
    std::memcpy(std::span<uint8_t>(packet).subspan(1).data(), &length, sizeof(length));
    if (length > 0)
        std::memcpy(std::span<uint8_t>(packet).subspan(3).data(), data, length);
    return transmit_to({.ip = ip, .port = port}, packet.data(), length + 3, flags);
}

// 0x00457300
void AppHost::sever_link(Conn* conn)
{
    drop_out(&conn->app_node());
    delete_conn(conn);
}

// 0x004573D0
Conn* AppHost::step_conn_walk(ConnSearch* search)
{
    memory::ListNode* next = search->list_link_->next();
    search->list_link_ = next->data() == memory::ListNode::head_marker() ? nullptr : next;
    return search->list_link_ ? static_cast<Conn*>(search->list_link_->data()) : nullptr;
}

// 0x00457400
void AppHost::block_connection(Conn* conn)
{
    auto* banned = static_cast<BlockedConnEntry*>(memory::alloc_zeroed(sizeof(BlockedConnEntry)));
    if (banned)
        banned->make_node(banned);
    banned->set_remote_ip(conn->remote_ip()); // (an allocation failure crashes here, as in the original)
    // at the end of the list
    banned->set_prev(banned_.prev());
    banned->set_next(&banned_);
    banned_.prev()->set_next(banned);
    banned_.set_prev(banned);
}

// 0x00457490
void AppHost::tear_down_state()
{
    if (!(flag_bits_ & kLocal))
        closesocket(sock_);
    dispatcher_deinit(&dispatcher_);
    lists::Hunt<Conn> search;
    for (Conn *conn = search.head_element(&circuits_), *next = nullptr; conn; conn = next) {
        next = search.following_item();
        delete_conn(conn);
    }
}

// 0x00457560
bool AppHost::dispatch_queued_sequenced_messages(Conn* conn)
{
    InboundMsgCtx context;
    for (memory::ListNode& queue : conn->received_queues()) {
        lists::Hunt<OrderedMsgNode> search;
        for (OrderedMsgNode *link = search.head_element(&queue), *next = nullptr; link; link = next) {
            next = search.following_item();
            const uint8_t group = link->bundle_id();
            if (link->order_index() != core::at(conn->sequence_numbers(), group))
                break; // not its turn yet
            SeqQueuedMsg* msg = link->shared_msg();
            context.set_sender_conn(static_cast<Conn*>(conn));
            context.set_body(msg->content());
            context.set_receiver_app(static_cast<AppHost*>(this));
            context.wire_options() = 0;
            context.set_type_code(msg->ident());
            context.set_body_size(msg->byte_count());
            ++core::at(conn->sequence_numbers(), group);
            const int result = DispatchMessage(&dispatcher_, &context);
            if (result == kHandlerKeep)
                return true;
            if (result == kResultMsgFreed)
                return false;
            if (msg)
                delete_msg(msg);
            delete_link(link);
            if (result != kResultPassOn)
                return true;
        }
    }
    return true;
}

// 0x00457710
AppHost::~AppHost()
{
    lists::Hunt<BlockedConnEntry> search;
    for (BlockedConnEntry *banned = search.head_element(&banned_), *next = nullptr; banned; banned = next) {
        next = search.following_item();
        if (banned)
            banned->destroy_banned(1);
    }
    for (std::unique_ptr<tasks::SchedulerTask>* task : {&send_task_, &receive_task_, &process_task_, &metrics_task_})
        memory::destroy_then_clear(*task);
    banned_.uncouple();
    circuits_.uncouple();
    list1414_.uncouple();
    dispatcher_.teardown();
}

// 0x004578B0
Conn* AppHost::find_conn_at_address(uint32_t ip, uint16_t port)
{
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
        if (conn->remote_ip() == ip && conn->port_no() == port)
            return conn;
    }
    return nullptr;
}

// 0x00457910
Conn* AppHost::find_conn_with_id(int handle)
{
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
        if (conn->hdl() == handle)
            return conn;
    }
    return nullptr;
}

// 0x00457970
bool AppHost::important_messages_due(Conn* conn)
{
    lists::Hunt<ReliableMsgNode> important;
    for (const ReliableMsgNode* link = important.head_element(&conn->important_msgs()); link;
         link = important.following_item()) {
        const uint32_t sent = link->stamp_ms();
        if (!sent || clock_ - sent > retransmit_limit(conn))
            return true;
    }
    lists::Hunt<OrderedMsgNode> sequenced;
    for (const OrderedMsgNode* link = sequenced.head_element(&conn->sequenced_msgs()); link;
         link = sequenced.following_item()) {
        const uint32_t sent = link->stamp_ms();
        if (!sent || clock_ - sent > retransmit_limit(conn))
            return true;
    }
    return false;
}

// 0x00457AC0
bool AppHost::messages_due(Conn* conn)
{
    lists::Hunt<MsgNormalLink> normal;
    for (const MsgNormalLink* link = normal.head_element(&conn->normal_msgs()); link; link = normal.following_item()) {
        if (clock_ >= link->due_time())
            return true;
    }
    lists::Hunt<ReliableMsgNode> important;
    for (const ReliableMsgNode* link = important.head_element(&conn->important_msgs()); link;
         link = important.following_item()) {
        const uint32_t sent = link->stamp_ms();
        if (!sent || clock_ < link->due_time() || clock_ - sent > retransmit_limit(conn))
            return true;
    }
    lists::Hunt<OrderedMsgNode> sequenced;
    for (const OrderedMsgNode* link = sequenced.head_element(&conn->sequenced_msgs()); link;
         link = sequenced.following_item()) {
        const uint32_t sent = link->stamp_ms();
        if (!sent || clock_ < link->due_time() || clock_ - sent > retransmit_limit(conn))
            return true;
    }
    return false;
}

// 0x00457C80
void AppHost::free_messages(Conn* conn, QueueMessages queue_messages)
{
    const auto queue = queue_messages.queue;
    const auto id = queue_messages.id;
    memory::ListNode* list = nullptr;
    switch (queue) {
    case 0:
        list = &conn->normal_msgs();
        break;
    case 1:
        list = &conn->important_msgs();
        break;
    case 2:
        list = &conn->sequenced_msgs();
        break;
    default:
        return;
    }
    // The link's message (each queue's link type keeps it at its own place).
    auto message_of = [queue](const TimedMsgNode* link) -> void* {
        if (queue == 0)
            return static_cast<MsgNormalLink*>(link->data())->shared_msg();
        if (queue == 1)
            return static_cast<ReliableMsgNode*>(link->data())->shared_msg();
        return static_cast<OrderedMsgNode*>(link->data())->shared_msg();
    };
    lists::Hunt<TimedMsgNode> search;
    for (TimedMsgNode *link = search.head_element(list), *next = nullptr; link; link = next) {
        next = search.following_item();
        void* msg = message_of(link);
        if (static_cast<PendingMsg*>(msg)->ident() != id)
            continue;
        int& ref_count =
            queue == 2 ? static_cast<SeqQueuedMsg*>(msg)->share_count() : static_cast<PendingMsg*>(msg)->share_count();
        if (--ref_count == 0)
            delete_msg(msg);
        delete_link(link);
    }
}

} // namespace net
