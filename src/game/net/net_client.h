#pragma once

// The network library's client (Net::Client: an App with a single connection, the server). vtable 0x006475CC:
// the deleting destructor 0x0045B8E0, 0x00459690, ReceiveData, SendData, Init, Deinit (0x0045B820).

#include <cstdint>

#include "game/net/net_app_messages.h"

namespace Net {

struct MsgDesc;

class Client : public App {
public:
    // 0x0045A250: the App (0x004594C0, from `params`), then the App's task (Tsk::Task, priority -2000).
    Client* Construct(void* params);
    // 0x0045B8E0 (scalar deleting destructor; Server's vtable has it too)
    Client* Destroy(unsigned int flags);
    // 0x0045B910: the destructor (a jump to App's, 0x00457710; Server's too).
    void DestroyBody();
    // 0x0045B820: vtable slot 5 (a jump to App's, 0x00457490; Server's too).
    void Deinit();

    // 0x00459EC0: the App's Init, then handlers for "connection accepted", "refused", "terminated" and
    // timestamps.
    bool Init();
    // 0x00459F40: connect() the socket to the server (retried while it would block). With a non-zero address the
    // socket is "connected" from then on (send/recv); false and the error reported if connect fails.
    bool ConnectToServer(uint32_t ip, uint16_t port);
    // 0x00459E50: a message queued for the server (the first connection).
    void EnqueueMessageToServer(MsgDesc* desc);
    // 0x00459DC0: a message streamed to the server (App::StreamMessage, 0x00458740, on the first connection).
    void StreamMessageToServer(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                               uint32_t a7);

    // 0x0045A300: everything waiting on the socket, into the server connection's read buffer (or, from an unknown
    // address while not connected, into a new connection).
    void ReceiveData();
    // 0x00459FD0: packets to the server until its queues are empty or a packet is full: a timestamp when due, a
    // latency test, then the queues.
    void SendData(Conn* conn);

    // Handlers.
    // 0x0045ABC0 (id 4): the packet's messages will be acked.
    static int __cdecl HandleConnectionAccepted(MsgHandlerContext* context);
    // 0x0045ABD0 (ids 5, 6): the connection is terminated; the packet stops.
    static int __cdecl HandleConnectionRefused(MsgHandlerContext* context);
    // 0x0045AA70 (id 9): the server's timestamp, acked; lost, late and duplicate packets counted from it.
    static int __cdecl HandleTimestamp(MsgHandlerContext* context);
};

} // namespace Net
