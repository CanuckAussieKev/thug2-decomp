// A connection's traffic counts (NetLink::Gauges). See net_metrics.h.

#include <cstdint>

#include "engine/net/net_metrics.h"
#include "engine/core/checked_index.h"

namespace net {

// 0x004544E0
Gauges::Gauges()
{
    for (int& count : per_kind_counts_)
        count = 0;
    for (int& bytes : message_data_)
        bytes = 0;
}

// 0x00454550
void Gauges::append_note(int msg_id, int size)
{
    ++core::at(per_kind_counts_, msg_id);
    core::at(message_data_, msg_id) += size;
}

// 0x00454D90
void Gauges::count_packet(int size, int time)
{
    Packet& packet = core::at(packet_tally_, packets_seen_ % kTallyWindow); // (a signed remainder, like the original's)
    packet.clock_ = time;
    packet.extent_ = size;
    byte_sum_ += size;
    ++packets_seen_;
}

// 0x00454D00
void Gauges::calculate_usage(int time)
{
    int total = 0;
    for (int i = 0; i < kTallyWindow && i < packets_seen_; ++i) {
        const int age = static_cast<int>(static_cast<uint32_t>(time) - static_cast<uint32_t>(core::at(packet_tally_, i).clock_));
        if (age < kUsagePeriod) // (a packet "from the future" counts too)
            total += core::at(packet_tally_, i).extent_;
    }
    throughput_ = total;
}

} // namespace net
