#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

// Neversoft's network library (Net): an App (the base of the Server and the Client) owns its connections (Conn),
// queues messages for them and streams big messages in pieces. Rewritten here: the App code at 0x00457EB0 -
// 0x004598C0; the rest of the library is still the game's (named where it's called).

namespace Net {

// A connection to another machine (0x3D20 bytes; its constructor 0x00455C80 isn't rewritten yet). Only reached
// through accessors in net_app.cpp.
struct Conn;

// A message to be sent (0x20 bytes; the constructor 0x004549F0 sets id, length, data, priority, queue, delay
// and the forced sequence id - not stream, group, singular or compression).
struct MsgDesc {
    enum Queue : int { kQueueDefault = 0, kQueueImportant = 1, kQueueSequenced = 2 };

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

// The game's Lst::Search<T> as callers lay it out (a vtable, then the current node).
struct ConnSearch {
    void** m_vtable;
    Mem::ListNode* m_node;
};

// A streamed message not sent all at once (0x44 bytes, constructor 0x004549D0, destructor body 0x00454E00): sent
// in 256-byte pieces, a few each frame (App::ProcessStreamMessages).
struct StreamDesc {
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
    uint32_t m_unknown3C;
    bool m_sendInPlace;         // 0x40 m_data is the caller's (not freed)
    uint8_t m_pad41[3];
};
static_assert(sizeof(StreamDesc) == 0x44 && offsetof(StreamDesc, m_data) == 0x2C &&
                  offsetof(StreamDesc, m_sequenceId) == 0x38 && offsetof(StreamDesc, m_sendInPlace) == 0x40,
              "layout must match the original");

// Net::App (vtable 0x006475B4; the Server's 0x006475CC and the Client's 0x006475EC derive from it).
// Virtual slots: 0 deleting destructor (0x00459660), 1 ProcessData (0x00459690), 2 ReceiveData (pure),
// 3 SendEnqueuedMessages(Conn*) (pure), 4 0x00457110, 5 0x00457490.
struct App {
    // The message dispatcher (Net::Dispatcher): a handler list per message id, and its app.
    struct Dispatcher {
        Mem::ListNode m_handlers[0xFF]; // Lst::Head<MsgHandler> (constructor 0x00457010, destructor 0x00457030)
        App* m_app;
    };
    static_assert(sizeof(Dispatcher) == 0x13F0, "layout must match the original");

    void** m_vtable;            // 0x0000
    uint32_t m_unknown0004;
    uint32_t m_timestamp;       // 0x0008 milliseconds (set each frame by the receive task)
    Dispatcher m_dispatcher;    // 0x000C
    uint8_t m_unknown13FC[0x18];
    Mem::ListNode m_node;       // 0x1414 Lst::Node<App> (data = this)
    uint8_t m_unknown1428[0x18];
    void* m_manager;            // 0x1440 (0x004545B0 reads its +0x378: the total bandwidth allowed)
    void* m_processTask;        // 0x1444 Tsk::Task<App>s (vtable 0x006476F0)
    void* m_sendTask;           // 0x1448 (made by the Server's and the Client's constructors)
    void* m_receiveTask;        // 0x144C
    void* m_metricsTask;        // 0x1450
    int m_numConnections;       // 0x1454
    int m_maxConnections;       // 0x1458
    uint8_t m_unknown145C[0x1E84 - 0x145C];
    int m_flags;                // 0x1E84
    uint32_t m_unknown1E88;     // 0x1E88
    Mem::ListNode m_connections; // 0x1E8C Lst::Head<Conn> (vtable 0x006475AC)
    Mem::ListNode m_banned;     // 0x1EA0 Lst::Head<BannedConn> (vtable 0x006475B0): +0x14 the address
    uint8_t m_unknown1EB4[0x1F00 - 0x1EB4];
    double m_unknown1F00;       // 0x1F00

    // 0x004594C0 (constructor)
    App* Construct(int flags);
    // 0x00459660 (scalar deleting destructor; the body is 0x00457710)
    App* Destroy(unsigned int flags);

    // 0x00457EB0: queue `desc` for `handle`'s connection, for every ready one (0xFF), or every one but
    // handle & 0x7F (handle with 0x80 set).
    void EnqueueMessage(int handle, MsgDesc* desc);
    // 0x00458740: send `size` bytes in pieces (a start message with the first 256, then data messages).
    // allAtOnce: queue every piece now; else a few each frame (the data copied unless sendInPlace).
    void StreamMessageToConn(Conn* conn, uint8_t msgId, uint16_t size, void* data, const char* desc,
                             uint8_t groupId, bool allAtOnce, bool sendInPlace);
    // 0x00458AB0: the same for `handle`'s connection or every valid one (0xFF).
    void StreamMessage(int handle, uint8_t msgId, uint16_t size, void* data, const char* desc, uint8_t groupId,
                       bool allAtOnce, bool sendInPlace);
    // 0x00458BE0
    Conn* FirstConnection(ConnSearch* search);
    // 0x00458C10: SendEnqueuedMessages(conn) (virtual slot 3) for each connection.
    void SendEnqueuedMessages();
    // 0x00458CA0: !sendInFrame: advance the timestamp and make every connection send this frame.
    void SendData(bool sendInFrame);
    // 0x00458D50: whether conn's address is banned.
    bool IsConnectionBanned(Conn* conn);
    // 0x00458DB0: the outgoing bytes of the last second over the remote connections.
    int GetBandwidthUsed();
    // 0x00458E60
    bool BandwidthExceeded(Conn* conn);
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
    Conn* NewConnection(int ip, uint16_t port, int flags);

    // Task callbacks (the task's +0x1C is the app).
    // 0x004593C0: timestamp, dead connections, ReceiveData (virtual slot 2).
    static void __cdecl ReceiveNetworkData(void* task);
    // 0x004593E0: each connection's bandwidth meters (in and out).
    static void __cdecl NetworkMetrics(void* task);
    // 0x004594A0: stream pieces, then SendData(true).
    static void __cdecl SendNetworkData(void* task);
};
static_assert(offsetof(App, m_dispatcher) == 0xC && offsetof(App, m_node) == 0x1414 &&
                  offsetof(App, m_manager) == 0x1440 && offsetof(App, m_numConnections) == 0x1454 &&
                  offsetof(App, m_flags) == 0x1E84 && offsetof(App, m_connections) == 0x1E8C &&
                  offsetof(App, m_banned) == 0x1EA0 && offsetof(App, m_unknown1F00) == 0x1F00,
              "layout must match the original");

} // namespace Net
