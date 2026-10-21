#pragma once

// The game clock (in Tmr): real milliseconds since the last change of speed times the speed (0 while paused,
// else the slow motion), counted on from the offset at 0x00696D60 that each change adds to.
namespace Tmr {

// 0x0044ECE0: the game clock's milliseconds since the last change of speed (callers add the offset).
int GameClockElapsed();
// 0x0041C200: the game clock's time: GameClockElapsed() + the offset.
int GameClockTime();
// 0x00420250 (cdecl): the game clock's milliseconds since `start` (a GameClockTime): GameClockElapsed() + (the offset -
// start) - src/game/obj/model_component.cpp.
int GameClockTimeSince(int start);
// 0x0044ED10: the time so far into the offset; then stopped when `pause`, else running at the slow motion.
void PauseGameClock(bool pause);
// 0x0044ED70: the time so far into the offset; then running at `speed`, which also becomes the slow motion.
void SetGameClockSpeed(float speed);

} // namespace Tmr
