#pragma once

#include <cstddef>
#include <cstdint>

namespace NetLink {

// A connection's traffic in one direction (Neversoft's NetLink::Gauges, 0x100C bytes; each Conn has two: incoming at
// +0x1CDC, outgoing at +0x2CE8): the last 256 packets (size and time), and per message id how many went and
// their bytes.
struct Gauges {
    static constexpr int kNumPackets = 256;   // the packet history (a ring)
    static constexpr int kNumMessageIds = 256;
    static constexpr int kUsagePeriod = 1000; // ms: what the bandwidth is measured over

    struct Packet {
        int m_extent;
        int m_clock;
    };

    // 0x004544E0: counts and totals zeroed (the packet history isn't touched).
    Gauges* MakeNew();
    // 0x00454550: a message of `msgId` with `size` bytes.
    void AppendNote(int msgId, int size);
    // 0x00454D90: a packet of `size` bytes at `time` into the history.
    void CountPacket(int size, int time);
    // 0x00454D00: the bytes of the packets in the history from less than a second before `time`.
    void CalculateUsage(int time);
    // 0x00454520: what CalculateUsage found.
    int GetThroughput() { return m_bytesPerSec; }
    // 0x00454530: the bytes of the messages of `msgId`.
    int GetMsgByteTotal(int msgId) { return m_messageData[msgId]; }
    // 0x00454540: how many messages of `msgId`.
    int GetMsgCountForType(int msgId) { return m_perKindCounts[msgId]; }

    Packet m_packetTally[kNumPackets];       // +0x0000 the newest at (m_numPackets - 1) % 256
    int m_numPackets;                    // +0x0800 every packet added
    int m_perKindCounts[kNumMessageIds];   // +0x0804
    int m_messageData[kNumMessageIds];   // +0x0C04
    int m_totalBytes;                    // +0x1004 every packet's bytes
    int m_bytesPerSec;                   // +0x1008
};
static_assert(offsetof(Gauges, m_numPackets) == 0x800 && offsetof(Gauges, m_perKindCounts) == 0x804 &&
                  offsetof(Gauges, m_messageData) == 0xC04 && offsetof(Gauges, m_totalBytes) == 0x1004 &&
                  offsetof(Gauges, m_bytesPerSec) == 0x1008 && sizeof(Gauges) == 0x100C,
              "layout must match the original");

} // namespace Net
