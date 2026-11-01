// NetLink::AppHost (the network library's base of the Server and the Client): queueing and streaming messages, the
// connection list and the app's tasks.

#include "engine/net/net_app.h"
#include "engine/net/net_lists.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
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
#include "engine/net/net_wire.h"
#include "engine/util/byte_stream.h"
#include "platform/stopwatch.h"
#include "engine/core/owner.h"

namespace net {

namespace {

using memory::ListNode;

// Message targets (not AppHost::kBroadcast, a flag).
constexpr int kHandleBroadcast = 0xFF;        // every connection
constexpr int kHandleExcludeBroadcast = 0x80; // every connection but handle & 0x7F
constexpr uint8_t kMsgBulkBegin = 0x0E;
constexpr uint8_t kMsgBulkChunk = 0x0F;
constexpr uint8_t kMsgUnreadyOk = 0x09; // default-queue messages that go to a connection that isn't ready
constexpr int kStreamPieceSize = static_cast<int>(wire::kStreamPieceSize);

constexpr int kMetricsTaskPriority = -2500;
constexpr int kReceiveTaskPriority = 1000;
constexpr int kProcessTaskPriority = 0;

// --- Conn (net_conn.h): short names for the fields used here ------------------------------------------------
// The outgoing message queues (lists::Heads of links, highest priority first).
ListNode* normal_queue(Conn* conn) { return &conn->normal_msgs_; }
ListNode* important_queue(Conn* conn) { return &conn->important_msgs_; }
ListNode* sequenced_queue(Conn* conn) { return &conn->sequenced_msgs_; }
// The receive buffer's read and write positions (its start and end).
uint8_t*& read_position(Conn* conn) { return reinterpret_cast<uint8_t*&>(conn->read_buffer_); }
uint8_t*& write_position(Conn* conn) { return reinterpret_cast<uint8_t*&>(conn->read_ptr_); }
// Status: 1 ready, bit 2 invalid, bit 3 disconnecting.
uint32_t& get_standing(Conn* conn) { return conn->cur_standing_; }
constexpr uint32_t kConnReady = Conn::kConnReady;
constexpr uint32_t kConnClosing = 8;
AppHost*& owner_app(Conn* conn) { return conn->owner_app_; }
// The next sequence id of each message group.
uint32_t& next_sequence_id(Conn* conn, uint8_t group) { return conn->next_sequence_ids_[group]; }
int& alloc_stream_key(Conn* conn) { return conn->stream_counter_; }
ListNode* get_streams(Conn* conn) { return &conn->send_streams_; } // lists::Foremost<BulkStreamNode>
void* in_metrics(Conn* conn) { return &conn->receive_metrics_; }   // NetLink::Gauges (0x100C bytes)
void* out_metrics(Conn* conn) { return &conn->send_metrics_; }
// The send buffer (its start, the end of what's written): a local connection takes its alias's (the other end,
// in this process) straight into its own receive buffer.
uint8_t*& send_buffer_start(Conn* conn) { return reinterpret_cast<uint8_t*&>(conn->outgoing_bytes_); }
uint8_t*& send_buffer_end(Conn* conn) { return reinterpret_cast<uint8_t*&>(conn->write_ptr_); }

// Conn's methods (net_conn.cpp).
Conn* construct_conn(void* memory, int flags) { return new (memory) Conn(flags); }
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
// The app's manager: the bandwidth allowed over all connections.
int total_bandwidth(void* manager) { return static_cast<Hub*>(manager)->fetch_throughput(); }
// 1 = a modem: a slow connection.
int connection_type(void* manager) { return static_cast<Hub*>(manager)->get_net_link_kind(); }

// --- Messages ---------------------------------------------------------------------------------------------------
// PendingMsg (0x0C) and SeqQueuedMsg (0x10, made by AppHost::new_queued_msg): a copy of a message shared by the
// links that send it to each connection; freed with the last.
PendingMsg* construct_queued_msg(void* memory, uint8_t id, uint16_t length, const void* data, uint8_t compression)
{
    return new (memory) PendingMsg(id, length, data, compression);
}
// ~PendingMsg / ~SeqQueuedMsg: frees the data.
void destroy_queued_msg_body(void* message) { static_cast<PendingMsg*>(message)->~PendingMsg(); }

// The links (net_messages.h): list nodes (+8 the priority) pointing at a queued message - the normal queue's (with
// the time it may be sent), the important queue's (also when it was last sent, 0 = never) and the sequenced queue's
// (also the group, whether it's a stream piece and the sequence id).
MsgNormalLink* construct_msg_link(void* memory, PendingMsg* message) { return new (memory) MsgNormalLink(message); }
ReliableMsgNode* construct_msg_important_link(void* memory, PendingMsg* message)
{
    return new (memory) ReliableMsgNode(message);
}
OrderedMsgNode* construct_msg_seq_link(void* memory, SeqQueuedMsg* message)
{
    return new (memory) OrderedMsgNode(message);
}

// BulkStreamNode: a lists::Elem (data = itself) with its StreamDesc.
BulkStreamNode* construct_stream_link(void* memory, StreamDesc* stream) { return new (memory) BulkStreamNode(stream); }
StreamDesc* stream_of(ListNode* link) { return reinterpret_cast<BulkStreamNode*>(link)->msg_meta_; }
StreamDesc* construct_stream_desc(void* memory) { return new (memory) StreamDesc; }
// ~StreamDesc: frees the data unless it was sent in place ("Destroying StreamDesc's Data").
void destroy_stream_desc_body(StreamDesc* stream) { stream->~StreamDesc(); }

// strcpy: the original copies descriptions with no length limit (32 bytes are kept for them).
void copy_string(char* out, const char* source)
{
    while ((*out++ = *source++) != '\0') {
    }
}

void init_msg_desc(NetMessageInfo* desc) { *desc = NetMessageInfo(); }

// The tail of a list.
void append_last(ListNode* head, ListNode* node)
{
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}
// Out of its list.
void drop_out(ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// The memory manager.
memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
void push_network_heap() { ram_stats()->enter_heap_scope(ram_stats()->network_heap_); }
void pop_heap() { ram_stats()->leave_heap_scope(); }

// The pieces of a streamed message (net_wire.h): the start (the description, the total size, the stream's id, a
// checksum of all the data, the message id and the first 256 bytes) and the pieces after it, each built in a buffer
// of its own.
struct StreamPieceMessage {
    uint8_t bytes[wire::kStreamPieceMaxSize]{};
    uint16_t size = 0;
    void build(int stream_id, const void* piece, int length)
    {
        util::ByteWriter writer(bytes, sizeof(bytes));
        wire::write_stream_piece(writer, stream_id, piece, static_cast<size_t>(length));
        size = static_cast<uint16_t>(writer.position());
    }
};

// --- The rest of the library ------------------------------------------------------------------------------
// A message link into a queue: lists::Foremost::append_node (0x00457350).
void add_link(ListNode* queue, ListNode* link) { reinterpret_cast<lists::Foremost*>(queue)->append_node(link); }
// conn's received messages to their handlers (Router::invoke_handlers_for_msg); 2 = halted, 3 = stop
int dispatch_messages(HandlerLists* dispatcher, Conn* conn, int flags)
{
    return static_cast<Router*>(dispatcher)->invoke_handlers_for_msg(conn, static_cast<uint32_t>(flags));
}

// A task of the app's (null if out of memory).
memory::owner<tasks::SchedulerTask*> new_task(int priority, tasks::Task<AppHost>::Code callback, AppHost* app)
{
    return new tasks::Task<AppHost>(callback, app, priority);
}
AppHost* task_app(tasks::Task<AppHost>* task) { return task->content_; }

} // namespace

// 0x004594C0
AppHost::AppHost(int flags)
{
    for (memory::ListNode& handlers : dispatcher_.lists_)
        handlers.make_head(); // (0x00457010)
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    dispatcher_app_ = this;
    list1414_.make_node(this);
    flag_bits_ = 0;
    circuits_.make_head();
    banned_.make_head();
    metrics_task_ = new_task(kMetricsTaskPriority, &AppHost::network_metrics, this);
    receive_task_ = new_task(kReceiveTaskPriority, &AppHost::receive_network_data, this);
    process_task_ = new_task(kProcessTaskPriority, &AppHost::pump_incoming_traffic, this);
    unknown1f00_[0] = unknown1f00_[1] = 0; // the double 0.0
    flag_bits_ = static_cast<uint32_t>(flags);
    clock_ = 0;
    unknown_packet_sink_ = nullptr;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

namespace {
EnqueueFilter g_enqueue_filter = nullptr; // NOT ORIGINAL - set_enqueue_filter's
}

void set_enqueue_filter(EnqueueFilter filter) { g_enqueue_filter = filter; }

// 0x00457EB0
void AppHost::put_on_outbox(int handle, NetMessageInfo* desc)
{
    // (NOT ORIGINAL: the filter - set_enqueue_filter)
    if (g_enqueue_filter && g_enqueue_filter(this, handle, desc))
        return;
    push_network_heap();
    // Every connection that takes it (`accepts`), or every one but handle & 0x7F, or the handle's.
    auto for_each_target = [&](auto accepts, auto queue) {
        if (handle == kHandleBroadcast) {
            lists::Hunt<Conn> search;
            for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
                if (accepts(conn))
                    queue(conn);
            }
        } else if (handle & kHandleExcludeBroadcast) {
            const int excluded = handle & ~kHandleExcludeBroadcast;
            lists::Hunt<Conn> search;
            for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
                if (conn->hdl_ != excluded && accepts(conn))
                    queue(conn);
            }
        } else {
            Conn* conn = find_conn_with_id(handle); // (not checked for null)
            if (accepts(conn))
                queue(conn);
        }
    };
    auto not_disconnecting = [](Conn* conn) { return (get_standing(conn) & kConnClosing) == 0; };

    switch (desc->queue_) {
    case NetMessageInfo::kLaneNormal: {
        void* memory = memory::alloc(0x0C);
        PendingMsg* message =
            memory ? construct_queued_msg(memory, desc->ident_, desc->byte_count_, desc->content_, desc->compression_)
                   : nullptr;
        // To ready connections only (to a single connection, also message 9).
        auto ready = [&](Conn* conn) {
            return get_standing(conn) == kConnReady ||
                   (handle != kHandleBroadcast && !(handle & kHandleExcludeBroadcast) && desc->ident_ == kMsgUnreadyOk);
        };
        for_each_target(ready, [&](Conn* conn) {
            if (desc->singular_)
                free_messages(conn, desc->queue_, desc->ident_);
            ++message->share_count_;
            void* link_memory = memory::alloc_zeroed(sizeof(MsgNormalLink));
            MsgNormalLink* link = link_memory ? construct_msg_link(link_memory, message) : nullptr;
            link->list_link_.priority = desc->rank_;
            link->due_time_ = desc->delay_ + clock_;
            add_link(normal_queue(conn), &link->list_link_);
        });
        if (message->share_count_ == 0) {
            destroy_queued_msg_body(message);
            ram_stats()->dealloc(message);
        }
        break;
    }
    case NetMessageInfo::kLanePriority: {
        void* memory = memory::alloc(0x0C);
        PendingMsg* message =
            memory ? construct_queued_msg(memory, desc->ident_, desc->byte_count_, desc->content_, desc->compression_)
                   : nullptr;
        for_each_target(not_disconnecting, [&](Conn* conn) {
            if (desc->singular_)
                free_messages(conn, desc->queue_, desc->ident_);
            ++message->share_count_;
            void* link_memory = memory::alloc_zeroed(sizeof(ReliableMsgNode));
            ReliableMsgNode* link = link_memory ? construct_msg_important_link(link_memory, message) : nullptr;
            link->list_link_.priority = desc->rank_;
            link->due_time_ = desc->delay_ + clock_;
            link->stamp_ms_ = 0;
            add_link(important_queue(conn), &link->list_link_);
        });
        if (message->share_count_ == 0) {
            destroy_queued_msg_body(message);
            ram_stats()->dealloc(message);
        }
        break;
    }
    case NetMessageInfo::kLaneOrdered: {
        SeqQueuedMsg* message =
            new_queued_msg(desc->ident_, desc->byte_count_, desc->content_, desc->group_id_, desc->compression_);
        for_each_target(not_disconnecting, [&](Conn* conn) {
            ++message->share_count_;
            void* link_memory = memory::alloc_zeroed(sizeof(OrderedMsgNode));
            OrderedMsgNode* link = link_memory ? construct_msg_seq_link(link_memory, message) : nullptr;
            // The original's broadcast gives the links the queue number (2) as their priority instead.
            link->list_link_.priority = handle == kHandleBroadcast ? desc->queue_ : desc->rank_;
            link->stamp_ms_ = 0;
            link->due_time_ = desc->delay_ + clock_;
            link->bundle_id_ = desc->group_id_;
            if (desc->fixed_seq_no_)
                link->order_index_ = desc->fixed_seq_no_;
            else
                link->order_index_ = next_sequence_id(conn, desc->group_id_)++;
            link->bulk_msg_ = desc->feed_;
            add_link(sequenced_queue(conn), &link->list_link_);
        });
        if (message->share_count_ == 0) {
            destroy_queued_msg_body(message);
            ram_stats()->dealloc(message);
        }
        break;
    }
    default:
        break;
    }
    pop_heap();
}

// 0x00458740
void AppHost::send_bulk_to_conn(Conn* conn, uint8_t msg_id, uint16_t size, void* data, const char* desc,
                                uint8_t group_id, bool queue_all_pieces, bool borrow_caller_buffer)
{
    // The original leaves the start message uninitialised on the stack (the 3 bytes sent past the first piece);
    // they're zeros here. (NetMessageInfo's constructor sets every field.)
    NetMessageInfo message = {};
    init_msg_desc(&message);
    wire::StreamStart start;
    start.total_size = size;
    start.stream_id = alloc_stream_key(conn);
    start.msg_id = msg_id;
    start.checksum = checksums::generate_crc_from_buffer(data, size);
    std::snprintf(start.desc, sizeof(start.desc), "%s", desc);
    const int first = size > kStreamPieceSize ? kStreamPieceSize : size;
    uint8_t start_bytes[wire::kStreamStartMaxSize];
    util::ByteWriter start_writer(start_bytes, sizeof(start_bytes));
    wire::write_stream_start(start_writer, start, data, static_cast<size_t>(first));
    message.byte_count_ = static_cast<uint16_t>(start_writer.position());
    message.content_ = start_bytes;
    message.group_id_ = group_id;
    message.ident_ = kMsgBulkBegin;
    message.queue_ = NetMessageInfo::kLaneOrdered;
    message.feed_ = 1;
    put_on_outbox(conn->hdl_, &message);

    if (size > kStreamPieceSize) {
        if (queue_all_pieces) {
            // Every piece now.
            const uint8_t* next = static_cast<const uint8_t*>(data) + first;
            StreamPieceMessage piece;
            for (int sent = first; sent < size;) {
                int length = size - sent;
                if (length > kStreamPieceSize)
                    length = kStreamPieceSize;
                piece.build(alloc_stream_key(conn), next, length);
                message.content_ = piece.bytes;
                message.byte_count_ = piece.size;
                message.group_id_ = group_id;
                message.ident_ = kMsgBulkChunk;
                message.queue_ = NetMessageInfo::kLaneOrdered;
                message.feed_ = 1;
                put_on_outbox(conn->hdl_, &message);
                sent += length;
                next += length;
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
            uint8_t* next = copy + first;
            void* memory = memory::alloc(sizeof(StreamDesc));
            StreamDesc* stream = memory ? construct_stream_desc(memory) : nullptr;
            void* link_memory = memory::alloc_zeroed(sizeof(BulkStreamNode));
            BulkStreamNode* link = link_memory ? construct_stream_link(link_memory, stream) : nullptr;
            stream->extent_ = size;
            stream->stream_id_ = alloc_stream_key(conn);
            stream->msg_id_ = msg_id;
            copy_string(stream->desc_, desc);
            stream->content_ = copy;
            stream->group_id_ = group_id;
            stream->succ_ = next;
            stream->order_number_ = next_sequence_id(conn, group_id);
            stream->borrowed_payload_ = borrow_caller_buffer;
            append_last(get_streams(conn), &link->list_link_);
            next_sequence_id(conn, group_id) += later_pieces;
            pop_heap();
        }
    }
    ++alloc_stream_key(conn);
}

// 0x00458AB0
void AppHost::send_bulk_message(int handle, uint8_t msg_id, uint16_t size, void* data, const char* desc,
                                uint8_t group_id, bool queue_all_pieces, bool borrow_caller_buffer)
{
    if (handle == kHandleBroadcast) {
        lists::Hunt<Conn> search;
        for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
            if (is_sound(conn))
                send_bulk_to_conn(conn, msg_id, size, data, desc, group_id, queue_all_pieces, borrow_caller_buffer);
        }
    } else {
        Conn* conn = find_conn_with_id(handle); // (not checked for null)
        if (is_sound(conn))
            send_bulk_to_conn(conn, msg_id, size, data, desc, group_id, queue_all_pieces, borrow_caller_buffer);
    }
}

// 0x00458BE0
Conn* AppHost::begin_conn_walk(ConnSearch* search)
{
    search->list_link_ = &circuits_;
    ListNode* next = circuits_.next;
    search->list_link_ = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
    return search->list_link_ ? static_cast<Conn*>(search->list_link_->data) : nullptr;
}

// 0x00458C10
void AppHost::flush_outbox()
{
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item())
        flush_outbox(conn);
}

// 0x00458CA0
void AppHost::transmit(bool send_in_frame)
{
    if (!send_in_frame) {
        ++clock_;
        lists::Hunt<Conn> search;
        for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item())
            change_send_now(conn, true);
    }
    flush_outbox();
}

// 0x00458D50
bool AppHost::is_conn_blocked(Conn* conn)
{
    lists::Hunt<BlockedConnEntry> search;
    for (const BlockedConnEntry* banned = search.head_element(&banned_); banned; banned = search.following_item()) {
        if (banned->remote_ip_ == conn->remote_ip_)
            return true;
    }
    return false;
}

// 0x00458DB0
int AppHost::fetch_traffic_spent()
{
    int total = 0;
    lists::Hunt<Conn> search;
    for (Conn* conn = search.head_element(&circuits_); conn; conn = search.following_item()) {
        if (is_off_machine(conn))
            total += get_bytes_per_second(out_metrics(conn));
    }
    return total;
}

// 0x00458E60: over the total allowed, over conn's own limit, or over its share of the total (split between the
// remote connections that aren't foreign).
bool AppHost::over_bandwidth_budget(Conn* conn)
{
    if (fetch_traffic_spent() >= total_bandwidth(supervisor_))
        return true;
    const int limit = bandwidth_limit(conn);
    if (get_bytes_per_second(out_metrics(conn)) >= limit)
        return true;
    int sharing = 0;
    lists::Hunt<Conn> search;
    for (Conn* other = search.head_element(&circuits_); other; other = search.following_item()) {
        if (is_off_machine(other) && !is_outsider(other))
            ++sharing;
    }
    if (sharing == 0)
        sharing = 1;
    const int share = total_bandwidth(supervisor_) / sharing;
    return get_bytes_per_second(out_metrics(conn)) > share;
}

// 0x00458F80
int AppHost::fetch_unused_slot_id()
{
    for (int handle = 1; handle < 0x100; ++handle) {
        if (!find_conn_with_id(handle))
            return handle;
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
        if ((get_standing(conn) & kConnClosing) && !messages_due(conn))
            mark_stale(conn);
        if (!is_sound(conn) && write_position(conn) <= read_position(conn) && !messages_due(conn)) {
            drop_out(&conn->app_node_);
            destroy_conn_body(conn);
            ram_stats()->dealloc(conn);
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
        int max_queued = 5;
        int max_per_frame = 3;
        if (connection_type(manager) == 1 || link_speed_class(conn) == 1) {
            max_queued = 2;
            max_per_frame = 1;
        }
        unsigned int streams = 0;
        for (ListNode* node = get_streams(conn)->next; node->data != reinterpret_cast<void*>(-1) && node;
             node = node->next)
            ++streams;
        if (streams == 0)
            continue;
        const int queued = num_stream_pieces_queued(conn);
        if (queued >= max_queued)
            continue;
        int budget = max_queued - queued;
        if (budget > max_per_frame)
            budget = max_per_frame;

        lists::Hunt<ListNode> links;
        ListNode* link = links.head_element(get_streams(conn));
        while (link) {
            ListNode* next_link = links.following_item();
            StreamDesc* stream = stream_of(link);
            int sent = static_cast<int>(stream->succ_ - stream->content_);
            const int size = stream->extent_;
            bool unfinished = false;
            while (sent < size) {
                if (budget <= 0) {
                    unfinished = true;
                    break;
                }
                int length = size - sent;
                if (length > kStreamPieceSize)
                    length = kStreamPieceSize;
                piece.build(stream->stream_id_, stream->succ_, length);
                message.ident_ = kMsgBulkChunk;
                message.byte_count_ = piece.size;
                message.queue_ = NetMessageInfo::kLaneOrdered;
                message.content_ = piece.bytes;
                message.group_id_ = stream->group_id_;
                message.feed_ = 1;
                message.fixed_seq_no_ = stream->order_number_++;
                put_on_outbox(conn->hdl_, &message);
                stream->succ_ += length;
                sent += length;
                --budget;
            }
            if (!unfinished) {
                destroy_stream_desc_body(stream);
                ram_stats()->dealloc(stream);
                static_cast<BulkStreamNode*>(link->data)->teardown(1);
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
        if (is_local_conn(conn) && conn->alias_conn_) {
            Conn* alias = conn->alias_conn_;
            const size_t length = send_buffer_end(alias) - send_buffer_start(alias);
            std::memcpy(write_position(conn), send_buffer_start(alias), length);
            send_buffer_end(conn->alias_conn_) = send_buffer_start(conn->alias_conn_);
            write_position(conn) += length;
        }
        const int result = dispatch_messages(&dispatcher_, conn, 0);
        if (result == 2)
            return true;
        if (result == 3)
            return false;
        if (!dispatch_queued_sequenced_messages(conn))
            return false;
        conn = next;
    }
    return true;
}

// 0x004597C0
Conn* AppHost::opened_link(int ip, uint16_t port, int flags)
{
    Conn* conn = nullptr;
    push_network_heap();
    if (open_links_ < peer_cap_) {
        void* memory = memory::alloc(0x3D20);
        conn = memory ? construct_conn(memory, flags) : nullptr; // (not checked for null)
        conn->remote_ip_ = static_cast<uint32_t>(ip);
        conn->port_no_ = port;
        conn->hdl_ = fetch_unused_slot_id();
        owner_app(conn) = this;
        conn->last_heard_from_ = frame_clock::get_time();
        append_last(&circuits_, &conn->app_node_);
    }
    pop_heap();
    return conn;
}

} // namespace net
