// NOT ORIGINAL - the game exe's extensions (extensions.h): thug2-decomp.exe.

#include "app/extensions.h"

#include "app/no_editor.h"
#include "app/settings_report.h"
#include "engine/sys/editor_mode.h"
#include "platform/app_window.h"
#include "platform/run_log.h"
#include "devtools/automation.h"
#include "devtools/compare_split.h"
#include "devtools/console.h"
#include "devtools/fps_counter.h"
#include "devtools/demo.h"
#include "devtools/extensions.h"
#include "devtools/rt_check.h"
#include "engine/ext/extension.h"
#include "engine/ext/plugin.h"
#include "engine/gfx/texture_sets.h"
#include "engine/pad_input/input_source.h"
#include "engine/sys/debug_mode.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend_extension.h"
#include "game/commands/career_complete.h"
#include "game/front_end/start_level.h"
#include "game/rider/thaw_bmx.h"
#include "render_pc/texture_set_reload.h"

namespace app {

namespace {

// The editor's slot: the level editor plugin's extension (plugins\thug2-level-editor.dll - engine/ext/plugin.h), or,
// without it, the editor's script commands answered "no editor" (no_editor.h).
ext::Extension& editor()
{
    ext::Extension* plugin = ext::plugin_at("editor");
    return plugin != nullptr ? *plugin : no_editor_extension();
}

// The editor mode (engine/sys/editor_mode.h: -editor, [features] editor_mode=1), once the plugins are loaded: on with
// the level editor's plugin - the window then an application's, "THUG2 Editor" (platform/app_window.h); without it,
// logged, the game as usual.
void choose_editor_mode()
{
    if (!sys::editor_mode::requested()) {
        return;
    }
    if (ext::plugin_at("editor") == nullptr) {
        get_log("editor mode: the level editor isn't installed (no plugins\\thug2-level-editor.dll) - the game starts as "
                "usual");
        return;
    }
    sys::editor_mode::set_on(true);
    sys::app_window::request({.title = "THUG2 Editor",
                              .placement_file = game_folder_path("thug2-decomp-editor-window.ini"),
                              .app_id = L"THUG2.Editor",
                              .display_name = L"THUG2 Editor",
                              .launcher = L"thug2-editor.exe"});
    get_log("editor mode: on - the level editor's window, no front end");
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
    choose_editor_mode();
    ext::add(mouse_menus::extension());
    ext::add(frontend::restart_tester::extension());
    ext::add(skater::thaw_bmx::extension());
    ext::add(editor());
    ext::add(sys::debug_mode::extension());
    ext::add(frontend::begin_level::extension());
    // (the split comparison before the demos: its views after the editor's, its divider and labels drawn before a demo's
    // video takes its picture - devtools/compare_split.h)
    ext::add(devtools::compare_split::extension());
    ext::add(devtools::demo::extension()); // (right after the start level: a demo starts its level at the same point of
                                           // the frame as -level does)
    ext::add(frontend::automation::extension());
    ext::add(frontend::test_view::extension());
    ext::add(frontend::new_frontend::extension());
    // (the texture set switch after the front end and automation: one they asked for is made in the same frame)
    render::texture_sets::set_reloader(&render::reload_dictionary);
    ext::add(render::texture_sets::extension());
    ext::add(sys::screenshot::extension());
    ext::add(settings_report_extension());
    ext::add(devtools::rt_check::extension());
    ext::add(devtools::fps_counter::extension()); // (over the frame; the console still over it)
    ext::add(devtools::console::extension()); // (last: drawn over everything, the others' frames)
}

// The order the start-up has always registered them in (each makes its symbols in the script's tables in turn).
void register_added_script_commands()
{
    editor().register_script_commands();                            // (LevelEditor_Start...)
    frontend::career_complete::register_script_functions();          // (CREATE 100% CAS: Career100_Apply...)
    frontend::test_view::extension().register_script_commands();    // (TeleportSkaterStanding)
    skater::thaw_bmx::register_script_functions();                   // (ThawBmx_Available, the bike's members)
    frontend::new_frontend::extension().register_script_commands(); // (the overlay's pause items: NewFrontend_...)
    devtools::demo::extension().register_script_commands();          // (the overlay's DEMOS menu: Demo_...)
}

// (the demos last: a recording takes port 0 as every other source left it; a playback replaces it)
void add_input_sources()
{
    input::add_source(frontend::automation::input_source());
    input::add_source(devtools::demo::input_source());
}

} // namespace app
