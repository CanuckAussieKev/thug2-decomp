#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

// A connection's traffic in one direction (Neversoft's NetLink::Gauges, 0x100C bytes; each Conn has two: incoming at
// +0x1CDC, outgoing at +0x2CE8): the last 256 packets (size and time), and per message id how many went and
// their bytes.
struct Gauges {
    static constexpr int kTallyWindow = 256; // the packet history (a ring)
    static constexpr int kNumMessageIds = 256;
    static constexpr int kUsagePeriod = 1000; // ms: what the bandwidth is measured over

    struct Packet {
        int extent_;
        int clock_;
    };

    // 0x004544E0: counts and totals zeroed (the packet history isn't touched).
    Gauges* make_new();
    // 0x00454550: a message of `msg_id` with `size` bytes.
    void append_note(int msg_id, int size);
    // 0x00454D90: a packet of `size` bytes at `time` into the history.
    void count_packet(int size, int time);
    // 0x00454D00: the bytes of the packets in the history from less than a second before `time`.
    void calculate_usage(int time);
    // 0x00454520: what calculate_usage found.
    int get_throughput() { return throughput_; }
    // 0x00454530: the bytes of the messages of `msg_id`.
    int get_msg_byte_total(int msg_id) { return message_data_[msg_id]; }
    // 0x00454540: how many messages of `msg_id`.
    int get_msg_count_for_type(int msg_id) { return per_kind_counts_[msg_id]; }

    Packet packet_tally_[kTallyWindow];   // +0x0000 the newest at (packets_seen_ - 1) % 256
    int packets_seen_;                    // +0x0800 every packet added
    int per_kind_counts_[kNumMessageIds]; // +0x0804
    int message_data_[kNumMessageIds];    // +0x0C04
    int byte_sum_;                        // +0x1004 every packet's bytes
    int throughput_;                      // +0x1008
};
static_assert(offsetof(Gauges, packets_seen_) == 0x800 && offsetof(Gauges, per_kind_counts_) == 0x804 &&
                  offsetof(Gauges, message_data_) == 0xC04 && offsetof(Gauges, byte_sum_) == 0x1004 &&
                  offsetof(Gauges, throughput_) == 0x1008 && sizeof(Gauges) == 0x100C,
              "layout must match the original");

} // namespace net
