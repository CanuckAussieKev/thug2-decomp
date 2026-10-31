// The wireframe render mode (game/gfx/wireframe.h).

#include "game/gfx/wireframe.h"

#include <d3d9.h>

#include "game/gfx/device.h"
#include "game/gfx/engine.h"

namespace Nx::Wireframe {

namespace {

bool s_wire; // the fill set to wireframe for this frame's 3D

} // namespace

int RenderMode() { return g_renderMode; }

void BeginWorld()
{
    s_wire = RenderMode() != 0;
    if (s_wire)
        SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
}

void EndWorld()
{
    if (s_wire)
        SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    s_wire = false;
}

} // namespace Nx::Wireframe
