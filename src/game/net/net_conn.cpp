// Net::Conn (Neversoft's Net library): one end of a connection - its outgoing message queues, the sequenced
// messages and streams it has received, its buffers, latency and bandwidth. See net_conn.h.

#include "game/net/net_conn.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/search.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app.h"
#include "game/core/code_address.h"

namespace Net {

namespace {

using Mem::ListNode;

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
    if (--message->m_refCount == 0) {
        message->DestroyBody();
        Mem::Free(message);
    }
}

// Each item of a list (the links: their data is the link itself), the next one fetched before `action` runs
// on the current one (which may delete it).
template <typename T, typename F>
void ForEach(ListNode* head, F action)
{
    Lst::Search<T> search;
    T* item = search.FirstItem(head);
    while (item) {
        T* next = search.NextItem();
        action(item);
        item = next;
    }
}

} // namespace

// 0x00455C60
MsgSeqLinkHead* MsgSeqLinkHead::Construct()
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

// 0x00455B50 / 0x00455B70 / 0x00455B90 / 0x00455BB0: the message lists' Lst::Node<T> destructor bodies
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
ConnListSearch<SearchVtable>* ConnListSearch<SearchVtable>::Destroy(unsigned int flags)
{
    m_vtable = SearchVtable;
    if (flags & 1)
        Mem::Free(this);
    return this;
}

static VtableSlot kMsgSeqLinkSearchVtable_slots[] = {&Net::MsgSeqLinkSearch::Destroy};
void** const kMsgSeqLinkSearchVtable = reinterpret_cast<void**>(kMsgSeqLinkSearchVtable_slots);
static VtableSlot kMsgLinkSearchVtable_slots[] = {&Net::MsgLinkSearch::Destroy};
void** const kMsgLinkSearchVtable = reinterpret_cast<void**>(kMsgLinkSearchVtable_slots);
static VtableSlot kMsgImpLinkSearchVtable_slots[] = {&Net::MsgImpLinkSearch::Destroy};
void** const kMsgImpLinkSearchVtable = reinterpret_cast<void**>(kMsgImpLinkSearchVtable_slots);
static VtableSlot kStreamLinkSearchVtable_slots[] = {&Net::StreamLinkSearch::Destroy};
void** const kStreamLinkSearchVtable = reinterpret_cast<void**>(kStreamLinkSearchVtable_slots);

template struct ConnListSearch<kMsgSeqLinkSearchVtable>;
template struct ConnListSearch<kMsgLinkSearchVtable>;
template struct ConnListSearch<kMsgImpLinkSearchVtable>;
template struct ConnListSearch<kStreamLinkSearchVtable>;

// 0x00455C80
Conn* Conn::Construct(int flags)
{
    InitConnListHead(&m_normalMsgs, kMsgNormalLinkVtable);
    InitConnListHead(&m_importantMsgs, kMsgImpLinkVtable);
    InitConnListHead(&m_sequencedMsgs, kMsgSeqLinkVtable);
    m_status = 0;
    m_flags = 0;
    for (ListNode& queue : m_receivedQueues)
        static_cast<MsgSeqLinkHead*>(&queue)->Construct();
    InitConnListHead(&m_receivedStreams, kStreamLinkVtable);
    InitConnListHead(&m_sendStreams, kStreamLinkVtable);
    m_latencyTest.Construct();
    m_receiveMetrics.Construct();
    m_sendMetrics.Construct();
    m_appNode.vtable = kConnNodeVtable;
    m_appNode.data = this;
    m_appNode.priority = 0;
    m_appNode.next = &m_appNode;
    m_appNode.prev = &m_appNode;

    m_flags = static_cast<uint32_t>(flags);
    m_status = kStatusReady;
    m_readBuffer = m_readPtr = static_cast<char*>(Mem::New(kReadBufferSize));
    m_writeBuffer = m_writePtr = static_cast<char*>(Mem::New(kWriteBufferSize));
    m_sendInterval = 0;
    m_lastSendTime = 0;
    m_sendNow = false;
    m_aliasConn = nullptr;
    m_lastTimestampIn = 0;
    m_timestampOut = 0;
    m_nextStreamId = 0;
    m_bandwidth = kBroadbandBandwidth;
    // (only the first 64 groups' sequence numbers are zeroed: 0x40 dwords each - the rest keep what the
    // memory held)
    std::memset(m_nextSequenceIds, 0, 0x100);
    std::memset(m_sequenceNumbers, 0, 0x100);
    m_id = NextConnId()++;
    return this;
}

// 0x00456790
void Conn::Destroy()
{
    DestroyReceivedSequencedMessages();
    ReleaseQueuedMessages();
    DestroyLinks();
    Mem::Free(m_readBuffer);
    Mem::Free(m_writeBuffer);
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
uint32_t Conn::GetSendInterval() { return m_sendInterval; }

// 0x00455980
void Conn::SetForceSendThisFrame(bool force) { m_sendNow = force; }

// 0x00455990
bool Conn::GetForceSendThisFrame() { return m_sendNow; }

// 0x004559A0
int Conn::GetBandwidthType() { return m_bandwidthType; }

// 0x004559B0
void Conn::SetBandwidth(int bytesPerSecond) { m_bandwidth = bytesPerSecond; }

// 0x004559C0
int Conn::GetBandwidth() { return m_bandwidth; }

// 0x00455A10
bool Conn::IsForeign() { return (m_flags & kFlagForeign) != 0; }

// 0x00455A20
bool Conn::IsLocal() { return (m_flags & kFlagLocal) != 0; }

// 0x00455A30
bool Conn::IsRemote() { return (m_flags & kFlagRemote) != 0; }

// 0x00455A40
void Conn::SetBandwidthType(int type)
{
    m_bandwidthType = type;
    switch (type) {
    case kBandwidthBroadband:
        m_bandwidth = kBroadbandBandwidth;
        break;
    case kBandwidthModem:
        m_bandwidth = kModemBandwidth;
        break;
    case kBandwidthLan:
        m_bandwidth = kLanBandwidth;
        break;
    }
}

// 0x00455B40
int Conn::GetAveLatency()
{
    if (IsLocal())
        return 0;
    return m_latencyTest.m_averageLatency;
}

// 0x00455C10
void Conn::Invalidate()
{
    m_status &= ~kStatusReady;
    m_status |= kStatusInvalid;
}

// 0x00455C20
bool Conn::IsValid() { return (m_status & kStatusInvalid) == 0; }

// 0x00455C30
uint32_t Conn::GetResendThreshold()
{
    if (m_status & kStatus2)
        return kStatus2ResendThreshold;
    int threshold = 2 * GetAveLatency();
    if (threshold < static_cast<int>(kMinResendThreshold)) // (signed)
        threshold = kMinResendThreshold;
    return static_cast<uint32_t>(threshold);
}

// 0x00455E50
void Conn::DestroyReceivedSequencedMessages()
{
    for (ListNode& queue : m_receivedQueues) {
        ForEach<MsgSeqLink>(&queue, [](MsgSeqLink* link) {
            if (SeqQueuedMsg* message = link->m_QMsg) {
                message->DestroyBody();
                Mem::Free(message);
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
    ForEach<MsgImpLink>(&m_importantMsgs, deleteLink);
    ForEach<MsgSeqLink>(&m_sequencedMsgs, deleteLink);
    for (ListNode& queue : m_receivedQueues)
        ForEach<MsgSeqLink>(&queue, deleteLink);
    auto deleteStream = [](StreamLink* link) {
        if (StreamDesc* stream = link->m_Desc) {
            DestroyStreamDescBody(stream);
            Mem::Free(stream);
        }
        DeleteLink(link);
    };
    ForEach<StreamLink>(&m_receivedStreams, deleteStream);
    ForEach<StreamLink>(&m_sendStreams, deleteStream);
}

// 0x00456210
void Conn::DestroyImportantMessages()
{
    ForEach<MsgImpLink>(&m_importantMsgs, [](MsgImpLink* link) {
        Release(link->m_QMsg);
        DeleteLink(link);
    });
    ForEach<MsgSeqLink>(&m_sequencedMsgs, [](MsgSeqLink* link) {
        Release(link->m_QMsg);
        DeleteLink(link);
    });
}

// 0x00456370
void Conn::ReleaseQueuedMessages()
{
    ForEach<MsgNormalLink>(&m_normalMsgs, [](MsgNormalLink* link) { Release(link->m_QMsg); });
    ForEach<MsgImpLink>(&m_importantMsgs, [](MsgImpLink* link) { Release(link->m_QMsg); });
    ForEach<MsgSeqLink>(&m_sequencedMsgs, [](MsgSeqLink* link) { Release(link->m_QMsg); });
}

// 0x00456540
int Conn::GetHighestResendCount()
{
    int highest = 0;
    ForEach<MsgImpLink>(&m_importantMsgs, [&](MsgImpLink* link) {
        if (link->m_NumResends > highest)
            highest = link->m_NumResends;
    });
    ForEach<MsgSeqLink>(&m_sequencedMsgs, [&](MsgSeqLink* link) {
        if (link->m_NumResends > highest)
            highest = link->m_NumResends;
    });
    return highest;
}

// 0x004565E0
int Conn::GetNumQueuedStreamPieces()
{
    int count = 0;
    ForEach<MsgSeqLink>(&m_sequencedMsgs, [&](MsgSeqLink* link) {
        if (link->m_StreamMessage)
            ++count;
    });
    return count;
}

// 0x00456630
void Conn::ClearResendCounts()
{
    ForEach<MsgImpLink>(&m_importantMsgs, [](MsgImpLink* link) { link->m_NumResends = 0; });
    ForEach<MsgSeqLink>(&m_sequencedMsgs, [](MsgSeqLink* link) { link->m_NumResends = 0; });
}

// 0x004566D0
void Conn::FlagMessagesForResending(uint32_t packetstamp)
{
    ForEach<MsgImpLink>(&m_importantMsgs, [=](MsgImpLink* link) {
        if (link->m_Packetstamp == packetstamp)
            link->m_Timestamp = 0;
    });
    ForEach<MsgSeqLink>(&m_sequencedMsgs, [=](MsgSeqLink* link) {
        if (link->m_Packetstamp == packetstamp)
            link->m_Timestamp = 0;
    });
}

// 0x00456770
void Conn::DestroyMessageQueues()
{
    DestroyReceivedSequencedMessages();
    ReleaseQueuedMessages();
    DestroyLinks();
}

} // namespace Net
