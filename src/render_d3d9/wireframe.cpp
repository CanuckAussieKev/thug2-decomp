// The wireframe render mode (render_d3d9/wireframe.h).

#include "render_d3d9/wireframe.h"

#include "engine/gfx/gpu.h"
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
        set_render_state(GpuRenderState::kFillMode, gpu_value::kFillWireframe);
    }
}

void end_world()
{
    if (wire()) {
        set_render_state(GpuRenderState::kFillMode, gpu_value::kFillSolid);
    }
    wire() = false;
}

} // namespace render::wireframe
