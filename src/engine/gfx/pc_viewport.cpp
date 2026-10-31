// The PC viewport: see pc_viewport.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include <cstdint>

#include "engine/gfx/pc_viewport.h"
#include "engine/gfx/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/viewport.h"
#include "platform/platform_d3d.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "engine/gfx/glow.h"

namespace render {

uint8_t g_occlusion_slots[1024]{1};
uint32_t g_occlusion_pixels;
float g_query_rect_strip[20];
uint32_t g_query_point_strip[16];

float g_virtual_screen_height{448.0f};
float g_virtual_screen_width{640.0f};
float g_scale_2d_in_3d_numerator{300.0f};
float g_scale_2d_in_3d_max{3.0f};
float g_scale_2d_in_3d_min;

namespace {

extern void** const kViewportVtable;

IDirect3DDevice9* input_port() { return g_d3d_device; }
bool radeon8500() { return g_radeon8500; }

void xform(float* out, const float* v, const void* matrix) { d3dx::vec4_transform(out, v, matrix); }

// The queries' states: colour writes off, no z writes, no texture, the vertex colour's shader.
void begin_query_draw(uint32_t fvf)
{
    choose_blend_mode(0);
    set_texture(0, nullptr);
    set_pixel_shader(g_pixel_shaders[kPixelShader94c], nullptr);
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
PcViewport* __cdecl create_viewport(uint32_t a1, uint32_t a2)
{
    void* memory = memory::alloc(sizeof(PcViewport));
    return memory ? static_cast<PcViewport*>(memory)->make_new(a1, a2) : nullptr;
}

// 0x004CB8E0
PcViewport* PcViewport::make_new(uint32_t a1, uint32_t a2)
{
    reinterpret_cast<ViewRegion*>(this)->ViewRegion::make_new(
        reinterpret_cast<const float*>(static_cast<uintptr_t>(a1)),
        reinterpret_cast<gfx::ViewCam*>(static_cast<uintptr_t>(a2)));
    vtable = reinterpret_cast<void**>(kViewportVtable);
    return this;
}

// 0x004CB900
void PcViewport::destroy_body()
{
    vtable = reinterpret_cast<void**>(kViewportVtable);
    reinterpret_cast<ViewRegion*>(this)->ViewRegion::destroy_body();
}

// 0x004CB910
PcViewport* PcViewport::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004CB940
float PcViewport::project_to_screen(const maths::Vec4* point, float* x, float* y, int* depth)
{
    float v[4] = {point->x, point->y, point->z, 1.0f};
    float view[4], clip[4];
    xform(view, v, &g_view_matrix);
    xform(clip, view, &g_projection_matrix);
    if (clip[3] != 0.0f) {
        double inverse = 1.0f / static_cast<double>(clip[3]);
        clip[0] = static_cast<float>(clip[0] * inverse);
        clip[1] = static_cast<float>(clip[1] * inverse);
        clip[2] = static_cast<float>(inverse * clip[2]);
    }
    *x = static_cast<float>((static_cast<double>(clip[0]) + 1.0f) * 0.5f * g_virtual_screen_width);
    *y = static_cast<float>((1.0f - static_cast<double>(clip[1])) * 0.5f * g_virtual_screen_height);
    if (!(clip[2] < 1.0f))
        return -1.0f;
    float most = g_scale_2d_in_3d_max, least = g_scale_2d_in_3d_min;
    double factor = view[2] == 0.0f ? most : -1.0f / static_cast<double>(view[2]) * g_scale_2d_in_3d_numerator;
    if (factor > most)
        factor = most;
    else if (factor < least)
        factor = least;
    *depth = static_cast<int>(static_cast<double>(clip[2]) * 65536.0f);
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
    auto* strip = g_query_point_strip; // (four vertices: position, colour)
    for (int k = 0; k < 4; ++k) {
        strip[4 * k] = reinterpret_cast<const uint32_t*>(point)[0];
        strip[4 * k + 1] = reinterpret_cast<const uint32_t*>(point)[1];
        strip[4 * k + 2] = reinterpret_cast<const uint32_t*>(point)[2];
        strip[4 * k + 3] = 0xFFFFFFFF;
    }
    input_port()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, strip, 0x10);
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
    struct Vertex {
        float x, y, z, rhw;
        uint32_t colour;
    };
    auto* strip = reinterpret_cast<Vertex*>(g_query_rect_strip);
    float depth = static_cast<float>(static_cast<double>(z));
    strip[0] = {x0, y0, depth, 1.0f, 0xFFFFFFFF};
    strip[1] = {x0, y1, depth, 1.0f, 0xFFFFFFFF};
    strip[2] = {x1, y0, depth, 1.0f, 0xFFFFFFFF};
    strip[3] = {x1, y1, depth, 1.0f, 0xFFFFFFFF};
    input_port()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, strip, 0x14);
    end_query_draw(query);
    return query;
}

// 0x004CBD40
uint32_t __cdecl occlusion_pixels()
{
    if (radeon8500())
        return 0;
    DWORD pixels;
    HRESULT result = os_layer::get_occlusion_result(g_occlusion_requests[0].slot, &pixels);
    if (result == S_OK) {
        g_occlusion_slots[g_occlusion_requests[0].slot] = 0;
        pop_occlusion_request(); // (the requests moved on)
        g_occlusion_pixels = pixels;
        return pixels;
    }
    return result == S_FALSE ? g_occlusion_pixels : 0;
}

namespace {
static VtableSlot g_viewport_vtable_slots[] = {&render::PcViewport::teardown, &render::PcViewport::project_to_screen,
                                               &render::PcViewport::query_point, &render::PcViewport::query_rect,
                                               stubs::return_zero1};
void** const kViewportVtable = reinterpret_cast<void**>(g_viewport_vtable_slots);
} // namespace

} // namespace render
