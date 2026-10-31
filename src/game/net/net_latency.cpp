// A connection's round-trip times (NetLink::PingProbe). See net_latency.h.

#include <cstdint>

#include "game/net/net_latency.h"

namespace NetLink {

// 0x00456970
PingProbe* PingProbe::MakeNew()
{
    for (int& time : m_times)
        time = 0;
    m_averageLatency = 1000;
    m_succ = 0;
    m_probeSentAt = 0;
    m_echoSeenAt = 0;
    return this;
}

// 0x004569B0
void PingProbe::InputDelayAmount(int latency)
{
    m_times[m_succ] = latency;
    m_succ = (m_succ + 1) % kNumTimes;
    uint32_t total = 0; // (wraps like the original's int sum)
    int count = 0;
    for (int time : m_times) {
        if (time) {
            total += static_cast<uint32_t>(time);
            ++count;
        }
    }
    if (count > 0)
        m_averageLatency = static_cast<int>(total) / count;
}

} // namespace Net
