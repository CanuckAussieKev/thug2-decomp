// NetLink::Conn's communication times (the last ones below Conn's own range, 0x00455970 on: net_conn.cpp). See
// net_conn.h.

#include <cstdint>

#include "game/net/net_conn.h"
#include "game/sys/timer.h"

namespace NetLink {

// 0x00455920
void Conn::StampLastHeard(uint32_t extra)
{
    uint32_t time = FrameClock::GetTime() + extra;
    if (time > m_lastCommTime)
        m_lastCommTime = time;
}

// 0x00455940
uint32_t Conn::GetSilenceDuration()
{
    uint32_t now = FrameClock::GetTime();
    if (now < m_lastCommTime)
        return 0;
    return now - m_lastCommTime;
}

} // namespace Net
