// NOT ORIGINAL - the lights' state (light_sets.h): made on first use, never destroyed.

#include "engine/gfx/light_sets.h"

#include "base/core/no_destroy.h"

namespace render {

LightSets& light_sets()
{
    static core::NoDestroy<LightSets> state;
    return state.get();
}

} // namespace render
