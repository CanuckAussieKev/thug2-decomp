#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/net/net_app_view.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_lists.h"

struct sockaddr;

// Neversoft's network library (Net): an App (the base of the Server and the Client) owns its connections (Conn),
// queues messages for them and streams big messages in pieces. Here: App's constructor and the App code at
// 0x00457EB0 - 0x004598C0 (net_app.cpp); App's methods 0x00456910-0x00457EB0 are in net_app_view.cpp, its message
// handling in net_app_messages.cpp.

namespace Net {

struct App;

// What an App does with a packet from an address that isn't one of its connections (+0x1E88).
using ForeignPacketHandler = void(__cdecl*)(char* data, int size, sockaddr* from);

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
    uint32_t m_checksum;        // 0x3C received streams: the whole message's checksum
    bool m_sendInPlace;         // 0x40 m_data is the caller's (not freed)
    uint8_t m_pad41[3];
};
static_assert(sizeof(StreamDesc) == 0x44 && offsetof(StreamDesc, m_data) == 0x2C &&
                  offsetof(StreamDesc, m_sequenceId) == 0x38 && offsetof(StreamDesc, m_sendInPlace) == 0x40,
              "layout must match the original");

// Net::App (vtable 0x006475B4; the Client's 0x006475CC and the Server's 0x006475EC derive from it).
// Virtual slots: 0 deleting destructor (0x00459660), 1 ProcessData (0x00459690), 2 ReceiveData (pure),
// 3 SendEnqueuedMessages(Conn*) (pure), 4 Init (0x00457110), 5 Deinit (0x00457490).
struct App {
    // m_flags
    static constexpr uint32_t kBroadcast = 0x01; // the socket may broadcast
    static constexpr uint32_t kLocal = 0x08;     // no socket of its own (a client in the same process as its server)
    static constexpr uint32_t kMeasureLatency = 0x04; // send latency tests
    static constexpr uint32_t kAcceptForeign = 0x10; // connection requests from foreign addresses are answered
    static constexpr uint32_t kSecure = 0x20;    // packets carry a checksum and are scrambled (Encrypt / Decrypt)

    // The largest packet (the receive and scramble buffers).
    static constexpr uint32_t kMaxPacketSize = 0x51C;

    // 0x00456910: `data` scrambled into `out`: a 16-bit checksum (the CRC's halves added), then each byte XORed
    // with the checksum's low byte. *outLength = length + 2.
    void Encrypt(const uint8_t* data, uint8_t* out, int length, int* outLength);
    // 0x00456CC0: a received packet into `out`: false if it's shorter than 3 bytes or (secure) its checksum is
    // wrong; secure packets lose their checksum (`length` - 2 bytes copied).
    bool Decrypt(const uint8_t* packet, uint8_t* out, int length);

    // 0x00456A50 / 0x00456A60 / 0x00456A70 / 0x00456A80: the app's main-loop tasks (made by App::Construct and,
    // the send task, by the Server's and the Client's constructors).
    void* SendTask() { return m_sendTask; }
    void* ReceiveTask() { return m_receiveTask; }
    void* ProcessTask() { return m_processTask; }
    void* MetricsTask() { return m_metricsTask; }
    // 0x00456AF0
    void* NetManager() { return m_manager; }
    // 0x00456B60
    uint32_t Socket() { return m_socket; }
    // 0x00456B70
    void SetForeignPacketHandler(ForeignPacketHandler handler) { m_foreignPacketHandler = handler; }
    // 0x00456F90
    bool IsLocal() { return (m_flags >> 3) & 1; }
    // 0x00456FA0
    bool AcceptsForeignConnections() { return (m_flags >> 4) & 1; }

    // 0x00456A90: "<name>: Error <WSAGetLastError>" to the debugger output.
    void ReportError();
    // 0x00456B00: time to send to `conn` (its send interval has passed since its last send, or it's flagged to
    // send now).
    bool IsSendDue(Conn* conn);
    // 0x00456B40: the two connections know each other as aliases (the same player's server and client ends).
    void AliasConnections(Conn* first, Conn* second);
    // 0x00456BA0: bind the socket to `port` on any address (if it's taken: report, and take any free port), and
    // note the address it got. (`address` isn't used.) False (reported) on failure.
    bool Bind(uint32_t address, uint16_t port);
    // 0x00456D90: a new SeqQueuedMsg (null if out of memory).
    SeqQueuedMsg* NewQueuedMsg(uint8_t id, uint16_t length, void* data, uint8_t arg4, uint8_t arg5);
    // 0x00456E20: a datagram to ip:port (scrambled first if secure). True if sent or it would block.
    bool SendTo(uint32_t ip, uint16_t port, const uint8_t* data, int length, int flags);
    // 0x00456F10: a packet on the connected socket (scrambled first if secure). False if not connected or
    // send failed.
    bool Send(const uint8_t* data, int length, int flags);
    // 0x00457110 (virtual slot 4): the socket (unless local: UDP, non-blocking, broadcast if asked), the
    // counters zeroed, the dispatcher set up with the library's own handlers. False (reported) on failure.
    bool Init();
    // 0x00457280: a message (id, 16-bit length, data) as one datagram to ip:port.
    bool SendMessageTo(uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port, int flags);
    // 0x00457300: `conn` taken out of the connection list and deleted.
    void TerminateConnection(Conn* conn);
    // 0x004573D0: the next connection of a search of the connection list (null at the end).
    Conn* NextConnection(ConnSearch* search);
    // 0x00457400: `conn`'s address added to the banned list.
    void BanConnection(Conn* conn);
    // 0x00457490 (virtual slot 5): the socket closed (unless local), the dispatcher cleared, every connection
    // deleted.
    void Deinit();
    // 0x00457560: dispatch the sequenced messages received ahead of their turn whose turn has come, group by
    // group. False if a handler said the connection is gone.
    bool DispatchQueuedSequencedMessages(Conn* conn);
    // 0x00457710: the destructor body.
    void DestroyBody();
    // 0x004578B0
    Conn* GetConnectionByAddress(uint32_t ip, uint16_t port);
    // 0x00457910
    Conn* GetConnectionByHandle(int handle);
    // 0x00457970: an important or sequenced message is unsent or waiting for its ack longer than `conn`'s
    // resend threshold.
    bool ImportantMessagesDue(Conn* conn);
    // 0x00457AC0: any message to send to `conn` now (a normal one whose time has come, or as above).
    bool MessagesDue(Conn* conn);
    // 0x00457C80: the messages with id `id` taken out of one of `conn`'s queues (0 normal, 1 important,
    // 2 sequenced), a QueuedMsg deleted when its last link goes.
    void FreeMessages(Conn* conn, int queue, uint8_t id);


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
    // 0x00456CA0: counts the frame (+0x1410), then ProcessData (virtual slot 1; a tail jump).
    static void __cdecl ProcessNetworkData(void* task);
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

    void** m_vtable;          // +0x0000
    uint32_t m_unknown4;      // +0x0004
    uint32_t m_time;          // +0x0008 the current time (ms)
    Dispatcher m_dispatcher;  // +0x000C (wt/netC)
    App* m_dispatcherApp;     // +0x13F8 the Dispatcher's app
    // +0x13FC statistics (zeroed by Init)
    int m_totalBytesOut;      // +0x13FC
    int m_totalBytesIn;       // +0x1400
    int m_lostPackets;        // +0x1404 timestamps skipped over
    int m_latePackets;        // +0x1408 timestamps older than the newest
    int m_dupePackets;        // +0x140C timestamps seen twice
    int m_frameCount;         // +0x1410 frames processed (ProcessNetworkData)
    Mem::ListNode m_list1414; // +0x1414 Lst::Node<App> (vtable 0x006474D8, data = the app)
    uint32_t m_socket;        // +0x1428
    uint8_t m_localAddress[0x10]; // +0x142C sockaddr_in (Bind)
    bool m_connected;         // +0x143C the socket is connect()ed (send/recv, not sendto/recvfrom)
    uint8_t m_pad143D[3];
    void* m_manager;          // +0x1440 the Net::Manager (its message flags: kMsgNoLength)
    // Tsk::Task<App>s (vtable 0x006476F0): process 0x00456CA0 (priority 0), send 0x004594A0 (-2000),
    // receive 0x004593C0 (1000), metrics 0x004593E0 (-2500).
    void* m_processTask;      // +0x1444
    void* m_sendTask;         // +0x1448
    void* m_receiveTask;      // +0x144C
    void* m_metricsTask;      // +0x1450
    int m_numConnections;     // +0x1454 (zeroed by Init)
    int m_maxConnections;     // +0x1458 (App::NewConnection makes no more; Server::Init clears it)
    uint8_t m_scrambled[0x514]; // +0x145C a secure packet being sent
    char m_receiveBuffer[0x514]; // +0x1970
    uint32_t m_flags;         // +0x1E84
    ForeignPacketHandler m_foreignPacketHandler; // +0x1E88 packets from addresses that aren't connections
    ConnNode m_connections;   // +0x1E8C head (data -1)
    BannedConnNode m_banned;  // +0x1EA0 head
    uint32_t m_unknown1EB4;
    char m_name[0x48];        // +0x1EB8 ("server", "client"...)
    double m_unknown1F00;     // +0x1F00 (0.0 from the constructor; not used by the library)
};
static_assert(offsetof(App, m_dispatcher) == 0x0C && offsetof(App, m_dispatcherApp) == 0x13F8 &&
                  offsetof(App, m_totalBytesOut) == 0x13FC && offsetof(App, m_frameCount) == 0x1410 &&
                  offsetof(App, m_list1414) == 0x1414 && offsetof(App, m_socket) == 0x1428 &&
                  offsetof(App, m_connected) == 0x143C && offsetof(App, m_manager) == 0x1440 &&
                  offsetof(App, m_processTask) == 0x1444 && offsetof(App, m_numConnections) == 0x1454 &&
                  offsetof(App, m_maxConnections) == 0x1458 && offsetof(App, m_scrambled) == 0x145C &&
                  offsetof(App, m_receiveBuffer) == 0x1970 && offsetof(App, m_flags) == 0x1E84 &&
                  offsetof(App, m_foreignPacketHandler) == 0x1E88 && offsetof(App, m_connections) == 0x1E8C &&
                  offsetof(App, m_banned) == 0x1EA0 && offsetof(App, m_name) == 0x1EB8 &&
                  offsetof(App, m_unknown1F00) == 0x1F00 && sizeof(App) == 0x1F08,
              "layout must match the original (the Client, 0x1F18 bytes, adds the server's address)");

} // namespace Net
