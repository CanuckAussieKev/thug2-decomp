// NOT ORIGINAL - the mesh pass's state (mesh_pass.h): made on first use, never destroyed.

#include "engine/gfx/mesh_pass.h"

#include "base/core/no_destroy.h"

namespace render {

MeshPass& mesh_pass()
{
    static core::NoDestroy<MeshPass> state;
    return state.get();
}

} // namespace render
