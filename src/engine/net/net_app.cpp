// NetLink::AppHost (the network library's base of the Server and the Client): queueing and streaming messages, the
// connection list and the app's tasks.

#include "engine/net/net_app.h"
#include "wire/message_ids.h"
#include "engine/core/enum_ops.h"
#include "engine/net/net_lists.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/lists/chain_start.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_manager.h"
#include "engine/net/net_metrics.h"
#include "engine/net/net_messages.h"
#include "wire/net_wire.h"
#include "formats/byte_stream.h"
#include "platform/stopwatch.h"
#include <gsl/pointers>
#include <array>
#include <gsl/assert>
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/span_copy.h"
#include "engine/core/format.h"

namespace net {

namespace {
constexpr unsigned kQueuedMsgBytes = 0x0C;
constexpr int kMaxConnHandles = 0x100;
constexpr int kDefaultMaxQueued = 5;
constexpr unsigned kConnBytes = 0x3D20;

using memory::ListNode;

// Message targets (not AppHost::kBroadcast, a flag).
constexpr int kHandleBroadcast = 0xFF;        // every connection
constexpr int kHandleExcludeBroadcast = 0x80; // every connection but handle & 0x7F
constexpr int kStreamPieceSize = static_cast<int>(wire::kStreamPieceSize);

constexpr int kMetricsTaskPriority = -2500;
constexpr int kReceiveTaskPriority = 1000;
constexpr int kProcessTaskPriority = 0;

// --- Conn (net_conn.h): short names for the fields used here ------------------------------------------------
// The outgoing message queues (lists::Heads of links, highest priority first).
ListNode* normal_queue(Conn* conn) { return &conn->normal_msgs(); }
ListNode* important_queue(Conn* conn) { return &conn->important_msgs(); }
ListNode* sequenced_queue(Conn* conn) { return &conn->sequenced_msgs(); }
// The receive buffer's read and write positions (its start and end).
// Status: 1 ready, bit 2 invalid, bit 3 disconnecting.
uint32_t& get_standing(Conn* conn) { return conn->cur_standing(); }
constexpr uint32_t kConnReady = Conn::kConnReady;
constexpr uint32_t kConnClosing = 8;
AppHost*& owner_app(Conn* conn) { return conn->owner_app(); }
// The next sequence id of each message group.
uint32_t& next_sequence_id(Conn* conn, wire::MessageGroup group) { return core::at(conn->next_sequence_ids(), core::to_underlying(group)); }
int& alloc_stream_key(Conn* conn) { return conn->stream_counter(); }
ListNode* get_streams(Conn* conn) { return &conn->send_streams(); } // lists::Foremost<BulkStreamNode>
void* in_metrics(Conn* conn) { return &conn->receive_metrics(); }   // NetLink::Gauges (0x100C bytes)
void* out_metrics(Conn* conn) { return &conn->send_metrics(); }

// Conn's methods (net_conn.cpp).
gsl::owner<Conn*> construct_conn(void* memory, int flags) { return new (memory) Conn(flags); }
void destroy_conn_body(Conn* conn) { conn->~Conn(); }
bool is_sound(Conn* conn) { return conn->is_sound(); }
void mark_stale(Conn* conn) { conn->mark_stale(); }
bool is_local_conn(Conn* conn) { return conn->is_host_local(); }
bool is_off_machine(Conn* conn) { return conn->is_off_machine(); }
bool is_outsider(Conn* conn) { return conn->is_outsider(); }
int link_speed_class(Conn* conn) { return conn->get_link_speed_class(); }
int bandwidth_limit(Conn* conn) { return conn->fetch_throughput(); }
void change_send_now(Conn* conn, bool force) { conn->force_send_now(force); }
// The sequenced links that carry streamed pieces.
int num_stream_pieces_queued(Conn* conn) { return conn->get_num_queued_stream_pieces(); }

// NetLink::Gauges: bytes over the last second.
int get_bytes_per_second(void* metrics) { return static_cast<Gauges*>(metrics)->get_throughput(); }
void update_metrics(void* metrics, uint32_t time)
{
    static_cast<Gauges*>(metrics)->calculate_usage(static_cast<int>(time));
}
// 1 = a modem: a slow connection.
int connection_type(void* manager) { return static_cast<Hub*>(manager)->get_net_link_kind(); }

// --- Messages ---------------------------------------------------------------------------------------------------
// PendingMsg (0x0C) and SeqQueuedMsg (0x10, made by AppHost::new_queued_msg): a copy of a message shared by the
// links that send it to each connection; freed with the last.
gsl::owner<PendingMsg*> construct_queued_msg(void* memory, wire::MessageId id, uint16_t length, const void* data, uint8_t compression)
{
    return new (memory) PendingMsg(id, length, data, compression);
}
// ~PendingMsg / ~SeqQueuedMsg: frees the data.
void destroy_queued_msg_body(void* message) { static_cast<PendingMsg*>(message)->~PendingMsg(); }

// The links (net_messages.h): list nodes (+8 the priority) pointing at a queued message - the normal queue's (with
// the time it may be sent), the important queue's (also when it was last sent, 0 = never) and the sequenced queue's
// (also the group, whether it's a stream piece and the sequence id).
gsl::owner<MsgNormalLink*> construct_msg_link(void* memory, PendingMsg* message) { return new (memory) MsgNormalLink(message); }
gsl::owner<ReliableMsgNode*> construct_msg_important_link(void* memory, PendingMsg* message)
{
    return new (memory) ReliableMsgNode(message);
}
gsl::owner<OrderedMsgNode*> construct_msg_seq_link(void* memory, SeqQueuedMsg* message)
{
    return new (memory) OrderedMsgNode(message);
}

// BulkStreamNode: a lists::Elem (data = itself) with its StreamDesc.
gsl::owner<BulkStreamNode*> construct_stream_link(void* memory, StreamDesc* stream) { return new (memory) BulkStreamNode(stream); }
StreamDesc* stream_of(const ListNode* link) { return static_cast<BulkStreamNode*>(link->data())->msg_meta(); }
gsl::owner<StreamDesc*> construct_stream_desc(void* memory) { return new (memory) StreamDesc; }
// ~StreamDesc: frees the data unless it was sent in place ("Destroying StreamDesc's Data").
void destroy_stream_desc_body(StreamDesc* stream) { stream->~StreamDesc(); }

// strcpy: the original copies descriptions with no length limit (32 bytes are kept for them).
void copy_string(std::span<char> out, const char* source)
{
    size_t at = 0;
    for (const char c : std::string_view(source)) {
        core::at(out, at++) = c;
    }
    core::at(out, at) = '\0';
}

void init_msg_desc(NetMessageInfo* desc) { *desc = NetMessageInfo(); }

// The tail of a list.
void append_last(ListNode* head, ListNode* node)
{
    node->set_next(head);
    node->set_prev(head->prev());
    head->prev()->set_next(node);
    head->set_prev(node);
}
// Out of its list.
void drop_out(ListNode* node)
{
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
}

// The memory manager.
memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
void push_network_heap() { ram_stats()->enter_heap_scope(ram_stats()->network_heap()); }
void pop_heap() { ram_stats()->leave_heap_scope(); }

// The pieces of a streamed message (net_wire.h): the start (the description, the total size, the stream's id, a
// checksum of all the data, the message id and the first 256 bytes) and the pieces after it, each built in a buffer
// of its own.
struct StreamPieceMessage {
    // The fields other code reads and sets.
    std::array<uint8_t, wire::kStreamPieceMaxSize>& bytes() { return bytes_; }
    [[nodiscard]] const std::array<uint8_t, wire::kStreamPieceMaxSize>& bytes() const { return bytes_; }
    [[nodiscard]] uint16_t size() const { return size_; }

private:
    std::array<uint8_t, wire::kStreamPieceMaxSize> bytes_{};
    uint16_t size_ = 0;

public:
    void build(int stream_id, const void* piece, int length)
    {
        util::ByteWriter writer(bytes_.data(), sizeof(bytes_));
        wire::write_stream_piece(writer, stream_id, piece, static_cast<size_t>(length));
        size_ = static_cast<uint16_t>(writer.position());
    }
};

// --- The rest of the library ------------------------------------------------------------------------------
// A message link into a queue: lists::Foremost::append_node (0x00457350).
void add_link(ListNode* queue, ListNode* link) { lists::append_node(*queue, link); }
// conn's received messages to their handlers (Router::invoke_handlers_for_msg); 2 = halted, 3 = stop
int dispatch_messages(HandlerLists* dispatcher, Conn* conn, int flags)
{
    return core::down_cast<Router>(dispatcher)->invoke_handlers_for_msg(conn, static_cast<uint32_t>(flags));
}

// A task of the app's (null if out of memory).
gsl::owner<tasks::SchedulerTask*> new_task(int priority, tasks::Task<AppHost>::Code callback, AppHost* app)
{
    return new tasks::Task<AppHost>(callback, app, priority);
}
AppHost* task_app(tasks::Task<AppHost>* task) { return task->content(); }

} // namespace

// 0x004594C0
AppHost::AppHost(int flags) : dispatcher_app_(this), flag_bits_(static_cast<uint32_t>(flags))
{
    for (memory::ListNode& handlers : dispatcher_.lists()) {
        handlers.make_head(); // (0x00457010)
    }
    list1414_.make_node(this);
    circuits_.make_head();
    banned_.make_head();
    metrics_task_ = memory::take_unique<tasks::SchedulerTask>(new_task(kMetricsTaskPriority, &AppHost::network_metrics, this));
    receive_task_ = memory::take_unique<tasks::SchedulerTask>(new_task(kReceiveTaskPriority, &AppHost::receive_network_data, this));
    process_task_ = memory::take_unique<tasks::SchedulerTask>(new_task(kProcessTaskPriority, &AppHost::pump_incoming_traffic, this));
    core::at(unknown1f00_, 0) = core::at(unknown1f00_, 1) = 0; // the double 0.0
}

namespace {
EnqueueFilter& enqueue_filter()
{
    static EnqueueFilter enqueue_filter{nullptr}; // NOT ORIGINAL - set_enqueue_filter's
    return enqueue_filter;
}
}

void set_enqueue_filter(EnqueueFilter filter) noexcept { enqueue_filter() = filter; }

// 0x00457EB0
void AppHost::put_on_outbox(int handle, NetMessageInfo* desc)
{
    // (NOT ORIGINAL: the filter - set_enqueue_filter)
    if (enqueue_filter() && enqueue_filter()(this, handle, desc)) {
        return;
    }
    push_network_heap();
    // Every connection that takes it (`accepts`), or every one but handle & 0x7F, or the handle's.
    auto for_each_target = [&](auto accepts, auto queue) {
        if (handle == kHandleBroadcast) {
            lists::Hunt<Conn> search;
            for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
                if (accepts(conn)) {
                    queue(conn);
                }
            }
        } else if (handle & kHandleExcludeBroadcast) {
            const int excluded = handle & ~kHandleExcludeBroadcast;
            lists::Hunt<Conn> search;
            for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
                if (conn->hdl() != excluded && accepts(conn)) {
                    queue(conn);
                }
            }
        } else {
            Conn* conn = find_conn_with_id(handle); // (not checked for null)
            if (accepts(conn)) {
                queue(conn);
            }
        }
    };
    auto not_disconnecting = [](Conn* conn) { return (get_standing(conn) & kConnClosing) == 0; };

    switch (desc->queue()) {
    case NetMessageInfo::kLaneNormal: {
        void* memory = memory::alloc(kQueuedMsgBytes);
        PendingMsg* message =
            memory ? construct_queued_msg(memory, desc->ident(), desc->byte_count(), desc->content(), desc->compression())
                   : nullptr;
        // To ready connections only (to a single connection, also message 9).
        auto ready = [&](Conn* conn) {
            return get_standing(conn) == kConnReady ||
                   handle != kHandleBroadcast && !(handle & kHandleExcludeBroadcast) && desc->ident() == wire::MessageId::kClockStamp;
        };
        for_each_target(ready, [&](Conn* conn) {
            if (desc->singular()) {
                free_messages(conn, {.queue = desc->queue(), .id = desc->ident()});
            }
            ++message->share_count();
            void* link_memory = memory::alloc_zeroed(sizeof(MsgNormalLink));
            MsgNormalLink* link = link_memory ? construct_msg_link(link_memory, message) : nullptr;
            Expects(link != nullptr);
            link->list_link().set_priority(desc->rank());
            link->set_due_time(desc->delay() + clock_);
            add_link(normal_queue(conn), &link->list_link());
        });
        if (message->share_count() == 0) {
            destroy_queued_msg_body(message);
            memory::Hub::dealloc(message);
        }
        break;
    }
    case NetMessageInfo::kLanePriority: {
        void* memory = memory::alloc(kQueuedMsgBytes);
        PendingMsg* message =
            memory ? construct_queued_msg(memory, desc->ident(), desc->byte_count(), desc->content(), desc->compression())
                   : nullptr;
        for_each_target(not_disconnecting, [&](Conn* conn) {
            if (desc->singular()) {
                free_messages(conn, {.queue = desc->queue(), .id = desc->ident()});
            }
            ++message->share_count();
            void* link_memory = memory::alloc_zeroed(sizeof(ReliableMsgNode));
            ReliableMsgNode* link = link_memory ? construct_msg_important_link(link_memory, message) : nullptr;
            Expects(link != nullptr);
            link->list_link().set_priority(desc->rank());
            link->set_due_time(desc->delay() + clock_);
            link->set_stamp_ms(0);
            add_link(important_queue(conn), &link->list_link());
        });
        if (message->share_count() == 0) {
            destroy_queued_msg_body(message);
            memory::Hub::dealloc(message);
        }
        break;
    }
    case NetMessageInfo::kLaneOrdered: {
        gsl::owner<SeqQueuedMsg*> message =
            new_queued_msg(desc->ident(), desc->byte_count(), desc->content(), desc->group_id(), desc->compression());
        for_each_target(not_disconnecting, [&](Conn* conn) {
            ++message->share_count();
            void* link_memory = memory::alloc_zeroed(sizeof(OrderedMsgNode));
            OrderedMsgNode* link = link_memory ? construct_msg_seq_link(link_memory, message) : nullptr;
            // The original's broadcast gives the links the queue number (2) as their priority instead.
            Expects(link != nullptr);
            link->list_link().set_priority(handle == kHandleBroadcast ? desc->queue() : desc->rank());
            link->set_stamp_ms(0);
            link->set_due_time(desc->delay() + clock_);
            link->set_bundle_id(desc->group_id());
            if (desc->fixed_seq_no()) {
                link->set_order_index(desc->fixed_seq_no());
            } else {
                link->set_order_index(next_sequence_id(conn, desc->group_id())++);
            }
            link->set_bulk_msg(desc->feed());
            add_link(sequenced_queue(conn), &link->list_link());
        });
        if (message->share_count() == 0) {
            destroy_queued_msg_body(message);
            memory::Hub::dealloc(message);
        }
        break;
    }
    default:
        break;
    }
    pop_heap();
}

// 0x00458740
void AppHost::send_bulk_to_conn(Conn* conn, BulkHead head, void* data, const char* desc,
                                wire::MessageGroup group_id, bool queue_all_pieces, bool borrow_caller_buffer)
{
    const auto msg_id = head.msg_id_;
    const auto size = head.size;
    // The original leaves the start message uninitialised on the stack (the 3 bytes sent past the first piece);
    // they're zeros here. (NetMessageInfo's constructor sets every field.)
    NetMessageInfo message = {};
    init_msg_desc(&message);
    wire::StreamStart start;
    start.total_size = size;
    start.stream_id = alloc_stream_key(conn);
    start.msg_id = msg_id;
    start.checksum = checksums::generate_crc_from_buffer(data, size);
    text::format_to(start.desc, "%s", desc);
    const int first = size > kStreamPieceSize ? kStreamPieceSize : size;
    std::array<uint8_t, wire::kStreamStartMaxSize> start_bytes{};
    util::ByteWriter start_writer(start_bytes.data(), sizeof(start_bytes));
    wire::write_stream_start(start_writer, start, data, static_cast<size_t>(first));
    message.set_byte_count(static_cast<uint16_t>(start_writer.position()));
    message.set_content(start_bytes.data());
    message.set_group_id(group_id);
    message.set_ident(wire::MessageId::kBulkBegin);
    message.set_queue(NetMessageInfo::kLaneOrdered);
    message.set_feed(1);
    put_on_outbox(conn->hdl(), &message);

    if (size > kStreamPieceSize) {
        if (queue_all_pieces) {
            // Every piece now.
            const std::span<const uint8_t> bytes(static_cast<const uint8_t*>(data), static_cast<size_t>(size));
            StreamPieceMessage piece;
            for (int sent = first; std::cmp_less(sent, size);) {
                int length = size - sent;
                length = std::min(length, kStreamPieceSize);
                piece.build(alloc_stream_key(conn), bytes.subspan(static_cast<size_t>(sent)).data(), length);
                message.set_content(piece.bytes().data());
                message.set_byte_count(piece.size());
                message.set_group_id(group_id);
                message.set_ident(wire::MessageId::kBulkChunk);
                message.set_queue(NetMessageInfo::kLaneOrdered);
                message.set_feed(1);
                put_on_outbox(conn->hdl(), &message);
                sent += length;
            }
        } else {
            // The rest goes a few pieces a frame (pump_stream_traffic), with sequence ids reserved now.
            push_network_heap();
            const int later_pieces = (size - 1) / kStreamPieceSize;
            uint8_t* copy = nullptr;
            if (borrow_caller_buffer) {
                copy = static_cast<uint8_t*>(data);
            } else {
                copy = static_cast<uint8_t*>(memory::alloc(size));
                std::memcpy(copy, data, size);
            }
            uint8_t* next = std::span<uint8_t>(copy, static_cast<size_t>(size)).subspan(static_cast<size_t>(first)).data();
            void* memory = memory::alloc(sizeof(StreamDesc));
            StreamDesc* stream = memory ? construct_stream_desc(memory) : nullptr;
            void* link_memory = memory::alloc_zeroed(sizeof(BulkStreamNode));
            BulkStreamNode* link = link_memory ? construct_stream_link(link_memory, stream) : nullptr;
            Expects(stream != nullptr);
            stream->set_extent(size);
            stream->set_stream_id(alloc_stream_key(conn));
            stream->set_msg_id(msg_id);
            copy_string(stream->desc(), desc);
            stream->set_content(copy);
            stream->set_group_id(group_id);
            stream->set_succ(next);
            stream->order_number() = next_sequence_id(conn, group_id);
            stream->set_borrowed_payload(borrow_caller_buffer);
            append_last(get_streams(conn), &link->list_link());
            next_sequence_id(conn, group_id) += later_pieces;
            pop_heap();
        }
    }
    ++alloc_stream_key(conn);
}

// 0x00458AB0
void AppHost::send_bulk_message(int handle, AppHost::BulkHead head, void* data, const char* desc,
                                wire::MessageGroup group_id, bool queue_all_pieces, bool borrow_caller_buffer)
{
    const auto msg_id = head.msg_id_;
    const auto size = head.size;
    if (handle == kHandleBroadcast) {
        lists::Hunt<Conn> search;
        for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
            if (is_sound(conn)) {
                send_bulk_to_conn(conn, {.msg_id_ = msg_id, .size = size}, data, desc, group_id, queue_all_pieces, borrow_caller_buffer);
            }
        }
    } else {
        Conn* conn = find_conn_with_id(handle); // (not checked for null)
        if (is_sound(conn)) {
            send_bulk_to_conn(conn, {.msg_id_ = msg_id, .size = size}, data, desc, group_id, queue_all_pieces, borrow_caller_buffer);
        }
    }
}

// 0x00458BE0
Conn* AppHost::begin_conn_walk(ConnSearch* search)
{
    search->list_link_ = &circuits_;
    ListNode* next = circuits_.next();
    search->list_link_ = next->data() == memory::ListNode::head_marker() ? nullptr : next;
    return search->list_link_ ? static_cast<Conn*>(search->list_link_->data()) : nullptr;
}

// 0x00458C10
void AppHost::flush_outbox()
{
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
        flush_outbox(conn);
    }
}

// 0x00458CA0
void AppHost::transmit(bool send_in_frame)
{
    if (!send_in_frame) {
        ++clock_;
        lists::Hunt<Conn> search;
        for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
            change_send_now(conn, true);
        }
    }
    flush_outbox();
}

// 0x00458D50
bool AppHost::is_conn_blocked(Conn* conn)
{
    lists::Hunt<BlockedConnEntry> search;
    for (const BlockedConnEntry* banned = search.head_element(&banned_); banned; banned = search.following_item()) {
        if (banned->remote_ip() == conn->remote_ip()) {
            return true;
        }
    }
    return false;
}

// 0x00458DB0
int AppHost::fetch_traffic_spent()
{
    int total = 0;
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
        if (is_off_machine(conn)) {
            total += get_bytes_per_second(out_metrics(conn));
        }
    }
    return total;
}

// 0x00458E60: over the total allowed, over conn's own limit, or over its share of the total (split between the
// remote connections that aren't foreign).
bool AppHost::over_bandwidth_budget(Conn* conn)
{
    if (fetch_traffic_spent() >= supervisor_->fetch_throughput()) {
        return true;
    }
    const int limit = bandwidth_limit(conn);
    if (get_bytes_per_second(out_metrics(conn)) >= limit) {
        return true;
    }
    int sharing = 0;
    lists::Hunt<Conn> search;
    for (Conn* other = search.head_element(&circuits_); other; other = search.following_item()) {
        if (is_off_machine(other) && !is_outsider(other)) {
            ++sharing;
        }
    }
    if (sharing == 0) {
        sharing = 1;
    }
    const int share = supervisor_->fetch_throughput() / sharing;
    return get_bytes_per_second(out_metrics(conn)) > share;
}

// 0x00458F80
int AppHost::fetch_unused_slot_id()
{
    for (int handle = 1; handle < kMaxConnHandles; ++handle) {
        if (!find_conn_with_id(handle)) {
            return handle;
        }
    }
    return 0;
}

// 0x00458FB0
void AppHost::drop_broken_links()
{
    lists::Hunt<Conn> search;
    Conn* conn = search.head_element(&circuits_);
    while (conn) {
        Conn* next = search.following_item();
        if ((get_standing(conn) & kConnClosing) && !messages_due(conn)) {
            mark_stale(conn);
        }
        if (!is_sound(conn) && conn->incoming().used == 0 && !messages_due(conn)) {
            drop_out(&conn->app_node());
            destroy_conn_body(conn);
            memory::Hub::dealloc(conn);
        }
        conn = next;
    }
}

// 0x004590C0: per connection, at most 5 stream pieces queued and 3 more a frame (2 and 1 on a slow connection);
// finished streams are deleted.
void AppHost::pump_stream_traffic()
{
    NetMessageInfo message = {};
    init_msg_desc(&message);
    StreamPieceMessage piece;
    Hub* manager = Hub::sole();
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
        int max_queued = kDefaultMaxQueued;
        int max_per_frame = 3;
        if (connection_type(manager) == 1 || link_speed_class(conn) == 1) {
            max_queued = 2;
            max_per_frame = 1;
        }
        unsigned int streams = 0;
        for (ListNode* node = get_streams(conn)->next(); node->data() != memory::ListNode::head_marker() && node;
             node = node->next()) {
            ++streams;
        }
        if (streams == 0) {
            continue;
        }
        const int queued = num_stream_pieces_queued(conn);
        if (queued >= max_queued) {
            continue;
        }
        int budget = max_queued - queued;
        budget = std::min(budget, max_per_frame);

        lists::Hunt<ListNode> links;
        ListNode* link = links.head_element(get_streams(conn));
        while (link) {
            ListNode* next_link = links.following_item();
            StreamDesc* stream = stream_of(link);
            int sent = static_cast<int>(stream->succ() - stream->content());
            const int size = stream->extent();
            bool unfinished = false;
            while (sent < size) {
                if (budget <= 0) {
                    unfinished = true;
                    break;
                }
                int length = size - sent;
                length = std::min(length, kStreamPieceSize);
                piece.build(stream->stream_id(), stream->succ(), length);
                message.set_ident(wire::MessageId::kBulkChunk);
                message.set_byte_count(piece.size());
                message.set_queue(NetMessageInfo::kLaneOrdered);
                message.set_content(piece.bytes().data());
                message.set_group_id(stream->group_id());
                message.set_feed(1);
                message.set_fixed_seq_no(stream->order_number()++);
                put_on_outbox(conn->hdl(), &message);
                stream->set_succ(std::next(stream->succ(), length));
                sent += length;
                --budget;
            }
            if (!unfinished) {
                destroy_stream_desc_body(stream);
                memory::Hub::dealloc(stream);
                static_cast<BulkStreamNode*>(link->data())->teardown(1);
            }
            link = next_link;
        }
    }
}

// 0x00456CA0
void AppHost::pump_incoming_traffic(tasks::Task<AppHost>* task)
{
    AppHost* app = task_app(task);
    ++app->frame_count_;
    app->handle_incoming();
}

// 0x004593C0
void AppHost::receive_network_data(tasks::Task<AppHost>* task)
{
    AppHost* app = task_app(task);
    app->clock_ = frame_clock::get_time();
    app->drop_broken_links();
    app->pull_inbound();
}

// 0x004593E0
void AppHost::network_metrics(tasks::Task<AppHost>* task)
{
    AppHost* app = task_app(task);
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&app->circuits_); conn; conn = search.following_item()) {
        update_metrics(in_metrics(conn), app->clock_);
        update_metrics(out_metrics(conn), app->clock_);
    }
}

// 0x004594A0
void AppHost::flush_outgoing_traffic(tasks::Task<AppHost>* task)
{
    AppHost* app = task_app(task);
    app->pump_stream_traffic();
    app->transmit(true);
}

// 0x00459690
bool AppHost::handle_incoming()
{
    lists::Hunt<Conn> search;
    Conn* conn = search.head_element(&circuits_);
    while (conn) {
        Conn* next = search.following_item();
        if (is_local_conn(conn) && conn->alias_conn()) {
            Conn* alias = conn->alias_conn();
            // (a local connection takes its alias's - the other end, in this process - send buffer straight into its
            // own receive buffer)
            const size_t length = alias->outgoing().used;
            core::copy_into(std::span<const uint8_t>(filled(alias->outgoing())), vacant(conn->incoming()));
            alias->outgoing().used = 0;
            conn->incoming().used += length;
        }
        const int result = dispatch_messages(&dispatcher_, conn, 0);
        if (result == 2) {
            return true;
        }
        if (result == 3) {
            return false;
        }
        if (!dispatch_queued_sequenced_messages(conn)) {
            return false;
        }
        conn = next;
    }
    return true;
}

// 0x004597C0
Conn* AppHost::opened_link(LinkTarget link_target)
{
    const auto ip = link_target.ip;
    const auto port = link_target.port;
    const auto flags = link_target.flags;
    Conn* conn = nullptr;
    push_network_heap();
    if (open_links_ < peer_cap_) {
        void* memory = memory::alloc(kConnBytes);
        conn = memory ? construct_conn(memory, flags) : nullptr; // (not checked for null)
        Expects(conn != nullptr);
        conn->set_remote_ip(static_cast<uint32_t>(ip));
        conn->set_port_no(port);
        conn->set_hdl(fetch_unused_slot_id());
        owner_app(conn) = this;
        conn->set_last_heard_from(frame_clock::get_time());
        append_last(&circuits_, &conn->app_node());
    }
    pop_heap();
    return conn;
}

void AppHost::set_send_task(std::unique_ptr<tasks::SchedulerTask> value) { send_task_ = std::move(value); }

} // namespace net
