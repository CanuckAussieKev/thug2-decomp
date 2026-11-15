#pragma once

// NOT ORIGINAL - the network's cadence at any tick rate (docs/design/tick-rate.md "Online"). The original runs its
// network once a 1/60 s tick: the app's tasks (receive, the received messages to their handlers, send, the connection
// metrics), the server's relay and score tasks and the skater's object update. At [Graphics] tick_rate 120 / 144 / 240 they run once each 1/60 s, in the
// tick that ends the 1/60 s frame (frame_clock::grid_frame_ends - every tick at 60), so peers see the 60 Hz run's
// stream: the same messages, the same frame counts (AppHost::frame_count counts 1/60 s frames), the same clock stamps
// (frame_clock::get_time is the 60 Hz grid's), the same packets a second. Nothing on the wire changes.
#include <cstdint>

namespace net_cadence {

// The skater's object update forces every field (and is sent whatever the send interval) every 15th frame.
inline constexpr uint32_t kForcedEveryFrames = 15;

// What RiderHostSync::send_update (game/rider/local_net_logic.cpp) decides by.
struct SkaterSend {
    bool on_server = false;
    uint32_t frame_count = 0;        // the app's frames (AppHost::frame_count)
    uint32_t clock_ms = 0;           // the app's clock
    uint32_t last_transmit_ms = 0;   // the clock when the last update went
    uint32_t transmit_period_ms = 0; // the connection's send interval
};

// The server's own skater always sends; a client's every 15th of the app's frames, and between them once the
// connection's send interval has gone by.
inline bool skater_update_due(const SkaterSend& send)
{
    if (send.on_server || send.frame_count % kForcedEveryFrames == 0) {
        return true;
    }
    return send.clock_ms - send.last_transmit_ms >= send.transmit_period_ms;
}

} // namespace net_cadence
