// The network library's client (Net::Client). See net_client.h.

#include "game/net/net_client.h"

#include <winsock2.h>

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/search.h"
#include "game/net/net_access_c.h"
#include "game/sys/timer.h"
#include "hook/hook.h"

namespace Net {

namespace {

void** const kClientVtable = reinterpret_cast<void**>(0x006475CC);
void** const kAppTaskVtable = reinterpret_cast<void**>(0x006476F0);
constexpr int kAppTaskPriority = -2000;
constexpr uint32_t kTaskSize = 0x20;
constexpr int kTimestampWindow = 5000; // timestamps further apart than this have wrapped around

uint32_t& Timestamp(App* app) { return Field<uint32_t>(app, AppField::kTimestamp); }
Dispatcher* GetDispatcher(App* app) { return &Field<Dispatcher>(app, AppField::kDispatcher); }


// The first connection (Lst::Search<Conn> over the App's connections; the search's vtable is 0x0064759C).
Conn* ServerConnection(App* app)
{
    Lst::Search<Conn> search;
    return search.FirstItem(&Field<Mem::ListNode>(app, AppField::kConnections));
}

} // namespace

// Shared with Server (net_server.cpp).
void ConstructAppTask(App* app)
{
    void* task = Mem::Calloc(kTaskSize);
    if (task) {
        // Tsk::Task's constructor (not decompiled yet)
        GameFunction<void*(__thiscall*)(void*, int)>(0x00406890)(task, kAppTaskPriority);
        Field<App*>(task, 0x1C) = app;
        Field<void**>(task, 0x0) = kAppTaskVtable;
        Field<void*>(task, 0x18) = reinterpret_cast<void*>(&App::SendNetworkData);
    }
    Field<void*>(app, AppField::kTask) = task;
}

// 0x0045A250
Client* Client::Construct(int flags)
{
    App::Construct(flags);
    Field<void**>(this, 0) = kClientVtable;
    ConstructAppTask(this);
    return this;
}

// 0x0045B910
void Client::DestroyBody()
{
    App::DestroyBody();
}

// 0x0045B8E0
Client* Client::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0045B820
void Client::Deinit()
{
    App::Deinit();
}

// 0x00459EC0
bool Client::Init()
{
    Field<bool>(this, AppField::kConnected) = false;
    Timestamp(this) = 0;
    if (!App::Init())
        return false;
    Dispatcher* dispatcher = GetDispatcher(this);
    dispatcher->AddHandler(kMsgConnectionAccepted, HandleConnectionAccepted, 3, nullptr, 0x80);
    dispatcher->AddHandler(kMsgConnectionRefused, HandleConnectionRefused, 2, nullptr, 0x80);
    dispatcher->AddHandler(kMsgConnectionTerminated, HandleConnectionRefused, 2, nullptr, 0x80);
    dispatcher->AddHandler(kMsgTimestamp, HandleTimestamp, 3, nullptr, 0x80);
    return true;
}

// 0x00459F40
bool Client::ConnectToServer(uint32_t ip, uint16_t port)
{
    auto& address = Field<sockaddr_in>(this, AppField::kServerAddress);
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = ip;
    std::memset(address.sin_zero, 0, sizeof(address.sin_zero));
    while (connect(Field<SOCKET>(this, AppField::kSocket), reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
           SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            ReportSocketError(this);
            return false;
        }
    }
    Field<bool>(this, AppField::kConnected) = ip != 0;
    return true;
}

// 0x00459DC0
void Client::StreamMessageToServer(uint8_t msgId, uint16_t size, void* data, const char* desc, uint8_t groupId,
                                   bool allAtOnce, bool sendInPlace)
{
    if (Conn* server = ServerConnection(this))
        StreamMessageToConn(server, msgId, size, data, desc, groupId, allAtOnce, sendInPlace);
}

// 0x00459E50
void Client::EnqueueMessageToServer(MsgDesc* desc)
{
    if (Conn* server = ServerConnection(this))
        Net::EnqueueMessage(this, Field<int>(server, ConnField::kHandle), desc);
}

// 0x0045A300
void Client::ReceiveData()
{
    int fromLength = sizeof(sockaddr_in);
    sockaddr_in from = {}; // (the original leaves it uninitialised: recv doesn't fill it in)
    if (HasNoSocket(this))
        return;
    int received;
    do {
        char* buffer = &Field<char>(this, AppField::kReceiveBuffer);
        SOCKET socket = Field<SOCKET>(this, AppField::kSocket);
        if (!Field<bool>(this, AppField::kConnected))
            received = recvfrom(socket, buffer, kReceiveBufferSize, 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
        else
            received = recv(socket, buffer, kReceiveBufferSize, 0);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        Field<int>(this, AppField::kTotalBytesIn) += received;
        int size = (Field<uint32_t>(this, AppField::kFlags) & kAppSecure) ? received - 2 : received;
        auto& server = Field<sockaddr_in>(this, AppField::kServerAddress);
        if (Conn* conn = Net::GetConnectionByAddress(this, server.sin_addr.s_addr, ntohs(server.sin_port))) {
            char*& readPtr = Field<char*>(conn, ConnField::kReadPtr);
            if (readPtr + size < Field<char*>(conn, ConnField::kReadBuffer) + kConnReceiveBufferSize) {
                uint32_t time = Tmr::GetTime();
                if (ReadPacket(this, buffer, readPtr, received)) {
                    readPtr += size;
                    AddPacketMetric(&Field<uint8_t>(conn, ConnField::kReceiveMetrics), received + 0x1C,
                                    Timestamp(this));
                    if (time > Field<uint32_t>(conn, ConnField::kLastCommTime))
                        Field<uint32_t>(conn, ConnField::kLastCommTime) = time;
                } else if (auto handler = Field<ForeignPacketHandler>(this, AppField::kForeignPacketHandler)) {
                    handler(buffer, received, reinterpret_cast<sockaddr*>(&from));
                }
            }
        } else if (!Field<bool>(this, AppField::kConnected)) {
            // from an unknown address
            if (Conn* fresh = Net::NewConnection(this, from.sin_addr.s_addr, ntohs(from.sin_port), 6)) {
                Field<App*>(fresh, ConnField::kApp) = this;
                if (ReadPacket(this, buffer, Field<char*>(fresh, ConnField::kReadPtr), received)) {
                    Field<char*>(fresh, ConnField::kReadPtr) = Field<char*>(fresh, ConnField::kReadBuffer) + size;
                    MarkNewConnection(fresh);
                } else {
                    if (auto handler = Field<ForeignPacketHandler>(this, AppField::kForeignPacketHandler))
                        handler(buffer, received, reinterpret_cast<sockaddr*>(&from));
                    DestroyConn(fresh);
                    Mem::Manager::Get()->Free(fresh);
                }
            }
        }
    } while (received > 0);
}

// 0x00459FD0
void Client::SendData(Conn* conn)
{
    if (!ShouldSendThisFrame(this, conn))
        return;
    bool done = false;
    do {
        if (!MessagesToSend(this, conn))
            return;
        if (Net::BandwidthExceeded(this, conn))
            return;
        if (TimestampDue(this, conn)) {
            MsgDesc desc;
            ConstructMsgDesc(&desc);
            auto timestamp = static_cast<uint16_t>(Field<uint32_t>(conn, ConnField::kTimestampOut));
            desc.m_data = &timestamp;
            desc.m_length = sizeof(timestamp);
            desc.m_id = kMsgTimestamp;
            desc.m_priority = kHighestPriority;
            Net::EnqueueMessage(this, Field<int>(conn, ConnField::kHandle), &desc);
        }
        if (IsRemote(conn) && !IsForeign(conn) && (Field<uint32_t>(this, AppField::kFlags) & kAppMeasureLatency)) {
            MsgDesc desc;
            ConstructMsgDesc(&desc);
            uint32_t& sent = Field<uint32_t>(conn, ConnField::kLatencyTestSent);
            uint32_t now = Timestamp(this);
            if (sent == 0 || now - sent > MaxLatency()) {
                // a test due: if the last went unanswered, the latency is at least that much more
                if (sent > Field<uint32_t>(conn, ConnField::kLatencyTestAnswered)) {
                    uint32_t latency = (Net::IsLocal(conn) ? 0 : Field<uint32_t>(conn, ConnField::kLatency)) + 100;
                    if (latency > MaxLatency())
                        latency = MaxLatency();
                    AddLatency(conn, latency);
                }
                sent = now;
                desc.m_data = &now;
                desc.m_id = kMsgLatencyTest;
                desc.m_length = sizeof(now);
                Net::EnqueueMessage(this, Field<int>(conn, ConnField::kHandle), &desc);
            }
        }
        if (!BundleMessages(conn, kQueueNormal, false))
            done = true;
        if (!BundleMessages(conn, kQueueSequenced, true))
            done = true;
        if (!BundleMessages(conn, kQueueImportant, true))
            done = true;
        if (!BundleMessages(conn, kQueueSequenced, false))
            done = true;
        if (!BundleMessages(conn, kQueueImportant, false))
            done = true;
        char*& writePtr = Field<char*>(conn, ConnField::kWritePtr);
        int size = static_cast<int>(writePtr - Field<char*>(conn, ConnField::kWriteBuffer));
        if (size > 0 && IsRemote(conn)) {
            char* packet = Field<char*>(conn, ConnField::kWriteBuffer);
            bool sent = Field<bool>(this, AppField::kConnected)
                            ? Net::Send(this, packet, size, 0)
                            : Net::SendTo(this, Field<uint32_t>(conn, ConnField::kIp), Field<uint16_t>(conn, ConnField::kPort),
                                     packet, size, 0);
            if (!sent) {
                FlagMessagesForResending(conn, Field<uint32_t>(conn, ConnField::kTimestampOut));
                writePtr = Field<char*>(conn, ConnField::kWriteBuffer);
                return;
            }
            Field<int>(this, AppField::kTotalBytesOut) += size;
            AddPacketMetric(&Field<uint8_t>(conn, ConnField::kSendMetrics), size + 0x1C, Timestamp(this));
            Field<uint32_t>(conn, ConnField::kLastSendTime) = Timestamp(this);
            SetForceSendThisFrame(conn, false);
            writePtr = Field<char*>(conn, ConnField::kWriteBuffer);
        }
        ++Field<uint32_t>(conn, ConnField::kTimestampOut);
    } while (!done);
}

// 0x0045ABC0
int Client::HandleConnectionAccepted(MsgHandlerContext* context)
{
    context->m_Acknowledge = true;
    return kHandlerContinue;
}

// 0x0045ABD0
int Client::HandleConnectionRefused(MsgHandlerContext* context)
{
    Net::TerminateConnection(context->m_App, context->m_Conn);
    return kHandlerHalt;
}

// 0x0045AA70
int Client::HandleTimestamp(MsgHandlerContext* context)
{
    context->m_MsgLength = sizeof(uint16_t);
    if (context->m_PacketFlags & kPacketForeign)
        return kHandlerMsgDone;
    auto* stamp = reinterpret_cast<uint16_t*>(context->m_Msg);
    // (no bounds check: more than 32 timestamps in a packet would overrun the list)
    context->m_SequencesToAck[context->m_NumSequencesToAck] = *stamp;
    ++context->m_NumSequencesToAck;
    Conn* conn = context->m_Conn;
    uint32_t& newest = Field<uint32_t>(conn, ConnField::kLastTimestampIn);
    if (newest == 0) {
        newest = *stamp;
        return kHandlerContinue;
    }
    uint32_t timestamp = *stamp;
    bool late;
    if (timestamp < newest) {
        late = newest - timestamp <= kTimestampWindow; // (else it has wrapped around: newer)
    } else if (timestamp == newest) {
        ++Field<int>(context->m_App, AppField::kDupePackets);
        return kHandlerError;
    } else {
        uint32_t gap = timestamp - newest;
        if (gap > 1 && gap <= kTimestampWindow) {
            Field<int>(context->m_App, AppField::kLostPackets) += gap - 1;
            Field<uint32_t>(context->m_Conn, ConnField::kLastTimestampIn) = *stamp;
            return kHandlerContinue;
        }
        late = gap > kTimestampWindow; // (so far ahead it has wrapped around: older)
    }
    if (late) {
        // it was counted as lost when a newer one came
        ++Field<int>(context->m_App, AppField::kLatePackets);
        --Field<int>(context->m_App, AppField::kLostPackets);
        context->m_PacketFlags |= kPacketLate;
        return kHandlerContinue;
    }
    newest = timestamp;
    return kHandlerContinue;
}

} // namespace Net

REPLACE(0x0045A250, &Net::Client::Construct);
REPLACE(0x0045B8E0, &Net::Client::Destroy);
REPLACE_SOURCE_ONLY(0x0045B910, &Net::Client::DestroyBody);
REPLACE_SOURCE_ONLY(0x0045B820, &Net::Client::Deinit);
REPLACE(0x00459EC0, &Net::Client::Init);
REPLACE(0x00459F40, &Net::Client::ConnectToServer);
REPLACE(0x00459DC0, &Net::Client::StreamMessageToServer);
REPLACE(0x00459E50, &Net::Client::EnqueueMessageToServer);
REPLACE(0x0045A300, &Net::Client::ReceiveData);
REPLACE(0x00459FD0, &Net::Client::SendData);
REPLACE(0x0045ABC0, Net::Client::HandleConnectionAccepted);
REPLACE(0x0045ABD0, Net::Client::HandleConnectionRefused);
REPLACE(0x0045AA70, Net::Client::HandleTimestamp);
