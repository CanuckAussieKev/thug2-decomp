// Texture splats on PC and the projection's geometry helpers: see texture_splats.h. The maths follows the original's
// x87 code: double precision where it keeps values in registers, floats where it stores them.

#include "engine/gfx/texture_splats.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/d3dx.h"

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
#include "engine/lists/key_map.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/core/bits_as.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

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
lists::KeyMap* get_splats() { return g_splat_sets; }

// The table's first value, and the next one (null when there are no more).
TextureSplats* next_splats(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return static_cast<TextureSplats*>(table->iter_item_->value);
}
TextureSplats* first_splats(lists::KeyMap* table)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    return next_splats(table);
}

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
uint8_t* vertex_stack() { return raw::bytes(&g_vertex_stack); }
uint32_t get_vertex_size() { return g_vertex_stack.vertex_size_; }
void pop_vertex(uint8_t* out) { raw::as<VertexStack>(vertex_stack())->stack_off(out); }
void push_vertex(uint8_t* vertex) { raw::as<VertexStack>(vertex_stack())->stack_on(vertex); }

// A byte channel's midpoint as the original computes it: a + (b - a) / 2, the division rounding towards 0.
uint8_t byte_midpoint(uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + (static_cast<int>(b) - a) / 2); }
float midpoint(float a, float b) { return static_cast<float>((static_cast<double>(b) - a) * 0.5 + a); }

} // namespace

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
        std::array<maths::Vec4, 3> points{};
        for (auto& point : points)
            std::memcpy(&point, core::at(triangle, 0).data(), 12);
        light_vertex_colours(base, colors.data(), points.data(), nullptr, 3);
        for (int k = 0; k < 3; ++k)
            *raw::field_at<uint32_t>(core::at(triangle, k).data(), 0xC) = to_argb(core::at(colors, k));
    }
}

// 0x004B9200
void kill_all_texture_splats()
{
    TextureSplats* splats = first_splats(get_splats());
    while (splats) {
        if (MaterialRef* material = splats->material) {
            material->~MaterialRef(); // (the destructor's body)
            memory::Hub::get_pull()->dealloc(material);
        }
        TextureSplats* next = next_splats(get_splats());
        auto key = cell::of(splats);
        get_splats()->erase_entry(key);
        memory::Hub::get_pull()->dealloc(splats);
        splats = next;
    }
}

// 0x004B92A0
void kill_texture_splats_in_box(const float* box)
{
    TextureSplats* splats = first_splats(get_splats());
    while (splats) {
        TextureSplats* next = next_splats(get_splats());
        for (int i = 0; i < 0x100; ++i) {
            const auto* p = raw::as<const float>(core::at(core::at(splats->vertices, i), 0).data());
            if (p[0] >= box[0] && p[1] >= box[1] && p[2] >= box[2] && p[0] <= box[4] && p[1] <= box[5] &&
                p[2] <= box[6])
                core::at(splats->alive, i) = 0;
        }
        splats = next;
    }
}

// 0x004B9380
void draw_texture_splats()
{
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    input_port()->SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0);
    DWORD min_filter = 0;
    input_port()->GetSamplerState(0, D3DSAMP_MINFILTER, &min_filter);
    input_port()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    for (TextureSplats* splats = first_splats(get_splats()); splats; splats = next_splats(get_splats())) {
        if (splats->last < 0)
            continue;
        for (int j = 0; j <= splats->last; ++j) {
            double factor = 1.0;
            if (!(0.0f == core::at(splats->correction_a_, j) - core::at(splats->correction_b_, j))) {
                const double r = static_cast<double>(core::at(splats->correction_a_, j)) / core::at(splats->correction_b_, j);
                const double steps = core::at(splats->correction_steps_, j);
                const double t = (steps - static_cast<int>(core::at(splats->alive, j))) / steps;
                factor = 1.0 / (t * (1.0 - r) + r);
            }
            auto* uv = raw::field_at<float>(core::at(splats->vertices, 0).data()[0].data(), 0x18 * j + 0x10);
            uv[0] = static_cast<float>((static_cast<double>(core::at(core::at(splats->uvs, j), 0)) - 0.5f) * factor + 0.5f);
            uv[1] = static_cast<float>((static_cast<double>(core::at(core::at(splats->uvs, j), 1)) - 0.5f) * factor + 0.5f);
        }
        splats->material->hand_in(); // (the material's set-up)
        set_pixel_shader(core::at(g_pixel_shaders, kPixelShader964), splats->material);
        draw_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, splats->last + 1, splats->vertices.data(), 0x18);
    }
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
bool split_next_triangle(uint8_t** out, const SplitFormat* format)
{
    if (!*raw::as<uint32_t>(&g_vertex_stack))
        return false;
    uint8_t* v2 = g_splat_vertex2.data(); // (the three popped, last pushed first)
    uint8_t* v1 = g_splat_vertex1.data();
    uint8_t* v0 = g_splat_vertex0.data();
    pop_vertex(v2);
    pop_vertex(v1);
    pop_vertex(v0);
    auto* p0 = raw::as<float>(v0);
    auto* p1 = raw::as<float>(v1);
    auto* p2 = raw::as<float>(v2);
    // the edges from v0 and their cross product (as the original: some parts rounded to floats on the way)
    const double ax = static_cast<double>(p1[0]) - p0[0];
    const double ay = static_cast<double>(p1[1]) - p0[1];
    const double az = static_cast<double>(p1[2]) - p0[2];
    auto ayf = static_cast<float>(ay), azf = static_cast<float>(az);
    auto bxf = static_cast<float>(static_cast<double>(p2[0]) - p0[0]);
    auto byf = static_cast<float>(static_cast<double>(p2[1]) - p0[1]);
    const double bz = static_cast<double>(p2[2]) - p0[2];
    auto cross_x = static_cast<float>(bz * ay - byf * az);
    auto cross_y = static_cast<float>(az * bxf - bz * ax);
    const double cross_z = ax * byf - ay * bxf;
    const double length_squared =
        cross_z * cross_z + static_cast<double>(cross_y) * cross_y + static_cast<double>(cross_x) * cross_x;
    const uint32_t size = get_vertex_size();
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
    auto* q01 = raw::as<float>(m01);
    q01[0] = static_cast<float>(ax * 0.5 + p0[0]);
    std::memcpy(m12, v1, size);
    q01[1] = static_cast<float>(static_cast<double>(ayf) * 0.5 + p0[1]);
    std::memcpy(m20, v2, size);
    q01[2] = static_cast<float>(static_cast<double>(azf) * 0.5 + p0[2]);
    auto* q12 = raw::as<float>(m12);
    auto* q20 = raw::as<float>(m20);
    for (int k = 0; k < 3; ++k)
        q12[k] = midpoint(p1[k], p2[k]);
    for (int k = 0; k < 3; ++k)
        q20[k] = midpoint(p2[k], p0[k]);
    if (const uint8_t c = format->color_offset_) {
        for (int k = 0; k < 4; ++k) {
            m01[c + k] = byte_midpoint(v0[c + k], v1[c + k]);
            m12[c + k] = byte_midpoint(v1[c + k], v2[c + k]);
            m20[c + k] = byte_midpoint(v2[c + k], v0[c + k]);
        }
    }
    if (uint8_t t = format->uv_offset_) {
        for (int k = 0; k < 2; ++k) {
            auto at = [&](uint8_t* v) { return raw::field_at<float>(v, t) + k; };
            *at(m01) = midpoint(*at(v0), *at(v1));
            *at(m12) = midpoint(*at(v1), *at(v2));
            *at(m20) = midpoint(*at(v2), *at(v0));
        }
    }
    for (uint8_t* v : {v0, m01, m20, m01, v1, m12, m01, m12, m20, m20, m12, v2})
        push_vertex(v);
    return true;
}

namespace {

int get_rand() { return crt::get_rand(); } // (the game's C runtime's)
const float* get_at(const uint8_t* vertex) { return raw::as<const float>(vertex); }

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
int oldest_triangle(TextureSplats* splats) { return raw::as<SplatSlots>(splats)->oldest_slot(0x100); }
// The lifetime in milliseconds (the C runtime's _ftol: truncated).
int get_milliseconds(float seconds) { return static_cast<int>(static_cast<double>(seconds) * 1000.0f); }
// A vertex colour as the splats keep it: opaque-ish (alpha 0x80).
uint32_t splat_color(uint32_t color) { return (color & 0xFFFFFF) | 0x80000000; }

// The splat vertex layout: position, colour (+0x0C), u, v (+0x10).
float* get_uv(uint8_t* vertex) { return raw::field_at<float>(vertex, 0x10); }
uint32_t& get_color(uint8_t* vertex) { return *raw::field_at<uint32_t>(vertex, 0xC); }

} // namespace

// 0x004B9A60
TextureSplats* get_texture_splats(uint32_t texture)
{
    for (TextureSplats* splats = first_splats(get_splats()); splats; splats = next_splats(get_splats())) {
        if (cell::of(core::at(splats->material->textures, 0)) == texture)
            return splats;
    }
    void* memory = memory::alloc(sizeof(MaterialRef));
    MaterialRef* material = memory ? memory::construct_in<MaterialRef>(memory) : nullptr;
    // (no check: a failed allocation writes through null, as the original)
    core::at(material->pass_flags_, 0) = texture ? 4 : 0; // (the pass's flags)
    const int a = get_rand();
    const int b = get_rand();
    material->checksum = a * b;                                  // a random checksum
    core::at(material->textures, 0) = cell::pointer<uint8_t>(texture); // (the texture's key: its PC texture's address)
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
        uint8_t* vertex = core::at(core::at(splats->vertices, 0), 0).data() + 0x18 * v;
        std::memset(vertex, 0, 12);
        get_color(vertex) = 0x80808080;
    }
    auto key = cell::of(splats);
    get_splats()->store_entry(key, splats);
    return splats;
}

// 0x004B9C30
TextureSplats* create_texture_splat(void** sectors, int /*unused*/, SplatParams* params)
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
    std::array<float, 4> up{};
    maths::Mat4 view{}, projection{}, combined{};
    if (params->up) {
        const float* from = params->up;
        const double az = static_cast<double>(start[2]) - from[2];
        const double ay = static_cast<double>(start[1]) - from[1];
        core::at(up, 0) = static_cast<float>(static_cast<double>(start[0]) - from[0]);
        core::at(up, 1) = static_cast<float>(ay);
        core::at(up, 2) = static_cast<float>(az);
        core::at(up, 3) = 1.0f;
        auto height = static_cast<float>(std::sqrt(static_cast<double>(core::at(up, 0)) * core::at(up, 0) + ay * ay + az * az));
        start[0] = static_cast<float>(static_cast<double>(start[0]) - core::at(up, 0));
        start[1] = static_cast<float>(static_cast<double>(start[1]) - ay);
        start[2] = static_cast<float>(static_cast<double>(start[2]) - az);
        start[3] = static_cast<float>(static_cast<double>(start[3]) - 1.0f);
        end[0] = static_cast<float>(static_cast<double>(end[0]) - core::at(up, 0));
        end[1] = static_cast<float>(static_cast<double>(end[1]) - ay);
        end[2] = static_cast<float>(static_cast<double>(end[2]) - az);
        end[3] = static_cast<float>(static_cast<double>(end[3]) - 1.0f);
        raw::as<maths::Vec4>(up.data())->make_unit();
        get_look_at(&view, start, end, up.data());
        ortho(&projection, params->size, height, 0.1f, length);
    } else {
        if (std::fabs(core::at(direction, 1)) > 0.5f) {
            double a = params->angle;
            if (0.0f == params->angle)
                a = get_rand() * static_cast<double>(std::bit_cast<float>(0x40C90FDBu)) *
                    std::bit_cast<float>(0x38000100u); // (2 pi, 1 / 32767)
            core::at(up, 0) = static_cast<float>(std::sin(a));
            core::at(up, 1) = 0.0f;
            core::at(up, 2) = static_cast<float>(std::cos(a));
        } else {
            core::at(up, 0) = 0.0f;
            core::at(up, 1) = 1.0f;
            core::at(up, 2) = 0.0f;
        }
        core::at(up, 3) = 1.0f;
        get_look_at(&view, start, end, up.data());
        ortho(&projection, params->size, params->size, 0.1f, length);
    }
    get_multiply(&combined, &view, &projection);

    TextureSplats* splats = nullptr;
    uint8_t* splat_vertices = nullptr;
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
                const uint32_t stride = mesh->stride;
                std::array<float, 3> t0{};
                std::array<float, 3> t1{};
                std::array<float, 3> t2{};
                transform_coord(t0.data(), get_at(vertices + stride * ia), &combined);
                transform_coord(t1.data(), get_at(vertices + stride * ib), &combined);
                transform_coord(t2.data(), get_at(vertices + stride * ic), &combined);
                if ((core::at(t0, 2) < -1.0f && core::at(t1, 2) < -1.0f && core::at(t2, 2) < -1.0f) || (core::at(t0, 2) > 1.0f && core::at(t1, 2) > 1.0f && core::at(t2, 2) > 1.0f))
                    continue;
                if (!triangle_overlaps_screen(core::at(t0, 0), core::at(t0, 1), core::at(t1, 0), core::at(t1, 1), core::at(t2, 0), core::at(t2, 1)))
                    continue;
                if (!splat_vertices) {
                    uint8_t* holder = params->texture;
                    uint8_t* key = *raw::field_at<uint8_t*>(holder, 0x10);
                    if (holder[8] & 1)
                        key = *raw::as<uint8_t*>(key);
                    splats = get_texture_splats(cell::of(key));
                    splat_vertices = core::at(splats->vertices, 0).data()[0].data();
                }
                slot = oldest_triangle(splats);
                uint8_t* triangle = splat_vertices + slot * 0x48;
                uint8_t* v0 = triangle;
                uint8_t* v1 = triangle + 0x18;
                uint8_t* v2 = triangle + 0x30;
                std::memcpy(v0, vertices + stride * ia, 12);
                std::memcpy(v1, vertices + stride * ib, 12);
                std::memcpy(v2, vertices + stride * ic, 12);
                // the face's normal against the ray (as the original rounds it)
                {
                    const float* p0 = get_at(v0);
                    const float* p1 = get_at(v1);
                    const float* p2 = get_at(v2);
                    const double ez2 = static_cast<double>(p2[2]) - p0[2];
                    const double ey2 = static_cast<double>(p2[1]) - p0[1];
                    const double ex2 = static_cast<double>(p2[0]) - p0[0];
                    const double ez1 = static_cast<double>(p1[2]) - p0[2];
                    auto ey1f = static_cast<float>(static_cast<double>(p1[1]) - p0[1]);
                    auto ex1f = static_cast<float>(static_cast<double>(p1[0]) - p0[0]);
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
                const std::array<const float*, 3> t = {{t0.data(), t1.data(), t2.data()}};
                const std::array<uint8_t*, 3> v = {{v0, v1, v2}};
                for (int k = 0; k < 3; ++k) {
                    get_uv(core::at(v, k))[0] = static_cast<float>((static_cast<double>(core::at(t, k)[0]) + 1.0f) * 0.5f);
                    get_uv(core::at(v, k))[1] = static_cast<float>((static_cast<double>(core::at(t, k)[1]) + 1.0f) * 0.5f);
                }
                for (int k = 0; k < 3; ++k) {
                    core::at(core::at(splats->uvs, 3 * slot + k), 0) = get_uv(core::at(v, k))[0];
                    core::at(core::at(splats->uvs, 3 * slot + k), 1) = get_uv(core::at(v, k))[1];
                }
                const std::array<uint32_t, 3> index = {{ia, ib, ic}};
                for (int k = 0; k < 3; ++k)
                    get_color(core::at(v, k)) =
                        splat_color(*raw::field_at<uint32_t>(vertices, stride * core::at(index, k) + core::at(mesh->unknown1c_, 0)));
                // how fine to split it: a 128th of its cross product's square
                {
                    const float* p0 = get_at(v0);
                    const float* p1 = get_at(v1);
                    const float* p2 = get_at(v2);
                    const double ez1 = static_cast<double>(p1[2]) - p0[2];
                    const double ey1 = static_cast<double>(p1[1]) - p0[1];
                    const double ex1 = static_cast<double>(p1[0]) - p0[0];
                    auto ez2f = static_cast<float>(static_cast<double>(p2[2]) - p0[2]);
                    auto ey2f = static_cast<float>(static_cast<double>(p2[1]) - p0[1]);
                    const double ex2 = static_cast<double>(p2[0]) - p0[0];
                    auto cz = static_cast<float>(ey2f * ex1 - ex2 * ey1);
                    auto cy = static_cast<float>(ex2 * ez1 - ez2f * ex1);
                    const double cx = ez2f * ey1 - ey2f * ez1;
                    g_split_area_limit = static_cast<float>(
                        (cx * cx + static_cast<double>(cy) * cy + static_cast<double>(cz) * cz) * (1.0 / 128.0));
                }
                new (vertex_stack()) VertexStack; // (emptied)
                g_vertex_stack.vertex_size_ = 0x18;
                push_vertex(v0);
                push_vertex(v1);
                push_vertex(v2);
                auto* pieces = static_cast<uint8_t*>(memory::alloc(0x2000));
                std::memset(pieces, 0, 0x2000);
                void* memory = memory::alloc(0xD8);
                auto* format = memory ? raw::bytes(memory::construct_in<MeshRef>(memory)) : nullptr;
                format[0x1C] = 0x0C;
                format[0x1E] = 0x10;
                uint8_t* cursor = pieces;
                while (split_next_triangle(&cursor, raw::as<const SplitFormat>(format)))
                    ;
                // the pieces inside the projection, each into a triangle of the set
                bool kept = false;
                for (uint8_t* piece = pieces; piece != cursor; piece += 0x48) {
                    auto twice = [&](int offset) {
                        const double value = *raw::field_at<const float>(piece, offset);
                        return static_cast<float>(value + value - 1.0f);
                    };
                    if (!triangle_overlaps_screen(twice(0x10), twice(0x14), twice(0x28), twice(0x2C), twice(0x40),
                                                  twice(0x44)))
                        continue;
                    kept = true;
                    core::at(splats->alive, slot) = get_milliseconds(params->lifetime);
                    core::at(splats->correction_steps_, slot) = get_milliseconds(params->lifetime);
                    uint8_t* into = splat_vertices + slot * 0x48;
                    for (int k = 0; k < 3; ++k) {
                        std::memcpy(into + 0x18 * k, piece + 0x18 * k, 12);
                        std::memcpy(get_uv(into + 0x18 * k), get_uv(piece + 0x18 * k), 8);
                    }
                    for (int k = 0; k < 3; ++k) {
                        core::at(core::at(splats->uvs, 3 * slot + k), 0) = get_uv(into + 0x18 * k)[0];
                        core::at(core::at(splats->uvs, 3 * slot + k), 1) = get_uv(into + 0x18 * k)[1];
                    }
                    for (int k = 0; k < 3; ++k)
                        get_color(into + 0x18 * k) = splat_color(get_color(piece + 0x18 * k));
                    // the base colours: grey from each vertex colour's first byte
                    for (int k = 0; k < 3; ++k) {
                        const uint8_t grey = into[0x18 * k + 0xC];
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
                raw::as<MeshRef>(format)->~MeshRef(); // (the format mesh's destructor body)
                memory::Hub::get_pull()->dealloc(format);
                memory::Hub::get_pull()->dealloc(pieces);
            }
        }
    }
    return splats;
}

} // namespace render
