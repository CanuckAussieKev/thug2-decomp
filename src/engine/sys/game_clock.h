#pragma once

// The game clock (in Tmr): real milliseconds since the last change of speed times the speed (0 while paused,
// else the slow motion), counted on from the offset at 0x00696D60 that each change adds to.
#include <cstdint>

namespace frame_clock {

// 0x0044ECE0: the game clock's milliseconds since the last change of speed (callers add the offset).
int game_clock_elapsed();
// 0x0041C200: the game clock's time: game_clock_elapsed() + the offset.
int game_clock_time();
// 0x00420250 (cdecl): the game clock's milliseconds since `start` (a game_clock_time): game_clock_elapsed() + (the offset -
// start) - src/engine/objects/model_component.cpp.
int game_clock_time_since(int start);
// 0x0044ED10: the time so far into the offset; then stopped when `pause`, else running at the slow motion.
void pause_game_clock(bool pause);
// 0x0044ED70: the time so far into the offset; then running at `speed`, which also becomes the slow motion.
void set_game_clock_speed(float speed);

// NOT ORIGINAL - the game clock's whole state (the real time of its last change of speed, the speed since, and the
// offset game_clock_time adds), read and put back as a whole: a demo starts its run with the clock it was recorded
// with (devtools/demo.h).
struct GameClockSnapshot {
    uint32_t last_change = 0;
    float pace = 1.0F;
    int offset = 0;
};
GameClockSnapshot game_clock_snapshot();
void set_game_clock_snapshot(const GameClockSnapshot& snapshot);

} // namespace frame_clock
