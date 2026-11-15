#pragma once

// NOT ORIGINAL grouping - when a skater's object update goes to the host (game/rider/local_net_logic.cpp
// RiderHostSync::send_update), as a pure function the simulation check (src/tests/sim_check.cpp) runs too. The network
// runs once a 60 Hz simulation step, as the original's: the app counts a frame a step (AppHost::frame_count), its clock
// is the game clock's milliseconds, and the decision below is the original's.
#include <cstdint>

namespace net_cadence {

// The skater's object update forces every field (and is sent whatever the send interval) every 15th frame.
inline constexpr uint32_t kForcedEveryFrames = 15;

// What the decision reads.
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
