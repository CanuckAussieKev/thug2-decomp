// A connection's traffic counts (NetLink::Gauges). See net_metrics.h.

#include <cstdint>

#include "game/net/net_metrics.h"

namespace NetLink {

// 0x004544E0
Gauges* Gauges::MakeNew()
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
void Gauges::AppendNote(int msgId, int size)
{
    ++m_numMessages[msgId];
    m_messageData[msgId] += size;
}

// 0x00454D90
void Gauges::CountPacket(int size, int time)
{
    Packet& packet = m_packetTally[m_numPackets % kNumPackets]; // (a signed remainder, like the original's)
    packet.m_clock = time;
    packet.m_extent = size;
    m_totalBytes += size;
    ++m_numPackets;
}

// 0x00454D00
void Gauges::CalculateUsage(int time)
{
    int total = 0;
    for (int i = 0; i < kNumPackets && i < m_numPackets; ++i) {
        int age = static_cast<int>(static_cast<uint32_t>(time) - static_cast<uint32_t>(m_packetTally[i].m_clock));
        if (age < kUsagePeriod) // (a packet "from the future" counts too)
            total += m_packetTally[i].m_extent;
    }
    m_bytesPerSec = total;
}

} // namespace Net
