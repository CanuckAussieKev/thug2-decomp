// NOT ORIGINAL - the game exe's extensions (extensions.h).

#include "app/extensions.h"

#include "devtools/extensions.h"
#include "editor/level_editor_extension.h"
#include "engine/ext/extension.h"
#include "engine/sys/debug_mode.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend_extension.h"
#include "game/rider/thaw_bmx.h"

namespace app {

// The order is the call order at every extension point (engine/ext/extension.h), the order the original call sites
// had before the extension points replaced them.
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

void register_added_script_commands() {}

} // namespace app
