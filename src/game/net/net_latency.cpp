// A connection's round-trip times (Net::LatencyTest). See net_latency.h.

#include "game/net/net_latency.h"

#include "hook/hook.h"

namespace Net {

// 0x00456970
LatencyTest* LatencyTest::Construct()
{
    for (int& time : m_times)
        time = 0;
    m_averageLatency = 1000;
    m_next = 0;
    m_sendTime = 0;
    m_receiveTime = 0;
    return this;
}

// 0x004569B0
void LatencyTest::InputLatencyValue(int latency)
{
    m_times[m_next] = latency;
    m_next = (m_next + 1) % kNumTimes;
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

REPLACE(0x00456970, &Net::LatencyTest::Construct);
REPLACE(0x004569B0, &Net::LatencyTest::InputLatencyValue);
