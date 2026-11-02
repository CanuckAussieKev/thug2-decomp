// The network library's client (NetLink::Guest). See net_client.h.

#include "engine/net/net_client.h"

#include <winsock2.h>

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "platform/stopwatch.h"
#include "engine/core/checked_index.h"
#include "wire/net_wire.h"
#include "platform/sockets.h"

namespace net {

namespace {

constexpr int kAppTaskPriority = -2000;

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
    app->send_task_ = std::make_unique<tasks::Task<AppHost>>(&AppHost::flush_outgoing_traffic, app, kAppTaskPriority);
}

// 0x0045A250
Guest::Guest(int flags) : AppHost(flags) { construct_app_task(this); }

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
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = ip;
    std::memset(std::data(address.sin_zero), 0, sizeof(address.sin_zero));
    std::memcpy(host_sock_addr_.data(), &address, sizeof(address));
    while (sockets::connect_to(sock_, address) == SOCKET_ERROR) {
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
    if (const Conn* server = server_connection(this))
        net::put_on_outbox(this, server->hdl_, desc);
}

// 0x0045A300
void Guest::pull_inbound()
{
    int from_length = sizeof(sockaddr_in);
    sockaddr_in from = {}; // (the original leaves it uninitialised: recv doesn't fill it in)
    if (has_no_socket(this))
        return;
    int received = 0;
    for (bool again = true; again; again = received > 0) {
        uint8_t* buffer = receive_buffer_.data();
        const SOCKET socket = sock_;
        if (!linked_up_)
            received =
                sockets::receive_bytes_from(socket, buffer, kReceiveBufferSize, 0, &from, &from_length);
        else
            received = sockets::receive_bytes(socket, buffer, kReceiveBufferSize, 0);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        inbound_byte_total_ += received;
        const int size = (flag_bits_ & kAppSecure) ? received - 2 : received;
        sockaddr_in server{};
        std::memcpy(&server, host_sock_addr_.data(), sizeof(server));
        if (Conn* conn = net::find_conn_at_address(this, server.sin_addr.s_addr, ntohs(server.sin_port))) {
            if (static_cast<int>(conn->incoming_.used) + size < static_cast<int>(kConnReceiveBufferSize)) {
                const uint32_t time = frame_clock::get_time();
                if (pull_datagram(this, std::span<const uint8_t>(receive_buffer_).first(static_cast<size_t>(received)),
                                  conn->incoming_.vacant())) {
                    conn->incoming_.used += static_cast<size_t>(size);
                    add_packet_metric(&conn->receive_metrics_, received + 0x1C, get_timestamp(this));
                    if (time > conn->last_heard_from_)
                        conn->last_heard_from_ = time;
                } else if (auto handler = unknown_packet_sink_) {
                    handler(buffer, received, sockets::generic(&from));
                }
            }
        } else if (!linked_up_) {
            // from an unknown address
            if (Conn* fresh = net::opened_link(this, from.sin_addr.s_addr, ntohs(from.sin_port), 6)) {
                fresh->owner_app_ = this;
                if (pull_datagram(this, std::span<const uint8_t>(receive_buffer_).first(static_cast<size_t>(received)),
                                  fresh->incoming_.vacant())) {
                    fresh->incoming_.used = static_cast<size_t>(size);
                    mark_new_connection(fresh);
                } else {
                    if (auto handler = unknown_packet_sink_)
                        handler(buffer, received, sockets::generic(&from));
                    destroy_conn(fresh);
                    memory::Hub::get_pull()->dealloc(fresh);
                }
            }
        }
    }
}

// 0x00459FD0
void Guest::flush_outbox(Conn* conn)
{
    if (!transmit_due_now(this, conn))
        return;
    bool done = false;
    for (bool again = true; again; again = !done) {
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
                    uint32_t latency =
                        (net::is_host_local(conn) ? 0 : static_cast<uint32_t>(conn->ping_probe_.average_latency_)) +
                        100;
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
        const int size = static_cast<int>(conn->outgoing_.used);
        if (size > 0 && is_off_machine(conn)) {
            const uint8_t* packet = conn->outgoing_.bytes.data();
            const bool sent = linked_up_ ? net::post_out(this, packet, size, 0)
                                         : net::transmit_to(this, conn->remote_ip_, conn->port_no_, packet, size, 0);
            if (!sent) {
                mark_messages_for_retry(conn, conn->timestamp_out_);
                conn->outgoing_.used = 0;
                return;
            }
            outbound_byte_total_ += size;
            add_packet_metric(&conn->send_metrics_, size + 0x1C, get_timestamp(this));
            conn->last_transmit_at_ = get_timestamp(this);
            force_send_now(conn, false);
            conn->outgoing_.used = 0;
        }
        ++conn->timestamp_out_;
    }
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
    const auto stamp = net::wire::value_at<uint16_t>(context->body_);
    // (no bounds check: more than 32 timestamps in a packet would overrun the list)
    core::at(context->sequences_to_ack_, context->num_sequences_to_ack_) = stamp;
    ++context->num_sequences_to_ack_;
    Conn* conn = context->sender_conn_;
    uint32_t& newest = conn->last_timestamp_in_;
    if (newest == 0) {
        newest = stamp;
        return kResultPassOn;
    }
    const uint32_t timestamp = stamp;
    bool late = false;
    if (timestamp < newest) {
        late = newest - timestamp <= kTimestampWindow; // (else it has wrapped around: newer)
    } else if (timestamp == newest) {
        ++context->receiver_app_->repeat_arrivals_;
        return kResultFailed;
    } else {
        const uint32_t gap = timestamp - newest;
        if (gap > 1 && gap <= kTimestampWindow) {
            context->receiver_app_->skipped_stamps_ += static_cast<int>(gap - 1);
            context->sender_conn_->last_timestamp_in_ = stamp;
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

} // namespace net
