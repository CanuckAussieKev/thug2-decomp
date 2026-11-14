#pragma once

#include <cstdint>

// The game clock: counted in vblanks (60 a second on PC; not original: one a tick, tick_rate.h), with the render frame count, an
// average frame length over the last four frames, and slow motion.
namespace frame_clock {

// 0x005BEA50: start the clock (also notes the C runtime clock and the CPU's time stamp counter).
void prepare_all();
// 0x005BE9B0: back to frame 0 with a one-vblank frame length (the vblank rate is set the first time).
void get_reset();

// 0x005BE700: a vblank has passed (counted since Reset, and in total).
void note_refresh();
// 0x005BE740: a frame has been rendered: its length in vblanks goes into the four-frame history.
void once_every_draw();
// 0x005BE7F0: once a frame: the frame length from the history (each entry 0-4 vblanks; outside 0-10 counts
// as 1; at least 1), the uncapped one, and the slow motion asked for since the last frame.
void update_frame_length();
// 0x005BE920: wait for the next vblank, running the idle function (if any) meanwhile.
void wait_for_vblank();

// 0x005BE8E0 / 0x005BE8D0: vblanks in total; frames rendered.
uint64_t fetch_retrace_count();
uint64_t get_draw_frame();
// 0x005BE970: milliseconds since Reset (from the low 32 bits of the vblank count).
uint32_t get_time();
// NOT ORIGINAL - the tick's own milliseconds (ticks x 1000 / ticks a second): get_time at 60; at another rate get_time
// is the 60 Hz grid's (the frame the tick ends in) and this the tick's - for what is drawn between ticks
// (render_pc/frame_interp.cpp).
uint32_t get_tick_time();
// 0x005BE910: the uncapped average frame length, in seconds.
double get_frame_seconds();
// 0x005BEAA0: slow motion times a tick (the original's nominal 1/60 s frame; not original: the tick rate's tick -
// engine/sys/tick_rate.h).
double get_slomo_frame_seconds();
// 0x005BE8F0 / 0x005BE900: slow motion (1 = normal speed); a new value takes effect at the next frame.
float get_time_scale();
void set_slomo(float slomo);

// NOT ORIGINAL - the vblanks since Reset (what get_time counts), read and set: a demo starts its run at the time it
// was recorded at (devtools/demo.h). Setting moves the last frames' vblanks along with it, so the frame history (and
// the frame length) stays as it was.
uint64_t vblanks_since_reset();
void set_vblanks_since_reset(uint64_t vblanks);

// NOT ORIGINAL - the 60 Hz grid (engine/sys/tick_rate.h, docs/design/tick-rate.md "The 60 Hz grid"): what the original
// does once a frame (truncated milliseconds of goal and effect timers, frame counters, script waits) happens once each
// 1/60 s, in the tick that starts the 1/60 s frame - that tick begins with the state the 60 Hz run's tick for the frame
// begins with (the scripts run before the objects in a tick) - so at every 1/60 s boundary it holds what the 60 Hz run
// holds. At 60 every tick is a frame.
// grid_frames: the 1/60 s frames that start in this tick (0 or 1 - always 1 at 60).
int grid_frames();
// This tick ends a 1/60 s frame (always at 60): what the original keeps for "this frame" (a flag set for the rest of a
// tick) keeps it until then.
bool grid_frame_ends();
// Set while the front end runs the spawned scripts and the game flow's - at another rate in the tick that ends a 1/60 s
// frame (they run after the objects in a tick, so the 60 Hz run's do at the frame's end): their waits step then, the
// objects' own scripts' in the tick that starts it (QbScript::update_wait). Never set at 60.
bool& script_frame_end_pass();
// The 1/60 s frame this tick is part of (the vblanks themselves at 60).
uint64_t grid_frame_of_tick();
// The current tick ends 1/60 s frame `frame` (a demo played at another rate than it was recorded at puts the clock where
// the recording's was: devtools/demo.cpp). Nothing at 60.
void anchor_grid(uint64_t frame);
// The original's step, whatever the rate: slow motion times 1.0F / 60 (get_slomo_frame_seconds at 60, to the bit) - what
// counts on the grid adds it each 1/60 s.
double get_slomo_grid_seconds();

// 0x005BE6F0: the CPU's time stamp counter.
uint64_t read_cpu_time_stamp();
// 0x005BE7C0: sets the two flags at 0x007D1EE8 / 0x007D1EE9 (meaning not known yet).
void change_bits();

} // namespace frame_clock
