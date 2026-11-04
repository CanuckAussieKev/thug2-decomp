// NOT ORIGINAL - the script runtime's state its files share: the script cache's pools and store, the permanent strings, the stored randoms and the expression evaluator, and the script commands' level files, node list name, join flag and last omnigon broadcast (script_state.h): made on first use, never destroyed.

#include "engine/script/script_state.h"

#include "base/core/no_destroy.h"

namespace script {

ScriptState& script_state()
{
    static core::NoDestroy<ScriptState> state;
    return state.get();
}

} // namespace script
