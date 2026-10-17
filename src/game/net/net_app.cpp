// Net::App (the network library's base of the Server and the Client): queueing and streaming messages, the
// connection list and the app's tasks.

#include "game/net/net_app.h"

#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/head.h"
#include "game/lst/search.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Net {

using Conn = ConnView;

namespace {

using Mem::ListNode;

void** const kAppVtable = reinterpret_cast<void**>(0x006475B4);
void** const kAppNodeVtable = reinterpret_cast<void**>(0x006474D8);      // Lst::Node<App>
void** const kTaskVtable = reinterpret_cast<void**>(0x006476F0);         // Tsk::Task<App>

// Message targets (not AppView::kHandleBroadcast, a flag).
constexpr int kHandleBroadcast = 0xFF;        // every connection
constexpr int kHandleExcludeBroadcast = 0x80; // every connection but handle & 0x7F
constexpr uint8_t kMsgStreamStart = 0x0E;
constexpr uint8_t kMsgStreamData = 0x0F;
constexpr uint8_t kMsgUnreadyOk = 0x09;    // default-queue messages that go to a connection that isn't ready
constexpr int kStreamPieceSize = 0x100;

constexpr int kMetricsTaskPriority = -2500;
constexpr int kReceiveTaskPriority = 1000;
constexpr int kProcessTaskPriority = 0;

// --- Conn (0x3D20 bytes, ConnView; the fields it doesn't name) -----------------------------------------------
uint8_t* Bytes(Conn* conn) { return reinterpret_cast<uint8_t*>(conn); }
template <typename T>
T& At(Conn* conn, uint32_t offset)
{
    return *reinterpret_cast<T*>(Bytes(conn) + offset);
}
// The outgoing message queues (Lst::Heads of links, highest priority first).
ListNode* NormalQueue(Conn* conn) { return &At<ListNode>(conn, 0x00); }
ListNode* ImportantQueue(Conn* conn) { return &At<ListNode>(conn, 0x14); }
ListNode* SequencedQueue(Conn* conn) { return &At<ListNode>(conn, 0x28); }
// The receive buffer's read and write positions.
uint8_t*& ReadPosition(Conn* conn) { return At<uint8_t*>(conn, 0x54); }
uint8_t*& WritePosition(Conn* conn) { return At<uint8_t*>(conn, 0x58); }
// Status: 1 ready, bit 2 invalid, bit 3 disconnecting.
uint32_t& Status(Conn* conn) { return At<uint32_t>(conn, 0x60); }
constexpr uint32_t kStatusReady = 1;
constexpr uint32_t kStatusDisconnecting = 8;
App*& OwnerApp(Conn* conn) { return At<App*>(conn, 0x68); }
uint32_t& CreatedTime(Conn* conn) { return At<uint32_t>(conn, 0x6C); }
// The next sequence id of each message group.
uint32_t& NextSequenceId(Conn* conn, uint8_t group) { return At<uint32_t>(conn, 0x78 + 4 * group); }
int& NextStreamId(Conn* conn) { return At<int>(conn, 0x878); }
ListNode* Streams(Conn* conn) { return &At<ListNode>(conn, 0x1C90); } // Lst::Head<StreamLink>
void* InMetrics(Conn* conn) { return Bytes(conn) + 0x1CDC; }         // Net::Metrics (0x100C bytes)
void* OutMetrics(Conn* conn) { return Bytes(conn) + 0x2CE8; }
// The send buffer (+0x4C its start, +0x50 the end of what's written): a local connection takes its alias's
// (the other end, in this process) straight into its own receive buffer.
uint8_t*& SendBufferStart(Conn* conn) { return At<uint8_t*>(conn, 0x4C); }
uint8_t*& SendBufferEnd(Conn* conn) { return At<uint8_t*>(conn, 0x50); }

// Conn's methods (0x00455xxx-0x00456xxx, not rewritten yet).
Conn* ConstructConn(void* memory, int flags)
{
    return GameFunction<Conn*(__thiscall*)(void*, int)>(0x00455C80)(memory, flags);
}
void DestroyConnBody(Conn* conn) { GameFunction<void(__thiscall*)(Conn*)>(0x00456790)(conn); }
bool IsValid(Conn* conn) { return GameFunction<bool(__thiscall*)(Conn*)>(0x00455C20)(conn); }
void Invalidate(Conn* conn) { GameFunction<void(__thiscall*)(Conn*)>(0x00455C10)(conn); }
bool IsLocalConn(Conn* conn) { return GameFunction<bool(__thiscall*)(Conn*)>(0x00455A20)(conn); }
bool IsRemote(Conn* conn) { return GameFunction<bool(__thiscall*)(Conn*)>(0x00455A30)(conn); }
bool IsForeign(Conn* conn) { return GameFunction<bool(__thiscall*)(Conn*)>(0x00455A10)(conn); }
int BandwidthType(Conn* conn) { return GameFunction<int(__thiscall*)(Conn*)>(0x004559A0)(conn); }
int BandwidthLimit(Conn* conn) { return GameFunction<int(__thiscall*)(Conn*)>(0x004559C0)(conn); }
void SetForceSend(Conn* conn, bool force) { GameFunction<void(__thiscall*)(Conn*, bool)>(0x00455980)(conn, force); }
// The sequenced links that carry streamed pieces.
int NumStreamPiecesQueued(Conn* conn) { return GameFunction<int(__thiscall*)(Conn*)>(0x004565E0)(conn); }

// Net::Metrics: bytes over the last second.
int BytesPerSecond(void* metrics) { return GameFunction<int(__thiscall*)(void*)>(0x00454520)(metrics); }
void UpdateMetrics(void* metrics, uint32_t time)
{
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x00454D00)(metrics, time);
}
// +0x378 of the app's manager: the bandwidth allowed over all connections.
int TotalBandwidth(void* manager) { return GameFunction<int(__thiscall*)(void*)>(0x004545B0)(manager); }
// The Net::Manager's +0x128: 1 = a slow connection.
int ConnectionType(void* manager) { return GameFunction<int(__thiscall*)(void*)>(0x00454960)(manager); }

// --- Messages ---------------------------------------------------------------------------------------------------
// QueuedMsg (0x0C) and SeqQueuedMsg (0x10, made by AppView::NewQueuedMsg): a copy of a message shared by the
// links that send it to each connection; freed with the last.
QueuedMsg* ConstructQueuedMsg(void* memory, uint8_t id, uint16_t length, void* data, uint8_t compression)
{
    return GameFunction<QueuedMsg*(__thiscall*)(void*, uint8_t, uint16_t, void*, uint8_t)>(0x00454FA0)(
        memory, id, length, data, compression);
}
// ~QueuedMsg / ~SeqQueuedMsg: frees the data.
void DestroyQueuedMsgBody(void* message) { GameFunction<void(__thiscall*)(void*)>(0x004550F0)(message); }

// The links: Lst::Nodes (+8 the priority) pointing at a QueuedMsg.
// MsgLink (0x1C bytes, vtable 0x00647578): +0x18 the time it may be sent.
ListNode* ConstructMsgLink(void* memory, QueuedMsg* message)
{
    return GameFunction<ListNode*(__thiscall*)(void*, QueuedMsg*)>(0x004555F0)(memory, message);
}
// MsgImportantLink (0x28, vtable 0x0064757C): +0x14 when it was last sent (0 = never), +0x24 the time it may go.
ListNode* ConstructMsgImportantLink(void* memory, QueuedMsg* message)
{
    return GameFunction<ListNode*(__thiscall*)(void*, QueuedMsg*)>(0x00455620)(memory, message);
}
// MsgSeqLink (0x2C, vtable 0x00647580): +0x15 the group, +0x16 a stream piece, +0x1C when last sent,
// +0x20 the sequence id, +0x24 the time it may go.
ListNode* ConstructMsgSeqLink(void* memory, SeqQueuedMsg* message)
{
    return GameFunction<ListNode*(__thiscall*)(void*, SeqQueuedMsg*)>(0x00455650)(memory, message);
}
template <typename T>
T& LinkField(ListNode* link, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(link) + offset);
}

// StreamLink (0x18 bytes, vtable 0x00647584): a Lst::Node (data = itself) with +0x14 its StreamDesc.
ListNode* ConstructStreamLink(void* memory, StreamDesc* stream)
{
    return GameFunction<ListNode*(__thiscall*)(void*, StreamDesc*)>(0x00455680)(memory, stream);
}
StreamDesc* StreamOf(ListNode* link) { return LinkField<StreamDesc*>(link, 0x14); }
StreamDesc* ConstructStreamDesc(void* memory)
{
    return GameFunction<StreamDesc*(__thiscall*)(void*)>(0x004549D0)(memory);
}
// ~StreamDesc: frees the data unless it was sent in place ("Destroying StreamDesc's Data").
void DestroyStreamDescBody(StreamDesc* stream) { GameFunction<void(__thiscall*)(StreamDesc*)>(0x00454E00)(stream); }

// strcpy: the original copies descriptions with no length limit (32 bytes are kept for them).
void CopyString(char* out, const char* source)
{
    while ((*out++ = *source++) != '\0') {
    }
}

void InitMsgDesc(MsgDesc* desc) { GameFunction<void(__thiscall*)(MsgDesc*)>(0x004549F0)(desc); }

// A node's deleting destructor (virtual slot 0).
void DeleteNode(ListNode* node)
{
    reinterpret_cast<void*(__thiscall*)(ListNode*, unsigned int)>(node->vtable[0])(node, 1);
}

// The tail of a list.
void AddToTail(ListNode* head, ListNode* node)
{
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}
// Out of its list (the node keeps its vtable).
void Remove(ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// The memory manager.
Mem::Manager* Memory() { return Mem::Manager::Get(); }
void PushNetworkHeap() { Memory()->PushContext(Memory()->m_networkHeap); }
void PopHeap() { Memory()->PopContext(); }

// The pieces of a streamed message. The start: the description, the total size, the stream's id, a checksum of
// all the data, the message id and the first 256 bytes.
struct StreamStart {
    char m_desc[32];
    int m_size;
    int m_streamId;
    uint32_t m_checksum;
    uint8_t m_msgId;
    uint8_t m_data[kStreamPieceSize];
};
static_assert(sizeof(StreamStart) == 0x130 && offsetof(StreamStart, m_data) == 0x2D, "the original's layout");
// The start's length is the piece + 0x30 (the struct's size less the unused part of m_data): it carries the
// struct's 3 bytes of padding after a full piece, and 3 bytes past a shorter one.
constexpr uint16_t kStreamStartOverhead = sizeof(StreamStart) - kStreamPieceSize;
struct StreamData {
    int m_streamId;
    uint8_t m_data[kStreamPieceSize];
};

// --- The rest of the library ------------------------------------------------------------------------------
// A message link into a queue: Lst::Head::AddNodeFromTail (0x00457350).
void AddLink(ListNode* queue, ListNode* link) { reinterpret_cast<Lst::Head*>(queue)->AddNodeFromTail(link); }
// The handler lists' constructor (0x00457010, in wt/netA's range; not a function in Ghidra, not rewritten).
void ConstructHandlerList(ListNode* head) { GameFunction<void(__thiscall*)(ListNode*)>(0x00457010)(head); }
// The process task (0x00456CA0, in wt/netA's range; not a function in Ghidra, not rewritten).
void* const kProcessNetworkData = reinterpret_cast<void*>(0x00456CA0);
// conn's received messages to their handlers (Dispatcher::DispatchMsgHandlers); 2 = halted, 3 = stop
int DispatchMessages(HandlerLists* dispatcher, Conn* conn, int flags)
{
    return static_cast<Dispatcher*>(dispatcher)->DispatchMsgHandlers(conn, static_cast<uint32_t>(flags));
}

// App's virtual functions.
void ReceiveData(App* app) { reinterpret_cast<void(__thiscall*)(App*)>(app->m_vtable[2])(app); }
void SendEnqueuedMessagesTo(App* app, Conn* conn)
{
    reinterpret_cast<void(__thiscall*)(App*, Conn*)>(app->m_vtable[3])(app, conn);
}

// A Tsk::Task<App> (0x20 bytes): the task node (0x00406890, with the priority), +0x18 the callback, +0x1C the app.
void* NewTask(int priority, void* callback, App* app)
{
    auto* task = static_cast<uint32_t*>(Mem::Calloc(0x20));
    if (!task)
        return nullptr;
    GameFunction<void*(__thiscall*)(void*, int)>(0x00406890)(task, priority);
    task[0] = reinterpret_cast<uint32_t>(kTaskVtable);
    task[6] = reinterpret_cast<uint32_t>(callback);
    task[7] = reinterpret_cast<uint32_t>(app);
    return task;
}
App* TaskApp(void* task) { return reinterpret_cast<App*>(static_cast<uint32_t*>(task)[7]); }

// A list head: data -1, priority 0, linked to itself.
void InitHead(ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}

} // namespace

// 0x004594C0
App* App::Construct(int flags)
{
    m_vtable = kAppVtable;
    for (MsgHandlerNode& handlers : m_dispatcher.m_lists)
        ConstructHandlerList(&handlers);
    m_dispatcherApp = this;
    m_list1414.vtable = kAppNodeVtable;
    m_list1414.data = this;
    m_list1414.priority = 0;
    m_list1414.next = &m_list1414;
    m_list1414.prev = &m_list1414;
    m_flags = 0;
    InitHead(&m_connections, kConnHeadVtable);
    InitHead(&m_banned, kBannedConnVtable);
    m_metricsTask = NewTask(kMetricsTaskPriority, reinterpret_cast<void*>(&App::NetworkMetrics), this);
    m_receiveTask = NewTask(kReceiveTaskPriority, reinterpret_cast<void*>(&App::ReceiveNetworkData), this);
    m_processTask = NewTask(kProcessTaskPriority, kProcessNetworkData, this);
    Unknown1F00() = GameVariable<double>(0x006453A0); // 0.0
    m_flags = static_cast<uint32_t>(flags);
    m_time = 0;
    m_unknown1E88 = 0;
    return this;
}

// 0x00459660
App* App::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x00457EB0
void App::EnqueueMessage(int handle, MsgDesc* desc)
{
    PushNetworkHeap();
    // Every connection that takes it (`accepts`), or every one but handle & 0x7F, or the handle's.
    auto forEachTarget = [&](auto accepts, auto queue) {
        if (handle == kHandleBroadcast) {
            Lst::Search<Conn> search;
            for (Conn* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem()) {
                if (accepts(conn))
                    queue(conn);
            }
        } else if (handle & kHandleExcludeBroadcast) {
            int excluded = handle & ~kHandleExcludeBroadcast;
            Lst::Search<Conn> search;
            for (Conn* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem()) {
                if (conn->m_handle != excluded && accepts(conn))
                    queue(conn);
            }
        } else {
            Conn* conn = GetConnectionByHandle(handle); // (not checked for null)
            if (accepts(conn))
                queue(conn);
        }
    };
    auto notDisconnecting = [](Conn* conn) { return (Status(conn) & kStatusDisconnecting) == 0; };

    switch (desc->m_queue) {
    case MsgDesc::kQueueDefault: {
        void* memory = Mem::New(0x0C);
        QueuedMsg* message =
            memory ? ConstructQueuedMsg(memory, desc->m_id, desc->m_length, desc->m_data, desc->m_compression)
                   : nullptr;
        // To ready connections only (to a single connection, also message 9).
        auto ready = [&](Conn* conn) {
            return Status(conn) == kStatusReady || (handle != kHandleBroadcast && !(handle & kHandleExcludeBroadcast) &&
                                                    desc->m_id == kMsgUnreadyOk);
        };
        forEachTarget(ready, [&](Conn* conn) {
            if (desc->m_singular)
                FreeMessages(conn, desc->m_queue, desc->m_id);
            ++message->m_refCount;
            void* linkMemory = Mem::Calloc(0x1C);
            ListNode* link = linkMemory ? ConstructMsgLink(linkMemory, message) : nullptr;
            link->priority = desc->m_priority;
            LinkField<uint32_t>(link, 0x18) = desc->m_delay + m_time;
            AddLink(NormalQueue(conn), link);
        });
        if (message->m_refCount == 0) {
            DestroyQueuedMsgBody(message);
            Memory()->Free(message);
        }
        break;
    }
    case MsgDesc::kQueueImportant: {
        void* memory = Mem::New(0x0C);
        QueuedMsg* message =
            memory ? ConstructQueuedMsg(memory, desc->m_id, desc->m_length, desc->m_data, desc->m_compression)
                   : nullptr;
        forEachTarget(notDisconnecting, [&](Conn* conn) {
            if (desc->m_singular)
                FreeMessages(conn, desc->m_queue, desc->m_id);
            ++message->m_refCount;
            void* linkMemory = Mem::Calloc(0x28);
            ListNode* link = linkMemory ? ConstructMsgImportantLink(linkMemory, message) : nullptr;
            link->priority = desc->m_priority;
            LinkField<uint32_t>(link, 0x24) = desc->m_delay + m_time;
            LinkField<uint32_t>(link, 0x14) = 0;
            AddLink(ImportantQueue(conn), link);
        });
        if (message->m_refCount == 0) {
            DestroyQueuedMsgBody(message);
            Memory()->Free(message);
        }
        break;
    }
    case MsgDesc::kQueueSequenced: {
        SeqQueuedMsg* message =
            NewQueuedMsg(desc->m_id, desc->m_length, desc->m_data, desc->m_groupId, desc->m_compression);
        forEachTarget(notDisconnecting, [&](Conn* conn) {
            ++message->m_refCount;
            void* linkMemory = Mem::Calloc(0x2C);
            ListNode* link = linkMemory ? ConstructMsgSeqLink(linkMemory, message) : nullptr;
            // The original's broadcast gives the links the queue number (2) as their priority instead.
            link->priority = handle == kHandleBroadcast ? desc->m_queue : desc->m_priority;
            LinkField<uint32_t>(link, 0x1C) = 0;
            LinkField<uint32_t>(link, 0x24) = desc->m_delay + m_time;
            LinkField<uint8_t>(link, 0x15) = desc->m_groupId;
            if (desc->m_forcedSequenceId)
                LinkField<uint32_t>(link, 0x20) = desc->m_forcedSequenceId;
            else
                LinkField<uint32_t>(link, 0x20) = NextSequenceId(conn, desc->m_groupId)++;
            LinkField<uint8_t>(link, 0x16) = desc->m_stream;
            AddLink(SequencedQueue(conn), link);
        });
        if (message->m_refCount == 0) {
            DestroyQueuedMsgBody(message);
            Memory()->Free(message);
        }
        break;
    }
    default:
        break;
    }
    PopHeap();
}

// 0x00458740
void App::StreamMessageToConn(Conn* conn, uint8_t msgId, uint16_t size, void* data, const char* desc,
                              uint8_t groupId, bool allAtOnce, bool sendInPlace)
{
    // The original leaves the descriptor's singular and compression fields (and the start's padding)
    // uninitialised; they are zero here (a non-zero compression would garble the pieces).
    MsgDesc message = {};
    InitMsgDesc(&message);
    StreamStart start = {};
    start.m_size = size;
    start.m_streamId = NextStreamId(conn);
    start.m_msgId = msgId;
    start.m_checksum = Crc::GenerateCRCFromBuffer(data, size);
    CopyString(start.m_desc, desc);
    int first = size > kStreamPieceSize ? kStreamPieceSize : size;
    std::memcpy(start.m_data, data, first);
    message.m_length = static_cast<uint16_t>(first + kStreamStartOverhead);
    message.m_data = &start;
    message.m_groupId = groupId;
    message.m_id = kMsgStreamStart;
    message.m_queue = MsgDesc::kQueueSequenced;
    message.m_stream = 1;
    EnqueueMessage(conn->m_handle, &message);

    if (size > kStreamPieceSize) {
        if (allAtOnce) {
            // Every piece now.
            const uint8_t* next = static_cast<const uint8_t*>(data) + first;
            StreamData piece;
            for (int sent = first; sent < size;) {
                int length = size - sent;
                if (length > kStreamPieceSize)
                    length = kStreamPieceSize;
                piece.m_streamId = NextStreamId(conn);
                std::memcpy(piece.m_data, next, length);
                message.m_data = &piece;
                message.m_length = static_cast<uint16_t>(length + 4);
                message.m_groupId = groupId;
                message.m_id = kMsgStreamData;
                message.m_queue = MsgDesc::kQueueSequenced;
                message.m_stream = 1;
                EnqueueMessage(conn->m_handle, &message);
                sent += length;
                next += length;
            }
        } else {
            // The rest goes a few pieces a frame (ProcessStreamMessages), with sequence ids reserved now.
            PushNetworkHeap();
            int laterPieces = (size - 1) / kStreamPieceSize;
            uint8_t* copy;
            if (sendInPlace) {
                copy = static_cast<uint8_t*>(data);
            } else {
                copy = static_cast<uint8_t*>(Mem::New(size));
                std::memcpy(copy, data, size);
            }
            uint8_t* next = copy + first;
            void* memory = Mem::New(sizeof(StreamDesc));
            StreamDesc* stream = memory ? ConstructStreamDesc(memory) : nullptr;
            void* linkMemory = Mem::Calloc(0x18);
            ListNode* link = linkMemory ? ConstructStreamLink(linkMemory, stream) : nullptr;
            stream->m_size = size;
            stream->m_streamId = NextStreamId(conn);
            stream->m_msgId = msgId;
            CopyString(stream->m_desc, desc);
            stream->m_data = copy;
            stream->m_groupId = groupId;
            stream->m_next = next;
            stream->m_sequenceId = NextSequenceId(conn, groupId);
            stream->m_sendInPlace = sendInPlace;
            AddToTail(Streams(conn), link);
            NextSequenceId(conn, groupId) += laterPieces;
            PopHeap();
        }
    }
    ++NextStreamId(conn);
}

// 0x00458AB0
void App::StreamMessage(int handle, uint8_t msgId, uint16_t size, void* data, const char* desc, uint8_t groupId,
                        bool allAtOnce, bool sendInPlace)
{
    if (handle == kHandleBroadcast) {
        Lst::Search<Conn> search;
        for (Conn* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem()) {
            if (IsValid(conn))
                StreamMessageToConn(conn, msgId, size, data, desc, groupId, allAtOnce, sendInPlace);
        }
    } else {
        Conn* conn = GetConnectionByHandle(handle); // (not checked for null)
        if (IsValid(conn))
            StreamMessageToConn(conn, msgId, size, data, desc, groupId, allAtOnce, sendInPlace);
    }
}

// 0x00458BE0
Conn* App::FirstConnection(ConnSearch* search)
{
    search->m_node = &m_connections;
    ListNode* next = m_connections.next;
    search->m_node = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
    return search->m_node ? static_cast<Conn*>(search->m_node->data) : nullptr;
}

// 0x00458C10
void App::SendEnqueuedMessages()
{
    Lst::Search<Conn> search;
    for (Conn* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem())
        SendEnqueuedMessagesTo(this, conn);
}

// 0x00458CA0
void App::SendData(bool sendInFrame)
{
    if (!sendInFrame) {
        ++m_time;
        Lst::Search<Conn> search;
        for (Conn* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem())
            SetForceSend(conn, true);
    }
    SendEnqueuedMessages();
}

// 0x00458D50
bool App::IsConnectionBanned(Conn* conn)
{
    Lst::Search<uint8_t> search;
    for (uint8_t* banned = search.FirstItem(&m_banned); banned; banned = search.NextItem()) {
        if (*reinterpret_cast<uint32_t*>(banned + 0x14) == conn->m_ip)
            return true;
    }
    return false;
}

// 0x00458DB0
int App::GetBandwidthUsed()
{
    int total = 0;
    Lst::Search<Conn> search;
    for (Conn* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem()) {
        if (IsRemote(conn))
            total += BytesPerSecond(OutMetrics(conn));
    }
    return total;
}

// 0x00458E60: over the total allowed, over conn's own limit, or over its share of the total (split between the
// remote connections that aren't foreign).
bool App::BandwidthExceeded(Conn* conn)
{
    if (GetBandwidthUsed() >= TotalBandwidth(m_manager))
        return true;
    int limit = BandwidthLimit(conn);
    if (BytesPerSecond(OutMetrics(conn)) >= limit)
        return true;
    int sharing = 0;
    Lst::Search<Conn> search;
    for (Conn* other = search.FirstItem(&m_connections); other; other = search.NextItem()) {
        if (IsRemote(other) && !IsForeign(other))
            ++sharing;
    }
    if (sharing == 0)
        sharing = 1;
    int share = TotalBandwidth(m_manager) / sharing;
    return BytesPerSecond(OutMetrics(conn)) > share;
}

// 0x00458F80
int App::GetFreeHandle()
{
    for (int handle = 1; handle < 0x100; ++handle) {
        if (!GetConnectionByHandle(handle))
            return handle;
    }
    return 0;
}

// 0x00458FB0
void App::TerminateInvalidConnections()
{
    Lst::Search<Conn> search;
    Conn* conn = search.FirstItem(&m_connections);
    while (conn) {
        Conn* next = search.NextItem();
        if ((Status(conn) & kStatusDisconnecting) && !MessagesDue(conn))
            Invalidate(conn);
        if (!IsValid(conn) && WritePosition(conn) <= ReadPosition(conn) && !MessagesDue(conn)) {
            Remove(&conn->m_appNode);
            DestroyConnBody(conn);
            Memory()->Free(conn);
        }
        conn = next;
    }
}

// 0x004590C0: per connection, at most 5 stream pieces queued and 3 more a frame (2 and 1 on a slow connection);
// finished streams are deleted.
void App::ProcessStreamMessages()
{
    MsgDesc message = {}; // (singular and compression uninitialised in the original - see StreamMessageToConn)
    InitMsgDesc(&message);
    StreamData piece;
    void* manager = GameVariable<void*>(0x006EFDD8); // the Net::Manager
    Lst::Search<Conn> search;
    for (Conn* conn = search.FirstItem(&m_connections); conn; conn = search.NextItem()) {
        int maxQueued = 5;
        int maxPerFrame = 3;
        if (ConnectionType(manager) == 1 || BandwidthType(conn) == 1) {
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

        Lst::Search<ListNode> links;
        ListNode* link = links.FirstItem(Streams(conn));
        while (link) {
            ListNode* nextLink = links.NextItem();
            StreamDesc* stream = StreamOf(link);
            int sent = static_cast<int>(stream->m_next - stream->m_data);
            int size = stream->m_size;
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
                std::memcpy(piece.m_data, stream->m_next, length);
                message.m_id = kMsgStreamData;
                message.m_length = static_cast<uint16_t>(length + 4);
                message.m_queue = MsgDesc::kQueueSequenced;
                message.m_data = &piece;
                message.m_groupId = stream->m_groupId;
                message.m_stream = 1;
                message.m_forcedSequenceId = stream->m_sequenceId++;
                EnqueueMessage(conn->m_handle, &message);
                stream->m_next += length;
                sent += length;
                --budget;
            }
            if (!unfinished) {
                DestroyStreamDescBody(stream);
                Memory()->Free(stream);
                DeleteNode(link);
            }
            link = nextLink;
        }
    }
}

// 0x004593C0
void __cdecl App::ReceiveNetworkData(void* task)
{
    App* app = TaskApp(task);
    app->m_time = Tmr::GetTime();
    app->TerminateInvalidConnections();
    ReceiveData(app);
}

// 0x004593E0
void __cdecl App::NetworkMetrics(void* task)
{
    App* app = TaskApp(task);
    Lst::Search<Conn> search;
    for (Conn* conn = search.FirstItem(&app->m_connections); conn; conn = search.NextItem()) {
        UpdateMetrics(InMetrics(conn), app->m_time);
        UpdateMetrics(OutMetrics(conn), app->m_time);
    }
}

// 0x004594A0
void __cdecl App::SendNetworkData(void* task)
{
    App* app = TaskApp(task);
    app->ProcessStreamMessages();
    app->SendData(true);
}

// 0x00459690
bool App::ProcessData()
{
    Lst::Search<Conn> search;
    Conn* conn = search.FirstItem(&m_connections);
    while (conn) {
        Conn* next = search.NextItem();
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
Conn* App::NewConnection(int ip, uint16_t port, int flags)
{
    Conn* conn = nullptr;
    PushNetworkHeap();
    if (m_numConnections < m_maxConnections) {
        void* memory = Mem::New(0x3D20);
        conn = memory ? ConstructConn(memory, flags) : nullptr; // (not checked for null)
        conn->m_ip = static_cast<uint32_t>(ip);
        conn->m_port = port;
        conn->m_handle = GetFreeHandle();
        OwnerApp(conn) = this;
        CreatedTime(conn) = Tmr::GetTime();
        AddToTail(&m_connections, &conn->m_appNode);
    }
    PopHeap();
    return conn;
}

} // namespace Net

REPLACE(0x00457EB0, &Net::App::EnqueueMessage);
REPLACE(0x00458740, &Net::App::StreamMessageToConn);
REPLACE(0x00458AB0, &Net::App::StreamMessage);
REPLACE(0x00458BE0, &Net::App::FirstConnection);
REPLACE(0x00458C10, &Net::App::SendEnqueuedMessages);
REPLACE(0x00458CA0, &Net::App::SendData);
REPLACE(0x00458D50, &Net::App::IsConnectionBanned);
REPLACE(0x00458DB0, &Net::App::GetBandwidthUsed);
REPLACE(0x00458E60, &Net::App::BandwidthExceeded);
REPLACE(0x00458F80, &Net::App::GetFreeHandle);
REPLACE(0x00458FB0, &Net::App::TerminateInvalidConnections);
REPLACE(0x004590C0, &Net::App::ProcessStreamMessages);
REPLACE(0x004593C0, Net::App::ReceiveNetworkData);
REPLACE(0x004593E0, Net::App::NetworkMetrics);
REPLACE(0x004594A0, Net::App::SendNetworkData);
REPLACE(0x004594C0, &Net::App::Construct);
REPLACE(0x00459660, &Net::App::Destroy);
REPLACE(0x00459690, &Net::App::ProcessData);
REPLACE(0x004597C0, &Net::App::NewConnection);
