// NetLink::AppHost (the network library's base of the Server and the Client): queueing and streaming messages, the
// connection list and the app's tasks.

#include "engine/net/net_app.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/lists/chain_start.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_manager.h"
#include "engine/net/net_metrics.h"
#include "engine/net/net_messages.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/net/task_vtable.h"
#include "game/skate/skate_module.h"
#include "engine/net/buddy_status.h"
#include "engine/net/gamenet_lobby.h"

namespace GameCode {

static VtableSlot kTaskVtable_slots[] = {GameModules::Skate::RunTask, &OnlinePlay::FoldedGetters::Word18,
                &OnlinePlay::TaskDeleter::Teardown};
void** const kTaskVtable = reinterpret_cast<void**>(kTaskVtable_slots);

} // namespace GameCode

namespace NetLink {

static VtableSlot kAppVtable_slots[] = {&AppHost::Teardown, &AppHost::HandleIncoming, &_purecall,
                &_purecall, &AppHost::PrepareAll, &AppHost::TearDownState};
void** const kAppVtable = reinterpret_cast<void**>(kAppVtable_slots);

namespace {

using MemSys::ListNode;

// Message targets (not AppHost::kBroadcast, a flag).
constexpr int kHandleBroadcast = 0xFF;        // every connection
constexpr int kHandleExcludeBroadcast = 0x80; // every connection but handle & 0x7F
constexpr uint8_t kMsgBulkBegin = 0x0E;
constexpr uint8_t kMsgBulkChunk = 0x0F;
constexpr uint8_t kMsgUnreadyOk = 0x09;    // default-queue messages that go to a connection that isn't ready
constexpr int kStreamPieceSize = 0x100;

constexpr int kMetricsTaskPriority = -2500;
constexpr int kReceiveTaskPriority = 1000;
constexpr int kProcessTaskPriority = 0;

// --- Conn (net_conn.h): short names for the fields used here ------------------------------------------------
// The outgoing message queues (ListKit::Heads of links, highest priority first).
ListNode* NormalQueue(Conn* conn) { return &conn->m_normalMsgs; }
ListNode* ImportantQueue(Conn* conn) { return &conn->m_importantMsgs; }
ListNode* SequencedQueue(Conn* conn) { return &conn->m_sequencedMsgs; }
// The receive buffer's read and write positions (its start and end).
uint8_t*& ReadPosition(Conn* conn) { return reinterpret_cast<uint8_t*&>(conn->m_readBuffer); }
uint8_t*& WritePosition(Conn* conn) { return reinterpret_cast<uint8_t*&>(conn->m_readPtr); }
// Status: 1 ready, bit 2 invalid, bit 3 disconnecting.
uint32_t& Standing(Conn* conn) { return conn->m_curStanding; }
constexpr uint32_t kConnReady = Conn::kConnReady;
constexpr uint32_t kConnClosing = 8;
AppHost*& OwnerApp(Conn* conn) { return conn->m_ownerApp; }
// The next sequence id of each message group.
uint32_t& NextSequenceId(Conn* conn, uint8_t group) { return conn->m_nextSequenceIds[group]; }
int& AllocStreamKey(Conn* conn) { return conn->m_streamCounter; }
ListNode* Streams(Conn* conn) { return &conn->m_sendStreams; } // ListKit::Foremost<BulkStreamNode>
void* InMetrics(Conn* conn) { return &conn->m_receiveMetrics; } // NetLink::Gauges (0x100C bytes)
void* OutMetrics(Conn* conn) { return &conn->m_sendMetrics; }
// The send buffer (its start, the end of what's written): a local connection takes its alias's (the other end,
// in this process) straight into its own receive buffer.
uint8_t*& SendBufferStart(Conn* conn) { return reinterpret_cast<uint8_t*&>(conn->m_outgoingBytes); }
uint8_t*& SendBufferEnd(Conn* conn) { return reinterpret_cast<uint8_t*&>(conn->m_writePtr); }

// Conn's methods (net_conn.cpp).
Conn* ConstructConn(void* memory, int flags) { return static_cast<Conn*>(memory)->MakeNew(flags); }
void DestroyConnBody(Conn* conn) { conn->Teardown(); }
bool IsSound(Conn* conn) { return conn->IsSound(); }
void MarkStale(Conn* conn) { conn->MarkStale(); }
bool IsLocalConn(Conn* conn) { return conn->IsHostLocal(); }
bool IsOffMachine(Conn* conn) { return conn->IsOffMachine(); }
bool IsOutsider(Conn* conn) { return conn->IsOutsider(); }
int LinkSpeedClass(Conn* conn) { return conn->GetLinkSpeedClass(); }
int BandwidthLimit(Conn* conn) { return conn->FetchThroughput(); }
void ChangeSendNow(Conn* conn, bool force) { conn->ForceSendNow(force); }
// The sequenced links that carry streamed pieces.
int NumStreamPiecesQueued(Conn* conn) { return conn->GetNumQueuedStreamPieces(); }

// NetLink::Gauges: bytes over the last second.
int BytesPerSecond(void* metrics) { return static_cast<Gauges*>(metrics)->GetThroughput(); }
void UpdateMetrics(void* metrics, uint32_t time) { static_cast<Gauges*>(metrics)->CalculateUsage(static_cast<int>(time)); }
// The app's manager: the bandwidth allowed over all connections.
int TotalBandwidth(void* manager) { return static_cast<Hub*>(manager)->FetchThroughput(); }
// 1 = a modem: a slow connection.
int ConnectionType(void* manager) { return static_cast<Hub*>(manager)->GetNetLinkKind(); }

// --- Messages ---------------------------------------------------------------------------------------------------
// PendingMsg (0x0C) and SeqQueuedMsg (0x10, made by AppHost::NewQueuedMsg): a copy of a message shared by the
// links that send it to each connection; freed with the last.
PendingMsg* ConstructQueuedMsg(void* memory, uint8_t id, uint16_t length, void* data, uint8_t compression)
{
    return static_cast<PendingMsg*>(memory)->MakeNew(id, length, data, compression);
}
// ~PendingMsg / ~SeqQueuedMsg: frees the data.
void DestroyQueuedMsgBody(void* message) { static_cast<PendingMsg*>(message)->DestroyBody(); }

// The links: ListKit::Nodes (+8 the priority) pointing at a PendingMsg.
// TimedMsgNode (0x1C bytes, vtable 0x00647578): +0x18 the time it may be sent.
ListNode* ConstructMsgLink(void* memory, PendingMsg* message)
{
    return &static_cast<MsgNormalLink*>(memory)->MakeNew(message)->m_listLink;
}
// MsgImportantLink (0x28, vtable 0x0064757C): +0x14 when it was last sent (0 = never), +0x24 the time it may go.
ListNode* ConstructMsgImportantLink(void* memory, PendingMsg* message)
{
    return &static_cast<ReliableMsgNode*>(memory)->MakeNew(message)->m_listLink;
}
// OrderedMsgNode (0x2C, vtable 0x00647580): +0x15 the group, +0x16 a stream piece, +0x1C when last sent,
// +0x20 the sequence id, +0x24 the time it may go.
ListNode* ConstructMsgSeqLink(void* memory, SeqQueuedMsg* message)
{
    return &static_cast<OrderedMsgNode*>(memory)->MakeNew(message)->m_listLink;
}
template <typename T>
T& LinkField(ListNode* link, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(link) + offset);
}

// BulkStreamNode (0x18 bytes, vtable 0x00647584): a ListKit::Elem (data = itself) with +0x14 its StreamDesc.
ListNode* ConstructStreamLink(void* memory, StreamDesc* stream)
{
    return &static_cast<BulkStreamNode*>(memory)->MakeNew(stream)->m_listLink;
}
StreamDesc* StreamOf(ListNode* link) { return LinkField<StreamDesc*>(link, 0x14); }
StreamDesc* ConstructStreamDesc(void* memory) { return static_cast<StreamDesc*>(memory)->MakeNew(); }
// ~StreamDesc: frees the data unless it was sent in place ("Destroying StreamDesc's Data").
void DestroyStreamDescBody(StreamDesc* stream) { stream->DestroyBody(); }

// strcpy: the original copies descriptions with no length limit (32 bytes are kept for them).
void CopyString(char* out, const char* source)
{
    while ((*out++ = *source++) != '\0') {
    }
}

void InitMsgDesc(NetMessageInfo* desc) { desc->MakeNew(); }

// A node's deleting destructor (virtual slot 0).
void UnlinkListNode(ListNode* node)
{
    reinterpret_cast<void*(__thiscall*)(ListNode*, unsigned int)>(node->vtable[0])(node, 1);
}

// The tail of a list.
void AppendLast(ListNode* head, ListNode* node)
{
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}
// Out of its list (the node keeps its vtable).
void DropOut(ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// The memory manager.
MemSys::Hub* RamStats() { return MemSys::Hub::Pull(); }
void PushNetworkHeap() { RamStats()->EnterHeapScope(RamStats()->m_networkHeap); }
void PopHeap() { RamStats()->LeaveHeapScope(); }

// The pieces of a streamed message. The start: the description, the total size, the stream's id, a checksum of
// all the data, the message id and the first 256 bytes.
struct StreamStart {
    char m_desc[32];
    int m_extent;
    int m_streamId;
    uint32_t m_checksum;
    uint8_t m_msgId;
    uint8_t m_content[kStreamPieceSize];
};
static_assert(sizeof(StreamStart) == 0x130 && offsetof(StreamStart, m_content) == 0x2D, "the original's layout");
// The start's length is the piece + 0x30 (the struct's size less the unused part of m_content): it carries the
// struct's 3 bytes of padding after a full piece, and 3 bytes past a shorter one.
constexpr uint16_t kStreamStartOverhead = sizeof(StreamStart) - kStreamPieceSize;
struct StreamData {
    int m_streamId;
    uint8_t m_content[kStreamPieceSize];
};

// --- The rest of the library ------------------------------------------------------------------------------
// A message link into a queue: ListKit::Foremost::AppendNode (0x00457350).
void AddLink(ListNode* queue, ListNode* link) { reinterpret_cast<ListKit::Foremost*>(queue)->AppendNode(link); }
// conn's received messages to their handlers (Router::InvokeHandlersForMsg); 2 = halted, 3 = stop
int DispatchMessages(HandlerLists* dispatcher, Conn* conn, int flags)
{
    return static_cast<Router*>(dispatcher)->InvokeHandlersForMsg(conn, static_cast<uint32_t>(flags));
}

// App's virtual functions.
void PullInbound(AppHost* app) { reinterpret_cast<void(__thiscall*)(AppHost*)>(app->m_vtable[2])(app); }
void SendEnqueuedMessagesTo(AppHost* app, Conn* conn)
{
    reinterpret_cast<void(__thiscall*)(AppHost*, Conn*)>(app->m_vtable[3])(app, conn);
}

// A Scheduling::Task<App> (0x20 bytes): the task node (0x00406890, with the priority), +0x18 the callback, +0x1C the app.
void* NewTask(int priority, void* callback, AppHost* app)
{
    auto* task = static_cast<uint32_t*>(MemSys::AllocZeroed(0x20));
    if (!task)
        return nullptr;
    reinterpret_cast<Scheduling::SchedulerTask*>(task)->MakeNew(priority);
    task[0] = reinterpret_cast<uint32_t>(GameCode::kTaskVtable);
    task[6] = reinterpret_cast<uint32_t>(callback);
    task[7] = reinterpret_cast<uint32_t>(app);
    return task;
}
AppHost* TaskApp(void* task) { return reinterpret_cast<AppHost*>(static_cast<uint32_t*>(task)[7]); }

// A list head: data -1, priority 0, linked to itself.
void SetupListSentinel(ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}

} // namespace

double g_doubleZero;

// 0x004594C0
AppHost* AppHost::MakeNew(int flags)
{
    m_vtable = kAppVtable;
    for (MsgHandlerNode& handlers : m_dispatcher.m_lists)
        handlers.ConstructHead();
    m_dispatcherApp = this;
    m_list1414.vtable = kAppListNodeVtable;
    m_list1414.data = this;
    m_list1414.priority = 0;
    m_list1414.next = &m_list1414;
    m_list1414.prev = &m_list1414;
    m_flagBits = 0;
    SetupListSentinel(&m_circuits, kConnHeadVtable);
    SetupListSentinel(&m_banned, kBannedConnVtable);
    m_metricsTask = NewTask(kMetricsTaskPriority, reinterpret_cast<void*>(&AppHost::NetworkMetrics), this);
    m_receiveTask = NewTask(kReceiveTaskPriority, reinterpret_cast<void*>(&AppHost::ReceiveNetworkData), this);
    m_processTask = NewTask(kProcessTaskPriority, reinterpret_cast<void*>(&AppHost::PumpIncomingTraffic), this);
    m_unknown1F00 = g_doubleZero; // 0.0
    m_flagBits = static_cast<uint32_t>(flags);
    m_clock = 0;
    m_unknownPacketSink = nullptr;
    return this;
}

// 0x00459660
AppHost* AppHost::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

namespace {
EnqueueFilter g_enqueueFilter = nullptr; // NOT ORIGINAL - SetEnqueueFilter's
}

void SetEnqueueFilter(EnqueueFilter filter) { g_enqueueFilter = filter; }

// 0x00457EB0
void AppHost::PutOnOutbox(int handle, NetMessageInfo* desc)
{
    // (NOT ORIGINAL: the filter - SetEnqueueFilter)
    if (g_enqueueFilter && g_enqueueFilter(this, handle, desc))
        return;
    PushNetworkHeap();
    // Every connection that takes it (`accepts`), or every one but handle & 0x7F, or the handle's.
    auto forEachTarget = [&](auto accepts, auto queue) {
        if (handle == kHandleBroadcast) {
            ListKit::Hunt<Conn> search;
            for (Conn* conn = search.HeadElement(&m_circuits); conn; conn = search.FollowingItem()) {
                if (accepts(conn))
                    queue(conn);
            }
        } else if (handle & kHandleExcludeBroadcast) {
            int excluded = handle & ~kHandleExcludeBroadcast;
            ListKit::Hunt<Conn> search;
            for (Conn* conn = search.HeadElement(&m_circuits); conn; conn = search.FollowingItem()) {
                if (conn->m_hdl != excluded && accepts(conn))
                    queue(conn);
            }
        } else {
            Conn* conn = FindConnWithId(handle); // (not checked for null)
            if (accepts(conn))
                queue(conn);
        }
    };
    auto notDisconnecting = [](Conn* conn) { return (Standing(conn) & kConnClosing) == 0; };

    switch (desc->m_queue) {
    case NetMessageInfo::kLaneNormal: {
        void* memory = MemSys::Alloc(0x0C);
        PendingMsg* message =
            memory ? ConstructQueuedMsg(memory, desc->m_ident, desc->m_byteCount, desc->m_content, desc->m_compression)
                   : nullptr;
        // To ready connections only (to a single connection, also message 9).
        auto ready = [&](Conn* conn) {
            return Standing(conn) == kConnReady || (handle != kHandleBroadcast && !(handle & kHandleExcludeBroadcast) &&
                                                    desc->m_ident == kMsgUnreadyOk);
        };
        forEachTarget(ready, [&](Conn* conn) {
            if (desc->m_singular)
                FreeMessages(conn, desc->m_queue, desc->m_ident);
            ++message->m_shareCount;
            void* linkMemory = MemSys::AllocZeroed(0x1C);
            ListNode* link = linkMemory ? ConstructMsgLink(linkMemory, message) : nullptr;
            link->priority = desc->m_rank;
            LinkField<uint32_t>(link, 0x18) = desc->m_delay + m_clock;
            AddLink(NormalQueue(conn), link);
        });
        if (message->m_shareCount == 0) {
            DestroyQueuedMsgBody(message);
            RamStats()->Dealloc(message);
        }
        break;
    }
    case NetMessageInfo::kLanePriority: {
        void* memory = MemSys::Alloc(0x0C);
        PendingMsg* message =
            memory ? ConstructQueuedMsg(memory, desc->m_ident, desc->m_byteCount, desc->m_content, desc->m_compression)
                   : nullptr;
        forEachTarget(notDisconnecting, [&](Conn* conn) {
            if (desc->m_singular)
                FreeMessages(conn, desc->m_queue, desc->m_ident);
            ++message->m_shareCount;
            void* linkMemory = MemSys::AllocZeroed(0x28);
            ListNode* link = linkMemory ? ConstructMsgImportantLink(linkMemory, message) : nullptr;
            link->priority = desc->m_rank;
            LinkField<uint32_t>(link, 0x24) = desc->m_delay + m_clock;
            LinkField<uint32_t>(link, 0x14) = 0;
            AddLink(ImportantQueue(conn), link);
        });
        if (message->m_shareCount == 0) {
            DestroyQueuedMsgBody(message);
            RamStats()->Dealloc(message);
        }
        break;
    }
    case NetMessageInfo::kLaneOrdered: {
        SeqQueuedMsg* message =
            NewQueuedMsg(desc->m_ident, desc->m_byteCount, desc->m_content, desc->m_groupId, desc->m_compression);
        forEachTarget(notDisconnecting, [&](Conn* conn) {
            ++message->m_shareCount;
            void* linkMemory = MemSys::AllocZeroed(0x2C);
            ListNode* link = linkMemory ? ConstructMsgSeqLink(linkMemory, message) : nullptr;
            // The original's broadcast gives the links the queue number (2) as their priority instead.
            link->priority = handle == kHandleBroadcast ? desc->m_queue : desc->m_rank;
            LinkField<uint32_t>(link, 0x1C) = 0;
            LinkField<uint32_t>(link, 0x24) = desc->m_delay + m_clock;
            LinkField<uint8_t>(link, 0x15) = desc->m_groupId;
            if (desc->m_fixedSeqNo)
                LinkField<uint32_t>(link, 0x20) = desc->m_fixedSeqNo;
            else
                LinkField<uint32_t>(link, 0x20) = NextSequenceId(conn, desc->m_groupId)++;
            LinkField<uint8_t>(link, 0x16) = desc->m_feed;
            AddLink(SequencedQueue(conn), link);
        });
        if (message->m_shareCount == 0) {
            DestroyQueuedMsgBody(message);
            RamStats()->Dealloc(message);
        }
        break;
    }
    default:
        break;
    }
    PopHeap();
}

// 0x00458740
void AppHost::SendBulkToConn(Conn* conn, uint8_t msgId, uint16_t size, void* data, const char* desc,
                              uint8_t groupId, bool queueAllPieces, bool borrowCallerBuffer)
{
    // The original leaves the start message uninitialised on the stack (the 3 bytes sent past a short first
    // piece, the padding after a full one); it's zeroed here. (NetMessageInfo's constructor sets every field.)
    NetMessageInfo message = {};
    InitMsgDesc(&message);
    StreamStart start = {};
    start.m_extent = size;
    start.m_streamId = AllocStreamKey(conn);
    start.m_msgId = msgId;
    start.m_checksum = Checksums::GenerateCRCFromBuffer(data, size);
    CopyString(start.m_desc, desc);
    int first = size > kStreamPieceSize ? kStreamPieceSize : size;
    std::memcpy(start.m_content, data, first);
    message.m_byteCount = static_cast<uint16_t>(first + kStreamStartOverhead);
    message.m_content = &start;
    message.m_groupId = groupId;
    message.m_ident = kMsgBulkBegin;
    message.m_queue = NetMessageInfo::kLaneOrdered;
    message.m_feed = 1;
    PutOnOutbox(conn->m_hdl, &message);

    if (size > kStreamPieceSize) {
        if (queueAllPieces) {
            // Every piece now.
            const uint8_t* next = static_cast<const uint8_t*>(data) + first;
            StreamData piece;
            for (int sent = first; sent < size;) {
                int length = size - sent;
                if (length > kStreamPieceSize)
                    length = kStreamPieceSize;
                piece.m_streamId = AllocStreamKey(conn);
                std::memcpy(piece.m_content, next, length);
                message.m_content = &piece;
                message.m_byteCount = static_cast<uint16_t>(length + 4);
                message.m_groupId = groupId;
                message.m_ident = kMsgBulkChunk;
                message.m_queue = NetMessageInfo::kLaneOrdered;
                message.m_feed = 1;
                PutOnOutbox(conn->m_hdl, &message);
                sent += length;
                next += length;
            }
        } else {
            // The rest goes a few pieces a frame (PumpStreamTraffic), with sequence ids reserved now.
            PushNetworkHeap();
            int laterPieces = (size - 1) / kStreamPieceSize;
            uint8_t* copy;
            if (borrowCallerBuffer) {
                copy = static_cast<uint8_t*>(data);
            } else {
                copy = static_cast<uint8_t*>(MemSys::Alloc(size));
                std::memcpy(copy, data, size);
            }
            uint8_t* next = copy + first;
            void* memory = MemSys::Alloc(sizeof(StreamDesc));
            StreamDesc* stream = memory ? ConstructStreamDesc(memory) : nullptr;
            void* linkMemory = MemSys::AllocZeroed(0x18);
            ListNode* link = linkMemory ? ConstructStreamLink(linkMemory, stream) : nullptr;
            stream->m_extent = size;
            stream->m_streamId = AllocStreamKey(conn);
            stream->m_msgId = msgId;
            CopyString(stream->m_desc, desc);
            stream->m_content = copy;
            stream->m_groupId = groupId;
            stream->m_succ = next;
            stream->m_orderNumber = NextSequenceId(conn, groupId);
            stream->m_borrowedPayload = borrowCallerBuffer;
            AppendLast(Streams(conn), link);
            NextSequenceId(conn, groupId) += laterPieces;
            PopHeap();
        }
    }
    ++AllocStreamKey(conn);
}

// 0x00458AB0
void AppHost::SendBulkMessage(int handle, uint8_t msgId, uint16_t size, void* data, const char* desc, uint8_t groupId,
                        bool queueAllPieces, bool borrowCallerBuffer)
{
    if (handle == kHandleBroadcast) {
        ListKit::Hunt<Conn> search;
        for (Conn* conn = search.HeadElement(&m_circuits); conn; conn = search.FollowingItem()) {
            if (IsSound(conn))
                SendBulkToConn(conn, msgId, size, data, desc, groupId, queueAllPieces, borrowCallerBuffer);
        }
    } else {
        Conn* conn = FindConnWithId(handle); // (not checked for null)
        if (IsSound(conn))
            SendBulkToConn(conn, msgId, size, data, desc, groupId, queueAllPieces, borrowCallerBuffer);
    }
}

// 0x00458BE0
Conn* AppHost::BeginConnWalk(ConnSearch* search)
{
    search->m_listLink = &m_circuits;
    ListNode* next = m_circuits.next;
    search->m_listLink = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
    return search->m_listLink ? static_cast<Conn*>(search->m_listLink->data) : nullptr;
}

// 0x00458C10
void AppHost::FlushOutbox()
{
    ListKit::Hunt<Conn> search;
    for (Conn* conn = search.HeadElement(&m_circuits); conn; conn = search.FollowingItem())
        SendEnqueuedMessagesTo(this, conn);
}

// 0x00458CA0
void AppHost::Transmit(bool sendInFrame)
{
    if (!sendInFrame) {
        ++m_clock;
        ListKit::Hunt<Conn> search;
        for (Conn* conn = search.HeadElement(&m_circuits); conn; conn = search.FollowingItem())
            ChangeSendNow(conn, true);
    }
    FlushOutbox();
}

// 0x00458D50
bool AppHost::IsConnBlocked(Conn* conn)
{
    ListKit::Hunt<uint8_t> search;
    for (uint8_t* banned = search.HeadElement(&m_banned); banned; banned = search.FollowingItem()) {
        if (*reinterpret_cast<uint32_t*>(banned + 0x14) == conn->m_remoteIp)
            return true;
    }
    return false;
}

// 0x00458DB0
int AppHost::FetchTrafficSpent()
{
    int total = 0;
    ListKit::Hunt<Conn> search;
    for (Conn* conn = search.HeadElement(&m_circuits); conn; conn = search.FollowingItem()) {
        if (IsOffMachine(conn))
            total += BytesPerSecond(OutMetrics(conn));
    }
    return total;
}

// 0x00458E60: over the total allowed, over conn's own limit, or over its share of the total (split between the
// remote connections that aren't foreign).
bool AppHost::OverBandwidthBudget(Conn* conn)
{
    if (FetchTrafficSpent() >= TotalBandwidth(m_supervisor))
        return true;
    int limit = BandwidthLimit(conn);
    if (BytesPerSecond(OutMetrics(conn)) >= limit)
        return true;
    int sharing = 0;
    ListKit::Hunt<Conn> search;
    for (Conn* other = search.HeadElement(&m_circuits); other; other = search.FollowingItem()) {
        if (IsOffMachine(other) && !IsOutsider(other))
            ++sharing;
    }
    if (sharing == 0)
        sharing = 1;
    int share = TotalBandwidth(m_supervisor) / sharing;
    return BytesPerSecond(OutMetrics(conn)) > share;
}

// 0x00458F80
int AppHost::FetchUnusedSlotId()
{
    for (int handle = 1; handle < 0x100; ++handle) {
        if (!FindConnWithId(handle))
            return handle;
    }
    return 0;
}

// 0x00458FB0
void AppHost::DropBrokenLinks()
{
    ListKit::Hunt<Conn> search;
    Conn* conn = search.HeadElement(&m_circuits);
    while (conn) {
        Conn* next = search.FollowingItem();
        if ((Standing(conn) & kConnClosing) && !MessagesDue(conn))
            MarkStale(conn);
        if (!IsSound(conn) && WritePosition(conn) <= ReadPosition(conn) && !MessagesDue(conn)) {
            DropOut(&conn->m_appNode);
            DestroyConnBody(conn);
            RamStats()->Dealloc(conn);
        }
        conn = next;
    }
}

// 0x004590C0: per connection, at most 5 stream pieces queued and 3 more a frame (2 and 1 on a slow connection);
// finished streams are deleted.
void AppHost::PumpStreamTraffic()
{
    NetMessageInfo message = {};
    InitMsgDesc(&message);
    StreamData piece;
    Hub* manager = Hub::Sole();
    ListKit::Hunt<Conn> search;
    for (Conn* conn = search.HeadElement(&m_circuits); conn; conn = search.FollowingItem()) {
        int maxQueued = 5;
        int maxPerFrame = 3;
        if (ConnectionType(manager) == 1 || LinkSpeedClass(conn) == 1) {
            maxQueued = 2;
            maxPerFrame = 1;
        }
        unsigned int streams = 0;
        for (ListNode* node = Streams(conn)->next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
            ++streams;
        if (streams == 0)
            continue;
        int queued = NumStreamPiecesQueued(conn);
        if (queued >= maxQueued)
            continue;
        int budget = maxQueued - queued;
        if (budget > maxPerFrame)
            budget = maxPerFrame;

        ListKit::Hunt<ListNode> links;
        ListNode* link = links.HeadElement(Streams(conn));
        while (link) {
            ListNode* nextLink = links.FollowingItem();
            StreamDesc* stream = StreamOf(link);
            int sent = static_cast<int>(stream->m_succ - stream->m_content);
            int size = stream->m_extent;
            bool unfinished = false;
            while (sent < size) {
                if (budget <= 0) {
                    unfinished = true;
                    break;
                }
                int length = size - sent;
                if (length > kStreamPieceSize)
                    length = kStreamPieceSize;
                piece.m_streamId = stream->m_streamId;
                std::memcpy(piece.m_content, stream->m_succ, length);
                message.m_ident = kMsgBulkChunk;
                message.m_byteCount = static_cast<uint16_t>(length + 4);
                message.m_queue = NetMessageInfo::kLaneOrdered;
                message.m_content = &piece;
                message.m_groupId = stream->m_groupId;
                message.m_feed = 1;
                message.m_fixedSeqNo = stream->m_orderNumber++;
                PutOnOutbox(conn->m_hdl, &message);
                stream->m_succ += length;
                sent += length;
                --budget;
            }
            if (!unfinished) {
                DestroyStreamDescBody(stream);
                RamStats()->Dealloc(stream);
                UnlinkListNode(link);
            }
            link = nextLink;
        }
    }
}

// 0x00456CA0
void __cdecl AppHost::PumpIncomingTraffic(void* task)
{
    AppHost* app = TaskApp(task);
    ++app->m_frameCount;
    reinterpret_cast<bool(__thiscall*)(AppHost*)>(app->m_vtable[1])(app); // HandleIncoming
}

// 0x004593C0
void __cdecl AppHost::ReceiveNetworkData(void* task)
{
    AppHost* app = TaskApp(task);
    app->m_clock = FrameClock::GetTime();
    app->DropBrokenLinks();
    PullInbound(app);
}

// 0x004593E0
void __cdecl AppHost::NetworkMetrics(void* task)
{
    AppHost* app = TaskApp(task);
    ListKit::Hunt<Conn> search;
    for (Conn* conn = search.HeadElement(&app->m_circuits); conn; conn = search.FollowingItem()) {
        UpdateMetrics(InMetrics(conn), app->m_clock);
        UpdateMetrics(OutMetrics(conn), app->m_clock);
    }
}

// 0x004594A0
void __cdecl AppHost::FlushOutgoingTraffic(void* task)
{
    AppHost* app = TaskApp(task);
    app->PumpStreamTraffic();
    app->Transmit(true);
}

// 0x00459690
bool AppHost::HandleIncoming()
{
    ListKit::Hunt<Conn> search;
    Conn* conn = search.HeadElement(&m_circuits);
    while (conn) {
        Conn* next = search.FollowingItem();
        if (IsLocalConn(conn) && conn->m_aliasConn) {
            Conn* alias = conn->m_aliasConn;
            size_t length = SendBufferEnd(alias) - SendBufferStart(alias);
            std::memcpy(WritePosition(conn), SendBufferStart(alias), length);
            SendBufferEnd(conn->m_aliasConn) = SendBufferStart(conn->m_aliasConn);
            WritePosition(conn) += length;
        }
        int result = DispatchMessages(&m_dispatcher, conn, 0);
        if (result == 2)
            return true;
        if (result == 3)
            return false;
        if (!DispatchQueuedSequencedMessages(conn))
            return false;
        conn = next;
    }
    return true;
}

// 0x004597C0
Conn* AppHost::OpenedLink(int ip, uint16_t port, int flags)
{
    Conn* conn = nullptr;
    PushNetworkHeap();
    if (m_openLinks < m_peerCap) {
        void* memory = MemSys::Alloc(0x3D20);
        conn = memory ? ConstructConn(memory, flags) : nullptr; // (not checked for null)
        conn->m_remoteIp = static_cast<uint32_t>(ip);
        conn->m_portNo = port;
        conn->m_hdl = FetchUnusedSlotId();
        OwnerApp(conn) = this;
        conn->m_lastHeardFrom = FrameClock::GetTime();
        AppendLast(&m_circuits, &conn->m_appNode);
    }
    PopHeap();
    return conn;
}

} // namespace Net
