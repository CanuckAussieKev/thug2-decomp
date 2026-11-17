#pragma once

#include <array>

namespace objects {

// NOT ORIGINAL - whether objects are suspended, animated less often and hidden by their distance to the camera (the
// original's dormancy: suspend_component.h's suspend_task, wants_animating, refresh_shown_state) - only in a net game
// (online or LAN), where it stays the original's for fairness with original players. Elsewhere every object is updated
// and animated on every step whatever the camera, the window or the settings (distance 0, never far: the simulation the
// same for every player and every replay), and only the drawing culls - by its view and lod_dist2
// (render_pc/draw_range.h). The decision (2026-11-17): peds, traffic and most animated level objects are
// AbsentInNetGames anyway (docs/design/simulation-rate.md "Objects' dormancy").
bool dormancy_by_distance();
// NOT ORIGINAL - the simulation check's guard of the net-game rule (tools/game/sim_check.ts, sim_reference/net): with
// it on, dormancy_by_distance() is true outside a net game too, so a demo plays under the original's dormancy without
// going online. Set once at start-up by devtools/demo.cpp, only for a command-line demo playback (-demo <name>
// -demo-net-rule) in a test workspace (platform::test_workspace); never by the main install's play. In a net game it
// changes nothing (the rule is on there anyway).
void force_net_dormancy(bool on);
[[nodiscard]] bool net_dormancy_forced();
// NOT ORIGINAL - the animation LOD counters (frames, interleave 2 and 4) for the simulation check's state hash.
std::array<int, 3> suspend_counters();

} // namespace objects
