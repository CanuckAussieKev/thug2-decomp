#pragma once

// NOT ORIGINAL - (round CS) the settings' owners (settings_appliers.cpp): an applier for each owner the settings model
// names (engine/config/settings_model.h Owner) and the level reload (game/front_end/level_reload.h), installed in
// engine/config/settings_live.h - what the console's `set`, the automation channel and the Options screen change a
// setting through. WinMain, with the extensions (app/extensions.cpp register_extensions); missing ones logged.
namespace app {

void install_setting_appliers();

} // namespace app
