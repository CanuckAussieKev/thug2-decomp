// NOT ORIGINAL - thug2_sim_check.exe: the parts of the simulation's exactness proof that need no game
// (docs/design/simulation-rate.md "The proof"). The game simulates at the original's 60 Hz; what is checked here:
//
// - Online: the skater's object update goes out as the original's does (engine/net/net_cadence.h - the decision the
//   game's RiderHostSync::send_update makes): over 10 s of 60 Hz frames, the frames and clock stamps it sends on are
//   the original's rule written out (the server always; a client every 15th of the app's frames and between them once
//   the connection's send interval - two frames of 60 a second - has gone by).
//
// - The loading screen over a level's first frames (engine/gfx/level_cover.h): the main loop's hold rule leaves the
//   simulation's steps the same, in the same order - only later, by the loops held under the cover.
//
// The rest of the proof plays recorded runs in the game: tools/game/sim_check.ts (every frame's simulation state, bit
// for bit, against the reference recorded from the build before a change, at several display rates).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/gfx/level_cover.h"
#include "engine/net/net_cadence.h"
#include "tests/no_error_dialogs.h"

namespace {

struct Tally {
    int checked = 0;
    int failed = 0;
};

Tally& tally()
{
    static Tally counts;
    return counts;
}

void expect(bool ok, const std::string& what)
{
    ++tally().checked;
    if (!ok) {
        ++tally().failed;
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
    expect(server == skater_updates(true, false) && server.size() == std::size_t{kFramesASecond} * kSeconds,
           "online: the server's skater sends every frame");
}

// (round LS2) The level's first frames under the loading screen (engine/gfx/level_cover.h): the main loop asks
// level_cover::holds once a loop and runs no step on a held one. A model of the loop: the simulation a function of its
// step count (a stand-in state, hashed), the cover up from the original's hide for `covered_loops` loops, the level's
// own time-of-day scripts owing `owed_steps` steps after the hide. The states the steps make, in order, are the same
// with the cover as without - only later by the held loops; under the cover the simulation steps `owed_steps` (none
// when the level's time of day is its own) - in a network game every loop steps.
struct CoverRun {
    std::vector<uint64_t> states; // each step's state, in order
    uint32_t steps_under_cover = 0;
    uint32_t held_loops = 0;
};

uint64_t step_state(uint64_t state)
{
    constexpr uint64_t kPrime = 1099511628211ULL;       // (FNV-1a's)
    constexpr uint64_t kGolden = 0x9E3779B97F4A7C15ULL; // (2^64 / the golden ratio: any odd constant would do)
    return (state ^ kGolden) * kPrime;
}

struct CoverCase {
    uint32_t loops = 0;
    uint32_t hide_loop = 0;
    uint32_t covered_loops = 0; // (0: no cover)
    uint32_t owed_steps = 0;
    bool net_game = false;
};

CoverRun cover_run(const CoverCase& with)
{
    CoverRun run;
    uint64_t state = 0;
    uint32_t steps_since_hide = 0;
    for (uint32_t loop = 0; loop < with.loops; ++loop) {
        const bool covered =
            with.covered_loops > 0 && loop > with.hide_loop && loop <= with.hide_loop + with.covered_loops;
        const bool owed = steps_since_hide < with.owed_steps;
        if (render::level_cover::holds({.covered = covered, .net_game = with.net_game, .owed = owed})) {
            ++run.held_loops;
            continue;
        }
        state = step_state(state);
        run.states.push_back(state);
        // (the clock sets the level's state in the hide's own loop: its scripts run in the steps after it)
        if (loop > with.hide_loop) {
            ++steps_since_hide;
        }
        run.steps_under_cover += covered ? 1U : 0U;
    }
    return run;
}

void check_cover_hold()
{
    constexpr uint32_t kLoops = 600;
    constexpr uint32_t kHide = 200;
    constexpr uint32_t kCovered = 90; // (1.5 s of the level's first frames under the cover)
    constexpr uint32_t kTodSteps = 5; // (tod_manager_instant_fakelights_and_geo's waits)
    const CoverRun plain = cover_run({.loops = kLoops, .hide_loop = kHide});
    const CoverRun held = cover_run({.loops = kLoops, .hide_loop = kHide, .covered_loops = kCovered});
    const auto same_prefix = [&plain](const CoverRun& run) {
        return run.states.size() <= plain.states.size() &&
               std::equal(run.states.begin(), run.states.end(), plain.states.begin());
    };
    expect(held.steps_under_cover == 0 && held.held_loops == kCovered && same_prefix(held) &&
               held.states.size() + kCovered == plain.states.size(),
           "the cover: no step under it, the same steps in the same order - " + std::to_string(kCovered) + " loops later");
    const CoverRun tod =
        cover_run({.loops = kLoops, .hide_loop = kHide, .covered_loops = kCovered, .owed_steps = kTodSteps});
    expect(tod.steps_under_cover == kTodSteps && tod.held_loops == kCovered - kTodSteps && same_prefix(tod),
           "the cover with the clock's time of day: the level's own scripts for it run their steps under it, then held");
    const CoverRun net = cover_run({.loops = kLoops, .hide_loop = kHide, .covered_loops = kCovered, .net_game = true});
    expect(net.held_loops == 0 && net.states == plain.states, "the cover in a network game: nothing held");
    std::printf("cover: %zu steps run in %u loops with %u held (the frames covered); the same states as %zu steps "
                "without the cover\n",
                held.states.size(), kLoops, held.held_loops, plain.states.size());
}

} // namespace

int main()
{
    tests::no_error_dialogs();
    check_online_cadence();
    check_cover_hold();
    std::printf("sim check: %d checks, %d failed\n", tally().checked, tally().failed);
    return tally().failed == 0 ? 0 : 1;
}
