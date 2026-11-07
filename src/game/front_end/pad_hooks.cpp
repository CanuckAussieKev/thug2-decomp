// The game's hooks for the input (see pad_hooks.h). Each is the code the engine file called before (the same calls in
// the same order).

#include "game/front_end/pad_hooks.h"

#include "engine/pad_input/game_hooks.h"
#include "game/front_end/front_end.h"
#include "game/park_editing/park_editor.h"
#include "game/online/gamenet_manager.h"
#include "game/online/net_game_state.h"
#include "game/rider/thaw_bmx.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/skate/profile_manager.h"

namespace sio {

namespace {

bool menus_active() { return modules::menus_active(); }
int menu_port() { return objects::profile_roster()->cursor_pos(); }

bool editing_park()
{
    const park_editing::ParkBuilderMode* editor = park_editing::ParkBuilderMode::sole();
    return editor != nullptr && editor->editor_state() == park_editing::ParkBuilderMode::kStateEdit;
}

bool half_stick_key() { return skater::thaw_bmx::half_stick_key(); }

// (InSplitScreenGame's test, net_state_cfuncs.cpp; no before the game mode and the online hub are made)
bool split_screen_game()
{
    online::Hub* hub = online::online_hub();
    return goals::game_mode() != nullptr && hub != nullptr && modules::Skate::is_multiplayer() != 0 &&
           !hub->in_net_game();
}

} // namespace

void install_game_hooks()
{
    set_game_hooks({.menus_active = &menus_active,
                    .menu_port = &menu_port,
                    .park_editing = &editing_park,
                    .half_stick_key = &half_stick_key,
                    .split_screen_game = &split_screen_game});
}

} // namespace sio
