// The game's hooks for the input (see pad_hooks.h). Each is the code the engine file called before (the same calls in
// the same order).

#include "game/front_end/pad_hooks.h"

#include "engine/pad_input/game_hooks.h"
#include "game/front_end/front_end.h"
#include "game/park_editing/park_editor.h"
#include "game/rider/thaw_bmx.h"
#include "game/skate/profile_manager.h"

namespace sio {

namespace {

bool menus_active() { return modules::menus_active(); }
int menu_port() { return objects::profile_roster()->cursor_pos(); }

bool editing_park()
{
    const park_editing::ParkBuilderMode* editor = park_editing::ParkBuilderMode::sole();
    return editor && editor->editor_state() == park_editing::ParkBuilderMode::kStateEdit;
}

bool half_stick_key() { return skater::thaw_bmx::half_stick_key(); }

} // namespace

void install_game_hooks()
{
    set_game_hooks({.menus_active = &menus_active,
                    .menu_port = &menu_port,
                    .park_editing = &editing_park,
                    .half_stick_key = &half_stick_key});
}

} // namespace sio
