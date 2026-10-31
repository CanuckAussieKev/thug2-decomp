#pragma once

// The network library's client (NetLink::Guest: an App with a single connection, the server). vtable 0x006475CC:
// the deleting destructor 0x0045B8E0, 0x00459690, pull_inbound, Transmit, Init, Deinit (0x0045B820).

#include <cstdint>

#include "engine/net/net_app_messages.h"

namespace net {

struct NetMessageInfo;

class Guest : public AppHost {
public:
    // 0x0045A250: the App (0x004594C0, with `flags`), then the App's task (tasks::Task, priority -2000).
    Guest* make_new(int flags);
    // 0x0045B8E0 (scalar deleting destructor; Server's vtable has it too)
    Guest* teardown(unsigned int flags);
    // 0x0045B910: the destructor (a jump to App's, 0x00457710; Server's too).
    void destroy_body();
    // 0x0045B820: vtable slot 5 (a jump to App's, 0x00457490; Server's too).
    void tear_down_state();

    // 0x00459EC0: the App's Init, then handlers for "connection accepted", "refused", "terminated" and
    // timestamps.
    bool prepare_all();
    // 0x00459F40: connect() the socket to the server (retried while it would block). With a non-zero address the
    // socket is "connected" from then on (send/recv); false and the error reported if connect fails.
    bool dial_server(uint32_t ip, uint16_t port);
    // 0x00459E50: a message queued for the server (the first connection).
    void send_to_host(NetMessageInfo* desc);
    // 0x00459DC0: a message streamed to the server (AppHost::send_bulk_to_conn, 0x00458740, on the first
    // connection).
    void send_bulk_to_host(uint8_t msg_id, uint16_t size, void* data, const char* desc, uint8_t group_id,
                               bool queue_all_pieces, bool borrow_caller_buffer);

    // 0x0045A300: everything waiting on the socket, into the server connection's read buffer (or, from an unknown
    // address while not connected, into a new connection).
    void pull_inbound();
    // 0x00459FD0: packets to the server until its queues are empty or a packet is full: a timestamp when due, a
    // latency test, then the queues.
    void transmit(Conn* conn);

    // Handlers.
    // 0x0045ABC0 (id 4): the packet's messages will be acked.
    static int __cdecl on_link_accepted_msg(InboundMsgCtx* context);
    // 0x0045ABD0 (ids 5, 6): the connection is terminated; the packet stops.
    static int __cdecl handle_connection_refused(InboundMsgCtx* context);
    // 0x0045AA70 (id 9): the server's timestamp, acked; lost, late and duplicate packets counted from it.
    static int __cdecl on_clock_sync_msg(InboundMsgCtx* context);

    uint8_t host_sock_addr_[0x10]; // +0x1F08 sockaddr_in (dial_server; pull_inbound looks the server up by it)
};
static_assert(sizeof(Guest) == 0x1F18, "allocated as 0x1F18 bytes (0x00455460)");

} // namespace Net
