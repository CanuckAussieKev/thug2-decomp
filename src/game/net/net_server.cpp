// The network library's server (NetLink::HostEnd). See net_server.h.

#include <cstdint>

#include "game/net/net_server.h"

#include <winsock2.h>

#include "game/core/manager.h"
#include "game/net/net_access_c.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/net/net_client.h"

namespace NetLink {

void ConstructAppTask(AppHost* app); // net_client.cpp

namespace {

static VtableSlot kServerVtable_slots[] = {&NetLink::Guest::Teardown, &NetLink::AppHost::HandleIncoming, &NetLink::HostEnd::PullInbound,
        &NetLink::HostEnd::Transmit, &NetLink::HostEnd::PrepareAll, &NetLink::Guest::TearDownState};
void** const kServerVtable = reinterpret_cast<void**>(kServerVtable_slots);
constexpr int kBandwidthStep = 500;      // less when messages pile up
constexpr int kMinBandwidth = 0x530;
constexpr int kIdleBandwidthStep = 1000; // more when nothing waits for an ack

uint32_t& Timestamp(AppHost* app) { return app->m_clock; }
Router* GetDispatcher(AppHost* app) { return &app->m_dispatcher; }

} // namespace

// 0x0045B830
HostEnd* HostEnd::MakeNew(int flags)
{
    AppHost::MakeNew(flags);
    Prop<void**>(this, 0) = kServerVtable;
    ConstructAppTask(this);
    return this;
}

// 0x0045B7A0
bool HostEnd::PrepareAll()
{
    Timestamp(this) = 0;
    m_linkedUp = false;
    m_maxConnections = 0;
    if (!AppHost::PrepareAll())
        return false;
    Router* dispatcher = GetDispatcher(this);
    dispatcher->AppendListener(kMsgConnectionRequest, HandleConnectionRequest, 3, nullptr, kHighestPriority);
    dispatcher->AppendListener(kMsgTimestamp, HandleTimestamp, 3, nullptr, 0x80);
    dispatcher->AppendListener(kMsgDisconnRequest, HandleDisconnRequest, 2, nullptr, kHighestPriority);
    return true;
}

// 0x0045BBD0
void HostEnd::PullInbound()
{
    int fromLength = sizeof(sockaddr_in);
    sockaddr_in from;
    if (HasNoSocket(this))
        return;
    int received;
    do {
        char* buffer = m_receiveBuffer;
        received = recvfrom(m_sock, buffer, kReceiveBufferSize, 0,
                            reinterpret_cast<sockaddr*>(&from), &fromLength);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        m_totalBytesIn += received;
        int size = (m_flagBits & kAppSecure) ? received - 2 : received;
        if (Conn* conn = NetLink::FindConnAtAddress(this, from.sin_addr.s_addr, ntohs(from.sin_port))) {
            char*& readPtr = conn->m_readPtr;
            if ((IsNew(conn) || !(m_flagBits & kAppSecure)) &&
                readPtr + size < conn->m_readBuffer + kConnReceiveBufferSize) {
                if (ReadPacket(this, buffer, readPtr, received)) {
                    readPtr += size;
                    AddPacketMetric(&conn->m_receiveMetrics, received + 0x1C,
                                    Timestamp(this));
                    conn->m_lastCommTime = FrameClock::GetTime();
                } else if (auto handler = m_foreignPacketHandler) {
                    // (given the connection's read pointer, not the packet - as in the original)
                    handler(readPtr, received, reinterpret_cast<sockaddr*>(&from));
                }
            }
        } else if (Conn* fresh = NetLink::OpenedLink(this, from.sin_addr.s_addr, ntohs(from.sin_port), 6)) {
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
    } while (received > 0);
}

// 0x0045B920
void HostEnd::Transmit(Conn* conn)
{
    NetMessageInfo timestampDesc;
    ConstructMsgDesc(&timestampDesc);
    if (!TransmitDueNow(this, conn))
        return;
    bool done = false;
    do {
        if (!OutgoingBacklog(this, conn) || NetLink::OverBandwidthBudget(this, conn))
            break;
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
                desc.m_byteCount = sizeof(now);
                desc.m_ident = kMsgLatencyTest;
                NetLink::PutOnOutbox(this, conn->m_hdl, &desc);
            }
        }
        // every packet carries a timestamp
        auto timestamp = static_cast<uint16_t>(conn->m_timestampOut);
        timestampDesc.m_content = &timestamp;
        timestampDesc.m_byteCount = sizeof(timestamp);
        timestampDesc.m_ident = kMsgTimestamp;
        timestampDesc.m_rank = kHighestPriority;
        NetLink::PutOnOutbox(this, conn->m_hdl, &timestampDesc);

        int pendingBefore = GetNumPendingMessages(conn);
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
        if (GetNumPendingMessages(conn) > pendingBefore &&
            (conn->m_curStanding & kConnStatus1)) {
            // more waiting for acks: take it as high latency and lower the bandwidth
            AddLatency(conn, MaxLatency());
            ChangeThroughput(conn, FetchThroughput(conn) - kBandwidthStep);
            if (FetchThroughput(conn) < kMinBandwidth)
                ChangeThroughput(conn, kMinBandwidth);
        }

        char*& writePtr = conn->m_writePtr;
        int size = static_cast<int>(writePtr - conn->m_writeBuffer);
        if (size > 0 && IsOffMachine(conn)) {
            if (!NetLink::TransmitTo(this, conn->m_hostAddr, conn->m_portNo,
                        conn->m_writeBuffer, size, 0)) {
                MarkMessagesForRetry(conn, conn->m_timestampOut);
                writePtr = conn->m_writeBuffer;
                break;
            }
            m_totalBytesOut += size;
            AddPacketMetric(&conn->m_sendMetrics, size + 0x1C, Timestamp(this));
            ForceSendNow(conn, false);
            conn->m_lastSendTime = Timestamp(this);
            writePtr = conn->m_writeBuffer;
        }
        ++conn->m_timestampOut;
    } while (!done);

    if (NetLink::OverBandwidthBudget(this, conn) && GetNumPendingMessages(conn) == 0)
        ChangeThroughput(conn, FetchThroughput(conn) + kIdleBandwidthStep); // limited, though nothing is waiting
}

// 0x0045ADB0
int HostEnd::HandleConnectionRequest(InboundMsgCtx* context)
{
    NetMessageInfo desc;
    ConstructMsgDesc(&desc);
    if (context->m_wireOptions & kPacketForeign) {
        if (!NetLink::AllowsOutsidePeers(context->m_receiverApp))
            return kHandlerMsgDone;
        Conn* conn = context->m_senderConn;
        NetLink::DeliverTo(context->m_receiverApp, kMsgConnectionAccepted, 0, nullptr, conn->m_hostAddr,
                      conn->m_portNo, 0);
        return kHandlerContinue;
    }
    if (!NetLink::IsHostLocal(context->m_senderConn))
        return kHandlerMsgDone;
    desc.m_ident = kMsgConnectionAccepted;
    desc.m_rank = kHighestPriority;
    desc.m_queue = kQueueImportant;
    NetLink::PutOnOutbox(context->m_receiverApp, context->m_senderConn->m_hdl, &desc);
    return kHandlerContinue;
}

// 0x0045AE50
int HostEnd::HandleDisconnRequest(InboundMsgCtx* context)
{
    NetMessageInfo desc;
    ConstructMsgDesc(&desc);
    Conn* conn = context->m_senderConn;
    if (conn->m_curStanding & kConnDisconnAccepted)
        return kHandlerMsgDone;
    desc.m_ident = kMsgDisconnAccepted;
    NetLink::PutOnOutbox(context->m_receiverApp, conn->m_hdl, &desc);
    context->m_senderConn->m_curStanding |= kConnDisconnAccepted;
    return kHandlerContinue;
}

// 0x0045AA30
int HostEnd::HandleTimestamp(InboundMsgCtx* context)
{
    context->m_bodySize = sizeof(uint16_t);
    if (context->m_wireOptions & kPacketForeign)
        return kHandlerMsgDone;
    // (no bounds check: more than 32 timestamps in a packet would overrun the list)
    context->m_SequencesToAck[context->m_NumSequencesToAck] = *reinterpret_cast<uint16_t*>(context->m_body);
    ++context->m_NumSequencesToAck;
    return kHandlerContinue;
}

} // namespace Net
