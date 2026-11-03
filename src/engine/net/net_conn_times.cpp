// NetLink::Conn's communication times (the last ones below Conn's own range, 0x00455970 on: net_conn.cpp). See
// net_conn.h.

#include <algorithm>
#include <cstdint>

#include "engine/net/net_conn.h"
#include "platform/stopwatch.h"

namespace net {

// 0x00455920
void Conn::stamp_last_heard(uint32_t extra)
{
    const uint32_t time = frame_clock::get_time() + extra;
    last_heard_from_ = std::max(last_heard_from_, time);
}

// 0x00455940
uint32_t Conn::get_silence_duration() const
{
    const uint32_t now = frame_clock::get_time();
    if (now < last_heard_from_) {
        return 0;
    }
    return now - last_heard_from_;
}

} // namespace net
