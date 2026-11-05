#pragma once

// NOT ORIGINAL - the game's answers to the graphics' questions (engine/gfx/game_hooks.h): the skate module, the game
// mode, the front end, the park editor. Installed once from app/main.cpp, before any of the original's code runs.
namespace render {

void install_game_hooks();

} // namespace render
