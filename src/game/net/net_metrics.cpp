// A connection's traffic counts (Net::Metrics). See net_metrics.h.

#include <cstdint>

#include "game/net/net_metrics.h"

namespace Net {

// 0x004544E0
Metrics* Metrics::Construct()
{
    m_numPackets = 0;
    m_totalBytes = 0;
    m_bytesPerSec = 0;
    for (int& count : m_numMessages)
        count = 0;
    for (int& bytes : m_messageData)
        bytes = 0;
    return this;
}

// 0x00454550
void Metrics::AddMessage(int msgId, int size)
{
    ++m_numMessages[msgId];
    m_messageData[msgId] += size;
}

// 0x00454D90
void Metrics::AddPacket(int size, int time)
{
    Packet& packet = m_packets[m_numPackets % kNumPackets]; // (a signed remainder, like the original's)
    packet.m_time = time;
    packet.m_size = size;
    m_totalBytes += size;
    ++m_numPackets;
}

// 0x00454D00
void Metrics::CalculateUsage(int time)
{
    int total = 0;
    for (int i = 0; i < kNumPackets && i < m_numPackets; ++i) {
        int age = static_cast<int>(static_cast<uint32_t>(time) - static_cast<uint32_t>(m_packets[i].m_time));
        if (age < kUsagePeriod) // (a packet "from the future" counts too)
            total += m_packets[i].m_size;
    }
    m_bytesPerSec = total;
}

} // namespace Net
