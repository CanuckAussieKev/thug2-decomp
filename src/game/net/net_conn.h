#pragma once

// Net::Conn (Neversoft's Net library): one end of a connection - its outgoing message queues, the sequenced
// messages and streams it has received, its buffers, latency and bandwidth; and the list types of its queues
// (Lst::Node<T> / Lst::Head<T> / Lst::Search<T> for the message links and the stream links) with the
// destructors and constructors in Conn's address range (0x00455970-0x00456910).

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/net/net_latency.h"
#include "game/net/net_lists.h"
#include "game/net/net_messages.h"

namespace Net {

struct App;

// (The message lists' node and link vtables - the heads Conn's constructor makes use the links' own, folded -
// and node types: net_messages.h.)

// Lst::Head<MsgSeqLink> (Conn's sequenced queue and received queues).
struct MsgSeqLinkHead : Mem::ListNode {
    // 0x00455C60: the constructor (the received queues', through the compiler's array helper): an empty list
    // (data -1, priority 0, linked to itself).
    MsgSeqLinkHead* Construct();
};
// The same for the other heads Conn's constructor makes (inline there).
void InitConnListHead(Mem::ListNode* head, void** vtable);

// A local Lst::Search<T> over one of Conn's lists: its vtable (only a virtual destructor, which resets it).
template <uint32_t SearchVtable>
struct ConnListSearch {
    void** m_vtable;
    Mem::ListNode* m_node;

    void DestroyBody();
    // The scalar deleting destructor.
    ConnListSearch* Destroy(unsigned int flags);
};
using MsgSeqLinkSearch = ConnListSearch<0x00647564>; // 0x004559D0 / 0x00455A80
using MsgLinkSearch = ConnListSearch<0x00647568>;    // 0x004559E0 / 0x00455AB0
using MsgImpLinkSearch = ConnListSearch<0x0064756C>; // 0x004559F0 / 0x00455AE0
using StreamLinkSearch = ConnListSearch<0x00647570>; // 0x00455A00 / 0x00455B10

// Net::Conn: one end of a connection, as an App keeps it (0x3D20 bytes). Its methods: net_conn.cpp.
// (Neversoft's Net::Conn; formerly `Conn`, a view of its fields at their offsets.)
struct Conn {
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
    Conn* Construct(int flags);
    // 0x00456790 (the destructor)
    void Destroy();

    // 0x00455920 (net_conn_times.cpp): the last communication time moved to now + `extra` ms, unless it's
    // later already.
    void UpdateCommTime(uint32_t extra);
    // 0x00455940 (net_conn_times.cpp): milliseconds since the last communication (0 if that's in the future).
    uint32_t GetTimeElapsedSinceCommunication();
    // 0x00455960
    void SetSendInterval(uint32_t interval) { m_sendInterval = interval; }
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
    char* m_writeBuffer;            // +0x4C the packet being built (0x514 bytes): its start
    char* m_writePtr;               // +0x50 and end
    char* m_readBuffer;             // +0x54 received data (0xA28 bytes): its start
    char* m_readPtr;                // +0x58 and end
    uint32_t m_unknown5C;           // +0x5C (never touched by the library)
    uint32_t m_status;              // +0x60 kStatus...
    uint32_t m_flags;               // +0x64 kFlag...
    App* m_app;                     // +0x68 the App (set by App::NewConnection)
    uint32_t m_lastCommTime;        // +0x6C the last communication (App::NewConnection: the creation time)
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
    Conn* m_aliasConn;          // +0x3D0C
    int m_bandwidthType;            // +0x3D10
    uint32_t m_lastTimestampIn;     // +0x3D14
    uint32_t m_timestampOut;        // +0x3D18
    int m_bandwidth;                // +0x3D1C bytes per second
};
static_assert(offsetof(Conn, m_handle) == 0x3C && offsetof(Conn, m_port) == 0x48 &&
                  offsetof(Conn, m_writeBuffer) == 0x4C && offsetof(Conn, m_status) == 0x60 &&
                  offsetof(Conn, m_lastSendTime) == 0x70 && offsetof(Conn, m_nextSequenceIds) == 0x78 &&
                  offsetof(Conn, m_sequenceNumbers) == 0x478 && offsetof(Conn, m_nextStreamId) == 0x878 &&
                  offsetof(Conn, m_receivedQueues) == 0x87C && offsetof(Conn, m_receivedStreams) == 0x1C7C &&
                  offsetof(Conn, m_sendStreams) == 0x1C90 && offsetof(Conn, m_latencyTest) == 0x1CA4 &&
                  offsetof(Conn, m_receiveMetrics) == 0x1CDC && offsetof(Conn, m_sendMetrics) == 0x2CE8 &&
                  offsetof(Conn, m_sendNow) == 0x3CF4 && offsetof(Conn, m_appNode) == 0x3CF8 &&
                  offsetof(Conn, m_aliasConn) == 0x3D0C && offsetof(Conn, m_bandwidthType) == 0x3D10 &&
                  offsetof(Conn, m_bandwidth) == 0x3D1C && sizeof(Conn) == 0x3D20,
              "layout must match the original");

} // namespace Net
