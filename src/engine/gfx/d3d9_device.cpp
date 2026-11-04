// NOT ORIGINAL - the Direct3D 9 backend's render::Device (d3d9_device.h). Each method calls the platform layer's own
// function, or does what the engine side did with Direct3D at that place, unchanged: the same calls in the same order.

#include "engine/gfx/d3d9_device.h"

#include <d3d9.h>

#include "base/core/no_destroy.h"
#include "engine/gfx/bitmap_texture.h"
#include "engine/gfx/d3d9_state.h"
#include "engine/gfx/device.h"
#include "engine/gfx/device_d3d.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/render_device.h"
#include "engine/gfx/sub_mesh.h"

namespace render::d3d9 {

namespace {

class D3d9Device final : public Device {
public:
    // --- the frame ---
    void start_engine() override { render::start_engine(); }
    void begin_frame_draw() override { render::begin_frame_draw(); }
    void draw_world() override { render::draw_world(); }
    void finish_rendering() override { render::finish_rendering(); }

    // --- a lost device (the main loop's 0x0044EE90) ---
    bool can_reset() override { return state().d3d_device()->TestCooperativeLevel() != D3DERR_DEVICELOST; }
    void release_device_objects() override
    {
        release_render_target_textures();
        release_buffers();
        release_render_targets();
    }
    void restore_device_objects() override
    {
        restore_render_target_textures();
        recreate_buffers();
        create_render_targets();
    }
    bool reset() override { return state().d3d_device()->Reset(&present_parameters()) >= 0; }
};

} // namespace

Device& device()
{
    static core::NoDestroy<D3d9Device> made;
    return made.get();
}

} // namespace render::d3d9
