// NOT ORIGINAL - the DirectInput side's state its files share (dinput_state.h): made on first use, never destroyed.

#include "input_dinput/dinput_state.h"

#include "base/core/no_destroy.h"

namespace os_layer {

DirectInputState& direct_input_state()
{
    static core::NoDestroy<DirectInputState> state;
    return state.get();
}

} // namespace os_layer
