#pragma once

// The App's (Net::App's) message handling in 0x4598C0-0x45BDA0: bundling queued messages into a packet, and the
// handlers App::Init registers for the library's own messages. App's constructor (0x004594C0) is wt/netB's: here
// App is wt/netA's AppView (its fields and methods) plus these; fields AppView doesn't name are reached by offset
// (net_access_c.h).

#include <cstdint>

#include "game/net/net_app_view.h"
#include "game/net/net_dispatcher.h"

namespace Net {

class App : public AppView {
public:
    // 0x004598C0: the messages of one queue (0 normal, 1 important, 2 sequenced) that are due go into the
    // connection's packet; false if one didn't fit. Normal messages are dequeued as they go; important and
    // sequenced ones stay until acked (`resendsOnly`: only those sent before whose ack is overdue).
    bool BundleMessages(Conn* conn, int queue, bool resendsOnly);

    // Handlers (registered by App::Init, 0x00457110).
    // 0x0045AD60 (id 1): answers a latency test with its time.
    static int __cdecl HandleLatencyTest(MsgHandlerContext* context);
    // 0x0045AB60 (id 2): the answer to our latency test: the round trip goes into the latency average.
    static int __cdecl HandleLatencyResponse(MsgHandlerContext* context);
    // 0x0045AF10 (id 7): a sequenced message, buffered in its group's list by sequence number (to be handled
    // in order); acks are sent for the packet.
    static int __cdecl HandleSequencedMessages(MsgHandlerContext* context);
    // 0x0045B0B0 (id 8): an ack: the important and sequenced messages sent with that timestamp are done; the
    // round trip is a latency measurement.
    static int __cdecl HandleAck(MsgHandlerContext* context);
    // 0x0045ABF0 (id 0x12): a compressed message (method 1 or 2), expanded and dispatched.
    static int __cdecl HandleCompressedMessages(MsgHandlerContext* context);
    // 0x0045B2F0 (ids 0xE, 0xF): a stream's start and its pieces; the whole message is dispatched when complete.
    static int __cdecl HandleStreamMessages(MsgHandlerContext* context);
};

} // namespace Net
