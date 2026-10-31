#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/arena_alloc.h"
#include "game/net/net_app_view.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_lists.h"

struct sockaddr;

// Neversoft's network library (Net): an App (the base of the Server and the Client) owns its connections (Conn),
// queues messages for them and streams big messages in pieces. Here: App's constructor and the App code at
// 0x00457EB0 - 0x004598C0 (net_app.cpp); App's methods 0x00456910-0x00457EB0 are in net_app_view.cpp, its message
// handling in net_app_messages.cpp.

namespace NetLink {

struct AppHost;

extern void** const kAppVtable; // 0x006475B4

// What an App does with a packet from an address that isn't one of its connections (+0x1E88).
using UnknownPacketCallback = void(__cdecl*)(char* data, int size, sockaddr* from);

// A message to be sent (0x20 bytes; the constructor 0x004549F0 sets every field: zero, priority 0x80).
struct NetMessageInfo {
    enum Queue : int { kLaneNormal = 0, kLanePriority = 1, kLaneOrdered = 2 };

    // 0x004549F0 (net_descs.cpp)
    NetMessageInfo* MakeNew();

    uint8_t m_ident;               // 0x00
    uint8_t m_feed;           // 0x01 a piece of a streamed message (counted by Conn 0x004565E0)
    uint16_t m_byteCount;          // 0x02
    void* m_content;               // 0x04
    int m_rank;             // 0x08 (0x80 by default)
    int m_queue;                // 0x0C
    uint8_t m_groupId;          // 0x10 sequenced messages: each group has its own sequence
    bool m_singular;            // 0x11 drop the connection's queued messages with the same id first
    uint8_t m_pad12[2];
    int m_delay;                // 0x14 added to the app's timestamp: when the message may go
    uint32_t m_fixedSeqNo; // 0x18 sequenced: this id instead of the group's next one (0 = the next)
    uint8_t m_compression;      // 0x1C 0 none, 1 RLE, 2 LZSS (0x00454E30)
    uint8_t m_pad1D[3];
};
static_assert(sizeof(NetMessageInfo) == 0x20 && offsetof(NetMessageInfo, m_rank) == 0x08 &&
                  offsetof(NetMessageInfo, m_groupId) == 0x10 && offsetof(NetMessageInfo, m_delay) == 0x14 &&
                  offsetof(NetMessageInfo, m_compression) == 0x1C,
              "layout must match the original");

// A streamed message not sent all at once (0x44 bytes, constructor 0x004549D0, destructor body 0x00454E00): sent
// in 256-byte pieces, a few each frame (AppHost::PumpStreamTraffic).
struct StreamDesc {
    // 0x004549D0 (net_descs.cpp): size, data, next, group and borrowCallerBuffer zeroed.
    StreamDesc* MakeNew();
    // 0x00454E00: the data freed unless it's the sender's ("Destroying StreamDesc's Data").
    void DestroyBody();

    int m_extent;                 // 0x00
    int m_streamId;             // 0x04
    uint8_t m_msgId;            // 0x08
    char m_desc[32];            // 0x09 the sender's description of the data
    uint8_t m_pad29[3];
    uint8_t* m_content;            // 0x2C
    uint8_t* m_succ;            // 0x30 the first byte not sent yet
    uint8_t m_groupId;          // 0x34
    uint8_t m_pad35[3];
    uint32_t m_orderNumber;      // 0x38 the next piece's sequence id (reserved when the stream was queued)
    uint32_t m_checksum;        // 0x3C received streams: the whole message's checksum
    bool m_borrowedPayload;         // 0x40 m_content is the caller's (not freed)
    uint8_t m_pad41[3];
};
static_assert(sizeof(StreamDesc) == 0x44 && offsetof(StreamDesc, m_content) == 0x2C &&
                  offsetof(StreamDesc, m_orderNumber) == 0x38 && offsetof(StreamDesc, m_borrowedPayload) == 0x40,
              "layout must match the original");

// NetLink::AppHost (vtable 0x006475B4; the Client's 0x006475CC and the Server's 0x006475EC derive from it).
// Virtual slots: 0 deleting destructor (0x00459660), 1 HandleIncoming (0x00459690), 2 PullInbound (pure),
// 3 FlushOutbox(Conn*) (pure), 4 Init (0x00457110), 5 Deinit (0x00457490).
struct AppHost {
    // m_flagBits
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

    // 0x00456A50 / 0x00456A60 / 0x00456A70 / 0x00456A80: the app's main-loop tasks (made by AppHost::MakeNew and,
    // the send task, by the Server's and the Client's constructors).
    void* SendTask() { return m_sendTask; }
    void* ReceiveTask() { return m_receiveTask; }
    void* ProcessTask() { return m_processTask; }
    void* MetricsTask() { return m_metricsTask; }
    // 0x00456AF0
    void* NetSessionSlot() { return m_supervisor; }
    // 0x00456B60
    uint32_t Socket() { return m_sock; }
    // 0x00456B70
    void SetOutsidePacketHook(UnknownPacketCallback handler) { m_unknownPacketSink = handler; }
    // 0x00456F90
    bool IsHostLocal() { return (m_flagBits >> 3) & 1; }
    // 0x00456FA0
    bool AllowsOutsidePeers() { return (m_flagBits >> 4) & 1; }

    // 0x00456A90: "<name>: Error <WSAGetLastError>" to the debugger output.
    void RaiseFault();
    // 0x00456B00: time to send to `conn` (its send interval has passed since its last send, or it's flagged to
    // send now).
    bool IsSendDue(Conn* conn);
    // 0x00456B40: the two connections know each other as aliases (the same player's server and client ends).
    void LinkDuplicateConns(Conn* first, Conn* second);
    // 0x00456BA0: bind the socket to `port` on any address (if it's taken: report, and take any free port), and
    // note the address it got. (`address` isn't used.) False (reported) on failure.
    bool Bind(uint32_t address, uint16_t port);
    // 0x00456D90: a new SeqQueuedMsg (null if out of memory).
    SeqQueuedMsg* NewQueuedMsg(uint8_t id, uint16_t length, void* data, uint8_t arg4, uint8_t arg5);
    // 0x00456E20: a datagram to ip:port (scrambled first if secure). True if sent or it would block.
    bool TransmitTo(uint32_t ip, uint16_t port, const uint8_t* data, int length, int flags);
    // 0x00456F10: a packet on the connected socket (scrambled first if secure). False if not connected or
    // send failed.
    bool PostOut(const uint8_t* data, int length, int flags);
    // 0x00457110 (virtual slot 4): the socket (unless local: UDP, non-blocking, broadcast if asked), the
    // counters zeroed, the dispatcher set up with the library's own handlers. False (reported) on failure.
    bool PrepareAll();
    // 0x00457280: a message (id, 16-bit length, data) as one datagram to ip:port.
    bool DeliverTo(uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port, int flags);
    // 0x00457300: `conn` taken out of the connection list and deleted.
    void SeverLink(Conn* conn);
    // 0x004573D0: the next connection of a search of the connection list (null at the end).
    Conn* StepConnWalk(ConnSearch* search);
    // 0x00457400: `conn`'s address added to the banned list.
    void BlockConnection(Conn* conn);
    // 0x00457490 (virtual slot 5): the socket closed (unless local), the dispatcher cleared, every connection
    // deleted.
    void TearDownState();
    // 0x00457560: dispatch the sequenced messages received ahead of their turn whose turn has come, group by
    // group. False if a handler said the connection is gone.
    bool DispatchQueuedSequencedMessages(Conn* conn);
    // 0x00457710: the destructor body.
    void DestroyBody();
    // 0x004578B0
    Conn* FindConnAtAddress(uint32_t ip, uint16_t port);
    // 0x00457910
    Conn* FindConnWithId(int handle);
    // 0x00457970: an important or sequenced message is unsent or waiting for its ack longer than `conn`'s
    // resend threshold.
    bool ImportantMessagesDue(Conn* conn);
    // 0x00457AC0: any message to send to `conn` now (a normal one whose time has come, or as above).
    bool MessagesDue(Conn* conn);
    // 0x00457C80: the messages with id `id` taken out of one of `conn`'s queues (0 normal, 1 important,
    // 2 sequenced), a PendingMsg deleted when its last link goes.
    void FreeMessages(Conn* conn, int queue, uint8_t id);


    // 0x004594C0 (constructor)
    AppHost* MakeNew(int flags);
    // 0x00459660 (scalar deleting destructor; the body is 0x00457710)
    AppHost* Teardown(unsigned int flags);

    // 0x00457EB0: queue `desc` for `handle`'s connection, for every ready one (0xFF), or every one but
    // handle & 0x7F (handle with 0x80 set).
    void PutOnOutbox(int handle, NetMessageInfo* desc);
    // 0x00458740: send `size` bytes in pieces (a start message with the first 256, then data messages).
    // queueAllPieces: queue every piece now; else a few each frame (the data copied unless borrowCallerBuffer).
    void SendBulkToConn(Conn* conn, uint8_t msgId, uint16_t size, void* data, const char* desc,
                             uint8_t groupId, bool queueAllPieces, bool borrowCallerBuffer);
    // 0x00458AB0: the same for `handle`'s connection or every valid one (0xFF).
    void SendBulkMessage(int handle, uint8_t msgId, uint16_t size, void* data, const char* desc, uint8_t groupId,
                       bool queueAllPieces, bool borrowCallerBuffer);
    // 0x00458BE0
    Conn* BeginConnWalk(ConnSearch* search);
    // 0x00458C10: FlushOutbox(conn) (virtual slot 3) for each connection.
    void FlushOutbox();
    // 0x00458CA0: !sendInFrame: advance the timestamp and make every connection send this frame.
    void Transmit(bool sendInFrame);
    // 0x00458D50: whether conn's address is banned.
    bool IsConnBlocked(Conn* conn);
    // 0x00458DB0: the outgoing bytes of the last second over the remote connections.
    int FetchTrafficSpent();
    // 0x00458E60
    bool OverBandwidthBudget(Conn* conn);
    // 0x00458F80: the lowest handle (1-255) no connection has; 0 if none is free.
    int FetchUnusedSlotId();
    // 0x00458FB0: invalidate disconnecting connections with nothing left to send; delete invalid ones with
    // nothing left to read or send.
    void DropBrokenLinks();
    // 0x004590C0: send the next pieces of the streamed messages.
    void PumpStreamTraffic();
    // 0x00459690 (virtual slot 1): local connections' data handed over, then each connection's messages
    // dispatched. False if processing should stop.
    bool HandleIncoming();
    // 0x004597C0: a new connection to ip:port (null when there are already the maximum).
    Conn* OpenedLink(int ip, uint16_t port, int flags);

    // Task callbacks (the task's +0x1C is the app).
    // 0x00456CA0: counts the frame (+0x1410), then HandleIncoming (virtual slot 1; a tail jump).
    static void __cdecl PumpIncomingTraffic(void* task);
    // 0x004593C0: timestamp, dead connections, PullInbound (virtual slot 2).
    static void __cdecl ReceiveNetworkData(void* task);
    // 0x004593E0: each connection's bandwidth meters (in and out).
    static void __cdecl NetworkMetrics(void* task);
    // 0x004594A0: stream pieces, then Transmit(true).
    static void __cdecl FlushOutgoingTraffic(void* task);

    // 0x004598C0 - 0x0045B2F0 (net_app_messages.cpp)
    // 0x004598C0: the messages of one queue (0 normal, 1 important, 2 sequenced) that are due go into the
    // connection's packet; false if one didn't fit. Normal messages are dequeued as they go; important and
    // sequenced ones stay until acked (`resendsOnly`: only those sent before whose ack is overdue).
    bool BundleMessages(Conn* conn, int queue, bool resendsOnly);

    // Handlers (registered by AppHost::PrepareAll, 0x00457110).
    // 0x0045AD60 (id 1): answers a latency test with its time.
    static int __cdecl OnPingProbeMsg(InboundMsgCtx* context);
    // 0x0045AB60 (id 2): the answer to our latency test: the round trip goes into the latency average.
    static int __cdecl OnPingReplyMsg(InboundMsgCtx* context);
    // 0x0045AF10 (id 7): a sequenced message, buffered in its group's list by sequence number (to be handled
    // in order); acks are sent for the packet.
    static int __cdecl OnOrderedMsgs(InboundMsgCtx* context);
    // 0x0045B0B0 (id 8): an ack: the important and sequenced messages sent with that timestamp are done; the
    // round trip is a latency measurement.
    static int __cdecl OnReceiptMsg(InboundMsgCtx* context);
    // 0x0045ABF0 (id 0x12): a compressed message (method 1 or 2; NOT ORIGINAL: 3, THUGPRO's LZHAM), expanded and
    // dispatched. NOT ORIGINAL: every length in it bounded (short messages, streams ending inside a code, an LZSS
    // stream expanding past its size, an inner length past the data, unknown methods: dropped).
    static int __cdecl HandleCompressedMessages(InboundMsgCtx* context);
    // 0x0045B2F0 (ids 0xE, 0xF): a stream's start and its pieces; the whole message is dispatched when complete.
    static int __cdecl DrainStreamMsgs(InboundMsgCtx* context);

    void** m_vtable;          // +0x0000
    uint32_t m_unknown4;      // +0x0004
    uint32_t m_clock;          // +0x0008 the current time (ms)
    Router m_dispatcher;  // +0x000C (wt/netC)
    AppHost* m_dispatcherApp;     // +0x13F8 the Dispatcher's app
    // +0x13FC statistics (zeroed by Init)
    int m_outboundByteTotal;      // +0x13FC
    int m_inboundByteTotal;       // +0x1400
    int m_skippedStamps;        // +0x1404 timestamps skipped over
    int m_staleArrivals;        // +0x1408 timestamps older than the newest
    int m_repeatArrivals;        // +0x140C timestamps seen twice
    int m_frameCount;         // +0x1410 frames processed (PumpIncomingTraffic)
    MemSys::ListNode m_list1414; // +0x1414 ListKit::Elem<App> (vtable 0x006474D8, data = the app)
    uint32_t m_sock;        // +0x1428
    uint8_t m_localAddress[0x10]; // +0x142C sockaddr_in (Bind)
    bool m_linkedUp;         // +0x143C the socket is connect()ed (send/recv, not sendto/recvfrom)
    uint8_t m_pad143D[3];
    void* m_supervisor;          // +0x1440 the NetLink::Hub (its message flags: kMsgNoLength)
    // Scheduling::Task<App>s (vtable 0x006476F0): process 0x00456CA0 (priority 0), send 0x004594A0 (-2000),
    // receive 0x004593C0 (1000), metrics 0x004593E0 (-2500).
    void* m_processTask;      // +0x1444
    void* m_sendTask;         // +0x1448
    void* m_receiveTask;      // +0x144C
    void* m_metricsTask;      // +0x1450
    int m_openLinks;     // +0x1454 (zeroed by Init)
    int m_peerCap;     // +0x1458 (AppHost::OpenedLink makes no more; HostEnd::PrepareAll clears it)
    uint8_t m_scrambled[0x514]; // +0x145C a secure packet being sent
    char m_receiveBuffer[0x514]; // +0x1970
    uint32_t m_flagBits;         // +0x1E84
    UnknownPacketCallback m_unknownPacketSink; // +0x1E88 packets from addresses that aren't connections
    ConnNode m_circuits;   // +0x1E8C head (data -1)
    BannedConnNode m_banned;  // +0x1EA0 head
    uint32_t m_unknown1EB4;
    char m_label[0x48];        // +0x1EB8 ("server", "client"...)
    double m_unknown1F00;     // +0x1F00 (0.0 from the constructor; not used by the library)
};
static_assert(offsetof(AppHost, m_dispatcher) == 0x0C && offsetof(AppHost, m_dispatcherApp) == 0x13F8 &&
                  offsetof(AppHost, m_outboundByteTotal) == 0x13FC && offsetof(AppHost, m_frameCount) == 0x1410 &&
                  offsetof(AppHost, m_list1414) == 0x1414 && offsetof(AppHost, m_sock) == 0x1428 &&
                  offsetof(AppHost, m_linkedUp) == 0x143C && offsetof(AppHost, m_supervisor) == 0x1440 &&
                  offsetof(AppHost, m_processTask) == 0x1444 && offsetof(AppHost, m_openLinks) == 0x1454 &&
                  offsetof(AppHost, m_peerCap) == 0x1458 && offsetof(AppHost, m_scrambled) == 0x145C &&
                  offsetof(AppHost, m_receiveBuffer) == 0x1970 && offsetof(AppHost, m_flagBits) == 0x1E84 &&
                  offsetof(AppHost, m_unknownPacketSink) == 0x1E88 && offsetof(AppHost, m_circuits) == 0x1E8C &&
                  offsetof(AppHost, m_banned) == 0x1EA0 && offsetof(AppHost, m_label) == 0x1EB8 &&
                  offsetof(AppHost, m_unknown1F00) == 0x1F00 && sizeof(AppHost) == 0x1F08,
              "layout must match the original (the Client, 0x1F18 bytes, adds the server's address)");

// NOT ORIGINAL - a look at every message AppHost::PutOnOutbox queues, first (THUGPRO's additions to messages we send:
// game/net/thugpro_peer.h). The filter may queue other messages itself (through PutOnOutbox) and returns true
// when it has queued this one itself (PutOnOutbox then does nothing more). Null (the default): the original.
using EnqueueFilter = bool (*)(AppHost* app, int handle, NetMessageInfo* desc);
void SetEnqueueFilter(EnqueueFilter filter);

} // namespace Net
