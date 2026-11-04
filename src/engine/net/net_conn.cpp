// NetLink::Conn: one end of a connection - its outgoing message queues, the sequenced
// messages and streams it has received, its buffers, latency and bandwidth. See net_conn.h.

#include "engine/net/net_conn.h"

#include <cstdint>
#include <cstring>
#include <utility>

#include "engine/memory/arena_alloc.h"
#include "engine/memory/mem.h"
#include "engine/lists/list_walk.h"
#include "engine/net/net_app.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_app_view.h"
#include "engine/net/net_messages.h"

namespace net {

namespace {
constexpr size_t kSequenceTableBytes = 0x100;

using memory::ListNode;

// The bandwidth of each bandwidth type (bytes per second).
constexpr int kModemBandwidth = 4200;
constexpr int kLanBandwidth = 800000;

constexpr uint32_t kRecvChunkBytes = 0xA28;
constexpr uint32_t kWriteBufferSize = 0x514;

constexpr uint32_t kMinResendThreshold = 500;
constexpr uint32_t kStatus2ResendThreshold = 1000;

// The running number each connection gets (+0x44).
uint32_t& next_conn_id()
{
    static uint32_t next_conn_id{1};
    return next_conn_id;
}

// ~StreamDesc: frees the data unless it was sent in place.
void destroy_stream_desc_body(StreamDesc* stream) { stream->~StreamDesc(); }

// A link deleted (its scalar deleting destructor).
template <typename Link>
void delete_link(Link* link)
{
    link->teardown(1);
}

// A queued message's last link gone: deleted.
template <typename Msg>
void get_release(Msg* message)
{
    if (--message->share_count() == 0) {
        message->~Msg();
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

// 0x00455C80
Conn::Conn(int flags) : ident_(next_conn_id()++), flag_bits_(static_cast<uint32_t>(flags))
{
    normal_msgs_.make_head();
    important_msgs_.make_head();
    sequenced_msgs_.make_head();
    for (ListNode& queue : received_queues_) {
        queue.make_head(); // (0x00455C60)
    }
    received_streams_.make_head();
    send_streams_.make_head();
    // (the ping probe and the two gauges made first by their constructors)
    app_node_.make_node(this);

    incoming_ = {.bytes = {static_cast<uint8_t*>(memory::alloc(kRecvChunkBytes)), kRecvChunkBytes}, .used = 0};
    outgoing_ = {.bytes = {static_cast<uint8_t*>(memory::alloc(kWriteBufferSize)), kWriteBufferSize}, .used = 0};
    // (only the first 64 groups' sequence numbers are zeroed: 0x40 dwords each - the rest keep what the
    // memory held)
    std::memset(next_sequence_ids_.data(), 0, kSequenceTableBytes);
    std::memset(sequence_numbers_.data(), 0, kSequenceTableBytes);
}

// 0x00456790
Conn::~Conn()
{
    destroy_received_sequenced_messages();
    release_queued_messages();
    destroy_links();
    memory::dealloc(incoming_.bytes.data());
    memory::dealloc(outgoing_.bytes.data());
    app_node_.uncouple();
    send_streams_.uncouple();
    received_streams_.uncouple();
    for (int group = kChannelGroups - 1; group >= 0; --group) {
        core::at(received_queues_, group).uncouple();
    }
    sequenced_msgs_.uncouple();
    important_msgs_.uncouple();
    normal_msgs_.uncouple();
}

// 0x00455970
uint32_t Conn::get_transmit_period() const { return tx_spacing_ms_; }

// 0x00455980
void Conn::force_send_now(bool force) { send_now_ = force; }

// 0x00455990
bool Conn::is_send_forced() const { return send_now_; }

// 0x004559A0
int Conn::get_link_speed_class() const { return link_class_; }

// 0x004559B0
void Conn::change_throughput(int bytes_per_second) { link_speed_ = bytes_per_second; }

// 0x004559C0
int Conn::fetch_throughput() const { return link_speed_; }

// 0x00455A10
bool Conn::is_outsider() const { return (flag_bits_ & kFlagForeign) != 0; }

// 0x00455A20
bool Conn::is_host_local() const { return (flag_bits_ & kFlagLocal) != 0; }

// 0x00455A30
bool Conn::is_off_machine() const { return (flag_bits_ & kFlagRemote) != 0; }

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
    default:
        break;
    }
}

// 0x00455B40
int Conn::get_mean_ping()
{
    if (is_host_local()) {
        return 0;
    }
    return ping_probe_.average_latency();
}

// 0x00455C10
void Conn::mark_stale()
{
    cur_standing_ &= ~kConnReady;
    cur_standing_ |= kConnBroken;
}

// 0x00455C20
bool Conn::is_sound() const { return (cur_standing_ & kConnBroken) == 0; }

// 0x00455C30
uint32_t Conn::get_retry_delay()
{
    if ((cur_standing_ & kStatus2) != 0U) {
        return kStatus2ResendThreshold;
    }
    int threshold = 2 * get_mean_ping();
    if (threshold < ints::as_signed(kMinResendThreshold)) { // (signed)
        threshold = kMinResendThreshold;
    }
    return static_cast<uint32_t>(threshold);
}

// 0x00455E50
void Conn::destroy_received_sequenced_messages()
{
    for (ListNode& queue : received_queues_) {
        for_each<OrderedMsgNode>(&queue, [](OrderedMsgNode* link) {
            if (SeqQueuedMsg* message = link->shared_msg()) {
                message->~SeqQueuedMsg();
                memory::dealloc(message);
            }
            delete_link(link);
        });
    }
}

// 0x00455F30
void Conn::destroy_links()
{
    auto drop_link = [](auto* link) { delete_link(link); };
    for_each<MsgNormalLink>(&normal_msgs_, drop_link);
    for_each<ReliableMsgNode>(&important_msgs_, drop_link);
    for_each<OrderedMsgNode>(&sequenced_msgs_, drop_link);
    for (ListNode& queue : received_queues_) {
        for_each<OrderedMsgNode>(&queue, drop_link);
    }
    auto delete_stream = [](BulkStreamNode* link) {
        if (StreamDesc* stream = link->msg_meta()) {
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
        get_release(link->shared_msg());
        delete_link(link);
    });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [](OrderedMsgNode* link) {
        get_release(link->shared_msg());
        delete_link(link);
    });
}

// 0x00456370
void Conn::release_queued_messages()
{
    for_each<MsgNormalLink>(&normal_msgs_, [](MsgNormalLink* link) { get_release(link->shared_msg()); });
    for_each<ReliableMsgNode>(&important_msgs_, [](ReliableMsgNode* link) { get_release(link->shared_msg()); });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [](OrderedMsgNode* link) { get_release(link->shared_msg()); });
}

// 0x00456540
int Conn::get_highest_resend_count()
{
    int highest = 0;
    for_each<ReliableMsgNode>(&important_msgs_, [&](ReliableMsgNode* link) {
        if (std::cmp_greater(link->retry_count(), highest)) {
            highest = link->retry_count();
        }
    });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [&](OrderedMsgNode* link) {
        if (std::cmp_greater(link->retry_count(), highest)) {
            highest = link->retry_count();
        }
    });
    return highest;
}

// 0x004565E0
int Conn::get_num_queued_stream_pieces()
{
    int count = 0;
    for_each<OrderedMsgNode>(&sequenced_msgs_, [&](OrderedMsgNode* link) {
        if (link->bulk_msg()) {
            ++count;
        }
    });
    return count;
}

// 0x00456630
void Conn::clear_resend_counts()
{
    for_each<ReliableMsgNode>(&important_msgs_, [](ReliableMsgNode* link) { link->retry_count() = 0; });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [](OrderedMsgNode* link) { link->retry_count() = 0; });
}

// 0x004566D0
void Conn::mark_messages_for_retry(uint32_t packetstamp)
{
    for_each<ReliableMsgNode>(&important_msgs_, [=](ReliableMsgNode* link) {
        if (link->packet_clock() == packetstamp) {
            link->set_stamp_ms(0);
        }
    });
    for_each<OrderedMsgNode>(&sequenced_msgs_, [=](OrderedMsgNode* link) {
        if (link->packet_clock() == packetstamp) {
            link->set_stamp_ms(0);
        }
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
