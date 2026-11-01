// The PC flexible particle systems: see pc_flexible_particles.h.

#include "engine/gfx/pc_flexible_particles.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/particle_system.h"
#include "engine/gfx/particle_types.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "platform/platform_d3d.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine.h"
#include "engine/core/owner.h"

namespace render {

IDirect3DIndexBuffer9* g_quad_indices;

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
IDirect3DIndexBuffer9* quad_indices() { return g_quad_indices; }
void* default_pixel_shader() { return g_pixel_shaders[kPixelShader964]; }
// The frame's draw counts: draws, triangles.
uint32_t& draw_calls() { return reinterpret_cast<uint32_t&>(g_draw_calls); }
uint32_t& drawn_triangles() { return reinterpret_cast<uint32_t&>(g_drawn_triangles); }

// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

void set_material(MaterialRef* material) { material->hand_in(); } // (its set-up)

// The camera's across and up directions from the view matrix's third column (a, b, c): r = (a, b, c) x (0, 1, 0)
// and u = r x (a, b, c), then normalised (maths::Vec4::make_unit).
void camera_axes(maths::Vec4& r, maths::Vec4& u)
{
    double a = g_view_matrix.row[0].z;
    double b = g_view_matrix.row[1].z;
    double c = g_view_matrix.row[2].z;
    double b0 = b * 0.0f;
    r.x = static_cast<float>(b0 - c);
    r.y = static_cast<float>(c * 0.0f - a * 0.0f);
    r.z = static_cast<float>(a - b0);
    r.w = 0.0f;
    u.x = static_cast<float>(r.y * c - r.z * b);
    u.y = static_cast<float>(r.z * a - c * r.x);
    u.z = static_cast<float>(b * r.x - r.y * a);
    u.w = 0.0f;
    r.make_unit();
    u.make_unit();
}

// A quad's vertex: position, colour, texture coordinates.
struct QuadVertex {
    float x, y, z;
    uint32_t colour;
    float u, v;
};
static_assert(sizeof(QuadVertex) == 0x18, "layout must match the original");

uint8_t clamped_byte(double value)
{
    if (value < 0.0f)
        value = 0.0f;
    else if (value > 255.0f)
        value = 255.0f;
    return static_cast<uint8_t>(truncate(value));
}

// The colours as the PC code reads them: RGBA words.
const uint32_t* as_rgba(const uint8_t* colours) { return reinterpret_cast<const uint32_t*>(colours); }

// 0x004BD470: the colour `t` of the way from `from` to `to` (RGBA bytes, each channel wrapping as a byte), its red,
// green and blue then scaled by the colour scale x the brightness (clamped to 0-255) - into *out as a d_3d colour
// (BGRA bytes). Returns out.
uint32_t* lerp_colour(const EmitterInstance& system, uint32_t* out, const uint8_t* from, const uint8_t* to, float t)
{
    const float* colour_scale = system.colour_scale_;
    const float brightness = system.brightness_;
    double red = static_cast<double>(colour_scale[0]) * brightness;
    double green = static_cast<double>(colour_scale[1]) * brightness;
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(from[k] + truncate(static_cast<double>(to[k] - from[k]) * t));
    };
    uint8_t b = lerp(2), g = lerp(1), r = lerp(0), a = lerp(3);
    double blue = static_cast<double>(colour_scale[2]) * brightness;
    uint8_t bytes[4] = {clamped_byte(blue * b), clamped_byte(green * g), clamped_byte(red * r), a};
    std::memcpy(out, bytes, 4);
    return out;
}

// The rate's particles for this frame (1/60 s) emitted through the engine's 0x00496FE0, the fraction kept.
void emit_for_frame(EmitterInstance& system, float& accumulator)
{
    if (!(system.emit_rate_ > 0.0f))
        return;
    double owed = static_cast<double>(system.emit_rate_) * (1.0f / 60.0f) + accumulator; // (0x006463B8)
    accumulator = static_cast<float>(owed);
    if (owed < 1.0f || owed != owed)
        return;
    system.get_emit(truncate(owed));
    accumulator = static_cast<float>(accumulator - static_cast<double>(truncate(accumulator)));
}

// Which colours a particle `life` of the way through its life is between, and how far.
struct ColourSpan {
    const uint32_t* from;
    const uint32_t* to;
    float t;
};
ColourSpan colour_span(const EmitterInstance& system, float life)
{
    const float colour_mid = system.colour_mid_;
    ColourSpan span{as_rgba(system.start_colour_), as_rgba(system.end_colour_), life};
    if (colour_mid >= 0.0f) {
        if (life < colour_mid) {
            span.t = static_cast<float>(static_cast<double>(life) / colour_mid);
            span.to = as_rgba(system.mid_colour_);
        } else {
            span.t =
                static_cast<float>((static_cast<double>(life) - colour_mid) / (1.0f - static_cast<double>(colour_mid)));
            span.from = as_rgba(system.mid_colour_);
        }
    }
    return span;
}

// lerp_colour of two colours (copies).
uint32_t get_colour(const EmitterInstance& system, uint32_t from, uint32_t to, float t)
{
    uint32_t colour = 0;
    lerp_colour(system, &colour, reinterpret_cast<const uint8_t*>(&from), reinterpret_cast<const uint8_t*>(&to), t);
    return colour;
}

// A PC class's destructor: its material destroyed and freed (then the engine class's destructor runs).
void free_material(SimpleMaterial* material)
{
    if (material) {
        material->~SimpleMaterial();
        memory::Hub::get_pull()->dealloc(material);
    }
}

// The axes of a quad facing along alignTo: r = alignTo x (0, 1, 0) (x when alignTo is y itself), u = r x alignTo,
// normalised.
void aligned_axes(const EmitterInstance& system, maths::Vec4& r, maths::Vec4& u)
{
    const maths::Vec4& align_to = system.align_to_;
    double ax = align_to.x, ay = align_to.y, az = align_to.z;
    double p = 0.0f, q = 1.0f; // (up: y - or x when alignTo is y itself)
    double dot = az * 0.0f + ax * 0.0f + ay;
    if (1.0f == dot)
        p = 1.0f, q = 0.0f;
    r.x = static_cast<float>(ay * 0.0f - az * q);
    r.y = static_cast<float>(az * p - ax * 0.0f);
    r.z = static_cast<float>(q * ax - ay * p);
    r.w = 0.0f;
    u.x = static_cast<float>(r.y * az - r.z * ay);
    u.y = static_cast<float>(r.z * ax - az * r.x);
    u.z = static_cast<float>(ay * r.x - r.y * ax);
    u.w = 0.0f;
    r.make_unit();
    u.make_unit();
}

// What a system's constructor is given: (name, max, texture, blend, alpha, segments, unknown), and some also the
// history; the shaded class only the first five; the engine's model class its model name instead of the texture.
struct ConstructArgs {
    uint32_t name;
    int particle_cap_;
    uint32_t texture;
    uint32_t blend_mode_;
    int fixed_alpha_;
    int segments;
    float unknown;
    int history;
    const char* model;
};
template <typename T>
memory::owner<EmitterInstance*> make_plain(const ConstructArgs& a)
{
    return new T(a.name, a.particle_cap_, a.texture, a.blend_mode_, a.fixed_alpha_, a.segments, a.unknown);
}
template <typename T>
memory::owner<EmitterInstance*> make_with_history(const ConstructArgs& a)
{
    return new T(a.name, a.particle_cap_, a.texture, a.blend_mode_, a.fixed_alpha_, a.segments, a.unknown, a.history);
}
template <typename T>
memory::owner<EmitterInstance*> make_five(const ConstructArgs& a)
{
    return new T(a.name, a.particle_cap_, a.texture, a.blend_mode_, a.fixed_alpha_);
}
memory::owner<EmitterInstance*> make_model(const ConstructArgs& a)
{
    return new ModelParticles(a.name, a.particle_cap_, a.model, a.blend_mode_, a.fixed_alpha_, a.segments, a.unknown,
                              a.history);
}

} // namespace

// 0x004BCEF0
EmitterInstance* create_flexible_particles(uint32_t name, uint32_t type, int particle_cap, int, uint32_t texture,
                                           uint32_t blend_mode, int fixed_alpha, int segments, float unknown,
                                           int history, const char* model)
{
    struct EmitterType {
        uint32_t type;
        EmitterInstance* (*construct)(const ConstructArgs&); // a new one of the class (null if no memory)
    };
    static constexpr EmitterType kTypes[] = {
        {0x15834EEA, &make_plain<PcGlowParticles>},                   // glow
        {0x0EE6FC5B, &make_with_history<PcRibbonParticles>},          // ribbon
        {0x097CB7A9, &make_plain<PcSmoothStarParticles>},             // smoothstar
        {0x286A8D26, &make_model},                                    // (the engine's model class)
        {0x3624A5EB, &make_plain<PcStarParticles>},                   // star
        {0x2EEB4B09, &make_plain<PcLineParticles>},                   // line
        {0x3D56A7A6, &make_with_history<PcFlatRibbonParticles>},      // (a flat ribbon)
        {0x8ADDAC1F, &make_plain<PcSmoothParticles>},                 // smooth
        {0x7EC7252D, &make_with_history<PcGlowRibbonTrailParticles>}, // glowribbontrail
        {0x3F109FCC, &make_with_history<PcSmoothRibbonParticles>},    // smoothribbon
        {0xC4D5A4CB, &make_with_history<PcRibbonTrailParticles>},     // ribbontrail
        {0xF4D8D486, &make_five<PcShadedParticles>},                  // shaded
        {0xDEDFC057, &make_plain<PcNewFlatParticles>},                // newflat
    };
    const EmitterType* chosen = &kTypes[12]; // (any other type: newflat)
    for (const EmitterType& t : kTypes)
        if (t.type == type)
            chosen = &t;
    const ConstructArgs args{name, particle_cap, texture, blend_mode, fixed_alpha, segments, unknown, history, model};
    return chosen->construct(args); // (zeroed memory on the current heap, with the assert flag: the system's new)
}

// --- newflat ---

// 0x004BD3A0
PcNewFlatParticles::PcNewFlatParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                       int fixed_alpha, int segments, float unknown)
    : NewFlatParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segments, unknown, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory) SimpleMaterial(particle_cap, 1, texture, blend_mode, fixed_alpha, 0, 0) : nullptr;
}

// 0x004BD610
PcNewFlatParticles::~PcNewFlatParticles() { free_material(material); }

// 0x004BD680
void PcNewFlatParticles::render()
{
    if (particle_count_ > 0) {
        maths::Vec4 r, u;
        camera_axes(r, u);
        set_material(material->material);
        set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        set_pixel_shader(default_pixel_shader(), nullptr);
        const float* place = position_array_;
        FlexibleParticle* particle = particle_list_;
        int remaining = particle_count_;
        int index = 0;
        while (remaining != 0) {
            IDirect3DVertexBuffer9* buffer = nullptr;
            uint32_t first_vertex = 0, room = 0;
            auto* vertex = static_cast<QuadVertex*>(os_layer::lock_dynamic_vertices(
                particle_count_ * 4, sizeof(QuadVertex), &buffer, &first_vertex, &room));
            uint32_t quads = 0;
            if (index < particle_count_) {
                for (;;) {
                    auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                    double across = (static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                                    particle->start_size_[0];
                    double up = (static_cast<double>(particle->end_size_[1]) - particle->start_size_[1]) * life +
                                particle->start_size_[1];
                    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
                    if (emitter_independent_) {
                        cx = place[0], cy = place[1], cz = place[2];
                    } else {
                        cx = static_cast<float>(static_cast<double>(at_point_.x) + place[0]);
                        cy = static_cast<float>(static_cast<double>(place[1]) + at_point_.y);
                        cz = static_cast<float>(static_cast<double>(place[2]) + at_point_.z);
                    }
                    auto rx = static_cast<float>(r.x * across), ry = static_cast<float>(r.y * across),
                         rz = static_cast<float>(r.z * across);
                    auto ux = static_cast<float>(u.x * up), uy = static_cast<float>(u.y * up),
                         uz = static_cast<float>(u.z * up);

                    ColourSpan span = colour_span(*this, life);
                    uint32_t colour = get_colour(*this, *span.from, *span.to, span.t);

                    double dx = static_cast<double>(cx) - rx, dy = static_cast<double>(cy) - ry,
                           dz = static_cast<double>(cz) - rz;
                    auto fdx = static_cast<float>(dx), fdy = static_cast<float>(dy), fdz = static_cast<float>(dz);
                    double sz = static_cast<double>(rz) + cz;
                    auto fsx = static_cast<float>(static_cast<double>(rx) + cx),
                         fsy = static_cast<float>(static_cast<double>(ry) + cy), fsz = static_cast<float>(sz);
                    vertex[0] = {static_cast<float>(static_cast<double>(fdx) + ux),
                                 static_cast<float>(static_cast<double>(fdy) + uy),
                                 static_cast<float>(dz + uz),
                                 colour,
                                 0.0f,
                                 0.0f};
                    vertex[1] = {static_cast<float>(static_cast<double>(fsx) + ux),
                                 static_cast<float>(static_cast<double>(fsy) + uy),
                                 static_cast<float>(sz + uz),
                                 colour,
                                 1.0f,
                                 0.0f};
                    vertex[2] = {static_cast<float>(static_cast<double>(fsx) - ux),
                                 static_cast<float>(static_cast<double>(fsy) - uy),
                                 static_cast<float>(static_cast<double>(fsz) - uz),
                                 colour,
                                 1.0f,
                                 1.0f};
                    vertex[3] = {static_cast<float>(static_cast<double>(fdx) - ux),
                                 static_cast<float>(static_cast<double>(fdy) - uy),
                                 static_cast<float>(static_cast<double>(fdz) - uz),
                                 colour,
                                 0.0f,
                                 1.0f};
                    vertex += 4;
                    ++quads;
                    if (quads >= room - 4)
                        break;
                    ++particle;
                    ++index;
                    place += 3;
                    if (index >= particle_count_)
                        break;
                }
            }
            remaining -= static_cast<int>(quads);
            os_layer::unlock_dynamic_vertices(quads * 4, sizeof(QuadVertex));
            if (quads >= room - 4)
                break; // (full: the original stops here, undrawn)
            input_port()->SetStreamSource(0, buffer, 0, sizeof(QuadVertex));
            input_port()->SetIndices(quad_indices());
            input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, quads * 4, 0,
                                               quads * 2);
            ++draw_calls();
            drawn_triangles() += quads * 2;
        }
    }
    emit_for_frame(*this, emitted);
}

// --- the flat ribbon (0x3D56A7A6) ---

namespace {

// A ribbon's vertex: position, colour.
struct TrailVertex {
    float x, y, z;
    uint32_t colour;
};
static_assert(sizeof(TrailVertex) == 0x10, "layout must match the original");

// A particle's position (`index` in the positions), the emitter's added unless emitterIndependent.
void place_of(const EmitterInstance* system, int index, float out[3])
{
    const float* p = system->position_array_ + 3 * index;
    if (system->emitter_independent_) {
        out[0] = p[0], out[1] = p[1], out[2] = p[2];
    } else {
        out[0] = static_cast<float>(static_cast<double>(system->at_point_.x) + p[0]);
        out[1] = static_cast<float>(static_cast<double>(p[1]) + system->at_point_.y);
        out[2] = static_cast<float>(static_cast<double>(p[2]) + system->at_point_.z);
    }
}

// Across the path from p0 to p1 and the camera's direction (a, b, c): (p1 - p0) x (a, b, c), normalised when it has
// a length. The ribbon's first segment rounds every component first; the later ones keep z unrounded.
void get_across(const float p0[3], const float p1[3], double a, double b, double c, bool first, float n[3])
{
    double dx = static_cast<double>(p1[0]) - p0[0], dy = static_cast<double>(p1[1]) - p0[1],
           dz = static_cast<double>(p1[2]) - p0[2];
    n[0] = static_cast<float>(c * dy - b * dz);
    n[1] = static_cast<float>(a * dz - c * dx);
    double z = b * dx - a * dy;
    n[2] = static_cast<float>(z);
    if (first)
        z = n[2];
    double length = std::sqrt(static_cast<double>(n[0]) * n[0] + z * z + static_cast<double>(n[1]) * n[1]);
    if (length > 0.0f) {
        double inverse = 1.0f / length;
        n[0] = static_cast<float>(n[0] * inverse);
        n[1] = static_cast<float>(n[1] * inverse);
        n[2] = static_cast<float>(inverse * z);
    }
}

} // namespace

// 0x004BDC80
PcFlatRibbonParticles::PcFlatRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                             int fixed_alpha, int segment_count, float ribbon_width, int history)
    : FlatRibbonParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, ribbon_width, history)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory)
                            SimpleMaterial(particle_cap, 0xB, texture, blend_mode, fixed_alpha, segment_count, history)
                      : nullptr;
    width = ribbon_width;
}

// 0x004BDD60
PcFlatRibbonParticles::~PcFlatRibbonParticles() { free_material(material); }

// 0x004BDDD0
void PcFlatRibbonParticles::render()
{
    if (particle_count_ <= 0)
        return;
    double a = g_view_matrix.row[0].z;
    double b = g_view_matrix.row[1].z;
    double c = g_view_matrix.row[2].z;
    maths::Vec4 r, u;
    camera_axes(r, u);
    uint32_t particle_bytes = (segments_ - 1) * 16;
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(g_pixel_shaders[kPixelShader95c], material->material);

    // The ribbons.
    FlexibleParticle* particle = particle_list_;
    int remaining = (segments_ - 1) * particle_count_ * 4;
    int index = 0;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<TrailVertex*>(
            os_layer::lock_dynamic_vertices(remaining, sizeof(TrailVertex), &buffer, &first_vertex, &room));
        if (index < particle_count_) {
            for (uint32_t bytes = particle_bytes; bytes <= 0x7FF; bytes += particle_bytes) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                float p0[3], p1[3], n[3];
                place_of(this, index, p0);
                place_of(this, max_particles_ + index, p1);
                get_across(p0, p1, a, b, c, true, n);
                auto w =
                    static_cast<float>((static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                                       particle->start_size_[0]);
                ColourSpan span = colour_span(*this, life);
                const uint32_t* from = span.from;
                const uint32_t* to = span.to;
                uint32_t previous_colour = get_colour(*this, *from++, *to++, span.t);

                // The head end's edge points.
                double n0w = static_cast<double>(n[0]) * w, n1w = static_cast<double>(w) * n[1],
                       n2w = static_cast<double>(w) * n[2];
                auto o0 = static_cast<float>(static_cast<double>(static_cast<float>(n0w)) * width);
                auto o1 = static_cast<float>(width * n1w);
                float previous_a[3] = {static_cast<float>(static_cast<double>(o0) + p0[0]),
                                       static_cast<float>(static_cast<double>(o1) + p0[1]),
                                       static_cast<float>(width * n2w + p0[2])};
                float previous_b[3] = {
                    static_cast<float>(static_cast<double>(p0[0]) - o0),
                    static_cast<float>(static_cast<double>(p0[1]) - o1),
                    static_cast<float>(p0[2] - width * static_cast<double>(static_cast<float>(n2w)))};

                for (int k = 1; k < segments_; ++k) {
                    uint32_t colour = get_colour(*this, *from++, *to++, span.t);
                    if (k > 1) {
                        place_of(this, max_particles_ * k + index, p1);
                        get_across(p0, p1, a, b, c, false, n);
                    }
                    n0w = static_cast<double>(n[0]) * w;
                    n1w = static_cast<double>(w) * n[1];
                    n2w = static_cast<double>(w) * n[2];
                    o0 = static_cast<float>(static_cast<double>(static_cast<float>(n0w)) * width);
                    o1 = static_cast<float>(width * n1w);
                    float next_b[3] = {static_cast<float>(static_cast<double>(p1[0]) - o0),
                                       static_cast<float>(static_cast<double>(p1[1]) - o1),
                                       static_cast<float>(p1[2] - width * n2w)};
                    auto o2 = static_cast<float>(width * static_cast<double>(static_cast<float>(n2w)));
                    float next_a[3] = {static_cast<float>(static_cast<double>(o0) + p1[0]),
                                       static_cast<float>(static_cast<double>(o1) + p1[1]),
                                       static_cast<float>(static_cast<double>(o2) + p1[2])};
                    vertex[0] = {previous_a[0], previous_a[1], previous_a[2], previous_colour};
                    vertex[1] = {previous_b[0], previous_b[1], previous_b[2], previous_colour};
                    vertex[2] = {next_a[0], next_a[1], next_a[2], colour};
                    vertex[3] = {next_b[0], next_b[1], next_b[2], colour};
                    vertex += 4;
                    std::memcpy(previous_a, next_a, sizeof(next_a));
                    std::memcpy(previous_b, next_b, sizeof(next_b));
                    std::memcpy(p0, p1, sizeof(p1));
                    previous_colour = colour;
                    triangles += 2;
                    vertices += 4;
                    if (vertices >= room - 4)
                        break;
                }
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                if (index >= particle_count_)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - 4)
            break;
        input_port()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        input_port()->SetIndices(quad_indices());
        input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        ++draw_calls();
        drawn_triangles() += triangles;
    }

    // The heads.
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    set_pixel_shader(default_pixel_shader(), nullptr);
    particle = particle_list_;
    remaining = particle_count_ * 4;
    index = 0;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<QuadVertex*>(
            os_layer::lock_dynamic_vertices(remaining, sizeof(QuadVertex), &buffer, &first_vertex, &room));
        if (index < particle_count_) {
            for (uint32_t bytes = sizeof(QuadVertex); bytes <= 0x7FF; bytes += sizeof(QuadVertex)) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                double size = ((static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                               particle->start_size_[0]) *
                              width * 5.0f; // (0x006469AC)
                auto r0 = static_cast<float>(r.x * size), r1 = static_cast<float>(r.y * size),
                     r2 = static_cast<float>(r.z * size);
                auto u0 = static_cast<float>(u.x * size), u1 = static_cast<float>(u.y * size);
                double u2 = u.z * size;
                const float* p = position_array_ + 3 * index;
                double px = 0.0, py = 0.0;
                float pz = 0.0f;
                if (emitter_independent_) {
                    px = p[0], py = p[1], pz = p[2];
                } else {
                    px = static_cast<double>(at_point_.x) + p[0];
                    py = static_cast<double>(p[1]) + at_point_.y;
                    pz = static_cast<float>(static_cast<double>(p[2]) + at_point_.z);
                }
                double e0 = px + r0;
                auto e1 = static_cast<float>(py + r1), e2 = static_cast<float>(static_cast<double>(pz) + r2);
                auto g0 = static_cast<float>(px - r0);
                double g1 = py - r1, g2 = static_cast<double>(pz) - r2;
                auto g1f = static_cast<float>(g1);
                ColourSpan span = colour_span(*this, life);
                uint32_t colour = get_colour(*this, *span.from, *span.to, span.t);
                vertex[0] = {static_cast<float>(e0 - u0),
                             static_cast<float>(static_cast<double>(e1) - u1),
                             static_cast<float>(e2 - u2),
                             colour,
                             0.0f,
                             0.0f};
                vertex[1] = {static_cast<float>(static_cast<double>(g0) - u0),
                             static_cast<float>(static_cast<double>(g1f) - u1),
                             static_cast<float>(g2 - u2),
                             colour,
                             1.0f,
                             0.0f};
                vertex[2] = {static_cast<float>(static_cast<double>(g0) + u0),
                             static_cast<float>(static_cast<double>(g1f) + u1),
                             static_cast<float>(g2 + u2),
                             colour,
                             1.0f,
                             1.0f};
                vertex[3] = {static_cast<float>(static_cast<double>(static_cast<float>(e0)) + u0),
                             static_cast<float>(static_cast<double>(e1) + u1),
                             static_cast<float>(e2 + u2),
                             colour,
                             0.0f,
                             1.0f};
                vertex += 4;
                vertices += 4;
                triangles += 2;
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                if (index >= particle_count_)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(QuadVertex));
        if (vertices >= room - 4)
            break;
        input_port()->SetStreamSource(0, buffer, 0, sizeof(QuadVertex));
        input_port()->SetIndices(quad_indices());
        input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        ++draw_calls();
        drawn_triangles() += triangles;
    }
}

// --- glow ---

// 0x004BED10
PcGlowParticles::PcGlowParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                 int fixed_alpha, int segment_count, float unknown)
    : GlowParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory) SimpleMaterial(particle_cap, 4, texture, blend_mode, fixed_alpha, segment_count, 0)
                      : nullptr;
}

// 0x004BEDE0
PcGlowParticles::~PcGlowParticles() { free_material(material); }

// 0x004BEE50
void PcGlowParticles::render()
{
    if (particle_count_ <= 0)
        return;
    maths::Vec4 r, u;
    camera_axes(r, u);
    uint32_t particle_words = segments_ * 9 * 4; // (nine vertices of four words a segment)
    uint32_t remaining = particle_count_ * particle_words;
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(g_pixel_shaders[kPixelShader95c], material->material);
    FlexibleParticle* particle = particle_list_;
    const float* place = position_array_;
    int index = 0;
    TrailVertex buffer[0x200];
    while (remaining > 0) {
        TrailVertex* vertex = buffer;
        uint32_t words = 0;
        int total = particle_count_;
        for (uint32_t budget = particle_words; index < total && budget <= 0x7FF; budget += particle_words) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            double across = (static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                            particle->start_size_[0];
            double up = (static_cast<double>(particle->end_size_[1]) - particle->start_size_[1]) * life +
                        particle->start_size_[1];
            float p[3];
            if (emitter_independent_) {
                p[0] = place[0], p[1] = place[1], p[2] = place[2];
            } else {
                p[0] = static_cast<float>(static_cast<double>(at_point_.x) + place[0]);
                p[1] = static_cast<float>(static_cast<double>(place[1]) + at_point_.y);
                p[2] = static_cast<float>(static_cast<double>(place[2]) + at_point_.z);
            }
            auto rx = static_cast<float>(r.x * across), ry = static_cast<float>(r.y * across),
                 rz = static_cast<float>(r.z * across);
            auto ux = static_cast<float>(u.x * up), uy = static_cast<float>(u.y * up),
                 uz = static_cast<float>(u.z * up);
            // The centre's, the edge's and the outer ring's colours (each its own of the three colour arrays').
            ColourSpan span = colour_span(*this, life);
            uint32_t colours[3];
            for (int k = 0; k < 3; ++k)
                colours[k] = get_colour(*this, span.from[k], span.to[k], span.t);
            double w = glow_width_;

            // The rings' points at angle 0: outer (r sin, u cos, times the width), inner.
            double s = std::sin(0.0), c = std::cos(0.0);
            float outer[3], inner[3];
            {
                double ry_s = static_cast<float>(ry * s), rz_s = static_cast<float>(rz * s);
                outer[0] = static_cast<float>(rx * s * w + p[0]);
                outer[1] = static_cast<float>(ry_s * w + p[1]);
                outer[2] = static_cast<float>(static_cast<double>(static_cast<float>(rz_s * w)) + p[2]);
                double uy_c = static_cast<float>(uy * c), uz_c = static_cast<float>(uz * c);
                outer[0] = static_cast<float>(ux * c * w + outer[0]);
                outer[1] = static_cast<float>(uy_c * w + outer[1]);
                outer[2] = static_cast<float>(static_cast<double>(static_cast<float>(uz_c * w)) + outer[2]);
                inner[0] = static_cast<float>(rx * s + p[0]);
                inner[1] = static_cast<float>(static_cast<double>(static_cast<float>(ry * s)) + p[1]);
                inner[2] = static_cast<float>(static_cast<double>(static_cast<float>(rz * s)) + p[2]);
                inner[0] = static_cast<float>(ux * c + inner[0]);
                inner[1] = static_cast<float>(static_cast<double>(static_cast<float>(uy * c)) + inner[1]);
                inner[2] = static_cast<float>(static_cast<double>(static_cast<float>(uz * c)) + inner[2]);
            }
            auto step = static_cast<float>(6.28318548f / static_cast<double>(segments_)); // (0x0064545C)
            for (int i = 1; i <= segments_; ++i) {
                double angle = static_cast<double>(i) * step;
                double si = std::sin(angle), ci = std::cos(angle);
                float next_outer[3], next_inner[3];
                double rsw = rx * si * w;
                double r1 = static_cast<double>(static_cast<float>(ry * si)) * w;
                double r2 =
                    static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(rz * si)) * w)) +
                    p[2];
                auto cosine = static_cast<float>(ci);
                double usw = ux * ci * w;
                double u1 = static_cast<double>(static_cast<float>(uy * ci)) * w;
                auto u2 = static_cast<float>(static_cast<double>(static_cast<float>(uz * ci)) * w);
                next_outer[0] = static_cast<float>(usw + static_cast<float>(rsw + p[0]));
                next_outer[1] = static_cast<float>(u1 + (r1 + p[1]));
                next_outer[2] = static_cast<float>(u2 + r2);
                double x = rx * si + p[0];
                double y = static_cast<double>(static_cast<float>(ry * si)) + p[1];
                auto z = static_cast<float>(static_cast<double>(static_cast<float>(rz * si)) + p[2]);
                next_inner[0] = static_cast<float>(ux * static_cast<double>(cosine) + x);
                next_inner[1] =
                    static_cast<float>(static_cast<double>(static_cast<float>(uy * static_cast<double>(cosine))) + y);
                next_inner[2] =
                    static_cast<float>(static_cast<double>(static_cast<float>(uz * static_cast<double>(cosine))) + z);
                vertex[0] = {p[0], p[1], p[2], colours[0]};
                vertex[1] = {outer[0], outer[1], outer[2], colours[1]};
                vertex[2] = {next_outer[0], next_outer[1], next_outer[2], colours[1]};
                vertex[3] = {outer[0], outer[1], outer[2], colours[1]};
                vertex[4] = {next_outer[0], next_outer[1], next_outer[2], colours[1]};
                vertex[5] = {inner[0], inner[1], inner[2], colours[2]};
                vertex[6] = {next_outer[0], next_outer[1], next_outer[2], colours[1]};
                vertex[7] = {inner[0], inner[1], inner[2], colours[2]};
                vertex[8] = {next_inner[0], next_inner[1], next_inner[2], colours[2]};
                vertex += 9;
                std::memcpy(outer, next_outer, sizeof(outer));
                std::memcpy(inner, next_inner, sizeof(inner));
            }
            words += particle_words;
            remaining -= particle_words;
            ++index;
            place += 3;
            ++particle;
        }
        uint32_t triangles = (words >> 4) / 3; // (the original's count: a quarter of what it wrote)
        if (!g_suppress_draw) {                // (rendering not skipped)
            drawn_triangles() += triangles;
            ++draw_calls();
            input_port()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, buffer, sizeof(TrailVertex));
        }
    }
}

// --- line ---

// 0x004C09F0
PcLineParticles::PcLineParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                 int fixed_alpha, int segment_count, float unknown)
    : LineParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory) SimpleMaterial(particle_cap, 0, texture, blend_mode, fixed_alpha, 0, 0) : nullptr;
}

// 0x004C0AC0
PcLineParticles::~PcLineParticles() { free_material(material); }

// 0x004C0B30
void PcLineParticles::render()
{
    if (particle_count_ <= 0)
        return;
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(g_pixel_shaders[kPixelShader95c], material->material);
    int remaining = particle_count_ * 2;
    int index = 0;
    FlexibleParticle* particle = particle_list_;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, lines = 0;
        auto* vertex = static_cast<TrailVertex*>(
            os_layer::lock_dynamic_vertices(particle_count_ * 2, sizeof(TrailVertex), &buffer, &first_vertex, &room));
        while (index < particle_count_) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            float p0[3], p1[3];
            place_of(this, index, p0);
            place_of(this, max_particles_ + index, p1);
            ColourSpan span = colour_span(*this, life);
            uint32_t colours[2];
            for (int k = 0; k < 2; ++k)
                colours[k] = get_colour(*this, span.from[k], span.to[k], span.t);
            vertex[0] = {p0[0], p0[1], p0[2], colours[0]};
            vertex[1] = {p1[0], p1[1], p1[2], colours[1]};
            vertex += 2;
            vertices += 2;
            ++lines;
            if (vertices >= room - 2)
                break;
            ++index;
            ++particle;
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - 2)
            break;
        input_port()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        input_port()->DrawPrimitive(D3DPT_LINELIST, first_vertex, lines);
        ++draw_calls();
        drawn_triangles() += lines;
    }
}

// --- ribbon ---

// 0x004C0EB0
PcRibbonParticles::PcRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                     int fixed_alpha, int segment_count, float unknown, int history)
    : RibbonParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, history)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory) SimpleMaterial(particle_cap, 7, texture, blend_mode, fixed_alpha, segment_count, 0)
                      : nullptr;
}

// 0x004C0F80
PcRibbonParticles::~PcRibbonParticles() { free_material(material); }

// 0x004C0FF0
void PcRibbonParticles::render()
{
    if (particle_count_ <= 0)
        return;
    double a = g_view_matrix.row[0].z;
    double b = g_view_matrix.row[1].z;
    double c = g_view_matrix.row[2].z;
    bool textured = material->material->textures[0] != nullptr;
    uint32_t stride = textured ? sizeof(QuadVertex) : sizeof(TrailVertex);
    set_material(material->material);
    if (textured) {
        set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        set_pixel_shader(default_pixel_shader(), nullptr);
    } else {
        set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        set_pixel_shader(g_pixel_shaders[kPixelShader95c], material->material);
    }
    FlexibleParticle* particle = particle_list_;
    const float* head = position_array_;
    const float* tail = position_array_ + 3 * (steps_ - 1) * max_particles_;
    int index = 0;
    int remaining = particle_count_ * 4;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* out =
            static_cast<uint8_t*>(os_layer::lock_dynamic_vertices(remaining, stride, &buffer, &first_vertex, &room));
        while (index < particle_count_) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            double hx = 0.0, hy = 0.0;
            float hz = 0.0f, t[3];
            if (emitter_independent_) {
                hx = head[0], hy = head[1], hz = head[2];
                t[0] = tail[0], t[1] = tail[1], t[2] = tail[2];
            } else {
                hx = static_cast<double>(at_point_.x) + head[0];
                hy = static_cast<double>(head[1]) + at_point_.y;
                hz = static_cast<float>(static_cast<double>(head[2]) + at_point_.z);
                t[0] = static_cast<float>(static_cast<double>(at_point_.x) + tail[0]);
                t[1] = static_cast<float>(static_cast<double>(tail[1]) + at_point_.y);
                t[2] = static_cast<float>(static_cast<double>(tail[2]) + at_point_.z);
            }
            // Across the ribbon: (tail - head) x the camera's direction, normalised when it has a length.
            auto dx = static_cast<float>(t[0] - hx), dy = static_cast<float>(t[1] - hy),
                 dz = static_cast<float>(static_cast<double>(t[2]) - hz);
            double n0 = dy * c - dz * b;
            double n1 = dz * a - c * dx;
            auto n2 = static_cast<float>(b * dx - dy * a);
            double length = std::sqrt(n0 * n0 + static_cast<double>(n2) * n2 + n1 * n1);
            if (length > 0.0f) {
                double inverse = 1.0f / length;
                n0 *= inverse;
                n1 *= inverse;
                n2 = static_cast<float>(n2 * inverse);
            }
            auto w =
                static_cast<float>((static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                                   particle->start_size_[0]);
            double n0w = n0 * w, n1w = n1 * w, n2w = static_cast<double>(n2) * w;
            auto f0 = static_cast<float>(n0w), f1 = static_cast<float>(n1w), f2 = static_cast<float>(n2w);
            float corners[4][3] = {
                {static_cast<float>(f0 + hx), static_cast<float>(f1 + hy), static_cast<float>(n2w + hz)},
                {static_cast<float>(hx - f0), static_cast<float>(hy - f1),
                 static_cast<float>(static_cast<double>(hz) - f2)},
                {static_cast<float>(static_cast<double>(t[0]) - f0), static_cast<float>(static_cast<double>(t[1]) - f1),
                 static_cast<float>(static_cast<double>(t[2]) - f2)},
                {static_cast<float>(static_cast<double>(f0) + t[0]), static_cast<float>(static_cast<double>(f1) + t[1]),
                 static_cast<float>(static_cast<double>(f2) + t[2])}};
            ColourSpan span = colour_span(*this, life);
            uint32_t colours[2];
            for (int k = 0; k < 2; ++k)
                colours[k] = get_colour(*this, span.from[k], span.to[k], span.t);
            static constexpr float kUv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            for (int k = 0; k < 4; ++k) {
                uint32_t colour = colours[k / 2];
                if (textured) {
                    reinterpret_cast<QuadVertex*>(out)[k] = {corners[k][0], corners[k][1], corners[k][2],
                                                             colour,        kUv[k][0],     kUv[k][1]};
                } else {
                    reinterpret_cast<TrailVertex*>(out)[k] = {corners[k][0], corners[k][1], corners[k][2], colour};
                }
            }
            out += 4 * stride;
            vertices += 4;
            triangles += 2;
            if (vertices >= room - 4)
                break;
            head += 3;
            tail += 3;
            ++index;
            ++particle;
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, stride);
        if (vertices >= room - 4)
            break;
        input_port()->SetStreamSource(0, buffer, 0, stride);
        input_port()->SetIndices(quad_indices());
        input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        ++draw_calls();
        drawn_triangles() += triangles;
    }
}

// --- ribbontrail ---

// The colour of a ribbon trail's head end (inline in its Render): lerp_colour's channels, the red, green and blue
// scaled by the brightness alone, not clamped (each the low byte).
uint32_t PcRibbonTrailParticles::head_colour(uint32_t from, uint32_t to, float t)
{
    const auto* f = reinterpret_cast<const uint8_t*>(&from);
    const auto* g = reinterpret_cast<const uint8_t*>(&to);
    auto lerp = [&](int k) { return static_cast<uint8_t>(f[k] + truncate(static_cast<double>(g[k] - f[k]) * t)); };
    uint8_t bytes[4] = {lerp(2), lerp(1), lerp(0), lerp(3)};
    for (int k = 0; k < 3; ++k)
        bytes[k] = static_cast<uint8_t>(truncate(static_cast<double>(bytes[k]) * brightness_));
    uint32_t colour = 0;
    std::memcpy(&colour, bytes, 4);
    return colour;
}

// 0x004C1720
PcRibbonTrailParticles::PcRibbonTrailParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                               int fixed_alpha, int segment_count, float unknown, int history)
    : RibbonTrailParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, history)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory)
                            SimpleMaterial(particle_cap, 9, texture, blend_mode, fixed_alpha, segment_count, history)
                      : nullptr;
}

// 0x004C17F0
PcRibbonTrailParticles::~PcRibbonTrailParticles() { free_material(material); }

// 0x004C1860
void PcRibbonTrailParticles::render()
{
    if (particle_count_ <= 0)
        return;
    double a = g_view_matrix.row[0].z;
    double b = g_view_matrix.row[1].z;
    double c = g_view_matrix.row[2].z;
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(g_pixel_shaders[kPixelShader95c], material->material);
    FlexibleParticle* particle = particle_list_;
    int remaining = (steps_ - 1) * particle_count_ * 4;
    int index = 0;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<TrailVertex*>(
            os_layer::lock_dynamic_vertices(remaining, sizeof(TrailVertex), &buffer, &first_vertex, &room));
        while (index < particle_count_) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            float p0[3], p1[3], n[3];
            place_of(this, index, p0);
            place_of(this, max_particles_ + index, p1);
            {
                double dx = static_cast<double>(p1[0]) - p0[0], dy = static_cast<double>(p1[1]) - p0[1],
                       dz = static_cast<double>(p1[2]) - p0[2];
                n[0] = static_cast<float>(dy * c - dz * b);
                n[1] = static_cast<float>(dz * a - c * dx);
                n[2] = static_cast<float>(b * dx - dy * a);
                double length = std::sqrt(static_cast<double>(n[2]) * n[2] + static_cast<double>(n[1]) * n[1] +
                                          static_cast<double>(n[0]) * n[0]);
                if (length > 0.0f) {
                    double inverse = 1.0f / length;
                    for (float& f : n)
                        f = static_cast<float>(f * inverse);
                }
            }
            auto w =
                static_cast<float>((static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                                   particle->start_size_[0]);
            ColourSpan span = colour_span(*this, life);
            const uint32_t* from = span.from;
            const uint32_t* to = span.to;
            uint32_t previous_colour = head_colour(*from++, *to++, span.t);
            double n0w = static_cast<double>(n[0]) * w, n1w = static_cast<double>(n[1]) * w,
                   n2w = static_cast<double>(n[2]) * w;
            auto f0 = static_cast<float>(n0w), f1 = static_cast<float>(n1w), f2 = static_cast<float>(n2w);
            float previous_plus[3] = {static_cast<float>(static_cast<double>(f0) + p0[0]),
                                      static_cast<float>(static_cast<double>(f1) + p0[1]),
                                      static_cast<float>(n2w + p0[2])};
            float previous_minus[3] = {static_cast<float>(static_cast<double>(p0[0]) - f0),
                                       static_cast<float>(static_cast<double>(p0[1]) - f1),
                                       static_cast<float>(static_cast<double>(p0[2]) - f2)};
            for (int k = 1; k < steps_; ++k) {
                uint32_t colour = get_colour(*this, *from++, *to++, span.t);
                if (k > 1) {
                    place_of(this, max_particles_ * k + index, p1);
                    double dx = static_cast<double>(p1[0]) - p0[0], dy = static_cast<double>(p1[1]) - p0[1],
                           dz = static_cast<double>(p1[2]) - p0[2];
                    n[0] = static_cast<float>(dy * c - dz * b);
                    n[1] = static_cast<float>(dz * a - c * dx);
                    double z = b * dx - dy * a;
                    n[2] = static_cast<float>(z);
                    double length =
                        std::sqrt(z * z + static_cast<double>(n[1]) * n[1] + static_cast<double>(n[0]) * n[0]);
                    if (length > 0.0f) {
                        double inverse = 1.0f / length;
                        n[0] = static_cast<float>(n[0] * inverse);
                        n[1] = static_cast<float>(n[1] * inverse);
                        n[2] = static_cast<float>(z * inverse);
                    }
                }
                n0w = static_cast<double>(n[0]) * w;
                n1w = static_cast<double>(n[1]) * w;
                n2w = static_cast<double>(n[2]) * w;
                f0 = static_cast<float>(n0w), f1 = static_cast<float>(n1w), f2 = static_cast<float>(n2w);
                float plus[3] = {static_cast<float>(static_cast<double>(f0) + p1[0]), static_cast<float>(n1w + p1[1]),
                                 static_cast<float>(n2w + p1[2])};
                float minus[3] = {static_cast<float>(static_cast<double>(p1[0]) - f0),
                                  static_cast<float>(static_cast<double>(p1[1]) - f1),
                                  static_cast<float>(static_cast<double>(p1[2]) - f2)};
                vertex[0] = {previous_plus[0], previous_plus[1], previous_plus[2], previous_colour};
                vertex[1] = {previous_minus[0], previous_minus[1], previous_minus[2], previous_colour};
                vertex[2] = {minus[0], minus[1], minus[2], colour};
                vertex[3] = {plus[0], plus[1], plus[2], colour};
                vertex += 4;
                previous_colour = colour;
                std::memcpy(p0, p1, sizeof(p1));
                std::memcpy(previous_plus, plus, sizeof(plus));
                std::memcpy(previous_minus, minus, sizeof(minus));
                triangles += 2;
                vertices += 4;
                if (vertices >= room - 4)
                    break;
            }
            if (vertices >= room - 4)
                break;
            ++index;
            ++particle;
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - 4)
            break;
        input_port()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        input_port()->SetIndices(quad_indices());
        input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        ++draw_calls();
        drawn_triangles() += triangles;
    }
}

// --- shaded ---

// 0x004C21A0
PcShadedParticles::PcShadedParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                     int fixed_alpha)
    : ShadedParticles(name, particle_cap, texture, blend_mode, fixed_alpha, 0, 0, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory) SimpleMaterial(particle_cap, 2, texture, blend_mode, fixed_alpha, 0, 0) : nullptr;
}

// 0x004C2260
PcShadedParticles::~PcShadedParticles() { free_material(material); }

// 0x004C22D0
void PcShadedParticles::render()
{
    if (particle_count_ <= 0)
        return;
    maths::Vec4 r, u;
    if (aligned_)
        aligned_axes(*this, r, u);
    else
        camera_axes(r, u);
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    set_pixel_shader(material->shader, nullptr);
    FlexibleParticle* particle = particle_list_;
    const float* place = position_array_;
    int index = 0;
    int remaining = particle_count_ * 4;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<QuadVertex*>(
            os_layer::lock_dynamic_vertices(remaining, sizeof(QuadVertex), &buffer, &first_vertex, &room));
        if (index < particle_count_) {
            for (uint32_t bytes = sizeof(QuadVertex); bytes <= 0x7FF; bytes += sizeof(QuadVertex)) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                double across = (static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                                particle->start_size_[0];
                double up = (static_cast<double>(particle->end_size_[1]) - particle->start_size_[1]) * life +
                            particle->start_size_[1];
                float p[3];
                if (emitter_independent_) {
                    p[0] = place[0], p[1] = place[1], p[2] = place[2];
                } else {
                    p[0] = static_cast<float>(static_cast<double>(at_point_.x) + place[0]);
                    p[1] = static_cast<float>(static_cast<double>(place[1]) + at_point_.y);
                    p[2] = static_cast<float>(static_cast<double>(place[2]) + at_point_.z);
                }
                auto rx = static_cast<float>(r.x * across), ry = static_cast<float>(r.y * across),
                     rz = static_cast<float>(r.z * across);
                auto ux = static_cast<float>(u.x * up), uy = static_cast<float>(u.y * up),
                     uz = static_cast<float>(u.z * up);
                // The corners' colours (each its own of the colour arrays').
                ColourSpan span = colour_span(*this, life);
                uint32_t colours[4];
                for (int k = 0; k < 4; ++k)
                    colours[k] = get_colour(*this, span.from[k], span.to[k], span.t);
                // The texture: all of it, or (UseRandomUVQuadrants) a quarter by the particle's index.
                float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
                if (flag_bits_ & 0x4000) {
                    switch (index % 4) {
                    case 0:
                        u0 = 0.5f, v0 = 0.5f;
                        break;
                    case 1:
                        v1 = 0.5f, u0 = 0.5f;
                        break;
                    case 2:
                        v1 = 0.5f, u1 = 0.5f;
                        break;
                    case 3:
                        u1 = 0.5f, v0 = 0.5f;
                        break;
                    default:
                        break;
                    }
                }
                double e0 = static_cast<double>(rx) + p[0], e1 = static_cast<double>(ry) + p[1],
                       e2 = static_cast<double>(rz) + p[2];
                auto e0f = static_cast<float>(e0), e1f = static_cast<float>(e1), e2f = static_cast<float>(e2);
                double g0 = static_cast<double>(p[0]) - rx, g1 = static_cast<double>(p[1]) - ry,
                       g2 = static_cast<double>(p[2]) - rz;
                auto g0f = static_cast<float>(g0), g1f = static_cast<float>(g1), g2f = static_cast<float>(g2);
                vertex[0] = {static_cast<float>(static_cast<double>(e0f) - ux),
                             static_cast<float>(static_cast<double>(e1f) - uy),
                             static_cast<float>(e2 - uz),
                             colours[0],
                             u0,
                             v0};
                vertex[1] = {static_cast<float>(static_cast<double>(g0f) - ux),
                             static_cast<float>(static_cast<double>(g1f) - uy),
                             static_cast<float>(g2 - uz),
                             colours[1],
                             u1,
                             v0};
                vertex[2] = {static_cast<float>(static_cast<double>(g0f) + ux),
                             static_cast<float>(static_cast<double>(g1f) + uy),
                             static_cast<float>(static_cast<double>(g2f) + uz),
                             colours[2],
                             u1,
                             v1};
                vertex[3] = {static_cast<float>(static_cast<double>(e0f) + ux),
                             static_cast<float>(static_cast<double>(e1f) + uy),
                             static_cast<float>(static_cast<double>(e2f) + uz),
                             colours[3],
                             u0,
                             v1};
                vertex += 4;
                vertices += 4;
                triangles += 2;
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                place += 3;
                if (index >= particle_count_)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(QuadVertex));
        if (vertices >= room - 4)
            break;
        input_port()->SetStreamSource(0, buffer, 0, sizeof(QuadVertex));
        input_port()->SetIndices(quad_indices());
        input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        ++draw_calls();
        drawn_triangles() += triangles;
    }
}

// --- smooth ---

// 0x004C2B30
PcSmoothParticles::PcSmoothParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                     int fixed_alpha, int segment_count, float unknown)
    : SmoothParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory) SimpleMaterial(particle_cap, 3, texture, blend_mode, fixed_alpha, 0, 0) : nullptr;
}

// 0x004C2C00
PcSmoothParticles::~PcSmoothParticles() { free_material(material); }

// 0x004C2C70
void PcSmoothParticles::render()
{
    if (particle_count_ <= 0)
        return;
    maths::Vec4 r, u;
    if (aligned_)
        aligned_axes(*this, r, u);
    else
        camera_axes(r, u);
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    set_pixel_shader(default_pixel_shader(), nullptr);
    FlexibleParticle* particle = particle_list_;
    const float* place = position_array_;
    int index = 0;
    int remaining = particle_count_ * 12;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<QuadVertex*>(
            os_layer::lock_dynamic_vertices(remaining, sizeof(QuadVertex), &buffer, &first_vertex, &room));
        while (index < particle_count_) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            double across = (static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                            particle->start_size_[0];
            auto up =
                static_cast<float>((static_cast<double>(particle->end_size_[1]) - particle->start_size_[1]) * life +
                                   particle->start_size_[1]);
            float p[3];
            if (emitter_independent_) {
                p[0] = place[0], p[1] = place[1], p[2] = place[2];
            } else {
                p[0] = static_cast<float>(static_cast<double>(at_point_.x) + place[0]);
                p[1] = static_cast<float>(static_cast<double>(place[1]) + at_point_.y);
                p[2] = static_cast<float>(static_cast<double>(place[2]) + at_point_.z);
            }
            auto rx = static_cast<float>(r.x * across), ry = static_cast<float>(r.y * across),
                 rz = static_cast<float>(r.z * across);
            double ux = static_cast<double>(u.x) * up;
            auto uy = static_cast<float>(static_cast<double>(u.y) * up),
                 uz = static_cast<float>(static_cast<double>(u.z) * up);
            double g0 = static_cast<double>(p[0]) - rx, g1 = static_cast<double>(p[1]) - ry,
                   g2 = static_cast<double>(p[2]) - rz;
            auto g0f = static_cast<float>(g0), g1f = static_cast<float>(g1), g2f = static_cast<float>(g2);
            double e0 = static_cast<double>(rx) + p[0], e1 = static_cast<double>(ry) + p[1];
            auto e0f = static_cast<float>(e0), e1f = static_cast<float>(e1),
                 e2f = static_cast<float>(static_cast<double>(rz) + p[2]);
            // The four corners (each with a triangle to the centre).
            float k[4][3] = {{static_cast<float>(g0f + ux), static_cast<float>(static_cast<double>(g1f) + uy),
                              static_cast<float>(g2 + uz)},
                             {static_cast<float>(e0f + ux), static_cast<float>(static_cast<double>(e1f) + uy),
                              static_cast<float>(static_cast<double>(e2f) + uz)},
                             {static_cast<float>(e0f - ux), static_cast<float>(static_cast<double>(e1f) - uy),
                              static_cast<float>(static_cast<double>(e2f) - uz)},
                             {static_cast<float>(g0f - ux), static_cast<float>(static_cast<double>(g1f) - uy),
                              static_cast<float>(static_cast<double>(g2f) - uz)}};
            static constexpr float kUv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            // The centre's colour and the edge's.
            ColourSpan span = colour_span(*this, life);
            uint32_t colours[2];
            for (int i = 0; i < 2; ++i)
                colours[i] = get_colour(*this, span.from[i], span.to[i], span.t);
            for (int corner = 0; corner < 4; ++corner) {
                int next = (corner + 1) % 4;
                vertex[0] = {p[0], p[1], p[2], colours[0], 0.0f, 0.0f};
                vertex[1] = {k[corner][0], k[corner][1], k[corner][2], colours[1], kUv[corner][0], kUv[corner][1]};
                vertex[2] = {k[next][0], k[next][1], k[next][2], colours[1], kUv[next][0], kUv[next][1]};
                vertex += 3;
            }
            vertices += 12;
            triangles += 4;
            if (vertices >= room - 12)
                break;
            ++particle;
            ++index;
            place += 3;
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(QuadVertex));
        if (vertices >= room - 12)
            break;
        input_port()->SetStreamSource(0, buffer, 0, sizeof(QuadVertex));
        input_port()->DrawPrimitive(D3DPT_TRIANGLELIST, first_vertex, triangles);
        ++draw_calls();
        drawn_triangles() += triangles;
    }
}

// --- smoothribbon ---

// 0x004C3550
PcSmoothRibbonParticles::PcSmoothRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                                 int fixed_alpha, int segment_count, float unknown, int history)
    : SmoothRibbonParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, history)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory) SimpleMaterial(particle_cap, 8, texture, blend_mode, fixed_alpha, 0, 0) : nullptr;
}

// 0x004C3620
PcSmoothRibbonParticles::~PcSmoothRibbonParticles() { free_material(material); }

// 0x004C3530
int PcSmoothRibbonParticles::get_num_vertex_lists() { return segments; }

// 0x004C3690
void PcSmoothRibbonParticles::render()
{
    if (particle_count_ <= 0)
        return;
    double a = g_view_matrix.row[0].z;
    double b = g_view_matrix.row[1].z;
    double c = g_view_matrix.row[2].z;
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(g_pixel_shaders[kPixelShader95c], material->material);
    const float* head = position_array_;
    const float* tail = position_array_ + 3 * (segments - 1) * max_particles_;
    FlexibleParticle* particle = particle_list_;
    int index = 0;
    int remaining = particle_count_ * 8;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<TrailVertex*>(
            os_layer::lock_dynamic_vertices(remaining, sizeof(TrailVertex), &buffer, &first_vertex, &room));
        if (index < particle_count_) {
            for (uint32_t bytes = 0x20; bytes <= 0x7FF; bytes += 0x20) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                float h[3], t[3];
                if (emitter_independent_) {
                    h[0] = head[0], h[1] = head[1], h[2] = head[2];
                    t[0] = tail[0], t[1] = tail[1], t[2] = tail[2];
                } else {
                    for (int k = 0; k < 3; ++k) {
                        h[k] = static_cast<float>(static_cast<double>(head[k]) + (&at_point_.x)[k]);
                        t[k] = static_cast<float>(static_cast<double>(tail[k]) + (&at_point_.x)[k]);
                    }
                }
                auto dx = static_cast<float>(static_cast<double>(t[0]) - h[0]),
                     dy = static_cast<float>(static_cast<double>(t[1]) - h[1]),
                     dz = static_cast<float>(static_cast<double>(t[2]) - h[2]);
                double n0 = dy * c - dz * b;
                double n1 = dz * a - c * dx;
                auto n2 = static_cast<float>(b * dx - dy * a);
                double length = std::sqrt(n0 * n0 + static_cast<double>(n2) * n2 + n1 * n1);
                if (length > 0.0f) {
                    double inverse = 1.0f / length;
                    n0 *= inverse;
                    n1 *= inverse;
                    n2 = static_cast<float>(n2 * inverse);
                }
                auto w =
                    static_cast<float>((static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                                       particle->start_size_[0]);
                double n0w = n0 * w, n1w = n1 * w, n2w = static_cast<double>(n2) * w;
                auto f0 = static_cast<float>(n0w), f1 = static_cast<float>(n1w), f2 = static_cast<float>(n2w);
                float plus_head[3] = {static_cast<float>(static_cast<double>(f0) + h[0]),
                                      static_cast<float>(static_cast<double>(f1) + h[1]),
                                      static_cast<float>(n2w + h[2])};
                float minus_head[3] = {static_cast<float>(static_cast<double>(h[0]) - f0),
                                       static_cast<float>(static_cast<double>(h[1]) - f1),
                                       static_cast<float>(static_cast<double>(h[2]) - f2)};
                float minus_tail[3] = {static_cast<float>(static_cast<double>(t[0]) - f0),
                                       static_cast<float>(static_cast<double>(t[1]) - f1),
                                       static_cast<float>(static_cast<double>(t[2]) - f2)};
                float plus_tail[3] = {static_cast<float>(static_cast<double>(f0) + t[0]),
                                      static_cast<float>(static_cast<double>(f1) + t[1]),
                                      static_cast<float>(n2w + t[2])};
                ColourSpan span = colour_span(*this, life);
                uint32_t colours[4];
                for (int k = 0; k < 4; ++k)
                    colours[k] = get_colour(*this, span.from[k], span.to[k], span.t);
                // Two quads along the middle: to the plus side and to the minus side.
                vertex[0] = {h[0], h[1], h[2], colours[0]};
                vertex[1] = {t[0], t[1], t[2], colours[3]};
                vertex[2] = {plus_tail[0], plus_tail[1], plus_tail[2], colours[2]};
                vertex[3] = {plus_head[0], plus_head[1], plus_head[2], colours[1]};
                vertex[4] = {h[0], h[1], h[2], colours[0]};
                vertex[5] = {t[0], t[1], t[2], colours[3]};
                vertex[6] = {minus_tail[0], minus_tail[1], minus_tail[2], colours[2]};
                vertex[7] = {minus_head[0], minus_head[1], minus_head[2], colours[1]};
                vertex += 8;
                vertices += 8;
                triangles += 4;
                if (vertices >= room - 8)
                    break;
                ++particle;
                ++index;
                head += 3;
                tail += 3;
                if (index >= particle_count_)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - 8)
            break;
        input_port()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        input_port()->SetIndices(quad_indices());
        input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        ++draw_calls();
        drawn_triangles() += triangles;
    }
}

// --- smoothstar ---

// 0x004C3D80
PcSmoothStarParticles::PcSmoothStarParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                             int fixed_alpha, int segment_count, float unknown)
    : SmoothStarParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory) SimpleMaterial(particle_cap, 6, texture, blend_mode, fixed_alpha, segment_count, 0)
                      : nullptr;
}

// 0x004C3E50
PcSmoothStarParticles::~PcSmoothStarParticles() { free_material(material); }

// 0x004C3EC0
void PcSmoothStarParticles::render()
{
    if (particle_count_ <= 0)
        return;
    maths::Vec4 r, u;
    camera_axes(r, u);
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(g_pixel_shaders[kPixelShader95c], material->material);
    FlexibleParticle* particle = particle_list_;
    const float* place = position_array_;
    int index = 0;
    int remaining = segments_ * particle_count_ * 8;
    constexpr float kDegrees = 0.0174532924f; // (0x00645464)
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<TrailVertex*>(
            os_layer::lock_dynamic_vertices(remaining, sizeof(TrailVertex), &buffer, &first_vertex, &room));
        if (index < particle_count_) {
            auto sin0 = static_cast<float>(std::sin(0.0)), cos0 = static_cast<float>(std::cos(0.0));
            for (;;) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                double across = (static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                                particle->start_size_[0];
                double up_size = (static_cast<double>(particle->end_size_[1]) - particle->start_size_[1]) * life +
                                 particle->start_size_[1];
                float p[3];
                if (emitter_independent_) {
                    p[0] = place[0], p[1] = place[1], p[2] = place[2];
                } else {
                    p[0] = static_cast<float>(static_cast<double>(at_point_.x) + place[0]);
                    p[1] = static_cast<float>(static_cast<double>(place[1]) + at_point_.y);
                    p[2] = static_cast<float>(static_cast<double>(place[2]) + at_point_.z);
                }
                float rv[3] = {static_cast<float>(r.x * across), static_cast<float>(r.y * across),
                               static_cast<float>(r.z * across)};
                float uv[3] = {static_cast<float>(u.x * up_size), static_cast<float>(u.y * up_size),
                               static_cast<float>(u.z * up_size)};
                // The centre's, the points' and the edge's colours.
                ColourSpan span = colour_span(*this, life);
                uint32_t colours[3];
                for (int k = 0; k < 3; ++k)
                    colours[k] = get_colour(*this, span.from[k], span.to[k], span.t);
                double w = extent_;
                // The outer point at angle 0 (the previous one for the first segment).
                float outer[3];
                {
                    double x = static_cast<double>(rv[0]) * sin0;
                    double y = static_cast<double>(rv[1]) * sin0;
                    auto z = static_cast<float>(static_cast<double>(rv[2]) * sin0);
                    auto ox = static_cast<float>(static_cast<double>(static_cast<float>(x * w)) + p[0]);
                    auto oy = static_cast<float>(y * w + p[1]);
                    double oz = static_cast<double>(z) * w + p[2];
                    double ux = static_cast<double>(cos0) * uv[0];
                    double uy = static_cast<double>(uv[1]) * cos0;
                    auto uz = static_cast<float>(static_cast<double>(uv[2]) * cos0);
                    outer[0] = static_cast<float>(static_cast<double>(static_cast<float>(ux * w)) + ox);
                    outer[1] = static_cast<float>(uy * w + oy);
                    outer[2] =
                        static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(uz) * w)) + oz);
                }
                for (int i = 0, k = 1; i < segments_; ++i, k += 2) {
                    auto step = static_cast<float>(360.0f / (static_cast<double>(segments_) + segments_));
                    // The point between this segment's two outer points, out at the star's width.
                    double angle1 = static_cast<double>(k) * step * kDegrees;
                    double s1 = std::sin(angle1), c1 = std::cos(angle1);
                    auto s1f = static_cast<float>(s1);
                    float tip[3];
                    {
                        double x = rv[0] * s1 * w;
                        double y = static_cast<double>(static_cast<float>(rv[1] * s1)) * w;
                        auto z = static_cast<float>(static_cast<double>(static_cast<float>(rv[2] * s1)) * w);
                        tip[0] = static_cast<float>(x + p[0]);
                        tip[1] = static_cast<float>(y + p[1]);
                        tip[2] = static_cast<float>(static_cast<double>(z) + p[2]);
                        double ux = uv[0] * c1 * w;
                        double uy = static_cast<double>(static_cast<float>(uv[1] * c1)) * w;
                        auto uz = static_cast<float>(static_cast<double>(static_cast<float>(uv[2] * c1)) * w);
                        tip[0] = static_cast<float>(ux + tip[0]);
                        tip[1] = static_cast<float>(uy + tip[1]);
                        tip[2] = static_cast<float>(static_cast<double>(uz) + tip[2]);
                    }
                    // The next outer point.
                    double angle2 = static_cast<double>(k + 1) * step * kDegrees;
                    double s2 = std::sin(angle2), c2 = std::cos(angle2);
                    float next[3];
                    double next_y = 0.0;
                    {
                        double x = rv[0] * s2 * w;
                        double y = static_cast<double>(static_cast<float>(rv[1] * s2)) * w;
                        auto z = static_cast<float>(static_cast<double>(static_cast<float>(rv[2] * s2)) * w);
                        auto nx = static_cast<float>(x + p[0]);
                        double ny = y + p[1];
                        auto nz = static_cast<float>(static_cast<double>(z) + p[2]);
                        double ux = uv[0] * c2 * w;
                        auto uy = static_cast<float>(static_cast<double>(static_cast<float>(uv[1] * c2)) * w);
                        auto uz = static_cast<float>(static_cast<double>(static_cast<float>(uv[2] * c2)) * w);
                        next[0] = static_cast<float>(ux + nx);
                        next_y = static_cast<double>(uy) + ny;
                        next[1] = static_cast<float>(next_y);
                        next[2] = static_cast<float>(static_cast<double>(uz) + nz);
                    }
                    // The inner point at angle 1 (unscaled).
                    float inner[3];
                    {
                        double x = static_cast<double>(rv[0]) * s1f + p[0];
                        auto ry = static_cast<float>(static_cast<double>(rv[1]) * s1f);
                        auto rz = static_cast<float>(static_cast<double>(rv[2]) * s1f);
                        auto iy = static_cast<float>(static_cast<double>(ry) + p[1]);
                        auto iz = static_cast<float>(static_cast<double>(rz) + p[2]);
                        auto ux = static_cast<float>(uv[0] * c1), uy = static_cast<float>(uv[1] * c1),
                             uz = static_cast<float>(uv[2] * c1);
                        inner[0] = static_cast<float>(static_cast<double>(ux) + x);
                        inner[1] = static_cast<float>(static_cast<double>(uy) + iy);
                        inner[2] = static_cast<float>(static_cast<double>(uz) + iz);
                    }
                    vertex[0] = {tip[0], tip[1], tip[2], colours[1]};
                    vertex[1] = {p[0], p[1], p[2], colours[0]};
                    vertex[2] = {next[0], next[1], next[2], colours[2]};
                    vertex[3] = {inner[0], inner[1], inner[2], colours[2]};
                    vertex[4] = {tip[0], tip[1], tip[2], colours[1]};
                    vertex[5] = {p[0], p[1], p[2], colours[0]};
                    vertex[6] = {outer[0], outer[1], outer[2], colours[2]};
                    vertex[7] = {inner[0], inner[1], inner[2], colours[2]};
                    vertex += 8;
                    std::memcpy(outer, next, sizeof(next));
                    vertices += 8;
                    triangles += 4;
                    if (vertices >= room - 8)
                        break;
                }
                if (vertices >= room - 8)
                    break;
                ++index;
                ++particle;
                place += 3;
                if (index >= particle_count_)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - 8)
            break;
        input_port()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        input_port()->SetIndices(quad_indices());
        input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        ++draw_calls();
        drawn_triangles() += triangles;
    }
}

// --- star ---

// 0x004C48C0
PcStarParticles::PcStarParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                 int fixed_alpha, int segment_count, float unknown)
    : StarParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory) SimpleMaterial(particle_cap, 5, texture, blend_mode, fixed_alpha, segment_count, 0)
                      : nullptr;
}

// 0x004C4990
PcStarParticles::~PcStarParticles() { free_material(material); }

// 0x004C4A00
void PcStarParticles::render()
{
    if (particle_count_ <= 0)
        return;
    maths::Vec4 r, u;
    camera_axes(r, u);
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(g_pixel_shaders[kPixelShader95c], material->material);
    FlexibleParticle* particle = particle_list_;
    const float* place = position_array_;
    int index = 0;
    int remaining = segments_ * particle_count_ * 4;
    constexpr float kDegrees = 0.0174532924f; // (0x00645464)
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0, room = 0, vertices = 0, triangles = 0;
        auto* vertex = static_cast<TrailVertex*>(
            os_layer::lock_dynamic_vertices(remaining, sizeof(TrailVertex), &buffer, &first_vertex, &room));
        if (index < particle_count_) {
            auto sin0 = static_cast<float>(std::sin(0.0)), cos0 = static_cast<float>(std::cos(0.0));
            for (;;) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                double across = (static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                                particle->start_size_[0];
                double up_size = (static_cast<double>(particle->end_size_[1]) - particle->start_size_[1]) * life +
                                 particle->start_size_[1];
                float p[3];
                if (emitter_independent_) {
                    p[0] = place[0], p[1] = place[1], p[2] = place[2];
                } else {
                    p[0] = static_cast<float>(static_cast<double>(at_point_.x) + place[0]);
                    p[1] = static_cast<float>(static_cast<double>(place[1]) + at_point_.y);
                    p[2] = static_cast<float>(static_cast<double>(place[2]) + at_point_.z);
                }
                float rv[3] = {static_cast<float>(r.x * across), static_cast<float>(r.y * across),
                               static_cast<float>(r.z * across)};
                float uv[3] = {static_cast<float>(u.x * up_size), static_cast<float>(u.y * up_size),
                               static_cast<float>(u.z * up_size)};
                ColourSpan span = colour_span(*this, life);
                uint32_t colours[3];
                for (int k = 0; k < 3; ++k)
                    colours[k] = get_colour(*this, span.from[k], span.to[k], span.t);
                double w = extent_;
                // The outer point at angle 0 (the previous one for the first segment).
                float outer[3];
                {
                    double x = static_cast<double>(rv[0]) * sin0;
                    double y = static_cast<double>(rv[1]) * sin0;
                    auto z = static_cast<float>(static_cast<double>(rv[2]) * sin0);
                    auto ox = static_cast<float>(static_cast<double>(static_cast<float>(x * w)) + p[0]);
                    auto oy = static_cast<float>(y * w + p[1]);
                    double oz = static_cast<double>(z) * w + p[2];
                    double ux = static_cast<double>(cos0) * uv[0];
                    double uy = static_cast<double>(uv[1]) * cos0;
                    auto uz = static_cast<float>(static_cast<double>(uv[2]) * cos0);
                    outer[0] = static_cast<float>(static_cast<double>(static_cast<float>(ux * w)) + ox);
                    outer[1] = static_cast<float>(uy * w + oy);
                    outer[2] =
                        static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(uz) * w)) + oz);
                }
                for (int k = 1; k <= segments_; ++k) {
                    // The next outer point (out at the width) and the inner one between (unscaled).
                    double angle_a = 360.0f / static_cast<double>(segments_) * k * kDegrees;
                    double a = std::sin(angle_a), c_a = std::cos(angle_a);
                    float next[3];
                    {
                        double x = rv[0] * a * w;
                        double y = static_cast<double>(static_cast<float>(rv[1] * a)) * w;
                        auto z = static_cast<float>(static_cast<double>(static_cast<float>(rv[2] * a)) * w);
                        auto nx = static_cast<float>(x + p[0]);
                        double ny = y + p[1];
                        auto nz = static_cast<float>(static_cast<double>(z) + p[2]);
                        double ux = uv[0] * c_a * w;
                        auto uy = static_cast<float>(static_cast<double>(static_cast<float>(uv[1] * c_a)) * w);
                        auto uz = static_cast<float>(static_cast<double>(static_cast<float>(uv[2] * c_a)) * w);
                        next[0] = static_cast<float>(ux + nx);
                        next[1] = static_cast<float>(static_cast<double>(uy) + ny);
                        next[2] = static_cast<float>(static_cast<double>(uz) + nz);
                    }
                    double angle_b = 360.0f / (static_cast<double>(segments_) + segments_) * (2 * k - 1) * kDegrees;
                    double b = std::sin(angle_b), c_b = std::cos(angle_b);
                    float inner[3];
                    {
                        double x = rv[0] * b + p[0];
                        auto iy = static_cast<float>(static_cast<double>(static_cast<float>(rv[1] * b)) + p[1]);
                        auto iz = static_cast<float>(static_cast<double>(static_cast<float>(rv[2] * b)) + p[2]);
                        auto ux = static_cast<float>(uv[0] * c_b), uy = static_cast<float>(uv[1] * c_b),
                             uz = static_cast<float>(uv[2] * c_b);
                        inner[0] = static_cast<float>(static_cast<double>(ux) + x);
                        inner[1] = static_cast<float>(static_cast<double>(uy) + iy);
                        inner[2] = static_cast<float>(static_cast<double>(uz) + iz);
                    }
                    vertex[0] = {p[0], p[1], p[2], colours[0]};
                    vertex[1] = {outer[0], outer[1], outer[2], colours[1]};
                    vertex[2] = {inner[0], inner[1], inner[2], colours[2]};
                    vertex[3] = {next[0], next[1], next[2], colours[1]};
                    vertex += 4;
                    std::memcpy(outer, next, sizeof(next));
                    vertices += 4;
                    triangles += 2;
                    if (vertices >= room - 4)
                        break;
                }
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                place += 3;
                if (index >= particle_count_)
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - 4)
            break;
        input_port()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        input_port()->SetIndices(quad_indices());
        input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        ++draw_calls();
        drawn_triangles() += triangles;
    }
}

// --- glowribbontrail ---

// The colour `t` of the way from `from` to `to` in 4096ths (RGBA bytes in, BGRA out), not scaled.
uint32_t PcGlowRibbonTrailParticles::fine_colour(uint32_t from, uint32_t to, double t)
{
    const auto* f = reinterpret_cast<const uint8_t*>(&from);
    const auto* g = reinterpret_cast<const uint8_t*>(&to);
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(truncate((static_cast<double>(g[k] - f[k]) * t + (f[k] << 12)) * (1.0f / 4096.0f)));
    };
    uint8_t bytes[4] = {lerp(2), lerp(1), lerp(0), lerp(3)};
    uint32_t colour = 0;
    std::memcpy(&colour, bytes, 4);
    return colour;
}

// 0x004BF720
PcGlowRibbonTrailParticles::PcGlowRibbonTrailParticles(uint32_t name, int particle_cap, uint32_t texture,
                                                       uint32_t blend_mode, int fixed_alpha, int segment_count,
                                                       float unknown, int history)
    : GlowRibbonTrailParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, history)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? new (memory)
                            SimpleMaterial(particle_cap, 10, texture, blend_mode, fixed_alpha, segment_count, history)
                      : nullptr;
}

// 0x004C0940
PcGlowRibbonTrailParticles::~PcGlowRibbonTrailParticles() { free_material(material); }

// 0x004BF7F0
void PcGlowRibbonTrailParticles::render()
{
    if (particle_count_ <= 0)
        return;
    double a = g_view_matrix.row[0].z;
    double b = g_view_matrix.row[1].z;
    double c = g_view_matrix.row[2].z;
    // The camera's across and up directions, not normalised.
    double b0 = b * 0.0f;
    float r[3] = {static_cast<float>(b0 - c), static_cast<float>(c * 0.0f - a * 0.0f), static_cast<float>(a - b0)};
    float u[3] = {static_cast<float>(r[1] * c - r[2] * b), static_cast<float>(a * r[2] - r[0] * c),
                  static_cast<float>(r[0] * b - a * r[1])};
    uint32_t per_particle = segments_ * 9 * 4 + (history_length_ - 1) * 3 * 8;
    uint32_t remaining = particle_count_ * per_particle;
    set_material(material->material);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(g_pixel_shaders[kPixelShader95c], material->material);
    IDirect3DVertexBuffer9* buffer = nullptr;
    uint32_t first_vertex = 0, room = 0;
    uint32_t total_vertices = (segments_ * 9 + (history_length_ * 3 - 3) * 2) * particle_count_;
    uint32_t total_triangles = (segments_ * 3 - 2 + history_length_ * 2) * particle_count_;
    auto* vertex = static_cast<TrailVertex*>(
        os_layer::lock_dynamic_vertices(total_vertices, sizeof(TrailVertex), &buffer, &first_vertex, &room));
    FlexibleParticle* particle = particle_list_;
    int index = 0;
    while (remaining > 0) {
        for (; index < particle_count_; ++index, ++particle) {
            double life = static_cast<double>(particle->age) / particle->lifetime;
            const float* p = position_array_ + 3 * index;
            float p0[3] = {p[0], p[1], p[2]}, p1[3];
            const float* q = position_array_ + 3 * (max_particles_ + index);
            p1[0] = q[0], p1[1] = q[1], p1[2] = q[2];
            // Across the trail's first segment, normalised (unrounded).
            double n[3];
            {
                auto dx = static_cast<float>(static_cast<double>(p1[0]) - p0[0]),
                     dy = static_cast<float>(static_cast<double>(p1[1]) - p0[1]),
                     dz = static_cast<float>(static_cast<double>(p1[2]) - p0[2]);
                n[0] = dy * c - dz * b;
                n[1] = a * dz - dx * c;
                n[2] = dx * b - a * dy;
                double length = std::sqrt(n[0] * n[0] + n[2] * n[2] + n[1] * n[1]);
                if (length > 0.0f) {
                    double inverse = 1.0f / length;
                    n[0] *= inverse;
                    n[1] *= inverse;
                    n[2] *= inverse;
                }
            }
            auto size0 =
                static_cast<float>((static_cast<double>(particle->end_size_[0]) - particle->start_size_[0]) * life +
                                   particle->start_size_[0]);
            auto size1 =
                static_cast<float>((static_cast<double>(particle->end_size_[1]) - particle->start_size_[1]) * life +
                                   particle->start_size_[1]);
            // The colours (unrounded fraction): the trail's from index 3 on.
            const uint32_t* from = as_rgba(start_colour_);
            const uint32_t* to = as_rgba(end_colour_);
            double t = life;
            if (colour_mid_ >= 0.0f) {
                if (life < colour_mid_) {
                    t = life / colour_mid_;
                    to = as_rgba(mid_colour_);
                } else {
                    t = (life - colour_mid_) / (1.0f - static_cast<double>(colour_mid_));
                    from = as_rgba(mid_colour_);
                }
            }
            uint32_t previous_colour = fine_colour(from[3], to[3], t);
            from += 4;
            to += 4;
            double w = span_;
            // The head end's edge points.
            float previous_a[3], previous_b[3];
            {
                double n0s = n[0] * size0, n1s = n[1] * size0;
                auto f0 = static_cast<float>(n0s), f2 = static_cast<float>(n[2] * size0);
                auto o0 = static_cast<float>(static_cast<double>(f0) * w), o1 = static_cast<float>(w * n1s),
                     o2 = static_cast<float>(w * static_cast<double>(f2));
                previous_a[0] = static_cast<float>(static_cast<double>(o0) + p0[0]);
                previous_a[1] = static_cast<float>(static_cast<double>(o1) + p0[1]);
                previous_a[2] = static_cast<float>(static_cast<double>(o2) + p0[2]);
                previous_b[0] = static_cast<float>(static_cast<double>(p0[0]) - o0);
                previous_b[1] = static_cast<float>(static_cast<double>(p0[1]) - o1);
                previous_b[2] = static_cast<float>(static_cast<double>(p0[2]) - o2);
            }
            for (int k = 1; k < history_length_; ++k) {
                uint32_t colour = fine_colour(*from++, *to++, t);
                if (k > 1) {
                    const float* next = position_array_ + 3 * (max_particles_ * k + index);
                    p1[0] = next[0], p1[1] = next[1], p1[2] = next[2];
                    double dx = static_cast<double>(p1[0]) - p0[0], dy = static_cast<double>(p1[1]) - p0[1],
                           dz = static_cast<double>(p1[2]) - p0[2];
                    auto m0 = static_cast<float>(dy * c - dz * b);
                    auto m1 = static_cast<float>(a * dz - dx * c);
                    auto m2 = static_cast<float>(dx * b - a * dy);
                    double length = std::sqrt(static_cast<double>(m2) * m2 + static_cast<double>(m1) * m1 +
                                              static_cast<double>(m0) * m0);
                    if (length > 0.0f) {
                        auto inverse = static_cast<float>(1.0f / static_cast<double>(static_cast<float>(length)));
                        n[0] = static_cast<double>(m0) * inverse;
                        n[1] = static_cast<double>(inverse) * m1;
                        n[2] = static_cast<double>(inverse) * m2;
                    } else {
                        n[0] = m0, n[1] = m1, n[2] = m2;
                    }
                }
                double n0s = n[0] * size0, n1s = n[1] * size0;
                auto f0 = static_cast<float>(n0s), f2 = static_cast<float>(n[2] * size0);
                auto o0 = static_cast<float>(static_cast<double>(f0) * w), o1 = static_cast<float>(w * n1s),
                     o2 = static_cast<float>(w * static_cast<double>(f2));
                float next_c[3] = {static_cast<float>(static_cast<double>(o0) + p1[0]),
                                   static_cast<float>(static_cast<double>(o1) + p1[1]),
                                   static_cast<float>(w * static_cast<double>(f2) + p1[2])};
                float next_d[3] = {static_cast<float>(static_cast<double>(p1[0]) - o0),
                                   static_cast<float>(static_cast<double>(p1[1]) - o1),
                                   static_cast<float>(static_cast<double>(p1[2]) - o2)};
                vertex[0] = {previous_a[0], previous_a[1], previous_a[2], previous_colour};
                vertex[1] = {previous_b[0], previous_b[1], previous_b[2], previous_colour};
                vertex[2] = {next_c[0], next_c[1], next_c[2], colour};
                vertex[3] = {previous_b[0], previous_b[1], previous_b[2], previous_colour};
                vertex[4] = {next_d[0], next_d[1], next_d[2], colour};
                vertex[5] = {next_c[0], next_c[1], next_c[2], colour};
                vertex += 6;
                std::memcpy(previous_a, next_c, sizeof(next_c));
                std::memcpy(previous_b, next_d, sizeof(next_d));
                std::memcpy(p0, p1, sizeof(p1));
                previous_colour = colour;
            }

            // The glow about the head: its colours from the colour fraction gone through the selection again.
            std::memcpy(p0, p, sizeof(p0));
            float rr[3] = {static_cast<float>(static_cast<double>(r[0]) * size0),
                           static_cast<float>(static_cast<double>(r[1]) * size0),
                           static_cast<float>(static_cast<double>(r[2]) * size0)};
            float uu[3] = {static_cast<float>(static_cast<double>(u[0]) * size1),
                           static_cast<float>(static_cast<double>(u[1]) * size1),
                           static_cast<float>(static_cast<double>(u[2]) * size1)};
            const uint32_t* glow_from = as_rgba(start_colour_);
            const uint32_t* glow_to = as_rgba(end_colour_);
            if (colour_mid_ >= 0.0f) {
                if (t < colour_mid_) {
                    t = t / colour_mid_;
                    glow_to = as_rgba(mid_colour_);
                } else {
                    t = (t - colour_mid_) / (1.0f - static_cast<double>(colour_mid_));
                    glow_from = as_rgba(mid_colour_);
                }
            }
            uint32_t glow[3];
            for (int k = 0; k < 3; ++k)
                glow[k] = fine_colour(glow_from[k], glow_to[k], t);
            float outer[3], inner[3];
            {
                double r0 = static_cast<double>(rr[0]) * 0.0f, r1 = static_cast<double>(rr[1]) * 0.0f,
                       r2 = static_cast<double>(rr[2]) * 0.0f;
                double x =
                    static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(r0)) * w)) + p0[0];
                outer[1] = static_cast<float>(static_cast<double>(static_cast<float>(w * r1)) + p0[1]);
                outer[2] = static_cast<float>(w * r2 + p0[2]);
                outer[0] = static_cast<float>(x + static_cast<float>(static_cast<double>(uu[0]) * w));
                outer[1] = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(uu[1]) * w)) +
                                              outer[1]);
                outer[2] = static_cast<float>(static_cast<double>(static_cast<float>(w * uu[2])) + outer[2]);
                inner[0] = static_cast<float>(static_cast<double>(p0[0]) + static_cast<float>(r0));
                inner[1] = static_cast<float>(r1 + p0[1]);
                double z = static_cast<double>(static_cast<float>(r2)) + p0[2];
                inner[0] = static_cast<float>(static_cast<double>(uu[0]) + inner[0]);
                inner[1] = static_cast<float>(static_cast<double>(uu[1]) + inner[1]);
                inner[2] = static_cast<float>(static_cast<double>(uu[2]) + z);
            }
            for (int i = 1; i <= segments_; ++i) {
                double angle = static_cast<double>(i) * glow_step_ * 0.0174532924f;
                auto s = static_cast<float>(std::sin(angle)), cs = static_cast<float>(std::cos(angle));
                double r0s = static_cast<double>(rr[0]) * s, r1s = static_cast<double>(s) * rr[1],
                       r2s = static_cast<double>(s) * rr[2];
                float next_outer[3], next_inner[3];
                double x =
                    static_cast<double>(p0[0]) + static_cast<float>(static_cast<double>(static_cast<float>(r0s)) * w);
                next_outer[1] = static_cast<float>(static_cast<double>(static_cast<float>(r1s * w)) + p0[1]);
                next_outer[2] = static_cast<float>(w * r2s + p0[2]);
                double u0c = static_cast<double>(uu[0]) * cs, u1c = static_cast<double>(cs) * uu[1];
                auto u2c = static_cast<float>(static_cast<double>(cs) * uu[2]);
                next_outer[0] = static_cast<float>(x + static_cast<float>(u0c * w));
                next_outer[1] = static_cast<float>(static_cast<double>(static_cast<float>(u1c * w)) + next_outer[1]);
                next_outer[2] = static_cast<float>(
                    static_cast<double>(static_cast<float>(static_cast<double>(u2c) * w)) + next_outer[2]);
                auto ix = static_cast<float>(static_cast<double>(p0[0]) + static_cast<float>(r0s));
                auto iy = static_cast<float>(r1s + p0[1]);
                double iz = static_cast<double>(static_cast<float>(r2s)) + p0[2];
                next_inner[0] = static_cast<float>(static_cast<double>(static_cast<float>(u0c)) + ix);
                next_inner[1] = static_cast<float>(static_cast<double>(static_cast<float>(u1c)) + iy);
                next_inner[2] = static_cast<float>(static_cast<double>(u2c) + iz);
                vertex[0] = {p0[0], p0[1], p0[2], glow[0]};
                vertex[1] = {outer[0], outer[1], outer[2], glow[1]};
                vertex[2] = {next_outer[0], next_outer[1], next_outer[2], glow[1]};
                vertex[3] = {outer[0], outer[1], outer[2], glow[1]};
                vertex[4] = {next_outer[0], next_outer[1], next_outer[2], glow[1]};
                vertex[5] = {inner[0], inner[1], inner[2], glow[2]};
                vertex[6] = {next_outer[0], next_outer[1], next_outer[2], glow[1]};
                vertex[7] = {inner[0], inner[1], inner[2], glow[2]};
                vertex[8] = {next_inner[0], next_inner[1], next_inner[2], glow[2]};
                vertex += 9;
                std::memcpy(outer, next_outer, sizeof(outer));
                std::memcpy(inner, next_inner, sizeof(inner));
            }
            remaining -= per_particle;
        }
    }
    input_port()->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    os_layer::unlock_dynamic_vertices(total_vertices, sizeof(TrailVertex));
    input_port()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
    input_port()->DrawPrimitive(D3DPT_TRIANGLELIST, first_vertex, total_triangles);
    input_port()->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    ++draw_calls();
    drawn_triangles() += total_triangles;
}

} // namespace render
