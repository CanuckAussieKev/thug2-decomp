// NOT ORIGINAL - the level editor exe's extensions (extensions.h): thug2-editor.exe - the game on the engine and the game
// library with the level editor and no new front end or dev tools. Started with -edit <level> (or [features]
// edit_level=) it goes straight into the editor in that level, as the game exe does (game/front_end/start_level.h);
// without one the game's own menus (the data overlay's standard main menu) are its level picker (a level's pause menu
// has LEVEL EDITOR). Play mode skates with
// the game's physics, as the editor does in the game exe.

#include "app/extensions.h"

#include <array>

#include "editor/level_editor_extension.h"
#include "engine/ext/extension.h"
#include "engine/script/context.h"
#include "engine/script/fwd.h"
#include "engine/script/names.h"
#include "engine/sys/debug_mode.h"
#include "game/commands/career_complete.h"
#include "game/front_end/start_level.h"
#include "game/rider/test_view.h"
#include "game/rider/thaw_bmx.h"

namespace app {

namespace {

// The editor exe's own: the data overlay's front_end folder - the standard main menu's scripts (the game's own menus,
// with the SKATE NOW start level waits for: game/front_end/start_level.h) - is searched first, as the game exe searches it
// while the new front end is on.
class EditorExeExtension final : public ext::Extension {
public:
    bool uses_front_end_overlay() override { return true; }
};

// The overlay's menus ask whether an item is the new front end's (NewFrontend_Item) and open it (NewFrontend_Open):
// this exe has no new front end, so - as the game's with [features] new_frontend=0 - never.
bool cfunc_no_new_frontend(script::Struct* /*params*/, script::Context /*context*/) { return false; }

} // namespace

const char* exe_name() { return "thug2-editor"; }

// The game exe's order (extensions.cpp) without the front end's and the dev tools': THAW's BMX, the editor, debug mode,
// start level.
void register_extensions()
{
    ext::add(skater::thaw_bmx::extension());
    ext::add(frontend::level_editor::extension());
    ext::add(sys::debug_mode::extension());
    ext::add(frontend::begin_level::extension());
    static EditorExeExtension editor_exe;
    ext::add(editor_exe);
}

// The game exe's order, the new front end's commands answered "no" (above); TeleportSkaterStanding is the test views' (a game feature
// here, registered by the dev tools' extension in the game exe).
void register_added_script_commands()
{
    frontend::level_editor::extension().register_script_commands(); // (LevelEditor_Start...)
    frontend::career_complete::register_script_functions();          // (CREATE 100% CAS: Career100_Apply...)
    frontend::test_view::register_script_functions();                // (TeleportSkaterStanding)
    skater::thaw_bmx::register_script_functions();                   // (ThawBmx_Available, the bike's members)
    static const auto kNoNewFrontend = std::to_array<script::NativeCommand>({
        {.name = "NewFrontend_Item", .function = &cfunc_no_new_frontend},
        {.name = "NewFrontend_Open", .function = &cfunc_no_new_frontend},
    });
    script::install_script_functions(kNoNewFrontend); // (where the game exe registers the new front end's)
}

// (no input sources of its own: no automation channel)
void add_input_sources() {}

} // namespace app
