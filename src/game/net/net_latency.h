#pragma once

#include <cstddef>
#include <cstdint>

namespace NetLink {

// A connection's round-trip times (Neversoft's Net library; one in each connection, at +0x1CA4): the last ten
// measurements and their average. A connection's resend threshold is twice the average (at least 500 ms).
struct PingProbe {
    static constexpr int kNumTimes = 10;

    // 0x00456970: no measurements yet; the average starts at 1000 ms.
    PingProbe* MakeNew();
    // 0x004569B0: `latency` replaces the oldest measurement; the average is taken over the nonzero ones (left
    // as it was if there are none).
    void InputDelayAmount(int latency);

    int m_averageLatency;   // +0x00
    int m_times[kNumTimes]; // +0x04 (0 = none)
    uint32_t m_succ;        // +0x2C where the next measurement goes
    uint32_t m_probeSentAt;    // +0x30 (set by the ping handlers, wt/netC)
    uint32_t m_echoSeenAt; // +0x34
};
static_assert(offsetof(PingProbe, m_times) == 0x04 && offsetof(PingProbe, m_succ) == 0x2C &&
                  offsetof(PingProbe, m_echoSeenAt) == 0x34 && sizeof(PingProbe) == 0x38,
              "layout must match the original");

} // namespace Net
