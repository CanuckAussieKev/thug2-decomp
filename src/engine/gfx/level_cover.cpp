// NOT ORIGINAL - a level's first frames under the loading screen (level_cover.h).
#include "engine/gfx/level_cover.h"

#include <atomic>

#include "engine/gfx/day_clock.h"
#include "engine/net/session_query.h"

namespace render::level_cover {

namespace {
// (read by the device's meter as it records - the drawing thread's - and set by the frame's end: atomic)
std::atomic<bool>& covered_flag()
{
    static std::atomic<bool> flag{false};
    return flag;
}
std::atomic<uint64_t>& screens_count()
{
    static std::atomic<uint64_t> count{0};
    return count;
}
} // namespace

void set_covered(bool covered) { covered_flag().store(covered); }

void note_loading_screen() { screens_count().fetch_add(1); }

uint64_t loading_screens() { return screens_count().load(); }

bool covered() { return covered_flag().load(); }

bool holds_simulation()
{
    if (!covered()) {
        return false; // (asked once a loop: nothing more read while nothing is covered)
    }
    return holds({.covered = true, .net_game = net::in_net_game(), .owed = !day_clock::light_ready()});
}

} // namespace render::level_cover
