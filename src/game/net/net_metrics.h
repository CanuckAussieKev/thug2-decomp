#pragma once

#include <cstddef>
#include <cstdint>

namespace Net {

// A connection's traffic in one direction (Neversoft's Net::Metrics, 0x100C bytes; each Conn has two: incoming at
// +0x1CDC, outgoing at +0x2CE8): the last 256 packets (size and time), and per message id how many went and
// their bytes.
struct Metrics {
    static constexpr int kNumPackets = 256;   // the packet history (a ring)
    static constexpr int kNumMessageIds = 256;
    static constexpr int kUsagePeriod = 1000; // ms: what the bandwidth is measured over

    struct Packet {
        int m_size;
        int m_time;
    };

    // 0x004544E0: counts and totals zeroed (the packet history isn't touched).
    Metrics* Construct();
    // 0x00454550: a message of `msgId` with `size` bytes.
    void AddMessage(int msgId, int size);
    // 0x00454D90: a packet of `size` bytes at `time` into the history.
    void AddPacket(int size, int time);
    // 0x00454D00: the bytes of the packets in the history from less than a second before `time`.
    void CalculateUsage(int time);
    // 0x00454520: what CalculateUsage found.
    int GetBytesPerSec() { return m_bytesPerSec; }
    // 0x00454530: the bytes of the messages of `msgId`.
    int GetTotalMessageData(int msgId) { return m_messageData[msgId]; }
    // 0x00454540: how many messages of `msgId`.
    int GetTotalNumMessagesOfId(int msgId) { return m_numMessages[msgId]; }

    Packet m_packets[kNumPackets];       // +0x0000 the newest at (m_numPackets - 1) % 256
    int m_numPackets;                    // +0x0800 every packet added
    int m_numMessages[kNumMessageIds];   // +0x0804
    int m_messageData[kNumMessageIds];   // +0x0C04
    int m_totalBytes;                    // +0x1004 every packet's bytes
    int m_bytesPerSec;                   // +0x1008
};
static_assert(offsetof(Metrics, m_numPackets) == 0x800 && offsetof(Metrics, m_numMessages) == 0x804 &&
                  offsetof(Metrics, m_messageData) == 0xC04 && offsetof(Metrics, m_totalBytes) == 0x1004 &&
                  offsetof(Metrics, m_bytesPerSec) == 0x1008 && sizeof(Metrics) == 0x100C,
              "layout must match the original");

} // namespace Net
