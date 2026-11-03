#pragma once

// NetLink::Conn: one end of a connection - its outgoing message queues, the sequenced
// messages and streams it has received, its buffers, latency and bandwidth; and the list types of its queues
// (lists::Elem<T> / lists::Foremost<T> / lists::Hunt<T> for the message links and the stream links) with the
// destructors and constructors in Conn's address range (0x00455970-0x00456910).

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/core/arena_alloc.h"
#include "engine/net/net_latency.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_messages.h"
#include "engine/net/net_metrics.h"

namespace net {

struct AppHost;

// NOT ORIGINAL - one of a connection's packet buffers (the original kept its start and a write pointer): the bytes
// of the whole buffer and how many of them are filled.
struct PacketBuffer {
    std::span<uint8_t> bytes;
    std::size_t used = 0;
};

[[nodiscard]] inline std::span<uint8_t> filled(const PacketBuffer& buffer) { return buffer.bytes.first(buffer.used); }

[[nodiscard]] inline std::span<uint8_t> vacant(const PacketBuffer& buffer) { return buffer.bytes.subspan(buffer.used); }

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

    // The fields other code reads and sets.
    memory::ListNode& normal_msgs() { return normal_msgs_; }
    [[nodiscard]] const memory::ListNode& normal_msgs() const { return normal_msgs_; }
    memory::ListNode& important_msgs() { return important_msgs_; }
    [[nodiscard]] const memory::ListNode& important_msgs() const { return important_msgs_; }
    memory::ListNode& sequenced_msgs() { return sequenced_msgs_; }
    [[nodiscard]] const memory::ListNode& sequenced_msgs() const { return sequenced_msgs_; }
    [[nodiscard]] int hdl() const { return hdl_; }
    void set_hdl(int value) { hdl_ = value; }
    [[nodiscard]] uint32_t remote_ip() const { return remote_ip_; }
    void set_remote_ip(uint32_t value) { remote_ip_ = value; }
    uint32_t& ident() { return ident_; }
    [[nodiscard]] uint32_t ident() const { return ident_; }
    [[nodiscard]] uint16_t port_no() const { return port_no_; }
    void set_port_no(uint16_t value) { port_no_ = value; }
    PacketBuffer& outgoing() { return outgoing_; }
    [[nodiscard]] const PacketBuffer& outgoing() const { return outgoing_; }
    PacketBuffer& incoming() { return incoming_; }
    [[nodiscard]] const PacketBuffer& incoming() const { return incoming_; }
    uint32_t& cur_standing() { return cur_standing_; }
    [[nodiscard]] uint32_t cur_standing() const { return cur_standing_; }
    AppHost*& owner_app() { return owner_app_; }
    [[nodiscard]] AppHost* owner_app() const { return owner_app_; }
    void set_owner_app(AppHost* value) { owner_app_ = value; }
    [[nodiscard]] uint32_t last_heard_from() const { return last_heard_from_; }
    void set_last_heard_from(uint32_t value) { last_heard_from_ = value; }
    [[nodiscard]] uint32_t last_transmit_at() const { return last_transmit_at_; }
    void set_last_transmit_at(uint32_t value) { last_transmit_at_ = value; }
    std::array<uint32_t, kChannelGroups>& next_sequence_ids() { return next_sequence_ids_; }
    [[nodiscard]] const std::array<uint32_t, kChannelGroups>& next_sequence_ids() const { return next_sequence_ids_; }
    std::array<uint32_t, kChannelGroups>& sequence_numbers() { return sequence_numbers_; }
    [[nodiscard]] const std::array<uint32_t, kChannelGroups>& sequence_numbers() const { return sequence_numbers_; }
    int& stream_counter() { return stream_counter_; }
    [[nodiscard]] int stream_counter() const { return stream_counter_; }
    std::array<memory::ListNode, kChannelGroups>& received_queues() { return received_queues_; }
    [[nodiscard]] const std::array<memory::ListNode, kChannelGroups>& received_queues() const { return received_queues_; }
    memory::ListNode& received_streams() { return received_streams_; }
    [[nodiscard]] const memory::ListNode& received_streams() const { return received_streams_; }
    memory::ListNode& send_streams() { return send_streams_; }
    [[nodiscard]] const memory::ListNode& send_streams() const { return send_streams_; }
    PingProbe& ping_probe() { return ping_probe_; }
    [[nodiscard]] const PingProbe& ping_probe() const { return ping_probe_; }
    Gauges& receive_metrics() { return receive_metrics_; }
    [[nodiscard]] const Gauges& receive_metrics() const { return receive_metrics_; }
    Gauges& send_metrics() { return send_metrics_; }
    [[nodiscard]] const Gauges& send_metrics() const { return send_metrics_; }
    memory::ListNode& app_node() { return app_node_; }
    [[nodiscard]] const memory::ListNode& app_node() const { return app_node_; }
    [[nodiscard]] Conn* alias_conn() const { return alias_conn_; }
    void set_alias_conn(Conn* value) { alias_conn_ = value; }
    uint32_t& last_timestamp_in() { return last_timestamp_in_; }
    [[nodiscard]] uint32_t last_timestamp_in() const { return last_timestamp_in_; }
    uint32_t& timestamp_out() { return timestamp_out_; }
    [[nodiscard]] uint32_t timestamp_out() const { return timestamp_out_; }

private:
    memory::ListNode normal_msgs_{};    // +0x00 links: PendingMsg* at +0x14, send time at +0x18
    memory::ListNode important_msgs_{}; // +0x14 links: last send time at +0x14 (0 = not sent), PendingMsg* at +0x18,
                                        //       a time at +0x24
    memory::ListNode sequenced_msgs_{}; // +0x28 links: last send time at +0x1C, a time at +0x24, SeqQueuedMsg* at
                                        //       +0x28
    int hdl_{};                         // +0x3C
    uint32_t remote_ip_{};              // +0x40
    uint32_t ident_;                  // +0x44 a running number (0x0067D800, counted by the constructor)
    uint16_t port_no_{};              // +0x48
    std::array<uint8_t, 2> pad4a_{};
    PacketBuffer outgoing_;     // +0x4C the packet being built (0x514 bytes) and how much of it is written
    PacketBuffer incoming_;     // +0x54 received data (0xA28 bytes) and how much of it has come
    uint32_t unknown5c_{};      // +0x5C (never touched by the library)
    uint32_t cur_standing_{kConnReady}; // +0x60 kStatus...
    uint32_t flag_bits_;        // +0x64 kFlag...
    AppHost* owner_app_{};      // +0x68 the App (set by AppHost::opened_link)
    uint32_t last_heard_from_{}; // +0x6C the last communication (AppHost::opened_link: the creation time)
    uint32_t last_transmit_at_{0};                             // +0x70
    uint32_t tx_spacing_ms_{0};                                // +0x74
    std::array<uint32_t, kChannelGroups> next_sequence_ids_{}; // +0x78 outgoing: the next sequence id of each group
    std::array<uint32_t, kChannelGroups> sequence_numbers_{};  // +0x478 the next sequence number expected in each group
    int stream_counter_{0};                                    // +0x878
    // +0x87C sequenced messages received ahead of their turn, by group: links with the group at +0x15, the
    // sequence number at +0x20, SeqQueuedMsg* at +0x28.
    std::array<memory::ListNode, kChannelGroups> received_queues_{};
    memory::ListNode received_streams_{}; // +0x1C7C lists::Foremost<BulkStreamNode>: streams being received
    memory::ListNode send_streams_{};     // +0x1C90 lists::Foremost<BulkStreamNode>: streams being sent
    PingProbe ping_probe_;              // +0x1CA4
    Gauges receive_metrics_;            // +0x1CDC
    Gauges send_metrics_;               // +0x2CE8
    bool send_now_{false};              // +0x3CF4 force a send this frame
    memory::ListNode app_node_{};       // +0x3CF8 in the App's connection list (data = the connection)
    Conn* alias_conn_{nullptr};         // +0x3D0C
    int link_class_{};                  // +0x3D10
    uint32_t last_timestamp_in_{0};     // +0x3D14
    uint32_t timestamp_out_{0};         // +0x3D18
    // a broadband link's bandwidth (bytes per second): what a connection starts with
    static constexpr int kBroadbandBandwidth = 400000;
    int link_speed_{kBroadbandBandwidth}; // +0x3D1C bytes per second
};

} // namespace net
