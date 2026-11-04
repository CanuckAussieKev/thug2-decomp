// The network library's server (NetLink::HostEnd). See net_server.h.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "engine/net/net_server.h"

#include <winsock2.h>

#include "wire/message_ids.h"
#include "engine/memory/manager.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/sys/stopwatch.h"
#include "engine/net/net_client.h"
#include "base/core/checked_index.h"
#include "wire/net_wire.h"
#include "platform/sockets.h"

namespace net {


namespace {
constexpr uint32_t kLatencyPadding = 100; // ms

constexpr int kBandwidthStep = 500; // less when messages pile up
constexpr int kMinBandwidth = 0x530;
constexpr int kIdleBandwidthStep = 1000; // more when nothing waits for an ack

uint32_t& get_timestamp(AppHost* app) { return app->clock(); }
Router* get_dispatcher(AppHost* app) { return &app->dispatcher(); }

} // namespace

// 0x0045B830
HostEnd::HostEnd(int flags) : AppHost(flags) { construct_app_task(this); }

// 0x0045B7A0
bool HostEnd::prepare_all()
{
    get_timestamp(this) = 0;
    set_linked_up(false);
    set_peer_cap(0);
    if (!AppHost::prepare_all()) {
        return false;
    }
    Router* dispatcher = get_dispatcher(this);
    dispatcher->append_listener(wire::MessageId::kConnectionRequest, on_link_ask_msg, 3, nullptr, kTopRank);
    dispatcher->append_listener(wire::MessageId::kClockStamp, on_clock_sync_msg, 3, nullptr, kListenerPriority);
    dispatcher->append_listener(wire::MessageId::kDisconnRequest, on_link_drop_ask_msg, 2, nullptr, kTopRank);
    return true;
}

// 0x0045BBD0
void HostEnd::pull_inbound()
{
    int from_length = sizeof(sockaddr_in);
    sockaddr_in from{};
    if (has_no_socket(this)) {
        return;
    }
    int received = 0;
    for (bool again = true; again; again = received > 0) {
        uint8_t* buffer = receive_buffer().data();
        received = sockets::receive_bytes_from(sock(), buffer, kReceiveBufferSize, 0, &from, &from_length);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        inbound_byte_total() += received;
        const int size = ((flag_bits() & kAppSecure) != 0U) ? received - 2 : received;
        if (Conn* conn = net::find_conn_at_address(this, from.sin_addr.s_addr, ntohs(from.sin_port))) {
            if ((is_new(conn) || ((flag_bits() & kAppSecure) == 0U)) &&
                static_cast<int>(conn->incoming().used) + size < static_cast<int>(kConnReceiveBufferSize)) {
                if (pull_datagram(this, std::span<const uint8_t>(receive_buffer()).first(static_cast<size_t>(received)),
                                  vacant(conn->incoming()))) {
                    conn->incoming().used += static_cast<size_t>(size);
                    add_packet_metric(&conn->receive_metrics(), received + kUdpOverheadBytes, get_timestamp(this));
                    conn->set_last_heard_from(frame_clock::get_time());
                } else if (auto handler = unknown_packet_sink()) {
                    // (given the connection's read pointer, not the packet - as in the original)
                    handler(vacant(conn->incoming()).data(), received, sockets::generic(&from));
                }
            }
        } else if (Conn* fresh = net::opened_link(this, from.sin_addr.s_addr, ntohs(from.sin_port), (Conn::kFlagRemote | Conn::kFlagForeign))) {
            fresh->set_owner_app(this);
            if (pull_datagram(this, std::span<const uint8_t>(receive_buffer()).first(static_cast<size_t>(received)),
                              vacant(fresh->incoming()))) {
                fresh->incoming().used = static_cast<size_t>(size);
                mark_new_connection(fresh);
            } else {
                if (auto handler = unknown_packet_sink()) {
                    handler(buffer, received, sockets::generic(&from));
                }
                destroy_conn(fresh);
                memory::Hub::dealloc(fresh);
            }
        }
    }
}

// 0x0045B920
void HostEnd::flush_outbox(Conn* conn)
{
    NetMessageInfo timestamp_desc;
    construct_msg_desc(&timestamp_desc);
    if (!transmit_due_now(this, conn)) {
        return;
    }
    bool done = false;
    for (bool again = true; again; again = !done) {
        if (!outgoing_backlog(this, conn) || net::over_bandwidth_budget(this, conn)) {
            break;
        }
        if (is_off_machine(conn) && !is_outsider(conn) && ((flag_bits() & kAppMeasureLatency) != 0U)) {
            NetMessageInfo desc;
            construct_msg_desc(&desc);
            uint32_t& sent = conn->ping_probe().probe_sent_at();
            uint32_t now = get_timestamp(this);
            if (sent == 0 || now - sent > max_latency()) {
                // a test due: if the last went unanswered, the latency is at least that much more
                if (sent > conn->ping_probe().echo_seen_at()) {
                    uint32_t latency =
                        (net::is_host_local(conn) ? 0 : static_cast<uint32_t>(conn->ping_probe().average_latency())) +
                        kLatencyPadding;
                    latency = std::min(latency, max_latency());
                    add_latency(conn, latency);
                }
                sent = now;
                desc.set_content(&now);
                desc.set_byte_count(sizeof(now));
                desc.set_ident(wire::MessageId::kLatencyTest);
                net::put_on_outbox(this, conn->hdl(), &desc);
            }
        }
        // every packet carries a timestamp
        auto timestamp = static_cast<uint16_t>(conn->timestamp_out());
        timestamp_desc.set_content(&timestamp);
        timestamp_desc.set_byte_count(sizeof(timestamp));
        timestamp_desc.set_ident(wire::MessageId::kClockStamp);
        timestamp_desc.set_rank(kTopRank);
        net::put_on_outbox(this, conn->hdl(), &timestamp_desc);

        const int pending_before = get_num_pending_messages(conn);
        if (!bundle_messages(conn, kQueueNormal, false)) {
            done = true;
        }
        if (!bundle_messages(conn, kLaneOrdered, true)) {
            done = true;
        }
        if (!bundle_messages(conn, kLanePriority, true)) {
            done = true;
        }
        if (!bundle_messages(conn, kLaneOrdered, false)) {
            done = true;
        }
        if (!bundle_messages(conn, kLanePriority, false)) {
            done = true;
        }
        if (get_num_pending_messages(conn) > pending_before && ((conn->cur_standing() & kConnStatus1) != 0U)) {
            // more waiting for acks: take it as high latency and lower the bandwidth
            add_latency(conn, max_latency());
            change_throughput(conn, fetch_throughput(conn) - kBandwidthStep);
            if (fetch_throughput(conn) < kMinBandwidth) {
                change_throughput(conn, kMinBandwidth);
            }
        }

        const int size = static_cast<int>(conn->outgoing().used);
        if (size > 0 && is_off_machine(conn)) {
            if (!net::transmit_to(this, conn->remote_ip(), conn->port_no(), conn->outgoing().bytes.data(), size, 0)) {
                mark_messages_for_retry(conn, conn->timestamp_out());
                conn->outgoing().used = 0;
                break;
            }
            outbound_byte_total() += size;
            add_packet_metric(&conn->send_metrics(), size + kUdpOverheadBytes, get_timestamp(this));
            force_send_now(conn, false);
            conn->set_last_transmit_at(get_timestamp(this));
            conn->outgoing().used = 0;
        }
        ++conn->timestamp_out();
    }

    if (net::over_bandwidth_budget(this, conn) && get_num_pending_messages(conn) == 0) {
        change_throughput(conn, fetch_throughput(conn) + kIdleBandwidthStep); // limited, though nothing is waiting
    }
}

// 0x0045ADB0
int HostEnd::on_link_ask_msg(InboundMsgCtx* context)
{
    NetMessageInfo desc;
    construct_msg_desc(&desc);
    if ((context->wire_options() & kPacketForeign) != 0U) {
        if (!net::allows_outside_peers(context->receiver_app())) {
            return kResultConsumed;
        }
        const Conn* conn = context->sender_conn();
        net::deliver_to(context->receiver_app(), wire::MessageId::kConnectionAccepted, 0, nullptr, conn->remote_ip(), conn->port_no(),
                        0);
        return kResultPassOn;
    }
    if (!net::is_host_local(context->sender_conn())) {
        return kResultConsumed;
    }
    desc.set_ident(wire::MessageId::kConnectionAccepted);
    desc.set_rank(kTopRank);
    desc.set_queue(kLanePriority);
    net::put_on_outbox(context->receiver_app(), context->sender_conn()->hdl(), &desc);
    return kResultPassOn;
}

// 0x0045AE50
int HostEnd::on_link_drop_ask_msg(InboundMsgCtx* context)
{
    NetMessageInfo desc;
    construct_msg_desc(&desc);
    const Conn* conn = context->sender_conn();
    if ((conn->cur_standing() & kConnDisconnAccepted) != 0U) {
        return kResultConsumed;
    }
    desc.set_ident(wire::MessageId::kDisconnAccepted);
    net::put_on_outbox(context->receiver_app(), conn->hdl(), &desc);
    context->sender_conn()->cur_standing() |= kConnDisconnAccepted;
    return kResultPassOn;
}

// 0x0045AA30
int HostEnd::on_clock_sync_msg(InboundMsgCtx* context)
{
    context->set_body_size(sizeof(uint16_t));
    if ((context->wire_options() & kPacketForeign) != 0U) {
        return kResultConsumed;
    }
    // (no bounds check: more than 32 timestamps in a packet would overrun the list)
    core::at(context->sequences_to_ack(), context->num_sequences_to_ack()) = net::wire::value_at<uint16_t>(context->body());
    ++context->num_sequences_to_ack();
    return kResultPassOn;
}

} // namespace net
