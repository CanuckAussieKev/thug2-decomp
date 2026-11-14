#pragma once

// NOT ORIGINAL - frame rates above 60 without touching the game ([Graphics] max_fps, present_mode). The game's simulation
// runs in ticks of 1/60 s on the wall clock, as it always has (frame_loop::Hub::get_main_loop: the logic tasks, the
// clock's vblank count - one per tick, so frame_length stays what it was -, the effects' tick in render::engine's
// render_frame). Between two ticks the main loop draws render-only frames: the world drawn again with every instance's
// transform, its bone palette and the cameras blended between the tick before and this one (render_pc/frame_interp.h),
// by alpha = the time since this tick was due / the tick's length. So what is shown lags the simulation by up to one tick
// (1/60 s) - the price of interpolation - and is never a guess (no extrapolation: nothing is drawn where the game didn't
// put it). A render-only frame runs no task, counts no vblank, asks for no input and leaves the game's random sequence
// as it found it.
//
//   max_fps=0          (the default) the original: one frame per tick, drawn as the tick left it - nothing here runs.
//   max_fps=N          up to N frames a second (60 - 1000): each tick's share of them (N / 60, the fraction carried to
//                      the next tick) spread evenly across the tick.
//   max_fps=uncapped   (or -1) as many as fit before the next tick is due.
//   present_mode=vsync (the default: FIFO, presents wait for the display - the original's), mailbox (the newest frame
//                      shown at the display's refresh, nothing waits) or immediate (shown at once, may tear) - each only
//                      where the device offers it, else vsync. With vsync the display's refresh caps the rate too.
//
// Where frame generation (DLSS Frame Generation, FSR 3) would go: the frame's present is render::engine::end_render
// (mlp.cpp end_scene) and the pacing is the main loop's extra-frame loop (mlp.cpp draw_extra_frames) with this file's
// schedule; the frame's motion vectors and camera are the upscaler's (gpu_vulkan/vulkan_motion.h), which already see
// every render-only frame as a frame of its own.
#include <cstdint>

namespace frame_pacing {

inline constexpr int kOriginal = 0;  // max_fps=0: one frame a tick
inline constexpr int kUncapped = -1; // max_fps=uncapped
inline constexpr int kTicksPerSecond = 60;
inline constexpr int kLowestCap = 60;
inline constexpr int kHighestCap = 1000;

enum class PresentMode : uint8_t { kVsync, kMailbox, kImmediate };

// [Graphics] max_fps as written: "" / "0" / anything unreadable -> 0 (the original); "uncapped" / "unlimited" / "-1" (any
// negative) -> kUncapped; N -> N clamped to 60 - 1000.
int parse_max_fps(const char* text);
// [Graphics] present_mode: "mailbox", "immediate" (any case); anything else vsync.
PresentMode parse_present_mode(const char* text);
const char* present_mode_name(PresentMode mode);

// The frames to draw in a tick (its own frame and the render-only ones after it, at least 1) for a cap, the fraction
// of a frame left over carried from tick to tick (`carry`, 0 - 1, updated) so that the frames a second come out at the
// cap. Uncapped: a large number (as many as fit - the caller stops at the next tick).
int frames_this_tick(int max_fps, double* carry);
inline constexpr int kUncappedFrames = 1000;

// When a tick's frame `index` (1 .. frames - 1; 0 is the tick's own) is due: evenly across the tick, from the time the
// tick was due (`tick_time`, `tick_length` performance-counter ticks).
uint64_t frame_due(uint64_t tick_time, uint64_t tick_length, int index, int frames);
// Whether a render-only frame started at `now` that takes about `cost` would end by the next tick (`next_tick`).
bool frame_fits(uint64_t now, uint64_t cost, uint64_t next_tick);
// The smoothed cost after a render-only frame that took `took`: a quarter of the new time, which counts as at most one
// tick (`tick_length`) - a hitch (a texture load, the window moved) mustn't stop the frames for long.
uint64_t cost_after_frame(uint64_t cost, uint64_t took, uint64_t tick_length);
// The smoothed cost after a tick whose frames it refused: an eighth less, so the frames are tried again - the estimate
// only learns from frames drawn, and without this a cost once too high would keep every frame out for good.
uint64_t cost_after_refusal(uint64_t cost);
// The blend between the tick before and this one for a frame drawn at `now`: the time since this tick was due over the
// tick's length, 0 - 1.
float blend_alpha(uint64_t now, uint64_t tick_time, uint64_t tick_length);
// The milliseconds the frame shows (the clock's time, frame_clock::get_time, is this tick's): `now_ms` less the part of a
// tick it is behind (1 - alpha ticks).
uint32_t shown_time_ms(uint32_t now_ms, float alpha);

} // namespace frame_pacing
