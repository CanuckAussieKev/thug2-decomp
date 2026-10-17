#pragma once

// What 0x4598C0-0x45BDA0 (Client, Server, the message dispatcher, App's message handling) uses of the rest of
// the net library: flags and sizes, message ids, short names for App's and Conn's calls, and the message
// classes' functions not decompiled yet by address. (App: net_app.h; Conn: net_conn.h; links: net_messages.h.)

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_messages.h"
#include "hook/game.h"

namespace Net {

// An object's field by offset (the tests' stand-in objects use it with offsetof).
template <typename T, typename O>
T& Field(O* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(object) + offset);
}

// The network manager (Net::Manager, App +0x1440) keeps a flags byte per message id (at +0x28).
// Bit 0: the message is sent without its length (fixed size).
constexpr uint8_t kMsgNoLength = 1;

// (The App's fields: net_app.h; the Client's server address: net_client.h.)

// App flags (+0x1E84).
constexpr uint32_t kAppMeasureLatency = 0x4;   // send latency tests
constexpr uint32_t kAppAcceptForeign = 0x10;   // (0x00456FA0)
constexpr uint32_t kAppSecure = 0x20;          // checksummed packets: two bytes fewer counted

constexpr uint32_t kMaxPacketSize = 0x512;     // a packet being built
constexpr uint32_t kReceiveBufferSize = 0x514;
constexpr uint32_t kConnReceiveBufferSize = 0xA28;

// (The connection's fields: net_conn.h.)

// Connection status (+0x60).
constexpr uint32_t kConnStatus1 = 0x1;     // (acks only measure latency with it; MarkNewConnection clears it)
constexpr uint32_t kConnDisconnAccepted = 0x8;

// Queued messages: QueuedMsg and SeqQueuedMsg (net_app_view.h).

// The queues' links and the stream link: net_messages.h.

// (Net::MsgDesc: net_app.h)

constexpr int kQueueNormal = 0;
constexpr int kQueueImportant = 1;
constexpr int kQueueSequenced = 2;
constexpr int kHighestPriority = 0xFF;

// Message ids.
constexpr uint8_t kMsgLatencyTest = 1;
constexpr uint8_t kMsgLatencyResponse = 2;
constexpr uint8_t kMsgConnectionRequest = 3;
constexpr uint8_t kMsgConnectionAccepted = 4;
constexpr uint8_t kMsgConnectionRefused = 5;
constexpr uint8_t kMsgConnectionTerminated = 6;
constexpr uint8_t kMsgSequenced = 7;
constexpr uint8_t kMsgAck = 8;
constexpr uint8_t kMsgTimestamp = 9;
constexpr uint8_t kMsgDisconnRequest = 0xB;
constexpr uint8_t kMsgDisconnAccepted = 0xC;
constexpr uint8_t kMsgStreamStart = 0xE;
constexpr uint8_t kMsgStreamData = 0xF;
constexpr uint8_t kMsgCompressed = 0x12;

// The longest round trip counted as latency (0x00647588: 2000 ms).
inline uint32_t MaxLatency() { return GameVariable<uint32_t>(0x00647588); }

// --- Functions of other classes, by address ---

// MsgDesc (0x004549F0)
inline void ConstructMsgDesc(MsgDesc* desc) { GameFunction<MsgDesc*(__thiscall*)(MsgDesc*)>(0x004549F0)(desc); }

// App (wt/netA's App and wt/netB's App, net_app.h): short names for the calls this range makes
inline void EnqueueMessage(App* app, int handle, MsgDesc* desc) { app->EnqueueMessage(handle, desc); }
// Whether this frame sends to the connection at all (its send interval has passed, or it's flagged).
inline bool ShouldSendThisFrame(App* app, Conn* conn) { return app->IsSendDue(conn); }
// Anything to send to the connection now.
inline bool MessagesToSend(App* app, Conn* conn) { return app->MessagesDue(conn); }
inline bool BandwidthExceeded(App* app, Conn* conn) { return app->BandwidthExceeded(conn); }
// An important or sequenced message is due (unsent, or overdue for its ack): the client sends a timestamp
// with the packet so it gets acked.
inline bool TimestampDue(App* app, Conn* conn) { return app->ImportantMessagesDue(conn); }
// A packet to ip:port (sendto) / on the connected socket.
inline bool SendTo(App* app, uint32_t ip, uint16_t port, char* data, int size, int flags)
{
    return app->SendTo(ip, port, reinterpret_cast<const uint8_t*>(data), size, flags);
}
inline bool Send(App* app, char* data, int size, int flags)
{
    return app->Send(reinterpret_cast<const uint8_t*>(data), size, flags);
}
// App flag 0x8: the App has no socket of its own (nothing to receive).
inline bool HasNoSocket(App* app) { return app->IsLocal(); }
// App flag 0x10: connection requests from foreign addresses are answered.
inline bool AcceptsForeignConnections(App* app) { return app->AcceptsForeignConnections(); }
inline Conn* GetConnectionByAddress(App* app, uint32_t ip, uint16_t port)
{
    return app->GetConnectionByAddress(ip, port);
}
inline Conn* NewConnection(App* app, uint32_t ip, uint16_t port, int flags)
{
    return app->NewConnection(static_cast<int>(ip), port, flags);
}
// A received packet into the connection's read buffer (false: not valid; secure packets lose their checksum).
inline bool ReadPacket(App* app, char* packet, char* to, int size)
{
    return app->Decrypt(reinterpret_cast<const uint8_t*>(packet), reinterpret_cast<uint8_t*>(to), size);
}
// A message straight to ip:port, not through a connection.
inline void SendMessageTo(App* app, uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port,
                          int flags)
{
    app->SendMessageTo(id, length, data, ip, port, flags);
}
inline void TerminateConnection(App* app, Conn* conn) { app->TerminateConnection(conn); }
inline void ReportSocketError(App* app) { app->ReportError(); }
inline uint8_t* GetManager(App* app) { return static_cast<uint8_t*>(app->NetManager()); }
// Net::Manager's flags for a message id (0x00454980).
inline uint8_t MessageFlags(uint8_t* manager, uint8_t id)
{
    return GameFunction<uint8_t(__thiscall*)(uint8_t*, uint32_t)>(0x00454980)(manager, id);
}

// Conn's methods (net_conn.cpp). Its flags (+0x64): 1 local (the App's own client/server in this process),
// 2 remote, 4 foreign (created for a packet from an unknown address: NewConnection's flags 6).
inline bool IsRemote(Conn* conn) { return conn->IsRemote(); }
inline bool IsForeign(Conn* conn) { return conn->IsForeign(); }
inline bool IsLocal(Conn* conn) { return conn->IsLocal(); }
// Status bit 2 clear: not invalidated (MarkNewConnection = Conn::Invalidate hasn't been called).
inline bool IsNew(Conn* conn) { return conn->IsValid(); }
// Status: bit 0 off, bit 2 on (Conn::Invalidate)
inline void MarkNewConnection(Conn* conn) { conn->Invalidate(); }
// How long an important message waits for its ack before going again.
inline uint32_t GetResendThreshold(Conn* conn) { return conn->GetResendThreshold(); }
inline void SetForceSendThisFrame(Conn* conn, bool on) { conn->SetForceSendThisFrame(on); }
inline int GetBandwidth(Conn* conn) { return conn->GetBandwidth(); }
inline void SetBandwidth(Conn* conn, int bandwidth) { conn->SetBandwidth(bandwidth); }
// The most times one important/sequenced message has been resent (Conn::GetHighestResendCount).
inline int GetNumPendingMessages(Conn* conn) { return conn->GetHighestResendCount(); }
// A send failed: the messages that went with this packet's timestamp go again.
inline void FlagMessagesForResending(Conn* conn, uint32_t timestamp) { conn->FlagMessagesForResending(timestamp); }
// The connection's destructor.
inline void DestroyConn(Conn* conn) { conn->Destroy(); }
// The connection's latency test takes a measurement.
inline void AddLatency(Conn* conn, uint32_t latency) { conn->m_latencyTest.InputLatencyValue(static_cast<int>(latency)); }
// Message/packet metrics (0x00454550, 0x00454D90).
inline void AddMessageMetric(void* metrics, uint8_t id, uint32_t size)
{
    GameFunction<void(__thiscall*)(void*, uint32_t, uint32_t)>(0x00454550)(metrics, id, size);
}
inline void AddPacketMetric(void* metrics, uint32_t size, uint32_t time)
{
    GameFunction<void(__thiscall*)(void*, uint32_t, uint32_t)>(0x00454D90)(metrics, size, time);
}

// Queued messages: the destructor frees the data (0x004550F0).
inline void DestroyQueuedMsg(void* msg) { static_cast<QueuedMsg*>(msg)->DestroyBody(); }
inline SeqQueuedMsg* ConstructSeqMsg(void* memory, uint8_t id, uint16_t length, char* data, uint8_t group, bool compressed)
{
    return static_cast<SeqQueuedMsg*>(memory)->Construct(id, length, data, group, compressed);
}
inline MsgSeqLink* ConstructMsgSeqLink(void* memory, SeqQueuedMsg* msg)
{
    return static_cast<MsgSeqLink*>(memory)->Construct(msg);
}
inline StreamDesc* ConstructStreamDesc(void* memory) { return GameFunction<StreamDesc*(__thiscall*)(void*)>(0x004549D0)(memory); }
inline StreamLink* ConstructStreamLink(void* memory, StreamDesc* desc)
{
    return static_cast<StreamLink*>(memory)->Construct(desc);
}
inline void DestroyStreamDesc(StreamDesc* desc) { GameFunction<void(__thiscall*)(StreamDesc*)>(0x00454E00)(desc); }

// Lst::Node<T>'s deleting destructor (each node type's vtable slot 0).
inline void DeleteNode(void* node)
{
    auto* vtable = *static_cast<void***>(node);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(node, 1);
}

} // namespace Net
