// NOT ORIGINAL - the level editor's calls into the Direct3D 9 backend (editor_support.h).

#include "render_d3d9/editor_support.h"

#include <windows.h>

#include <d3d9.h>

#include <array>
#include <cstdint>
#include <span>

#include "base/core/checked_index.h"
#include "engine/gfx/render_device.h"
#include "render_d3d9/d3d9_state.h"
#include "render_d3d9/render_state.h"

namespace render::editor_support {

namespace {

uint32_t get_stage_state(int type)
{
    DWORD value = 0;
    d3d9::state().d3d_device()->GetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(type), &value);
    return value;
}
void set_stage_state(int type, uint32_t value)
{
    d3d9::state().d3d_device()->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(type), value);
}

} // namespace

void draw_screen_lines(std::span<const ScreenLineVertex> vertices)
{
    const int count = static_cast<int>(vertices.size());
    const ScreenLineVertex* line_data = vertices.data();
    // The renderer's own (cached) state: no texture, pre-transformed vertices with a colour.
    set_texture(0, nullptr);
    set_vertex_format(kScreenVertexFormat); // XYZRHW | DIFFUSE | TEX1
    set_pixel_shader(nullptr, nullptr);     // no pixel shader
    // Stage 0 takes the vertex colour (put back after: the renderer doesn't know of this).
    const std::array<int, 4> states = {{1, 2, 4, 5}};      // colour op / arg 1, alpha op / arg 1
    const std::array<uint32_t, 4> values = {{2, 0, 2, 0}}; // select arg 1, diffuse
    std::array<uint32_t, 4> saved{};
    for (int i = 0; i < 4; ++i) {
        core::at(saved, i) = get_stage_state(core::at(states, i));
        set_stage_state(core::at(states, i), core::at(values, i));
    }
    d3d9::state().d3d_device()->DrawPrimitiveUP(D3DPT_LINELIST, static_cast<unsigned int>(count / 2), line_data,
                                                sizeof(ScreenLineVertex));
    for (int i = 0; i < 4; ++i) {
        set_stage_state(core::at(states, i), core::at(saved, i));
    }
}

} // namespace render::editor_support
