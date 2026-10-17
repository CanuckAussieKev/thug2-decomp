// Net::App's socket I/O, connection list and message queues (Neversoft's Net library). See net_app_view.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "game/net/net_app.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/search.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app_messages.h"
#include "game/net/net_dispatcher.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Net {

namespace {

void** const kAppVtable = reinterpret_cast<void**>(0x006475B4);
void** const kList1414Vtable = reinterpret_cast<void**>(0x006474D8);

// (The message ids and handler results: net_access_c.h, net_dispatcher.h.)

// Dispatcher::AddHandler's packet-flags masks (3: foreign packets too) and the priority Init gives its handlers.
constexpr uint32_t kHandleFlags2 = 2;
constexpr uint32_t kHandleFlags3 = 3;
constexpr uint32_t kHighPriority = 0x80;

// (Net::MsgHandlerContext: net_dispatcher.h)

void Free(void* memory) { Mem::Manager::Get()->Free(memory); }

// An object deleted through its vtable's scalar deleting destructor (in `slot`).
void DeleteVirtual(void* object, int slot)
{
    auto* vtable = *static_cast<void***>(object);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[slot])(object, 1);
}

// Conn's methods (net_conn.cpp)
uint32_t SendInterval(Conn* conn) { return conn->GetSendInterval(); }
bool SendNow(Conn* conn) { return conn->GetForceSendThisFrame(); }
// twice the average latency, at least 500 ms (1000 ms with status bit 2)
uint32_t ResendThreshold(Conn* conn) { return conn->GetResendThreshold(); }
void DeleteConn(Conn* conn)
{
    if (conn) {
        conn->Destroy();
        Free(conn);
    }
}

// QueuedMsg's destructor (frees the data)
void DeleteMsg(void* msg)
{
    static_cast<QueuedMsg*>(msg)->DestroyBody();
    Free(msg);
}

// The Dispatcher (net_dispatcher.h) the App's handler lists are.
void DispatcherInit(HandlerLists* dispatcher) { static_cast<Dispatcher*>(dispatcher)->Init(); }
void DispatcherDeinit(HandlerLists* dispatcher) { static_cast<Dispatcher*>(dispatcher)->Deinit(); }
void AddHandler(HandlerLists* dispatcher, uint32_t id, MsgHandlerCode handler, uint32_t flags, uint32_t priority)
{
    static_cast<Dispatcher*>(dispatcher)->AddHandler(static_cast<uint8_t>(id), handler, flags, nullptr,
                                                     static_cast<int>(priority));
}
int DispatchMessage(HandlerLists* dispatcher, MsgHandlerContext* context)
{
    return static_cast<Dispatcher*>(dispatcher)->DispatchMsg(context);
}

uint16_t Checksum(const uint8_t* data, int length)
{
    uint32_t crc = Crc::GenerateCRCFromBuffer(data, length);
    return static_cast<uint16_t>(crc + (crc >> 16));
}

// `node` out of its list (and linked to itself; its vtable stays).
void Remove(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// 0x00456910
void App::Encrypt(const uint8_t* data, uint8_t* out, int length, int* outLength)
{
    uint16_t checksum = Checksum(data, length);
    std::memcpy(out, &checksum, sizeof(checksum));
    for (int i = 0; i < length; ++i)
        out[2 + i] = data[i] ^ static_cast<uint8_t>(checksum);
    *outLength = length + 2;
}

// 0x00456CC0
bool App::Decrypt(const uint8_t* packet, uint8_t* out, int length)
{
    if (length < 3)
        return false;
    if (!(m_flags & kSecure)) {
        std::memcpy(out, packet, length);
        return true;
    }
    uint16_t checksum;
    std::memcpy(&checksum, packet, sizeof(checksum));
    length -= 2;
    uint8_t buffer[kMaxPacketSize]; // (a longer packet overruns it, as in the original)
    std::memcpy(buffer, packet + 2, length);
    for (int i = 0; i < length; ++i)
        buffer[i] ^= static_cast<uint8_t>(checksum);
    if (Checksum(buffer, length) != checksum)
        return false;
    std::memcpy(out, buffer, length);
    return true;
}

// 0x00456A90
void App::ReportError()
{
    char message[0x400];
    std::snprintf(message, sizeof(message), "%s: Error %d\n", m_name, WSAGetLastError());
    OutputDebugStringA(message);
}

// 0x00456B00
bool App::IsSendDue(Conn* conn)
{
    uint32_t interval = SendInterval(conn);
    if (m_time - conn->m_lastSendTime >= interval)
        return true;
    return SendNow(conn);
}

// 0x00456B30
__declspec(naked) void VirtualSlot5Jump()
{
    __asm {
        mov eax, [ecx]
        jmp dword ptr [eax + 0x14]
    }
}

// 0x00456B40
void App::AliasConnections(Conn* first, Conn* second)
{
    first->m_aliasConn = second;
    second->m_aliasConn = first;
}

// 0x00456BA0
bool App::Bind(uint32_t /*address*/, uint16_t port)
{
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    // (The original asks the manager for its +0x128 value and sets the address to INADDR_ANY unless it's 1 - it's
    // INADDR_ANY already either way.)
    if (GameFunction<int(__thiscall*)(void*)>(0x00454960)(m_manager) != 1)
        local.sin_addr.s_addr = INADDR_ANY;
    if (bind(m_socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        if (WSAGetLastError() != WSAEADDRINUSE) {
            ReportError();
            return false;
        }
        ReportError();
        local.sin_port = htons(0);
        if (bind(m_socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
            ReportError();
            return false;
        }
    }
    int length = sizeof(sockaddr_in);
    if (getsockname(m_socket, reinterpret_cast<sockaddr*>(m_localAddress), &length) < 0) {
        ReportError();
        return false;
    }
    return true;
}

// 0x00456D90
SeqQueuedMsg* App::NewQueuedMsg(uint8_t id, uint16_t length, void* data, uint8_t arg4, uint8_t arg5)
{
    void* memory = Mem::New(sizeof(SeqQueuedMsg));
    if (!memory)
        return nullptr;
    // SeqQueuedMsg's constructor (copies the data, compressing it if `arg5`)
    return static_cast<SeqQueuedMsg*>(memory)->Construct(id, length, data, arg4, arg5);
}

// 0x00456E20
bool App::SendTo(uint32_t ip, uint16_t port, const uint8_t* data, int length, int flags)
{
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = ip;
    int result;
    if (m_flags & kSecure) {
        Encrypt(data, m_scrambled, length, &length);
        result = sendto(m_socket, reinterpret_cast<const char*>(m_scrambled), length, flags,
                        reinterpret_cast<sockaddr*>(&to), sizeof(to));
    } else {
        result = sendto(m_socket, reinterpret_cast<const char*>(data), length, flags, reinterpret_cast<sockaddr*>(&to),
                        sizeof(to));
    }
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
            return false;
    }
    return true;
}

// 0x00456F10
bool App::Send(const uint8_t* data, int length, int flags)
{
    if (!m_connected)
        return false;
    if (m_flags & kSecure) {
        Encrypt(data, m_scrambled, length, &length);
        return send(m_socket, reinterpret_cast<const char*>(m_scrambled), length, flags) != SOCKET_ERROR;
    }
    return send(m_socket, reinterpret_cast<const char*>(data), length, flags) != SOCKET_ERROR;
}

// 0x00457110
bool App::Init()
{
    if (!(m_flags & kLocal)) {
        m_socket = static_cast<uint32_t>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (m_socket == INVALID_SOCKET) {
            ReportError();
            return false;
        }
        u_long on = 1;
        if (ioctlsocket(m_socket, FIONBIO, &on) == SOCKET_ERROR) {
            ReportError();
            return false;
        }
        if ((m_flags & kBroadcast) &&
            setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on)) ==
                SOCKET_ERROR) {
            ReportError();
            return false;
        }
    }
    m_numConnections = 0;
    m_totalBytesOut = 0;
    m_totalBytesIn = 0;
    m_lostPackets = 0;
    m_latePackets = 0;
    m_dupePackets = 0;
    m_frameCount = 0;
    DispatcherInit(&m_dispatcher);
    // the handlers (net_app_messages.cpp)
    AddHandler(&m_dispatcher, kMsgAck, App::HandleAck, kHandleFlags3, kHighPriority);
    AddHandler(&m_dispatcher, kMsgSequenced, App::HandleSequencedMessages, kHandleFlags2, kHighPriority);
    AddHandler(&m_dispatcher, kMsgCompressed, App::HandleCompressedMessages, kHandleFlags3, kHighPriority);
    AddHandler(&m_dispatcher, kMsgStreamStart, App::HandleStreamMessages, kHandleFlags2, kHighPriority);
    AddHandler(&m_dispatcher, kMsgStreamData, App::HandleStreamMessages, kHandleFlags2, kHighPriority);
    AddHandler(&m_dispatcher, kMsgLatencyTest, App::HandleLatencyTest, kHandleFlags2, kHighPriority);
    AddHandler(&m_dispatcher, kMsgLatencyResponse, App::HandleLatencyResponse, kHandleFlags2, kHighPriority);
    return true;
}

// 0x00457280
bool App::SendMessageTo(uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port, int flags)
{
    uint8_t packet[kMaxPacketSize];
    packet[0] = id;
    std::memcpy(packet + 1, &length, sizeof(length));
    if (length > 0)
        std::memcpy(packet + 3, data, length);
    return SendTo(ip, port, packet, length + 3, flags);
}

// 0x00457300
void App::TerminateConnection(Conn* conn)
{
    Remove(&conn->m_appNode);
    DeleteConn(conn);
}

// 0x004573D0
Conn* App::NextConnection(ConnSearch* search)
{
    Mem::ListNode* next = search->m_node->next;
    search->m_node = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
    return search->m_node ? static_cast<Conn*>(search->m_node->data) : nullptr;
}

// 0x00457400
void App::BanConnection(Conn* conn)
{
    auto* banned = static_cast<BannedConn*>(Mem::Calloc(sizeof(BannedConn)));
    if (banned)
        *static_cast<Mem::ListNode*>(banned) = {kBannedConnVtable, banned, 0, banned, banned};
    banned->m_ip = conn->m_ip; // (an allocation failure crashes here, as in the original)
    // at the end of the list
    banned->prev = m_banned.prev;
    banned->next = &m_banned;
    m_banned.prev->next = banned;
    m_banned.prev = banned;
}

// 0x00457490
void App::Deinit()
{
    if (!(m_flags & kLocal))
        closesocket(m_socket);
    DispatcherDeinit(&m_dispatcher);
    Lst::Search<Conn> search;
    for (Conn *conn = search.FirstItem(&m_connections), *next; conn; conn = next) {
        next = search.NextItem();
        DeleteConn(conn);
    }
}

// 0x00457560
bool App::DispatchQueuedSequencedMessages(Conn* conn)
{
    MsgHandlerContext context;
    GameFunction<MsgHandlerContext*(__thiscall*)(MsgHandlerContext*)>(0x00454A20)(&context);
    for (Mem::ListNode& queue : conn->m_receivedQueues) {
        Lst::Search<MsgLink> search;
        for (MsgLink *link = search.FirstItem(&queue), *next; link; link = next) {
            next = search.NextItem();
            uint8_t group = link->At<uint8_t>(0x15);
            if (link->At<uint32_t>(0x20) != conn->m_sequenceNumbers[group])
                break; // not its turn yet
            auto* msg = link->At<SeqQueuedMsg*>(0x28);
            context.m_Conn = static_cast<Conn*>(conn);
            context.m_Msg = msg->m_data;
            context.m_App = static_cast<App*>(this);
            context.m_PacketFlags = 0;
            context.m_MsgId = msg->m_id;
            context.m_MsgLength = msg->m_length;
            ++conn->m_sequenceNumbers[group];
            int result = DispatchMessage(&m_dispatcher, &context);
            if (result == kHandlerKeep)
                return true;
            if (result == kHandlerMsgDestroyed)
                return false;
            if (msg)
                DeleteMsg(msg);
            DeleteVirtual(link, 0);
            if (result != kHandlerContinue)
                return true;
        }
    }
    return true;
}

// 0x00457710
void App::DestroyBody()
{
    m_vtable = kAppVtable;
    Lst::Search<BannedConn> search;
    for (BannedConn *banned = search.FirstItem(&m_banned), *next; banned; banned = next) {
        next = search.NextItem();
        if (banned)
            DeleteVirtual(banned, 0);
    }
    for (void* task : {m_sendTask, m_receiveTask, m_processTask, m_metricsTask}) {
        if (task)
            DeleteVirtual(task, 2);
    }
    m_banned.UnlinkAs(kBannedConnNodeVtable);
    m_connections.UnlinkAs(kConnNodeVtable);
    m_list1414.UnlinkAs(kList1414Vtable);
    m_dispatcher.Destroy();
}

// 0x004578B0
Conn* App::GetConnectionByAddress(uint32_t ip, uint16_t port)
{
    Lst::Search<Conn> search;
    for (Conn* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem()) {
        if (conn->m_ip == ip && conn->m_port == port)
            return conn;
    }
    return nullptr;
}

// 0x00457910
Conn* App::GetConnectionByHandle(int handle)
{
    Lst::Search<Conn> search;
    for (Conn* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem()) {
        if (conn->m_handle == handle)
            return conn;
    }
    return nullptr;
}

// 0x00457970
bool App::ImportantMessagesDue(Conn* conn)
{
    Lst::Search<MsgLink> important;
    for (MsgLink* link = important.FirstItem(&conn->m_importantMsgs); link; link = important.NextItem()) {
        uint32_t sent = link->At<uint32_t>(0x14);
        if (!sent || m_time - sent > ResendThreshold(conn))
            return true;
    }
    Lst::Search<MsgLink> sequenced;
    for (MsgLink* link = sequenced.FirstItem(&conn->m_sequencedMsgs); link; link = sequenced.NextItem()) {
        uint32_t sent = link->At<uint32_t>(0x1C);
        if (!sent || m_time - sent > ResendThreshold(conn))
            return true;
    }
    return false;
}

// 0x00457AC0
bool App::MessagesDue(Conn* conn)
{
    Lst::Search<MsgLink> normal;
    for (MsgLink* link = normal.FirstItem(&conn->m_normalMsgs); link; link = normal.NextItem()) {
        if (m_time >= link->At<uint32_t>(0x18))
            return true;
    }
    Lst::Search<MsgLink> important;
    for (MsgLink* link = important.FirstItem(&conn->m_importantMsgs); link; link = important.NextItem()) {
        uint32_t sent = link->At<uint32_t>(0x14);
        if (!sent || m_time < link->At<uint32_t>(0x24) || m_time - sent > ResendThreshold(conn))
            return true;
    }
    Lst::Search<MsgLink> sequenced;
    for (MsgLink* link = sequenced.FirstItem(&conn->m_sequencedMsgs); link; link = sequenced.NextItem()) {
        uint32_t sent = link->At<uint32_t>(0x1C);
        if (!sent || m_time < link->At<uint32_t>(0x24) || m_time - sent > ResendThreshold(conn))
            return true;
    }
    return false;
}

// 0x00457C80
void App::FreeMessages(Conn* conn, int queue, uint8_t id)
{
    Mem::ListNode* list;
    uint32_t msgOffset;
    switch (queue) {
    case 0:
        list = &conn->m_normalMsgs;
        msgOffset = 0x14;
        break;
    case 1:
        list = &conn->m_importantMsgs;
        msgOffset = 0x18;
        break;
    case 2:
        list = &conn->m_sequencedMsgs;
        msgOffset = 0x28;
        break;
    default:
        return;
    }
    Lst::Search<MsgLink> search;
    for (MsgLink *link = search.FirstItem(list), *next; link; link = next) {
        next = search.NextItem();
        void* msg = link->At<void*>(msgOffset);
        if (static_cast<QueuedMsg*>(msg)->m_id != id)
            continue;
        int& refCount =
            queue == 2 ? static_cast<SeqQueuedMsg*>(msg)->m_refCount : static_cast<QueuedMsg*>(msg)->m_refCount;
        if (--refCount == 0)
            DeleteMsg(msg);
        DeleteVirtual(link, 0);
    }
}

} // namespace Net

REPLACE(0x00456910, &Net::App::Encrypt);
REPLACE(0x00456CC0, &Net::App::Decrypt);
REPLACE(0x00456A50, &Net::App::SendTask);
REPLACE(0x00456A60, &Net::App::ReceiveTask);
REPLACE(0x00456A70, &Net::App::ProcessTask);
REPLACE(0x00456A80, &Net::App::MetricsTask);
REPLACE(0x00456AF0, &Net::App::NetManager);
REPLACE(0x00456B60, &Net::App::Socket);
REPLACE(0x00456B70, &Net::App::SetForeignPacketHandler);
REPLACE(0x00456F90, &Net::App::IsLocal);
REPLACE(0x00456FA0, &Net::App::AcceptsForeignConnections);
REPLACE(0x00456A90, &Net::App::ReportError);
REPLACE(0x00456B00, &Net::App::IsSendDue);
REPLACE_UNCHECKED(0x00456B30, Net::VirtualSlot5Jump);
REPLACE(0x00456B40, &Net::App::AliasConnections);
REPLACE(0x00456BA0, &Net::App::Bind);
REPLACE(0x00456D90, &Net::App::NewQueuedMsg);
REPLACE(0x00456E20, &Net::App::SendTo);
REPLACE(0x00456F10, &Net::App::Send);
REPLACE(0x00457110, &Net::App::Init);
REPLACE(0x00457280, &Net::App::SendMessageTo);
REPLACE(0x00457300, &Net::App::TerminateConnection);
REPLACE(0x004573D0, &Net::App::NextConnection);
REPLACE(0x00457400, &Net::App::BanConnection);
REPLACE(0x00457490, &Net::App::Deinit);
REPLACE(0x00457560, &Net::App::DispatchQueuedSequencedMessages);
REPLACE(0x00457710, &Net::App::DestroyBody);
REPLACE(0x004578B0, &Net::App::GetConnectionByAddress);
REPLACE(0x00457910, &Net::App::GetConnectionByHandle);
REPLACE(0x00457970, &Net::App::ImportantMessagesDue);
REPLACE(0x00457AC0, &Net::App::MessagesDue);
REPLACE(0x00457C80, &Net::App::FreeMessages);
