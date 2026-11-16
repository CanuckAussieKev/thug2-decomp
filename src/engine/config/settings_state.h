#pragma once

// NOT ORIGINAL - the settings model at run time (settings_model.h): this machine's capabilities, and the resolutions the
// game reads - what it runs (running) and what the ini says now (current, the Options screen's).
//
// The capabilities are set once at start-up, before anything reads a setting (app/renderer_choice.cpp: the Vulkan
// device's probe, the build, the data found in the game folder); the display's refresh rate when the window is made.
// running() is the ini as it is now for the settings put to use at once, and as it was when the game started for the
// ones read at start-up only (Apply::kRelaunch: the device was made for them) - so a setting changed for the next start
// never changes what the running renderer is told.

#include <string>
#include <utility>
#include <vector>

#include "engine/config/settings_model.h"

namespace config {

void set_capabilities(const Capabilities& caps);
[[nodiscard]] const Capabilities& capabilities();
void set_display_refresh(int hz);
// The level now (its fog): the rules about a level's facts (the haze: levels without fog). Unknown until set.
void set_level_facts(const LevelFacts& level);
// (the offline relit data) What the level now loaded is lit from (render_pc/rt_relit_world.cpp, each new level); true
// when it changed.
bool set_level_light(LevelLight light);
// (added by the time of day's clock) whether the game now is a network session (engine/gfx/day_clock.cpp says, each
// step it changes); true when it changed.
bool set_network_game(bool network);
// (added by the snow) whether the level now is outdoors (has a sky): the snow's rule (render_pc/snow_world.cpp says);
// true when it changed (the caller puts what moved to use)
bool set_level_outdoor(bool outdoor);

// (the relit bake - render_pc/relit_bake.h) Values put over the ini's for the whole run (IniValues' "section/key"
// slots): a batch run's own settings, the user's ini left as it is. Set before anything reads a setting.
void set_overrides(IniValues values);

// The inputs and the resolution of the ini as it is on disk now (read each call).
[[nodiscard]] Inputs current_inputs();
[[nodiscard]] Resolution current();

// What the game runs (made at the first call): see above.
[[nodiscard]] const Resolution& running();
[[nodiscard]] const Inputs& running_inputs();
// The ini read again (a setting changed while the game runs); the start-up ones stay as they were.
void refresh_running();
// The running inputs with ([Graphics] key, value) pairs put over them (a split comparison's side B).
[[nodiscard]] Resolution running_with(const std::vector<std::pair<std::string, std::string>>& graphics_overrides);

// One line per setting that isn't what was asked or isn't in effect, and one per conflict ("" none) - the log's.
[[nodiscard]] std::string describe(const Resolution& resolution);

} // namespace config
