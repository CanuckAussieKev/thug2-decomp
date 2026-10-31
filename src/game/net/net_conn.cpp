// NetLink::Conn (Neversoft's Net library): one end of a connection - its outgoing message queues, the sequenced
// messages and streams it has received, its buffers, latency and bandwidth. See net_conn.h.

#include "game/net/net_conn.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lists/list_walk.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app.h"
#include "game/core/code_address.h"

namespace NetLink {

namespace {

using MemSys::ListNode;

// The bandwidth of each bandwidth type (bytes per second).
constexpr int kBroadbandBandwidth = 400000;
constexpr int kModemBandwidth = 4200;
constexpr int kLanBandwidth = 800000;

constexpr uint32_t kReadBufferSize = 0xA28;
constexpr uint32_t kWriteBufferSize = 0x514;

constexpr uint32_t kMinResendThreshold = 500;
constexpr uint32_t kStatus2ResendThreshold = 1000;

uint32_t g_nextConnId{1};

// The running number each connection gets (+0x44).
uint32_t& NextConnId() { return g_nextConnId; }

// ~StreamDesc: frees the data unless it was sent in place.
void DestroyStreamDescBody(StreamDesc* stream) { stream->DestroyBody(); }

// A link deleted through its vtable (the scalar deleting destructor).
void DeleteLink(void* link)
{
    auto* vtable = *static_cast<void***>(link);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(link, 1);
}

// A queued message's last link gone: deleted.
template <typename Msg>
void Release(Msg* message)
{
    if (--message->m_shareCount == 0) {
        message->DestroyBody();
        MemSys::Dealloc(message);
    }
}

// Each item of a list (the links: their data is the link itself), the next one fetched before `action` runs
// on the current one (which may delete it).
template <typename T, typename F>
void ForEach(ListNode* head, F action)
{
    ListKit::Hunt<T> search;
    T* item = search.HeadElement(head);
    while (item) {
        T* next = search.FollowingItem();
        action(item);
        item = next;
    }
}

} // namespace

// 0x00455C60
MsgSeqLinkHead* MsgSeqLinkHead::MakeNew()
{
    InitConnListHead(this, kMsgSeqLinkVtable);
    return this;
}

void InitConnListHead(ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}

// 0x00455B50 / 0x00455B70 / 0x00455B90 / 0x00455BB0: the message lists' ListKit::Elem<T> destructor bodies
void MsgNormalNode::DestroyBody() { UnlinkAs(kMsgNormalNodeVtable); }
void MsgImpNode::DestroyBody() { UnlinkAs(kMsgImpNodeVtable); }
void MsgSeqNode::DestroyBody() { UnlinkAs(kMsgSeqNodeVtable); }
void StreamLinkNode::DestroyBody() { UnlinkAs(kStreamLinkNodeVtable); }

// 0x004559D0 / 0x004559E0 / 0x004559F0 / 0x00455A00
template <void** const& SearchVtable>
void ConnListSearch<SearchVtable>::DestroyBody()
{
    m_vtable = SearchVtable;
}

// 0x00455A80 / 0x00455AB0 / 0x00455AE0 / 0x00455B10
template <void** const& SearchVtable>
ConnListSearch<SearchVtable>* ConnListSearch<SearchVtable>::Teardown(unsigned int flags)
{
    m_vtable = SearchVtable;
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

static VtableSlot kMsgSeqLinkSearchVtable_slots[] = {&NetLink::MsgSeqLinkSearch::Teardown};
void** const kMsgSeqLinkSearchVtable = reinterpret_cast<void**>(kMsgSeqLinkSearchVtable_slots);
static VtableSlot kMsgLinkSearchVtable_slots[] = {&NetLink::MsgLinkSearch::Teardown};
void** const kMsgLinkSearchVtable = reinterpret_cast<void**>(kMsgLinkSearchVtable_slots);
static VtableSlot kMsgImpLinkSearchVtable_slots[] = {&NetLink::MsgImpLinkSearch::Teardown};
void** const kMsgImpLinkSearchVtable = reinterpret_cast<void**>(kMsgImpLinkSearchVtable_slots);
static VtableSlot kStreamLinkSearchVtable_slots[] = {&NetLink::StreamLinkSearch::Teardown};
void** const kStreamLinkSearchVtable = reinterpret_cast<void**>(kStreamLinkSearchVtable_slots);

template struct ConnListSearch<kMsgSeqLinkSearchVtable>;
template struct ConnListSearch<kMsgLinkSearchVtable>;
template struct ConnListSearch<kMsgImpLinkSearchVtable>;
template struct ConnListSearch<kStreamLinkSearchVtable>;

// 0x00455C80
Conn* Conn::MakeNew(int flags)
{
    InitConnListHead(&m_normalMsgs, kMsgNormalLinkVtable);
    InitConnListHead(&m_importantMsgs, kMsgImpLinkVtable);
    InitConnListHead(&m_sequencedMsgs, kMsgSeqLinkVtable);
    m_curStanding = 0;
    m_flagBits = 0;
    for (ListNode& queue : m_receivedQueues)
        static_cast<MsgSeqLinkHead*>(&queue)->MakeNew();
    InitConnListHead(&m_receivedStreams, kStreamLinkVtable);
    InitConnListHead(&m_sendStreams, kStreamLinkVtable);
    m_pingProbe.MakeNew();
    m_receiveMetrics.MakeNew();
    m_sendMetrics.MakeNew();
    m_appNode.vtable = kConnNodeVtable;
    m_appNode.data = this;
    m_appNode.priority = 0;
    m_appNode.next = &m_appNode;
    m_appNode.prev = &m_appNode;

    m_flagBits = static_cast<uint32_t>(flags);
    m_curStanding = kConnReady;
    m_readBuffer = m_readPtr = static_cast<char*>(MemSys::Alloc(kReadBufferSize));
    m_writeBuffer = m_writePtr = static_cast<char*>(MemSys::Alloc(kWriteBufferSize));
    m_txSpacingMs = 0;
    m_lastSendTime = 0;
    m_sendNow = false;
    m_aliasConn = nullptr;
    m_lastTimestampIn = 0;
    m_timestampOut = 0;
    m_streamCounter = 0;
    m_linkSpeed = kBroadbandBandwidth;
    // (only the first 64 groups' sequence numbers are zeroed: 0x40 dwords each - the rest keep what the
    // memory held)
    std::memset(m_nextSequenceIds, 0, 0x100);
    std::memset(m_sequenceNumbers, 0, 0x100);
    m_ident = NextConnId()++;
    return this;
}

// 0x00456790
void Conn::Teardown()
{
    DestroyReceivedSequencedMessages();
    ReleaseQueuedMessages();
    DestroyLinks();
    MemSys::Dealloc(m_readBuffer);
    MemSys::Dealloc(m_writeBuffer);
    m_appNode.DestroyBody();
    static_cast<StreamLinkNode*>(&m_sendStreams)->DestroyBody();
    static_cast<StreamLinkNode*>(&m_receivedStreams)->DestroyBody();
    for (int group = kNumGroups - 1; group >= 0; --group)
        static_cast<MsgSeqNode*>(&m_receivedQueues[group])->DestroyBody();
    static_cast<MsgSeqNode*>(&m_sequencedMsgs)->DestroyBody();
    static_cast<MsgImpNode*>(&m_importantMsgs)->DestroyBody();
    static_cast<MsgNormalNode*>(&m_normalMsgs)->DestroyBody();
}

// 0x00455970
uint32_t Conn::GetTransmitPeriod() { return m_txSpacingMs; }

// 0x00455980
void Conn::ForceSendNow(bool force) { m_sendNow = force; }

// 0x00455990
bool Conn::IsSendForced() { return m_sendNow; }

// 0x004559A0
int Conn::GetLinkSpeedClass() { return m_linkClass; }

// 0x004559B0
void Conn::ChangeThroughput(int bytesPerSecond) { m_linkSpeed = bytesPerSecond; }

// 0x004559C0
int Conn::FetchThroughput() { return m_linkSpeed; }

// 0x00455A10
bool Conn::IsOutsider() { return (m_flagBits & kFlagForeign) != 0; }

// 0x00455A20
bool Conn::IsHostLocal() { return (m_flagBits & kFlagLocal) != 0; }

// 0x00455A30
bool Conn::IsOffMachine() { return (m_flagBits & kFlagRemote) != 0; }

// 0x00455A40
void Conn::SetLinkSpeedClass(int type)
{
    m_linkClass = type;
    switch (type) {
    case kBandwidthBroadband:
        m_linkSpeed = kBroadbandBandwidth;
        break;
    case kBandwidthModem:
        m_linkSpeed = kModemBandwidth;
        break;
    case kBandwidthLan:
        m_linkSpeed = kLanBandwidth;
        break;
    }
}

// 0x00455B40
int Conn::GetMeanPing()
{
    if (IsHostLocal())
        return 0;
    return m_pingProbe.m_averageLatency;
}

// 0x00455C10
void Conn::MarkStale()
{
    m_curStanding &= ~kConnReady;
    m_curStanding |= kConnBroken;
}

// 0x00455C20
bool Conn::IsSound() { return (m_curStanding & kConnBroken) == 0; }

// 0x00455C30
uint32_t Conn::GetRetryDelay()
{
    if (m_curStanding & kStatus2)
        return kStatus2ResendThreshold;
    int threshold = 2 * GetMeanPing();
    if (threshold < static_cast<int>(kMinResendThreshold)) // (signed)
        threshold = kMinResendThreshold;
    return static_cast<uint32_t>(threshold);
}

// 0x00455E50
void Conn::DestroyReceivedSequencedMessages()
{
    for (ListNode& queue : m_receivedQueues) {
        ForEach<OrderedMsgNode>(&queue, [](OrderedMsgNode* link) {
            if (SeqQueuedMsg* message = link->m_sharedMsg) {
                message->DestroyBody();
                MemSys::Dealloc(message);
            }
            DeleteLink(link);
        });
    }
}

// 0x00455F30
void Conn::DestroyLinks()
{
    auto deleteLink = [](void* link) { DeleteLink(link); };
    ForEach<MsgNormalLink>(&m_normalMsgs, deleteLink);
    ForEach<ReliableMsgNode>(&m_importantMsgs, deleteLink);
    ForEach<OrderedMsgNode>(&m_sequencedMsgs, deleteLink);
    for (ListNode& queue : m_receivedQueues)
        ForEach<OrderedMsgNode>(&queue, deleteLink);
    auto deleteStream = [](BulkStreamNode* link) {
        if (StreamDesc* stream = link->m_msgMeta) {
            DestroyStreamDescBody(stream);
            MemSys::Dealloc(stream);
        }
        DeleteLink(link);
    };
    ForEach<BulkStreamNode>(&m_receivedStreams, deleteStream);
    ForEach<BulkStreamNode>(&m_sendStreams, deleteStream);
}

// 0x00456210
void Conn::DestroyImportantMessages()
{
    ForEach<ReliableMsgNode>(&m_importantMsgs, [](ReliableMsgNode* link) {
        Release(link->m_sharedMsg);
        DeleteLink(link);
    });
    ForEach<OrderedMsgNode>(&m_sequencedMsgs, [](OrderedMsgNode* link) {
        Release(link->m_sharedMsg);
        DeleteLink(link);
    });
}

// 0x00456370
void Conn::ReleaseQueuedMessages()
{
    ForEach<MsgNormalLink>(&m_normalMsgs, [](MsgNormalLink* link) { Release(link->m_sharedMsg); });
    ForEach<ReliableMsgNode>(&m_importantMsgs, [](ReliableMsgNode* link) { Release(link->m_sharedMsg); });
    ForEach<OrderedMsgNode>(&m_sequencedMsgs, [](OrderedMsgNode* link) { Release(link->m_sharedMsg); });
}

// 0x00456540
int Conn::GetHighestResendCount()
{
    int highest = 0;
    ForEach<ReliableMsgNode>(&m_importantMsgs, [&](ReliableMsgNode* link) {
        if (link->m_retryCount > highest)
            highest = link->m_retryCount;
    });
    ForEach<OrderedMsgNode>(&m_sequencedMsgs, [&](OrderedMsgNode* link) {
        if (link->m_retryCount > highest)
            highest = link->m_retryCount;
    });
    return highest;
}

// 0x004565E0
int Conn::GetNumQueuedStreamPieces()
{
    int count = 0;
    ForEach<OrderedMsgNode>(&m_sequencedMsgs, [&](OrderedMsgNode* link) {
        if (link->m_bulkMsg)
            ++count;
    });
    return count;
}

// 0x00456630
void Conn::ClearResendCounts()
{
    ForEach<ReliableMsgNode>(&m_importantMsgs, [](ReliableMsgNode* link) { link->m_retryCount = 0; });
    ForEach<OrderedMsgNode>(&m_sequencedMsgs, [](OrderedMsgNode* link) { link->m_retryCount = 0; });
}

// 0x004566D0
void Conn::MarkMessagesForRetry(uint32_t packetstamp)
{
    ForEach<ReliableMsgNode>(&m_importantMsgs, [=](ReliableMsgNode* link) {
        if (link->m_packetClock == packetstamp)
            link->m_stampMs = 0;
    });
    ForEach<OrderedMsgNode>(&m_sequencedMsgs, [=](OrderedMsgNode* link) {
        if (link->m_packetClock == packetstamp)
            link->m_stampMs = 0;
    });
}

// 0x00456770
void Conn::DropQueuedTraffic()
{
    DestroyReceivedSequencedMessages();
    ReleaseQueuedMessages();
    DestroyLinks();
}

} // namespace Net
