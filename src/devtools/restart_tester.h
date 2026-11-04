#pragma once

namespace modules {
struct FrontEnd;
}

// NOT ORIGINAL - the restart tester (ours, added at the user's request; not the game's and not PartyMod's): for
// checking a level's restarts (the ported levels' especially) one by one.
//
// In a level (not paused, not in the level editor), F8 puts the skater at the level's next restart not visited yet
// (ResetSkaters node_name=<restart>, as the goals do) - the restarts in node array order - and shows
// "Restart n / N: <name>" (also logged, with the restart's position and types). Once every one has been visited it
// shows "All restarts visited"; the next press starts the round again. The visited ones are forgotten when another
// level (node array) loads.
//
// thug2-decomp.ini [features] restart_tester=0 turns it off (default 1).
namespace frontend::restart_tester {

bool& is_switched_on();
// Once a frame from the front end's input logic (its extension).
void tick_frame(modules::FrontEnd* front_end);

} // namespace frontend::restart_tester
