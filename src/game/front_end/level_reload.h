#pragma once

#include <string>

namespace ext {
class Extension;
}

// NOT ORIGINAL - (round CS) the level loaded again where the player stands: what a setting read as a level loads
// (engine/config/settings_model.h Apply::kReload - smooth curves, building relief, displacement) does when it is changed
// while the game runs (engine/config/settings_live.h: the console's `set`, the Options screen). The game's own load:
// the level select's level_select_change_level with the level now loaded (its load script) - the loading screen, every
// mesh, texture and script of the level made again - in the game mode it is in; once the level is up (the skater
// there, not paused, a second settled) the skater is put back where he stood, standing, facing the same way (as
// -at / start_at does it: game/rider/test_view.h jump_to).
//
// It starts the level's run again: the score, the goals, the peds and the cars, the time of day's transitions and
// everything the level's scripts made. Offline only: refused in a network game (every player shares the level), with
// the level editor open (its own state is the level's), in the front end and while a level loads. A change asked for
// while the game is paused (the Options screen in the pause menu) loads the level once the game is resumed; several
// changes, one load.
namespace frontend::level_reload {

// Asked for (settings_live's reloader - app/ installs it): true when the level will load again; false - `why` - when it
// can't (above).
bool request(std::string* why);
// Whether a load is waiting for the game to be resumed, or the skater for the level to be up.
bool pending();

// Its frame (an extension: app/ registers it).
ext::Extension& extension();

} // namespace frontend::level_reload
