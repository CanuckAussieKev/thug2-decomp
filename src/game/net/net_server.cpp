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

uint32_t& Timestamp(App* app) { return Field<uint32_t>(app, AppField::kTimestamp); }
Dispatcher* GetDispatcher(App* app) { return &Field<Dispatcher>(app, AppField::kDispatcher); }

using ForeignPacketHandler = void(__cdecl*)(char* data, int size, sockaddr* from);

} // namespace

// 0x0045B830
Server* Server::Construct(void* params)
{
    // -> App::App (wt/netB)
    GameFunction<App*(__thiscall*)(App*, void*)>(0x004594C0)(this, params);
    Field<void**>(this, 0) = kServerVtable;
    ConstructAppTask(this);
    return this;
}

// 0x0045B7A0
bool Server::Init()
{
    Timestamp(this) = 0;
    Field<bool>(this, AppField::kConnected) = false;
    Field<uint32_t>(this, AppField::kServerUnknown1458) = 0;
    if (!AppView::Init())
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
        char* buffer = &Field<char>(this, AppField::kReceiveBuffer);
        received = recvfrom(Field<SOCKET>(this, AppField::kSocket), buffer, kReceiveBufferSize, 0,
                            reinterpret_cast<sockaddr*>(&from), &fromLength);
        if (received < 0) {
            WSAGetLastError();
            return;
        }
        Field<int>(this, AppField::kTotalBytesIn) += received;
        int size = (Field<uint32_t>(this, AppField::kFlags) & kAppSecure) ? received - 2 : received;
        if (Conn* conn = Net::GetConnectionByAddress(this, from.sin_addr.s_addr, ntohs(from.sin_port))) {
            char*& readPtr = Field<char*>(conn, ConnField::kReadPtr);
            if ((IsNew(conn) || !(Field<uint32_t>(this, AppField::kFlags) & kAppSecure)) &&
                readPtr + size < Field<char*>(conn, ConnField::kReadBuffer) + kConnReceiveBufferSize) {
                if (ReadPacket(this, buffer, readPtr, received)) {
                    readPtr += size;
                    AddPacketMetric(&Field<uint8_t>(conn, ConnField::kReceiveMetrics), received + 0x1C,
                                    Timestamp(this));
                    Field<uint32_t>(conn, ConnField::kLastCommTime) = Tmr::GetTime();
                } else if (auto handler = Field<ForeignPacketHandler>(this, AppField::kForeignPacketHandler)) {
                    // (given the connection's read pointer, not the packet - as in the original)
                    handler(readPtr, received, reinterpret_cast<sockaddr*>(&from));
                }
            }
        } else if (Conn* fresh = NewConnection(this, from.sin_addr.s_addr, ntohs(from.sin_port), 6)) {
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
        if (!MessagesToSend(this, conn) || BandwidthExceeded(this, conn))
            break;
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
                desc.m_Data = &now;
                desc.m_Length = sizeof(now);
                desc.m_Id = kMsgLatencyTest;
                EnqueueMessage(this, Field<int>(conn, ConnField::kHandle), &desc);
            }
        }
        // every packet carries a timestamp
        auto timestamp = static_cast<uint16_t>(Field<uint32_t>(conn, ConnField::kTimestampOut));
        timestampDesc.m_Data = &timestamp;
        timestampDesc.m_Length = sizeof(timestamp);
        timestampDesc.m_Id = kMsgTimestamp;
        timestampDesc.m_Priority = kHighestPriority;
        EnqueueMessage(this, Field<int>(conn, ConnField::kHandle), &timestampDesc);

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
            (Field<uint32_t>(conn, ConnField::kStatus) & kConnStatus1)) {
            // more waiting for acks: take it as high latency and lower the bandwidth
            AddLatency(conn, MaxLatency());
            SetBandwidth(conn, GetBandwidth(conn) - kBandwidthStep);
            if (GetBandwidth(conn) < kMinBandwidth)
                SetBandwidth(conn, kMinBandwidth);
        }

        char*& writePtr = Field<char*>(conn, ConnField::kWritePtr);
        int size = static_cast<int>(writePtr - Field<char*>(conn, ConnField::kWriteBuffer));
        if (size > 0 && IsRemote(conn)) {
            if (!Net::SendTo(this, Field<uint32_t>(conn, ConnField::kIp), Field<uint16_t>(conn, ConnField::kPort),
                        Field<char*>(conn, ConnField::kWriteBuffer), size, 0)) {
                FlagMessagesForResending(conn, Field<uint32_t>(conn, ConnField::kTimestampOut));
                writePtr = Field<char*>(conn, ConnField::kWriteBuffer);
                break;
            }
            Field<int>(this, AppField::kTotalBytesOut) += size;
            AddPacketMetric(&Field<uint8_t>(conn, ConnField::kSendMetrics), size + 0x1C, Timestamp(this));
            SetForceSendThisFrame(conn, false);
            Field<uint32_t>(conn, ConnField::kLastSendTime) = Timestamp(this);
            writePtr = Field<char*>(conn, ConnField::kWriteBuffer);
        }
        ++Field<uint32_t>(conn, ConnField::kTimestampOut);
    } while (!done);

    if (BandwidthExceeded(this, conn) && GetNumPendingMessages(conn) == 0)
        SetBandwidth(conn, GetBandwidth(conn) + kIdleBandwidthStep); // limited, though nothing is waiting
}

// 0x0045ADB0
int Server::HandleConnectionRequest(MsgHandlerContext* context)
{
    MsgDesc desc;
    ConstructMsgDesc(&desc);
    if (context->m_PacketFlags & kPacketForeign) {
        if (!AcceptsForeignConnections(context->m_App))
            return kHandlerMsgDone;
        Conn* conn = context->m_Conn;
        Net::SendMessageTo(context->m_App, kMsgConnectionAccepted, 0, nullptr, Field<uint32_t>(conn, ConnField::kIp),
                      Field<uint16_t>(conn, ConnField::kPort), 0);
        return kHandlerContinue;
    }
    if (!Net::IsLocal(context->m_Conn))
        return kHandlerMsgDone;
    desc.m_Id = kMsgConnectionAccepted;
    desc.m_Priority = kHighestPriority;
    desc.m_Queue = kQueueImportant;
    EnqueueMessage(context->m_App, Field<int>(context->m_Conn, ConnField::kHandle), &desc);
    return kHandlerContinue;
}

// 0x0045AE50
int Server::HandleDisconnRequest(MsgHandlerContext* context)
{
    MsgDesc desc;
    ConstructMsgDesc(&desc);
    Conn* conn = context->m_Conn;
    if (Field<uint32_t>(conn, ConnField::kStatus) & kConnDisconnAccepted)
        return kHandlerMsgDone;
    desc.m_Id = kMsgDisconnAccepted;
    EnqueueMessage(context->m_App, Field<int>(conn, ConnField::kHandle), &desc);
    Field<uint32_t>(context->m_Conn, ConnField::kStatus) |= kConnDisconnAccepted;
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
