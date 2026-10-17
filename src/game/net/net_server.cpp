// The network library's server (Net::Server). See net_server.h.

#include "game/net/net_server.h"

#include <winsock2.h>

#include "game/core/manager.h"
#include "game/net/net_access_c.h"
#include "game/sys/timer.h"
#include "hook/hook.h"

namespace Net {

void ConstructAppTask(App* app); // net_client.cpp

namespace {

void** const kServerVtable = reinterpret_cast<void**>(0x006475EC);
constexpr int kBandwidthStep = 500;      // less when messages pile up
constexpr int kMinBandwidth = 0x530;
constexpr int kIdleBandwidthStep = 1000; // more when nothing waits for an ack

uint32_t& Timestamp(App* app) { return app->m_time; }
Dispatcher* GetDispatcher(App* app) { return &app->m_dispatcher; }


} // namespace

// 0x0045B830
Server* Server::Construct(int flags)
{
    App::Construct(flags);
    Field<void**>(this, 0) = kServerVtable;
    ConstructAppTask(this);
    return this;
}

// 0x0045B7A0
bool Server::Init()
{
    Timestamp(this) = 0;
    m_connected = false;
    m_maxConnections = 0;
    if (!App::Init())
        return false;
    Dispatcher* dispatcher = GetDispatcher(this);
    dispatcher->AddHandler(kMsgConnectionRequest, HandleConnectionRequest, 3, nullptr, kHighestPriority);
    dispatcher->AddHandler(kMsgTimestamp, HandleTimestamp, 3, nullptr, 0x80);
    dispatcher->AddHandler(kMsgDisconnRequest, HandleDisconnRequest, 2, nullptr, kHighestPriority);
    return true;
}

// 0x0045BBD0
void Server::ReceiveData()
{
    int fromLength = sizeof(sockaddr_in);
    sockaddr_in from;
    if (HasNoSocket(this))
        return;
    int received;
    do {
        char* buffer = m_receiveBuffer;
        received = recvfrom(m_socket, buffer, kReceiveBufferSize, 0,
                            reinterpret_cast<sockaddr*>(&from), &fromLength);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        m_totalBytesIn += received;
        int size = (m_flags & kAppSecure) ? received - 2 : received;
        if (Conn* conn = Net::GetConnectionByAddress(this, from.sin_addr.s_addr, ntohs(from.sin_port))) {
            char*& readPtr = conn->m_readPtr;
            if ((IsNew(conn) || !(m_flags & kAppSecure)) &&
                readPtr + size < conn->m_readBuffer + kConnReceiveBufferSize) {
                if (ReadPacket(this, buffer, readPtr, received)) {
                    readPtr += size;
                    AddPacketMetric(conn->m_receiveMetrics, received + 0x1C,
                                    Timestamp(this));
                    conn->m_lastCommTime = Tmr::GetTime();
                } else if (auto handler = m_foreignPacketHandler) {
                    // (given the connection's read pointer, not the packet - as in the original)
                    handler(readPtr, received, reinterpret_cast<sockaddr*>(&from));
                }
            }
        } else if (Conn* fresh = Net::NewConnection(this, from.sin_addr.s_addr, ntohs(from.sin_port), 6)) {
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
    } while (received > 0);
}

// 0x0045B920
void Server::SendData(Conn* conn)
{
    MsgDesc timestampDesc;
    ConstructMsgDesc(&timestampDesc);
    if (!ShouldSendThisFrame(this, conn))
        return;
    bool done = false;
    do {
        if (!MessagesToSend(this, conn) || Net::BandwidthExceeded(this, conn))
            break;
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
                desc.m_length = sizeof(now);
                desc.m_id = kMsgLatencyTest;
                Net::EnqueueMessage(this, conn->m_handle, &desc);
            }
        }
        // every packet carries a timestamp
        auto timestamp = static_cast<uint16_t>(conn->m_timestampOut);
        timestampDesc.m_data = &timestamp;
        timestampDesc.m_length = sizeof(timestamp);
        timestampDesc.m_id = kMsgTimestamp;
        timestampDesc.m_priority = kHighestPriority;
        Net::EnqueueMessage(this, conn->m_handle, &timestampDesc);

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
            (conn->m_status & kConnStatus1)) {
            // more waiting for acks: take it as high latency and lower the bandwidth
            AddLatency(conn, MaxLatency());
            SetBandwidth(conn, GetBandwidth(conn) - kBandwidthStep);
            if (GetBandwidth(conn) < kMinBandwidth)
                SetBandwidth(conn, kMinBandwidth);
        }

        char*& writePtr = conn->m_writePtr;
        int size = static_cast<int>(writePtr - conn->m_writeBuffer);
        if (size > 0 && IsRemote(conn)) {
            if (!Net::SendTo(this, conn->m_ip, conn->m_port,
                        conn->m_writeBuffer, size, 0)) {
                FlagMessagesForResending(conn, conn->m_timestampOut);
                writePtr = conn->m_writeBuffer;
                break;
            }
            m_totalBytesOut += size;
            AddPacketMetric(conn->m_sendMetrics, size + 0x1C, Timestamp(this));
            SetForceSendThisFrame(conn, false);
            conn->m_lastSendTime = Timestamp(this);
            writePtr = conn->m_writeBuffer;
        }
        ++conn->m_timestampOut;
    } while (!done);

    if (Net::BandwidthExceeded(this, conn) && GetNumPendingMessages(conn) == 0)
        SetBandwidth(conn, GetBandwidth(conn) + kIdleBandwidthStep); // limited, though nothing is waiting
}

// 0x0045ADB0
int Server::HandleConnectionRequest(MsgHandlerContext* context)
{
    MsgDesc desc;
    ConstructMsgDesc(&desc);
    if (context->m_PacketFlags & kPacketForeign) {
        if (!Net::AcceptsForeignConnections(context->m_App))
            return kHandlerMsgDone;
        Conn* conn = context->m_Conn;
        Net::SendMessageTo(context->m_App, kMsgConnectionAccepted, 0, nullptr, conn->m_ip,
                      conn->m_port, 0);
        return kHandlerContinue;
    }
    if (!Net::IsLocal(context->m_Conn))
        return kHandlerMsgDone;
    desc.m_id = kMsgConnectionAccepted;
    desc.m_priority = kHighestPriority;
    desc.m_queue = kQueueImportant;
    Net::EnqueueMessage(context->m_App, context->m_Conn->m_handle, &desc);
    return kHandlerContinue;
}

// 0x0045AE50
int Server::HandleDisconnRequest(MsgHandlerContext* context)
{
    MsgDesc desc;
    ConstructMsgDesc(&desc);
    Conn* conn = context->m_Conn;
    if (conn->m_status & kConnDisconnAccepted)
        return kHandlerMsgDone;
    desc.m_id = kMsgDisconnAccepted;
    Net::EnqueueMessage(context->m_App, conn->m_handle, &desc);
    context->m_Conn->m_status |= kConnDisconnAccepted;
    return kHandlerContinue;
}

// 0x0045AA30
int Server::HandleTimestamp(MsgHandlerContext* context)
{
    context->m_MsgLength = sizeof(uint16_t);
    if (context->m_PacketFlags & kPacketForeign)
        return kHandlerMsgDone;
    // (no bounds check: more than 32 timestamps in a packet would overrun the list)
    context->m_SequencesToAck[context->m_NumSequencesToAck] = *reinterpret_cast<uint16_t*>(context->m_Msg);
    ++context->m_NumSequencesToAck;
    return kHandlerContinue;
}

} // namespace Net

REPLACE(0x0045B830, &Net::Server::Construct);
REPLACE(0x0045B7A0, &Net::Server::Init);
REPLACE(0x0045BBD0, &Net::Server::ReceiveData);
REPLACE(0x0045B920, &Net::Server::SendData);
REPLACE(0x0045ADB0, Net::Server::HandleConnectionRequest);
REPLACE(0x0045AE50, Net::Server::HandleDisconnRequest);
REPLACE(0x0045AA30, Net::Server::HandleTimestamp);
