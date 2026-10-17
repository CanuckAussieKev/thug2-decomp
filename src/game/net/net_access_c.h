#pragma once

// What 0x4598C0-0x45BDA0 (Client, Server, the message dispatcher, App's message handling) uses of the rest of
// the net library: App (net_app.h) and Conn (ConnView, net_app_view.h) fields they don't name yet, by offset;
// the queue links' and stream link's layouts; message ids; short names for App's calls; and Conn's / the
// message classes' functions (0x00454xxx-0x00456xxx, not decompiled yet) by address.

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_messages.h"
#include "hook/game.h"

namespace Net {

// (Net::App: net_app.h; Net::Conn: ConnView, net_app_view.h - the fields they don't name by offset below.)

template <typename T, typename O>
T& Field(O* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(object) + offset);
}

// The network manager (Net::Manager, App +0x1440) keeps a flags byte per message id (at +0x28).
// Bit 0: the message is sent without its length (fixed size).
constexpr uint8_t kMsgNoLength = 1;

// The App's fields.
namespace AppField {
constexpr uint32_t kTimestamp = 0x8;           // the frame time (milliseconds)
constexpr uint32_t kDispatcher = 0xC;          // Dispatcher
constexpr uint32_t kTotalBytesOut = 0x13FC;
constexpr uint32_t kTotalBytesIn = 0x1400;
constexpr uint32_t kLostPackets = 0x1404;      // timestamps skipped over
constexpr uint32_t kLatePackets = 0x1408;      // timestamps older than the newest
constexpr uint32_t kDupePackets = 0x140C;      // timestamps seen twice
constexpr uint32_t kSocket = 0x1428;
constexpr uint32_t kConnected = 0x143C;        // bool: the socket is connect()ed (send/recv, not sendto/recvfrom)
constexpr uint32_t kManager = 0x1440;          // Net::Manager (its message flags: kMsgNoLength)
constexpr uint32_t kTask = 0x1448;             // Tsk::Task: the App's per-frame work
constexpr uint32_t kServerUnknown1458 = 0x1458; // cleared by Server::Init
constexpr uint32_t kReceiveBuffer = 0x1970;    // 0x514 bytes
constexpr uint32_t kFlags = 0x1E84;
constexpr uint32_t kForeignPacketHandler = 0x1E88; // void (*)(char* data, int size, sockaddr* from)
constexpr uint32_t kConnections = 0x1E8C;      // Lst::Head<Conn>
constexpr uint32_t kServerAddress = 0x1F08;    // sockaddr_in (Client::ConnectToServer)
} // namespace AppField

// App flags (+0x1E84).
constexpr uint32_t kAppMeasureLatency = 0x4;   // send latency tests
constexpr uint32_t kAppAcceptForeign = 0x10;   // (0x00456FA0)
constexpr uint32_t kAppSecure = 0x20;          // checksummed packets: two bytes fewer counted

constexpr uint32_t kMaxPacketSize = 0x512;     // a packet being built
constexpr uint32_t kReceiveBufferSize = 0x514;
constexpr uint32_t kConnReceiveBufferSize = 0xA28;

// The connection's fields.
namespace ConnField {
constexpr uint32_t kNormalQueue = 0x0;         // Lst::Head<MsgNormalLink>
constexpr uint32_t kImportantQueue = 0x14;     // Lst::Head<MsgImpLink>
constexpr uint32_t kSequencedQueue = 0x28;     // Lst::Head<MsgSeqLink>
constexpr uint32_t kHandle = 0x3C;
constexpr uint32_t kIp = 0x40;
constexpr uint32_t kPort = 0x48;               // uint16_t
constexpr uint32_t kWriteBuffer = 0x4C;        // the packet being built: its start
constexpr uint32_t kWritePtr = 0x50;           // and end
constexpr uint32_t kReadBuffer = 0x54;         // received data: its start
constexpr uint32_t kReadPtr = 0x58;            // and end
constexpr uint32_t kStatus = 0x60;
constexpr uint32_t kApp = 0x68;
constexpr uint32_t kLastCommTime = 0x6C;
constexpr uint32_t kLastSendTime = 0x70;
constexpr uint32_t kNextSequence = 0x478;      // per group: the next sequence number expected
constexpr uint32_t kSequencedBuffer = 0x87C;   // per group: Lst::Head<MsgSeqLink>, received out of order
constexpr uint32_t kStreams = 0x1C7C;          // Lst::Head<StreamLink>: streams being received
constexpr uint32_t kLatency = 0x1CA4;          // the latency average (first dword: the current value)
constexpr uint32_t kLatencyTestSent = 0x1CD4;
constexpr uint32_t kLatencyTestAnswered = 0x1CD8;
constexpr uint32_t kReceiveMetrics = 0x1CDC;
constexpr uint32_t kSendMetrics = 0x2CE8;
constexpr uint32_t kLastTimestampIn = 0x3D14;
constexpr uint32_t kTimestampOut = 0x3D18;
} // namespace ConnField

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

// App (wt/netA's AppView and wt/netB's App, net_app.h): short names for the calls this range makes
inline void EnqueueMessage(App* app, int handle, MsgDesc* desc) { app->EnqueueMessage(handle, desc); }
// Whether this frame sends to the connection at all (its send interval has passed, or it's flagged).
inline bool ShouldSendThisFrame(AppView* app, Conn* conn) { return app->IsSendDue(conn); }
// Anything to send to the connection now.
inline bool MessagesToSend(AppView* app, Conn* conn) { return app->MessagesDue(conn); }
inline bool BandwidthExceeded(App* app, Conn* conn) { return app->BandwidthExceeded(conn); }
// An important or sequenced message is due (unsent, or overdue for its ack): the client sends a timestamp
// with the packet so it gets acked.
inline bool TimestampDue(AppView* app, Conn* conn) { return app->ImportantMessagesDue(conn); }
// A packet to ip:port (sendto) / on the connected socket.
inline bool SendTo(AppView* app, uint32_t ip, uint16_t port, char* data, int size, int flags)
{
    return app->SendTo(ip, port, reinterpret_cast<const uint8_t*>(data), size, flags);
}
inline bool Send(AppView* app, char* data, int size, int flags)
{
    return app->Send(reinterpret_cast<const uint8_t*>(data), size, flags);
}
// App flag 0x8: the App has no socket of its own (nothing to receive).
inline bool HasNoSocket(AppView* app) { return app->IsLocal(); }
// App flag 0x10: connection requests from foreign addresses are answered.
inline bool AcceptsForeignConnections(AppView* app) { return app->HasFlag10(); }
inline Conn* GetConnectionByAddress(AppView* app, uint32_t ip, uint16_t port)
{
    return app->GetConnectionByAddress(ip, port);
}
inline Conn* NewConnection(App* app, uint32_t ip, uint16_t port, int flags)
{
    return app->NewConnection(static_cast<int>(ip), port, flags);
}
// A received packet into the connection's read buffer (false: not valid; secure packets lose their checksum).
inline bool ReadPacket(AppView* app, char* packet, char* to, int size)
{
    return app->Decrypt(reinterpret_cast<const uint8_t*>(packet), reinterpret_cast<uint8_t*>(to), size);
}
// A message straight to ip:port, not through a connection.
inline void SendMessageTo(AppView* app, uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port,
                          int flags)
{
    app->SendMessageTo(id, length, data, ip, port, flags);
}
inline void TerminateConnection(AppView* app, Conn* conn) { app->TerminateConnection(conn); }
inline void ReportSocketError(AppView* app) { app->ReportError(); }
inline uint8_t* GetManager(AppView* app) { return static_cast<uint8_t*>(app->NetManager()); }
// Net::Manager's flags for a message id (0x00454980).
inline uint8_t MessageFlags(uint8_t* manager, uint8_t id)
{
    return GameFunction<uint8_t(__thiscall*)(uint8_t*, uint32_t)>(0x00454980)(manager, id);
}

// Conn (0x00455xxx-0x00456xxx, not decompiled yet). Its flags (+0x64): 1 local (the App's own client/server in
// this process), 2 remote, 4 foreign (created for a packet from an unknown address: NewConnection's flags 6).
inline bool IsRemote(Conn* conn) { return GameFunction<bool(__thiscall*)(Conn*)>(0x00455A30)(conn); }
inline bool IsForeign(Conn* conn) { return GameFunction<bool(__thiscall*)(Conn*)>(0x00455A10)(conn); }
inline bool IsLocal(Conn* conn) { return GameFunction<bool(__thiscall*)(Conn*)>(0x00455A20)(conn); }
// (0x00455C20) status bit 2 clear: MarkNewConnection hasn't been called
inline bool IsNew(Conn* conn) { return GameFunction<bool(__thiscall*)(Conn*)>(0x00455C20)(conn); }
// (0x00455C10) status: bit 0 off, bit 2 on
inline void MarkNewConnection(Conn* conn) { GameFunction<void(__thiscall*)(Conn*)>(0x00455C10)(conn); }
// How long an important message waits for its ack before going again (0x00455C30).
inline uint32_t GetResendThreshold(Conn* conn) { return GameFunction<uint32_t(__thiscall*)(Conn*)>(0x00455C30)(conn); }
inline void SetForceSendThisFrame(Conn* conn, bool on)
{
    GameFunction<void(__thiscall*)(Conn*, bool)>(0x00455980)(conn, on);
}
inline int GetBandwidth(Conn* conn) { return GameFunction<int(__thiscall*)(Conn*)>(0x004559C0)(conn); }
inline void SetBandwidth(Conn* conn, int bandwidth) { GameFunction<void(__thiscall*)(Conn*, int)>(0x004559B0)(conn, bandwidth); }
// The number of important/sequenced messages waiting for acks (0x00456540).
inline int GetNumPendingMessages(Conn* conn) { return GameFunction<int(__thiscall*)(Conn*)>(0x00456540)(conn); }
// A send failed: the queues are rolled back to before this timestamp (0x004566D0).
inline void FlagMessagesForResending(Conn* conn, uint32_t timestamp)
{
    GameFunction<void(__thiscall*)(Conn*, uint32_t)>(0x004566D0)(conn, timestamp);
}
// The connection's destructor (0x00456790).
inline void DestroyConn(Conn* conn) { GameFunction<void(__thiscall*)(Conn*)>(0x00456790)(conn); }
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
