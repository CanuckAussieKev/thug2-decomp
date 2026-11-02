// NOT ORIGINAL - the engine's state (engine_parts.h): made on first use, never destroyed.

#include "engine/gfx/engine_parts.h"

#include "engine/core/no_destroy.h"
#include "engine/gfx/engine_imposters.h" // IWYU pragma: keep - the managers' owners' complete types
#include "engine/gfx/flexible_particle_manager.h" // IWYU pragma: keep
#include "engine/gfx/screen_effects.h" // IWYU pragma: keep
#include "engine/gfx/weather.h" // IWYU pragma: keep

namespace render {

EngineParts& engine_parts()
{
    static core::NoDestroy<EngineParts> state;
    return state.get();
}

} // namespace render
