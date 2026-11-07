// NOT ORIGINAL - the game exe's extensions (extensions.h): thug2-decomp.exe.

#include "app/extensions.h"

#include "app/no_editor.h"
#include "devtools/automation.h"
#include "devtools/extensions.h"
#include "engine/ext/extension.h"
#include "engine/ext/plugin.h"
#include "engine/pad_input/input_source.h"
#include "engine/sys/debug_mode.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend_extension.h"
#include "game/commands/career_complete.h"
#include "game/front_end/start_level.h"
#include "game/rider/thaw_bmx.h"

namespace app {

namespace {

// The editor's slot: the level editor plugin's extension (plugins\thug2-level-editor.dll - engine/ext/plugin.h), or,
// without it, the editor's script commands answered "no editor" (no_editor.h).
ext::Extension& editor()
{
    ext::Extension* plugin = ext::plugin_at("editor");
    return plugin != nullptr ? *plugin : no_editor_extension();
}

} // namespace

const char* exe_name() { return "thug2-decomp"; }

// The order is the call order at every extension point (engine/ext/extension.h). It is the one order that keeps every
// point's calls as they were made before the extension points: the front end's frame (the mouse menus' stand-aside
// first; the restart tester, THAW's BMX, the editor, debug mode, the start level, automation, the new front end; the
// mouse's menu events after them all), the input handlers (mouse menus, editor, new front end, screenshot key), the
// camera (editor, then the automation's camera lock), the view noted (editor, screenshot), the draw over the frame
// and the device reset (editor, new front end). The editor is a plugin, loaded first (engine/ext/plugin.h): its
// extension goes in at the editor's slot.
void register_extensions()
{
    ext::load_plugins();
    ext::add(mouse_menus::extension());
    ext::add(frontend::restart_tester::extension());
    ext::add(skater::thaw_bmx::extension());
    ext::add(editor());
    ext::add(sys::debug_mode::extension());
    ext::add(frontend::begin_level::extension());
    ext::add(frontend::automation::extension());
    ext::add(frontend::test_view::extension());
    ext::add(frontend::new_frontend::extension());
    ext::add(sys::screenshot::extension());
}

// The order the start-up has always registered them in (each makes its symbols in the script's tables in turn).
void register_added_script_commands()
{
    editor().register_script_commands();                            // (LevelEditor_Start...)
    frontend::career_complete::register_script_functions();          // (CREATE 100% CAS: Career100_Apply...)
    frontend::test_view::extension().register_script_commands();    // (TeleportSkaterStanding)
    skater::thaw_bmx::register_script_functions();                   // (ThawBmx_Available, the bike's members)
    frontend::new_frontend::extension().register_script_commands(); // (the overlay's pause items: NewFrontend_...)
}

void add_input_sources() { input::add_source(frontend::automation::input_source()); }

} // namespace app
