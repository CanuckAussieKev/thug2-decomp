#pragma once

// What 0x4598C0-0x45BDA0 (Client, Server, the message dispatcher, App's message handling) uses of the rest of
// the net library: flags and sizes, message ids, short names for App's and Conn's calls, and the message
// classes' functions not decompiled yet by address. (App: net_app.h; Conn: net_conn.h; links: net_messages.h.)

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/net/net_app.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_manager.h"
#include "engine/net/net_metrics.h"
#include "engine/net/net_messages.h"
#include <gsl/pointers>

namespace net {

// The network manager (NetLink::Hub, App +0x1440) keeps a flags byte per message id (at +0x28).
// Bit 0: the message is sent without its length (fixed size).
constexpr uint8_t kMsgNoLength = 1;

// (The App's fields: net_app.h; the Client's server address: net_client.h.)

// App flags (+0x1E84).
constexpr uint32_t kAppMeasureLatency = 0x4; // send latency tests
constexpr uint32_t kAppAcceptForeign = 0x10; // (0x00456FA0)
constexpr uint32_t kAppSecure = 0x20;        // checksummed packets: two bytes fewer counted

constexpr uint32_t kMaxPacketSize = 0x512; // a packet being built
constexpr uint32_t kReceiveBufferSize = 0x514;
constexpr uint32_t kConnReceiveBufferSize = 0xA28;

// (The connection's fields: net_conn.h.)

// Connection status (+0x60).
constexpr uint32_t kConnStatus1 = 0x1; // (acks only measure latency with it; mark_new_connection clears it)
constexpr uint32_t kConnDisconnAccepted = 0x8;

// Queued messages: PendingMsg and SeqQueuedMsg (net_app_view.h).

// The queues' links and the stream link: net_messages.h.

// (NetLink::NetMessageInfo: net_app.h)

constexpr int kQueueNormal = 0;
constexpr int kLanePriority = 1;
constexpr int kLaneOrdered = 2;
constexpr int kTopRank = 0xFF;

// Message ids.
constexpr uint8_t kMsgLatencyTest = 1;
constexpr uint8_t kMsgLatencyResponse = 2;
constexpr uint8_t kMsgConnectionRequest = 3;
constexpr uint8_t kMsgConnectionAccepted = 4;
constexpr uint8_t kMsgConnectionRefused = 5;
constexpr uint8_t kMsgConnectionTerminated = 6;
constexpr uint8_t kMsgSequenced = 7;
constexpr uint8_t kMsgAck = 8;
constexpr uint8_t kMsgClockStamp = 9;
constexpr uint8_t kMsgDisconnRequest = 0xB;
constexpr uint8_t kMsgDisconnAccepted = 0xC;
constexpr uint8_t kMsgBulkBegin = 0xE;
constexpr uint8_t kMsgBulkChunk = 0xF;
constexpr uint8_t kMsgCompressed = 0x12;

// The longest round trip counted as latency (0x00647588: 2000 ms).
extern uint32_t g_max_latency;
inline uint32_t max_latency() { return g_max_latency; }

// --- Functions of other classes, by address ---

// NetMessageInfo (0x004549F0)
inline void construct_msg_desc(NetMessageInfo* desc) { *desc = NetMessageInfo(); }

// App (wt/netA's App and wt/netB's App, net_app.h): short names for the calls this range makes
inline void put_on_outbox(AppHost* app, int handle, NetMessageInfo* desc) { app->put_on_outbox(handle, desc); }
// Whether this frame sends to the connection at all (its send interval has passed, or it's flagged).
inline bool transmit_due_now(AppHost* app, Conn* conn) { return app->is_send_due(conn); }
// Anything to send to the connection now.
inline bool outgoing_backlog(AppHost* app, Conn* conn) { return app->messages_due(conn); }
inline bool over_bandwidth_budget(AppHost* app, Conn* conn) { return app->over_bandwidth_budget(conn); }
// An important or sequenced message is due (unsent, or overdue for its ack): the client sends a timestamp
// with the packet so it gets acked.
inline bool timestamp_due(AppHost* app, Conn* conn) { return app->important_messages_due(conn); }
// A packet to ip:port (sendto) / on the connected socket.
inline bool transmit_to(AppHost* app, uint32_t ip, uint16_t port, const uint8_t* data, int size, int flags)
{
    return app->transmit_to(ip, port, data, size, flags);
}
inline bool post_out(AppHost* app, const uint8_t* data, int size, int flags) { return app->post_out(data, size, flags); }
// App flag 0x8: the App has no socket of its own (nothing to receive).
inline bool has_no_socket(AppHost* app) { return app->is_host_local(); }
// App flag 0x10: connection requests from foreign addresses are answered.
inline bool allows_outside_peers(AppHost* app) { return app->allows_outside_peers(); }
inline Conn* find_conn_at_address(AppHost* app, uint32_t ip, uint16_t port)
{
    return app->find_conn_at_address(ip, port);
}
inline Conn* opened_link(AppHost* app, uint32_t ip, uint16_t port, int flags)
{
    return app->opened_link(static_cast<int>(ip), port, flags);
}
// A received packet into the connection's read buffer (false: not valid; secure packets lose their checksum).
inline bool pull_datagram(AppHost* app, uint8_t* packet, uint8_t* to, int size) { return app->decrypt(packet, to, size); }
// A message straight to ip:port, not through a connection.
inline void deliver_to(AppHost* app, uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port,
                       int flags)
{
    app->deliver_to(id, length, data, ip, port, flags);
}
inline void sever_link(AppHost* app, Conn* conn) { app->sever_link(conn); }
inline void report_socket_error(AppHost* app) { app->raise_fault(); }
inline Hub* fetch_hub(AppHost* app) { return app->net_session_slot(); }
// NetLink::Hub's flags for a message id (0x00454980).
inline uint8_t message_flags(Hub* manager, uint8_t id) { return static_cast<uint8_t>(manager->get_msg_options(id)); }

// Conn's methods (net_conn.cpp). Its flags (+0x64): 1 local (the App's own client/server in this process),
// 2 remote, 4 foreign (created for a packet from an unknown address: opened_link's flags 6).
inline bool is_off_machine(Conn* conn) { return conn->is_off_machine(); }
inline bool is_outsider(Conn* conn) { return conn->is_outsider(); }
inline bool is_host_local(Conn* conn) { return conn->is_host_local(); }
// Status bit 2 clear: not invalidated (mark_new_connection = Conn::mark_stale hasn't been called).
inline bool is_new(Conn* conn) { return conn->is_sound(); }
// Status: bit 0 off, bit 2 on (Conn::mark_stale)
inline void mark_new_connection(Conn* conn) { conn->mark_stale(); }
// How long an important message waits for its ack before going again.
inline uint32_t get_retry_delay(Conn* conn) { return conn->get_retry_delay(); }
inline void force_send_now(Conn* conn, bool on) { conn->force_send_now(on); }
inline int fetch_throughput(Conn* conn) { return conn->fetch_throughput(); }
inline void change_throughput(Conn* conn, int bandwidth) { conn->change_throughput(bandwidth); }
// The most times one important/sequenced message has been resent (Conn::get_highest_resend_count).
inline int get_num_pending_messages(Conn* conn) { return conn->get_highest_resend_count(); }
// A send failed: the messages that went with this packet's timestamp go again.
inline void mark_messages_for_retry(Conn* conn, uint32_t timestamp) { conn->mark_messages_for_retry(timestamp); }
// The connection's destructor.
inline void destroy_conn(Conn* conn) { conn->~Conn(); }
// The connection's latency test takes a measurement.
inline void add_latency(Conn* conn, uint32_t latency)
{
    conn->ping_probe_.input_delay_amount(static_cast<int>(latency));
}
// Message/packet metrics (0x00454550, 0x00454D90).
inline void add_message_metric(void* metrics, uint8_t id, uint32_t size)
{
    static_cast<Gauges*>(metrics)->append_note(id, static_cast<int>(size));
}
inline void add_packet_metric(void* metrics, uint32_t size, uint32_t time)
{
    static_cast<Gauges*>(metrics)->count_packet(static_cast<int>(size), static_cast<int>(time));
}

// Queued messages: the destructor frees the data (0x004550F0).
inline void destroy_queued_msg(void* msg) { static_cast<PendingMsg*>(msg)->~PendingMsg(); }
inline gsl::owner<SeqQueuedMsg*> construct_seq_msg(void* memory, uint8_t id, uint16_t length, const uint8_t* data, uint8_t group,
                                       bool compressed)
{
    return new (memory) SeqQueuedMsg(id, length, data, group, compressed);
}
inline gsl::owner<OrderedMsgNode*> construct_msg_seq_link(void* memory, SeqQueuedMsg* msg)
{
    return new (memory) OrderedMsgNode(msg);
}
inline gsl::owner<StreamDesc*> construct_stream_desc(void* memory) { return new (memory) StreamDesc; }
inline gsl::owner<BulkStreamNode*> construct_stream_link(void* memory, StreamDesc* desc)
{
    return new (memory) BulkStreamNode(desc);
}
inline void destroy_stream_desc(StreamDesc* desc) { desc->~StreamDesc(); }

} // namespace net
