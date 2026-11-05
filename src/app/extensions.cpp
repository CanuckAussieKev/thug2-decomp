// NOT ORIGINAL - the game exe's extensions (extensions.h).

#include "app/extensions.h"

#include "devtools/extensions.h"
#include "editor/level_editor_extension.h"
#include "engine/ext/extension.h"
#include "engine/sys/debug_mode.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend_extension.h"
#include "game/commands/career_complete.h"
#include "game/front_end/start_level.h"
#include "game/rider/thaw_bmx.h"

namespace app {

// The order is the call order at every extension point (engine/ext/extension.h). It is the one order that keeps every
// point's calls as they were made before the extension points: the front end's frame (the mouse menus' stand-aside
// first; the restart tester, THAW's BMX, the editor, debug mode, the start level, automation, the new front end; the
// mouse's menu events after them all), the input handlers (mouse menus, editor, new front end, screenshot key), the
// camera (editor, then the automation's camera lock), the view noted (editor, screenshot), the draw over the frame
// and the device reset (editor, new front end). An exe of the editor alone would register only the editor (and the
// game features it wants: THAW's BMX, debug mode).
void register_extensions()
{
    ext::add(mouse_menus::extension());
    ext::add(frontend::restart_tester::extension());
    ext::add(skater::thaw_bmx::extension());
    ext::add(frontend::level_editor::extension());
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
    frontend::level_editor::extension().register_script_commands(); // (LevelEditor_Start...)
    frontend::career_complete::register_script_functions();          // (CREATE 100% CAS: Career100_Apply...)
    frontend::test_view::extension().register_script_commands();    // (TeleportSkaterStanding)
    skater::thaw_bmx::register_script_functions();                   // (ThawBmx_Available, the bike's members)
    frontend::new_frontend::extension().register_script_commands(); // (the overlay's pause items: NewFrontend_...)
}

} // namespace app
