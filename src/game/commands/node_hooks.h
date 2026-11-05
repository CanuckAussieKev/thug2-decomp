#pragma once

// NOT ORIGINAL - gives the engine's time of day and colour cycles the node commands (engine/script/node_hooks.h),
// set once at start-up before any of the original's code runs.
namespace commands {

void install_node_hooks();

} // namespace commands
