// NOT ORIGINAL - the renderer's state (renderer.h): made on first use, never destroyed.

#include "engine/gfx/renderer.h"

#include "engine/core/no_destroy.h"

namespace render {

Renderer& renderer()
{
    static core::NoDestroy<Renderer> state;
    return state.get();
}

} // namespace render
