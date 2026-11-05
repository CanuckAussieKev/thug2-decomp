// The game clock: real time scaled by the game's speed (stopped while paused).

#include "engine/sys/game_clock.h"

#include <cstdint>

#include "engine/sys/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "base/core/no_destroy.h"

namespace frame_clock {

constexpr double kCounterWrap = 4294967296.0; // 2^32: the clock counter's range

namespace {

// The game time up to the last change of speed (callers add game_clock_elapsed to it), the real time of that
// change, and the speed since.


// NOT ORIGINAL grouping - game_clock.cpp's state (the original's globals). Made on first use, never destroyed
// (game_clock_state()).
struct GameClockState {
    float speed_{1.0F};
    uint32_t last_change_{};
};

GameClockState& game_clock_state()
{
    static core::NoDestroy<GameClockState> state;
    return state.get();
}

uint32_t& last_change() { return game_clock_state().last_change_; }
float& get_pace() { return game_clock_state().speed_; }

// The original's x87 code: the unsigned difference times the speed, truncated (_ftol: through 64 bits).
int get_elapsed()
{
    const int difference = static_cast<int>(get_time() - last_change());
    auto scaled = static_cast<double>(difference);
    if (difference < 0) {
        scaled += kCounterWrap; // (0x006453B4)
    }
    scaled *= get_pace();
    return static_cast<int>(static_cast<int64_t>(scaled));
}

void catch_up()
{
    objects::set_clock_offset(objects::clock_offset() + get_elapsed());
    last_change() = get_time();
}

} // namespace

// 0x0044ECE0
int game_clock_elapsed() { return get_elapsed(); }

// 0x0041C200
int game_clock_time() { return game_clock_elapsed() + objects::clock_offset(); }

// 0x0044ED10
void pause_game_clock(bool pause)
{
    catch_up();
    get_pace() = pause ? 0.0F : get_time_scale();
}

// 0x0044ED70
void set_game_clock_speed(float speed)
{
    catch_up();
    get_pace() = speed;
    set_slomo(speed);
}

} // namespace frame_clock
