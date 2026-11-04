#pragma once

// NOT ORIGINAL - the game's answers to the object framework's questions (engine/objects/game_hooks.h): the skate
// module, the front end, the park editor, the skater's components, the projectiles, the trick objects, THAW's BMX.
// Installed once from app/main.cpp, before any of the original's code runs.
namespace objects {

void install_game_hooks();

} // namespace objects
