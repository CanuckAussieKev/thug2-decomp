// NetLink::Conn: one end of a connection - its outgoing message queues, the sequenced
// messages and streams it has received, its buffers, latency and bandwidth. See net_conn.h.

#include "engine/net/net_conn.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_access_c.h"
#include "engine/net/net_app.h"

namespace net {

namespace {

using memory::ListNode;

// The bandwidth of each bandwidth type (bytes per second).
constexpr int kBroadbandBandwidth = 400000;
constexpr int kModemBandwidth = 4200;
constexpr int kLanBandwidth = 800000;

constexpr uint32_t kRecvChunkBytes = 0xA28;
constexpr uint32_t kWriteBufferSize = 0x514;

constexpr uint32_t kMinResendThreshold = 500;
constexpr uint32_t kStatus2ResendThreshold = 1000;

uint32_t g_next_conn_id{1};

// The running number each connection gets (+0x44).
uint32_t& next_conn_id() { return g_next_conn_id; }

// ~StreamDesc: frees the data unless it was sent in place.
void destroy_stream_desc_body(StreamDesc* stream) { stream->destroy_body(); }

// A link deleted through its vtable (the scalar deleting destructor).
void delete_link(void* link)
{
    auto* vtable = *static_cast<void***>(link);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(link, 1);
}

// A queued message's last link gone: deleted.
template <typename Msg>
void get_release(Msg* message)
{
    if (--message->share_count_ == 0) {
        message->destroy_body();
        memory::dealloc(message);
    }
}

// Each item of a list (the links: their data is the link itself), the next one fetched before `action` runs
// on the current one (which may delete it).
template <typename T, typename F>
void for_each(ListNode* head, F action)
{
    lists::Hunt<T> search;
    T* item = search.head_element(head);
    while (item) {
        T* next = search.following_item();
        action(item);
        item = next;
    }
}

} // namespace

// 0x00455C60
MsgSeqLinkHead* MsgSeqLinkHead::make_new()
{
    init_conn_list_head(this, kMsgSeqLinkVtable);
    return this;
}

void init_conn_list_head(ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}

// 0x00455B50 / 0x00455B70 / 0x00455B90 / 0x00455BB0: the message lists' lists::Elem<T> destructor bodies
void MsgNormalNode::destroy_body() { unlink_as(kMsgNormalNodeVtable); }
void MsgImpNode::destroy_body() { unlink_as(kMsgImpNodeVtable); }
void MsgSeqNode::destroy_body() { unlink_as(kMsgSeqNodeVtable); }
void StreamLinkNode::destroy_body() { unlink_as(kStreamLinkNodeVtable); }

// 0x00455C80
Conn* Conn::make_new(int flags)
{
    init_conn_list_head(&normal_msgs_, kMsgNormalLinkVtable);
    init_conn_list_head(&important_msgs_, kMsgImpLinkVtable);
    init_conn_list_head(&sequenced_msgs_, kMsgSeqLinkVtable);
    cur_standing_ = 0;
    flag_bits_ = 0;
    for (ListNode& queue : received_queues_)
        static_cast<MsgSeqLinkHead*>(&queue)->make_new();
    init_conn_list_head(&received_streams_, kStreamLinkVtable);
    init_conn_list_head(&send_streams_, kStreamLinkVtable);
    ping_probe_.make_new();
    receive_metrics_.make_new();
    send_metrics_.make_new();
    app_node_.vtable = kConnNodeVtable;
    app_node_.data = this;
    app_node_.priority = 0;
    app_node_.next = &app_node_;
    app_node_.prev = &app_node_;

    flag_bits_ = static_cast<uint32_t>(flags);
    cur_standing_ = kConnReady;
    read_buffer_ = read_ptr_ = static_cast<char*>(memory::alloc(kRecvChunkBytes));
    outgoing_bytes_ = write_ptr_ = static_cast<char*>(memory::alloc(kWriteBufferSize));
    tx_spacing_ms_ = 0;
    last_transmit_at_ = 0;
    send_now_ = false;
    alias_conn_ = nullptr;
    last_timestamp_in_ = 0;
    timestamp_out_ = 0;
    stream_counter_ = 0;
    link_speed_ = kBroadbandBandwidth;
    // (only the first 64 groups' sequence numbers are zeroed: 0x40 dwords each - the rest keep what the
    // memory held)
    std::memset(next_sequence_ids_, 0, 0x100);
    std::memset(sequence_numbers_, 0, 0x100);
    ident_ = next_conn_id()++;
    return this;
}

// 0x00456790
void Conn::teardown()
{
    destroy_received_sequenced_messages();
    release_queued_messages();
    destroy_links();
    memory::dealloc(read_buffer_);
    memory::dealloc(outgoing_bytes_);
    app_node_.destroy_body();
    static_cast<StreamLinkNode*>(&send_streams_)->destroy_body();
    static_cast<StreamLinkNode*>(&received_streams_)->destroy_body();
    for (int group = kChannelGroups - 1; group >= 0; --group)
        static_cast<MsgSeqNode*>(&received_queues_[group])->destroy_body();
    static_cast<MsgSeqNode*>(&sequenced_msgs_)->destroy_body();
    static_cast<MsgImpNode*>(&important_msgs_)->destroy_body();
    static_cast<MsgNormalNode*>(&normal_msgs_)->destroy_body();
}

// 0x00455970
uint32_t Conn::get_transmit_period() { return tx_spacing_ms_; }

// 0x00455980
void Conn::force_send_now(bool force) { send_now_ = force; }

// 0x00455990
bool Conn::is_send_forced() { return send_now_; }

// 0x004559A0
int Conn::get_link_speed_class() { return link_class_; }

// 0x004559B0
void Conn::change_throughput(int bytes_per_second) { link_speed_ = bytes_per_second; }

// 0x004559C0
int Conn::fetch_throughput() { return link_speed_; }

// 0x00455A10
bool Conn::is_outsider() { return (flag_bits_ & kFlagForeign) != 0; }

// 0x00455A20
bool Conn::is_host_local() { return (flag_bits_ & kFlagLocal) != 0; }

// 0x00455A30
bool Conn::is_off_machine() { return (flag_bits_ & kFlagRemote) != 0; }

// 0x00455A40
void Conn::set_link_speed_class(int type)
{
    link_class_ = type;
    switch (type) {
    case kBandwidthBroadband:
        link_speed_ = kBroadbandBandwidth;
        break;
    case kBandwidthModem:
        link_speed_ = kModemBandwidth;
        break;
    case kBandwidthLan:
        link_speed_ = kLanBandwidth;
        break;
    }
}

// 0x00455B40
int Conn::get_mean_ping()
{
    if (is_host_local())
        return 0;
    return ping_probe_.average_latency_;
}

// 0x00455C10
void Conn::mark_stale()
{
    cur_standing_ &= ~kConnReady;
    cur_standing_ |= kConnBroken;
}

// 0x00455C20
bool Conn::is_sound() { return (cur_standing_ & kConnBroken) == 0; }

// 0x00455C30
uint32_t Conn::get_retry_delay()
{
    if (cur_standing_ & kStatus2)
        return kStatus2ResendThreshold;
    int threshold = 2 * get_mean_ping();
    if (threshold < static_cast<int>(kMinResendThreshold)) // (signed)
        threshold = kMinResendThreshold;
    return static_cast<uint32_t>(threshold);
}

// 0x00455E50
void Conn::destroy_received_sequenced_messages()
{
    for (ListNode& queue : received_queues_) {
        for_each<OrderedMsgNode>(&queue, [](OrderedMsgNode* link) {
            if (SeqQueuedMsg* message = link->shared_msg_) {
                message->destroy_body();
                memory::dealloc(message);
            }
            delete_link(link);
        });
    }
}

// 0x00455F30
void Conn::destroy_links()
{
    auto drop_link = [](void* link) { delete_link(link); };
    for_each<MsgNormalLink>(&normal_msgs_, drop_link);
    for_each<ReliableMsgNode>(&important_msgs_, drop_link);
    for_each<OrderedMsgNode>(&sequenced_msgs_, drop_link);
    for (ListNode& queue : received_queues_)
        for_each<OrderedMsgNode>(&queue, drop_link);
    auto delete_stream = [](BulkStreamNode* link) {
        if (StreamDesc* stream = link->msg_meta_) {
            destroy_stream_desc_body(stream);
            memory::dealloc(stream);
        }
        delete_link(link);
    };
    for_each<BulkStreamNode>(&received_streams_, delete_stream);
    for_each<BulkStreamNode>(&send_streams_, delete_stream);
}

// 0x00456210
void Conn::destroy_important_messages()
{
    for_each<ReliableMsgNode>(&important_msgs_, [](ReliableMsgNode* link) {
        get_release(link->shared_msg_);
        delete_link(link);
    });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [](OrderedMsgNode* link) {
        get_release(link->shared_msg_);
        delete_link(link);
    });
}

// 0x00456370
void Conn::release_queued_messages()
{
    for_each<MsgNormalLink>(&normal_msgs_, [](MsgNormalLink* link) { get_release(link->shared_msg_); });
    for_each<ReliableMsgNode>(&important_msgs_, [](ReliableMsgNode* link) { get_release(link->shared_msg_); });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [](OrderedMsgNode* link) { get_release(link->shared_msg_); });
}

// 0x00456540
int Conn::get_highest_resend_count()
{
    int highest = 0;
    for_each<ReliableMsgNode>(&important_msgs_, [&](ReliableMsgNode* link) {
        if (link->retry_count_ > highest)
            highest = link->retry_count_;
    });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [&](OrderedMsgNode* link) {
        if (link->retry_count_ > highest)
            highest = link->retry_count_;
    });
    return highest;
}

// 0x004565E0
int Conn::get_num_queued_stream_pieces()
{
    int count = 0;
    for_each<OrderedMsgNode>(&sequenced_msgs_, [&](OrderedMsgNode* link) {
        if (link->bulk_msg_)
            ++count;
    });
    return count;
}

// 0x00456630
void Conn::clear_resend_counts()
{
    for_each<ReliableMsgNode>(&important_msgs_, [](ReliableMsgNode* link) { link->retry_count_ = 0; });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [](OrderedMsgNode* link) { link->retry_count_ = 0; });
}

// 0x004566D0
void Conn::mark_messages_for_retry(uint32_t packetstamp)
{
    for_each<ReliableMsgNode>(&important_msgs_, [=](ReliableMsgNode* link) {
        if (link->packet_clock_ == packetstamp)
            link->stamp_ms_ = 0;
    });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [=](OrderedMsgNode* link) {
        if (link->packet_clock_ == packetstamp)
            link->stamp_ms_ = 0;
    });
}

// 0x00456770
void Conn::drop_queued_traffic()
{
    destroy_received_sequenced_messages();
    release_queued_messages();
    destroy_links();
}

} // namespace net
