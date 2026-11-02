// NOT ORIGINAL - the 2D layer's state (screen_2d.h): made on first use, never destroyed.

#include "engine/gfx/screen_2d.h"

#include "engine/core/no_destroy.h"
#include "engine/gfx/engine_texture.h" // IWYU pragma: keep - the loading picture owner's complete type

namespace render {

Screen2d& screen_2d()
{
    static core::NoDestroy<Screen2d> state;
    return state.get();
}

} // namespace render
