#pragma once

// NOT ORIGINAL - the game's answers to the system code's questions (engine/sys/game_hooks.h): the online session,
// THUGPRO's position scale, the skate module, the front end. Installed once from app/main.cpp, before any of the
// original's code runs.
namespace sys {

void install_game_hooks();

} // namespace sys
