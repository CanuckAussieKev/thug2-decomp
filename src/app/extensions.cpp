// NOT ORIGINAL - the game exe's extensions (extensions.h).

#include "app/extensions.h"

#include "editor/level_editor_extension.h"
#include "engine/ext/extension.h"

namespace app {

// The order is the call order at every extension point (engine/ext/extension.h), the order the original call sites
// had before the extension points replaced them.
void register_extensions()
{
    ext::add(frontend::level_editor::extension());
}

void register_added_script_commands() {}

} // namespace app
