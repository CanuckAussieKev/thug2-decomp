// NOT ORIGINAL - the level editor exe's extensions (extensions.h): thug2-editor.exe - the game on the engine and the game
// library with the level editor and no new front end or dev tools. Started with -edit <level> (or [features]
// edit_level=) it goes straight into the editor in that level, as the game exe does (game/front_end/start_level.h);
// without one the game's own menus are its level picker (a level's pause menu has LEVEL EDITOR). Play mode skates with
// the game's physics, as the editor does in the game exe.

#include "app/extensions.h"

#include "editor/level_editor_extension.h"
#include "engine/ext/extension.h"
#include "engine/sys/debug_mode.h"
#include "game/commands/career_complete.h"
#include "game/front_end/start_level.h"
#include "game/rider/test_view.h"
#include "game/rider/thaw_bmx.h"

namespace app {

const char* exe_name() { return "thug2-editor"; }

// The game exe's order (extensions.cpp) without the front end's and the dev tools': THAW's BMX, the editor, debug mode,
// start level.
void register_extensions()
{
    ext::add(skater::thaw_bmx::extension());
    ext::add(frontend::level_editor::extension());
    ext::add(sys::debug_mode::extension());
    ext::add(frontend::begin_level::extension());
}

// The game exe's order without the new front end's commands; TeleportSkaterStanding is the test views' (a game feature
// here, registered by the dev tools' extension in the game exe).
void register_added_script_commands()
{
    frontend::level_editor::extension().register_script_commands(); // (LevelEditor_Start...)
    frontend::career_complete::register_script_functions();          // (CREATE 100% CAS: Career100_Apply...)
    frontend::test_view::register_script_functions();                // (TeleportSkaterStanding)
    skater::thaw_bmx::register_script_functions();                   // (ThawBmx_Available, the bike's members)
}

// (no input sources of its own: no automation channel)
void add_input_sources() {}

} // namespace app
