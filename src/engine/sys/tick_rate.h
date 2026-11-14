#pragma once

// NOT ORIGINAL - the simulation's tick rate ([Graphics] tick_rate, docs/design/tick-rate.md). The game steps its
// simulation (physics, scripts, the logic tasks) in fixed ticks; the original's tick is 1/60 s. tick_rate=120 / 144 / 240
// runs it in shorter ticks: the main loop's pace (engine/sys/mlp.cpp), the step every system multiplies by
// (frame_clock::get_slomo_frame_seconds), the clock's vblanks a second (so frame_clock::get_time stays real
// milliseconds), the render-only frames' schedule (frame_pacing.h) and the hard 1/60 steps that were the tick's length all
// read it here.
//
// At 60 (the default) everything is computed exactly as before: the step is the float 1.0F / 60 (0x3C888889), the
// clock's rate is game_config::frame_rate (60, or 50 with the PAL command line, as ever), counts of ticks are unchanged.
// The rate is set once at start-up (before the clock starts) and never changes during a run.
//
// What stays 60 whatever the rate: units of time that data and the original's code count in 1/60 s (script and data
// "frames", the animations' 60 frames a second, kFramesToMs...), the network's send periods
// (game_config::frame_rate - online play stays at the original's cadence: docs/design/tick-rate.md "Online").
#include <cstdint>

#include "base/core/api.h"

namespace tick_rate {

inline constexpr int kOriginal = 60; // the original's ticks a second
inline constexpr int kLowest = 60;
inline constexpr int kHighest = 240;

// [Graphics] tick_rate as written: "" / unreadable / 0 -> 60; N -> N clamped to 60 - 240.
int parse(const char* text);

// The rate (clamped to 60 - 240). Start-up only: before frame_clock::prepare_all (the checks set it freely).
void set_rate(int rate);
// Reads [Graphics] tick_rate the first time it is called (later calls keep the rate: it never changes in a run) and logs
// a rate other than 60. (engine/sys/tick_rate_settings.cpp - the only part that reads the ini.)
void read_settings();

// Ticks a second (60 unless set).
int rate();
// The rate is the original's (60): every conversion below is then the identity / the original's constant.
bool original();
// A tick in seconds as a float: 1.0F / rate (bits 0x3C888889 at 60 - the original's 1/60 constant).
THUG2_API float step_seconds();
// A tick in seconds as a double: 1.0 / rate.
THUG2_API double tick_seconds();
// The rate as a float (60.0F at 60): "ticks a second" where the original multiplied seconds by 60.0F to count ticks.
float rate_float();
// The main loop's tick in performance-counter ticks: frequency / rate (the unsigned division the original did by 60).
uint64_t counter_ticks(uint64_t frequency);
// The clock's vblanks a second: `configured` (game_config::frame_rate - 60, 50 for PAL) at the original rate, else the
// tick rate (a vblank is a tick in this build: frame_clock::note_refresh runs once a tick).
int clock_rate(int configured);
// A number of ticks that was meant as time at 60 ticks a second, for this rate: n * rate / 60 rounded to the nearest
// (n itself at 60; at least 1 when n > 0).
uint64_t ticks_for(uint64_t ticks_at_60);
// A factor applied once every 1/60 s (a decay, a damping), for one tick: pow(factor, 60 / rate) (`factor` itself at 60).
double per_tick_factor(double factor_per_60th);
// A blend fraction applied once every 1/60 s (a lerp a frame towards a target), for one tick: what leaves the same
// remainder after 1/60 s - 1 - pow(1 - fraction, 60 / rate) (`fraction` itself at 60).
double per_tick_fraction(double fraction_per_60th);
// An amount added or taken once every 1/60 s (a fade step, a damping speed) for one tick: amount * 60 / rate (`amount`
// itself at 60).
float per_tick_amount(float amount_per_60th);

// The 60 Hz grid (docs/design/tick-rate.md "The 60 Hz grid"): `ticks` ticks of this rate cover floor(ticks * 60 / rate)
// whole 1/60 s frames (`ticks` itself at 60) - what counts in 60 Hz frames (timers, tick counters, script waits) steps
// when a tick ends one, so it holds the 60 Hz run's value at every 1/60 s boundary.
uint64_t frames_ended_by(uint64_t ticks);
// The 1/60 s frame the tick `ticks` (covering (ticks - 1, ticks] of them) ends in: ceil(ticks * 60 / rate).
uint64_t frame_of_tick(uint64_t ticks);

} // namespace tick_rate
