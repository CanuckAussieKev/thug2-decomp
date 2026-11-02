// Texture splats on PC and the projection's geometry helpers: see texture_splats.h. The maths follows the original's
// x87 code: double precision where it keeps values in registers, floats where it stores them.

#include "engine/gfx/texture_splats.h"
#include "engine/gfx/geom.h"
#include "platform/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_mesh.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/render_state.h"
#include "engine/lists/object_set.h"
#include "gpu/vertex_view.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"

namespace render {

VertexStack g_vertex_stack;
namespace {
std::array<uint8_t, 128> g_splat_middle20;
std::array<uint8_t, 128> g_splat_middle12;
std::array<uint8_t, 128> g_splat_middle01;
std::array<uint8_t, 128> g_splat_vertex0;
std::array<uint8_t, 128> g_splat_vertex1;
std::array<uint8_t, 128> g_splat_vertex2;
} // namespace

float g_split_area_limit{82944.0f};

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
IDirect3DDevice9* draw_device() { return g_draw_device; } // (the same device, kept twice)

// RGBA bytes as ARGB.
uint32_t to_argb(uint32_t rgba)
{
    const uint8_t r = rgba & 0xFF;
    const uint8_t g = (rgba >> 8) & 0xFF;
    const uint8_t b = (rgba >> 16) & 0xFF;
    const uint8_t a = rgba >> 24;
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

// The vertex stack (0x006FC4A0: +0 what's on it, +4 a vertex's size; not rewritten yet): popped into / pushed from.
uint32_t get_vertex_size() { return g_vertex_stack.vertex_size_; }
void pop_vertex(uint8_t* out) { g_vertex_stack.stack_off(out); }
void push_vertex(const uint8_t* vertex) { g_vertex_stack.stack_on(vertex); }
void push_vertex(const ProjectedVertex& vertex)
{
    const auto bytes = std::bit_cast<std::array<uint8_t, sizeof(ProjectedVertex)>>(vertex);
    g_vertex_stack.stack_on(bytes.data());
}

// A byte channel's midpoint as the original computes it: a + (b - a) / 2, the division rounding towards 0.
uint8_t byte_midpoint(uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + (static_cast<int>(b) - a) / 2); }
float midpoint(float a, float b) { return static_cast<float>((static_cast<double>(b) - a) * 0.5 + a); }

} // namespace

ProjectedVertex& TextureSplats::vertex(int index) { return core::at(core::at(vertices, index / 3), index % 3); }

// 0x00490CD0
int TextureSplats::oldest_slot(uint32_t count) const
{
    int oldest = 0;
    int most = core::at(correction_steps_, 0) - static_cast<int>(core::at(alive, 0));
    for (uint32_t i = 0; i < count; ++i) {
        if (!core::at(alive, i))
            return static_cast<int>(i);
        const int age = core::at(correction_steps_, i) - static_cast<int>(core::at(alive, i));
        if (age > most) {
            most = age;
            oldest = static_cast<int>(i);
        }
    }
    return oldest;
}

// 0x004B7F30
void collapse_splat_triangle(TextureSplats* splats, int index)
{
    auto& triangle = core::at(splats->vertices, index);
    core::at(triangle, 1) = core::at(triangle, 0);
    core::at(triangle, 2) = core::at(triangle, 0);
}

// 0x004B87B0
void light_texture_splats(TextureSplats* splats)
{
    for (int i = 0; i <= splats->last; ++i) {
        auto& triangle = core::at(splats->vertices, i);
        const uint32_t* base = core::at(splats->base_colors_, i).data();
        // (the lit colours come back over `colors`, which first holds the base colours)
        std::array<uint32_t, 3> colors{};
        std::memcpy(colors.data(), base, 12);
        const auto& at = core::at(triangle, 0).position;
        std::array<maths::Vec4, 3> points{};
        for (auto& point : points)
            point = {core::at(at, 0), core::at(at, 1), core::at(at, 2), 0.0f};
        light_vertex_colours(base, colors.data(), points.data(), nullptr, 3);
        for (int k = 0; k < 3; ++k)
            core::at(triangle, k).argb = to_argb(core::at(colors, k));
    }
}

// 0x004B9200
void kill_all_texture_splats()
{
    g_splat_sets.for_each([](TextureSplats* splats) {
        if (MaterialRef* material = splats->material) {
            material->~MaterialRef(); // (the destructor's body)
            memory::Hub::get_pull()->dealloc(material);
        }
        g_splat_sets.erase(splats);
        memory::Hub::get_pull()->dealloc(splats);
    });
}

// 0x004B92A0
void kill_texture_splats_in_box(const float* box)
{
    g_splat_sets.for_each([box](TextureSplats* splats) {
        for (int i = 0; i < 0x100; ++i) {
            const auto& p = core::at(core::at(splats->vertices, i), 0).position;
            if (core::at(p, 0) >= box[0] && core::at(p, 1) >= box[1] && core::at(p, 2) >= box[2] && core::at(p, 0) <= box[4] && core::at(p, 1) <= box[5] &&
                core::at(p, 2) <= box[6])
                core::at(splats->alive, i) = 0;
        }
    });
}

// 0x004B9380
void draw_texture_splats()
{
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    input_port()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
    DWORD min_filter = 0;
    input_port()->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_splat_sets.for_each([](TextureSplats* splats) {
        if (splats->last < 0)
            return;
        for (int j = 0; j <= splats->last; ++j) {
            double factor = 1.0;
            if (!(0.0f == core::at(splats->correction_a_, j) - core::at(splats->correction_b_, j))) {
                const double r = static_cast<double>(core::at(splats->correction_a_, j)) / core::at(splats->correction_b_, j);
                const double steps = core::at(splats->correction_steps_, j);
                const double t = (steps - static_cast<int>(core::at(splats->alive, j))) / steps;
                factor = 1.0 / (t * (1.0 - r) + r);
            }
            auto& uv = splats->vertex(j).uv; // (vertex j: the coordinates follow the vertices, not the triangles)
            core::at(uv, 0) = static_cast<float>((static_cast<double>(core::at(core::at(splats->uvs, j), 0)) - 0.5f) * factor + 0.5f);
            core::at(uv, 1) = static_cast<float>((static_cast<double>(core::at(core::at(splats->uvs, j), 1)) - 0.5f) * factor + 0.5f);
        }
        splats->material->hand_in(); // (the material's set-up)
        set_pixel_shader(core::at(g_pixel_shaders, kPixelShader964), splats->material);
        draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, splats->last + 1, splats->vertices.data(),
                                       sizeof(ProjectedVertex));
    });
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, min_filter);
}

// 0x004B7FA0
bool segments_intersect(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy)
{
    // (the stored differences are floats; the rest stays in registers)
    auto abx = static_cast<float>(static_cast<double>(bx) - ax);
    auto aby = static_cast<float>(static_cast<double>(by) - ay);
    const double dcx = static_cast<double>(cx) - dx;
    const double dcy = static_cast<double>(cy) - dy;
    auto cax = static_cast<float>(static_cast<double>(ax) - cx);
    auto cay = static_cast<float>(static_cast<double>(ay) - cy);
    auto along = static_cast<float>(cax * dcy - cay * dcx);
    auto denominator = static_cast<float>(aby * dcx - dcy * abx);
    const double other = static_cast<double>(cay) * abx - static_cast<double>(cax) * aby;
    if (denominator > 0.0f) {
        if (along < 0.0f || along > denominator)
            return false;
        return !(other < 0.0 || other > denominator);
    }
    if (along > 0.0f || along < denominator)
        return false;
    return !(other > 0.0 || other < denominator);
}

// 0x004B88D0
bool triangle_overlaps_screen(float x0, float y0, float x1, float y1, float x2, float y2)
{
    if ((x0 < -1.0f && x1 < -1.0f && x2 < -1.0f) || (x0 > 1.0f && x1 > 1.0f && x2 > 1.0f) ||
        (y0 < -1.0f && y1 < -1.0f && y2 < -1.0f) || (y0 > 1.0f && y1 > 1.0f && y2 > 1.0f))
        return false;
    // the square's edges against the triangle's
    const std::array<std::array<float, 2>, 4> corners = {{{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}}};
    for (int i = 0; i < 4; ++i) {
        const float* a = core::at(corners, i).data();
        const float* b = core::at(corners, (i + 1) % 4).data();
        if (segments_intersect(x0, y0, x1, y1, a[0], a[1], b[0], b[1]) ||
            segments_intersect(x1, y1, x2, y2, a[0], a[1], b[0], b[1]) ||
            segments_intersect(x2, y2, x0, y0, a[0], a[1], b[0], b[1]))
            return true;
    }
    // a vertex inside the square
    const std::array<std::array<float, 2>, 3> vertices = {{{x0, y0}, {x1, y1}, {x2, y2}}};
    for (const auto& v : vertices) {
        if (core::at(v, 0) >= -1.0f && core::at(v, 0) <= 1.0f && core::at(v, 1) >= -1.0f && core::at(v, 1) <= 1.0f)
            return true;
    }
    // a corner inside the triangle: on the inner side of each edge (as the original rounds)
    const double e0 = static_cast<double>(x2) - x1;
    const double f0 = static_cast<double>(y2) - y1;
    auto side0 = static_cast<float>((static_cast<double>(y0) - y1) * e0 - f0 * (static_cast<double>(x0) - x1));
    auto e0f = static_cast<float>(e0), f0f = static_cast<float>(f0);
    for (const auto& corner : corners) {
        double px = static_cast<double>(core::at(corner, 0)) - x1, py = static_cast<double>(core::at(corner, 1)) - y1;
        if (!((py * e0f - f0f * px) * side0 >= 0.0))
            continue;
        px = static_cast<double>(core::at(corner, 0)) - x2;
        py = static_cast<double>(core::at(corner, 1)) - y2;
        const double e1 = static_cast<double>(x0) - x2;
        const double f1 = static_cast<double>(y0) - y2;
        auto e1f = static_cast<float>(e1), f1f = static_cast<float>(f1);
        auto g1 = static_cast<float>(static_cast<double>(x1) - x2),
             h1 = static_cast<float>(static_cast<double>(y1) - y2);
        if (!((py * e1 - f1f * px) * (static_cast<double>(h1) * e1f - f1 * g1) >= 0.0))
            continue;
        px = static_cast<double>(core::at(corner, 0)) - x0;
        py = static_cast<double>(core::at(corner, 1)) - y0;
        const double e2 = static_cast<double>(x1) - x0;
        const double f2 = static_cast<double>(y1) - y0;
        auto e2f = static_cast<float>(e2), f2f = static_cast<float>(f2);
        auto g2 = static_cast<float>(static_cast<double>(x2) - x0),
             h2 = static_cast<float>(static_cast<double>(y2) - y0);
        if ((py * e2 - f2f * px) * (static_cast<double>(h2) * e2f - f2 * g2) >= 0.0)
            return true;
    }
    return false;
}

// 0x004B8220
bool split_next_triangle(uint8_t** out, const MeshRef* format)
{
    if (!g_vertex_stack.used)
        return false;
    uint8_t* v2 = g_splat_vertex2.data(); // (the three popped, last pushed first)
    uint8_t* v1 = g_splat_vertex1.data();
    uint8_t* v0 = g_splat_vertex0.data();
    pop_vertex(v2);
    pop_vertex(v1);
    pop_vertex(v0);
    const uint32_t size = get_vertex_size();
    const auto p0 = gpu::VertexView(v0, size).float3(0);
    const auto p1 = gpu::VertexView(v1, size).float3(0);
    const auto p2 = gpu::VertexView(v2, size).float3(0);
    // the edges from v0 and their cross product (as the original: some parts rounded to floats on the way)
    const double ax = static_cast<double>(core::at(p1, 0)) - core::at(p0, 0);
    const double ay = static_cast<double>(core::at(p1, 1)) - core::at(p0, 1);
    const double az = static_cast<double>(core::at(p1, 2)) - core::at(p0, 2);
    auto ayf = static_cast<float>(ay), azf = static_cast<float>(az);
    auto bxf = static_cast<float>(static_cast<double>(core::at(p2, 0)) - core::at(p0, 0));
    auto byf = static_cast<float>(static_cast<double>(core::at(p2, 1)) - core::at(p0, 1));
    const double bz = static_cast<double>(core::at(p2, 2)) - core::at(p0, 2);
    auto cross_x = static_cast<float>(bz * ay - byf * az);
    auto cross_y = static_cast<float>(az * bxf - bz * ax);
    const double cross_z = ax * byf - ay * bxf;
    const double length_squared =
        cross_z * cross_z + static_cast<double>(cross_y) * cross_y + static_cast<double>(cross_x) * cross_x;
    if (!(length_squared > g_split_area_limit)) {
        for (const uint8_t* v : {v0, v1, v2}) {
            std::memcpy(*out, v, size);
            *out += size;
        }
        return true;
    }
    // the midpoints of the edges: v0-v1, v1-v2, v2-v0
    uint8_t* m01 = g_splat_middle01.data();
    uint8_t* m12 = g_splat_middle12.data();
    uint8_t* m20 = g_splat_middle20.data();
    std::memcpy(m01, v0, size);
    std::memcpy(m12, v1, size);
    std::memcpy(m20, v2, size);
    gpu::VertexWriter(m01, size).set_float3(0, 0, static_cast<float>(ax * 0.5 + core::at(p0, 0)),
                                            static_cast<float>(static_cast<double>(ayf) * 0.5 + core::at(p0, 1)),
                                            static_cast<float>(static_cast<double>(azf) * 0.5 + core::at(p0, 2)));
    std::array<float, 3> q12{};
    std::array<float, 3> q20{};
    for (int k = 0; k < 3; ++k)
        core::at(q12, k) = midpoint(core::at(p1, k), core::at(p2, k));
    for (int k = 0; k < 3; ++k)
        core::at(q20, k) = midpoint(core::at(p2, k), core::at(p0, k));
    gpu::VertexWriter(m12, size).set_floats(0, 0, q12);
    gpu::VertexWriter(m20, size).set_floats(0, 0, q20);
    if (const uint8_t c = format->color_offset_) {
        for (int k = 0; k < 4; ++k) {
            m01[c + k] = byte_midpoint(v0[c + k], v1[c + k]);
            m12[c + k] = byte_midpoint(v1[c + k], v2[c + k]);
            m20[c + k] = byte_midpoint(v2[c + k], v0[c + k]);
        }
    }
    if (const uint8_t t = format->uv_offset_) {
        const auto uv0 = gpu::VertexView(v0, size).float2(0, t);
        const auto uv1 = gpu::VertexView(v1, size).float2(0, t);
        const auto uv2 = gpu::VertexView(v2, size).float2(0, t);
        std::array<float, 2> uv01{};
        std::array<float, 2> uv12{};
        std::array<float, 2> uv20{};
        for (int k = 0; k < 2; ++k) {
            core::at(uv01, k) = midpoint(core::at(uv0, k), core::at(uv1, k));
            core::at(uv12, k) = midpoint(core::at(uv1, k), core::at(uv2, k));
            core::at(uv20, k) = midpoint(core::at(uv2, k), core::at(uv0, k));
        }
        gpu::VertexWriter(m01, size).set_floats(0, t, uv01);
        gpu::VertexWriter(m12, size).set_floats(0, t, uv12);
        gpu::VertexWriter(m20, size).set_floats(0, t, uv20);
    }
    for (const uint8_t* v : {v0, m01, m20, m01, v1, m12, m01, m12, m20, m20, m12, v2})
        push_vertex(v);
    return true;
}

namespace {

int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

// D3DX (linked library code).
void transform_coord(float* out, const float* v, const maths::Mat4* m) { d3dx::vec3_transform_coord(out, v, m); }
void normalize3(float* v) { d3dx::vec3_normalize(v, v); }
void get_look_at(maths::Mat4* out, const float* eye, const float* at, const float* up)
{
    d3dx::matrix_look_at_rh(out, eye, at, up);
}
void ortho(maths::Mat4* out, float width, float height, float near_z, float far_z)
{
    d3dx::matrix_ortho_rh(out, width, height, near_z, far_z);
}
void get_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// A set's oldest triangle (a free one first: 0x00490CD0, the engine's).
int oldest_triangle(const TextureSplats* splats) { return splats->oldest_slot(0x100); }
// The lifetime in milliseconds (the C runtime's _ftol: truncated).
int get_milliseconds(float seconds) { return static_cast<int>(static_cast<double>(seconds) * 1000.0f); }
// A vertex colour as the splats keep it: opaque-ish (alpha 0x80).
uint32_t splat_color(uint32_t color) { return (color & 0xFFFFFF) | 0x80000000; }

// The pieces the splitter makes are splat vertices (the stack's vertex size is one's) - read as bytes.
ProjectedVertex piece_at(const gpu::VertexView& pieces, size_t index)
{
    return pieces.value<ProjectedVertex>(index, 0);
}

} // namespace

// 0x004B9A60
TextureSplats* get_texture_splats(TextureImage* texture)
{
    if (TextureSplats* splats = g_splat_sets.find_if(
            [texture](const TextureSplats* set) { return core::at(set->material->textures, 0) == texture; }))
        return splats;
    void* memory = memory::alloc(sizeof(MaterialRef));
    MaterialRef* material = memory ? memory::construct_in<MaterialRef>(memory) : nullptr;
    // (no check: a failed allocation writes through null, as the original)
    core::at(material->pass_flags_, 0) = texture ? 4 : 0; // (the pass's flags)
    const int a = get_rand();
    const int b = get_rand();
    material->checksum = a * b;                                  // a random checksum
    core::at(material->textures, 0) = texture;
    material->pass_count_ = 1;                                   // one pass
    material->two_sided_ = 1;
    material->depth_bias_ = 0x14;
    core::at(material->blend_modes_, 0) = 5; // its blend mode
    for (int k = 0; k < 3; ++k)
        core::at(core::at(material->pass_color_, 0), k) = 0.5f; // its colour
    core::at(material->address_modes_, 0) = 0x20002;
    core::at(material->mip_lod_bias_, 0) = 0;
    material->alpha_ref_ = 1;
    auto* splats = static_cast<TextureSplats*>(memory::alloc(sizeof(TextureSplats)));
    splats->last = 0;
    splats->material = material;
    std::memset(splats->alive.data(), 0, sizeof(splats->alive));
    for (int v = 0; v < 0x300; ++v) {
        ProjectedVertex& vertex = splats->vertex(v);
        vertex.position = {};
        vertex.argb = 0x80808080;
    }
    g_splat_sets.insert(splats);
    return splats;
}

// 0x004B9C30
TextureSplats* create_texture_splat(void** sectors, HitMesh* const* /*objects*/, SplatParams* params)
{
    float* start = params->start.data();
    float* end = params->end.data();
    // the ray's direction (normalised when it has a length; the parts stored are floats) and length
    const double dz = static_cast<double>(end[2]) - start[2];
    auto dyf = static_cast<float>(static_cast<double>(end[1]) - start[1]);
    auto dxf = static_cast<float>(static_cast<double>(end[0]) - start[0]);
    std::array<float, 3> direction = {{dxf, dyf, static_cast<float>(dz)}};
    const double length_squared = static_cast<double>(dxf) * dxf + static_cast<double>(dyf) * dyf + dz * dz;
    auto length = static_cast<float>(std::sqrt(length_squared));
    const double norm = std::sqrt(length_squared);
    if (norm > 0.0) {
        const double inverse = 1.0 / norm;
        core::at(direction, 0) = static_cast<float>(dxf * inverse);
        core::at(direction, 1) = static_cast<float>(dyf * inverse);
        core::at(direction, 2) = static_cast<float>(dz * inverse);
    }
    const float cone = std::cos(params->cone_angle_);
    // the projection: looking along the ray, the texture's up from `up` (the ray moved by it, and the height its
    // distance), else about y (turned by the angle - or a random one - when the ray is steep)
    maths::Vec4 up{};
    maths::Mat4 view{}, projection{}, combined{};
    if (params->up) {
        const float* from = params->up;
        const double az = static_cast<double>(start[2]) - from[2];
        const double ay = static_cast<double>(start[1]) - from[1];
        up[0] = static_cast<float>(static_cast<double>(start[0]) - from[0]);
        up[1] = static_cast<float>(ay);
        up[2] = static_cast<float>(az);
        up[3] = 1.0f;
        auto height = static_cast<float>(std::sqrt(static_cast<double>(up[0]) * up[0] + ay * ay + az * az));
        start[0] = static_cast<float>(static_cast<double>(start[0]) - up[0]);
        start[1] = static_cast<float>(static_cast<double>(start[1]) - ay);
        start[2] = static_cast<float>(static_cast<double>(start[2]) - az);
        start[3] = static_cast<float>(static_cast<double>(start[3]) - 1.0f);
        end[0] = static_cast<float>(static_cast<double>(end[0]) - up[0]);
        end[1] = static_cast<float>(static_cast<double>(end[1]) - ay);
        end[2] = static_cast<float>(static_cast<double>(end[2]) - az);
        end[3] = static_cast<float>(static_cast<double>(end[3]) - 1.0f);
        up.make_unit();
        get_look_at(&view, start, end, up.data());
        ortho(&projection, params->size, height, 0.1f, length);
    } else {
        if (std::fabs(core::at(direction, 1)) > 0.5f) {
            double a = params->angle;
            if (0.0f == params->angle)
                a = get_rand() * static_cast<double>(std::bit_cast<float>(0x40C90FDBu)) *
                    std::bit_cast<float>(0x38000100u); // (2 pi, 1 / 32767)
            up[0] = static_cast<float>(std::sin(a));
            up[1] = 0.0f;
            up[2] = static_cast<float>(std::cos(a));
        } else {
            up[0] = 0.0f;
            up[1] = 1.0f;
            up[2] = 0.0f;
        }
        up[3] = 1.0f;
        get_look_at(&view, start, end, up.data());
        ortho(&projection, params->size, params->size, 0.1f, length);
    }
    get_multiply(&combined, &view, &projection);

    TextureSplats* splats = nullptr;
    int slot = 0;
    for (; *sectors; ++sectors) {
        auto* geom = static_cast<PcGeom*>(static_cast<PcSector*>(*sectors)->geom_handle_); // (the sector's geom)
        if (!geom)
            continue;
        MeshRef* const* meshes = geom->meshes;
        const uint32_t mesh_count = geom->mesh_count_;
        for (uint32_t m = 0; m < mesh_count; ++m) {
            MeshRef* mesh = meshes[m];
            if (mesh->flags & 2)
                continue;
            uint8_t* vertices = nullptr;
            core::at(mesh->vertex_buffers_, 0)->get_lock(0, 0, &vertices, 0x10);
            const gpu::VertexView mesh_vertices(vertices, mesh->stride);
            uint16_t* indices = nullptr;
            core::at(mesh->index_buffers_, 0)->get_lock(0, 0, &indices, 0x10);
            uint32_t a = indices[0], b = indices[1];
            for (uint32_t strip = 2; strip < core::at(mesh->index_counts_, 0); ++strip) {
                const uint32_t c = indices[strip];
                const uint32_t ia = a;
                const uint32_t ib = b;
                const uint32_t ic = c;
                a = b;
                b = c;
                if (ia == ib || ia == ic || ib == ic)
                    continue;
                const std::array<float, 3> a0 = mesh_vertices.float3(ia);
                const std::array<float, 3> a1 = mesh_vertices.float3(ib);
                const std::array<float, 3> a2 = mesh_vertices.float3(ic);
                std::array<float, 3> t0{};
                std::array<float, 3> t1{};
                std::array<float, 3> t2{};
                transform_coord(t0.data(), a0.data(), &combined);
                transform_coord(t1.data(), a1.data(), &combined);
                transform_coord(t2.data(), a2.data(), &combined);
                if ((core::at(t0, 2) < -1.0f && core::at(t1, 2) < -1.0f && core::at(t2, 2) < -1.0f) || (core::at(t0, 2) > 1.0f && core::at(t1, 2) > 1.0f && core::at(t2, 2) > 1.0f))
                    continue;
                if (!triangle_overlaps_screen(core::at(t0, 0), core::at(t0, 1), core::at(t1, 0), core::at(t1, 1), core::at(t2, 0), core::at(t2, 1)))
                    continue;
                if (!splats)
                    splats = get_texture_splats(image_of(params->texture));
                slot = oldest_triangle(splats);
                auto& triangle = core::at(splats->vertices, slot);
                ProjectedVertex& v0 = core::at(triangle, 0);
                ProjectedVertex& v1 = core::at(triangle, 1);
                ProjectedVertex& v2 = core::at(triangle, 2);
                v0.position = a0;
                v1.position = a1;
                v2.position = a2;
                // the face's normal against the ray (as the original rounds it)
                {
                    const auto& p0 = v0.position;
                    const auto& p1 = v1.position;
                    const auto& p2 = v2.position;
                    const double ez2 = static_cast<double>(core::at(p2, 2)) - core::at(p0, 2);
                    const double ey2 = static_cast<double>(core::at(p2, 1)) - core::at(p0, 1);
                    const double ex2 = static_cast<double>(core::at(p2, 0)) - core::at(p0, 0);
                    const double ez1 = static_cast<double>(core::at(p1, 2)) - core::at(p0, 2);
                    auto ey1f = static_cast<float>(static_cast<double>(core::at(p1, 1)) - core::at(p0, 1));
                    auto ex1f = static_cast<float>(static_cast<double>(core::at(p1, 0)) - core::at(p0, 0));
                    std::array<float, 3> normal = {{static_cast<float>(ez1 * ey2 - ey1f * ez2),
                                       static_cast<float>(ex1f * ez2 - ez1 * ex2),
                                       static_cast<float>(ey1f * ex2 - ex1f * ey2)}};
                    normalize3(normal.data());
                    const double facing = static_cast<double>(core::at(normal, 2)) * core::at(direction, 2) +
                                          static_cast<double>(core::at(normal, 1)) * core::at(direction, 1) +
                                          static_cast<double>(core::at(normal, 0)) * core::at(direction, 0);
                    const double absolute = std::fabs(facing);
                    if (absolute < cone || absolute == cone)
                        continue;
                }
                core::at(splats->alive, slot) = params->lifetime < 0.0f || std::isnan(params->lifetime)
                                          ? 0x7FFFFFFF
                                          : get_milliseconds(params->lifetime);
                // texture coordinates from the projection (0-1 over the square)
                const std::array<const std::array<float, 3>*, 3> t = {{&t0, &t1, &t2}};
                for (int k = 0; k < 3; ++k) {
                    auto& uv = core::at(triangle, k).uv;
                    core::at(uv, 0) = static_cast<float>((static_cast<double>(core::at((*core::at(t, k)), 0)) + 1.0f) * 0.5f);
                    core::at(uv, 1) = static_cast<float>((static_cast<double>(core::at((*core::at(t, k)), 1)) + 1.0f) * 0.5f);
                }
                for (int k = 0; k < 3; ++k) {
                    core::at(core::at(splats->uvs, 3 * slot + k), 0) = core::at(core::at(triangle, k).uv, 0);
                    core::at(core::at(splats->uvs, 3 * slot + k), 1) = core::at(core::at(triangle, k).uv, 1);
                }
                const std::array<uint32_t, 3> index = {{ia, ib, ic}};
                for (int k = 0; k < 3; ++k)
                    core::at(triangle, k).argb =
                        splat_color(mesh_vertices.word(core::at(index, k), mesh->color_offset_));
                // how fine to split it: a 128th of its cross product's square
                {
                    const auto& p0 = v0.position;
                    const auto& p1 = v1.position;
                    const auto& p2 = v2.position;
                    const double ez1 = static_cast<double>(core::at(p1, 2)) - core::at(p0, 2);
                    const double ey1 = static_cast<double>(core::at(p1, 1)) - core::at(p0, 1);
                    const double ex1 = static_cast<double>(core::at(p1, 0)) - core::at(p0, 0);
                    auto ez2f = static_cast<float>(static_cast<double>(core::at(p2, 2)) - core::at(p0, 2));
                    auto ey2f = static_cast<float>(static_cast<double>(core::at(p2, 1)) - core::at(p0, 1));
                    const double ex2 = static_cast<double>(core::at(p2, 0)) - core::at(p0, 0);
                    auto cz = static_cast<float>(ey2f * ex1 - ex2 * ey1);
                    auto cy = static_cast<float>(ex2 * ez1 - ez2f * ex1);
                    const double cx = ez2f * ey1 - ey2f * ez1;
                    g_split_area_limit = static_cast<float>(
                        (cx * cx + static_cast<double>(cy) * cy + static_cast<double>(cz) * cz) * (1.0 / 128.0));
                }
                g_vertex_stack = VertexStack{}; // (emptied)
                g_vertex_stack.vertex_size_ = sizeof(ProjectedVertex);
                push_vertex(v0);
                push_vertex(v1);
                push_vertex(v2);
                auto* pieces = static_cast<uint8_t*>(memory::alloc(0x2000));
                std::memset(pieces, 0, 0x2000);
                void* memory = memory::alloc(sizeof(MeshRef));
                MeshRef* format = memory ? memory::construct_in<MeshRef>(memory) : nullptr;
                format->color_offset_ = offsetof(ProjectedVertex, argb); // (no check: as the original)
                format->uv_offset_ = offsetof(ProjectedVertex, uv);
                uint8_t* cursor = pieces;
                while (split_next_triangle(&cursor, format))
                    ;
                // the pieces inside the projection, each into a triangle of the set
                bool kept = false;
                const gpu::VertexView piece_vertices(pieces, sizeof(ProjectedVertex));
                const auto piece_count = static_cast<size_t>(cursor - pieces) / sizeof(ProjectedVertex);
                for (size_t first = 0; first != piece_count; first += 3) {
                    const std::array<ProjectedVertex, 3> piece = {
                        {piece_at(piece_vertices, first), piece_at(piece_vertices, first + 1),
                         piece_at(piece_vertices, first + 2)}};
                    auto twice = [&](int k, int axis) {
                        const double value = core::at(core::at(piece, k).uv, axis);
                        return static_cast<float>(value + value - 1.0f);
                    };
                    if (!triangle_overlaps_screen(twice(0, 0), twice(0, 1), twice(1, 0), twice(1, 1), twice(2, 0),
                                                  twice(2, 1)))
                        continue;
                    kept = true;
                    core::at(splats->alive, slot) = get_milliseconds(params->lifetime);
                    core::at(splats->correction_steps_, slot) = get_milliseconds(params->lifetime);
                    auto& into = core::at(splats->vertices, slot);
                    for (int k = 0; k < 3; ++k) {
                        core::at(into, k).position = core::at(piece, k).position;
                        core::at(into, k).uv = core::at(piece, k).uv;
                    }
                    for (int k = 0; k < 3; ++k) {
                        core::at(core::at(splats->uvs, 3 * slot + k), 0) = core::at(core::at(into, k).uv, 0);
                        core::at(core::at(splats->uvs, 3 * slot + k), 1) = core::at(core::at(into, k).uv, 1);
                    }
                    for (int k = 0; k < 3; ++k)
                        core::at(into, k).argb = splat_color(core::at(piece, k).argb);
                    // the base colours: grey from each vertex colour's first byte (its blue)
                    for (int k = 0; k < 3; ++k) {
                        const auto grey = static_cast<uint8_t>(core::at(into, k).argb & 0xFF);
                        core::at(core::at(splats->base_colors_, slot), k) = 0x80000000u | (grey << 16) | (grey << 8) | grey;
                    }
                    core::at(splats->correction_a_, slot) = params->depth;
                    core::at(splats->correction_b_, slot) = params->size;
                    slot = oldest_triangle(splats);
                }
                if (!kept) {
                    core::at(splats->alive, slot) = 0;
                    collapse_splat_triangle(splats, slot);
                }
                format->~MeshRef(); // (the format mesh's destructor body)
                memory::Hub::get_pull()->dealloc(format);
                memory::Hub::get_pull()->dealloc(pieces);
            }
        }
    }
    return splats;
}

} // namespace render
