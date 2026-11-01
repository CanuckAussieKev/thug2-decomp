#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/net/net_app_view.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_lists.h"

struct sockaddr;

// The network library: an App (the base of the Server and the Client) owns its connections (Conn),
// queues messages for them and streams big messages in pieces. Here: App's constructor and the App code at
// 0x00457EB0 - 0x004598C0 (net_app.cpp); App's methods 0x00456910-0x00457EB0 are in net_app_view.cpp, its message
// handling in net_app_messages.cpp.

namespace tasks {
class SchedulerTask;
template <typename Owner>
class Task;
} // namespace tasks

namespace net {

struct AppHost;

// What an App does with a packet from an address that isn't one of its connections (+0x1E88).
using UnknownPacketCallback = void(__cdecl*)(char* data, int size, sockaddr* from);

// A message to be sent (0x20 bytes; the constructor 0x004549F0 sets every field: zero, priority 0x80).
struct NetMessageInfo {
    enum Queue : int { kLaneNormal = 0, kLanePriority = 1, kLaneOrdered = 2 };

    // 0x004549F0 (net_descs.cpp)
    NetMessageInfo();

    uint8_t ident_;         // 0x00
    uint8_t feed_;          // 0x01 a piece of a streamed message (counted by Conn 0x004565E0)
    uint16_t byte_count_;   // 0x02
    void* content_;         // 0x04
    int rank_;              // 0x08 (0x80 by default)
    int queue_;             // 0x0C
    uint8_t group_id_;      // 0x10 sequenced messages: each group has its own sequence
    bool singular_;         // 0x11 drop the connection's queued messages with the same id first
    int delay_;             // 0x14 added to the app's timestamp: when the message may go
    uint32_t fixed_seq_no_; // 0x18 sequenced: this id instead of the group's next one (0 = the next)
    uint8_t compression_;   // 0x1C 0 none, 1 RLE, 2 LZSS (0x00454E30)
    uint8_t pad1d_[3];
};

// A streamed message not sent all at once (0x44 bytes, constructor 0x004549D0, destructor body 0x00454E00): sent
// in 256-byte pieces, a few each frame (AppHost::pump_stream_traffic).
struct StreamDesc {
    // 0x004549D0 (net_descs.cpp): size, data, next, group and borrow_caller_buffer zeroed.
    StreamDesc();
    // 0x00454E00: the data freed unless it's the sender's ("Destroying StreamDesc's Data").
    void destroy_body();

    int extent_;     // 0x00
    int stream_id_;  // 0x04
    uint8_t msg_id_; // 0x08
    char desc_[32];  // 0x09 the sender's description of the data
    uint8_t pad29_[3];
    uint8_t* content_;      // 0x2C
    uint8_t* succ_;         // 0x30 the first byte not sent yet
    uint8_t group_id_;      // 0x34
    uint32_t order_number_; // 0x38 the next piece's sequence id (reserved when the stream was queued)
    uint32_t checksum_;     // 0x3C received streams: the whole message's checksum
    bool borrowed_payload_; // 0x40 content_ is the caller's (not freed)
    uint8_t pad41_[3];
};

// NetLink::AppHost (vtable 0x006475B4; the Client's 0x006475CC and the Server's 0x006475EC derive from it). The
// platform side - reading the socket, sending a connection's packets - is the client's and the server's; slots 1
// and 5 (handle_incoming, tear_down_state) are the same function in every table, so they aren't virtual here.
struct AppHost {
    // From the current heap (not zeroed: the original's), freed to the memory manager.
    static void* operator new(std::size_t size) noexcept { return memory::alloc(static_cast<unsigned int>(size)); }
    static void operator delete(void* memory) { memory::dealloc(memory); }

    // 0x004594C0: the dispatcher's lists, the connection lists, the receive / process / metrics tasks, `flags`.
    explicit AppHost(int flags);
    // 0x00457710 / 0x00459660: the banned addresses and the tasks deleted, the lists unlinked, the dispatcher's.
    virtual ~AppHost();
    AppHost(const AppHost&) = delete;
    AppHost& operator=(const AppHost&) = delete;

    // Virtual slot 2: everything waiting on the socket, into the connections' read buffers.
    virtual void pull_inbound() = 0;
    // Virtual slot 3: packets to `conn` until its queues are empty or a packet is full.
    virtual void flush_outbox(Conn* conn) = 0;
    // 0x00457110 (virtual slot 4): the socket (unless local: UDP, non-blocking, broadcast if asked), the
    // counters zeroed, the dispatcher set up with the library's own handlers. False (reported) on failure.
    virtual bool prepare_all();
    // flag_bits_
    static constexpr uint32_t kBroadcast = 0x01; // the socket may broadcast
    static constexpr uint32_t kLocal = 0x08;     // no socket of its own (a client in the same process as its server)
    static constexpr uint32_t kMeasureLatency = 0x04; // send latency tests
    static constexpr uint32_t kAcceptForeign = 0x10;  // connection requests from foreign addresses are answered
    static constexpr uint32_t kSecure = 0x20;         // packets carry a checksum and are scrambled (Encrypt / Decrypt)

    // The largest packet (the receive and scramble buffers).
    static constexpr uint32_t kMaxPacketSize = 0x51C;

    // 0x00456910: `data` scrambled into `out`: a 16-bit checksum (the CRC's halves added), then each byte XORed
    // with the checksum's low byte. *out_length = length + 2.
    void encrypt(const uint8_t* data, uint8_t* out, int length, int* out_length);
    // 0x00456CC0: a received packet into `out`: false if it's shorter than 3 bytes or (secure) its checksum is
    // wrong; secure packets lose their checksum (`length` - 2 bytes copied).
    bool decrypt(const uint8_t* packet, uint8_t* out, int length);

    // 0x00456A50 / 0x00456A60 / 0x00456A70 / 0x00456A80: the app's main-loop tasks (made by AppHost::make_new and,
    // the send task, by the Server's and the Client's constructors).
    tasks::SchedulerTask* send_task() { return send_task_; }
    tasks::SchedulerTask* receive_task() { return receive_task_; }
    tasks::SchedulerTask* process_task() { return process_task_; }
    tasks::SchedulerTask* metrics_task() { return metrics_task_; }
    // 0x00456AF0
    void* net_session_slot() { return supervisor_; }
    // 0x00456B60
    uint32_t get_socket() { return sock_; }
    // 0x00456B70
    void set_outside_packet_hook(UnknownPacketCallback handler) { unknown_packet_sink_ = handler; }
    // 0x00456F90
    bool is_host_local() { return (flag_bits_ >> 3) & 1; }
    // 0x00456FA0
    bool allows_outside_peers() { return (flag_bits_ >> 4) & 1; }

    // 0x00456A90: "<name>: Error <WSAGetLastError>" to the debugger output.
    void raise_fault();
    // 0x00456B00: time to send to `conn` (its send interval has passed since its last send, or it's flagged to
    // send now).
    bool is_send_due(Conn* conn);
    // 0x00456B40: the two connections know each other as aliases (the same player's server and client ends).
    void link_duplicate_conns(Conn* first, Conn* second);
    // 0x00456BA0: bind the socket to `port` on any address (if it's taken: report, and take any free port), and
    // note the address it got. (`address` isn't used.) False (reported) on failure.
    bool get_bind(uint32_t address, uint16_t port);
    // 0x00456D90: a new SeqQueuedMsg (null if out of memory).
    SeqQueuedMsg* new_queued_msg(uint8_t id, uint16_t length, void* data, uint8_t arg4, uint8_t arg5);
    // 0x00456E20: a datagram to ip:port (scrambled first if secure). True if sent or it would block.
    bool transmit_to(uint32_t ip, uint16_t port, const uint8_t* data, int length, int flags);
    // 0x00456F10: a packet on the connected socket (scrambled first if secure). False if not connected or
    // send failed.
    bool post_out(const uint8_t* data, int length, int flags);
    // 0x00457280: a message (id, 16-bit length, data) as one datagram to ip:port.
    bool deliver_to(uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port, int flags);
    // 0x00457300: `conn` taken out of the connection list and deleted.
    void sever_link(Conn* conn);
    // 0x004573D0: the next connection of a search of the connection list (null at the end).
    Conn* step_conn_walk(ConnSearch* search);
    // 0x00457400: `conn`'s address added to the banned list.
    void block_connection(Conn* conn);
    // 0x00457490 (slot 5 of every app's table; the client's and the server's 0x0045B820 is a jump to it): the
    // socket closed (unless local), the dispatcher cleared, every connection deleted.
    void tear_down_state();
    // 0x00457560: dispatch the sequenced messages received ahead of their turn whose turn has come, group by
    // group. False if a handler said the connection is gone.
    bool dispatch_queued_sequenced_messages(Conn* conn);
    // 0x004578B0
    Conn* find_conn_at_address(uint32_t ip, uint16_t port);
    // 0x00457910
    Conn* find_conn_with_id(int handle);
    // 0x00457970: an important or sequenced message is unsent or waiting for its ack longer than `conn`'s
    // resend threshold.
    bool important_messages_due(Conn* conn);
    // 0x00457AC0: any message to send to `conn` now (a normal one whose time has come, or as above).
    bool messages_due(Conn* conn);
    // 0x00457C80: the messages with id `id` taken out of one of `conn`'s queues (0 normal, 1 important,
    // 2 sequenced), a PendingMsg deleted when its last link goes.
    void free_messages(Conn* conn, int queue, uint8_t id);

    // 0x00457EB0: queue `desc` for `handle`'s connection, for every ready one (0xFF), or every one but
    // handle & 0x7F (handle with 0x80 set).
    void put_on_outbox(int handle, NetMessageInfo* desc);
    // 0x00458740: send `size` bytes in pieces (a start message with the first 256, then data messages).
    // queue_all_pieces: queue every piece now; else a few each frame (the data copied unless borrow_caller_buffer).
    void send_bulk_to_conn(Conn* conn, uint8_t msg_id, uint16_t size, void* data, const char* desc, uint8_t group_id,
                           bool queue_all_pieces, bool borrow_caller_buffer);
    // 0x00458AB0: the same for `handle`'s connection or every valid one (0xFF).
    void send_bulk_message(int handle, uint8_t msg_id, uint16_t size, void* data, const char* desc, uint8_t group_id,
                           bool queue_all_pieces, bool borrow_caller_buffer);
    // 0x00458BE0
    Conn* begin_conn_walk(ConnSearch* search);
    // 0x00458C10: flush_outbox(conn) (virtual slot 3) for each connection.
    void flush_outbox();
    // 0x00458CA0: !send_in_frame: advance the timestamp and make every connection send this frame.
    void transmit(bool send_in_frame);
    // 0x00458D50: whether conn's address is banned.
    bool is_conn_blocked(Conn* conn);
    // 0x00458DB0: the outgoing bytes of the last second over the remote connections.
    int fetch_traffic_spent();
    // 0x00458E60
    bool over_bandwidth_budget(Conn* conn);
    // 0x00458F80: the lowest handle (1-255) no connection has; 0 if none is free.
    int fetch_unused_slot_id();
    // 0x00458FB0: invalidate disconnecting connections with nothing left to send; delete invalid ones with
    // nothing left to read or send.
    void drop_broken_links();
    // 0x004590C0: send the next pieces of the streamed messages.
    void pump_stream_traffic();
    // 0x00459690 (virtual slot 1): local connections' data handed over, then each connection's messages
    // dispatched. False if processing should stop.
    bool handle_incoming();
    // 0x004597C0: a new connection to ip:port (null when there are already the maximum).
    Conn* opened_link(int ip, uint16_t port, int flags);

    // Task callbacks (the task's +0x1C is the app).
    // 0x00456CA0: counts the frame (+0x1410), then handle_incoming (virtual slot 1; a tail jump).
    static void __cdecl pump_incoming_traffic(tasks::Task<AppHost>* task);
    // 0x004593C0: timestamp, dead connections, pull_inbound (virtual slot 2).
    static void __cdecl receive_network_data(tasks::Task<AppHost>* task);
    // 0x004593E0: each connection's bandwidth meters (in and out).
    static void __cdecl network_metrics(tasks::Task<AppHost>* task);
    // 0x004594A0: stream pieces, then Transmit(true).
    static void __cdecl flush_outgoing_traffic(tasks::Task<AppHost>* task);

    // 0x004598C0 - 0x0045B2F0 (net_app_messages.cpp)
    // 0x004598C0: the messages of one queue (0 normal, 1 important, 2 sequenced) that are due go into the
    // connection's packet; false if one didn't fit. Normal messages are dequeued as they go; important and
    // sequenced ones stay until acked (`resends_only`: only those sent before whose ack is overdue).
    bool bundle_messages(Conn* conn, int queue, bool resends_only);

    // Handlers (registered by AppHost::prepare_all, 0x00457110).
    // 0x0045AD60 (id 1): answers a latency test with its time.
    static int __cdecl on_ping_probe_msg(InboundMsgCtx* context);
    // 0x0045AB60 (id 2): the answer to our latency test: the round trip goes into the latency average.
    static int __cdecl on_ping_reply_msg(InboundMsgCtx* context);
    // 0x0045AF10 (id 7): a sequenced message, buffered in its group's list by sequence number (to be handled
    // in order); acks are sent for the packet.
    static int __cdecl on_ordered_msgs(InboundMsgCtx* context);
    // 0x0045B0B0 (id 8): an ack: the important and sequenced messages sent with that timestamp are done; the
    // round trip is a latency measurement.
    static int __cdecl on_receipt_msg(InboundMsgCtx* context);
    // 0x0045ABF0 (id 0x12): a compressed message (method 1 or 2; NOT ORIGINAL: 3, THUGPRO's LZHAM), expanded and
    // dispatched. NOT ORIGINAL: every length in it bounded (short messages, streams ending inside a code, an LZSS
    // stream expanding past its size, an inner length past the data, unknown methods: dropped).
    static int __cdecl handle_compressed_messages(InboundMsgCtx* context);
    // 0x0045B2F0 (ids 0xE, 0xF): a stream's start and its pieces; the whole message is dispatched when complete.
    static int __cdecl drain_stream_msgs(InboundMsgCtx* context);

    uint32_t unknown4_;       // +0x0004
    uint32_t clock_;          // +0x0008 the current time (ms)
    Router dispatcher_;       // +0x000C (wt/netC)
    AppHost* dispatcher_app_; // +0x13F8 the Dispatcher's app
    // +0x13FC statistics (zeroed by Init)
    int outbound_byte_total_;     // +0x13FC
    int inbound_byte_total_;      // +0x1400
    int skipped_stamps_;          // +0x1404 timestamps skipped over
    int stale_arrivals_;          // +0x1408 timestamps older than the newest
    int repeat_arrivals_;         // +0x140C timestamps seen twice
    int frame_count_;             // +0x1410 frames processed (pump_incoming_traffic)
    memory::ListNode list1414_;   // +0x1414 lists::Elem<App> (data = the app)
    uint32_t sock_;               // +0x1428
    uint8_t local_address_[0x10]; // +0x142C sockaddr_in (Bind)
    bool linked_up_;              // +0x143C the socket is connect()ed (send/recv, not sendto/recvfrom)
    void* supervisor_;            // +0x1440 the NetLink::Hub (its message flags: kMsgNoLength)
    // tasks::Task<App>s (vtable 0x006476F0): process 0x00456CA0 (priority 0), send 0x004594A0 (-2000),
    // receive 0x004593C0 (1000), metrics 0x004593E0 (-2500).
    tasks::SchedulerTask* process_task_; // +0x1444
    tasks::SchedulerTask* send_task_;    // +0x1448
    tasks::SchedulerTask* receive_task_; // +0x144C
    tasks::SchedulerTask* metrics_task_; // +0x1450
    int open_links_;                     // +0x1454 (zeroed by Init)
    int peer_cap_;                       // +0x1458 (AppHost::opened_link makes no more; HostEnd::prepare_all clears it)
    uint8_t scrambled_[0x514];           // +0x145C a secure packet being sent
    char receive_buffer_[0x514];         // +0x1970
    uint32_t flag_bits_;                 // +0x1E84
    UnknownPacketCallback unknown_packet_sink_; // +0x1E88 packets from addresses that aren't connections
    memory::ListNode circuits_;                 // +0x1E8C head (data -1)
    memory::ListNode banned_;                   // +0x1EA0 head
    uint32_t unknown1eb4_;
    char label_[0x48]; // +0x1EB8 ("server", "client"...)
    // +0x1F00 a double (0.0 from the constructor; not used by the library), as two words: an 8-aligned member would
    // move MSVC's first member after the vptr to +8.
    uint32_t unknown1f00_[2];
};

// NOT ORIGINAL - a look at every message AppHost::put_on_outbox queues, first (THUGPRO's additions to messages we send:
// engine/net/thugpro_peer.h). The filter may queue other messages itself (through put_on_outbox) and returns true
// when it has queued this one itself (put_on_outbox then does nothing more). Null (the default): the original.
using EnqueueFilter = bool (*)(AppHost* app, int handle, NetMessageInfo* desc);
void set_enqueue_filter(EnqueueFilter filter);

} // namespace net
