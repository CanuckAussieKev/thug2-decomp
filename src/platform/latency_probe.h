#pragma once

// NOT ORIGINAL - [debug] latency_probe=1: input-to-display latency measured inside the game, without a person or a
// camera (docs/design/simulation-rate.md "Input latency"). A thread pushes a stand-in input event into SDL's queue at
// random moments (every 37 - 113 ms; SDL stamps it with its time, as it stamps a key or a pad button); the main loop
// tells the probe when the event was handled (the event pump), which simulation step read it (the first step that starts
// after), and each frame it presents with the step it shows and how far between the step before and that one it is
// drawn (frame_blend's alpha). Every 300 probes the log gets a `latency:` line - the averages and 95th percentiles of
//   - event to step: how long the input waited to be read by the simulation (the sampling delay),
//   - event to first shown: to the end of the first present that shows any of the step's result (the step's own frame,
//     or with max_fps the first frame blended towards it),
//   - event to fully shown: to the first present that shows the step's state as it is (alpha 1, or a frame of the next
//     step - the interpolation's delay).
// Presents are timed when the present call returns: the display's own queue and scan-out come on top (Reflex's
// markers measure those). Nothing here changes what the game does: the event's type is one of SDL's user events that
// no handler reads.
#include <cstdint>

namespace latency_probe {

// Reads [debug] latency_probe once; starts the event thread when it is on (after SDL's events are set up).
void start();
// Whether it is on.
bool on();
// The SDL event pump's handler (registered by start): notes when a probe event was handled.
void note_event(const void* sdl_event);
// A simulation step starts (the main loop's, after its input is read): the probes handled before it are its.
void note_step(uint64_t step);
// A frame was presented showing `step` blended by `alpha` (0: the step before, 1: this one) - `blended` false: drawn
// as the step left it.
void note_present(uint64_t step, float alpha, bool blended);
// Stops the event thread (the main loop's end).
void stop();

} // namespace latency_probe
