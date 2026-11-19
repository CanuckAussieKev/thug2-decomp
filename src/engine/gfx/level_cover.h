#pragma once

// NOT ORIGINAL - (round LS2) a level's first frames under the loading screen (render_pc/load_cover.h keeps it up over
// them until the level is ready to be seen): what the rest of the engine reads of it.
//
// The player gets control on the first frame shown, not before: while the cover is up the main loop holds the
// simulation (engine/sys/mlp.cpp - the loop's held pace, as the demo viewer's pause: no clock counted, no input read, no
// task run, no simulation step of the renderer's - only the frame drawn again, as a render-only frame draws it). The
// original's state at its own hide moment (HideLoadingScreen) is where the simulation waits: its next step runs on the
// loop after the level is shown - the same steps in the same order, only later in wall-clock time.
//
// The one exception is what the simulation itself still owes the level at that moment: the time of day's clock
// (day_clock.h) sets the level's state on the step after the hide, and the level's own scripts for it
// (tod_manager_state_transition: night geometry with collision, trigger boxes, fake lights) run over the next few steps
// - the simulation runs, under the cover, until they have ended (day_clock.h light_ready), and is held from there. With
// the time of day the level's own, there is nothing owed: held from the hide.
//
// Not in a network game: the other players' simulations run on (their updates, the session's clocks and time-outs), so
// ours does too - the cover still hides the frames that aren't ready.
//
// No input while covered (engine/ext/extension.h game_input_blocked): the steps that do run under the cover - the time
// of day's owed ones, a network game's - read no key or controller, as a player who hasn't touched them yet.
namespace render::level_cover {

// render_pc/load_cover's: the level's first frames covered (from the original's hide until the first ready frame).
void set_covered(bool covered);
// Whether the frame being drawn is covered: a view not shown yet - the camera's eye starts at its target on every one of
// them (gpu_vulkan/vulkan_hdr.cpp's meter), as at a cut.
[[nodiscard]] bool covered();
// Whether the main loop holds this loop's simulation (asked once a loop, at its start): covered, not a network game,
// and nothing owed by the simulation (above).
[[nodiscard]] bool holds_simulation();

// The rule alone (the check's): whether a loop with these facts is held.
struct Facts {
    bool covered = false;
    bool net_game = false;
    bool owed = false; // the level's own time-of-day scripts still running (day_clock.h light_ready false)
};
[[nodiscard]] constexpr bool holds(const Facts& facts) { return facts.covered && !facts.net_game && !facts.owed; }

} // namespace render::level_cover
