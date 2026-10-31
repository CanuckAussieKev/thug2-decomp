// NetLink::AppHost's socket I/O, connection list and message queues (Neversoft's Net library). See net_app.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "game/net/net_app.h"

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lists/list_walk.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app_messages.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_messages.h"

namespace NetLink {

namespace {

// (The message ids and handler results: net_access_c.h, net_dispatcher.h.)

// Router::AppendListener's packet-flags masks (3: foreign packets too) and the priority Init gives its handlers.
constexpr uint32_t kHandleFlags2 = 2;
constexpr uint32_t kHandleFlags3 = 3;
constexpr uint32_t kHighPriority = 0x80;

// (NetLink::InboundMsgCtx: net_dispatcher.h)

void Dealloc(void* memory) { MemSys::Hub::Pull()->Dealloc(memory); }

// An object deleted through its vtable's scalar deleting destructor (in `slot`).
void DeleteVirtual(void* object, int slot)
{
    auto* vtable = *static_cast<void***>(object);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[slot])(object, 1);
}

// Conn's methods (net_conn.cpp)
uint32_t SendInterval(Conn* conn) { return conn->GetTransmitPeriod(); }
bool SendNow(Conn* conn) { return conn->IsSendForced(); }
// twice the average latency, at least 500 ms (1000 ms with status bit 2)
uint32_t ResendThreshold(Conn* conn) { return conn->GetRetryDelay(); }
void DeleteConn(Conn* conn)
{
    if (conn) {
        conn->Teardown();
        Dealloc(conn);
    }
}

// PendingMsg's destructor (frees the data)
void DeleteMsg(void* msg)
{
    static_cast<PendingMsg*>(msg)->DestroyBody();
    Dealloc(msg);
}

// The Dispatcher (net_dispatcher.h) the App's handler lists are.
void DispatcherInit(HandlerLists* dispatcher) { static_cast<Router*>(dispatcher)->PrepareAll(); }
void DispatcherDeinit(HandlerLists* dispatcher) { static_cast<Router*>(dispatcher)->TearDownState(); }
void AppendListener(HandlerLists* dispatcher, uint32_t id, HandlerSlotValue handler, uint32_t flags, uint32_t priority)
{
    static_cast<Router*>(dispatcher)->AppendListener(static_cast<uint8_t>(id), handler, flags, nullptr,
                                                     static_cast<int>(priority));
}
int DispatchMessage(HandlerLists* dispatcher, InboundMsgCtx* context)
{
    return static_cast<Router*>(dispatcher)->DispatchMsg(context);
}

uint16_t NameHash(const uint8_t* data, int length)
{
    uint32_t crc = Checksums::GenerateCRCFromBuffer(data, length);
    return static_cast<uint16_t>(crc + (crc >> 16));
}

// `node` out of its list (and linked to itself; its vtable stays).
void DropOut(MemSys::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// 0x00456910
void AppHost::Encrypt(const uint8_t* data, uint8_t* out, int length, int* outLength)
{
    uint16_t checksum = NameHash(data, length);
    std::memcpy(out, &checksum, sizeof(checksum));
    for (int i = 0; i < length; ++i)
        out[2 + i] = data[i] ^ static_cast<uint8_t>(checksum);
    *outLength = length + 2;
}

// 0x00456CC0
bool AppHost::Decrypt(const uint8_t* packet, uint8_t* out, int length)
{
    if (length < 3)
        return false;
    if (!(m_flagBits & kSecure)) {
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
    if (NameHash(buffer, length) != checksum)
        return false;
    std::memcpy(out, buffer, length);
    return true;
}

// 0x00456A90
void AppHost::RaiseFault()
{
    char message[0x400];
    std::snprintf(message, sizeof(message), "%s: Error %d\n", m_label, WSAGetLastError());
    OutputDebugStringA(message);
}

// 0x00456B00
bool AppHost::IsSendDue(Conn* conn)
{
    uint32_t interval = SendInterval(conn);
    if (m_clock - conn->m_lastSendTime >= interval)
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
void AppHost::LinkDuplicateConns(Conn* first, Conn* second)
{
    first->m_aliasConn = second;
    second->m_aliasConn = first;
}

// 0x00456BA0
bool AppHost::Bind(uint32_t /*address*/, uint16_t port)
{
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    // (The original asks the manager for its +0x128 value and sets the address to INADDR_ANY unless it's 1 - it's
    // INADDR_ANY already either way.)
    if (static_cast<Hub*>(m_supervisor)->GetNetLinkKind() != Hub::kConnectionModem)
        local.sin_addr.s_addr = INADDR_ANY;
    if (bind(m_sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        if (WSAGetLastError() != WSAEADDRINUSE) {
            RaiseFault();
            return false;
        }
        RaiseFault();
        local.sin_port = htons(0);
        if (bind(m_sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
            RaiseFault();
            return false;
        }
    }
    int length = sizeof(sockaddr_in);
    if (getsockname(m_sock, reinterpret_cast<sockaddr*>(m_localAddress), &length) < 0) {
        RaiseFault();
        return false;
    }
    return true;
}

// 0x00456D90
SeqQueuedMsg* AppHost::NewQueuedMsg(uint8_t id, uint16_t length, void* data, uint8_t arg4, uint8_t arg5)
{
    void* memory = MemSys::Alloc(sizeof(SeqQueuedMsg));
    if (!memory)
        return nullptr;
    // SeqQueuedMsg's constructor (copies the data, compressing it if `arg5`)
    return static_cast<SeqQueuedMsg*>(memory)->MakeNew(id, length, data, arg4, arg5);
}

// 0x00456E20
bool AppHost::TransmitTo(uint32_t ip, uint16_t port, const uint8_t* data, int length, int flags)
{
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = ip;
    int result;
    if (m_flagBits & kSecure) {
        Encrypt(data, m_scrambled, length, &length);
        result = sendto(m_sock, reinterpret_cast<const char*>(m_scrambled), length, flags,
                        reinterpret_cast<sockaddr*>(&to), sizeof(to));
    } else {
        result = sendto(m_sock, reinterpret_cast<const char*>(data), length, flags, reinterpret_cast<sockaddr*>(&to),
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
bool AppHost::PostOut(const uint8_t* data, int length, int flags)
{
    if (!m_linkedUp)
        return false;
    if (m_flagBits & kSecure) {
        Encrypt(data, m_scrambled, length, &length);
        return send(m_sock, reinterpret_cast<const char*>(m_scrambled), length, flags) != SOCKET_ERROR;
    }
    return send(m_sock, reinterpret_cast<const char*>(data), length, flags) != SOCKET_ERROR;
}

// 0x00457110
bool AppHost::PrepareAll()
{
    if (!(m_flagBits & kLocal)) {
        m_sock = static_cast<uint32_t>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (m_sock == INVALID_SOCKET) {
            RaiseFault();
            return false;
        }
        u_long on = 1;
        if (ioctlsocket(m_sock, FIONBIO, &on) == SOCKET_ERROR) {
            RaiseFault();
            return false;
        }
        if ((m_flagBits & kBroadcast) &&
            setsockopt(m_sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on)) ==
                SOCKET_ERROR) {
            RaiseFault();
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
    AppendListener(&m_dispatcher, kMsgAck, AppHost::HandleAck, kHandleFlags3, kHighPriority);
    AppendListener(&m_dispatcher, kMsgSequenced, AppHost::HandleSequencedMessages, kHandleFlags2, kHighPriority);
    AppendListener(&m_dispatcher, kMsgCompressed, AppHost::HandleCompressedMessages, kHandleFlags3, kHighPriority);
    AppendListener(&m_dispatcher, kMsgStreamStart, AppHost::HandleStreamMessages, kHandleFlags2, kHighPriority);
    AppendListener(&m_dispatcher, kMsgStreamData, AppHost::HandleStreamMessages, kHandleFlags2, kHighPriority);
    AppendListener(&m_dispatcher, kMsgLatencyTest, AppHost::HandleLatencyTest, kHandleFlags2, kHighPriority);
    AppendListener(&m_dispatcher, kMsgLatencyResponse, AppHost::HandleLatencyResponse, kHandleFlags2, kHighPriority);
    return true;
}

// 0x00457280
bool AppHost::DeliverTo(uint8_t id, uint16_t length, const void* data, uint32_t ip, uint16_t port, int flags)
{
    uint8_t packet[kMaxPacketSize];
    packet[0] = id;
    std::memcpy(packet + 1, &length, sizeof(length));
    if (length > 0)
        std::memcpy(packet + 3, data, length);
    return TransmitTo(ip, port, packet, length + 3, flags);
}

// 0x00457300
void AppHost::SeverLink(Conn* conn)
{
    DropOut(&conn->m_appNode);
    DeleteConn(conn);
}

// 0x004573D0
Conn* AppHost::StepConnWalk(ConnSearch* search)
{
    MemSys::ListNode* next = search->m_listLink->next;
    search->m_listLink = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
    return search->m_listLink ? static_cast<Conn*>(search->m_listLink->data) : nullptr;
}

// 0x00457400
void AppHost::BlockConnection(Conn* conn)
{
    auto* banned = static_cast<BlockedConnEntry*>(MemSys::AllocZeroed(sizeof(BlockedConnEntry)));
    if (banned)
        *static_cast<MemSys::ListNode*>(banned) = {kBannedConnVtable, banned, 0, banned, banned};
    banned->m_hostAddr = conn->m_hostAddr; // (an allocation failure crashes here, as in the original)
    // at the end of the list
    banned->prev = m_banned.prev;
    banned->next = &m_banned;
    m_banned.prev->next = banned;
    m_banned.prev = banned;
}

// 0x00457490
void AppHost::TearDownState()
{
    if (!(m_flagBits & kLocal))
        closesocket(m_sock);
    DispatcherDeinit(&m_dispatcher);
    ListKit::Hunt<Conn> search;
    for (Conn *conn = search.HeadElement(&m_circuits), *next; conn; conn = next) {
        next = search.FollowingItem();
        DeleteConn(conn);
    }
}

// 0x00457560
bool AppHost::DispatchQueuedSequencedMessages(Conn* conn)
{
    InboundMsgCtx context;
    context.MakeNew();
    for (MemSys::ListNode& queue : conn->m_receivedQueues) {
        ListKit::Hunt<TimedMsgNode> search;
        for (TimedMsgNode *link = search.HeadElement(&queue), *next; link; link = next) {
            next = search.FollowingItem();
            uint8_t group = link->At<uint8_t>(0x15);
            if (link->At<uint32_t>(0x20) != conn->m_sequenceNumbers[group])
                break; // not its turn yet
            auto* msg = link->At<SeqQueuedMsg*>(0x28);
            context.m_senderConn = static_cast<Conn*>(conn);
            context.m_body = msg->m_content;
            context.m_receiverApp = static_cast<AppHost*>(this);
            context.m_wireOptions = 0;
            context.m_typeCode = msg->m_ident;
            context.m_bodySize = msg->m_byteCount;
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
void AppHost::DestroyBody()
{
    m_vtable = kAppVtable;
    ListKit::Hunt<BlockedConnEntry> search;
    for (BlockedConnEntry *banned = search.HeadElement(&m_banned), *next; banned; banned = next) {
        next = search.FollowingItem();
        if (banned)
            DeleteVirtual(banned, 0);
    }
    for (void* task : {m_sendTask, m_receiveTask, m_processTask, m_metricsTask}) {
        if (task)
            DeleteVirtual(task, 2);
    }
    m_banned.UnlinkAs(kBannedConnNodeVtable);
    m_circuits.UnlinkAs(kConnNodeVtable);
    m_list1414.UnlinkAs(kAppListNodeVtable);
    m_dispatcher.Teardown();
}

// 0x004578B0
Conn* AppHost::FindConnAtAddress(uint32_t ip, uint16_t port)
{
    ListKit::Hunt<Conn> search;
    for (Conn* conn = search.HeadElement(&m_circuits); conn; conn = search.FollowingItem()) {
        if (conn->m_hostAddr == ip && conn->m_portNo == port)
            return conn;
    }
    return nullptr;
}

// 0x00457910
Conn* AppHost::FindConnWithId(int handle)
{
    ListKit::Hunt<Conn> search;
    for (Conn* conn = search.HeadElement(&m_circuits); conn; conn = search.FollowingItem()) {
        if (conn->m_hdl == handle)
            return conn;
    }
    return nullptr;
}

// 0x00457970
bool AppHost::ImportantMessagesDue(Conn* conn)
{
    ListKit::Hunt<TimedMsgNode> important;
    for (TimedMsgNode* link = important.HeadElement(&conn->m_importantMsgs); link; link = important.FollowingItem()) {
        uint32_t sent = link->At<uint32_t>(0x14);
        if (!sent || m_clock - sent > ResendThreshold(conn))
            return true;
    }
    ListKit::Hunt<TimedMsgNode> sequenced;
    for (TimedMsgNode* link = sequenced.HeadElement(&conn->m_sequencedMsgs); link; link = sequenced.FollowingItem()) {
        uint32_t sent = link->At<uint32_t>(0x1C);
        if (!sent || m_clock - sent > ResendThreshold(conn))
            return true;
    }
    return false;
}

// 0x00457AC0
bool AppHost::MessagesDue(Conn* conn)
{
    ListKit::Hunt<TimedMsgNode> normal;
    for (TimedMsgNode* link = normal.HeadElement(&conn->m_normalMsgs); link; link = normal.FollowingItem()) {
        if (m_clock >= link->At<uint32_t>(0x18))
            return true;
    }
    ListKit::Hunt<TimedMsgNode> important;
    for (TimedMsgNode* link = important.HeadElement(&conn->m_importantMsgs); link; link = important.FollowingItem()) {
        uint32_t sent = link->At<uint32_t>(0x14);
        if (!sent || m_clock < link->At<uint32_t>(0x24) || m_clock - sent > ResendThreshold(conn))
            return true;
    }
    ListKit::Hunt<TimedMsgNode> sequenced;
    for (TimedMsgNode* link = sequenced.HeadElement(&conn->m_sequencedMsgs); link; link = sequenced.FollowingItem()) {
        uint32_t sent = link->At<uint32_t>(0x1C);
        if (!sent || m_clock < link->At<uint32_t>(0x24) || m_clock - sent > ResendThreshold(conn))
            return true;
    }
    return false;
}

// 0x00457C80
void AppHost::FreeMessages(Conn* conn, int queue, uint8_t id)
{
    MemSys::ListNode* list;
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
    ListKit::Hunt<TimedMsgNode> search;
    for (TimedMsgNode *link = search.HeadElement(list), *next; link; link = next) {
        next = search.FollowingItem();
        void* msg = link->At<void*>(msgOffset);
        if (static_cast<PendingMsg*>(msg)->m_ident != id)
            continue;
        int& refCount =
            queue == 2 ? static_cast<SeqQueuedMsg*>(msg)->m_shareCount : static_cast<PendingMsg*>(msg)->m_shareCount;
        if (--refCount == 0)
            DeleteMsg(msg);
        DeleteVirtual(link, 0);
    }
}

} // namespace Net
