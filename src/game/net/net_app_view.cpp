// Net::App's socket I/O, connection list and message queues (Neversoft's Net library). See net_app_view.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "game/net/net_app_view.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/search.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Net {

namespace {

void** const kAppVtable = reinterpret_cast<void**>(0x006475B4);
void** const kList1414Vtable = reinterpret_cast<void**>(0x006474D8);

// The ids of the library's own messages (named by what their handlers do).
constexpr uint32_t kMsgPingTest = 0x01;
constexpr uint32_t kMsgPingResponse = 0x02;
constexpr uint32_t kMsgSequenced = 0x07;
constexpr uint32_t kMsgAck = 0x08;
constexpr uint32_t kMsgStreamStart = 0x0E;
constexpr uint32_t kMsgStreamData = 0x0F;
constexpr uint32_t kMsgDisconnect = 0x12;

// Dispatcher::AddHandler's flags (their meaning isn't established; the ack and disconnect handlers get 3, the
// others 2) and the priority Init gives its handlers.
constexpr uint32_t kHandleFlags2 = 2;
constexpr uint32_t kHandleFlags3 = 3;
constexpr uint32_t kHighPriority = 0x80;

// What a message handler returns, as DispatchQueuedSequencedMessages treats it (any other value: done, stop).
constexpr int kHandlerContinue = 1;       // done, go on with the next message
constexpr int kHandlerConnectionGone = 3; // stop, touch nothing more
constexpr int kHandlerKeepMessage = 5;    // stop, the message stays queued

// Net::MsgHandlerContext: what a handler is told about a message (constructor 0x00454A20 clears it).
struct MsgHandlerContext {
    char* m_msg;            // +0x00
    uint8_t m_msgId;        // +0x04
    int m_msgLength;        // +0x08
    AppView* m_app;         // +0x0C
    ConnView* m_conn;       // +0x10
    uint32_t m_packetFlags; // +0x14
    uint8_t m_rest[0xA4 - 0x18];
};
static_assert(offsetof(MsgHandlerContext, m_conn) == 0x10 && sizeof(MsgHandlerContext) == 0xA4,
              "layout must match the original");

void Free(void* memory) { Mem::Manager::Get()->Free(memory); }

// An object deleted through its vtable's scalar deleting destructor (in `slot`).
void DeleteVirtual(void* object, int slot)
{
    auto* vtable = *static_cast<void***>(object);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[slot])(object, 1);
}

// -> Conn's methods (not decompiled yet: below the three net ranges)
uint32_t SendInterval(ConnView* conn) { return GameFunction<uint32_t(__thiscall*)(ConnView*)>(0x00455970)(conn); }
bool SendNow(ConnView* conn) { return GameFunction<bool(__thiscall*)(ConnView*)>(0x00455990)(conn); }
// twice the average latency, at least 500 ms (1000 ms or no wait at all in some modes)
uint32_t ResendThreshold(ConnView* conn) { return GameFunction<uint32_t(__thiscall*)(ConnView*)>(0x00455C30)(conn); }
void DeleteConn(ConnView* conn)
{
    if (conn) {
        GameFunction<void(__thiscall*)(ConnView*)>(0x00456790)(conn); // -> Conn::~Conn
        Free(conn);
    }
}

// -> QueuedMsg's destructor (frees the data)
void DeleteMsg(void* msg)
{
    GameFunction<void(__thiscall*)(void*)>(0x004550F0)(msg);
    Free(msg);
}

// -> Dispatcher (wt/netC)
void DispatcherInit(HandlerLists* dispatcher) { GameFunction<void(__thiscall*)(HandlerLists*)>(0x0045A640)(dispatcher); }
void DispatcherDeinit(HandlerLists* dispatcher)
{
    GameFunction<void(__thiscall*)(HandlerLists*)>(0x0045A690)(dispatcher);
}
void AddHandler(HandlerLists* dispatcher, uint32_t id, uint32_t handler, uint32_t flags, uint32_t priority)
{
    GameFunction<void(__thiscall*)(HandlerLists*, uint32_t, uint32_t, uint32_t, void*, uint32_t)>(0x0045A5C0)(
        dispatcher, id, handler, flags, nullptr, priority);
}
int DispatchMessage(HandlerLists* dispatcher, MsgHandlerContext* context)
{
    return GameFunction<int(__thiscall*)(HandlerLists*, MsgHandlerContext*)>(0x0045A750)(dispatcher, context);
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
void AppView::Encrypt(const uint8_t* data, uint8_t* out, int length, int* outLength)
{
    uint16_t checksum = Checksum(data, length);
    std::memcpy(out, &checksum, sizeof(checksum));
    for (int i = 0; i < length; ++i)
        out[2 + i] = data[i] ^ static_cast<uint8_t>(checksum);
    *outLength = length + 2;
}

// 0x00456CC0
bool AppView::Decrypt(const uint8_t* packet, uint8_t* out, int length)
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
void AppView::ReportError()
{
    char message[0x400];
    std::snprintf(message, sizeof(message), "%s: Error %d\n", m_name, WSAGetLastError());
    OutputDebugStringA(message);
}

// 0x00456B00
bool AppView::IsSendDue(ConnView* conn)
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
void AppView::AliasConnections(ConnView* first, ConnView* second)
{
    first->m_aliasConn = second;
    second->m_aliasConn = first;
}

// 0x00456BA0
bool AppView::Bind(uint32_t /*address*/, uint16_t port)
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
SeqQueuedMsg* AppView::NewQueuedMsg(uint8_t id, uint16_t length, void* data, uint8_t arg4, uint8_t arg5)
{
    void* memory = Mem::New(sizeof(SeqQueuedMsg));
    if (!memory)
        return nullptr;
    // -> SeqQueuedMsg's constructor (copies the data, compressing it if `arg5`)
    return GameFunction<SeqQueuedMsg*(__thiscall*)(void*, uint8_t, uint16_t, void*, uint8_t, uint8_t)>(0x00455040)(
        memory, id, length, data, arg4, arg5);
}

// 0x00456E20
bool AppView::SendTo(uint32_t ip, uint16_t port, const uint8_t* data, int length, int flags)
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
bool AppView::Send(const uint8_t* data, int length, int flags)
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
bool AppView::Init()
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
    for (uint32_t& counter : m_counters)
        counter = 0;
    DispatcherInit(&m_dispatcher);
    // -> the handlers (wt/netC)
    AddHandler(&m_dispatcher, kMsgAck, 0x0045B0B0, kHandleFlags3, kHighPriority);
    AddHandler(&m_dispatcher, kMsgSequenced, 0x0045AF10, kHandleFlags2, kHighPriority);
    AddHandler(&m_dispatcher, kMsgDisconnect, 0x0045ABF0, kHandleFlags3, kHighPriority);
    AddHandler(&m_dispatcher, kMsgStreamStart, 0x0045B2F0, kHandleFlags2, kHighPriority);
    AddHandler(&m_dispatcher, kMsgStreamData, 0x0045B2F0, kHandleFlags2, kHighPriority);
    AddHandler(&m_dispatcher, kMsgPingTest, 0x0045AD60, kHandleFlags2, kHighPriority);
    AddHandler(&m_dispatcher, kMsgPingResponse, 0x0045AB60, kHandleFlags2, kHighPriority);
    return true;
}

// 0x00457280
bool AppView::SendMessageTo(uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port, int flags)
{
    uint8_t packet[kMaxPacketSize];
    packet[0] = id;
    std::memcpy(packet + 1, &length, sizeof(length));
    if (length > 0)
        std::memcpy(packet + 3, data, length);
    return SendTo(ip, port, packet, length + 3, flags);
}

// 0x00457300
void AppView::TerminateConnection(ConnView* conn)
{
    Remove(&conn->m_appNode);
    DeleteConn(conn);
}

// 0x004573D0
ConnView* AppView::NextConnection(ConnSearch* search)
{
    Mem::ListNode* next = search->m_node->next;
    search->m_node = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
    return search->m_node ? static_cast<ConnView*>(search->m_node->data) : nullptr;
}

// 0x00457400
void AppView::BanConnection(ConnView* conn)
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
void AppView::Deinit()
{
    if (!(m_flags & kLocal))
        closesocket(m_socket);
    DispatcherDeinit(&m_dispatcher);
    Lst::Search<ConnView> search;
    for (ConnView *conn = search.FirstItem(&m_connections), *next; conn; conn = next) {
        next = search.NextItem();
        DeleteConn(conn);
    }
}

// 0x00457560
bool AppView::DispatchQueuedSequencedMessages(ConnView* conn)
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
            context.m_conn = conn;
            context.m_msg = msg->m_data;
            context.m_app = this;
            context.m_packetFlags = 0;
            context.m_msgId = msg->m_id;
            context.m_msgLength = msg->m_length;
            ++conn->m_sequenceNumbers[group];
            int result = DispatchMessage(&m_dispatcher, &context);
            if (result == kHandlerKeepMessage)
                return true;
            if (result == kHandlerConnectionGone)
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
void AppView::DestroyBody()
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
ConnView* AppView::GetConnectionByAddress(uint32_t ip, uint16_t port)
{
    Lst::Search<ConnView> search;
    for (ConnView* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem()) {
        if (conn->m_ip == ip && conn->m_port == port)
            return conn;
    }
    return nullptr;
}

// 0x00457910
ConnView* AppView::GetConnectionByHandle(int handle)
{
    Lst::Search<ConnView> search;
    for (ConnView* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem()) {
        if (conn->m_handle == handle)
            return conn;
    }
    return nullptr;
}

// 0x00457970
bool AppView::ImportantMessagesDue(ConnView* conn)
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
bool AppView::MessagesDue(ConnView* conn)
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
void AppView::FreeMessages(ConnView* conn, int queue, uint8_t id)
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

REPLACE(0x00456910, &Net::AppView::Encrypt);
REPLACE(0x00456CC0, &Net::AppView::Decrypt);
REPLACE(0x00456A50, &Net::AppView::SendTask);
REPLACE(0x00456A60, &Net::AppView::ReceiveTask);
REPLACE(0x00456A70, &Net::AppView::ProcessTask);
REPLACE(0x00456A80, &Net::AppView::MetricsTask);
REPLACE(0x00456AF0, &Net::AppView::NetManager);
REPLACE(0x00456B60, &Net::AppView::Socket);
REPLACE(0x00456B70, &Net::AppView::SetValue1E88);
REPLACE(0x00456F90, &Net::AppView::IsLocal);
REPLACE(0x00456FA0, &Net::AppView::HasFlag10);
REPLACE(0x00456A90, &Net::AppView::ReportError);
REPLACE(0x00456B00, &Net::AppView::IsSendDue);
REPLACE_UNCHECKED(0x00456B30, Net::VirtualSlot5Jump);
REPLACE(0x00456B40, &Net::AppView::AliasConnections);
REPLACE(0x00456BA0, &Net::AppView::Bind);
REPLACE(0x00456D90, &Net::AppView::NewQueuedMsg);
REPLACE(0x00456E20, &Net::AppView::SendTo);
REPLACE(0x00456F10, &Net::AppView::Send);
REPLACE(0x00457110, &Net::AppView::Init);
REPLACE(0x00457280, &Net::AppView::SendMessageTo);
REPLACE(0x00457300, &Net::AppView::TerminateConnection);
REPLACE(0x004573D0, &Net::AppView::NextConnection);
REPLACE(0x00457400, &Net::AppView::BanConnection);
REPLACE(0x00457490, &Net::AppView::Deinit);
REPLACE(0x00457560, &Net::AppView::DispatchQueuedSequencedMessages);
REPLACE(0x00457710, &Net::AppView::DestroyBody);
REPLACE(0x004578B0, &Net::AppView::GetConnectionByAddress);
REPLACE(0x00457910, &Net::AppView::GetConnectionByHandle);
REPLACE(0x00457970, &Net::AppView::ImportantMessagesDue);
REPLACE(0x00457AC0, &Net::AppView::MessagesDue);
REPLACE(0x00457C80, &Net::AppView::FreeMessages);
