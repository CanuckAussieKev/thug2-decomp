// The PC viewport: see pc_viewport.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include <cstdint>

#include "engine/gfx/pc_viewport.h"
#include "platform/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include "engine/core/mem.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/viewport.h"
#include "engine/maths/vec4.h"
#include "platform/platform_d3d.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/checked_index.h"

namespace render {

std::array<uint8_t, 1024> g_occlusion_slots{{1}};
namespace {
uint32_t g_occlusion_pixels;
// The queries' strips: four pre-transformed corners (x, y, z, rhw, colour) for a rectangle, four points (x, y, z,
// colour) for a point.
struct QueryRectVertex {
    float x, y, z, rhw;
    uint32_t colour;
};
struct QueryPointVertex {
    float x, y, z;
    uint32_t colour;
};
std::array<QueryRectVertex, 4> g_query_rect_strip;
std::array<QueryPointVertex, 4> g_query_point_strip;

float g_virtual_screen_height{448.0f};
float g_virtual_screen_width{640.0f};
} // namespace
float g_scale_2d_in_3d_numerator{300.0f};
float g_scale_2d_in_3d_max{3.0f};
float g_scale_2d_in_3d_min;

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
bool radeon8500() { return g_radeon8500; }

void xform(float* out, const float* v, const void* matrix) { d3dx::vec4_transform(out, v, matrix); }

// The queries' states: colour writes off, no z writes, no texture, the vertex colour's shader.
void begin_query_draw(uint32_t fvf)
{
    choose_blend_mode(0);
    set_texture(0, nullptr);
    set_pixel_shader(core::at(g_pixel_shaders, kPixelShader94c), nullptr);
    set_vertex_format(fvf);
    os_layer::begin_occlusion_query();
}
void end_query_draw(uint8_t query)
{
    os_layer::end_occlusion_query(query);
    input_port()->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    change_phase(kZWrite, 1);
}

} // namespace

// 0x004CB860
gsl::owner<PcViewport*> create_viewport(const std::array<float, 4>* rect, gfx::ViewCam* camera)
{
    return new PcViewport(rect, camera);
}

// 0x004CB940
float PcViewport::project_to_screen(const maths::Vec4* point, float* x, float* y, int* depth)
{
    std::array<float, 4> v = {{point->x, point->y, point->z, 1.0f}};
    std::array<float, 4> view{};
    std::array<float, 4> clip{};
    xform(view.data(), v.data(), &g_view_matrix);
    xform(clip.data(), view.data(), &g_projection_matrix);
    if (core::at(clip, 3) != 0.0f) {
        const double inverse = 1.0f / static_cast<double>(core::at(clip, 3));
        core::at(clip, 0) = static_cast<float>(core::at(clip, 0) * inverse);
        core::at(clip, 1) = static_cast<float>(core::at(clip, 1) * inverse);
        core::at(clip, 2) = static_cast<float>(inverse * core::at(clip, 2));
    }
    *x = static_cast<float>((static_cast<double>(core::at(clip, 0)) + 1.0f) * 0.5f * g_virtual_screen_width);
    *y = static_cast<float>((1.0f - static_cast<double>(core::at(clip, 1))) * 0.5f * g_virtual_screen_height);
    if (!(core::at(clip, 2) < 1.0f))
        return -1.0f;
    const float most = g_scale_2d_in_3d_max;
    const float least = g_scale_2d_in_3d_min;
    double factor = core::at(view, 2) == 0.0f ? most : -1.0f / static_cast<double>(core::at(view, 2)) * g_scale_2d_in_3d_numerator;
    if (factor > most)
        factor = most;
    else if (factor < least)
        factor = least;
    *depth = static_cast<int>(static_cast<double>(core::at(clip, 2)) * 65536.0f);
    return static_cast<float>(factor);
}

// 0x004CBC00
uint8_t PcViewport::query_point(const maths::Vec4* point, int /*unused*/, uint8_t query)
{
    if (radeon8500() || !query)
        return 0;
    change_phase(kZWrite, 0);
    input_port()->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    begin_query_draw(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    for (QueryPointVertex& vertex : g_query_point_strip)
        vertex = {.x = point->x, .y = point->y, .z = point->z, .colour = 0xFFFFFFFF};
    input_port()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, g_query_point_strip.data(), sizeof(QueryPointVertex));
    end_query_draw(query);
    return query;
}

// 0x004CBA90
uint8_t PcViewport::query_rect(float x0, float y0, float x1, float y1, uint32_t z, uint8_t query)
{
    if (radeon8500() || !query)
        return 0;
    input_port()->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    change_phase(kZWrite, 0);
    begin_query_draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    auto depth = static_cast<float>(static_cast<double>(z));
    g_query_rect_strip = {{{.x = x0, .y = y0, .z = depth, .rhw = 1.0f, .colour = 0xFFFFFFFF},
                           {.x = x0, .y = y1, .z = depth, .rhw = 1.0f, .colour = 0xFFFFFFFF},
                           {.x = x1, .y = y0, .z = depth, .rhw = 1.0f, .colour = 0xFFFFFFFF},
                           {.x = x1, .y = y1, .z = depth, .rhw = 1.0f, .colour = 0xFFFFFFFF}}};
    input_port()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, g_query_rect_strip.data(), sizeof(QueryRectVertex));
    end_query_draw(query);
    return query;
}

// 0x004CBD40
uint32_t occlusion_pixels()
{
    if (radeon8500())
        return 0;
    DWORD pixels = 0;
    const HRESULT result = os_layer::get_occlusion_result(core::at(g_occlusion_requests, 0).slot, &pixels);
    if (result == S_OK) {
        core::at(g_occlusion_slots, core::at(g_occlusion_requests, 0).slot) = 0;
        pop_occlusion_request(); // (the requests moved on)
        g_occlusion_pixels = pixels;
        return pixels;
    }
    return result == S_FALSE ? g_occlusion_pixels : 0;
}

} // namespace render
