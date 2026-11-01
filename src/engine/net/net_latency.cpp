// A connection's round-trip times (NetLink::PingProbe). See net_latency.h.

#include <cstdint>

#include "engine/net/net_latency.h"

namespace net {

// 0x00456970
PingProbe::PingProbe()
{
    for (int& time : times_)
        time = 0;
    average_latency_ = 1000;
    succ_ = 0;
    probe_sent_at_ = 0;
    echo_seen_at_ = 0;
}

// 0x004569B0
void PingProbe::input_delay_amount(int latency)
{
    times_[succ_] = latency;
    succ_ = (succ_ + 1) % kNumTimes;
    uint32_t total = 0; // (wraps like the original's int sum)
    int count = 0;
    for (int time : times_) {
        if (time) {
            total += static_cast<uint32_t>(time);
            ++count;
        }
    }
    if (count > 0)
        average_latency_ = static_cast<int>(total) / count;
}

} // namespace net
