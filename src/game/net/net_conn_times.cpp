// Net::Conn's communication times (the last ones below Conn's own range, 0x00455970 on: net_conn.cpp). See
// net_conn.h.

#include "game/net/net_conn.h"
#include "game/sys/timer.h"
#include "hook/hook.h"

namespace Net {

// 0x00455920
void Conn::UpdateCommTime(uint32_t extra)
{
    uint32_t time = Tmr::GetTime() + extra;
    if (time > m_lastCommTime)
        m_lastCommTime = time;
}

// 0x00455940
uint32_t Conn::GetTimeElapsedSinceCommunication()
{
    uint32_t now = Tmr::GetTime();
    if (now < m_lastCommTime)
        return 0;
    return now - m_lastCommTime;
}

} // namespace Net

REPLACE(0x00455920, &Net::Conn::UpdateCommTime);
REPLACE(0x00455940, &Net::Conn::GetTimeElapsedSinceCommunication);
REPLACE(0x00455960, &Net::Conn::SetSendInterval);
