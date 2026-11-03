// The App's message bundling and its handlers for the library's own messages. See net_app_messages.h.

#include "engine/net/net_app_messages.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <gsl/assert>

#include "wire/message_ids.h"
#include "engine/core/enum_ops.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/chain_start.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app.h"
#include "engine/net/net_app_view.h"
#include "engine/net/net_compress.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_lzham.h"
#include "engine/net/net_messages.h"
#include "wire/net_wire.h"
#include "formats/byte_stream.h"
#include "formats/lzss.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"

namespace net {


namespace {
// The packed stream's flag byte: refilled every eight items; a match's length nibble; the sequenced header.
constexpr unsigned kFlagsRefillMarker = 0x100;
constexpr unsigned kFlagsRefill = 0xFF00;
constexpr int kMatchLengthMask = 0x0F;
constexpr int kSequencedHeaderBytes = 9;

constexpr size_t kMaxStreamStartData = wire::kStreamPieceSize; // the first piece of a stream, in its start message

uint32_t get_timestamp(AppHost* app) { return app->clock(); }
Router* get_dispatcher(AppHost* app) { return &app->dispatcher(); }

void construct_context(InboundMsgCtx* context) { *context = InboundMsgCtx(); }

// NOT ORIGINAL: the room left in the packet being built (the writers checked first that the message fits).
size_t packet_space_left(Conn* conn)
{
    return static_cast<size_t>(kMaxPacketSize) - conn->outgoing().used;
}

// NOT ORIGINAL: a received message's data.
util::ByteReader body_reader(const InboundMsgCtx* context) { return {context->body(), context->body_size()}; }


// NOT ORIGINAL: the received streams' bounds (a compressed message comes from the network - THUGPRO's players among
// others, docs/thugpro-online.md section 9). The original decoders read past a stream that ends inside a code, and
// LZSS writes as much as the stream says: each stream is walked first, and expanded only when it ends exactly at its
// end (RLE) or expands to exactly `expected` bytes (LZSS) - what the original's own size check then accepts.
bool rle_stream_fits(std::span<const uint8_t> stream)
{
    core::Cursor<const uint8_t> in(stream);
    int size = static_cast<int>(stream.size());
    while (size > 0) {
        const uint8_t code = *in++;
        --size;
        const int bytes = code < 0x80 ? code + 1 : 1; // a literal's bytes, or a run's one byte
        if (bytes > size) {
            return false;
        }
        in += bytes;
        size -= bytes;
    }
    return true;
}
bool lzss_stream_fits(std::span<const uint8_t> stream, int expected)
{
    core::Cursor<const uint8_t> in(stream);
    int size = static_cast<int>(stream.size());
    int expanded = 0;
    unsigned int flags = 0;
    for (;;) {
        flags >>= 1;
        if ((flags & kFlagsRefillMarker) == 0) {
            if (size == 0) {
                return expanded == expected;
            }
            flags = *in++ | kFlagsRefill;
            --size;
        }
        if (size == 0) {
            return expanded == expected;
        }
        if (flags & 1) {
            ++in;
            --size;
            ++expanded;
        } else {
            if (size < 2) {
                return false; // the original reads on past the end here
            }
            expanded += (in[1] & kMatchLengthMask) + 3;
            in += 2;
            size -= 2;
        }
        if (expanded > expected) {
            return false;
        }
    }
}

void free_queued_msg(PendingMsg* msg)
{
    if (--msg->share_count() == 0) {
        destroy_queued_msg(msg);
        memory::Hub::dealloc(msg);
    }
}

// A normal or important message into the packet: its id, its length (unless the id has none), its data.
bool append_message(AppHost* app, Conn* conn, PendingMsg* msg)
{
    const uint8_t flags = message_flags(app->supervisor(), msg->ident());
    auto size = static_cast<uint16_t>(msg->byte_count() + ((~flags & kMsgNoLength) ? 3 : 1));
    if (size + static_cast<int>(conn->outgoing().used) >= static_cast<int>(kMaxPacketSize)) {
        return false;
    }
    util::ByteWriter writer(vacant(conn->outgoing()).first(packet_space_left(conn)));
    writer.u8(core::to_underlying(msg->ident()));
    if (!(message_flags(app->supervisor(), msg->ident()) & kMsgNoLength)) {
        writer.u16(msg->byte_count());
    }
    writer.bytes(msg->content(), msg->byte_count());
    conn->outgoing().used += writer.position();
    add_message_metric(&conn->send_metrics(), msg->ident(), msg->byte_count());
    return true;
}

// A sequenced message into the packet, wrapped in a message of id 7: group, sequence number, id, data.
bool append_sequenced_message(Conn* conn, OrderedMsgNode* link)
{
    const SeqQueuedMsg* msg = link->shared_msg();
    const uint16_t length = msg->byte_count();
    auto size = static_cast<uint16_t>(length + kSequencedHeaderBytes);
    if (size + static_cast<int>(conn->outgoing().used) >= static_cast<int>(kMaxPacketSize)) {
        return false;
    }
    util::ByteWriter writer(vacant(conn->outgoing()).first(packet_space_left(conn)));
    writer.u8(core::to_underlying(wire::MessageId::kSequenced));
    writer.u16(static_cast<uint16_t>(length + wire::kSequencedHeaderSize));
    wire::write_sequenced_header(writer, {.group = link->bundle_id(), .sequence = link->order_index(), .id = msg->ident()});
    writer.bytes(msg->content(), msg->byte_count());
    conn->outgoing().used += writer.position();
    add_message_metric(&conn->send_metrics(), msg->ident(), msg->byte_count());
    return true;
}

// The messages a stream became, dispatched; the stream is deleted unless a handler keeps it.
int dispatch_stream(InboundMsgCtx* context, BulkStreamNode* link, StreamDesc* desc, bool check_checksum)
{
    InboundMsgCtx whole;
    construct_context(&whole);
    whole.set_receiver_app(context->receiver_app());
    whole.set_sender_conn(context->sender_conn());
    whole.set_type_code(desc->msg_id());
    whole.set_body_size(desc->extent());
    whole.wire_options() = 0;
    whole.set_body(static_cast<uint8_t*>(memory::alloc(desc->extent())));
    std::memcpy(whole.body(), desc->content(), desc->extent());
    if (check_checksum && checksums::generate_crc_from_buffer(whole.body(), desc->extent()) != desc->checksum()) {
        whole.wire_options() |= kPacketChecksumMismatch;
    }
    const int result = get_dispatcher(context->receiver_app())->dispatch_msg(&whole);
    if (result != kHandlerKeep && result != kResultMsgFreed) {
        if (StreamDesc* stream = link->msg_meta()) {
            destroy_stream_desc(stream);
            memory::Hub::dealloc(stream);
        }
        link->teardown(1);
    }
    memory::Hub::dealloc(whole.body());
    memory::Hub::get_pull()->leave_heap_scope();
    return result;
}

} // namespace

// 0x004598C0
bool AppHost::bundle_messages(Conn* conn, int queue, bool resends_only)
{
    bool fits = true;
    switch (queue) {
    case kQueueNormal: {
        lists::Hunt<MsgNormalLink> search;
        MsgNormalLink* next = nullptr;
        for (MsgNormalLink* link = search.head_element(&conn->normal_msgs()); link; link = next) {
            next = search.following_item();
            if (get_timestamp(this) < link->due_time()) {
                continue;
            }
            if (net::over_bandwidth_budget(this, conn)) {
                return fits;
            }
            PendingMsg* msg = link->shared_msg();
            if (!append_message(this, conn, msg)) {
                fits = false;
                continue;
            }
            free_queued_msg(msg);
            link->teardown(1);
        }
        break;
    }
    case kLanePriority: {
        lists::Hunt<ReliableMsgNode> search;
        for (ReliableMsgNode* link = search.head_element(&conn->important_msgs()); link;
             link = search.following_item()) {
            if (get_timestamp(this) < link->due_time()) {
                continue;
            }
            if (net::over_bandwidth_budget(this, conn)) {
                return fits;
            }
            // never sent (unless only resends are wanted), or not acked within the resend threshold
            if (link->stamp_ms() == 0) {
                if (resends_only) {
                    continue;
                }
            } else {
                const uint32_t threshold = get_retry_delay(conn);
                if (get_timestamp(this) - link->stamp_ms() <= threshold) {
                    continue;
                }
            }
            if (!append_message(this, conn, link->shared_msg())) {
                fits = false;
                continue;
            }
            if (link->stamp_ms() != 0) {
                ++link->retry_count();
            }
            link->set_stamp_ms(get_timestamp(this));
            link->set_packet_clock(conn->timestamp_out());
        }
        break;
    }
    case kLaneOrdered: {
        lists::Hunt<OrderedMsgNode> search;
        for (OrderedMsgNode* link = search.head_element(&conn->sequenced_msgs()); link; link = search.following_item()) {
            if (get_timestamp(this) < link->due_time()) {
                continue;
            }
            if (net::over_bandwidth_budget(this, conn)) {
                return fits;
            }
            // never sent (unless only resends are wanted), or not acked within the resend threshold
            if (link->stamp_ms() == 0) {
                if (resends_only) {
                    continue;
                }
            } else {
                const uint32_t threshold = get_retry_delay(conn);
                if (get_timestamp(this) - link->stamp_ms() <= threshold) {
                    continue;
                }
            }
            if (!append_sequenced_message(conn, link)) {
                fits = false;
                continue;
            }
            if (link->stamp_ms() != 0) {
                ++link->retry_count();
            }
            link->set_stamp_ms(get_timestamp(this));
            link->set_packet_clock(conn->timestamp_out());
        }
        break;
    }
    default:
        break;
    }
    return fits;
}

// 0x0045AB60
int AppHost::on_ping_reply_msg(InboundMsgCtx* context)
{
    Conn* conn = context->sender_conn();
    util::ByteReader reader = body_reader(context);
    const uint32_t sent_time = reader.u32();
    // (NOT ORIGINAL: a reply too short for the time is ignored)
    if (reader.ok() && !net::is_host_local(conn)) {
        const uint32_t now = get_timestamp(context->receiver_app());
        if (sent_time == conn->ping_probe().probe_sent_at()) {
            conn->ping_probe().set_echo_seen_at(sent_time);
            uint32_t latency = now - sent_time;
            latency = std::min(latency, max_latency());
            add_latency(conn, latency);
        }
    }
    return kResultPassOn;
}

// 0x0045AD60
int AppHost::on_ping_probe_msg(InboundMsgCtx* context)
{
    NetMessageInfo desc;
    construct_msg_desc(&desc);
    util::ByteReader reader = body_reader(context);
    uint32_t sent_time = reader.u32();
    if (!reader.ok()) { // NOT ORIGINAL: a probe too short for the time is not answered
        return kResultConsumed;
    }
    desc.set_content(&sent_time);
    desc.set_byte_count(sizeof(sent_time));
    desc.set_ident(wire::MessageId::kLatencyResponse);
    net::put_on_outbox(context->receiver_app(), context->sender_conn()->hdl(), &desc);
    return kResultPassOn;
}

// 0x0045ABF0
int AppHost::handle_compressed_messages(InboundMsgCtx* context)
{
    InboundMsgCtx inner;
    construct_context(&inner);
    // NOT ORIGINAL: a message too short for its header, or expanding to less than a message's id and length, is
    // dropped (the original reads past them).
    util::ByteReader reader = body_reader(context);
    wire::CompressedHeader header;
    if (!wire::read_compressed_header(reader, &header)) {
        return kResultConsumed;
    }
    int size = header.size;
    const uint8_t method = header.method;
    if (size < ints::as_signed(wire::kMessageHeaderSize)) {
        return kResultConsumed;
    }
    const std::span<const uint8_t> stream = reader.rest();
    auto* data = static_cast<uint8_t*>(memory::alloc(size));
    const int expected_size = size;
    if (method == 1) {
        if (!rle_stream_fits(stream) ||
            rle_decompress(stream, std::span<uint8_t>(data, static_cast<size_t>(size)), &size) != 0 ||
            expected_size != size) {
            memory::Hub::dealloc(data);
            return kResultConsumed;
        }
    } else if (method == 2) {
        if (!lzss_stream_fits(stream, expected_size)) {
            memory::Hub::dealloc(data);
            return kResultConsumed;
        }
        // (bounded by the header's size: the stream fits it, checked above)
        size_t unpacked = 0;
        const bool whole = lzss::unpack(stream.data(), stream.size(), data,
                                        static_cast<size_t>(expected_size), &unpacked);
        size = static_cast<int>(unpacked);
        if (!whole || expected_size != size) {
            memory::Hub::dealloc(data);
            return kResultConsumed;
        }
    } else if (method == lzham::kMethod) {
        // NOT ORIGINAL: THUGPRO's LZHAM (TP-R11), expanded into the header's size and no further.
        size_t expanded = 0;
        if (!lzham::decompress(stream.data(), stream.size(), data,
                               static_cast<size_t>(expected_size), &expanded) ||
            expanded != ints::as_unsigned(expected_size)) {
            memory::Hub::dealloc(data);
            return kResultConsumed;
        }
    } else {
        // (The original uses the buffer as it is, not expanded - uninitialised memory dispatched. NOT ORIGINAL: dropped.)
        memory::Hub::dealloc(data);
        return kResultConsumed;
    }

    // The expanded data is one message: id, length, data.
    inner.set_sender_conn(context->sender_conn());
    util::ByteReader expanded(data, static_cast<size_t>(expected_size));
    wire::MessageHeader inner_header;
    wire::read_message_header(expanded, &inner_header);
    const auto id = inner_header.id;
    const uint16_t length = inner_header.length;
    if (length > expanded.remaining()) { // NOT ORIGINAL: a length past the expanded data (the original copies on)
        memory::Hub::dealloc(data);
        return kResultConsumed;
    }
    auto* copy = static_cast<uint8_t*>(memory::alloc(length));
    expanded.bytes(copy, length);
    inner.set_body(copy);
    inner.set_receiver_app(context->receiver_app());
    inner.wire_options() = context->wire_options();
    inner.set_type_code(id);
    inner.set_body_size(length);
    const int result = get_dispatcher(context->receiver_app())->dispatch_msg(&inner);
    memory::Hub::dealloc(inner.body());
    memory::Hub::dealloc(data);
    return result;
}

// 0x0045AF10
int AppHost::on_ordered_msgs(InboundMsgCtx* context)
{
    const Conn* conn = context->sender_conn();
    context->set_acknowledge(true);
    util::ByteReader reader = body_reader(context);
    wire::SequencedHeader header;
    if (!wire::read_sequenced_header(reader, &header)) { // NOT ORIGINAL: too short for its header - dropped
        return kResultConsumed;
    }
    const uint32_t sequence = header.sequence;
    const auto group = header.group;
    if (sequence < core::at(conn->sequence_numbers(), core::to_underlying(group))) {
        return kResultPassOn; // handled already
    }

    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->network_heap());
    const auto id = header.id;
    auto length = static_cast<uint16_t>(reader.remaining());
    void* memory = memory::alloc(sizeof(SeqQueuedMsg));
    SeqQueuedMsg* seq_msg =
        memory ? construct_seq_msg(memory, id, length, reader.rest().data(),
                                   group, false)
               : nullptr;
    void* link_memory = memory::alloc_zeroed(sizeof(OrderedMsgNode));
    OrderedMsgNode* link = link_memory ? construct_msg_seq_link(link_memory, seq_msg) : nullptr;
    // (a failed allocation crashes here, as in the original)
    link->list_link().set_priority(static_cast<int>(sequence));
    link->set_stamp_ms(0);
    link->set_packet_clock(0);
    link->set_bundle_id(group);
    link->set_order_index(sequence);
    memory::Hub::get_pull()->leave_heap_scope();

    if (!lists::insert_by_unique_order(core::at(context->sender_conn()->received_queues(), group), &link->list_link())) { // a duplicate
        if (SeqQueuedMsg* duplicate = link->shared_msg()) {
            destroy_queued_msg(duplicate);
            memory::Hub::dealloc(duplicate);
        }
        link->teardown(1);
        return kResultConsumed;
    }
    return kResultPassOn;
}

// 0x0045B0B0
int AppHost::on_receipt_msg(InboundMsgCtx* context)
{
    // (a message without a length field: its body_size_ is the previous message's, and the body is a fixed-size copy
    // of the packet from the stamp on)
    util::ByteReader reader(context->body(), sizeof(uint16_t));
    const uint16_t packetstamp = reader.u16();
    uint32_t sent_time = 0;
    context->set_body_size(sizeof(uint16_t));
    if (context->wire_options() & kPacketForeign) {
        return kResultConsumed;
    }

    {
        lists::Hunt<ReliableMsgNode> search;
        ReliableMsgNode* next = nullptr;
        for (ReliableMsgNode* link = search.head_element(&context->sender_conn()->important_msgs()); link; link = next) {
            next = search.following_item();
            if (packetstamp == static_cast<uint16_t>(link->packet_clock()) && link->stamp_ms()) {
                free_queued_msg(link->shared_msg());
                sent_time = link->stamp_ms();
                link->teardown(1);
            }
        }
    }
    {
        lists::Hunt<OrderedMsgNode> search;
        OrderedMsgNode* next = nullptr;
        for (OrderedMsgNode* link = search.head_element(&context->sender_conn()->sequenced_msgs()); link; link = next) {
            next = search.following_item();
            if (packetstamp == static_cast<uint16_t>(link->packet_clock()) && link->stamp_ms()) {
                SeqQueuedMsg* msg = link->shared_msg();
                if (--msg->share_count() == 0) {
                    destroy_queued_msg(msg);
                    memory::Hub::dealloc(msg);
                }
                sent_time = link->stamp_ms();
                link->teardown(1);
            }
        }
    }

    Conn* conn = context->sender_conn();
    if (is_off_machine(conn) && (conn->cur_standing() & kConnStatus1) && sent_time) {
        uint32_t latency = get_timestamp(context->receiver_app()) - sent_time;
        const uint32_t current =
            net::is_host_local(conn) ? 0 : static_cast<uint32_t>(conn->ping_probe().average_latency());
        if (latency > current) {
            latency = std::min(latency, max_latency());
            add_latency(context->sender_conn(), latency);
        }
    }
    return kResultPassOn;
}

// 0x0045B2F0
int AppHost::drain_stream_msgs(InboundMsgCtx* context)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->network_heap());
    switch (context->type_code()) {
    case wire::MessageId::kBulkChunk: {
        // stream id (4 bytes), then the piece (net_wire.h)
        util::ByteReader reader = body_reader(context);
        const int stream_id = reader.i32();
        if (!reader.ok()) { // NOT ORIGINAL: too short for the stream id - dropped
            break;
        }
        lists::Hunt<BulkStreamNode> search;
        for (BulkStreamNode* link = search.head_element(&context->sender_conn()->received_streams()); link;
             link = search.following_item()) {
            StreamDesc* desc = link->msg_meta();
            if (desc->stream_id() != stream_id) {
                continue;
            }
            // NOT ORIGINAL: a piece past the stream's size is cut there (the original copied it all, past the end
            // of the stream's buffer)
            size_t size = reader.remaining();
            const size_t room = static_cast<size_t>(desc->extent()) - static_cast<size_t>(desc->succ() - desc->content());
            size = std::min(size, room);
            reader.bytes(desc->succ(), size);
            desc->set_succ(std::next(desc->succ(), static_cast<std::ptrdiff_t>(size)));
            if (static_cast<uint32_t>(desc->succ() - desc->content()) >= static_cast<uint32_t>(desc->extent())) {
                return dispatch_stream(context, link, desc, true);
            }
        }
        break;
    }
    case wire::MessageId::kBulkBegin: {
        // name (0x20 bytes), size, stream id, checksum, message id, then the first piece (net_wire.h)
        util::ByteReader reader = body_reader(context);
        wire::StreamStart start;
        // NOT ORIGINAL: a start too short for its fields, or of a negative size, is dropped
        if (!wire::read_stream_start(reader, &start) || start.total_size < 0) {
            memory::Hub::get_pull()->leave_heap_scope();
            return kResultConsumed;
        }
        const int stream_id = start.stream_id;
        lists::Hunt<BulkStreamNode> search;
        for (const BulkStreamNode* link = search.head_element(&context->sender_conn()->received_streams()); link;
             link = search.following_item()) {
            if (link->msg_meta()->stream_id() == stream_id) { // started already
                memory::Hub::get_pull()->leave_heap_scope();
                return kResultConsumed;
            }
        }
        void* memory = memory::alloc(sizeof(StreamDesc));
        StreamDesc* desc = memory ? construct_stream_desc(memory) : nullptr;
        // (the original copied the description with no limit: 32 bytes and a terminator here, the field's size)
        static_assert(sizeof(desc->desc()) == wire::kStreamDescSize);
        Expects(desc != nullptr);
        std::memcpy(desc->desc().data(), start.desc.data(), sizeof(desc->desc()));
        core::at(desc->desc(), sizeof(desc->desc()) - 1) = '\0';
        desc->set_extent(start.total_size);
        desc->set_stream_id(start.stream_id);
        desc->set_checksum(start.checksum);
        desc->set_msg_id(start.msg_id);
        desc->set_content(static_cast<uint8_t*>(memory::alloc(static_cast<uint32_t>(start.total_size))));
        desc->set_succ(desc->content());
        auto size = static_cast<size_t>(start.total_size);
        size = std::min(size, kMaxStreamStartData);
        // NOT ORIGINAL: no more than the message holds (the original copied on past its end)
        size = std::min(size, reader.remaining());
        reader.bytes(desc->content(), size);
        desc->set_succ(std::next(desc->succ(), static_cast<std::ptrdiff_t>(size)));
        void* link_memory = memory::alloc_zeroed(sizeof(BulkStreamNode));
        BulkStreamNode* link = link_memory ? construct_stream_link(link_memory, desc) : nullptr;
        // at the end of the connection's streams
        memory::ListNode* head = &context->sender_conn()->received_streams();
        link->list_link().set_next(head);
        link->list_link().set_prev(head->prev());
        head->prev()->set_next(&link->list_link());
        head->set_prev(&link->list_link());
        if (static_cast<uint32_t>(desc->succ() - desc->content()) >=
            static_cast<uint32_t>(desc->extent())) {            // all in one message
            return dispatch_stream(context, link, desc, false); // (no checksum check on this path)
        }
        break;
    }
    default:
        break;
    }
    memory::Hub::get_pull()->leave_heap_scope();
    return kResultPassOn;
}

} // namespace net
