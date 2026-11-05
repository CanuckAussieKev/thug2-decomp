// NOT ORIGINAL - the graphics options the input set-up clears and the registry loader reads (options_state.h): made on
// first use, never destroyed.

#include "engine/pad_input/options_state.h"

#include "base/core/no_destroy.h"

namespace os_layer {

OptionsState& options_state()
{
    static core::NoDestroy<OptionsState> state;
    return state.get();
}

} // namespace os_layer
