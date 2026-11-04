#pragma once

// NOT ORIGINAL - the game's answers to the input's questions (engine/pad_input/game_hooks.h): the front end, the
// profile roster, the park editor, THAW's BMX. Installed once from app/main.cpp, before any of the original's code
// runs.
namespace sio {

void install_game_hooks();

} // namespace sio
