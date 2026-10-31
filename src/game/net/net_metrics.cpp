// A connection's traffic counts (NetLink::Gauges). See net_metrics.h.

#include <cstdint>

#include "game/net/net_metrics.h"

namespace NetLink {

// 0x004544E0
Gauges* Gauges::MakeNew()
{
    m_packetsSeen = 0;
    m_byteSum = 0;
    m_throughput = 0;
    for (int& count : m_perKindCounts)
        count = 0;
    for (int& bytes : m_messageData)
        bytes = 0;
    return this;
}

// 0x00454550
void Gauges::AppendNote(int msgId, int size)
{
    ++m_perKindCounts[msgId];
    m_messageData[msgId] += size;
}

// 0x00454D90
void Gauges::CountPacket(int size, int time)
{
    Packet& packet = m_packetTally[m_packetsSeen % kTallyWindow]; // (a signed remainder, like the original's)
    packet.m_clock = time;
    packet.m_extent = size;
    m_byteSum += size;
    ++m_packetsSeen;
}

// 0x00454D00
void Gauges::CalculateUsage(int time)
{
    int total = 0;
    for (int i = 0; i < kTallyWindow && i < m_packetsSeen; ++i) {
        int age = static_cast<int>(static_cast<uint32_t>(time) - static_cast<uint32_t>(m_packetTally[i].m_clock));
        if (age < kUsagePeriod) // (a packet "from the future" counts too)
            total += m_packetTally[i].m_extent;
    }
    m_throughput = total;
}

} // namespace Net
