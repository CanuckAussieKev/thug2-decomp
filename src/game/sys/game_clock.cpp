// The game clock: real time scaled by the game's speed (stopped while paused).

#include "game/sys/game_clock.h"

#include <cstdint>

#include "game/sys/timer.h"
#include "game/obj/control_pad.h"

namespace Tmr {

namespace {

// The game time up to the last change of speed (callers add GameClockElapsed to it), the real time of that
// change, and the speed since.
int& Offset() { return Obj::g_clockOffset; }

float g_speed{1.0f};
uint32_t g_lastChange;

uint32_t& LastChange() { return g_lastChange; }
float& Speed() { return g_speed; }

// The original's x87 code: the unsigned difference times the speed, truncated (_ftol: through 64 bits).
int Elapsed()
{
    int difference = static_cast<int>(GetTime() - LastChange());
    double scaled = static_cast<double>(difference);
    if (difference < 0)
        scaled += 4294967296.0; // (0x006453B4)
    scaled *= Speed();
    return static_cast<int>(static_cast<int64_t>(scaled));
}

void CatchUp()
{
    Offset() += Elapsed();
    LastChange() = GetTime();
}

} // namespace

// 0x0044ECE0
int GameClockElapsed() { return Elapsed(); }

// 0x0041C200
int GameClockTime() { return GameClockElapsed() + Offset(); }

// 0x0044ED10
void PauseGameClock(bool pause)
{
    CatchUp();
    Speed() = pause ? 0.0f : GetSlomo();
}

// 0x0044ED70
void SetGameClockSpeed(float speed)
{
    CatchUp();
    Speed() = speed;
    SetSlomo(speed);
}

} // namespace Tmr
