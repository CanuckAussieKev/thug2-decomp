// The game clock: real time scaled by the game's speed (stopped while paused).

#include "game/sys/game_clock.h"

#include <cstdint>

#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/obj/control_pad.h"

namespace Tmr {

namespace {

// The game time up to the last change of speed (callers add GameClockElapsed to it), the real time of that
// change, and the speed since.
int& Offset() { return Obj::g_clockOffset; }

GAME_GLOBAL(float, g_speed, 0x0067D7C0, 1.0f);
GAME_GLOBAL(uint32_t, g_lastChange, 0x00696D64);

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

REPLACE(0x0044ECE0, Tmr::GameClockElapsed);
REPLACE(0x0041C200, Tmr::GameClockTime);
REPLACE(0x0044ED10, Tmr::PauseGameClock);
REPLACE(0x0044ED70, Tmr::SetGameClockSpeed);
