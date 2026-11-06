// NOT ORIGINAL - the renderer's Direct3D 9 objects (d3d9_state.h): made on first use, never destroyed.

#include "gpu_d3d9/d3d9_state.h"

#include "base/core/no_destroy.h"

namespace render::d3d9 {

State& state()
{
    static core::NoDestroy<State> objects;
    return objects.get();
}

} // namespace render::d3d9
