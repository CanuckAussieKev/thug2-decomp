// The network library's client (Net::Client). See net_client.h.

#include "game/net/net_client.h"

#include <winsock2.h>

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/lst/search.h"
#include "game/net/net_access_c.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/net/task_vtable.h"

namespace Net {

namespace {

static VtableSlot kClientVtable_slots[] = {&Net::Client::Destroy, &Net::App::ProcessData, &Net::Client::ReceiveData,
        &Net::Client::SendData, &Net::Client::Init, &Net::Client::Deinit};
void** const kClientVtable = reinterpret_cast<void**>(kClientVtable_slots);
constexpr int kAppTaskPriority = -2000;
constexpr uint32_t kTaskSize = 0x20;
constexpr int kTimestampWindow = 5000; // timestamps further apart than this have wrapped around

uint32_t& Timestamp(App* app) { return app->m_time; }
Dispatcher* GetDispatcher(App* app) { return &app->m_dispatcher; }

// The first connection (Lst::Search<Conn> over the App's connections; the search's vtable is 0x0064759C).
Conn* ServerConnection(App* app)
{
    Lst::Search<Conn> search;
    return search.FirstItem(&app->m_connections);
}

} // namespace

// Shared with Server (net_server.cpp).
void ConstructAppTask(App* app)
{
    void* task = Mem::Calloc(kTaskSize);
    if (task) {
        static_cast<Tsk::BaseTask*>(task)->Construct(kAppTaskPriority); // (Tsk::Task's base)
        Field<App*>(task, 0x1C) = app;
        Field<void**>(task, 0x0) = GameCode::kTaskVtable;
        Field<void*>(task, 0x18) = reinterpret_cast<void*>(&App::SendNetworkData);
    }
    app->m_sendTask = task;
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
    m_connected = false;
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
    auto& address = *reinterpret_cast<sockaddr_in*>(m_serverAddress);
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = ip;
    std::memset(address.sin_zero, 0, sizeof(address.sin_zero));
    while (connect(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
           SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            ReportSocketError(this);
            return false;
        }
    }
    m_connected = ip != 0;
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
        Net::EnqueueMessage(this, server->m_handle, desc);
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
        char* buffer = m_receiveBuffer;
        SOCKET socket = m_socket;
        if (!m_connected)
            received = recvfrom(socket, buffer, kReceiveBufferSize, 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
        else
            received = recv(socket, buffer, kReceiveBufferSize, 0);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        m_totalBytesIn += received;
        int size = (m_flags & kAppSecure) ? received - 2 : received;
        auto& server = *reinterpret_cast<sockaddr_in*>(m_serverAddress);
        if (Conn* conn = Net::GetConnectionByAddress(this, server.sin_addr.s_addr, ntohs(server.sin_port))) {
            char*& readPtr = conn->m_readPtr;
            if (readPtr + size < conn->m_readBuffer + kConnReceiveBufferSize) {
                uint32_t time = Tmr::GetTime();
                if (ReadPacket(this, buffer, readPtr, received)) {
                    readPtr += size;
                    AddPacketMetric(&conn->m_receiveMetrics, received + 0x1C,
                                    Timestamp(this));
                    if (time > conn->m_lastCommTime)
                        conn->m_lastCommTime = time;
                } else if (auto handler = m_foreignPacketHandler) {
                    handler(buffer, received, reinterpret_cast<sockaddr*>(&from));
                }
            }
        } else if (!m_connected) {
            // from an unknown address
            if (Conn* fresh = Net::NewConnection(this, from.sin_addr.s_addr, ntohs(from.sin_port), 6)) {
                fresh->m_app = this;
                if (ReadPacket(this, buffer, fresh->m_readPtr, received)) {
                    fresh->m_readPtr = fresh->m_readBuffer + size;
                    MarkNewConnection(fresh);
                } else {
                    if (auto handler = m_foreignPacketHandler)
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
            auto timestamp = static_cast<uint16_t>(conn->m_timestampOut);
            desc.m_data = &timestamp;
            desc.m_length = sizeof(timestamp);
            desc.m_id = kMsgTimestamp;
            desc.m_priority = kHighestPriority;
            Net::EnqueueMessage(this, conn->m_handle, &desc);
        }
        if (IsRemote(conn) && !IsForeign(conn) && (m_flags & kAppMeasureLatency)) {
            MsgDesc desc;
            ConstructMsgDesc(&desc);
            uint32_t& sent = conn->m_latencyTest.m_sendTime;
            uint32_t now = Timestamp(this);
            if (sent == 0 || now - sent > MaxLatency()) {
                // a test due: if the last went unanswered, the latency is at least that much more
                if (sent > conn->m_latencyTest.m_receiveTime) {
                    uint32_t latency = (Net::IsLocal(conn) ? 0 : static_cast<uint32_t>(conn->m_latencyTest.m_averageLatency)) + 100;
                    if (latency > MaxLatency())
                        latency = MaxLatency();
                    AddLatency(conn, latency);
                }
                sent = now;
                desc.m_data = &now;
                desc.m_id = kMsgLatencyTest;
                desc.m_length = sizeof(now);
                Net::EnqueueMessage(this, conn->m_handle, &desc);
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
        char*& writePtr = conn->m_writePtr;
        int size = static_cast<int>(writePtr - conn->m_writeBuffer);
        if (size > 0 && IsRemote(conn)) {
            char* packet = conn->m_writeBuffer;
            bool sent = m_connected
                            ? Net::Send(this, packet, size, 0)
                            : Net::SendTo(this, conn->m_ip, conn->m_port,
                                     packet, size, 0);
            if (!sent) {
                FlagMessagesForResending(conn, conn->m_timestampOut);
                writePtr = conn->m_writeBuffer;
                return;
            }
            m_totalBytesOut += size;
            AddPacketMetric(&conn->m_sendMetrics, size + 0x1C, Timestamp(this));
            conn->m_lastSendTime = Timestamp(this);
            SetForceSendThisFrame(conn, false);
            writePtr = conn->m_writeBuffer;
        }
        ++conn->m_timestampOut;
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
    uint32_t& newest = conn->m_lastTimestampIn;
    if (newest == 0) {
        newest = *stamp;
        return kHandlerContinue;
    }
    uint32_t timestamp = *stamp;
    bool late;
    if (timestamp < newest) {
        late = newest - timestamp <= kTimestampWindow; // (else it has wrapped around: newer)
    } else if (timestamp == newest) {
        ++context->m_App->m_dupePackets;
        return kHandlerError;
    } else {
        uint32_t gap = timestamp - newest;
        if (gap > 1 && gap <= kTimestampWindow) {
            context->m_App->m_lostPackets += gap - 1;
            context->m_Conn->m_lastTimestampIn = *stamp;
            return kHandlerContinue;
        }
        late = gap > kTimestampWindow; // (so far ahead it has wrapped around: older)
    }
    if (late) {
        // it was counted as lost when a newer one came
        ++context->m_App->m_latePackets;
        --context->m_App->m_lostPackets;
        context->m_PacketFlags |= kPacketLate;
        return kHandlerContinue;
    }
    newest = timestamp;
    return kHandlerContinue;
}

} // namespace Net
