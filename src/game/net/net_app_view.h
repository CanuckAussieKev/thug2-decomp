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

// Net::Conn: one end of a connection, as an App keeps it (0x3D20 bytes).
struct ConnView {
    Mem::ListNode m_normalMsgs;     // +0x00 links: QueuedMsg* at +0x14, send time at +0x18
    Mem::ListNode m_importantMsgs;  // +0x14 links: last send time at +0x14 (0 = not sent), QueuedMsg* at +0x18,
                                    //       a time at +0x24
    Mem::ListNode m_sequencedMsgs;  // +0x28 links: last send time at +0x1C, a time at +0x24, SeqQueuedMsg* at
                                    //       +0x28
    int m_handle;                   // +0x3C
    uint32_t m_ip;                  // +0x40
    uint32_t m_unknown44;           // +0x44 (a running number)
    uint16_t m_port;                // +0x48
    uint8_t m_unknown4A[0x70 - 0x4A];
    uint32_t m_lastSendTime;        // +0x70
    uint32_t m_sendInterval;        // +0x74 (0x00455970 reads it)
    uint8_t m_unknown78[0x478 - 0x78];
    uint32_t m_sequenceNumbers[256]; // +0x478 the next sequence number expected in each group
    uint32_t m_unknown878;
    // +0x87C sequenced messages received ahead of their turn, by group: links with the group at +0x15, the
    // sequence number at +0x20, SeqQueuedMsg* at +0x28.
    Mem::ListNode m_receivedQueues[256];
    uint8_t m_unknown1C7C[0x1CA4 - 0x1C7C];
    LatencyTest m_latencyTest;      // +0x1CA4
    uint8_t m_unknown1CDC[0x3CF4 - 0x1CDC];
    bool m_sendNow;                 // +0x3CF4 (0x00455990 reads it)
    ConnNode m_appNode;             // +0x3CF8 in the App's connection list (data = the connection)
    ConnView* m_aliasConn;          // +0x3D0C
};
static_assert(offsetof(ConnView, m_handle) == 0x3C && offsetof(ConnView, m_port) == 0x48 &&
                  offsetof(ConnView, m_lastSendTime) == 0x70 && offsetof(ConnView, m_sequenceNumbers) == 0x478 &&
                  offsetof(ConnView, m_receivedQueues) == 0x87C && offsetof(ConnView, m_latencyTest) == 0x1CA4 &&
                  offsetof(ConnView, m_sendNow) == 0x3CF4 && offsetof(ConnView, m_appNode) == 0x3CF8 &&
                  offsetof(ConnView, m_aliasConn) == 0x3D0C,
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

    // 0x00456A50 / 0x00456A60 / 0x00456A70 / 0x00456A80: the app's main-loop tasks (Neversoft's are receive,
    // send, process and metrics tasks; which is which isn't established).
    void* Task1448() { return m_task1448; }
    void* Task144C() { return m_task144C; }
    void* Task1444() { return m_task1444; }
    void* Task1450() { return m_task1450; }
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
    uint32_t m_unknown13F8;
    uint32_t m_counters[6];   // +0x13FC (zeroed by Init)
    Mem::ListNode m_list1414; // +0x1414 (vtable 0x006474D8)
    uint32_t m_socket;        // +0x1428
    uint8_t m_localAddress[0x10]; // +0x142C sockaddr_in (Bind)
    bool m_connected;         // +0x143C
    void* m_manager;          // +0x1440 the Net::Manager
    void* m_task1444;         // +0x1444
    void* m_task1448;
    void* m_task144C;
    void* m_task1450;
    uint32_t m_unknown1454;   // +0x1454 (zeroed by Init)
    uint32_t m_unknown1458;
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
                  offsetof(AppView, m_connected) == 0x143C && offsetof(AppView, m_task1444) == 0x1444 &&
                  offsetof(AppView, m_unknown1454) == 0x1454 && offsetof(AppView, m_scrambled) == 0x145C &&
                  offsetof(AppView, m_flags) == 0x1E84 && offsetof(AppView, m_connections) == 0x1E8C &&
                  offsetof(AppView, m_banned) == 0x1EA0 && offsetof(AppView, m_name) == 0x1EB8,
              "layout must match the original");

// 0x00456B30: a call through the object's vtable slot 5 (App::Deinit for an App) - a jump, its arguments left
// for the callee: the linker folded the identical non-virtual wrappers of several classes into this one.
void VirtualSlot5Jump();

} // namespace Net
