// NetLink::AppHost's socket I/O, connection list and message queues (Neversoft's Net library). See net_app.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "engine/net/net_app.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app_messages.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_messages.h"

namespace net {

namespace {

// (The message ids and handler results: net_access_c.h, net_dispatcher.h.)

// Router::append_listener's packet-flags masks (3: foreign packets too) and the priority Init gives its handlers.
constexpr uint32_t kHandleFlags2 = 2;
constexpr uint32_t kHandleFlags3 = 3;
constexpr uint32_t kHighPriority = 0x80;

// (NetLink::InboundMsgCtx: net_dispatcher.h)

void dealloc(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

// An object deleted through its vtable's scalar deleting destructor (in `slot`).
void delete_virtual(void* object, int slot)
{
    auto* vtable = *static_cast<void***>(object);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[slot])(object, 1);
}

// Conn's methods (net_conn.cpp)
uint32_t transmit_period(Conn* conn) { return conn->get_transmit_period(); }
bool send_now(Conn* conn) { return conn->is_send_forced(); }
// twice the average latency, at least 500 ms (1000 ms with status bit 2)
uint32_t retransmit_limit(Conn* conn) { return conn->get_retry_delay(); }
void delete_conn(Conn* conn)
{
    if (conn) {
        conn->teardown();
        dealloc(conn);
    }
}

// PendingMsg's destructor (frees the data)
void delete_msg(void* msg)
{
    static_cast<PendingMsg*>(msg)->destroy_body();
    dealloc(msg);
}

// The Dispatcher (net_dispatcher.h) the App's handler lists are.
void dispatcher_init(HandlerLists* dispatcher) { static_cast<Router*>(dispatcher)->prepare_all(); }
void dispatcher_deinit(HandlerLists* dispatcher) { static_cast<Router*>(dispatcher)->tear_down_state(); }
void append_listener(HandlerLists* dispatcher, uint32_t id, HandlerSlotValue handler, uint32_t flags, uint32_t priority)
{
    static_cast<Router*>(dispatcher)
        ->append_listener(static_cast<uint8_t>(id), handler, flags, nullptr, static_cast<int>(priority));
}
int DispatchMessage(HandlerLists* dispatcher, InboundMsgCtx* context)
{
    return static_cast<Router*>(dispatcher)->dispatch_msg(context);
}

uint16_t name_hash(const uint8_t* data, int length)
{
    uint32_t crc = checksums::generate_crc_from_buffer(data, length);
    return static_cast<uint16_t>(crc + (crc >> 16));
}

// `node` out of its list (and linked to itself; its vtable stays).
void drop_out(memory::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// 0x00456910
void AppHost::encrypt(const uint8_t* data, uint8_t* out, int length, int* out_length)
{
    uint16_t checksum = name_hash(data, length);
    std::memcpy(out, &checksum, sizeof(checksum));
    for (int i = 0; i < length; ++i)
        out[2 + i] = data[i] ^ static_cast<uint8_t>(checksum);
    *out_length = length + 2;
}

// 0x00456CC0
bool AppHost::decrypt(const uint8_t* packet, uint8_t* out, int length)
{
    if (length < 3)
        return false;
    if (!(flag_bits_ & kSecure)) {
        std::memcpy(out, packet, length);
        return true;
    }
    uint16_t checksum;
    std::memcpy(&checksum, packet, sizeof(checksum));
    length -= 2;
    uint8_t buffer[kMaxPacketSize]; // (a longer packet overruns it, as in the original)
    std::memcpy(buffer, packet + 2, length);
    for (int i = 0; i < length; ++i)
        buffer[i] ^= static_cast<uint8_t>(checksum);
    if (name_hash(buffer, length) != checksum)
        return false;
    std::memcpy(out, buffer, length);
    return true;
}

// 0x00456A90
void AppHost::raise_fault()
{
    char message[0x400];
    std::snprintf(message, sizeof(message), "%s: Error %d\n", label_, WSAGetLastError());
    OutputDebugStringA(message);
}

// 0x00456B00
bool AppHost::is_send_due(Conn* conn)
{
    uint32_t interval = transmit_period(conn);
    if (clock_ - conn->last_transmit_at_ >= interval)
        return true;
    return send_now(conn);
}

// 0x00456B30
__declspec(naked) void virtual_slot5_jump()
{
    __asm {
        mov eax, [ecx]
        jmp dword ptr [eax + 0x14]
    }
}

// 0x00456B40
void AppHost::link_duplicate_conns(Conn* first, Conn* second)
{
    first->alias_conn_ = second;
    second->alias_conn_ = first;
}

// 0x00456BA0
bool AppHost::get_bind(uint32_t /*address*/, uint16_t port)
{
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    // (The original asks the manager for its +0x128 value and sets the address to INADDR_ANY unless it's 1 - it's
    // INADDR_ANY already either way.)
    if (static_cast<Hub*>(supervisor_)->get_net_link_kind() != Hub::kConnectionModem)
        local.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        if (WSAGetLastError() != WSAEADDRINUSE) {
            raise_fault();
            return false;
        }
        raise_fault();
        local.sin_port = htons(0);
        if (bind(sock_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
            raise_fault();
            return false;
        }
    }
    int length = sizeof(sockaddr_in);
    if (getsockname(sock_, reinterpret_cast<sockaddr*>(local_address_), &length) < 0) {
        raise_fault();
        return false;
    }
    return true;
}

// 0x00456D90
SeqQueuedMsg* AppHost::new_queued_msg(uint8_t id, uint16_t length, void* data, uint8_t arg4, uint8_t arg5)
{
    void* memory = memory::alloc(sizeof(SeqQueuedMsg));
    if (!memory)
        return nullptr;
    // SeqQueuedMsg's constructor (copies the data, compressing it if `arg5`)
    return static_cast<SeqQueuedMsg*>(memory)->make_new(id, length, data, arg4, arg5);
}

// 0x00456E20
bool AppHost::transmit_to(uint32_t ip, uint16_t port, const uint8_t* data, int length, int flags)
{
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = ip;
    int result;
    if (flag_bits_ & kSecure) {
        encrypt(data, scrambled_, length, &length);
        result = sendto(sock_, reinterpret_cast<const char*>(scrambled_), length, flags,
                        reinterpret_cast<sockaddr*>(&to), sizeof(to));
    } else {
        result = sendto(sock_, reinterpret_cast<const char*>(data), length, flags, reinterpret_cast<sockaddr*>(&to),
                        sizeof(to));
    }
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
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
        encrypt(data, scrambled_, length, &length);
        return send(sock_, reinterpret_cast<const char*>(scrambled_), length, flags) != SOCKET_ERROR;
    }
    return send(sock_, reinterpret_cast<const char*>(data), length, flags) != SOCKET_ERROR;
}

// 0x00457110
bool AppHost::prepare_all()
{
    if (!(flag_bits_ & kLocal)) {
        sock_ = static_cast<uint32_t>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (sock_ == INVALID_SOCKET) {
            raise_fault();
            return false;
        }
        u_long on = 1;
        if (ioctlsocket(sock_, FIONBIO, &on) == SOCKET_ERROR) {
            raise_fault();
            return false;
        }
        if ((flag_bits_ & kBroadcast) && setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on),
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
bool AppHost::deliver_to(uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port, int flags)
{
    uint8_t packet[kMaxPacketSize];
    packet[0] = id;
    std::memcpy(packet + 1, &length, sizeof(length));
    if (length > 0)
        std::memcpy(packet + 3, data, length);
    return transmit_to(ip, port, packet, length + 3, flags);
}

// 0x00457300
void AppHost::sever_link(Conn* conn)
{
    drop_out(&conn->app_node_);
    delete_conn(conn);
}

// 0x004573D0
Conn* AppHost::step_conn_walk(ConnSearch* search)
{
    memory::ListNode* next = search->list_link_->next;
    search->list_link_ = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
    return search->list_link_ ? static_cast<Conn*>(search->list_link_->data) : nullptr;
}

// 0x00457400
void AppHost::block_connection(Conn* conn)
{
    auto* banned = static_cast<BlockedConnEntry*>(memory::alloc_zeroed(sizeof(BlockedConnEntry)));
    if (banned)
        *static_cast<memory::ListNode*>(banned) = {kBannedConnVtable, banned, 0, banned, banned};
    banned->remote_ip_ = conn->remote_ip_; // (an allocation failure crashes here, as in the original)
    // at the end of the list
    banned->prev = banned_.prev;
    banned->next = &banned_;
    banned_.prev->next = banned;
    banned_.prev = banned;
}

// 0x00457490
void AppHost::tear_down_state()
{
    if (!(flag_bits_ & kLocal))
        closesocket(sock_);
    dispatcher_deinit(&dispatcher_);
    lists::Hunt<Conn> search;
    for (Conn *conn = search.head_element(&circuits_), *next; conn; conn = next) {
        next = search.following_item();
        delete_conn(conn);
    }
}

// 0x00457560
bool AppHost::dispatch_queued_sequenced_messages(Conn* conn)
{
    InboundMsgCtx context;
    context.make_new();
    for (memory::ListNode& queue : conn->received_queues_) {
        lists::Hunt<TimedMsgNode> search;
        for (TimedMsgNode *link = search.head_element(&queue), *next; link; link = next) {
            next = search.following_item();
            uint8_t group = link->get_at<uint8_t>(0x15);
            if (link->get_at<uint32_t>(0x20) != conn->sequence_numbers_[group])
                break; // not its turn yet
            auto* msg = link->get_at<SeqQueuedMsg*>(0x28);
            context.sender_conn_ = static_cast<Conn*>(conn);
            context.body_ = msg->content_;
            context.receiver_app_ = static_cast<AppHost*>(this);
            context.wire_options_ = 0;
            context.type_code_ = msg->ident_;
            context.body_size_ = msg->byte_count_;
            ++conn->sequence_numbers_[group];
            int result = DispatchMessage(&dispatcher_, &context);
            if (result == kHandlerKeep)
                return true;
            if (result == kResultMsgFreed)
                return false;
            if (msg)
                delete_msg(msg);
            delete_virtual(link, 0);
            if (result != kResultPassOn)
                return true;
        }
    }
    return true;
}

// 0x00457710
void AppHost::destroy_body()
{
    vtable_ = kAppVtable;
    lists::Hunt<BlockedConnEntry> search;
    for (BlockedConnEntry *banned = search.head_element(&banned_), *next; banned; banned = next) {
        next = search.following_item();
        if (banned)
            delete_virtual(banned, 0);
    }
    for (void* task : {send_task_, receive_task_, process_task_, metrics_task_}) {
        if (task)
            delete_virtual(task, 2);
    }
    banned_.unlink_as(kBannedConnNodeVtable);
    circuits_.unlink_as(kConnNodeVtable);
    list1414_.unlink_as(kAppListNodeVtable);
    dispatcher_.teardown();
}

// 0x004578B0
Conn* AppHost::find_conn_at_address(uint32_t ip, uint16_t port)
{
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
        if (conn->remote_ip_ == ip && conn->port_no_ == port)
            return conn;
    }
    return nullptr;
}

// 0x00457910
Conn* AppHost::find_conn_with_id(int handle)
{
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
        if (conn->hdl_ == handle)
            return conn;
    }
    return nullptr;
}

// 0x00457970
bool AppHost::important_messages_due(Conn* conn)
{
    lists::Hunt<TimedMsgNode> important;
    for (TimedMsgNode* link = important.head_element(&conn->important_msgs_); link; link = important.following_item()) {
        uint32_t sent = link->get_at<uint32_t>(0x14);
        if (!sent || clock_ - sent > retransmit_limit(conn))
            return true;
    }
    lists::Hunt<TimedMsgNode> sequenced;
    for (TimedMsgNode* link = sequenced.head_element(&conn->sequenced_msgs_); link; link = sequenced.following_item()) {
        uint32_t sent = link->get_at<uint32_t>(0x1C);
        if (!sent || clock_ - sent > retransmit_limit(conn))
            return true;
    }
    return false;
}

// 0x00457AC0
bool AppHost::messages_due(Conn* conn)
{
    lists::Hunt<TimedMsgNode> normal;
    for (TimedMsgNode* link = normal.head_element(&conn->normal_msgs_); link; link = normal.following_item()) {
        if (clock_ >= link->get_at<uint32_t>(0x18))
            return true;
    }
    lists::Hunt<TimedMsgNode> important;
    for (TimedMsgNode* link = important.head_element(&conn->important_msgs_); link; link = important.following_item()) {
        uint32_t sent = link->get_at<uint32_t>(0x14);
        if (!sent || clock_ < link->get_at<uint32_t>(0x24) || clock_ - sent > retransmit_limit(conn))
            return true;
    }
    lists::Hunt<TimedMsgNode> sequenced;
    for (TimedMsgNode* link = sequenced.head_element(&conn->sequenced_msgs_); link; link = sequenced.following_item()) {
        uint32_t sent = link->get_at<uint32_t>(0x1C);
        if (!sent || clock_ < link->get_at<uint32_t>(0x24) || clock_ - sent > retransmit_limit(conn))
            return true;
    }
    return false;
}

// 0x00457C80
void AppHost::free_messages(Conn* conn, int queue, uint8_t id)
{
    memory::ListNode* list;
    uint32_t msg_offset;
    switch (queue) {
    case 0:
        list = &conn->normal_msgs_;
        msg_offset = 0x14;
        break;
    case 1:
        list = &conn->important_msgs_;
        msg_offset = 0x18;
        break;
    case 2:
        list = &conn->sequenced_msgs_;
        msg_offset = 0x28;
        break;
    default:
        return;
    }
    lists::Hunt<TimedMsgNode> search;
    for (TimedMsgNode *link = search.head_element(list), *next; link; link = next) {
        next = search.following_item();
        void* msg = link->get_at<void*>(msg_offset);
        if (static_cast<PendingMsg*>(msg)->ident_ != id)
            continue;
        int& ref_count =
            queue == 2 ? static_cast<SeqQueuedMsg*>(msg)->share_count_ : static_cast<PendingMsg*>(msg)->share_count_;
        if (--ref_count == 0)
            delete_msg(msg);
        delete_virtual(link, 0);
    }
}

} // namespace net
