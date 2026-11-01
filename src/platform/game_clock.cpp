// The game clock: real time scaled by the game's speed (stopped while paused).

#include "platform/game_clock.h"

#include <cstdint>

#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"

namespace frame_clock {

namespace {

// The game time up to the last change of speed (callers add game_clock_elapsed to it), the real time of that
// change, and the speed since.
int& get_offset() { return objects::g_clock_offset; }

float g_speed{1.0f};
uint32_t g_last_change;

uint32_t& last_change() { return g_last_change; }
float& get_pace() { return g_speed; }

// The original's x87 code: the unsigned difference times the speed, truncated (_ftol: through 64 bits).
int get_elapsed()
{
    const int difference = static_cast<int>(get_time() - last_change());
    auto scaled = static_cast<double>(difference);
    if (difference < 0)
        scaled += 4294967296.0; // (0x006453B4)
    scaled *= get_pace();
    return static_cast<int>(static_cast<int64_t>(scaled));
}

void catch_up()
{
    get_offset() += get_elapsed();
    last_change() = get_time();
}

} // namespace

// 0x0044ECE0
int game_clock_elapsed() { return get_elapsed(); }

// 0x0041C200
int game_clock_time() { return game_clock_elapsed() + get_offset(); }

// 0x0044ED10
void pause_game_clock(bool pause)
{
    catch_up();
    get_pace() = pause ? 0.0f : get_time_scale();
}

// 0x0044ED70
void set_game_clock_speed(float speed)
{
    catch_up();
    get_pace() = speed;
    set_slomo(speed);
}

} // namespace frame_clock
