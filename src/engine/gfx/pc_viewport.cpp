// The PC viewport: see pc_viewport.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include <cstdint>

#include "engine/gfx/pc_viewport.h"
#include "platform/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include "engine/memory/mem.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/viewport.h"
#include "base/maths/vec4.h"
#include "platform/platform_d3d.h"
#include "engine/gfx/shader_slots.h"
#include <gsl/pointers>
#include <array>
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/view_setup.h"
#include "base/maths/vec2.h"
#include "base/maths/rect.h"
#include "engine/gfx/screen.h"

namespace render {

constexpr int kColourWriteAll = 0xF; // red, green, blue and alpha
constexpr float kHalf = 0.5F;
constexpr float kDepthScale = 65536.0F;
constexpr uint32_t kWhiteVertex = 0xFFFFFFFFU;

namespace {
uint32_t& stored_occlusion_pixels()
{
    static uint32_t occlusion_pixels{};
    return occlusion_pixels;
}
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
// NOT ORIGINAL grouping - pc_viewport.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_viewport_state()).
struct PcViewportState {
    std::array<QueryRectVertex, 4> query_rect_strip_{};
    std::array<QueryPointVertex, 4> query_point_strip_{};
};

PcViewportState& pc_viewport_state()
{
    static core::NoDestroy<PcViewportState> state;
    return state.get();
}

} // namespace

namespace {

void xform(float* out, const float* v, const void* matrix) { d3dx::vec4_transform(out, v, matrix); }

// The queries' states: colour writes off, no z writes, no texture, the vertex colour's shader.
void begin_query_draw(uint32_t fvf)
{
    choose_blend_mode(0);
    set_texture(0, nullptr);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader94c), nullptr);
    set_vertex_format(fvf);
    os_layer::begin_occlusion_query();
}
void end_query_draw(uint8_t query)
{
    os_layer::end_occlusion_query(query);
    renderer().d3d_device()->SetRenderState(D3DRS_COLORWRITEENABLE, kColourWriteAll);
    change_phase(RenderStateId::kZWrite, 1);
}

} // namespace

// 0x004CB860
gsl::owner<PcViewport*> create_viewport(const std::array<float, 4>* rect, gfx::ViewCam* camera)
{
    return new PcViewport(rect, camera);
}

// 0x004CB940
float PcViewport::project_to_screen(const maths::Vec4* point, maths::Vec2* at, int* depth)
{
    float* x = &at->x;
    float* y = &at->y;
    std::array<float, 4> v = {{point->x, point->y, point->z, 1.0F}};
    std::array<float, 4> view{};
    std::array<float, 4> clip{};
    xform(view.data(), v.data(), &renderer().view_matrix());
    xform(clip.data(), view.data(), &renderer().projection_matrix());
    if (core::at(clip, 3) != 0.0F) {
        const double inverse = 1.0F / static_cast<double>(core::at(clip, 3));
        core::at(clip, 0) = static_cast<float>(core::at(clip, 0) * inverse);
        core::at(clip, 1) = static_cast<float>(core::at(clip, 1) * inverse);
        core::at(clip, 2) = static_cast<float>(inverse * core::at(clip, 2));
    }
    *x = static_cast<float>((static_cast<double>(core::at(clip, 0)) + 1.0F) * kHalf * kLayoutWidth);
    *y = static_cast<float>((1.0F - static_cast<double>(core::at(clip, 1))) * kHalf * kElementLayoutHeight);
    if (!(core::at(clip, 2) < 1.0F)) {
        return -1.0F;
    }
    const float most = view_setup().scale_2d_in_3d_max();
    const float least = view_setup().scale_2d_in_3d_min();
    double factor = core::at(view, 2) == 0.0F
                        ? most
                        : -1.0F / static_cast<double>(core::at(view, 2)) * view_setup().scale_2d_in_3d_numerator();
    if (factor > most) {
        factor = most;
    } else if (factor < least) {
        factor = least;
    }
    *depth = static_cast<int>(static_cast<double>(core::at(clip, 2)) * kDepthScale);
    return static_cast<float>(factor);
}

// 0x004CBC00
uint8_t PcViewport::query_point(const maths::Vec4* point, int /*unused*/, uint8_t query)
{
    if (renderer().radeon8500() || (query == 0U)) {
        return 0;
    }
    change_phase(RenderStateId::kZWrite, 0);
    renderer().d3d_device()->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    begin_query_draw(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    for (QueryPointVertex& vertex : pc_viewport_state().query_point_strip_) {
        vertex = {.x = point->x, .y = point->y, .z = point->z, .colour = kWhiteVertex};
    }
    renderer().d3d_device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, pc_viewport_state().query_point_strip_.data(), sizeof(QueryPointVertex));
    end_query_draw(query);
    return query;
}

// 0x004CBA90
uint8_t PcViewport::query_rect(maths::Rect box, RectQuery rect_query)
{
    const auto z = rect_query.z;
    const auto query = rect_query.query;
    const auto x0 = box.x0;
    const auto y0 = box.y0;
    const auto x1 = box.x1;
    const auto y1 = box.y1;
    if (renderer().radeon8500() || (query == 0U)) {
        return 0;
    }
    renderer().d3d_device()->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    change_phase(RenderStateId::kZWrite, 0);
    begin_query_draw(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    auto depth = static_cast<float>(static_cast<double>(z));
    pc_viewport_state().query_rect_strip_ = {{{.x = x0, .y = y0, .z = depth, .rhw = 1.0F, .colour = kWhiteVertex},
                                              {.x = x0, .y = y1, .z = depth, .rhw = 1.0F, .colour = kWhiteVertex},
                                              {.x = x1, .y = y0, .z = depth, .rhw = 1.0F, .colour = kWhiteVertex},
                                              {.x = x1, .y = y1, .z = depth, .rhw = 1.0F, .colour = kWhiteVertex}}};
    renderer().d3d_device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, pc_viewport_state().query_rect_strip_.data(), sizeof(QueryRectVertex));
    end_query_draw(query);
    return query;
}

// 0x004CBD40
uint32_t occlusion_pixels()
{
    if (renderer().radeon8500()) {
        return 0;
    }
    DWORD pixels = 0;
    const HRESULT result = os_layer::get_occlusion_result(core::at(view_setup().occlusion_requests(), 0).slot, &pixels);
    if (result == S_OK) {
        core::at(view_setup().occlusion_slots(), core::at(view_setup().occlusion_requests(), 0).slot) = 0;
        pop_occlusion_request(); // (the requests moved on)
        stored_occlusion_pixels() = pixels;
        return pixels;
    }
    return result == S_FALSE ? stored_occlusion_pixels() : 0;
}

} // namespace render
