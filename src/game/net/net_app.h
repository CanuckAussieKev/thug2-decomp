#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/net/net_app_view.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_lists.h"

// Neversoft's network library (Net): an App (the base of the Server and the Client) owns its connections (Conn),
// queues messages for them and streams big messages in pieces. Here: App's constructor and the App code at
// 0x00457EB0 - 0x004598C0. App's fields and its methods below 0x00457EB0 are AppView's (net_app_view.h); App adds
// these methods to it (no fields of its own: the merge of the two parts).

namespace Net {

// A message to be sent (0x20 bytes; the constructor 0x004549F0 sets every field: zero, priority 0x80).
struct MsgDesc {
    enum Queue : int { kQueueDefault = 0, kQueueImportant = 1, kQueueSequenced = 2 };

    // 0x004549F0 (net_descs.cpp)
    MsgDesc* Construct();

    uint8_t m_id;               // 0x00
    uint8_t m_stream;           // 0x01 a piece of a streamed message (counted by Conn 0x004565E0)
    uint16_t m_length;          // 0x02
    void* m_data;               // 0x04
    int m_priority;             // 0x08 (0x80 by default)
    int m_queue;                // 0x0C
    uint8_t m_groupId;          // 0x10 sequenced messages: each group has its own sequence
    bool m_singular;            // 0x11 drop the connection's queued messages with the same id first
    uint8_t m_pad12[2];
    int m_delay;                // 0x14 added to the app's timestamp: when the message may go
    uint32_t m_forcedSequenceId; // 0x18 sequenced: this id instead of the group's next one (0 = the next)
    uint8_t m_compression;      // 0x1C 0 none, 1 RLE, 2 LZSS (0x00454E30)
    uint8_t m_pad1D[3];
};
static_assert(sizeof(MsgDesc) == 0x20 && offsetof(MsgDesc, m_priority) == 0x08 &&
                  offsetof(MsgDesc, m_groupId) == 0x10 && offsetof(MsgDesc, m_delay) == 0x14 &&
                  offsetof(MsgDesc, m_compression) == 0x1C,
              "layout must match the original");

// A streamed message not sent all at once (0x44 bytes, constructor 0x004549D0, destructor body 0x00454E00): sent
// in 256-byte pieces, a few each frame (App::ProcessStreamMessages).
struct StreamDesc {
    // 0x004549D0 (net_descs.cpp): size, data, next, group and sendInPlace zeroed.
    StreamDesc* Construct();
    // 0x00454E00: the data freed unless it's the sender's ("Destroying StreamDesc's Data").
    void DestroyBody();

    int m_size;                 // 0x00
    int m_streamId;             // 0x04
    uint8_t m_msgId;            // 0x08
    char m_desc[32];            // 0x09 the sender's description of the data
    uint8_t m_pad29[3];
    uint8_t* m_data;            // 0x2C
    uint8_t* m_next;            // 0x30 the first byte not sent yet
    uint8_t m_groupId;          // 0x34
    uint8_t m_pad35[3];
    uint32_t m_sequenceId;      // 0x38 the next piece's sequence id (reserved when the stream was queued)
    uint32_t m_checksum;        // 0x3C received streams: the whole message's checksum
    bool m_sendInPlace;         // 0x40 m_data is the caller's (not freed)
    uint8_t m_pad41[3];
};
static_assert(sizeof(StreamDesc) == 0x44 && offsetof(StreamDesc, m_data) == 0x2C &&
                  offsetof(StreamDesc, m_sequenceId) == 0x38 && offsetof(StreamDesc, m_sendInPlace) == 0x40,
              "layout must match the original");

// Net::App (vtable 0x006475B4; the Client's 0x006475CC and the Server's 0x006475EC derive from it).
// Virtual slots: 0 deleting destructor (0x00459660), 1 ProcessData (0x00459690), 2 ReceiveData (pure),
// 3 SendEnqueuedMessages(ConnView*) (pure), 4 Init (0x00457110), 5 Deinit (0x00457490).
struct App : AppView {
    // +0x1F00 a double (0.0 from the constructor; not used here). Past AppView's fields (+0x1EB8 the name).
    double& Unknown1F00() { return *reinterpret_cast<double*>(reinterpret_cast<uint8_t*>(this) + 0x1F00); }

    // 0x004594C0 (constructor)
    App* Construct(int flags);
    // 0x00459660 (scalar deleting destructor; the body is 0x00457710)
    App* Destroy(unsigned int flags);

    // 0x00457EB0: queue `desc` for `handle`'s connection, for every ready one (0xFF), or every one but
    // handle & 0x7F (handle with 0x80 set).
    void EnqueueMessage(int handle, MsgDesc* desc);
    // 0x00458740: send `size` bytes in pieces (a start message with the first 256, then data messages).
    // allAtOnce: queue every piece now; else a few each frame (the data copied unless sendInPlace).
    void StreamMessageToConn(ConnView* conn, uint8_t msgId, uint16_t size, void* data, const char* desc,
                             uint8_t groupId, bool allAtOnce, bool sendInPlace);
    // 0x00458AB0: the same for `handle`'s connection or every valid one (0xFF).
    void StreamMessage(int handle, uint8_t msgId, uint16_t size, void* data, const char* desc, uint8_t groupId,
                       bool allAtOnce, bool sendInPlace);
    // 0x00458BE0
    ConnView* FirstConnection(ConnSearch* search);
    // 0x00458C10: SendEnqueuedMessages(conn) (virtual slot 3) for each connection.
    void SendEnqueuedMessages();
    // 0x00458CA0: !sendInFrame: advance the timestamp and make every connection send this frame.
    void SendData(bool sendInFrame);
    // 0x00458D50: whether conn's address is banned.
    bool IsConnectionBanned(ConnView* conn);
    // 0x00458DB0: the outgoing bytes of the last second over the remote connections.
    int GetBandwidthUsed();
    // 0x00458E60
    bool BandwidthExceeded(ConnView* conn);
    // 0x00458F80: the lowest handle (1-255) no connection has; 0 if none is free.
    int GetFreeHandle();
    // 0x00458FB0: invalidate disconnecting connections with nothing left to send; delete invalid ones with
    // nothing left to read or send.
    void TerminateInvalidConnections();
    // 0x004590C0: send the next pieces of the streamed messages.
    void ProcessStreamMessages();
    // 0x00459690 (virtual slot 1): local connections' data handed over, then each connection's messages
    // dispatched. False if processing should stop.
    bool ProcessData();
    // 0x004597C0: a new connection to ip:port (null when there are already the maximum).
    ConnView* NewConnection(int ip, uint16_t port, int flags);

    // Task callbacks (the task's +0x1C is the app).
    // 0x004593C0: timestamp, dead connections, ReceiveData (virtual slot 2).
    static void __cdecl ReceiveNetworkData(void* task);
    // 0x004593E0: each connection's bandwidth meters (in and out).
    static void __cdecl NetworkMetrics(void* task);
    // 0x004594A0: stream pieces, then SendData(true).
    static void __cdecl SendNetworkData(void* task);

    // 0x004598C0 - 0x0045B2F0 (net_app_messages.cpp)
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
static_assert(sizeof(App) == sizeof(AppView), "App adds no fields to AppView");

} // namespace Net
