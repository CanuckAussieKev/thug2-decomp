// The network library's client (NetLink::Guest). See net_client.h.

#include "engine/net/net_client.h"

#include <winsock2.h>

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_access_c.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/net/task_vtable.h"

namespace net {

namespace {

static VtableSlot g_client_vtable_slots[] = {&net::Guest::teardown, &net::AppHost::handle_incoming, &net::Guest::pull_inbound,
        &net::Guest::transmit, &net::Guest::prepare_all, &net::Guest::tear_down_state};
void** const kClientVtable = reinterpret_cast<void**>(g_client_vtable_slots);
constexpr int kAppTaskPriority = -2000;
constexpr uint32_t kTaskSize = 0x20;
constexpr int kTimestampWindow = 5000; // timestamps further apart than this have wrapped around

uint32_t& get_timestamp(AppHost* app) { return app->clock_; }
Router* get_dispatcher(AppHost* app) { return &app->dispatcher_; }

// The first connection (lists::Hunt<Conn> over the App's connections; the search's vtable is 0x0064759C).
Conn* server_connection(AppHost* app)
{
    lists::Hunt<Conn> search;
    return search.head_element(&app->circuits_);
}

} // namespace

// Shared with Server (net_server.cpp).
void construct_app_task(AppHost* app)
{
    void* task = memory::alloc_zeroed(kTaskSize);
    if (task) {
        static_cast<tasks::SchedulerTask*>(task)->make_new(kAppTaskPriority); // (tasks::Task's base)
        prop<AppHost*>(task, 0x1C) = app;
        prop<void**>(task, 0x0) = game_code::kTaskVtable;
        prop<void*>(task, 0x18) = reinterpret_cast<void*>(&AppHost::flush_outgoing_traffic);
    }
    app->send_task_ = task;
}

// 0x0045A250
Guest* Guest::make_new(int flags)
{
    AppHost::make_new(flags);
    prop<void**>(this, 0) = kClientVtable;
    construct_app_task(this);
    return this;
}

// 0x0045B910
void Guest::destroy_body()
{
    AppHost::destroy_body();
}

// 0x0045B8E0
Guest* Guest::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0045B820
void Guest::tear_down_state()
{
    AppHost::tear_down_state();
}

// 0x00459EC0
bool Guest::prepare_all()
{
    linked_up_ = false;
    get_timestamp(this) = 0;
    if (!AppHost::prepare_all())
        return false;
    Router* dispatcher = get_dispatcher(this);
    dispatcher->append_listener(kMsgConnectionAccepted, on_link_accepted_msg, 3, nullptr, 0x80);
    dispatcher->append_listener(kMsgConnectionRefused, handle_connection_refused, 2, nullptr, 0x80);
    dispatcher->append_listener(kMsgConnectionTerminated, handle_connection_refused, 2, nullptr, 0x80);
    dispatcher->append_listener(kMsgClockStamp, on_clock_sync_msg, 3, nullptr, 0x80);
    return true;
}

// 0x00459F40
bool Guest::dial_server(uint32_t ip, uint16_t port)
{
    auto& address = *reinterpret_cast<sockaddr_in*>(host_sock_addr_);
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = ip;
    std::memset(address.sin_zero, 0, sizeof(address.sin_zero));
    while (connect(sock_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
           SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            report_socket_error(this);
            return false;
        }
    }
    linked_up_ = ip != 0;
    return true;
}

// 0x00459DC0
void Guest::send_bulk_to_host(uint8_t msg_id, uint16_t size, void* data, const char* desc, uint8_t group_id,
                                   bool queue_all_pieces, bool borrow_caller_buffer)
{
    if (Conn* server = server_connection(this))
        send_bulk_to_conn(server, msg_id, size, data, desc, group_id, queue_all_pieces, borrow_caller_buffer);
}

// 0x00459E50
void Guest::send_to_host(NetMessageInfo* desc)
{
    if (Conn* server = server_connection(this))
        net::put_on_outbox(this, server->hdl_, desc);
}

// 0x0045A300
void Guest::pull_inbound()
{
    int from_length = sizeof(sockaddr_in);
    sockaddr_in from = {}; // (the original leaves it uninitialised: recv doesn't fill it in)
    if (has_no_socket(this))
        return;
    int received;
    do {
        char* buffer = receive_buffer_;
        SOCKET socket = sock_;
        if (!linked_up_)
            received = recvfrom(socket, buffer, kReceiveBufferSize, 0, reinterpret_cast<sockaddr*>(&from), &from_length);
        else
            received = recv(socket, buffer, kReceiveBufferSize, 0);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        inbound_byte_total_ += received;
        int size = (flag_bits_ & kAppSecure) ? received - 2 : received;
        auto& server = *reinterpret_cast<sockaddr_in*>(host_sock_addr_);
        if (Conn* conn = net::find_conn_at_address(this, server.sin_addr.s_addr, ntohs(server.sin_port))) {
            char*& read_ptr = conn->read_ptr_;
            if (read_ptr + size < conn->read_buffer_ + kConnReceiveBufferSize) {
                uint32_t time = frame_clock::get_time();
                if (pull_datagram(this, buffer, read_ptr, received)) {
                    read_ptr += size;
                    add_packet_metric(&conn->receive_metrics_, received + 0x1C,
                                    get_timestamp(this));
                    if (time > conn->last_heard_from_)
                        conn->last_heard_from_ = time;
                } else if (auto handler = unknown_packet_sink_) {
                    handler(buffer, received, reinterpret_cast<sockaddr*>(&from));
                }
            }
        } else if (!linked_up_) {
            // from an unknown address
            if (Conn* fresh = net::opened_link(this, from.sin_addr.s_addr, ntohs(from.sin_port), 6)) {
                fresh->owner_app_ = this;
                if (pull_datagram(this, buffer, fresh->read_ptr_, received)) {
                    fresh->read_ptr_ = fresh->read_buffer_ + size;
                    mark_new_connection(fresh);
                } else {
                    if (auto handler = unknown_packet_sink_)
                        handler(buffer, received, reinterpret_cast<sockaddr*>(&from));
                    destroy_conn(fresh);
                    memory::Hub::get_pull()->dealloc(fresh);
                }
            }
        }
    } while (received > 0);
}

// 0x00459FD0
void Guest::transmit(Conn* conn)
{
    if (!transmit_due_now(this, conn))
        return;
    bool done = false;
    do {
        if (!outgoing_backlog(this, conn))
            return;
        if (net::over_bandwidth_budget(this, conn))
            return;
        if (timestamp_due(this, conn)) {
            NetMessageInfo desc;
            construct_msg_desc(&desc);
            auto timestamp = static_cast<uint16_t>(conn->timestamp_out_);
            desc.content_ = &timestamp;
            desc.byte_count_ = sizeof(timestamp);
            desc.ident_ = kMsgClockStamp;
            desc.rank_ = kTopRank;
            net::put_on_outbox(this, conn->hdl_, &desc);
        }
        if (is_off_machine(conn) && !is_outsider(conn) && (flag_bits_ & kAppMeasureLatency)) {
            NetMessageInfo desc;
            construct_msg_desc(&desc);
            uint32_t& sent = conn->ping_probe_.probe_sent_at_;
            uint32_t now = get_timestamp(this);
            if (sent == 0 || now - sent > max_latency()) {
                // a test due: if the last went unanswered, the latency is at least that much more
                if (sent > conn->ping_probe_.echo_seen_at_) {
                    uint32_t latency = (net::is_host_local(conn) ? 0 : static_cast<uint32_t>(conn->ping_probe_.average_latency_)) + 100;
                    if (latency > max_latency())
                        latency = max_latency();
                    add_latency(conn, latency);
                }
                sent = now;
                desc.content_ = &now;
                desc.ident_ = kMsgLatencyTest;
                desc.byte_count_ = sizeof(now);
                net::put_on_outbox(this, conn->hdl_, &desc);
            }
        }
        if (!bundle_messages(conn, kQueueNormal, false))
            done = true;
        if (!bundle_messages(conn, kLaneOrdered, true))
            done = true;
        if (!bundle_messages(conn, kLanePriority, true))
            done = true;
        if (!bundle_messages(conn, kLaneOrdered, false))
            done = true;
        if (!bundle_messages(conn, kLanePriority, false))
            done = true;
        char*& write_ptr = conn->write_ptr_;
        int size = static_cast<int>(write_ptr - conn->outgoing_bytes_);
        if (size > 0 && is_off_machine(conn)) {
            char* packet = conn->outgoing_bytes_;
            bool sent = linked_up_
                            ? net::post_out(this, packet, size, 0)
                            : net::transmit_to(this, conn->remote_ip_, conn->port_no_,
                                     packet, size, 0);
            if (!sent) {
                mark_messages_for_retry(conn, conn->timestamp_out_);
                write_ptr = conn->outgoing_bytes_;
                return;
            }
            outbound_byte_total_ += size;
            add_packet_metric(&conn->send_metrics_, size + 0x1C, get_timestamp(this));
            conn->last_transmit_at_ = get_timestamp(this);
            force_send_now(conn, false);
            write_ptr = conn->outgoing_bytes_;
        }
        ++conn->timestamp_out_;
    } while (!done);
}

// 0x0045ABC0
int Guest::on_link_accepted_msg(InboundMsgCtx* context)
{
    context->acknowledge_ = true;
    return kResultPassOn;
}

// 0x0045ABD0
int Guest::handle_connection_refused(InboundMsgCtx* context)
{
    net::sever_link(context->receiver_app_, context->sender_conn_);
    return kResultStop;
}

// 0x0045AA70
int Guest::on_clock_sync_msg(InboundMsgCtx* context)
{
    context->body_size_ = sizeof(uint16_t);
    if (context->wire_options_ & kPacketForeign)
        return kResultConsumed;
    auto* stamp = reinterpret_cast<uint16_t*>(context->body_);
    // (no bounds check: more than 32 timestamps in a packet would overrun the list)
    context->sequences_to_ack_[context->num_sequences_to_ack_] = *stamp;
    ++context->num_sequences_to_ack_;
    Conn* conn = context->sender_conn_;
    uint32_t& newest = conn->last_timestamp_in_;
    if (newest == 0) {
        newest = *stamp;
        return kResultPassOn;
    }
    uint32_t timestamp = *stamp;
    bool late;
    if (timestamp < newest) {
        late = newest - timestamp <= kTimestampWindow; // (else it has wrapped around: newer)
    } else if (timestamp == newest) {
        ++context->receiver_app_->repeat_arrivals_;
        return kResultFailed;
    } else {
        uint32_t gap = timestamp - newest;
        if (gap > 1 && gap <= kTimestampWindow) {
            context->receiver_app_->skipped_stamps_ += gap - 1;
            context->sender_conn_->last_timestamp_in_ = *stamp;
            return kResultPassOn;
        }
        late = gap > kTimestampWindow; // (so far ahead it has wrapped around: older)
    }
    if (late) {
        // it was counted as lost when a newer one came
        ++context->receiver_app_->stale_arrivals_;
        --context->receiver_app_->skipped_stamps_;
        context->wire_options_ |= kPacketLate;
        return kResultPassOn;
    }
    newest = timestamp;
    return kResultPassOn;
}

} // namespace Net
