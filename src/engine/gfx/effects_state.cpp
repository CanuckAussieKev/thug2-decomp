// NOT ORIGINAL - the effects' state (effects_state.h): made on first use, never destroyed.

#include "engine/gfx/effects_state.h"

#include "base/core/no_destroy.h"

namespace render {

EffectsState& effects_state()
{
    static core::NoDestroy<EffectsState> state;
    return state.get();
}

} // namespace render
