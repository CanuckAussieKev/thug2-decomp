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

// 0x005BE6F0: the CPU's time stamp counter.
uint64_t read_cpu_time_stamp();
// 0x005BE7C0: sets the two flags at 0x007D1EE8 / 0x007D1EE9 (meaning not known yet).
void change_bits();

} // namespace frame_clock
