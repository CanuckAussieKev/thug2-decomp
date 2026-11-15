// NOT ORIGINAL - thug2_sim_check.exe: the parts of the simulation's exactness proof that need no game
// (docs/design/simulation-rate.md "The proof"). The game simulates at the original's 60 Hz; what is checked here:
//
// - Online: the skater's object update goes out as the original's does (engine/net/net_cadence.h - the decision the
//   game's RiderHostSync::send_update makes): over 10 s of 60 Hz frames, the frames and clock stamps it sends on are
//   the original's rule written out (the server always; a client every 15th of the app's frames and between them once
//   the connection's send interval - two frames of 60 a second - has gone by).
//
// The rest of the proof plays recorded runs in the game: tools/game/sim_check.ts (every frame's simulation state, bit
// for bit, against the reference recorded from the build before a change, at several display rates).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/net/net_cadence.h"
#include "tests/no_error_dialogs.h"

namespace {

int g_failures = 0;
int g_checked = 0;

void expect(bool ok, const std::string& what)
{
    ++g_checked;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

constexpr uint32_t kFramesASecond = 60;
constexpr uint32_t kMsASecond = 1000;
constexpr uint32_t kSeconds = 10;
constexpr uint32_t kTransmitPeriodMs = 2 * kMsASecond / kFramesASecond; // (the connection's: two frames)
constexpr uint32_t kOriginalForcedEvery = 15;                           // (the original's literal)

// One skater update as it leaves: the app's clock stamp and frame count.
struct Sent {
    uint32_t clock = 0;
    uint32_t frame = 0;
    bool operator==(const Sent&) const = default;
};

// The app's clock at frame `frame` (frame_clock::get_time: the vblanks x 1000 / 60, truncated).
uint32_t clock_at(uint32_t frame) { return static_cast<uint32_t>(static_cast<uint64_t>(frame) * kMsASecond / kFramesASecond); }

// 10 s of updates: `by_game` the game's decision (net_cadence::skater_update_due), else the original's rule as written.
std::vector<Sent> skater_updates(bool on_server, bool by_game)
{
    std::vector<Sent> out;
    uint32_t last_transmit = 0;
    for (uint32_t frame = 1; frame <= kFramesASecond * kSeconds; ++frame) {
        const uint32_t clock = clock_at(frame);
        bool due = false;
        if (by_game) {
            due = net_cadence::skater_update_due({.on_server = on_server,
                                                  .frame_count = frame,
                                                  .clock_ms = clock,
                                                  .last_transmit_ms = last_transmit,
                                                  .transmit_period_ms = kTransmitPeriodMs});
        } else {
            due = on_server || frame % kOriginalForcedEvery == 0 || clock - last_transmit >= kTransmitPeriodMs;
        }
        if (due) {
            last_transmit = clock;
            out.push_back({.clock = clock, .frame = frame});
        }
    }
    return out;
}

void check_online_cadence()
{
    expect(net_cadence::kForcedEveryFrames == kOriginalForcedEvery, "online: every field forced every 15th frame");
    const std::vector<Sent> client = skater_updates(false, true);
    expect(client == skater_updates(false, false),
           "online: a client's updates (frames, clock stamps) are the original's - " + std::to_string(client.size()) +
               " in 10 s");
    std::printf("online: a client's skater sends %zu updates in 10 s\n", client.size());
    const std::vector<Sent> server = skater_updates(true, true);
    expect(server == skater_updates(true, false) && server.size() == kFramesASecond * kSeconds,
           "online: the server's skater sends every frame");
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_online_cadence();
    std::printf("sim check: %d checks, %d failed\n", g_checked, g_failures);
    return g_failures == 0 ? 0 : 1;
}
