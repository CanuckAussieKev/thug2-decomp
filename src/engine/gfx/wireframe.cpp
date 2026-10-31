// The wireframe render mode (engine/gfx/wireframe.h).

#include "engine/gfx/wireframe.h"

#include <d3d9.h>

#include "engine/gfx/device.h"
#include "engine/gfx/engine.h"

namespace render::wireframe {

namespace {

bool s_wire; // the fill set to wireframe for this frame's 3D

} // namespace

int render_mode() { return g_render_mode; }

void begin_world()
{
    s_wire = render_mode() != 0;
    if (s_wire)
        set_render_state(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
}

void end_world()
{
    if (s_wire)
        set_render_state(D3DRS_FILLMODE, D3DFILL_SOLID);
    s_wire = false;
}

} // namespace render::Wireframe
