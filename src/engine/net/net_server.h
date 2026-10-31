#pragma once

// The network library's server (NetLink::HostEnd: an App with a connection per client). vtable 0x006475EC: the
// deleting destructor (Client's, 0x0045B8E0), 0x00459690, pull_inbound, Transmit, Init, Deinit (0x0045B820).

#include <cstdint>

#include "engine/net/net_app_messages.h"

namespace net {

class HostEnd final : public AppHost {
public:
    // 0x0045B830: the App (0x004594C0, with `flags`), then the App's task (tasks::Task, priority -2000).
    explicit HostEnd(int flags);

    // 0x0045B7A0: the App's Init, then handlers for connection requests, timestamps and disconnection requests.
    bool prepare_all() override;

    // 0x0045BBD0: everything waiting on the socket, each packet into its sender's connection (a new one for an
    // unknown address).
    void pull_inbound() override;
    // 0x0045B920: packets to one client until its queues are empty or a packet is full: a timestamp in each, a
    // latency test when due, then the queues; the bandwidth goes down when messages wait for acks.
    void flush_outbox(Conn* conn) override;

    // Handlers.
    // 0x0045ADB0 (id 3): a connection request: accepted (id 4, important) for a local connection, or for a
    // foreign one when the App takes those; else ignored.
    static int __cdecl on_link_ask_msg(InboundMsgCtx* context);
    // 0x0045AE50 (id 0xB): a disconnection request: accepted (id 0xC) once.
    static int __cdecl on_link_drop_ask_msg(InboundMsgCtx* context);
    // 0x0045AA30 (id 9): a client's timestamp, acked.
    static int __cdecl on_clock_sync_msg(InboundMsgCtx* context);

    uint8_t unused1f08_[0x400]; // +0x1F08 (allocated, never used)
};
static_assert(sizeof(HostEnd) == 0x2308, "allocated as 0x2308 bytes (0x00455270)");

} // namespace net
