// The wireframe render mode (render_d3d9/wireframe.h).

#include "render_d3d9/wireframe.h"

#include <d3d9.h>

#include "render_d3d9/device.h"
#include "engine/gfx/engine_parts.h"

namespace render::wireframe {

namespace {

bool& wire()
{
    static bool wire{}; // the fill set to wireframe for this frame's 3D
    return wire;
}

} // namespace

int render_mode() { return render::engine_parts().render_mode(); }

void begin_world()
{
    wire() = engine_parts().render_mode() != 0;
    if (wire()) {
        set_render_state(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    }
}

void end_world()
{
    if (wire()) {
        set_render_state(D3DRS_FILLMODE, D3DFILL_SOLID);
    }
    wire() = false;
}

} // namespace render::wireframe
