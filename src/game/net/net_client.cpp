// The network library's client (NetLink::Guest). See net_client.h.

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

namespace NetLink {

namespace {

static VtableSlot kClientVtable_slots[] = {&NetLink::Guest::Teardown, &NetLink::AppHost::HandleIncoming, &NetLink::Guest::PullInbound,
        &NetLink::Guest::Transmit, &NetLink::Guest::PrepareAll, &NetLink::Guest::TearDownState};
void** const kClientVtable = reinterpret_cast<void**>(kClientVtable_slots);
constexpr int kAppTaskPriority = -2000;
constexpr uint32_t kTaskSize = 0x20;
constexpr int kTimestampWindow = 5000; // timestamps further apart than this have wrapped around

uint32_t& Timestamp(AppHost* app) { return app->m_clock; }
Router* GetDispatcher(AppHost* app) { return &app->m_dispatcher; }

// The first connection (ListKit::Hunt<Conn> over the App's connections; the search's vtable is 0x0064759C).
Conn* ServerConnection(AppHost* app)
{
    ListKit::Hunt<Conn> search;
    return search.HeadElement(&app->m_circuits);
}

} // namespace

// Shared with Server (net_server.cpp).
void ConstructAppTask(AppHost* app)
{
    void* task = MemSys::AllocZeroed(kTaskSize);
    if (task) {
        static_cast<TaskList::SchedulerTask*>(task)->MakeNew(kAppTaskPriority); // (TaskList::Task's base)
        Prop<AppHost*>(task, 0x1C) = app;
        Prop<void**>(task, 0x0) = GameCode::kTaskVtable;
        Prop<void*>(task, 0x18) = reinterpret_cast<void*>(&AppHost::SendNetworkData);
    }
    app->m_sendTask = task;
}

// 0x0045A250
Guest* Guest::MakeNew(int flags)
{
    AppHost::MakeNew(flags);
    Prop<void**>(this, 0) = kClientVtable;
    ConstructAppTask(this);
    return this;
}

// 0x0045B910
void Guest::DestroyBody()
{
    AppHost::DestroyBody();
}

// 0x0045B8E0
Guest* Guest::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0045B820
void Guest::TearDownState()
{
    AppHost::TearDownState();
}

// 0x00459EC0
bool Guest::PrepareAll()
{
    m_linkedUp = false;
    Timestamp(this) = 0;
    if (!AppHost::PrepareAll())
        return false;
    Router* dispatcher = GetDispatcher(this);
    dispatcher->AppendListener(kMsgConnectionAccepted, HandleConnectionAccepted, 3, nullptr, 0x80);
    dispatcher->AppendListener(kMsgConnectionRefused, HandleConnectionRefused, 2, nullptr, 0x80);
    dispatcher->AppendListener(kMsgConnectionTerminated, HandleConnectionRefused, 2, nullptr, 0x80);
    dispatcher->AppendListener(kMsgTimestamp, HandleTimestamp, 3, nullptr, 0x80);
    return true;
}

// 0x00459F40
bool Guest::DialServer(uint32_t ip, uint16_t port)
{
    auto& address = *reinterpret_cast<sockaddr_in*>(m_serverAddress);
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = ip;
    std::memset(address.sin_zero, 0, sizeof(address.sin_zero));
    while (connect(m_sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
           SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            ReportSocketError(this);
            return false;
        }
    }
    m_linkedUp = ip != 0;
    return true;
}

// 0x00459DC0
void Guest::SendBulkToHost(uint8_t msgId, uint16_t size, void* data, const char* desc, uint8_t groupId,
                                   bool allAtOnce, bool sendInPlace)
{
    if (Conn* server = ServerConnection(this))
        SendBulkToConn(server, msgId, size, data, desc, groupId, allAtOnce, sendInPlace);
}

// 0x00459E50
void Guest::SendToHost(NetMessageInfo* desc)
{
    if (Conn* server = ServerConnection(this))
        NetLink::PutOnOutbox(this, server->m_hdl, desc);
}

// 0x0045A300
void Guest::PullInbound()
{
    int fromLength = sizeof(sockaddr_in);
    sockaddr_in from = {}; // (the original leaves it uninitialised: recv doesn't fill it in)
    if (HasNoSocket(this))
        return;
    int received;
    do {
        char* buffer = m_receiveBuffer;
        SOCKET socket = m_sock;
        if (!m_linkedUp)
            received = recvfrom(socket, buffer, kReceiveBufferSize, 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
        else
            received = recv(socket, buffer, kReceiveBufferSize, 0);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        m_totalBytesIn += received;
        int size = (m_flagBits & kAppSecure) ? received - 2 : received;
        auto& server = *reinterpret_cast<sockaddr_in*>(m_serverAddress);
        if (Conn* conn = NetLink::FindConnAtAddress(this, server.sin_addr.s_addr, ntohs(server.sin_port))) {
            char*& readPtr = conn->m_readPtr;
            if (readPtr + size < conn->m_readBuffer + kConnReceiveBufferSize) {
                uint32_t time = FrameClock::GetTime();
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
        } else if (!m_linkedUp) {
            // from an unknown address
            if (Conn* fresh = NetLink::OpenedLink(this, from.sin_addr.s_addr, ntohs(from.sin_port), 6)) {
                fresh->m_ownerApp = this;
                if (ReadPacket(this, buffer, fresh->m_readPtr, received)) {
                    fresh->m_readPtr = fresh->m_readBuffer + size;
                    MarkNewConnection(fresh);
                } else {
                    if (auto handler = m_foreignPacketHandler)
                        handler(buffer, received, reinterpret_cast<sockaddr*>(&from));
                    DestroyConn(fresh);
                    MemSys::Hub::Pull()->Dealloc(fresh);
                }
            }
        }
    } while (received > 0);
}

// 0x00459FD0
void Guest::Transmit(Conn* conn)
{
    if (!TransmitDueNow(this, conn))
        return;
    bool done = false;
    do {
        if (!OutgoingBacklog(this, conn))
            return;
        if (NetLink::OverBandwidthBudget(this, conn))
            return;
        if (TimestampDue(this, conn)) {
            NetMessageInfo desc;
            ConstructMsgDesc(&desc);
            auto timestamp = static_cast<uint16_t>(conn->m_timestampOut);
            desc.m_content = &timestamp;
            desc.m_byteCount = sizeof(timestamp);
            desc.m_ident = kMsgTimestamp;
            desc.m_rank = kHighestPriority;
            NetLink::PutOnOutbox(this, conn->m_hdl, &desc);
        }
        if (IsOffMachine(conn) && !IsOutsider(conn) && (m_flagBits & kAppMeasureLatency)) {
            NetMessageInfo desc;
            ConstructMsgDesc(&desc);
            uint32_t& sent = conn->m_latencyTest.m_sendTime;
            uint32_t now = Timestamp(this);
            if (sent == 0 || now - sent > MaxLatency()) {
                // a test due: if the last went unanswered, the latency is at least that much more
                if (sent > conn->m_latencyTest.m_receiveTime) {
                    uint32_t latency = (NetLink::IsHostLocal(conn) ? 0 : static_cast<uint32_t>(conn->m_latencyTest.m_averageLatency)) + 100;
                    if (latency > MaxLatency())
                        latency = MaxLatency();
                    AddLatency(conn, latency);
                }
                sent = now;
                desc.m_content = &now;
                desc.m_ident = kMsgLatencyTest;
                desc.m_byteCount = sizeof(now);
                NetLink::PutOnOutbox(this, conn->m_hdl, &desc);
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
        if (size > 0 && IsOffMachine(conn)) {
            char* packet = conn->m_writeBuffer;
            bool sent = m_linkedUp
                            ? NetLink::PostOut(this, packet, size, 0)
                            : NetLink::TransmitTo(this, conn->m_hostAddr, conn->m_portNo,
                                     packet, size, 0);
            if (!sent) {
                MarkMessagesForRetry(conn, conn->m_timestampOut);
                writePtr = conn->m_writeBuffer;
                return;
            }
            m_totalBytesOut += size;
            AddPacketMetric(&conn->m_sendMetrics, size + 0x1C, Timestamp(this));
            conn->m_lastSendTime = Timestamp(this);
            ForceSendNow(conn, false);
            writePtr = conn->m_writeBuffer;
        }
        ++conn->m_timestampOut;
    } while (!done);
}

// 0x0045ABC0
int Guest::HandleConnectionAccepted(InboundMsgCtx* context)
{
    context->m_Acknowledge = true;
    return kHandlerContinue;
}

// 0x0045ABD0
int Guest::HandleConnectionRefused(InboundMsgCtx* context)
{
    NetLink::SeverLink(context->m_receiverApp, context->m_senderConn);
    return kHandlerHalt;
}

// 0x0045AA70
int Guest::HandleTimestamp(InboundMsgCtx* context)
{
    context->m_bodySize = sizeof(uint16_t);
    if (context->m_wireOptions & kPacketForeign)
        return kHandlerMsgDone;
    auto* stamp = reinterpret_cast<uint16_t*>(context->m_body);
    // (no bounds check: more than 32 timestamps in a packet would overrun the list)
    context->m_SequencesToAck[context->m_NumSequencesToAck] = *stamp;
    ++context->m_NumSequencesToAck;
    Conn* conn = context->m_senderConn;
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
        ++context->m_receiverApp->m_dupePackets;
        return kHandlerError;
    } else {
        uint32_t gap = timestamp - newest;
        if (gap > 1 && gap <= kTimestampWindow) {
            context->m_receiverApp->m_lostPackets += gap - 1;
            context->m_senderConn->m_lastTimestampIn = *stamp;
            return kHandlerContinue;
        }
        late = gap > kTimestampWindow; // (so far ahead it has wrapped around: older)
    }
    if (late) {
        // it was counted as lost when a newer one came
        ++context->m_receiverApp->m_latePackets;
        --context->m_receiverApp->m_lostPackets;
        context->m_wireOptions |= kPacketLate;
        return kHandlerContinue;
    }
    newest = timestamp;
    return kHandlerContinue;
}

} // namespace Net
