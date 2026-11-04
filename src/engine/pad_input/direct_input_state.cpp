// NOT ORIGINAL - the DirectInput side's state its files share: the graphics options and draw distance read from the registry, DirectInput, the joysticks with their rumble effects and force resolutions, and the joysticks' last two states (direct_input_state.h): made on first use, never destroyed.

#include "engine/pad_input/direct_input_state.h"

#include "base/core/no_destroy.h"

namespace os_layer {

PlatformState& platform_state()
{
    static core::NoDestroy<PlatformState> state;
    return state.get();
}

} // namespace os_layer
