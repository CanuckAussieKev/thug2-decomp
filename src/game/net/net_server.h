#pragma once

// The network library's server (Net::Server: an App with a connection per client). vtable 0x006475EC: the
// deleting destructor (Client's, 0x0045B8E0), 0x00459690, ReceiveData, SendData, Init, Deinit (0x0045B820).

#include <cstdint>

#include "game/net/net_app_messages.h"

namespace Net {

class Server : public App {
public:
    // 0x0045B830: the App (0x004594C0, from `params`), then the App's task (Tsk::Task, priority -2000).
    Server* Construct(void* params);

    // 0x0045B7A0: the App's Init, then handlers for connection requests, timestamps and disconnection requests.
    bool Init();

    // 0x0045BBD0: everything waiting on the socket, each packet into its sender's connection (a new one for an
    // unknown address).
    void ReceiveData();
    // 0x0045B920: packets to one client until its queues are empty or a packet is full: a timestamp in each, a
    // latency test when due, then the queues; the bandwidth goes down when messages wait for acks.
    void SendData(Conn* conn);

    // Handlers.
    // 0x0045ADB0 (id 3): a connection request: accepted (id 4, important) for a local connection, or for a
    // foreign one when the App takes those; else ignored.
    static int __cdecl HandleConnectionRequest(MsgHandlerContext* context);
    // 0x0045AE50 (id 0xB): a disconnection request: accepted (id 0xC) once.
    static int __cdecl HandleDisconnRequest(MsgHandlerContext* context);
    // 0x0045AA30 (id 9): a client's timestamp, acked.
    static int __cdecl HandleTimestamp(MsgHandlerContext* context);
};

} // namespace Net
