// The App's message bundling and its handlers for the library's own messages. See net_app_messages.h.

#include "game/net/net_app_messages.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/search.h"
#include "game/net/net_access_c.h"
#include "game/net/net_compress.h"
#include "game/net/net_lzham.h"
#include "game/util/lzss.h"

namespace NetLink {

uint32_t g_maxLatency{2000};

namespace {

constexpr uint32_t kMaxStreamStartData = 0x100; // the first piece of a stream, in its start message

uint32_t Timestamp(App* app) { return app->m_clock; }
Dispatcher* GetDispatcher(App* app) { return &app->m_dispatcher; }

void ConstructContext(InboundMsgCtx* context) { context->Construct(); }

// Method 1's decompressor: 0 when it worked; `size` gets the expanded size.
int Decompress(const char* in, int inSize, char* out, int* size)
{
    return RleDecompress(reinterpret_cast<const uint8_t*>(in), inSize, reinterpret_cast<uint8_t*>(out), size);
}

// NOT ORIGINAL: the received streams' bounds (a compressed message comes from the network - THUGPRO's players among
// others, docs/thugpro-online.md section 9). The original decoders read past a stream that ends inside a code, and
// LZSS writes as much as the stream says: each stream is walked first, and expanded only when it ends exactly at its
// end (RLE) or expands to exactly `expected` bytes (LZSS) - what the original's own size check then accepts.
bool RleStreamFits(const uint8_t* in, int size)
{
    while (size > 0) {
        uint8_t code = *in++;
        --size;
        int bytes = code < 0x80 ? code + 1 : 1; // a literal's bytes, or a run's one byte
        if (bytes > size)
            return false;
        in += bytes;
        size -= bytes;
    }
    return true;
}
bool LzssStreamFits(const uint8_t* in, int size, int expected)
{
    int expanded = 0;
    unsigned int flags = 0;
    for (;;) {
        flags >>= 1;
        if ((flags & 0x100) == 0) {
            if (size == 0)
                return expanded == expected;
            flags = *in++ | 0xFF00;
            --size;
        }
        if (size == 0)
            return expanded == expected;
        if (flags & 1) {
            ++in;
            --size;
            ++expanded;
        } else {
            if (size < 2)
                return false; // the original reads on past the end here
            expanded += (in[1] & 0x0F) + 3;
            in += 2;
            size -= 2;
        }
        if (expanded > expected)
            return false;
    }
}

void FreeQueuedMsg(PendingMsg* msg)
{
    if (--msg->m_shareCount == 0) {
        DestroyQueuedMsg(msg);
        MemSys::Manager::Get()->Free(msg);
    }
}

// A normal or important message into the packet: its id, its length (unless the id has none), its data.
bool AppendMessage(App* app, Conn* conn, PendingMsg* msg)
{
    char*& writePtr = conn->m_writePtr;
    uint8_t flags = MessageFlags(static_cast<uint8_t*>(app->m_supervisor), msg->m_id);
    auto size = static_cast<uint16_t>(msg->m_byteCount + ((~flags & kMsgNoLength) ? 3 : 1));
    if (static_cast<int>(size - reinterpret_cast<uint32_t>(conn->m_writeBuffer) +
                         reinterpret_cast<uint32_t>(writePtr)) >= static_cast<int>(kMaxPacketSize))
        return false;
    *writePtr++ = static_cast<char>(msg->m_id);
    if (!(MessageFlags(static_cast<uint8_t*>(app->m_supervisor), msg->m_id) & kMsgNoLength)) {
        *reinterpret_cast<uint16_t*>(writePtr) = msg->m_byteCount;
        writePtr += 2;
    }
    if (msg->m_byteCount) {
        std::memcpy(writePtr, msg->m_data, msg->m_byteCount);
        writePtr += msg->m_byteCount;
    }
    AddMessageMetric(&conn->m_sendMetrics, msg->m_id, msg->m_byteCount);
    return true;
}

// A sequenced message into the packet, wrapped in a message of id 7: group, sequence number, id, data.
bool AppendSequencedMessage(Conn* conn, OrderedMsgNode* link)
{
    char*& writePtr = conn->m_writePtr;
    SeqQueuedMsg* msg = link->m_sharedMsg;
    uint16_t length = msg->m_byteCount;
    auto size = static_cast<uint16_t>(length + 9);
    if (static_cast<int>(size - reinterpret_cast<uint32_t>(conn->m_writeBuffer) +
                         reinterpret_cast<uint32_t>(writePtr)) >= static_cast<int>(kMaxPacketSize))
        return false;
    *writePtr++ = static_cast<char>(kMsgSequenced);
    *reinterpret_cast<uint16_t*>(writePtr) = static_cast<uint16_t>(length + 6);
    writePtr += 2;
    *writePtr++ = static_cast<char>(link->m_bundleId);
    *reinterpret_cast<uint32_t*>(writePtr) = link->m_orderIndex;
    writePtr += 4;
    *writePtr++ = static_cast<char>(msg->m_id);
    if (msg->m_byteCount) {
        std::memcpy(writePtr, msg->m_data, msg->m_byteCount);
        writePtr += msg->m_byteCount;
    }
    AddMessageMetric(&conn->m_sendMetrics, msg->m_id, msg->m_byteCount);
    return true;
}

// The messages a stream became, dispatched; the stream is deleted unless a handler keeps it.
int DispatchStream(InboundMsgCtx* context, BulkStreamNode* link, StreamDesc* desc, bool checkChecksum)
{
    InboundMsgCtx whole;
    ConstructContext(&whole);
    whole.m_receiverApp = context->m_receiverApp;
    whole.m_senderConn = context->m_senderConn;
    whole.m_typeCode = desc->m_msgId;
    whole.m_bodySize = desc->m_size;
    whole.m_wireOptions = 0;
    whole.m_body = static_cast<char*>(MemSys::New(desc->m_size));
    std::memcpy(whole.m_body, desc->m_data, desc->m_size);
    if (checkChecksum && Checksums::GenerateCRCFromBuffer(whole.m_body, desc->m_size) != desc->m_checksum)
        whole.m_wireOptions |= kPacketChecksumMismatch;
    int result = GetDispatcher(context->m_receiverApp)->DispatchMsg(&whole);
    if (result != kHandlerKeep && result != kHandlerMsgDestroyed) {
        if (StreamDesc* stream = link->m_msgMeta) {
            DestroyStreamDesc(stream);
            MemSys::Manager::Get()->Free(stream);
        }
        UnlinkListNode(link);
    }
    MemSys::Manager::Get()->Free(whole.m_body);
    MemSys::Manager::Get()->LeaveHeapScope();
    return result;
}

} // namespace

// 0x004598C0
bool App::BundleMessages(Conn* conn, int queue, bool resendsOnly)
{
    bool fits = true;
    switch (queue) {
    case kQueueNormal: {
        ListKit::Search<MsgNormalLink> search;
        MsgNormalLink* next;
        for (MsgNormalLink* link = search.HeadElement(&conn->m_normalMsgs); link; link = next) {
            next = search.NextItem();
            if (Timestamp(this) < link->m_dueTime)
                continue;
            if (NetLink::OverBandwidthBudget(this, conn))
                return fits;
            PendingMsg* msg = link->m_sharedMsg;
            if (!AppendMessage(this, conn, msg)) {
                fits = false;
                continue;
            }
            FreeQueuedMsg(msg);
            UnlinkListNode(link);
        }
        break;
    }
    case kQueueImportant: {
        ListKit::Search<ReliableMsgNode> search;
        for (ReliableMsgNode* link = search.HeadElement(&conn->m_importantMsgs); link;
             link = search.NextItem()) {
            if (Timestamp(this) < link->m_dueTime)
                continue;
            if (NetLink::OverBandwidthBudget(this, conn))
                return fits;
            // never sent (unless only resends are wanted), or not acked within the resend threshold
            if (link->m_stampMs == 0) {
                if (resendsOnly)
                    continue;
            } else {
                uint32_t threshold = GetRetryDelay(conn);
                if (Timestamp(this) - link->m_stampMs <= threshold)
                    continue;
            }
            if (!AppendMessage(this, conn, link->m_sharedMsg)) {
                fits = false;
                continue;
            }
            if (link->m_stampMs != 0)
                ++link->m_retryCount;
            link->m_stampMs = Timestamp(this);
            link->m_packetClock = conn->m_timestampOut;
        }
        break;
    }
    case kQueueSequenced: {
        ListKit::Search<OrderedMsgNode> search;
        for (OrderedMsgNode* link = search.HeadElement(&conn->m_sequencedMsgs); link;
             link = search.NextItem()) {
            if (Timestamp(this) < link->m_dueTime)
                continue;
            if (NetLink::OverBandwidthBudget(this, conn))
                return fits;
            // never sent (unless only resends are wanted), or not acked within the resend threshold
            if (link->m_stampMs == 0) {
                if (resendsOnly)
                    continue;
            } else {
                uint32_t threshold = GetRetryDelay(conn);
                if (Timestamp(this) - link->m_stampMs <= threshold)
                    continue;
            }
            if (!AppendSequencedMessage(conn, link)) {
                fits = false;
                continue;
            }
            if (link->m_stampMs != 0)
                ++link->m_retryCount;
            link->m_stampMs = Timestamp(this);
            link->m_packetClock = conn->m_timestampOut;
        }
        break;
    }
    default:
        break;
    }
    return fits;
}

// 0x0045AB60
int App::HandleLatencyResponse(InboundMsgCtx* context)
{
    Conn* conn = context->m_senderConn;
    auto* sentTime = reinterpret_cast<uint32_t*>(context->m_body);
    if (!NetLink::IsLocal(conn)) {
        uint32_t now = Timestamp(context->m_receiverApp);
        if (*sentTime == conn->m_latencyTest.m_sendTime) {
            conn->m_latencyTest.m_receiveTime = *sentTime;
            uint32_t latency = now - *sentTime;
            if (latency > MaxLatency())
                latency = MaxLatency();
            AddLatency(conn, latency);
        }
    }
    return kHandlerContinue;
}

// 0x0045AD60
int App::HandleLatencyTest(InboundMsgCtx* context)
{
    NetMessageInfo desc;
    ConstructMsgDesc(&desc);
    uint32_t sentTime = *reinterpret_cast<uint32_t*>(context->m_body);
    desc.m_data = &sentTime;
    desc.m_byteCount = sizeof(sentTime);
    desc.m_id = kMsgLatencyResponse;
    NetLink::PutOnOutbox(context->m_receiverApp, context->m_senderConn->m_handle, &desc);
    return kHandlerContinue;
}

// 0x0045ABF0
int App::HandleCompressedMessages(InboundMsgCtx* context)
{
    InboundMsgCtx inner;
    ConstructContext(&inner);
    char* msg = context->m_body;
    // NOT ORIGINAL: a message too short for its header, or expanding to less than a message's id and length, is
    // dropped (the original reads past them).
    if (context->m_bodySize < 3)
        return kHandlerMsgDone;
    int size = *reinterpret_cast<uint16_t*>(msg + 1);
    uint8_t method = static_cast<uint8_t>(msg[0]);
    if (size < 3)
        return kHandlerMsgDone;
    const auto* stream = reinterpret_cast<const uint8_t*>(msg + 3);
    int streamSize = static_cast<int>(context->m_bodySize) - 3;
    auto* data = static_cast<char*>(MemSys::New(size));
    int expectedSize = size;
    if (method == 1) {
        if (!RleStreamFits(stream, streamSize) || Decompress(msg + 3, streamSize, data, &size) != 0 ||
            expectedSize != size) {
            MemSys::Manager::Get()->Free(data);
            return kHandlerMsgDone;
        }
    } else if (method == 2) {
        if (!LzssStreamFits(stream, streamSize, expectedSize)) {
            MemSys::Manager::Get()->Free(data);
            return kHandlerMsgDone;
        }
        size = static_cast<int>(reinterpret_cast<char*>(Lzss::Decode(reinterpret_cast<uint8_t*>(msg + 3),
                                                                     reinterpret_cast<uint8_t*>(data),
                                                                     streamSize)) -
                                data);
        if (expectedSize != size) {
            MemSys::Manager::Get()->Free(data);
            return kHandlerMsgDone;
        }
    } else if (method == Lzham::kMethod) {
        // NOT ORIGINAL: THUGPRO's LZHAM (TP-R11), expanded into the header's size and no further.
        size_t expanded = 0;
        if (!Lzham::Decompress(stream, static_cast<size_t>(streamSize), reinterpret_cast<uint8_t*>(data),
                               static_cast<size_t>(expectedSize), &expanded) ||
            expanded != static_cast<size_t>(expectedSize)) {
            MemSys::Manager::Get()->Free(data);
            return kHandlerMsgDone;
        }
    } else {
        // (The original uses the buffer as it is, not expanded - uninitialised memory dispatched. NOT ORIGINAL: dropped.)
        MemSys::Manager::Get()->Free(data);
        return kHandlerMsgDone;
    }

    // The expanded data is one message: id, length, data.
    inner.m_senderConn = context->m_senderConn;
    uint8_t id = static_cast<uint8_t>(data[0]);
    uint16_t length = *reinterpret_cast<uint16_t*>(data + 1);
    if (length > expectedSize - 3) { // NOT ORIGINAL: a length past the expanded data (the original copies on)
        MemSys::Manager::Get()->Free(data);
        return kHandlerMsgDone;
    }
    auto* copy = static_cast<char*>(MemSys::New(length));
    std::memcpy(copy, data + 3, length);
    inner.m_body = copy;
    inner.m_receiverApp = context->m_receiverApp;
    inner.m_wireOptions = context->m_wireOptions;
    inner.m_typeCode = id;
    inner.m_bodySize = length;
    int result = GetDispatcher(context->m_receiverApp)->DispatchMsg(&inner);
    MemSys::Manager::Get()->Free(inner.m_body);
    MemSys::Manager::Get()->Free(data);
    return result;
}

// 0x0045AF10
int App::HandleSequencedMessages(InboundMsgCtx* context)
{
    auto* msg = reinterpret_cast<uint8_t*>(context->m_body);
    Conn* conn = context->m_senderConn;
    context->m_Acknowledge = true;
    uint32_t sequence = *reinterpret_cast<uint32_t*>(msg + 1);
    uint8_t group = msg[0];
    if (sequence < conn->m_sequenceNumbers[group])
        return kHandlerContinue; // handled already

    MemSys::Manager* manager = MemSys::Manager::Get();
    manager->EnterHeapScope(manager->m_networkHeap);
    uint8_t id = msg[5];
    auto length = static_cast<uint16_t>(context->m_bodySize - 6);
    void* memory = MemSys::New(sizeof(SeqQueuedMsg));
    SeqQueuedMsg* seqMsg = memory ? ConstructSeqMsg(memory, id, length, reinterpret_cast<char*>(msg + 6), group, false)
                            : nullptr;
    void* linkMemory = MemSys::AllocZeroed(sizeof(OrderedMsgNode));
    OrderedMsgNode* link = linkMemory ? ConstructMsgSeqLink(linkMemory, seqMsg) : nullptr;
    // (a failed allocation crashes here, as in the original)
    link->m_listLink.priority = static_cast<int>(sequence);
    link->m_stampMs = 0;
    link->m_packetClock = 0;
    link->m_bundleId = group;
    link->m_orderIndex = sequence;
    MemSys::Manager::Get()->LeaveHeapScope();

    auto& buffer = reinterpret_cast<ListKit::Head&>(context->m_senderConn->m_receivedQueues[group]);
    if (!buffer.InsertByUniqueOrder(&link->m_listLink)) { // a duplicate
        if (SeqQueuedMsg* duplicate = link->m_sharedMsg) {
            DestroyQueuedMsg(duplicate);
            MemSys::Manager::Get()->Free(duplicate);
        }
        UnlinkListNode(link);
        return kHandlerMsgDone;
    }
    return kHandlerContinue;
}

// 0x0045B0B0
int App::HandleAck(InboundMsgCtx* context)
{
    auto* packetstamp = reinterpret_cast<uint16_t*>(context->m_body);
    uint32_t sentTime = 0;
    context->m_bodySize = sizeof(uint16_t);
    if (context->m_wireOptions & kPacketForeign)
        return kHandlerMsgDone;

    {
        ListKit::Search<ReliableMsgNode> search;
        ReliableMsgNode* next;
        for (ReliableMsgNode* link = search.HeadElement(&context->m_senderConn->m_importantMsgs); link;
             link = next) {
            next = search.NextItem();
            if (*packetstamp == static_cast<uint16_t>(link->m_packetClock) && link->m_stampMs) {
                FreeQueuedMsg(link->m_sharedMsg);
                sentTime = link->m_stampMs;
                UnlinkListNode(link);
            }
        }
    }
    {
        ListKit::Search<OrderedMsgNode> search;
        OrderedMsgNode* next;
        for (OrderedMsgNode* link = search.HeadElement(&context->m_senderConn->m_sequencedMsgs); link;
             link = next) {
            next = search.NextItem();
            if (*packetstamp == static_cast<uint16_t>(link->m_packetClock) && link->m_stampMs) {
                SeqQueuedMsg* msg = link->m_sharedMsg;
                if (--msg->m_shareCount == 0) {
                    DestroyQueuedMsg(msg);
                    MemSys::Manager::Get()->Free(msg);
                }
                sentTime = link->m_stampMs;
                UnlinkListNode(link);
            }
        }
    }

    Conn* conn = context->m_senderConn;
    if (IsRemote(conn) && (conn->m_status & kConnStatus1) && sentTime) {
        uint32_t latency = Timestamp(context->m_receiverApp) - sentTime;
        uint32_t current = NetLink::IsLocal(conn) ? 0 : static_cast<uint32_t>(conn->m_latencyTest.m_averageLatency);
        if (latency > current) {
            if (latency > MaxLatency())
                latency = MaxLatency();
            AddLatency(context->m_senderConn, latency);
        }
    }
    return kHandlerContinue;
}

// 0x0045B2F0
int App::HandleStreamMessages(InboundMsgCtx* context)
{
    MemSys::Manager* manager = MemSys::Manager::Get();
    manager->EnterHeapScope(manager->m_networkHeap);
    switch (context->m_typeCode) {
    case kMsgStreamData: {
        // stream id (4 bytes), then the piece
        char* msg = context->m_body;
        ListKit::Search<BulkStreamNode> search;
        for (BulkStreamNode* link = search.HeadElement(&context->m_senderConn->m_receivedStreams); link;
             link = search.NextItem()) {
            StreamDesc* desc = link->m_msgMeta;
            if (desc->m_streamId != *reinterpret_cast<int*>(msg))
                continue;
            uint32_t size = context->m_bodySize - 4;
            std::memcpy(desc->m_next, msg + 4, size);
            desc->m_next += size;
            if (static_cast<uint32_t>(desc->m_next - desc->m_data) >= static_cast<uint32_t>(desc->m_size))
                return DispatchStream(context, link, desc, true);
        }
        break;
    }
    case kMsgStreamStart: {
        // name (0x20 bytes), size, stream id, checksum, message id, then the first piece
        char* msg = context->m_body;
        int streamId = *reinterpret_cast<int*>(msg + 0x24);
        ListKit::Search<BulkStreamNode> search;
        for (BulkStreamNode* link = search.HeadElement(&context->m_senderConn->m_receivedStreams); link;
             link = search.NextItem()) {
            if (link->m_msgMeta->m_streamId == streamId) { // started already
                MemSys::Manager::Get()->LeaveHeapScope();
                return kHandlerMsgDone;
            }
        }
        void* memory = MemSys::New(sizeof(StreamDesc));
        StreamDesc* desc = memory ? ConstructStreamDesc(memory) : nullptr;
        for (int i = 0; (desc->m_desc[i] = msg[i]) != 0; ++i) // (strcpy: no limit)
            ;
        desc->m_size = *reinterpret_cast<int*>(msg + 0x20);
        desc->m_streamId = *reinterpret_cast<int*>(msg + 0x24);
        desc->m_checksum = *reinterpret_cast<uint32_t*>(msg + 0x28);
        desc->m_msgId = static_cast<uint8_t>(msg[0x2C]);
        desc->m_data = static_cast<uint8_t*>(MemSys::New(*reinterpret_cast<uint32_t*>(msg + 0x20)));
        desc->m_next = desc->m_data;
        int size = *reinterpret_cast<int*>(msg + 0x20);
        if (size >= static_cast<int>(kMaxStreamStartData))
            size = kMaxStreamStartData;
        std::memcpy(desc->m_data, msg + 0x2D, size);
        desc->m_next += size;
        void* linkMemory = MemSys::AllocZeroed(sizeof(BulkStreamNode));
        BulkStreamNode* link = linkMemory ? ConstructStreamLink(linkMemory, desc) : nullptr;
        // at the end of the connection's streams
        MemSys::ListNode* head = &context->m_senderConn->m_receivedStreams;
        link->m_listLink.next = head;
        link->m_listLink.prev = head->prev;
        head->prev->next = &link->m_listLink;
        head->prev = &link->m_listLink;
        if (static_cast<uint32_t>(desc->m_next - desc->m_data) >= static_cast<uint32_t>(desc->m_size)) // all in one message
            return DispatchStream(context, link, desc, false); // (no checksum check on this path)
        break;
    }
    default:
        break;
    }
    MemSys::Manager::Get()->LeaveHeapScope();
    return kHandlerContinue;
}

} // namespace Net
