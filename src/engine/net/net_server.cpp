// The network library's server (NetLink::HostEnd). See net_server.h.

#include <cstdint>

#include "engine/net/net_server.h"

#include <winsock2.h>

#include "engine/core/manager.h"
#include "engine/net/net_access_c.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/net/net_client.h"

namespace net {

void construct_app_task(AppHost* app); // net_client.cpp

namespace {

static VtableSlot g_server_vtable_slots[] = {&net::Guest::teardown,       &net::AppHost::handle_incoming,
                                             &net::HostEnd::pull_inbound, &net::HostEnd::transmit,
                                             &net::HostEnd::prepare_all,  &net::Guest::tear_down_state};
void** const kServerVtable = reinterpret_cast<void**>(g_server_vtable_slots);
constexpr int kBandwidthStep = 500; // less when messages pile up
constexpr int kMinBandwidth = 0x530;
constexpr int kIdleBandwidthStep = 1000; // more when nothing waits for an ack

uint32_t& get_timestamp(AppHost* app) { return app->clock_; }
Router* get_dispatcher(AppHost* app) { return &app->dispatcher_; }

} // namespace

// 0x0045B830
HostEnd* HostEnd::make_new(int flags)
{
    AppHost::make_new(flags);
    prop<void**>(this, 0) = kServerVtable;
    construct_app_task(this);
    return this;
}

// 0x0045B7A0
bool HostEnd::prepare_all()
{
    get_timestamp(this) = 0;
    linked_up_ = false;
    peer_cap_ = 0;
    if (!AppHost::prepare_all())
        return false;
    Router* dispatcher = get_dispatcher(this);
    dispatcher->append_listener(kMsgConnectionRequest, on_link_ask_msg, 3, nullptr, kTopRank);
    dispatcher->append_listener(kMsgClockStamp, on_clock_sync_msg, 3, nullptr, 0x80);
    dispatcher->append_listener(kMsgDisconnRequest, on_link_drop_ask_msg, 2, nullptr, kTopRank);
    return true;
}

// 0x0045BBD0
void HostEnd::pull_inbound()
{
    int from_length = sizeof(sockaddr_in);
    sockaddr_in from;
    if (has_no_socket(this))
        return;
    int received;
    do {
        char* buffer = receive_buffer_;
        received = recvfrom(sock_, buffer, kReceiveBufferSize, 0, reinterpret_cast<sockaddr*>(&from), &from_length);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        inbound_byte_total_ += received;
        int size = (flag_bits_ & kAppSecure) ? received - 2 : received;
        if (Conn* conn = net::find_conn_at_address(this, from.sin_addr.s_addr, ntohs(from.sin_port))) {
            char*& read_ptr = conn->read_ptr_;
            if ((is_new(conn) || !(flag_bits_ & kAppSecure)) &&
                read_ptr + size < conn->read_buffer_ + kConnReceiveBufferSize) {
                if (pull_datagram(this, buffer, read_ptr, received)) {
                    read_ptr += size;
                    add_packet_metric(&conn->receive_metrics_, received + 0x1C, get_timestamp(this));
                    conn->last_heard_from_ = frame_clock::get_time();
                } else if (auto handler = unknown_packet_sink_) {
                    // (given the connection's read pointer, not the packet - as in the original)
                    handler(read_ptr, received, reinterpret_cast<sockaddr*>(&from));
                }
            }
        } else if (Conn* fresh = net::opened_link(this, from.sin_addr.s_addr, ntohs(from.sin_port), 6)) {
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
    } while (received > 0);
}

// 0x0045B920
void HostEnd::transmit(Conn* conn)
{
    NetMessageInfo timestamp_desc;
    construct_msg_desc(&timestamp_desc);
    if (!transmit_due_now(this, conn))
        return;
    bool done = false;
    do {
        if (!outgoing_backlog(this, conn) || net::over_bandwidth_budget(this, conn))
            break;
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
                desc.byte_count_ = sizeof(now);
                desc.ident_ = kMsgLatencyTest;
                net::put_on_outbox(this, conn->hdl_, &desc);
            }
        }
        // every packet carries a timestamp
        auto timestamp = static_cast<uint16_t>(conn->timestamp_out_);
        timestamp_desc.content_ = &timestamp;
        timestamp_desc.byte_count_ = sizeof(timestamp);
        timestamp_desc.ident_ = kMsgClockStamp;
        timestamp_desc.rank_ = kTopRank;
        net::put_on_outbox(this, conn->hdl_, &timestamp_desc);

        int pending_before = get_num_pending_messages(conn);
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
        if (get_num_pending_messages(conn) > pending_before && (conn->cur_standing_ & kConnStatus1)) {
            // more waiting for acks: take it as high latency and lower the bandwidth
            add_latency(conn, max_latency());
            change_throughput(conn, fetch_throughput(conn) - kBandwidthStep);
            if (fetch_throughput(conn) < kMinBandwidth)
                change_throughput(conn, kMinBandwidth);
        }

        char*& write_ptr = conn->write_ptr_;
        int size = static_cast<int>(write_ptr - conn->outgoing_bytes_);
        if (size > 0 && is_off_machine(conn)) {
            if (!net::transmit_to(this, conn->remote_ip_, conn->port_no_, conn->outgoing_bytes_, size, 0)) {
                mark_messages_for_retry(conn, conn->timestamp_out_);
                write_ptr = conn->outgoing_bytes_;
                break;
            }
            outbound_byte_total_ += size;
            add_packet_metric(&conn->send_metrics_, size + 0x1C, get_timestamp(this));
            force_send_now(conn, false);
            conn->last_transmit_at_ = get_timestamp(this);
            write_ptr = conn->outgoing_bytes_;
        }
        ++conn->timestamp_out_;
    } while (!done);

    if (net::over_bandwidth_budget(this, conn) && get_num_pending_messages(conn) == 0)
        change_throughput(conn, fetch_throughput(conn) + kIdleBandwidthStep); // limited, though nothing is waiting
}

// 0x0045ADB0
int HostEnd::on_link_ask_msg(InboundMsgCtx* context)
{
    NetMessageInfo desc;
    construct_msg_desc(&desc);
    if (context->wire_options_ & kPacketForeign) {
        if (!net::allows_outside_peers(context->receiver_app_))
            return kResultConsumed;
        Conn* conn = context->sender_conn_;
        net::deliver_to(context->receiver_app_, kMsgConnectionAccepted, 0, nullptr, conn->remote_ip_, conn->port_no_,
                        0);
        return kResultPassOn;
    }
    if (!net::is_host_local(context->sender_conn_))
        return kResultConsumed;
    desc.ident_ = kMsgConnectionAccepted;
    desc.rank_ = kTopRank;
    desc.queue_ = kLanePriority;
    net::put_on_outbox(context->receiver_app_, context->sender_conn_->hdl_, &desc);
    return kResultPassOn;
}

// 0x0045AE50
int HostEnd::on_link_drop_ask_msg(InboundMsgCtx* context)
{
    NetMessageInfo desc;
    construct_msg_desc(&desc);
    Conn* conn = context->sender_conn_;
    if (conn->cur_standing_ & kConnDisconnAccepted)
        return kResultConsumed;
    desc.ident_ = kMsgDisconnAccepted;
    net::put_on_outbox(context->receiver_app_, conn->hdl_, &desc);
    context->sender_conn_->cur_standing_ |= kConnDisconnAccepted;
    return kResultPassOn;
}

// 0x0045AA30
int HostEnd::on_clock_sync_msg(InboundMsgCtx* context)
{
    context->body_size_ = sizeof(uint16_t);
    if (context->wire_options_ & kPacketForeign)
        return kResultConsumed;
    // (no bounds check: more than 32 timestamps in a packet would overrun the list)
    context->sequences_to_ack_[context->num_sequences_to_ack_] = *reinterpret_cast<uint16_t*>(context->body_);
    ++context->num_sequences_to_ack_;
    return kResultPassOn;
}

} // namespace net
