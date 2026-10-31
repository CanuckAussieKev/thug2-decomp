// NetLink::Conn's communication times (the last ones below Conn's own range, 0x00455970 on: net_conn.cpp). See
// net_conn.h.

#include <cstdint>

#include "engine/net/net_conn.h"
#include "platform/stopwatch.h"

namespace NetLink {

// 0x00455920
void Conn::StampLastHeard(uint32_t extra)
{
    uint32_t time = FrameClock::GetTime() + extra;
    if (time > m_lastHeardFrom)
        m_lastHeardFrom = time;
}

// 0x00455940
uint32_t Conn::GetSilenceDuration()
{
    uint32_t now = FrameClock::GetTime();
    if (now < m_lastHeardFrom)
        return 0;
    return now - m_lastHeardFrom;
}

} // namespace Net
