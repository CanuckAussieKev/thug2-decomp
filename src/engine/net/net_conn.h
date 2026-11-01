#pragma once

// NetLink::Conn: one end of a connection - its outgoing message queues, the sequenced
// messages and streams it has received, its buffers, latency and bandwidth; and the list types of its queues
// (lists::Elem<T> / lists::Foremost<T> / lists::Hunt<T> for the message links and the stream links) with the
// destructors and constructors in Conn's address range (0x00455970-0x00456910).

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/net/net_latency.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_messages.h"
#include "engine/net/net_metrics.h"

namespace net {

struct AppHost;

// (The local searches over Conn's lists have vtables holding only a destructor that resets it - 0x004559D0 /
// 0x00455A80, 0x004559E0 / 0x00455AB0, 0x004559F0 / 0x00455AE0, 0x00455A00 / 0x00455B10: not kept.)

// NetLink::Conn: one end of a connection, as an App keeps it (0x3D20 bytes). Its methods: net_conn.cpp.
// (Formerly `Conn`, a view of its fields at their offsets.)
struct Conn {
    // flag_bits_
    static constexpr uint32_t kFlagLocal = 0x1; // the App's own client/server in this process
    static constexpr uint32_t kFlagRemote = 0x2;
    static constexpr uint32_t kFlagForeign = 0x4; // made for a packet from an unknown address
    // cur_standing_
    static constexpr uint32_t kConnReady = 0x1; // cleared by Invalidate
    static constexpr uint32_t kStatus2 = 0x2;   // (set by game code) resends wait 1000 ms whatever the latency
    static constexpr uint32_t kConnBroken = 0x4;
    // link_class_: the bandwidth set_link_speed_class gives each (bytes per second)
    static constexpr int kBandwidthBroadband = 0; // 400000
    static constexpr int kBandwidthModem = 1;     // 4200
    static constexpr int kBandwidthLan = 2;       // 800000
    static constexpr int kChannelGroups = 256;

    // 0x00455C80
    explicit Conn(int flags);
    // 0x00456790 (the destructor)
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    // 0x00455920 (net_conn_times.cpp): the last communication time moved to now + `extra` ms, unless it's
    // later already.
    void stamp_last_heard(uint32_t extra);
    // 0x00455940 (net_conn_times.cpp): milliseconds since the last communication (0 if that's in the future).
    uint32_t get_silence_duration();
    // 0x00455960
    void set_transmit_period(uint32_t interval) { tx_spacing_ms_ = interval; }
    // 0x00455970
    uint32_t get_transmit_period();
    // 0x00455980
    void force_send_now(bool force);
    // 0x00455990
    bool is_send_forced();
    // 0x004559A0
    int get_link_speed_class();
    // 0x004559B0
    void change_throughput(int bytes_per_second);
    // 0x004559C0
    int fetch_throughput();
    // 0x00455A10 / 0x00455A20 / 0x00455A30
    bool is_outsider();
    bool is_host_local();
    bool is_off_machine();
    // 0x00455A40: the type, and its bandwidth (an unknown type leaves the bandwidth).
    void set_link_speed_class(int type);
    // 0x00455B40: 0 for a local connection.
    int get_mean_ping();
    // 0x00455C10: no longer ready; invalid (AppHost::drop_broken_links deletes it).
    void mark_stale();
    // 0x00455C20
    bool is_sound();
    // 0x00455C30: how long an important message waits for its ack before going again: twice the average
    // latency (none for a local connection), at least 500 ms; 1000 ms with kStatus2.
    uint32_t get_retry_delay();
    // 0x00455E50: the sequenced messages received ahead of their turn deleted (links and messages).
    void destroy_received_sequenced_messages();
    // 0x00455F30: every link deleted (the three queues, the received ones) and the streams (theirs too).
    void destroy_links();
    // 0x00456210: the important and sequenced queues emptied: links deleted, messages released.
    void destroy_important_messages();
    // 0x00456370: the three queues' messages released (each deleted with its last link); links kept.
    void release_queued_messages();
    // 0x00456540: the most times one important or sequenced message has been resent.
    int get_highest_resend_count();
    // 0x004565E0: the sequenced links carrying stream pieces.
    int get_num_queued_stream_pieces();
    // 0x00456630: the important and sequenced messages' resend counts zeroed.
    void clear_resend_counts();
    // 0x004566D0: the important and sequenced messages that went out with `packetstamp` go again (a send failed).
    void mark_messages_for_retry(uint32_t packetstamp);
    // 0x00456770: every queue emptied (as the destructor does).
    void drop_queued_traffic();

    memory::ListNode normal_msgs_{};    // +0x00 links: PendingMsg* at +0x14, send time at +0x18
    memory::ListNode important_msgs_{}; // +0x14 links: last send time at +0x14 (0 = not sent), PendingMsg* at +0x18,
                                        //       a time at +0x24
    memory::ListNode sequenced_msgs_{}; // +0x28 links: last send time at +0x1C, a time at +0x24, SeqQueuedMsg* at
                                        //       +0x28
    int hdl_{};                         // +0x3C
    uint32_t remote_ip_{};              // +0x40
    uint32_t ident_;                  // +0x44 a running number (0x0067D800, counted by the constructor)
    uint16_t port_no_{};              // +0x48
    uint8_t pad4a_[2]{};
    char* outgoing_bytes_;      // +0x4C the packet being built (0x514 bytes): its start
    char* write_ptr_;           // +0x50 and end
    char* read_buffer_;         // +0x54 received data (0xA28 bytes): its start
    char* read_ptr_;            // +0x58 and end
    uint32_t unknown5c_{};      // +0x5C (never touched by the library)
    uint32_t cur_standing_;     // +0x60 kStatus...
    uint32_t flag_bits_;        // +0x64 kFlag...
    AppHost* owner_app_{};      // +0x68 the App (set by AppHost::opened_link)
    uint32_t last_heard_from_{}; // +0x6C the last communication (AppHost::opened_link: the creation time)
    uint32_t last_transmit_at_; // +0x70
    uint32_t tx_spacing_ms_;    // +0x74
    uint32_t next_sequence_ids_[kChannelGroups]{}; // +0x78 outgoing: the next sequence id of each group
    uint32_t sequence_numbers_[kChannelGroups]{};  // +0x478 the next sequence number expected in each group
    int stream_counter_;                         // +0x878
    // +0x87C sequenced messages received ahead of their turn, by group: links with the group at +0x15, the
    // sequence number at +0x20, SeqQueuedMsg* at +0x28.
    memory::ListNode received_queues_[kChannelGroups]{};
    memory::ListNode received_streams_{}; // +0x1C7C lists::Foremost<BulkStreamNode>: streams being received
    memory::ListNode send_streams_{};     // +0x1C90 lists::Foremost<BulkStreamNode>: streams being sent
    PingProbe ping_probe_;              // +0x1CA4
    Gauges receive_metrics_;            // +0x1CDC
    Gauges send_metrics_;               // +0x2CE8
    bool send_now_;                     // +0x3CF4 force a send this frame
    memory::ListNode app_node_{};       // +0x3CF8 in the App's connection list (data = the connection)
    Conn* alias_conn_;                  // +0x3D0C
    int link_class_{};                  // +0x3D10
    uint32_t last_timestamp_in_;        // +0x3D14
    uint32_t timestamp_out_;            // +0x3D18
    int link_speed_;                    // +0x3D1C bytes per second
};

} // namespace net
