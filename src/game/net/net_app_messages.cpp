// The App's message bundling and its handlers for the library's own messages. See net_app_messages.h.

#include "game/net/net_app_messages.h"

#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/lst/search.h"
#include "game/net/net_access_c.h"
#include "game/util/lzss.h"
#include "hook/hook.h"

namespace Net {

namespace {

constexpr uint32_t kMaxStreamStartData = 0x100; // the first piece of a stream, in its start message

uint32_t Timestamp(App* app) { return Field<uint32_t>(app, AppField::kTimestamp); }
Dispatcher* GetDispatcher(App* app) { return &Field<Dispatcher>(app, AppField::kDispatcher); }
Mem::ListNode* ListOf(Conn* conn, uint32_t offset) { return &Field<Mem::ListNode>(conn, offset); }

void ConstructContext(MsgHandlerContext* context)
{
    GameFunction<MsgHandlerContext*(__thiscall*)(MsgHandlerContext*)>(0x00454A20)(context);
}

// (0x00454C20) method 1's decompressor: 0 when it worked; `size` gets the expanded size.
int Decompress(const char* in, int inSize, char* out, int* size)
{
    return GameFunction<int(__cdecl*)(const char*, int, char*, int*)>(0x00454C20)(in, inSize, out, size);
}

void FreeQueuedMsg(QueuedMsg* msg)
{
    if (--msg->m_refCount == 0) {
        DestroyQueuedMsg(msg);
        Mem::Manager::Get()->Free(msg);
    }
}

// A normal or important message into the packet: its id, its length (unless the id has none), its data.
bool AppendMessage(App* app, Conn* conn, QueuedMsg* msg)
{
    char*& writePtr = Field<char*>(conn, ConnField::kWritePtr);
    uint8_t flags = MessageFlags(Field<uint8_t*>(app, AppField::kManager), msg->m_id);
    auto size = static_cast<uint16_t>(msg->m_length + ((~flags & kMsgNoLength) ? 3 : 1));
    if (static_cast<int>(size - reinterpret_cast<uint32_t>(Field<char*>(conn, ConnField::kWriteBuffer)) +
                         reinterpret_cast<uint32_t>(writePtr)) >= static_cast<int>(kMaxPacketSize))
        return false;
    *writePtr++ = static_cast<char>(msg->m_id);
    if (!(MessageFlags(Field<uint8_t*>(app, AppField::kManager), msg->m_id) & kMsgNoLength)) {
        *reinterpret_cast<uint16_t*>(writePtr) = msg->m_length;
        writePtr += 2;
    }
    if (msg->m_length) {
        std::memcpy(writePtr, msg->m_data, msg->m_length);
        writePtr += msg->m_length;
    }
    AddMessageMetric(&Field<uint8_t>(conn, ConnField::kSendMetrics), msg->m_id, msg->m_length);
    return true;
}

// A sequenced message into the packet, wrapped in a message of id 7: group, sequence number, id, data.
bool AppendSequencedMessage(Conn* conn, MsgSeqLink* link)
{
    char*& writePtr = Field<char*>(conn, ConnField::kWritePtr);
    SeqQueuedMsg* msg = link->m_QMsg;
    uint16_t length = msg->m_length;
    auto size = static_cast<uint16_t>(length + 9);
    if (static_cast<int>(size - reinterpret_cast<uint32_t>(Field<char*>(conn, ConnField::kWriteBuffer)) +
                         reinterpret_cast<uint32_t>(writePtr)) >= static_cast<int>(kMaxPacketSize))
        return false;
    *writePtr++ = static_cast<char>(kMsgSequenced);
    *reinterpret_cast<uint16_t*>(writePtr) = static_cast<uint16_t>(length + 6);
    writePtr += 2;
    *writePtr++ = static_cast<char>(link->m_GroupId);
    *reinterpret_cast<uint32_t*>(writePtr) = link->m_SequenceId;
    writePtr += 4;
    *writePtr++ = static_cast<char>(msg->m_id);
    if (msg->m_length) {
        std::memcpy(writePtr, msg->m_data, msg->m_length);
        writePtr += msg->m_length;
    }
    AddMessageMetric(&Field<uint8_t>(conn, ConnField::kSendMetrics), msg->m_id, msg->m_length);
    return true;
}

// The messages a stream became, dispatched; the stream is deleted unless a handler keeps it.
int DispatchStream(MsgHandlerContext* context, StreamLink* link, StreamDesc* desc, bool checkChecksum)
{
    MsgHandlerContext whole;
    ConstructContext(&whole);
    whole.m_App = context->m_App;
    whole.m_Conn = context->m_Conn;
    whole.m_MsgId = desc->m_msgId;
    whole.m_MsgLength = desc->m_size;
    whole.m_PacketFlags = 0;
    whole.m_Msg = static_cast<char*>(Mem::New(desc->m_size));
    std::memcpy(whole.m_Msg, desc->m_data, desc->m_size);
    if (checkChecksum && Crc::GenerateCRCFromBuffer(whole.m_Msg, desc->m_size) != desc->m_checksum)
        whole.m_PacketFlags |= kPacketChecksumMismatch;
    int result = GetDispatcher(context->m_App)->DispatchMsg(&whole);
    if (result != kHandlerKeep && result != kHandlerMsgDestroyed) {
        if (StreamDesc* stream = link->m_Desc) {
            DestroyStreamDesc(stream);
            Mem::Manager::Get()->Free(stream);
        }
        DeleteNode(link);
    }
    Mem::Manager::Get()->Free(whole.m_Msg);
    Mem::Manager::Get()->PopContext();
    return result;
}

} // namespace

// 0x004598C0
bool App::BundleMessages(Conn* conn, int queue, bool resendsOnly)
{
    bool fits = true;
    switch (queue) {
    case kQueueNormal: {
        Lst::Search<MsgNormalLink> search;
        MsgNormalLink* next;
        for (MsgNormalLink* link = search.FirstItem(ListOf(conn, ConnField::kNormalQueue)); link; link = next) {
            next = search.NextItem();
            if (Timestamp(this) < link->m_SendTime)
                continue;
            if (Net::BandwidthExceeded(this, conn))
                return fits;
            QueuedMsg* msg = link->m_QMsg;
            if (!AppendMessage(this, conn, msg)) {
                fits = false;
                continue;
            }
            FreeQueuedMsg(msg);
            DeleteNode(link);
        }
        break;
    }
    case kQueueImportant: {
        Lst::Search<MsgImpLink> search;
        for (MsgImpLink* link = search.FirstItem(ListOf(conn, ConnField::kImportantQueue)); link;
             link = search.NextItem()) {
            if (Timestamp(this) < link->m_SendTime)
                continue;
            if (Net::BandwidthExceeded(this, conn))
                return fits;
            // never sent (unless only resends are wanted), or not acked within the resend threshold
            if (link->m_Timestamp == 0) {
                if (resendsOnly)
                    continue;
            } else {
                uint32_t threshold = GetResendThreshold(conn);
                if (Timestamp(this) - link->m_Timestamp <= threshold)
                    continue;
            }
            if (!AppendMessage(this, conn, link->m_QMsg)) {
                fits = false;
                continue;
            }
            if (link->m_Timestamp != 0)
                ++link->m_NumResends;
            link->m_Timestamp = Timestamp(this);
            link->m_Packetstamp = Field<uint32_t>(conn, ConnField::kTimestampOut);
        }
        break;
    }
    case kQueueSequenced: {
        Lst::Search<MsgSeqLink> search;
        for (MsgSeqLink* link = search.FirstItem(ListOf(conn, ConnField::kSequencedQueue)); link;
             link = search.NextItem()) {
            if (Timestamp(this) < link->m_SendTime)
                continue;
            if (Net::BandwidthExceeded(this, conn))
                return fits;
            // never sent (unless only resends are wanted), or not acked within the resend threshold
            if (link->m_Timestamp == 0) {
                if (resendsOnly)
                    continue;
            } else {
                uint32_t threshold = GetResendThreshold(conn);
                if (Timestamp(this) - link->m_Timestamp <= threshold)
                    continue;
            }
            if (!AppendSequencedMessage(conn, link)) {
                fits = false;
                continue;
            }
            if (link->m_Timestamp != 0)
                ++link->m_NumResends;
            link->m_Timestamp = Timestamp(this);
            link->m_Packetstamp = Field<uint32_t>(conn, ConnField::kTimestampOut);
        }
        break;
    }
    default:
        break;
    }
    return fits;
}

// 0x0045AB60
int App::HandleLatencyResponse(MsgHandlerContext* context)
{
    Conn* conn = context->m_Conn;
    auto* sentTime = reinterpret_cast<uint32_t*>(context->m_Msg);
    if (!Net::IsLocal(conn)) {
        uint32_t now = Timestamp(context->m_App);
        if (*sentTime == Field<uint32_t>(conn, ConnField::kLatencyTestSent)) {
            Field<uint32_t>(conn, ConnField::kLatencyTestAnswered) = *sentTime;
            uint32_t latency = now - *sentTime;
            if (latency > MaxLatency())
                latency = MaxLatency();
            AddLatency(conn, latency);
        }
    }
    return kHandlerContinue;
}

// 0x0045AD60
int App::HandleLatencyTest(MsgHandlerContext* context)
{
    MsgDesc desc;
    ConstructMsgDesc(&desc);
    uint32_t sentTime = *reinterpret_cast<uint32_t*>(context->m_Msg);
    desc.m_data = &sentTime;
    desc.m_length = sizeof(sentTime);
    desc.m_id = kMsgLatencyResponse;
    Net::EnqueueMessage(context->m_App, Field<int>(context->m_Conn, ConnField::kHandle), &desc);
    return kHandlerContinue;
}

// 0x0045ABF0
int App::HandleCompressedMessages(MsgHandlerContext* context)
{
    MsgHandlerContext inner;
    ConstructContext(&inner);
    char* msg = context->m_Msg;
    int size = *reinterpret_cast<uint16_t*>(msg + 1);
    uint8_t method = static_cast<uint8_t>(msg[0]);
    auto* data = static_cast<char*>(Mem::New(size));
    int expectedSize = size;
    if (method == 1) {
        if (Decompress(msg + 3, context->m_MsgLength - 3, data, &size) != 0 || expectedSize != size) {
            Mem::Manager::Get()->Free(data);
            return kHandlerMsgDone;
        }
    } else if (method == 2) {
        size = static_cast<int>(reinterpret_cast<char*>(Lzss::Decode(reinterpret_cast<uint8_t*>(msg + 3),
                                                                     reinterpret_cast<uint8_t*>(data),
                                                                     context->m_MsgLength - 3)) -
                                data);
        if (expectedSize != size) {
            Mem::Manager::Get()->Free(data);
            return kHandlerMsgDone;
        }
    } // (any other method: the buffer is used as it is, not expanded)

    // The expanded data is one message: id, length, data.
    inner.m_Conn = context->m_Conn;
    uint8_t id = static_cast<uint8_t>(data[0]);
    uint16_t length = *reinterpret_cast<uint16_t*>(data + 1);
    auto* copy = static_cast<char*>(Mem::New(length));
    std::memcpy(copy, data + 3, length);
    inner.m_Msg = copy;
    inner.m_App = context->m_App;
    inner.m_PacketFlags = context->m_PacketFlags;
    inner.m_MsgId = id;
    inner.m_MsgLength = length;
    int result = GetDispatcher(context->m_App)->DispatchMsg(&inner);
    Mem::Manager::Get()->Free(inner.m_Msg);
    Mem::Manager::Get()->Free(data);
    return result;
}

// 0x0045AF10
int App::HandleSequencedMessages(MsgHandlerContext* context)
{
    auto* msg = reinterpret_cast<uint8_t*>(context->m_Msg);
    Conn* conn = context->m_Conn;
    context->m_Acknowledge = true;
    uint32_t sequence = *reinterpret_cast<uint32_t*>(msg + 1);
    uint8_t group = msg[0];
    if (sequence < Field<uint32_t>(conn, ConnField::kNextSequence + group * 4))
        return kHandlerContinue; // handled already

    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_networkHeap);
    uint8_t id = msg[5];
    auto length = static_cast<uint16_t>(context->m_MsgLength - 6);
    void* memory = Mem::New(sizeof(SeqQueuedMsg));
    SeqQueuedMsg* seqMsg = memory ? ConstructSeqMsg(memory, id, length, reinterpret_cast<char*>(msg + 6), group, false)
                            : nullptr;
    void* linkMemory = Mem::Calloc(sizeof(MsgSeqLink));
    MsgSeqLink* link = linkMemory ? ConstructMsgSeqLink(linkMemory, seqMsg) : nullptr;
    // (a failed allocation crashes here, as in the original)
    link->m_node.priority = static_cast<int>(sequence);
    link->m_Timestamp = 0;
    link->m_Packetstamp = 0;
    link->m_GroupId = group;
    link->m_SequenceId = sequence;
    Mem::Manager::Get()->PopContext();

    auto& buffer = Field<Lst::Head>(context->m_Conn, ConnField::kSequencedBuffer + group * sizeof(Lst::Head));
    if (!buffer.AddUniqueSequence(&link->m_node)) { // a duplicate
        if (SeqQueuedMsg* duplicate = link->m_QMsg) {
            DestroyQueuedMsg(duplicate);
            Mem::Manager::Get()->Free(duplicate);
        }
        DeleteNode(link);
        return kHandlerMsgDone;
    }
    return kHandlerContinue;
}

// 0x0045B0B0
int App::HandleAck(MsgHandlerContext* context)
{
    auto* packetstamp = reinterpret_cast<uint16_t*>(context->m_Msg);
    uint32_t sentTime = 0;
    context->m_MsgLength = sizeof(uint16_t);
    if (context->m_PacketFlags & kPacketForeign)
        return kHandlerMsgDone;

    {
        Lst::Search<MsgImpLink> search;
        MsgImpLink* next;
        for (MsgImpLink* link = search.FirstItem(ListOf(context->m_Conn, ConnField::kImportantQueue)); link;
             link = next) {
            next = search.NextItem();
            if (*packetstamp == static_cast<uint16_t>(link->m_Packetstamp) && link->m_Timestamp) {
                FreeQueuedMsg(link->m_QMsg);
                sentTime = link->m_Timestamp;
                DeleteNode(link);
            }
        }
    }
    {
        Lst::Search<MsgSeqLink> search;
        MsgSeqLink* next;
        for (MsgSeqLink* link = search.FirstItem(ListOf(context->m_Conn, ConnField::kSequencedQueue)); link;
             link = next) {
            next = search.NextItem();
            if (*packetstamp == static_cast<uint16_t>(link->m_Packetstamp) && link->m_Timestamp) {
                SeqQueuedMsg* msg = link->m_QMsg;
                if (--msg->m_refCount == 0) {
                    DestroyQueuedMsg(msg);
                    Mem::Manager::Get()->Free(msg);
                }
                sentTime = link->m_Timestamp;
                DeleteNode(link);
            }
        }
    }

    Conn* conn = context->m_Conn;
    if (IsRemote(conn) && (Field<uint32_t>(conn, ConnField::kStatus) & kConnStatus1) && sentTime) {
        uint32_t latency = Timestamp(context->m_App) - sentTime;
        uint32_t current = Net::IsLocal(conn) ? 0 : Field<uint32_t>(conn, ConnField::kLatency);
        if (latency > current) {
            if (latency > MaxLatency())
                latency = MaxLatency();
            AddLatency(context->m_Conn, latency);
        }
    }
    return kHandlerContinue;
}

// 0x0045B2F0
int App::HandleStreamMessages(MsgHandlerContext* context)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_networkHeap);
    switch (context->m_MsgId) {
    case kMsgStreamData: {
        // stream id (4 bytes), then the piece
        char* msg = context->m_Msg;
        Lst::Search<StreamLink> search;
        for (StreamLink* link = search.FirstItem(ListOf(context->m_Conn, ConnField::kStreams)); link;
             link = search.NextItem()) {
            StreamDesc* desc = link->m_Desc;
            if (desc->m_streamId != *reinterpret_cast<int*>(msg))
                continue;
            uint32_t size = context->m_MsgLength - 4;
            std::memcpy(desc->m_next, msg + 4, size);
            desc->m_next += size;
            if (static_cast<uint32_t>(desc->m_next - desc->m_data) >= static_cast<uint32_t>(desc->m_size))
                return DispatchStream(context, link, desc, true);
        }
        break;
    }
    case kMsgStreamStart: {
        // name (0x20 bytes), size, stream id, checksum, message id, then the first piece
        char* msg = context->m_Msg;
        int streamId = *reinterpret_cast<int*>(msg + 0x24);
        Lst::Search<StreamLink> search;
        for (StreamLink* link = search.FirstItem(ListOf(context->m_Conn, ConnField::kStreams)); link;
             link = search.NextItem()) {
            if (link->m_Desc->m_streamId == streamId) { // started already
                Mem::Manager::Get()->PopContext();
                return kHandlerMsgDone;
            }
        }
        void* memory = Mem::New(sizeof(StreamDesc));
        StreamDesc* desc = memory ? ConstructStreamDesc(memory) : nullptr;
        for (int i = 0; (desc->m_desc[i] = msg[i]) != 0; ++i) // (strcpy: no limit)
            ;
        desc->m_size = *reinterpret_cast<int*>(msg + 0x20);
        desc->m_streamId = *reinterpret_cast<int*>(msg + 0x24);
        desc->m_checksum = *reinterpret_cast<uint32_t*>(msg + 0x28);
        desc->m_msgId = static_cast<uint8_t>(msg[0x2C]);
        desc->m_data = static_cast<uint8_t*>(Mem::New(*reinterpret_cast<uint32_t*>(msg + 0x20)));
        desc->m_next = desc->m_data;
        int size = *reinterpret_cast<int*>(msg + 0x20);
        if (size >= static_cast<int>(kMaxStreamStartData))
            size = kMaxStreamStartData;
        std::memcpy(desc->m_data, msg + 0x2D, size);
        desc->m_next += size;
        void* linkMemory = Mem::Calloc(sizeof(StreamLink));
        StreamLink* link = linkMemory ? ConstructStreamLink(linkMemory, desc) : nullptr;
        // at the end of the connection's streams
        Mem::ListNode* head = ListOf(context->m_Conn, ConnField::kStreams);
        link->m_node.next = head;
        link->m_node.prev = head->prev;
        head->prev->next = &link->m_node;
        head->prev = &link->m_node;
        if (static_cast<uint32_t>(desc->m_next - desc->m_data) >= static_cast<uint32_t>(desc->m_size)) // all in one message
            return DispatchStream(context, link, desc, false); // (no checksum check on this path)
        break;
    }
    default:
        break;
    }
    Mem::Manager::Get()->PopContext();
    return kHandlerContinue;
}

} // namespace Net

REPLACE(0x004598C0, &Net::App::BundleMessages);
REPLACE(0x0045AB60, Net::App::HandleLatencyResponse);
REPLACE(0x0045AD60, Net::App::HandleLatencyTest);
REPLACE(0x0045ABF0, Net::App::HandleCompressedMessages);
REPLACE(0x0045AF10, Net::App::HandleSequencedMessages);
REPLACE(0x0045B0B0, Net::App::HandleAck);
REPLACE(0x0045B2F0, Net::App::HandleStreamMessages);
