// NOT ORIGINAL - the views' state (view_setup.h): made on first use, never destroyed.

#include "engine/gfx/view_setup.h"

#include "engine/core/no_destroy.h"

namespace render {

ViewSetup& view_setup()
{
    static core::NoDestroy<ViewSetup> state;
    return state.get();
}

} // namespace render
