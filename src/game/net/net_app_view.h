#pragma once

// Methods of Neversoft's Net::App (the base of the net library's Client and Server: a UDP socket, its
// connections, the message dispatcher and queues) and the Net::Conn fields they use.
//
// App's and Conn's constructors aren't in this file's address range (App: 0x004594C0, wt/netB; Conn:
// 0x00455C80), so the classes are defined elsewhere: AppView and ConnView here are views of the fields these
// methods use, at their offsets, for the merger to fold into the real classes.

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/net/net_latency.h"
#include "game/net/net_lists.h"

namespace Net {

// A message's contents as queued for sending. Several connections' queues can share one: a reference count.
// Two kinds: those of the normal and important queues (constructor 0x00454FA0) and those of the sequenced
// queue (0x00455040, 0x10 bytes); the destructor 0x004550F0 (both) frees the data.
struct QueuedMsg {
    uint8_t m_id;      // +0x00
    uint16_t m_length; // +0x02
    char* m_data;      // +0x04
    int m_refCount;    // +0x08
};
struct SeqQueuedMsg {
    uint8_t m_id;      // +0x00
    uint16_t m_length; // +0x02
    char* m_data;      // +0x04
    uint8_t m_byte8;   // +0x08 (the constructor's 4th argument)
    int m_refCount;    // +0x0C
};
static_assert(offsetof(QueuedMsg, m_refCount) == 0x08 && offsetof(SeqQueuedMsg, m_data) == 0x04 &&
                  offsetof(SeqQueuedMsg, m_refCount) == 0x0C && sizeof(SeqQueuedMsg) == 0x10,
              "layout must match the original");

// The links that queue a QueuedMsg on a connection: list nodes (data = the link itself), deleted through
// their vtable's scalar deleting destructor.
struct MsgLink : Mem::ListNode {
    // +0x14...: depends on the queue (see ConnView).
    uint8_t m_fields[0x18];

    template <typename T>
    T& At(uint32_t offset)
    {
        return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(this) + offset);
    }
};

// Net::Conn: one end of a connection, as an App keeps it (0x3D20 bytes). Its methods: net_conn.cpp.
struct ConnView {
    // m_flags
    static constexpr uint32_t kFlagLocal = 0x1;   // the App's own client/server in this process
    static constexpr uint32_t kFlagRemote = 0x2;
    static constexpr uint32_t kFlagForeign = 0x4; // made for a packet from an unknown address
    // m_status
    static constexpr uint32_t kStatusReady = 0x1;    // cleared by Invalidate
    static constexpr uint32_t kStatus2 = 0x2;        // (set by game code) resends wait 1000 ms whatever the latency
    static constexpr uint32_t kStatusInvalid = 0x4;
    // m_bandwidthType: the bandwidth SetBandwidthType gives each (bytes per second)
    static constexpr int kBandwidthBroadband = 0;   // 400000
    static constexpr int kBandwidthModem = 1;       // 4200
    static constexpr int kBandwidthLan = 2;         // 800000
    static constexpr int kNumGroups = 256;

    // 0x00455C80
    ConnView* Construct(int flags);
    // 0x00456790 (the destructor)
    void Destroy();

    // 0x00455970
    uint32_t GetSendInterval();
    // 0x00455980
    void SetForceSendThisFrame(bool force);
    // 0x00455990
    bool GetForceSendThisFrame();
    // 0x004559A0
    int GetBandwidthType();
    // 0x004559B0
    void SetBandwidth(int bytesPerSecond);
    // 0x004559C0
    int GetBandwidth();
    // 0x00455A10 / 0x00455A20 / 0x00455A30
    bool IsForeign();
    bool IsLocal();
    bool IsRemote();
    // 0x00455A40: the type, and its bandwidth (an unknown type leaves the bandwidth).
    void SetBandwidthType(int type);
    // 0x00455B40: 0 for a local connection.
    int GetAveLatency();
    // 0x00455C10: no longer ready; invalid (App::TerminateInvalidConnections deletes it).
    void Invalidate();
    // 0x00455C20
    bool IsValid();
    // 0x00455C30: how long an important message waits for its ack before going again: twice the average
    // latency (none for a local connection), at least 500 ms; 1000 ms with kStatus2.
    uint32_t GetResendThreshold();
    // 0x00455E50: the sequenced messages received ahead of their turn deleted (links and messages).
    void DestroyReceivedSequencedMessages();
    // 0x00455F30: every link deleted (the three queues, the received ones) and the streams (theirs too).
    void DestroyLinks();
    // 0x00456210: the important and sequenced queues emptied: links deleted, messages released.
    void DestroyImportantMessages();
    // 0x00456370: the three queues' messages released (each deleted with its last link); links kept.
    void ReleaseQueuedMessages();
    // 0x00456540: the most times one important or sequenced message has been resent.
    int GetHighestResendCount();
    // 0x004565E0: the sequenced links carrying stream pieces.
    int GetNumQueuedStreamPieces();
    // 0x00456630: the important and sequenced messages' resend counts zeroed.
    void ClearResendCounts();
    // 0x004566D0: the important and sequenced messages that went out with `packetstamp` go again (a send failed).
    void FlagMessagesForResending(uint32_t packetstamp);
    // 0x00456770: every queue emptied (as the destructor does).
    void DestroyMessageQueues();

    Mem::ListNode m_normalMsgs;     // +0x00 links: QueuedMsg* at +0x14, send time at +0x18
    Mem::ListNode m_importantMsgs;  // +0x14 links: last send time at +0x14 (0 = not sent), QueuedMsg* at +0x18,
                                    //       a time at +0x24
    Mem::ListNode m_sequencedMsgs;  // +0x28 links: last send time at +0x1C, a time at +0x24, SeqQueuedMsg* at
                                    //       +0x28
    int m_handle;                   // +0x3C
    uint32_t m_ip;                  // +0x40
    uint32_t m_id;                  // +0x44 a running number (0x0067D800, counted by the constructor)
    uint16_t m_port;                // +0x48
    uint8_t m_pad4A[2];
    uint8_t* m_writeBuffer;         // +0x4C the packet being built (0x514 bytes): its start
    uint8_t* m_writePtr;            // +0x50 and end
    uint8_t* m_readBuffer;          // +0x54 received data (0xA28 bytes): its start
    uint8_t* m_readPtr;             // +0x58 and end
    uint32_t m_unknown5C;           // +0x5C (never touched by the library)
    uint32_t m_status;              // +0x60 kStatus...
    uint32_t m_flags;               // +0x64 kFlag...
    void* m_app;                    // +0x68 the App (set by App::NewConnection)
    uint32_t m_lastCommTime;        // +0x6C
    uint32_t m_lastSendTime;        // +0x70
    uint32_t m_sendInterval;        // +0x74
    uint32_t m_nextSequenceIds[kNumGroups]; // +0x78 outgoing: the next sequence id of each group
    uint32_t m_sequenceNumbers[kNumGroups]; // +0x478 the next sequence number expected in each group
    int m_nextStreamId;             // +0x878
    // +0x87C sequenced messages received ahead of their turn, by group: links with the group at +0x15, the
    // sequence number at +0x20, SeqQueuedMsg* at +0x28.
    Mem::ListNode m_receivedQueues[kNumGroups];
    Mem::ListNode m_receivedStreams; // +0x1C7C Lst::Head<StreamLink>: streams being received
    Mem::ListNode m_sendStreams;    // +0x1C90 Lst::Head<StreamLink>: streams being sent
    LatencyTest m_latencyTest;      // +0x1CA4
    uint8_t m_receiveMetrics[0x100C]; // +0x1CDC Net::Metrics (wt/netD)
    uint8_t m_sendMetrics[0x100C];  // +0x2CE8
    bool m_sendNow;                 // +0x3CF4 force a send this frame
    uint8_t m_pad3CF5[3];
    ConnNode m_appNode;             // +0x3CF8 in the App's connection list (data = the connection)
    ConnView* m_aliasConn;          // +0x3D0C
    int m_bandwidthType;            // +0x3D10
    uint32_t m_lastTimestampIn;     // +0x3D14
    uint32_t m_timestampOut;        // +0x3D18
    int m_bandwidth;                // +0x3D1C bytes per second
};
static_assert(offsetof(ConnView, m_handle) == 0x3C && offsetof(ConnView, m_port) == 0x48 &&
                  offsetof(ConnView, m_writeBuffer) == 0x4C && offsetof(ConnView, m_status) == 0x60 &&
                  offsetof(ConnView, m_lastSendTime) == 0x70 && offsetof(ConnView, m_nextSequenceIds) == 0x78 &&
                  offsetof(ConnView, m_sequenceNumbers) == 0x478 && offsetof(ConnView, m_nextStreamId) == 0x878 &&
                  offsetof(ConnView, m_receivedQueues) == 0x87C && offsetof(ConnView, m_receivedStreams) == 0x1C7C &&
                  offsetof(ConnView, m_sendStreams) == 0x1C90 && offsetof(ConnView, m_latencyTest) == 0x1CA4 &&
                  offsetof(ConnView, m_receiveMetrics) == 0x1CDC && offsetof(ConnView, m_sendMetrics) == 0x2CE8 &&
                  offsetof(ConnView, m_sendNow) == 0x3CF4 && offsetof(ConnView, m_appNode) == 0x3CF8 &&
                  offsetof(ConnView, m_aliasConn) == 0x3D0C && offsetof(ConnView, m_bandwidthType) == 0x3D10 &&
                  offsetof(ConnView, m_bandwidth) == 0x3D1C && sizeof(ConnView) == 0x3D20,
              "layout must match the original");

// Net::App (vtable 0x006475B4; slot 4 Init, slot 5 Deinit).
class AppView {
public:
    // m_flags
    static constexpr uint32_t kBroadcast = 0x01; // the socket may broadcast
    static constexpr uint32_t kLocal = 0x08;     // no socket of its own (a client in the same process as its server)
    static constexpr uint32_t kFlag10 = 0x10;
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
    void SetValue1E88(uint32_t value) { m_unknown1E88 = value; }
    // 0x00456F90
    bool IsLocal() { return (m_flags >> 3) & 1; }
    // 0x00456FA0
    bool HasFlag10() { return (m_flags >> 4) & 1; }

    // 0x00456A90: "<name>: Error <WSAGetLastError>" to the debugger output.
    void ReportError();
    // 0x00456B00: time to send to `conn` (its send interval has passed since its last send, or it's flagged to
    // send now).
    bool IsSendDue(ConnView* conn);
    // 0x00456B40: the two connections know each other as aliases (the same player's server and client ends).
    void AliasConnections(ConnView* first, ConnView* second);
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
    void TerminateConnection(ConnView* conn);
    // 0x004573D0: the next connection of a search of the connection list (null at the end).
    ConnView* NextConnection(ConnSearch* search);
    // 0x00457400: `conn`'s address added to the banned list.
    void BanConnection(ConnView* conn);
    // 0x00457490 (virtual slot 5): the socket closed (unless local), the dispatcher cleared, every connection
    // deleted.
    void Deinit();
    // 0x00457560: dispatch the sequenced messages received ahead of their turn whose turn has come, group by
    // group. False if a handler said the connection is gone.
    bool DispatchQueuedSequencedMessages(ConnView* conn);
    // 0x00457710: the destructor body.
    void DestroyBody();
    // 0x004578B0
    ConnView* GetConnectionByAddress(uint32_t ip, uint16_t port);
    // 0x00457910
    ConnView* GetConnectionByHandle(int handle);
    // 0x00457970: an important or sequenced message is unsent or waiting for its ack longer than `conn`'s
    // resend threshold.
    bool ImportantMessagesDue(ConnView* conn);
    // 0x00457AC0: any message to send to `conn` now (a normal one whose time has come, or as above).
    bool MessagesDue(ConnView* conn);
    // 0x00457C80: the messages with id `id` taken out of one of `conn`'s queues (0 normal, 1 important,
    // 2 sequenced), a QueuedMsg deleted when its last link goes.
    void FreeMessages(ConnView* conn, int queue, uint8_t id);

    void** m_vtable;          // +0x0000
    uint32_t m_unknown4;      // +0x0004
    uint32_t m_time;          // +0x0008 the current time (ms)
    HandlerLists m_dispatcher; // +0x000C the Dispatcher (wt/netC) starts with its handler lists
    AppView* m_dispatcherApp; // +0x13F8 the Dispatcher's app
    uint32_t m_counters[6];   // +0x13FC (zeroed by Init)
    Mem::ListNode m_list1414; // +0x1414 Lst::Node<App> (vtable 0x006474D8, data = the app)
    uint32_t m_socket;        // +0x1428
    uint8_t m_localAddress[0x10]; // +0x142C sockaddr_in (Bind)
    bool m_connected;         // +0x143C
    void* m_manager;          // +0x1440 the Net::Manager
    // Tsk::Task<App>s (vtable 0x006476F0): process 0x00456CA0 (priority 0), send 0x004594A0 (-2000),
    // receive 0x004593C0 (1000), metrics 0x004593E0 (-2500).
    void* m_processTask;      // +0x1444
    void* m_sendTask;         // +0x1448
    void* m_receiveTask;      // +0x144C
    void* m_metricsTask;      // +0x1450
    int m_numConnections;     // +0x1454 (zeroed by Init)
    int m_maxConnections;     // +0x1458 (App::NewConnection makes no more)
    uint8_t m_scrambled[0x1E84 - 0x145C]; // +0x145C a secure packet being sent
    uint32_t m_flags;         // +0x1E84
    uint32_t m_unknown1E88;
    ConnNode m_connections;   // +0x1E8C head (data -1)
    BannedConnNode m_banned;  // +0x1EA0 head
    uint32_t m_unknown1EB4;
    char m_name[1];           // +0x1EB8 ("server", "client"...)
};
static_assert(offsetof(AppView, m_dispatcher) == 0x0C && offsetof(AppView, m_counters) == 0x13FC &&
                  offsetof(AppView, m_list1414) == 0x1414 && offsetof(AppView, m_socket) == 0x1428 &&
                  offsetof(AppView, m_connected) == 0x143C && offsetof(AppView, m_processTask) == 0x1444 &&
                  offsetof(AppView, m_numConnections) == 0x1454 && offsetof(AppView, m_scrambled) == 0x145C &&
                  offsetof(AppView, m_flags) == 0x1E84 && offsetof(AppView, m_connections) == 0x1E8C &&
                  offsetof(AppView, m_banned) == 0x1EA0 && offsetof(AppView, m_name) == 0x1EB8,
              "layout must match the original");

// 0x00456B30: a call through the object's vtable slot 5 (App::Deinit for an App) - a jump, its arguments left
// for the callee: the linker folded the identical non-virtual wrappers of several classes into this one.
void VirtualSlot5Jump();

} // namespace Net
