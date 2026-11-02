// NOT ORIGINAL - the platform layer's state its files share: the graphics options and draw distance read from the registry, DirectInput, the joysticks with their rumble effects and force resolutions, and the joysticks' last two states (platform_state.h): made on first use, never destroyed.

#include "platform/platform_state.h"

#include "engine/core/no_destroy.h"

namespace os_layer {

PlatformState& platform_state()
{
    static core::NoDestroy<PlatformState> state;
    return state.get();
}

} // namespace os_layer
