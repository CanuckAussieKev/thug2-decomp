// Net::Conn (Neversoft's Net library): one end of a connection - its outgoing message queues, the sequenced
// messages and streams it has received, its buffers, latency and bandwidth. See net_app_view.h (ConnView) and
// net_conn.h (its lists' types).

#include "game/net/net_conn.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/search.h"
#include "game/net/net_access_c.h"
#include "game/net/net_app.h"
#include "game/net/net_app_view.h"
#include "hook/game.h"
#include "hook/hook.h"

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

// The running number each connection gets (+0x44).
uint32_t& NextConnId() { return GameVariable<uint32_t>(0x0067D800); }

// -> Net::Metrics's constructor (wt/netD)
void ConstructMetrics(void* metrics) { GameFunction<void*(__thiscall*)(void*)>(0x004544E0)(metrics); }
// -> ~QueuedMsg / ~SeqQueuedMsg: frees the data (wt/netE)
void DestroyQueuedMsgBody(void* message) { GameFunction<void(__thiscall*)(void*)>(0x004550F0)(message); }
// -> ~StreamDesc: frees the data unless it was sent in place (wt/netD)
void DestroyStreamDescBody(StreamDesc* stream) { GameFunction<void(__thiscall*)(StreamDesc*)>(0x00454E00)(stream); }

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
        DestroyQueuedMsgBody(message);
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
    InitConnListHead(this, kMsgSeqLinkHeadVtable);
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

// 0x004559D0 / 0x004559E0 / 0x004559F0 / 0x00455A00
template <uint32_t SearchVtable>
void ConnListSearch<SearchVtable>::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(SearchVtable);
}

// 0x00455A80 / 0x00455AB0 / 0x00455AE0 / 0x00455B10
template <uint32_t SearchVtable>
ConnListSearch<SearchVtable>* ConnListSearch<SearchVtable>::Destroy(unsigned int flags)
{
    m_vtable = reinterpret_cast<void**>(SearchVtable);
    if (flags & 1)
        Mem::Free(this);
    return this;
}

template struct ConnListSearch<0x00647564>;
template struct ConnListSearch<0x00647568>;
template struct ConnListSearch<0x0064756C>;
template struct ConnListSearch<0x00647570>;

// 0x00455C80
ConnView* ConnView::Construct(int flags)
{
    InitConnListHead(&m_normalMsgs, kMsgLinkHeadVtable);
    InitConnListHead(&m_importantMsgs, kMsgImpLinkHeadVtable);
    InitConnListHead(&m_sequencedMsgs, kMsgSeqLinkHeadVtable);
    m_status = 0;
    m_flags = 0;
    for (ListNode& queue : m_receivedQueues)
        static_cast<MsgSeqLinkHead*>(&queue)->Construct();
    InitConnListHead(&m_receivedStreams, kStreamLinkHeadVtable);
    InitConnListHead(&m_sendStreams, kStreamLinkHeadVtable);
    m_latencyTest.Construct();
    ConstructMetrics(m_receiveMetrics);
    ConstructMetrics(m_sendMetrics);
    m_appNode.vtable = kConnNodeVtable;
    m_appNode.data = this;
    m_appNode.priority = 0;
    m_appNode.next = &m_appNode;
    m_appNode.prev = &m_appNode;

    m_flags = static_cast<uint32_t>(flags);
    m_status = kStatusReady;
    m_readBuffer = m_readPtr = static_cast<uint8_t*>(Mem::New(kReadBufferSize));
    m_writeBuffer = m_writePtr = static_cast<uint8_t*>(Mem::New(kWriteBufferSize));
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
void ConnView::Destroy()
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
        static_cast<MsgSeqLinkNode*>(&m_receivedQueues[group])->DestroyBody();
    static_cast<MsgSeqLinkNode*>(&m_sequencedMsgs)->DestroyBody();
    static_cast<MsgImpLinkNode*>(&m_importantMsgs)->DestroyBody();
    static_cast<MsgLinkNode*>(&m_normalMsgs)->DestroyBody();
}

// 0x00455970
uint32_t ConnView::GetSendInterval() { return m_sendInterval; }

// 0x00455980
void ConnView::SetForceSendThisFrame(bool force) { m_sendNow = force; }

// 0x00455990
bool ConnView::GetForceSendThisFrame() { return m_sendNow; }

// 0x004559A0
int ConnView::GetBandwidthType() { return m_bandwidthType; }

// 0x004559B0
void ConnView::SetBandwidth(int bytesPerSecond) { m_bandwidth = bytesPerSecond; }

// 0x004559C0
int ConnView::GetBandwidth() { return m_bandwidth; }

// 0x00455A10
bool ConnView::IsForeign() { return (m_flags & kFlagForeign) != 0; }

// 0x00455A20
bool ConnView::IsLocal() { return (m_flags & kFlagLocal) != 0; }

// 0x00455A30
bool ConnView::IsRemote() { return (m_flags & kFlagRemote) != 0; }

// 0x00455A40
void ConnView::SetBandwidthType(int type)
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
int ConnView::GetAveLatency()
{
    if (IsLocal())
        return 0;
    return m_latencyTest.m_averageLatency;
}

// 0x00455C10
void ConnView::Invalidate()
{
    m_status &= ~kStatusReady;
    m_status |= kStatusInvalid;
}

// 0x00455C20
bool ConnView::IsValid() { return (m_status & kStatusInvalid) == 0; }

// 0x00455C30
uint32_t ConnView::GetResendThreshold()
{
    if (m_status & kStatus2)
        return kStatus2ResendThreshold;
    int threshold = 2 * GetAveLatency();
    if (threshold < static_cast<int>(kMinResendThreshold)) // (signed)
        threshold = kMinResendThreshold;
    return static_cast<uint32_t>(threshold);
}

// 0x00455E50
void ConnView::DestroyReceivedSequencedMessages()
{
    for (ListNode& queue : m_receivedQueues) {
        ForEach<MsgSeqLink>(&queue, [](MsgSeqLink* link) {
            if (SeqQueuedMsg* message = link->m_QMsg) {
                DestroyQueuedMsgBody(message);
                Mem::Free(message);
            }
            DeleteLink(link);
        });
    }
}

// 0x00455F30
void ConnView::DestroyLinks()
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
void ConnView::DestroyImportantMessages()
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
void ConnView::ReleaseQueuedMessages()
{
    ForEach<MsgNormalLink>(&m_normalMsgs, [](MsgNormalLink* link) { Release(link->m_QMsg); });
    ForEach<MsgImpLink>(&m_importantMsgs, [](MsgImpLink* link) { Release(link->m_QMsg); });
    ForEach<MsgSeqLink>(&m_sequencedMsgs, [](MsgSeqLink* link) { Release(link->m_QMsg); });
}

// 0x00456540
int ConnView::GetHighestResendCount()
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
int ConnView::GetNumQueuedStreamPieces()
{
    int count = 0;
    ForEach<MsgSeqLink>(&m_sequencedMsgs, [&](MsgSeqLink* link) {
        if (link->m_StreamMessage)
            ++count;
    });
    return count;
}

// 0x00456630
void ConnView::ClearResendCounts()
{
    ForEach<MsgImpLink>(&m_importantMsgs, [](MsgImpLink* link) { link->m_NumResends = 0; });
    ForEach<MsgSeqLink>(&m_sequencedMsgs, [](MsgSeqLink* link) { link->m_NumResends = 0; });
}

// 0x004566D0
void ConnView::FlagMessagesForResending(uint32_t packetstamp)
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
void ConnView::DestroyMessageQueues()
{
    DestroyReceivedSequencedMessages();
    ReleaseQueuedMessages();
    DestroyLinks();
}

} // namespace Net

REPLACE(0x00455C60, &Net::MsgSeqLinkHead::Construct);
REPLACE(0x004559D0, &Net::MsgSeqLinkSearch::DestroyBody);
REPLACE(0x004559E0, &Net::MsgLinkSearch::DestroyBody);
REPLACE(0x004559F0, &Net::MsgImpLinkSearch::DestroyBody);
REPLACE(0x00455A00, &Net::StreamLinkSearch::DestroyBody);
REPLACE(0x00455A80, &Net::MsgSeqLinkSearch::Destroy);
REPLACE(0x00455AB0, &Net::MsgLinkSearch::Destroy);
REPLACE(0x00455AE0, &Net::MsgImpLinkSearch::Destroy);
REPLACE(0x00455B10, &Net::StreamLinkSearch::Destroy);
REPLACE(0x00455B50, &Net::MsgLinkNode::DestroyBody);
REPLACE(0x00455B70, &Net::MsgImpLinkNode::DestroyBody);
REPLACE(0x00455B90, &Net::MsgSeqLinkNode::DestroyBody);
REPLACE(0x00455BB0, &Net::StreamLinkNode::DestroyBody);
REPLACE(0x00455C80, &Net::ConnView::Construct);
REPLACE(0x00456790, &Net::ConnView::Destroy);
REPLACE_SOURCE_ONLY(0x00455970, &Net::ConnView::GetSendInterval); // 4 bytes
REPLACE(0x00455980, &Net::ConnView::SetForceSendThisFrame);
REPLACE(0x00455990, &Net::ConnView::GetForceSendThisFrame);
REPLACE(0x004559A0, &Net::ConnView::GetBandwidthType);
REPLACE(0x004559B0, &Net::ConnView::SetBandwidth);
REPLACE(0x004559C0, &Net::ConnView::GetBandwidth);
REPLACE(0x00455A10, &Net::ConnView::IsForeign);
REPLACE(0x00455A20, &Net::ConnView::IsLocal);
REPLACE(0x00455A30, &Net::ConnView::IsRemote);
REPLACE(0x00455A40, &Net::ConnView::SetBandwidthType);
REPLACE(0x00455B40, &Net::ConnView::GetAveLatency);
REPLACE(0x00455C10, &Net::ConnView::Invalidate);
REPLACE(0x00455C20, &Net::ConnView::IsValid);
REPLACE(0x00455C30, &Net::ConnView::GetResendThreshold);
REPLACE(0x00455E50, &Net::ConnView::DestroyReceivedSequencedMessages);
REPLACE(0x00455F30, &Net::ConnView::DestroyLinks);
REPLACE(0x00456210, &Net::ConnView::DestroyImportantMessages);
REPLACE(0x00456370, &Net::ConnView::ReleaseQueuedMessages);
REPLACE(0x00456540, &Net::ConnView::GetHighestResendCount);
REPLACE(0x004565E0, &Net::ConnView::GetNumQueuedStreamPieces);
REPLACE(0x00456630, &Net::ConnView::ClearResendCounts);
REPLACE(0x004566D0, &Net::ConnView::FlagMessagesForResending);
REPLACE(0x00456770, &Net::ConnView::DestroyMessageQueues);
