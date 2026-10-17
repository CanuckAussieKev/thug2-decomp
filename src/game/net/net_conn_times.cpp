// Net::Conn's communication times (the last ones below Conn's own range, 0x00455970 on: wt/netF). See ConnView
// in net_app_view.h.

#include "game/net/net_app_view.h"
#include "game/sys/timer.h"
#include "hook/hook.h"

namespace Net {

// 0x00455920
void ConnView::UpdateCommTime(uint32_t extra)
{
    uint32_t time = Tmr::GetTime() + extra;
    if (time > LastCommTime())
        LastCommTime() = time;
}

// 0x00455940
uint32_t ConnView::GetTimeElapsedSinceCommunication()
{
    uint32_t now = Tmr::GetTime();
    if (now < LastCommTime())
        return 0;
    return now - LastCommTime();
}

} // namespace Net

REPLACE(0x00455920, &Net::ConnView::UpdateCommTime);
REPLACE(0x00455940, &Net::ConnView::GetTimeElapsedSinceCommunication);
REPLACE(0x00455960, &Net::ConnView::SetSendInterval);
