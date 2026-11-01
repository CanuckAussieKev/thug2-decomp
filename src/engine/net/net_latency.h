#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

// A connection's round-trip times (one in each connection, at +0x1CA4): the last ten
// measurements and their average. A connection's resend threshold is twice the average (at least 500 ms).
struct PingProbe {
    static constexpr int kNumTimes = 10;

    // 0x00456970: no measurements yet; the average starts at 1000 ms.
    PingProbe();
    // 0x004569B0: `latency` replaces the oldest measurement; the average is taken over the nonzero ones (left
    // as it was if there are none).
    void input_delay_amount(int latency);

    int average_latency_{1000}; // +0x00
    int times_[kNumTimes]{}; // +0x04 (0 = none)
    uint32_t succ_{0};       // +0x2C where the next measurement goes
    uint32_t probe_sent_at_{0}; // +0x30 (set by the ping handlers, wt/netC)
    uint32_t echo_seen_at_{0};  // +0x34
};

} // namespace net
