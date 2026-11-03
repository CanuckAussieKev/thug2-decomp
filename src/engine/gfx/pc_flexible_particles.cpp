// The PC flexible particle systems: see pc_flexible_particles.h.

#include "engine/gfx/pc_flexible_particles.h"

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/particle_system.h"
#include "engine/gfx/particle_types.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "platform/platform_d3d.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/engine.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "gpu/vertex_view.h"
#include "engine/core/packed_colour.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"
#include "engine/core/checksums.h"
#include "engine/maths/vec4.h"
#include "engine/maths/trig.h"

namespace render {

constexpr float kByteMax = 255.0f;
constexpr int kDefaultTypeIndex = 12; // the emitter type used for any other type: newflat
// The second argument of the particle materials' construction (it differs for each particle class).
constexpr int kNewFlatMaterialKind = 0xB;
constexpr int kLineMaterialKind = 7;
constexpr int kRibbonTrailMaterialKind = 9;
constexpr int kSmoothMaterialKind = 8;
constexpr int kSmoothRibbonMaterialKind = 6;
constexpr int kSmoothStarMaterialKind = 5;
constexpr int kGlowRibbonTrailMaterialKind = 10;
constexpr uint32_t kMaxBatchBytes = 0x7FF; // the most one batch of vertices takes
constexpr std::size_t kTrailVertexCount = 0x200;
constexpr float kFullCircleDegrees = 360.0f;
// A ring particle's vertices: the sixth to ninth of each segment.
constexpr int kSixthVertex = 5;
constexpr int kSeventhVertex = 6;
constexpr int kEighthVertex = 7;
constexpr int kNinthVertex = 8;
constexpr int kGlowVerticesPerSegment = 9;
constexpr int kGlowRibbonVerticesPerSegment = 6;
constexpr int kGlowRibbonEndVertices = 9;
constexpr int kFlagRandomUvQuadrants = 0x4000; // UseRandomUVQuadrants
constexpr float kUvHalf = 0.5f;
constexpr int kSmoothParticleVertices = 12;
constexpr int kSmoothRibbonVertices = 8;
constexpr uint32_t kSmoothRibbonStep = 0x20;
constexpr int kSmoothStarVertices = 8;
constexpr int kFixedPointShift = 12;
constexpr float kFixedPointScale = 4096.0f;

IDirect3DIndexBuffer9*& quad_index_buffer()
{
    static IDirect3DIndexBuffer9* quad_indices{};
    return quad_indices;
}

namespace {

void* default_pixel_shader() { return core::at(pixel_shaders(), kPixelShader964); }
// The frame's draw counts: draws, triangles.

// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

void set_material(MaterialRef* material) { material->hand_in(); } // (its set-up)

// The camera's across and up directions from the view matrix's third column (a, b, c): r = (a, b, c) x (0, 1, 0)
// and u = r x (a, b, c), then normalised (maths::Vec4::make_unit).
void camera_axes(maths::Vec4& r, maths::Vec4& u)
{
    const double a = core::at(renderer().view_matrix().row, 0).z;
    const double b = core::at(renderer().view_matrix().row, 1).z;
    const double c = core::at(renderer().view_matrix().row, 2).z;
    const double b0 = b * 0.0f;
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
constexpr std::size_t kQuadVertexBytes = 0x18;

static_assert(sizeof(QuadVertex) == kQuadVertexBytes, "layout must match the original");

uint8_t clamped_byte(double value)
{
    if (value < 0.0f)
        value = 0.0f;
    else if (value > kByteMax)
        value = kByteMax;
    return static_cast<uint8_t>(truncate(value));
}

// 0x004BD470: the colour `t` of the way from `from` to `to` (RGBA bytes, each channel wrapping as a byte), its red,
// green and blue then scaled by the colour scale x the brightness (clamped to 0-255) - into *out as a d_3d colour
// (BGRA bytes). Returns out.
uint32_t* lerp_colour(const EmitterInstance& system, uint32_t* out, const colour::Bytes& from, const colour::Bytes& to,
                      float t)
{
    const std::array<float, 4>& colour_scale = system.colour_scale();
    const float brightness = system.brightness();
    const double red = static_cast<double>(core::at(colour_scale, 0)) * brightness;
    const double green = static_cast<double>(core::at(colour_scale, 1)) * brightness;
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(core::at(from, k) +
                                    truncate(static_cast<double>(core::at(to, k) - core::at(from, k)) * t));
    };
    const uint8_t b = lerp(2);
    const uint8_t g = lerp(1);
    const uint8_t r = lerp(0);
    const uint8_t a = lerp(3);
    const double blue = static_cast<double>(core::at(colour_scale, 2)) * brightness;
    std::array<uint8_t, 4> bytes = {{clamped_byte(blue * b), clamped_byte(green * g), clamped_byte(red * r), a}};
    std::memcpy(out, bytes.data(), 4);
    return out;
}

// The rate's particles for this frame (1/60 s) emitted through the engine's 0x00496FE0, the fraction kept.
void emit_for_frame(EmitterInstance& system, float& accumulator)
{
    if (!(system.emit_rate() > 0.0f))
        return;
    const double owed = static_cast<double>(system.emit_rate()) * (1.0f / 60.0f) + accumulator; // (0x006463B8)
    accumulator = static_cast<float>(owed);
    if (owed < 1.0f || owed != owed)
        return;
    system.get_emit(truncate(owed));
    accumulator = static_cast<float>(accumulator - static_cast<double>(truncate(accumulator)));
}

// Which colours a particle `life` of the way through its life is between, and how far.
struct ColourSpan {
    core::Cursor<const uint32_t> from;
    core::Cursor<const uint32_t> to;
    float t;
};
ColourSpan colour_span(const EmitterInstance& system, float life)
{
    const float colour_mid = system.colour_mid();
    ColourSpan span{.from = core::Cursor<const uint32_t>(system.start_colour()), .to = core::Cursor<const uint32_t>(system.end_colour()), .t = life};
    if (colour_mid >= 0.0f) {
        if (life < colour_mid) {
            span.t = static_cast<float>(static_cast<double>(life) / colour_mid);
            span.to = core::Cursor<const uint32_t>(system.mid_colour());
        } else {
            span.t =
                static_cast<float>((static_cast<double>(life) - colour_mid) / (1.0f - static_cast<double>(colour_mid)));
            span.from = core::Cursor<const uint32_t>(system.mid_colour());
        }
    }
    return span;
}

// The dynamic vertex buffer locked for `count` vertices of `stride` bytes: written by vertex index (the vertices that
// fit are `room`).
gpu::VertexWriter locked_writer(uint32_t count, uint32_t stride, IDirect3DVertexBuffer9** buffer, uint32_t* first_vertex,
                                uint32_t* room)
{
    void* where = os_layer::lock_dynamic_vertices(count, stride, buffer, {.first_vertex_ = first_vertex, .room = room});
    return {static_cast<uint8_t*>(where), stride, *room};
}

// lerp_colour of two colours (copies).
uint32_t get_colour(const EmitterInstance& system, uint32_t from, uint32_t to, float t)
{
    uint32_t colour = 0;
    lerp_colour(system, &colour, colour::bytes_of(from), colour::bytes_of(to), t);
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
    const maths::Vec4& align_to = system.align_to();
    const double ax = align_to.x;
    const double ay = align_to.y;
    const double az = align_to.z;
    double p = 0.0f;
    double q = 1.0f; // (up: y - or x when alignTo is y itself)
    const double dot = az * 0.0f + ax * 0.0f + ay;
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

template <typename T>
gsl::owner<EmitterInstance*> make_plain(const ConstructArgs& a)
{
    return new T(a.name, a.particle_cap_, a.texture, a.blend_mode_, a.fixed_alpha_, a.segments, a.unknown);
}
template <typename T>
gsl::owner<EmitterInstance*> make_with_history(const ConstructArgs& a)
{
    return new T(a.name, a.particle_cap_, a.texture, a.blend_mode_, a.fixed_alpha_, a.segments, a.unknown, a.history);
}
template <typename T>
gsl::owner<EmitterInstance*> make_five(const ConstructArgs& a)
{
    return new T(a.name, a.particle_cap_, a.texture, a.blend_mode_, a.fixed_alpha_);
}
gsl::owner<EmitterInstance*> make_model(const ConstructArgs& a)
{
    return new ModelParticles(a);
}

} // namespace

// 0x004BCEF0
gsl::owner<EmitterInstance*> create_flexible_particles(checksums::Name system_name, checksums::Name kind,
                                                       int particle_cap, int /*unused*/, uint32_t texture,
                                                       uint32_t blend_mode, int fixed_alpha, int segments,
                                                       float unknown, int history, const char* model)
{
    const uint32_t type = kind.value();
    const uint32_t name = system_name.value();
    struct EmitterType {
        uint32_t type;
        EmitterInstance* (*construct)(const ConstructArgs&); // a new one of the class (null if no memory)
    };
    static constexpr auto kTypes = std::to_array<EmitterType>({
        {.type = 0x15834EEA, .construct = &make_plain<PcGlowParticles>},                   // glow
        {.type = 0x0EE6FC5B, .construct = &make_with_history<PcRibbonParticles>},          // ribbon
        {.type = 0x097CB7A9, .construct = &make_plain<PcSmoothStarParticles>},             // smoothstar
        {.type = 0x286A8D26, .construct = &make_model},                                    // (the engine's model class)
        {.type = 0x3624A5EB, .construct = &make_plain<PcStarParticles>},                   // star
        {.type = 0x2EEB4B09, .construct = &make_plain<PcLineParticles>},                   // line
        {.type = 0x3D56A7A6, .construct = &make_with_history<PcFlatRibbonParticles>},      // (a flat ribbon)
        {.type = 0x8ADDAC1F, .construct = &make_plain<PcSmoothParticles>},                 // smooth
        {.type = 0x7EC7252D, .construct = &make_with_history<PcGlowRibbonTrailParticles>}, // glowribbontrail
        {.type = 0x3F109FCC, .construct = &make_with_history<PcSmoothRibbonParticles>},    // smoothribbon
        {.type = 0xC4D5A4CB, .construct = &make_with_history<PcRibbonTrailParticles>},     // ribbontrail
        {.type = 0xF4D8D486, .construct = &make_five<PcShadedParticles>},                  // shaded
        {.type = 0xDEDFC057, .construct = &make_plain<PcNewFlatParticles>},                // newflat
    });
    const EmitterType* chosen = &core::at(kTypes, kDefaultTypeIndex); // (any other type: newflat)
    for (const EmitterType& t : kTypes)
        if (t.type == type)
            chosen = &t;
    const ConstructArgs args{.name = name, .particle_cap_ = particle_cap, .texture = texture, .blend_mode_ = blend_mode, .fixed_alpha_ = fixed_alpha, .segments = segments, .unknown = unknown, .history = history, .model = model};
    return chosen->construct(args); // (zeroed memory on the current heap, with the assert flag: the system's new)
}

// --- newflat ---

// 0x004BD3A0
PcNewFlatParticles::PcNewFlatParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                       int fixed_alpha, int segments, float unknown)
    : NewFlatParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segments, unknown, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material =
        memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, 1, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha, 0, 0)
               : nullptr;
}

// 0x004BD610
PcNewFlatParticles::~PcNewFlatParticles() { free_material(material); }

// 0x004BD680
void PcNewFlatParticles::render()
{
    if (particle_count() > 0) {
        maths::Vec4 r;
        maths::Vec4 u;
        camera_axes(r, u);
        set_material(material->material());
        set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        set_pixel_shader(default_pixel_shader(), nullptr);
        core::Cursor<const float> place(position_array());
        core::Cursor<const FlexibleParticle> particle(particle_list());
        int remaining = particle_count();
        int index = 0;
        while (remaining != 0) {
            IDirect3DVertexBuffer9* buffer = nullptr;
            uint32_t first_vertex = 0;
            uint32_t room = 0;
            auto vertex = os_layer::lock_vertices<QuadVertex>(
                particle_count() * 4, &buffer, &first_vertex, &room);
            uint32_t quads = 0;
            if (index < particle_count()) {
                for (;;) {
                    auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                    const double across =
                        (static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                        core::at(particle->start_size_, 0);
                    const double up = (static_cast<double>(core::at(particle->end_size_, 1)) - core::at(particle->start_size_, 1)) * life +
                                      core::at(particle->start_size_, 1);
                    float cx = 0.0f;
                    float cy = 0.0f;
                    float cz = 0.0f;
                    if (emitter_independent()) {
                        cx = place[0], cy = place[1], cz = place[2];
                    } else {
                        cx = static_cast<float>(static_cast<double>(at_point().x) + place[0]);
                        cy = static_cast<float>(static_cast<double>(place[1]) + at_point().y);
                        cz = static_cast<float>(static_cast<double>(place[2]) + at_point().z);
                    }
                    auto rx = static_cast<float>(r.x * across);
                    auto ry = static_cast<float>(r.y * across);
                    auto rz = static_cast<float>(r.z * across);
                    auto ux = static_cast<float>(u.x * up);
                    auto uy = static_cast<float>(u.y * up);
                    auto uz = static_cast<float>(u.z * up);

                    const ColourSpan span = colour_span(*this, life);
                    const uint32_t colour = get_colour(*this, *span.from, *span.to, span.t);

                    const double dx = static_cast<double>(cx) - rx;
                    const double dy = static_cast<double>(cy) - ry;
                    const double dz = static_cast<double>(cz) - rz;
                    auto fdx = static_cast<float>(dx);
                    auto fdy = static_cast<float>(dy);
                    auto fdz = static_cast<float>(dz);
                    const double sz = static_cast<double>(rz) + cz;
                    auto fsx = static_cast<float>(static_cast<double>(rx) + cx);
                    auto fsy = static_cast<float>(static_cast<double>(ry) + cy);
                    auto fsz = static_cast<float>(sz);
                    vertex[0] = {.x = static_cast<float>(static_cast<double>(fdx) + ux),
                                 .y = static_cast<float>(static_cast<double>(fdy) + uy),
                                 .z = static_cast<float>(dz + uz),
                                 .colour = colour,
                                 .u = 0.0f,
                                 .v = 0.0f};
                    vertex[1] = {.x = static_cast<float>(static_cast<double>(fsx) + ux),
                                 .y = static_cast<float>(static_cast<double>(fsy) + uy),
                                 .z = static_cast<float>(sz + uz),
                                 .colour = colour,
                                 .u = 1.0f,
                                 .v = 0.0f};
                    vertex[2] = {.x = static_cast<float>(static_cast<double>(fsx) - ux),
                                 .y = static_cast<float>(static_cast<double>(fsy) - uy),
                                 .z = static_cast<float>(static_cast<double>(fsz) - uz),
                                 .colour = colour,
                                 .u = 1.0f,
                                 .v = 1.0f};
                    vertex[3] = {.x = static_cast<float>(static_cast<double>(fdx) - ux),
                                 .y = static_cast<float>(static_cast<double>(fdy) - uy),
                                 .z = static_cast<float>(static_cast<double>(fdz) - uz),
                                 .colour = colour,
                                 .u = 0.0f,
                                 .v = 1.0f};
                    vertex += 4;
                    ++quads;
                    if (quads >= room - 4)
                        break;
                    ++particle;
                    ++index;
                    place += 3;
                    if (index >= particle_count())
                        break;
                }
            }
            remaining -= static_cast<int>(quads);
            os_layer::unlock_dynamic_vertices(quads * 4, sizeof(QuadVertex));
            if (quads >= room - 4)
                break; // (full: the original stops here, undrawn)
            renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(QuadVertex));
            renderer().d3d_device()->SetIndices(quad_index_buffer());
            renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, quads * 4, 0,
                                               quads * 2);
            renderer().count_draw(quads * 2);
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
constexpr std::size_t kTrailVertexBytes = 0x10;

static_assert(sizeof(TrailVertex) == kTrailVertexBytes, "layout must match the original");

// A particle's position (`index` in the positions), the emitter's added unless emitterIndependent.
void place_of(const EmitterInstance* system, int index, std::span<float, 3> out)
{
    const core::Cursor<const float> p(system->position_array(), 3 * index);
    if (system->emitter_independent()) {
        core::at(out, 0) = p[0], core::at(out, 1) = p[1], core::at(out, 2) = p[2];
    } else {
        core::at(out, 0) = static_cast<float>(static_cast<double>(system->at_point().x) + p[0]);
        core::at(out, 1) = static_cast<float>(static_cast<double>(p[1]) + system->at_point().y);
        core::at(out, 2) = static_cast<float>(static_cast<double>(p[2]) + system->at_point().z);
    }
}

// Across the path from p0 to p1 and the camera's direction (a, b, c): (p1 - p0) x (a, b, c), normalised when it has
// a length. The ribbon's first segment rounds every component first; the later ones keep z unrounded.
void get_across(std::span<const float, 3> p0, std::span<const float, 3> p1, double a, double b, double c, bool first, std::span<float, 3> n)
{
    const double dx = static_cast<double>(core::at(p1, 0)) - core::at(p0, 0);
    const double dy = static_cast<double>(core::at(p1, 1)) - core::at(p0, 1);
    const double dz = static_cast<double>(core::at(p1, 2)) - core::at(p0, 2);
    core::at(n, 0) = static_cast<float>(c * dy - b * dz);
    core::at(n, 1) = static_cast<float>(a * dz - c * dx);
    double z = b * dx - a * dy;
    core::at(n, 2) = static_cast<float>(z);
    if (first)
        z = core::at(n, 2);
    const double length = std::sqrt(static_cast<double>(core::at(n, 0)) * core::at(n, 0) + z * z + static_cast<double>(core::at(n, 1)) * core::at(n, 1));
    if (length > 0.0f) {
        const double inverse = 1.0f / length;
        core::at(n, 0) = static_cast<float>(core::at(n, 0) * inverse);
        core::at(n, 1) = static_cast<float>(core::at(n, 1) * inverse);
        core::at(n, 2) = static_cast<float>(inverse * z);
    }
}

} // namespace

// 0x004BDC80
PcFlatRibbonParticles::PcFlatRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                             int fixed_alpha, int segment_count, float ribbon_width, int history)
    : FlatRibbonParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, ribbon_width, history),
      width(ribbon_width)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, kNewFlatMaterialKind, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode},
                                                             fixed_alpha, segment_count, history)
                      : nullptr;
}

// 0x004BDD60
PcFlatRibbonParticles::~PcFlatRibbonParticles() { free_material(material); }

// 0x004BDDD0
void PcFlatRibbonParticles::render()
{
    if (particle_count() <= 0)
        return;
    const double a = core::at(renderer().view_matrix().row, 0).z;
    const double b = core::at(renderer().view_matrix().row, 1).z;
    const double c = core::at(renderer().view_matrix().row, 2).z;
    maths::Vec4 r;
    maths::Vec4 u;
    camera_axes(r, u);
    const uint32_t particle_bytes = (segments() - 1) * 16;
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader95c), material->material());

    // The ribbons.
    core::Cursor<const FlexibleParticle> particle(particle_list());
    int remaining = (segments() - 1) * particle_count() * 4;
    int index = 0;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        auto vertex = os_layer::lock_vertices<TrailVertex>(remaining, &buffer, &first_vertex, &room);
        if (index < particle_count()) {
            for (uint32_t bytes = particle_bytes; bytes <= kMaxBatchBytes; bytes += particle_bytes) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                std::array<float, 3> p0{};
                std::array<float, 3> p1{};
                std::array<float, 3> n{};
                place_of(this, index, p0);
                place_of(this, max_particles() + index, p1);
                get_across(p0, p1, a, b, c, true, n);
                auto w =
                    static_cast<float>((static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                       core::at(particle->start_size_, 0));
                const ColourSpan span = colour_span(*this, life);
                core::Cursor<const uint32_t> from(span.from);
                core::Cursor<const uint32_t> to(span.to);
                uint32_t previous_colour = get_colour(*this, *from++, *to++, span.t);

                // The head end's edge points.
                double n0w = static_cast<double>(core::at(n, 0)) * w;
                double n1w = static_cast<double>(w) * core::at(n, 1);
                double n2w = static_cast<double>(w) * core::at(n, 2);
                auto o0 = static_cast<float>(static_cast<double>(static_cast<float>(n0w)) * width);
                auto o1 = static_cast<float>(width * n1w);
                std::array<float, 3> previous_a = {{static_cast<float>(static_cast<double>(o0) + core::at(p0, 0)),
                                       static_cast<float>(static_cast<double>(o1) + core::at(p0, 1)),
                                       static_cast<float>(width * n2w + core::at(p0, 2))}};
                std::array<float, 3> previous_b = {{
                    static_cast<float>(static_cast<double>(core::at(p0, 0)) - o0),
                    static_cast<float>(static_cast<double>(core::at(p0, 1)) - o1),
                    static_cast<float>(core::at(p0, 2) - width * static_cast<double>(static_cast<float>(n2w)))}};

                for (int k = 1; k < segments(); ++k) {
                    const uint32_t colour = get_colour(*this, *from++, *to++, span.t);
                    if (k > 1) {
                        place_of(this, max_particles() * k + index, p1);
                        get_across(p0, p1, a, b, c, false, n);
                    }
                    n0w = static_cast<double>(core::at(n, 0)) * w;
                    n1w = static_cast<double>(w) * core::at(n, 1);
                    n2w = static_cast<double>(w) * core::at(n, 2);
                    o0 = static_cast<float>(static_cast<double>(static_cast<float>(n0w)) * width);
                    o1 = static_cast<float>(width * n1w);
                    std::array<float, 3> next_b = {{static_cast<float>(static_cast<double>(core::at(p1, 0)) - o0),
                                       static_cast<float>(static_cast<double>(core::at(p1, 1)) - o1),
                                       static_cast<float>(core::at(p1, 2) - width * n2w)}};
                    auto o2 = static_cast<float>(width * static_cast<double>(static_cast<float>(n2w)));
                    std::array<float, 3> next_a = {{static_cast<float>(static_cast<double>(o0) + core::at(p1, 0)),
                                       static_cast<float>(static_cast<double>(o1) + core::at(p1, 1)),
                                       static_cast<float>(static_cast<double>(o2) + core::at(p1, 2))}};
                    vertex[0] = {.x = core::at(previous_a, 0), .y = core::at(previous_a, 1), .z = core::at(previous_a, 2), .colour = previous_colour};
                    vertex[1] = {.x = core::at(previous_b, 0), .y = core::at(previous_b, 1), .z = core::at(previous_b, 2), .colour = previous_colour};
                    vertex[2] = {.x = core::at(next_a, 0), .y = core::at(next_a, 1), .z = core::at(next_a, 2), .colour = colour};
                    vertex[3] = {.x = core::at(next_b, 0), .y = core::at(next_b, 1), .z = core::at(next_b, 2), .colour = colour};
                    vertex += 4;
                    std::memcpy(previous_a.data(), next_a.data(), sizeof(next_a));
                    std::memcpy(previous_b.data(), next_b.data(), sizeof(next_b));
                    std::memcpy(p0.data(), p1.data(), sizeof(p1));
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
                if (index >= particle_count())
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - 4)
            break;
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        renderer().d3d_device()->SetIndices(quad_index_buffer());
        renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        renderer().count_draw(triangles);
    }

    // The heads.
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    set_pixel_shader(default_pixel_shader(), nullptr);
    particle = core::Cursor<const FlexibleParticle>(particle_list());
    remaining = particle_count() * 4;
    index = 0;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        auto vertex = os_layer::lock_vertices<QuadVertex>(remaining, &buffer, &first_vertex, &room);
        if (index < particle_count()) {
            for (uint32_t bytes = sizeof(QuadVertex); bytes <= kMaxBatchBytes; bytes += sizeof(QuadVertex)) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                const double size = ((static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                     core::at(particle->start_size_, 0)) *
                                    width * 5.0f; // (0x006469AC)
                auto r0 = static_cast<float>(r.x * size);
                auto r1 = static_cast<float>(r.y * size);
                auto r2 = static_cast<float>(r.z * size);
                auto u0 = static_cast<float>(u.x * size);
                auto u1 = static_cast<float>(u.y * size);
                const double u2 = u.z * size;
                const core::Cursor<const float> p(position_array(), 3 * index);
                double px = 0.0;
                double py = 0.0;
                float pz = 0.0f;
                if (emitter_independent()) {
                    px = p[0], py = p[1], pz = p[2];
                } else {
                    px = static_cast<double>(at_point().x) + p[0];
                    py = static_cast<double>(p[1]) + at_point().y;
                    pz = static_cast<float>(static_cast<double>(p[2]) + at_point().z);
                }
                const double e0 = px + r0;
                auto e1 = static_cast<float>(py + r1);
                auto e2 = static_cast<float>(static_cast<double>(pz) + r2);
                auto g0 = static_cast<float>(px - r0);
                const double g1 = py - r1;
                const double g2 = static_cast<double>(pz) - r2;
                auto g1f = static_cast<float>(g1);
                const ColourSpan span = colour_span(*this, life);
                const uint32_t colour = get_colour(*this, *span.from, *span.to, span.t);
                vertex[0] = {.x = static_cast<float>(e0 - u0),
                             .y = static_cast<float>(static_cast<double>(e1) - u1),
                             .z = static_cast<float>(e2 - u2),
                             .colour = colour,
                             .u = 0.0f,
                             .v = 0.0f};
                vertex[1] = {.x = static_cast<float>(static_cast<double>(g0) - u0),
                             .y = static_cast<float>(static_cast<double>(g1f) - u1),
                             .z = static_cast<float>(g2 - u2),
                             .colour = colour,
                             .u = 1.0f,
                             .v = 0.0f};
                vertex[2] = {.x = static_cast<float>(static_cast<double>(g0) + u0),
                             .y = static_cast<float>(static_cast<double>(g1f) + u1),
                             .z = static_cast<float>(g2 + u2),
                             .colour = colour,
                             .u = 1.0f,
                             .v = 1.0f};
                vertex[3] = {.x = static_cast<float>(static_cast<double>(static_cast<float>(e0)) + u0),
                             .y = static_cast<float>(static_cast<double>(e1) + u1),
                             .z = static_cast<float>(e2 + u2),
                             .colour = colour,
                             .u = 0.0f,
                             .v = 1.0f};
                vertex += 4;
                vertices += 4;
                triangles += 2;
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                if (index >= particle_count())
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(QuadVertex));
        if (vertices >= room - 4)
            break;
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(QuadVertex));
        renderer().d3d_device()->SetIndices(quad_index_buffer());
        renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        renderer().count_draw(triangles);
    }
}

// --- glow ---

// 0x004BED10
PcGlowParticles::PcGlowParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                 int fixed_alpha, int segment_count, float unknown)
    : GlowParticles({.name = name, .particle_cap_ = particle_cap, .texture = texture, .blend_mode_ = blend_mode,
                     .fixed_alpha_ = fixed_alpha, .segments = segment_count, .unknown = unknown, .history = 0})
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, 4, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha,
                                                             segment_count, 0)
                      : nullptr;
}

// 0x004BEDE0
PcGlowParticles::~PcGlowParticles() { free_material(material); }

// 0x004BEE50
void PcGlowParticles::render()
{
    if (particle_count() <= 0)
        return;
    maths::Vec4 r;
    maths::Vec4 u;
    camera_axes(r, u);
    const uint32_t particle_words = segments() * 9 * 4; // (nine vertices of four words a segment)
    uint32_t remaining = particle_count() * particle_words;
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader95c), material->material());
    core::Cursor<const FlexibleParticle> particle(particle_list());
    core::Cursor<const float> place(position_array());
    int index = 0;
    std::array<TrailVertex, kTrailVertexCount> buffer{};
    while (remaining > 0) {
        core::Cursor<TrailVertex> vertex(buffer);
        uint32_t words = 0;
        const int total = particle_count();
        for (uint32_t budget = particle_words; index < total && budget <= kMaxBatchBytes; budget += particle_words) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            const double across = (static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                  core::at(particle->start_size_, 0);
            const double up = (static_cast<double>(core::at(particle->end_size_, 1)) - core::at(particle->start_size_, 1)) * life +
                              core::at(particle->start_size_, 1);
            std::array<float, 3> p{};
            if (emitter_independent()) {
                core::at(p, 0) = place[0], core::at(p, 1) = place[1], core::at(p, 2) = place[2];
            } else {
                core::at(p, 0) = static_cast<float>(static_cast<double>(at_point().x) + place[0]);
                core::at(p, 1) = static_cast<float>(static_cast<double>(place[1]) + at_point().y);
                core::at(p, 2) = static_cast<float>(static_cast<double>(place[2]) + at_point().z);
            }
            auto rx = static_cast<float>(r.x * across);
            auto ry = static_cast<float>(r.y * across);
            auto rz = static_cast<float>(r.z * across);
            auto ux = static_cast<float>(u.x * up);
            auto uy = static_cast<float>(u.y * up);
            auto uz = static_cast<float>(u.z * up);
            // The centre's, the edge's and the outer ring's colours (each its own of the three colour arrays').
            const ColourSpan span = colour_span(*this, life);
            std::array<uint32_t, 3> colours{};
            for (int k = 0; k < 3; ++k)
                core::at(colours, k) = get_colour(*this, span.from[k], span.to[k], span.t);
            const double w = glow_width();

            // The rings' points at angle 0: outer (r sin, u cos, times the width), inner.
            const double s = std::sin(0.0);
            const double c = std::cos(0.0);
            std::array<float, 3> outer{};
            std::array<float, 3> inner{};
            {
                const double ry_s = static_cast<float>(ry * s);
                const double rz_s = static_cast<float>(rz * s);
                core::at(outer, 0) = static_cast<float>(rx * s * w + core::at(p, 0));
                core::at(outer, 1) = static_cast<float>(ry_s * w + core::at(p, 1));
                core::at(outer, 2) = static_cast<float>(static_cast<double>(static_cast<float>(rz_s * w)) + core::at(p, 2));
                const double uy_c = static_cast<float>(uy * c);
                const double uz_c = static_cast<float>(uz * c);
                core::at(outer, 0) = static_cast<float>(ux * c * w + core::at(outer, 0));
                core::at(outer, 1) = static_cast<float>(uy_c * w + core::at(outer, 1));
                core::at(outer, 2) = static_cast<float>(static_cast<double>(static_cast<float>(uz_c * w)) + core::at(outer, 2));
                core::at(inner, 0) = static_cast<float>(rx * s + core::at(p, 0));
                core::at(inner, 1) = static_cast<float>(static_cast<double>(static_cast<float>(ry * s)) + core::at(p, 1));
                core::at(inner, 2) = static_cast<float>(static_cast<double>(static_cast<float>(rz * s)) + core::at(p, 2));
                core::at(inner, 0) = static_cast<float>(ux * c + core::at(inner, 0));
                core::at(inner, 1) = static_cast<float>(static_cast<double>(static_cast<float>(uy * c)) + core::at(inner, 1));
                core::at(inner, 2) = static_cast<float>(static_cast<double>(static_cast<float>(uz * c)) + core::at(inner, 2));
            }
            auto step = static_cast<float>(maths::kTwoPi / static_cast<double>(segments())); // (0x0064545C)
            for (int i = 1; i <= segments(); ++i) {
                const double angle = static_cast<double>(i) * step;
                const double si = std::sin(angle);
                const double ci = std::cos(angle);
                std::array<float, 3> next_outer{};
                std::array<float, 3> next_inner{};
                const double rsw = rx * si * w;
                const double r1 = static_cast<double>(static_cast<float>(ry * si)) * w;
                const double r2 =
                    static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(rz * si)) * w)) +
                    core::at(p, 2);
                auto cosine = static_cast<float>(ci);
                const double usw = ux * ci * w;
                const double u1 = static_cast<double>(static_cast<float>(uy * ci)) * w;
                auto u2 = static_cast<float>(static_cast<double>(static_cast<float>(uz * ci)) * w);
                core::at(next_outer, 0) = static_cast<float>(usw + static_cast<float>(rsw + core::at(p, 0)));
                core::at(next_outer, 1) = static_cast<float>(u1 + (r1 + core::at(p, 1)));
                core::at(next_outer, 2) = static_cast<float>(u2 + r2);
                const double x = rx * si + core::at(p, 0);
                const double y = static_cast<double>(static_cast<float>(ry * si)) + core::at(p, 1);
                auto z = static_cast<float>(static_cast<double>(static_cast<float>(rz * si)) + core::at(p, 2));
                core::at(next_inner, 0) = static_cast<float>(ux * static_cast<double>(cosine) + x);
                core::at(next_inner, 1) =
                    static_cast<float>(static_cast<double>(static_cast<float>(uy * static_cast<double>(cosine))) + y);
                core::at(next_inner, 2) =
                    static_cast<float>(static_cast<double>(static_cast<float>(uz * static_cast<double>(cosine))) + z);
                vertex[0] = {.x = core::at(p, 0), .y = core::at(p, 1), .z = core::at(p, 2), .colour = core::at(colours, 0)};
                vertex[1] = {.x = core::at(outer, 0), .y = core::at(outer, 1), .z = core::at(outer, 2), .colour = core::at(colours, 1)};
                vertex[2] = {.x = core::at(next_outer, 0), .y = core::at(next_outer, 1), .z = core::at(next_outer, 2), .colour = core::at(colours, 1)};
                vertex[3] = {.x = core::at(outer, 0), .y = core::at(outer, 1), .z = core::at(outer, 2), .colour = core::at(colours, 1)};
                vertex[4] = {.x = core::at(next_outer, 0), .y = core::at(next_outer, 1), .z = core::at(next_outer, 2), .colour = core::at(colours, 1)};
                vertex[kSixthVertex] = {.x = core::at(inner, 0), .y = core::at(inner, 1), .z = core::at(inner, 2), .colour = core::at(colours, 2)};
                vertex[kSeventhVertex] = {.x = core::at(next_outer, 0), .y = core::at(next_outer, 1), .z = core::at(next_outer, 2), .colour = core::at(colours, 1)};
                vertex[kEighthVertex] = {.x = core::at(inner, 0), .y = core::at(inner, 1), .z = core::at(inner, 2), .colour = core::at(colours, 2)};
                vertex[kNinthVertex] = {.x = core::at(next_inner, 0), .y = core::at(next_inner, 1), .z = core::at(next_inner, 2), .colour = core::at(colours, 2)};
                vertex += kGlowVerticesPerSegment;
                std::memcpy(outer.data(), next_outer.data(), sizeof(outer));
                std::memcpy(inner.data(), next_inner.data(), sizeof(inner));
            }
            words += particle_words;
            remaining -= particle_words;
            ++index;
            place += 3;
            ++particle;
        }
        const uint32_t triangles = (words >> 4) / 3; // (the original's count: a quarter of what it wrote)
        if (!engine_parts().suppress_draw()) {                // (rendering not skipped)
            renderer().count_draw(triangles);
            renderer().d3d_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, buffer.data(), sizeof(TrailVertex));
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
    material =
        memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, 0, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha, 0, 0)
               : nullptr;
}

// 0x004C0AC0
PcLineParticles::~PcLineParticles() { free_material(material); }

// 0x004C0B30
void PcLineParticles::render()
{
    if (particle_count() <= 0)
        return;
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader95c), material->material());
    int remaining = particle_count() * 2;
    int index = 0;
    core::Cursor<const FlexibleParticle> particle(particle_list());
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t lines = 0;
        auto vertex = os_layer::lock_vertices<TrailVertex>(particle_count() * 2, &buffer, &first_vertex, &room);
        while (index < particle_count()) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            std::array<float, 3> p0{};
            std::array<float, 3> p1{};
            place_of(this, index, p0);
            place_of(this, max_particles() + index, p1);
            const ColourSpan span = colour_span(*this, life);
            std::array<uint32_t, 2> colours{};
            for (int k = 0; k < 2; ++k)
                core::at(colours, k) = get_colour(*this, span.from[k], span.to[k], span.t);
            vertex[0] = {.x = core::at(p0, 0), .y = core::at(p0, 1), .z = core::at(p0, 2), .colour = core::at(colours, 0)};
            vertex[1] = {.x = core::at(p1, 0), .y = core::at(p1, 1), .z = core::at(p1, 2), .colour = core::at(colours, 1)};
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
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        renderer().d3d_device()->DrawPrimitive(D3DPT_LINELIST, first_vertex, lines);
        renderer().count_draw(lines);
    }
}

// --- ribbon ---

// 0x004C0EB0
PcRibbonParticles::PcRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                     int fixed_alpha, int segment_count, float unknown, int history)
    : RibbonParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, history)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, kLineMaterialKind, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha,
                                                             segment_count, 0)
                      : nullptr;
}

// 0x004C0F80
PcRibbonParticles::~PcRibbonParticles() { free_material(material); }

// 0x004C0FF0
void PcRibbonParticles::render()
{
    if (particle_count() <= 0)
        return;
    const double a = core::at(renderer().view_matrix().row, 0).z;
    const double b = core::at(renderer().view_matrix().row, 1).z;
    const double c = core::at(renderer().view_matrix().row, 2).z;
    const bool textured = core::at(material->material()->textures(), 0) != nullptr;
    const uint32_t stride = textured ? sizeof(QuadVertex) : sizeof(TrailVertex);
    set_material(material->material());
    if (textured) {
        set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        set_pixel_shader(default_pixel_shader(), nullptr);
    } else {
        set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        set_pixel_shader(core::at(pixel_shaders(), kPixelShader95c), material->material());
    }
    core::Cursor<const FlexibleParticle> particle(particle_list());
    core::Cursor<const float> head(position_array());
    core::Cursor<const float> tail(position_array(), 3 * (steps() - 1) * max_particles());
    int index = 0;
    int remaining = particle_count() * 4;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        const gpu::VertexWriter out = locked_writer(remaining, stride, &buffer, &first_vertex, &room);
        while (index < particle_count()) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            double hx = 0.0;
            double hy = 0.0;
            float hz = 0.0f;
            std::array<float, 3> t{};
            if (emitter_independent()) {
                hx = head[0], hy = head[1], hz = head[2];
                core::at(t, 0) = tail[0], core::at(t, 1) = tail[1], core::at(t, 2) = tail[2];
            } else {
                hx = static_cast<double>(at_point().x) + head[0];
                hy = static_cast<double>(head[1]) + at_point().y;
                hz = static_cast<float>(static_cast<double>(head[2]) + at_point().z);
                core::at(t, 0) = static_cast<float>(static_cast<double>(at_point().x) + tail[0]);
                core::at(t, 1) = static_cast<float>(static_cast<double>(tail[1]) + at_point().y);
                core::at(t, 2) = static_cast<float>(static_cast<double>(tail[2]) + at_point().z);
            }
            // Across the ribbon: (tail - head) x the camera's direction, normalised when it has a length.
            auto dx = static_cast<float>(core::at(t, 0) - hx);
            auto dy = static_cast<float>(core::at(t, 1) - hy);
            auto dz = static_cast<float>(static_cast<double>(core::at(t, 2)) - hz);
            double n0 = dy * c - dz * b;
            double n1 = dz * a - c * dx;
            auto n2 = static_cast<float>(b * dx - dy * a);
            const double length = std::sqrt(n0 * n0 + static_cast<double>(n2) * n2 + n1 * n1);
            if (length > 0.0f) {
                const double inverse = 1.0f / length;
                n0 *= inverse;
                n1 *= inverse;
                n2 = static_cast<float>(n2 * inverse);
            }
            auto w =
                static_cast<float>((static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                   core::at(particle->start_size_, 0));
            const double n0w = n0 * w;
            const double n1w = n1 * w;
            const double n2w = static_cast<double>(n2) * w;
            auto f0 = static_cast<float>(n0w);
            auto f1 = static_cast<float>(n1w);
            auto f2 = static_cast<float>(n2w);
            const std::array<std::array<float, 3>, 4> corners = {{
                {static_cast<float>(f0 + hx), static_cast<float>(f1 + hy), static_cast<float>(n2w + hz)},
                {static_cast<float>(hx - f0), static_cast<float>(hy - f1),
                 static_cast<float>(static_cast<double>(hz) - f2)},
                {static_cast<float>(static_cast<double>(core::at(t, 0)) - f0), static_cast<float>(static_cast<double>(core::at(t, 1)) - f1),
                 static_cast<float>(static_cast<double>(core::at(t, 2)) - f2)},
                {static_cast<float>(static_cast<double>(f0) + core::at(t, 0)), static_cast<float>(static_cast<double>(f1) + core::at(t, 1)),
                 static_cast<float>(static_cast<double>(f2) + core::at(t, 2))}}};
            const ColourSpan span = colour_span(*this, life);
            std::array<uint32_t, 2> colours{};
            for (int k = 0; k < 2; ++k)
                core::at(colours, k) = get_colour(*this, span.from[k], span.to[k], span.t);
            static constexpr std::array<std::array<float, 2>, 4> kUv = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
            for (int k = 0; k < 4; ++k) {
                const uint32_t colour = core::at(colours, k / 2);
                if (textured) {
                    const QuadVertex vertex = {.x = core::at(core::at(corners, k), 0), .y = core::at(core::at(corners, k), 1),
                                               .z = core::at(core::at(corners, k), 2), .colour = colour, .u = core::at(core::at(kUv, k), 0),
                                               .v = core::at(core::at(kUv, k), 1)};
                    out.set_value(vertices + static_cast<size_t>(k), 0, vertex);
                } else {
                    const TrailVertex vertex = {.x = core::at(core::at(corners, k), 0), .y = core::at(core::at(corners, k), 1),
                                                .z = core::at(core::at(corners, k), 2), .colour = colour};
                    out.set_value(vertices + static_cast<size_t>(k), 0, vertex);
                }
            }
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
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, stride);
        renderer().d3d_device()->SetIndices(quad_index_buffer());
        renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        renderer().count_draw(triangles);
    }
}

// --- ribbontrail ---

// The colour of a ribbon trail's head end (inline in its Render): lerp_colour's channels, the red, green and blue
// scaled by the brightness alone, not clamped (each the low byte).
uint32_t PcRibbonTrailParticles::head_colour(uint32_t from, uint32_t to, float t)
{
    const colour::Bytes f = colour::bytes_of(from);
    const colour::Bytes g = colour::bytes_of(to);
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(core::at(f, k) + truncate(static_cast<double>(core::at(g, k) - core::at(f, k)) * t));
    };
    std::array<uint8_t, 4> bytes = {{lerp(2), lerp(1), lerp(0), lerp(3)}};
    for (int k = 0; k < 3; ++k)
        core::at(bytes, k) = static_cast<uint8_t>(truncate(static_cast<double>(core::at(bytes, k)) * brightness()));
    uint32_t colour = 0;
    std::memcpy(&colour, bytes.data(), 4);
    return colour;
}

// 0x004C1720
PcRibbonTrailParticles::PcRibbonTrailParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                               int fixed_alpha, int segment_count, float unknown, int history)
    : RibbonTrailParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, history)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, kRibbonTrailMaterialKind, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha,
                                                             segment_count, history)
                      : nullptr;
}

// 0x004C17F0
PcRibbonTrailParticles::~PcRibbonTrailParticles() { free_material(material); }

// 0x004C1860
void PcRibbonTrailParticles::render()
{
    if (particle_count() <= 0)
        return;
    const double a = core::at(renderer().view_matrix().row, 0).z;
    const double b = core::at(renderer().view_matrix().row, 1).z;
    const double c = core::at(renderer().view_matrix().row, 2).z;
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader95c), material->material());
    core::Cursor<const FlexibleParticle> particle(particle_list());
    int remaining = (steps() - 1) * particle_count() * 4;
    int index = 0;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        auto vertex = os_layer::lock_vertices<TrailVertex>(remaining, &buffer, &first_vertex, &room);
        while (index < particle_count()) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            std::array<float, 3> p0{};
            std::array<float, 3> p1{};
            std::array<float, 3> n{};
            place_of(this, index, p0);
            place_of(this, max_particles() + index, p1);
            {
                const double dx = static_cast<double>(core::at(p1, 0)) - core::at(p0, 0);
                const double dy = static_cast<double>(core::at(p1, 1)) - core::at(p0, 1);
                const double dz = static_cast<double>(core::at(p1, 2)) - core::at(p0, 2);
                core::at(n, 0) = static_cast<float>(dy * c - dz * b);
                core::at(n, 1) = static_cast<float>(dz * a - c * dx);
                core::at(n, 2) = static_cast<float>(b * dx - dy * a);
                const double length = std::sqrt(static_cast<double>(core::at(n, 2)) * core::at(n, 2) + static_cast<double>(core::at(n, 1)) * core::at(n, 1) +
                                                static_cast<double>(core::at(n, 0)) * core::at(n, 0));
                if (length > 0.0f) {
                    const double inverse = 1.0f / length;
                    for (float& f : n)
                        f = static_cast<float>(f * inverse);
                }
            }
            auto w =
                static_cast<float>((static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                   core::at(particle->start_size_, 0));
            const ColourSpan span = colour_span(*this, life);
            core::Cursor<const uint32_t> from(span.from);
            core::Cursor<const uint32_t> to(span.to);
            uint32_t previous_colour = head_colour(*from++, *to++, span.t);
            double n0w = static_cast<double>(core::at(n, 0)) * w;
            double n1w = static_cast<double>(core::at(n, 1)) * w;
            double n2w = static_cast<double>(core::at(n, 2)) * w;
            auto f0 = static_cast<float>(n0w);
            auto f1 = static_cast<float>(n1w);
            auto f2 = static_cast<float>(n2w);
            std::array<float, 3> previous_plus = {{static_cast<float>(static_cast<double>(f0) + core::at(p0, 0)),
                                      static_cast<float>(static_cast<double>(f1) + core::at(p0, 1)),
                                      static_cast<float>(n2w + core::at(p0, 2))}};
            std::array<float, 3> previous_minus = {{static_cast<float>(static_cast<double>(core::at(p0, 0)) - f0),
                                       static_cast<float>(static_cast<double>(core::at(p0, 1)) - f1),
                                       static_cast<float>(static_cast<double>(core::at(p0, 2)) - f2)}};
            for (int k = 1; k < steps(); ++k) {
                const uint32_t colour = get_colour(*this, *from++, *to++, span.t);
                if (k > 1) {
                    place_of(this, max_particles() * k + index, p1);
                    const double dx = static_cast<double>(core::at(p1, 0)) - core::at(p0, 0);
                    const double dy = static_cast<double>(core::at(p1, 1)) - core::at(p0, 1);
                    const double dz = static_cast<double>(core::at(p1, 2)) - core::at(p0, 2);
                    core::at(n, 0) = static_cast<float>(dy * c - dz * b);
                    core::at(n, 1) = static_cast<float>(dz * a - c * dx);
                    const double z = b * dx - dy * a;
                    core::at(n, 2) = static_cast<float>(z);
                    const double length =
                        std::sqrt(z * z + static_cast<double>(core::at(n, 1)) * core::at(n, 1) + static_cast<double>(core::at(n, 0)) * core::at(n, 0));
                    if (length > 0.0f) {
                        const double inverse = 1.0f / length;
                        core::at(n, 0) = static_cast<float>(core::at(n, 0) * inverse);
                        core::at(n, 1) = static_cast<float>(core::at(n, 1) * inverse);
                        core::at(n, 2) = static_cast<float>(z * inverse);
                    }
                }
                n0w = static_cast<double>(core::at(n, 0)) * w;
                n1w = static_cast<double>(core::at(n, 1)) * w;
                n2w = static_cast<double>(core::at(n, 2)) * w;
                f0 = static_cast<float>(n0w), f1 = static_cast<float>(n1w), f2 = static_cast<float>(n2w);
                std::array<float, 3> plus = {{static_cast<float>(static_cast<double>(f0) + core::at(p1, 0)), static_cast<float>(n1w + core::at(p1, 1)),
                                 static_cast<float>(n2w + core::at(p1, 2))}};
                std::array<float, 3> minus = {{static_cast<float>(static_cast<double>(core::at(p1, 0)) - f0),
                                  static_cast<float>(static_cast<double>(core::at(p1, 1)) - f1),
                                  static_cast<float>(static_cast<double>(core::at(p1, 2)) - f2)}};
                vertex[0] = {.x = core::at(previous_plus, 0), .y = core::at(previous_plus, 1), .z = core::at(previous_plus, 2), .colour = previous_colour};
                vertex[1] = {.x = core::at(previous_minus, 0), .y = core::at(previous_minus, 1), .z = core::at(previous_minus, 2), .colour = previous_colour};
                vertex[2] = {.x = core::at(minus, 0), .y = core::at(minus, 1), .z = core::at(minus, 2), .colour = colour};
                vertex[3] = {.x = core::at(plus, 0), .y = core::at(plus, 1), .z = core::at(plus, 2), .colour = colour};
                vertex += 4;
                previous_colour = colour;
                std::memcpy(p0.data(), p1.data(), sizeof(p1));
                std::memcpy(previous_plus.data(), plus.data(), sizeof(plus));
                std::memcpy(previous_minus.data(), minus.data(), sizeof(minus));
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
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        renderer().d3d_device()->SetIndices(quad_index_buffer());
        renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        renderer().count_draw(triangles);
    }
}

// --- shaded ---

// 0x004C21A0
PcShadedParticles::PcShadedParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                     int fixed_alpha)
    : ShadedParticles(name, particle_cap, texture, blend_mode, fixed_alpha, 0, 0, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material =
        memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, 2, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha, 0, 0)
               : nullptr;
}

// 0x004C2260
PcShadedParticles::~PcShadedParticles() { free_material(material); }

// 0x004C22D0
void PcShadedParticles::render()
{
    if (particle_count() <= 0)
        return;
    maths::Vec4 r;
    maths::Vec4 u;
    if (aligned())
        aligned_axes(*this, r, u);
    else
        camera_axes(r, u);
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    set_pixel_shader(material->shader(), nullptr);
    core::Cursor<const FlexibleParticle> particle(particle_list());
    core::Cursor<const float> place(position_array());
    int index = 0;
    int remaining = particle_count() * 4;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        auto vertex = os_layer::lock_vertices<QuadVertex>(remaining, &buffer, &first_vertex, &room);
        if (index < particle_count()) {
            for (uint32_t bytes = sizeof(QuadVertex); bytes <= kMaxBatchBytes; bytes += sizeof(QuadVertex)) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                const double across = (static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                      core::at(particle->start_size_, 0);
                const double up = (static_cast<double>(core::at(particle->end_size_, 1)) - core::at(particle->start_size_, 1)) * life +
                                  core::at(particle->start_size_, 1);
                std::array<float, 3> p{};
                if (emitter_independent()) {
                    core::at(p, 0) = place[0], core::at(p, 1) = place[1], core::at(p, 2) = place[2];
                } else {
                    core::at(p, 0) = static_cast<float>(static_cast<double>(at_point().x) + place[0]);
                    core::at(p, 1) = static_cast<float>(static_cast<double>(place[1]) + at_point().y);
                    core::at(p, 2) = static_cast<float>(static_cast<double>(place[2]) + at_point().z);
                }
                auto rx = static_cast<float>(r.x * across);
                auto ry = static_cast<float>(r.y * across);
                auto rz = static_cast<float>(r.z * across);
                auto ux = static_cast<float>(u.x * up);
                auto uy = static_cast<float>(u.y * up);
                auto uz = static_cast<float>(u.z * up);
                // The corners' colours (each its own of the colour arrays').
                const ColourSpan span = colour_span(*this, life);
                std::array<uint32_t, 4> colours{};
                for (int k = 0; k < 4; ++k)
                    core::at(colours, k) = get_colour(*this, span.from[k], span.to[k], span.t);
                // The texture: all of it, or (UseRandomUVQuadrants) a quarter by the particle's index.
                float u0 = 0.0f;
                float v0 = 0.0f;
                float u1 = 1.0f;
                float v1 = 1.0f;
                if (flag_bits() & kFlagRandomUvQuadrants) {
                    switch (index % 4) {
                    case 0:
                        u0 = kUvHalf, v0 = kUvHalf;
                        break;
                    case 1:
                        v1 = kUvHalf, u0 = kUvHalf;
                        break;
                    case 2:
                        v1 = kUvHalf, u1 = kUvHalf;
                        break;
                    case 3:
                        u1 = kUvHalf, v0 = kUvHalf;
                        break;
                    default:
                        break;
                    }
                }
                const double e0 = static_cast<double>(rx) + core::at(p, 0);
                const double e1 = static_cast<double>(ry) + core::at(p, 1);
                const double e2 = static_cast<double>(rz) + core::at(p, 2);
                auto e0f = static_cast<float>(e0);
                auto e1f = static_cast<float>(e1);
                auto e2f = static_cast<float>(e2);
                const double g0 = static_cast<double>(core::at(p, 0)) - rx;
                const double g1 = static_cast<double>(core::at(p, 1)) - ry;
                const double g2 = static_cast<double>(core::at(p, 2)) - rz;
                auto g0f = static_cast<float>(g0);
                auto g1f = static_cast<float>(g1);
                auto g2f = static_cast<float>(g2);
                vertex[0] = {.x = static_cast<float>(static_cast<double>(e0f) - ux),
                             .y = static_cast<float>(static_cast<double>(e1f) - uy),
                             .z = static_cast<float>(e2 - uz),
                             .colour = core::at(colours, 0),
                             .u = u0,
                             .v = v0};
                vertex[1] = {.x = static_cast<float>(static_cast<double>(g0f) - ux),
                             .y = static_cast<float>(static_cast<double>(g1f) - uy),
                             .z = static_cast<float>(g2 - uz),
                             .colour = core::at(colours, 1),
                             .u = u1,
                             .v = v0};
                vertex[2] = {.x = static_cast<float>(static_cast<double>(g0f) + ux),
                             .y = static_cast<float>(static_cast<double>(g1f) + uy),
                             .z = static_cast<float>(static_cast<double>(g2f) + uz),
                             .colour = core::at(colours, 2),
                             .u = u1,
                             .v = v1};
                vertex[3] = {.x = static_cast<float>(static_cast<double>(e0f) + ux),
                             .y = static_cast<float>(static_cast<double>(e1f) + uy),
                             .z = static_cast<float>(static_cast<double>(e2f) + uz),
                             .colour = core::at(colours, 3),
                             .u = u0,
                             .v = v1};
                vertex += 4;
                vertices += 4;
                triangles += 2;
                if (vertices >= room - 4)
                    break;
                ++index;
                ++particle;
                place += 3;
                if (index >= particle_count())
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(QuadVertex));
        if (vertices >= room - 4)
            break;
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(QuadVertex));
        renderer().d3d_device()->SetIndices(quad_index_buffer());
        renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        renderer().count_draw(triangles);
    }
}

// --- smooth ---

// 0x004C2B30
PcSmoothParticles::PcSmoothParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                     int fixed_alpha, int segment_count, float unknown)
    : SmoothParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, 0)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material =
        memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, 3, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha, 0, 0)
               : nullptr;
}

// 0x004C2C00
PcSmoothParticles::~PcSmoothParticles() { free_material(material); }

// 0x004C2C70
void PcSmoothParticles::render()
{
    if (particle_count() <= 0)
        return;
    maths::Vec4 r;
    maths::Vec4 u;
    if (aligned())
        aligned_axes(*this, r, u);
    else
        camera_axes(r, u);
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    set_pixel_shader(default_pixel_shader(), nullptr);
    core::Cursor<const FlexibleParticle> particle(particle_list());
    core::Cursor<const float> place(position_array());
    int index = 0;
    int remaining = particle_count() * kSmoothParticleVertices;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        auto vertex = os_layer::lock_vertices<QuadVertex>(remaining, &buffer, &first_vertex, &room);
        while (index < particle_count()) {
            auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
            const double across = (static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                  core::at(particle->start_size_, 0);
            auto up =
                static_cast<float>((static_cast<double>(core::at(particle->end_size_, 1)) - core::at(particle->start_size_, 1)) * life +
                                   core::at(particle->start_size_, 1));
            std::array<float, 3> p{};
            if (emitter_independent()) {
                core::at(p, 0) = place[0], core::at(p, 1) = place[1], core::at(p, 2) = place[2];
            } else {
                core::at(p, 0) = static_cast<float>(static_cast<double>(at_point().x) + place[0]);
                core::at(p, 1) = static_cast<float>(static_cast<double>(place[1]) + at_point().y);
                core::at(p, 2) = static_cast<float>(static_cast<double>(place[2]) + at_point().z);
            }
            auto rx = static_cast<float>(r.x * across);
            auto ry = static_cast<float>(r.y * across);
            auto rz = static_cast<float>(r.z * across);
            const double ux = static_cast<double>(u.x) * up;
            auto uy = static_cast<float>(static_cast<double>(u.y) * up);
            auto uz = static_cast<float>(static_cast<double>(u.z) * up);
            const double g0 = static_cast<double>(core::at(p, 0)) - rx;
            const double g1 = static_cast<double>(core::at(p, 1)) - ry;
            const double g2 = static_cast<double>(core::at(p, 2)) - rz;
            auto g0f = static_cast<float>(g0);
            auto g1f = static_cast<float>(g1);
            auto g2f = static_cast<float>(g2);
            const double e0 = static_cast<double>(rx) + core::at(p, 0);
            const double e1 = static_cast<double>(ry) + core::at(p, 1);
            auto e0f = static_cast<float>(e0);
            auto e1f = static_cast<float>(e1);
            auto e2f = static_cast<float>(static_cast<double>(rz) + core::at(p, 2));
            // The four corners (each with a triangle to the centre).
            const std::array<std::array<float, 3>, 4> k = {{{static_cast<float>(g0f + ux), static_cast<float>(static_cast<double>(g1f) + uy),
                                    static_cast<float>(g2 + uz)},
                                   {static_cast<float>(e0f + ux), static_cast<float>(static_cast<double>(e1f) + uy),
                                    static_cast<float>(static_cast<double>(e2f) + uz)},
                                   {static_cast<float>(e0f - ux), static_cast<float>(static_cast<double>(e1f) - uy),
                                    static_cast<float>(static_cast<double>(e2f) - uz)},
                                   {static_cast<float>(g0f - ux), static_cast<float>(static_cast<double>(g1f) - uy),
                                    static_cast<float>(static_cast<double>(g2f) - uz)}}};
            static constexpr std::array<std::array<float, 2>, 4> kUv = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
            // The centre's colour and the edge's.
            const ColourSpan span = colour_span(*this, life);
            std::array<uint32_t, 2> colours{};
            for (int i = 0; i < 2; ++i)
                core::at(colours, i) = get_colour(*this, span.from[i], span.to[i], span.t);
            for (int corner = 0; corner < 4; ++corner) {
                const int next = (corner + 1) % 4;
                vertex[0] = {.x = core::at(p, 0), .y = core::at(p, 1), .z = core::at(p, 2), .colour = core::at(colours, 0), .u = 0.0f, .v = 0.0f};
                vertex[1] = {.x = core::at(core::at(k, corner), 0), .y = core::at(core::at(k, corner), 1), .z = core::at(core::at(k, corner), 2), .colour = core::at(colours, 1), .u = core::at(core::at(kUv, corner), 0), .v = core::at(core::at(kUv, corner), 1)};
                vertex[2] = {.x = core::at(core::at(k, next), 0), .y = core::at(core::at(k, next), 1), .z = core::at(core::at(k, next), 2), .colour = core::at(colours, 1), .u = core::at(core::at(kUv, next), 0), .v = core::at(core::at(kUv, next), 1)};
                vertex += 3;
            }
            vertices += kSmoothParticleVertices;
            triangles += 4;
            if (vertices >= room - kSmoothParticleVertices)
                break;
            ++particle;
            ++index;
            place += 3;
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(QuadVertex));
        if (vertices >= room - kSmoothParticleVertices)
            break;
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(QuadVertex));
        renderer().d3d_device()->DrawPrimitive(D3DPT_TRIANGLELIST, first_vertex, triangles);
        renderer().count_draw(triangles);
    }
}

// --- smoothribbon ---

// 0x004C3550
PcSmoothRibbonParticles::PcSmoothRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                                 int fixed_alpha, int segment_count, float unknown, int history)
    : SmoothRibbonParticles(name, particle_cap, texture, blend_mode, fixed_alpha, segment_count, unknown, history)
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material =
        memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, kSmoothMaterialKind, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha, 0, 0)
               : nullptr;
}

// 0x004C3620
PcSmoothRibbonParticles::~PcSmoothRibbonParticles() { free_material(material); }

// 0x004C3530
int PcSmoothRibbonParticles::get_num_vertex_lists() { return segments; }

// 0x004C3690
void PcSmoothRibbonParticles::render()
{
    if (particle_count() <= 0)
        return;
    const double a = core::at(renderer().view_matrix().row, 0).z;
    const double b = core::at(renderer().view_matrix().row, 1).z;
    const double c = core::at(renderer().view_matrix().row, 2).z;
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader95c), material->material());
    core::Cursor<const float> head(position_array());
    core::Cursor<const float> tail(position_array(), 3 * (segments - 1) * max_particles());
    core::Cursor<const FlexibleParticle> particle(particle_list());
    int index = 0;
    int remaining = particle_count() * kSmoothRibbonVertices;
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        auto vertex = os_layer::lock_vertices<TrailVertex>(remaining, &buffer, &first_vertex, &room);
        if (index < particle_count()) {
            for (uint32_t bytes = kSmoothRibbonStep; bytes <= kMaxBatchBytes; bytes += kSmoothRibbonStep) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                std::array<float, 3> h{};
                std::array<float, 3> t{};
                if (emitter_independent()) {
                    core::at(h, 0) = head[0], core::at(h, 1) = head[1], core::at(h, 2) = head[2];
                    core::at(t, 0) = tail[0], core::at(t, 1) = tail[1], core::at(t, 2) = tail[2];
                } else {
                    for (int k = 0; k < 3; ++k) {
                        core::at(h, k) = static_cast<float>(static_cast<double>(head[k]) + at_point()[k]);
                        core::at(t, k) = static_cast<float>(static_cast<double>(tail[k]) + at_point()[k]);
                    }
                }
                auto dx = static_cast<float>(static_cast<double>(core::at(t, 0)) - core::at(h, 0));
                auto dy = static_cast<float>(static_cast<double>(core::at(t, 1)) - core::at(h, 1));
                auto dz = static_cast<float>(static_cast<double>(core::at(t, 2)) - core::at(h, 2));
                double n0 = dy * c - dz * b;
                double n1 = dz * a - c * dx;
                auto n2 = static_cast<float>(b * dx - dy * a);
                const double length = std::sqrt(n0 * n0 + static_cast<double>(n2) * n2 + n1 * n1);
                if (length > 0.0f) {
                    const double inverse = 1.0f / length;
                    n0 *= inverse;
                    n1 *= inverse;
                    n2 = static_cast<float>(n2 * inverse);
                }
                auto w =
                    static_cast<float>((static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                       core::at(particle->start_size_, 0));
                const double n0w = n0 * w;
                const double n1w = n1 * w;
                const double n2w = static_cast<double>(n2) * w;
                auto f0 = static_cast<float>(n0w);
                auto f1 = static_cast<float>(n1w);
                auto f2 = static_cast<float>(n2w);
                const std::array<float, 3> plus_head = {{static_cast<float>(static_cast<double>(f0) + core::at(h, 0)),
                                            static_cast<float>(static_cast<double>(f1) + core::at(h, 1)),
                                            static_cast<float>(n2w + core::at(h, 2))}};
                const std::array<float, 3> minus_head = {{static_cast<float>(static_cast<double>(core::at(h, 0)) - f0),
                                             static_cast<float>(static_cast<double>(core::at(h, 1)) - f1),
                                             static_cast<float>(static_cast<double>(core::at(h, 2)) - f2)}};
                const std::array<float, 3> minus_tail = {{static_cast<float>(static_cast<double>(core::at(t, 0)) - f0),
                                             static_cast<float>(static_cast<double>(core::at(t, 1)) - f1),
                                             static_cast<float>(static_cast<double>(core::at(t, 2)) - f2)}};
                const std::array<float, 3> plus_tail = {{static_cast<float>(static_cast<double>(f0) + core::at(t, 0)),
                                            static_cast<float>(static_cast<double>(f1) + core::at(t, 1)),
                                            static_cast<float>(n2w + core::at(t, 2))}};
                const ColourSpan span = colour_span(*this, life);
                std::array<uint32_t, 4> colours{};
                for (int k = 0; k < 4; ++k)
                    core::at(colours, k) = get_colour(*this, span.from[k], span.to[k], span.t);
                // Two quads along the middle: to the plus side and to the minus side.
                vertex[0] = {.x = core::at(h, 0), .y = core::at(h, 1), .z = core::at(h, 2), .colour = core::at(colours, 0)};
                vertex[1] = {.x = core::at(t, 0), .y = core::at(t, 1), .z = core::at(t, 2), .colour = core::at(colours, 3)};
                vertex[2] = {.x = core::at(plus_tail, 0), .y = core::at(plus_tail, 1), .z = core::at(plus_tail, 2), .colour = core::at(colours, 2)};
                vertex[3] = {.x = core::at(plus_head, 0), .y = core::at(plus_head, 1), .z = core::at(plus_head, 2), .colour = core::at(colours, 1)};
                vertex[4] = {.x = core::at(h, 0), .y = core::at(h, 1), .z = core::at(h, 2), .colour = core::at(colours, 0)};
                vertex[kSixthVertex] = {.x = core::at(t, 0), .y = core::at(t, 1), .z = core::at(t, 2), .colour = core::at(colours, 3)};
                vertex[kSeventhVertex] = {.x = core::at(minus_tail, 0), .y = core::at(minus_tail, 1), .z = core::at(minus_tail, 2), .colour = core::at(colours, 2)};
                vertex[kEighthVertex] = {.x = core::at(minus_head, 0), .y = core::at(minus_head, 1), .z = core::at(minus_head, 2), .colour = core::at(colours, 1)};
                vertex += kSmoothRibbonVertices;
                vertices += kSmoothRibbonVertices;
                triangles += 4;
                if (vertices >= room - kSmoothRibbonVertices)
                    break;
                ++particle;
                ++index;
                head += 3;
                tail += 3;
                if (index >= particle_count())
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - kSmoothRibbonVertices)
            break;
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        renderer().d3d_device()->SetIndices(quad_index_buffer());
        renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        renderer().count_draw(triangles);
    }
}

// --- smoothstar ---

// 0x004C3D80
PcSmoothStarParticles::PcSmoothStarParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                             int fixed_alpha, int segment_count, float unknown)
    : SmoothStarParticles({.name = name, .particle_cap_ = particle_cap, .texture = texture, .blend_mode_ = blend_mode,
                           .fixed_alpha_ = fixed_alpha, .segments = segment_count, .unknown = unknown, .history = 0})
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, kSmoothRibbonMaterialKind, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha,
                                                             segment_count, 0)
                      : nullptr;
}

// 0x004C3E50
PcSmoothStarParticles::~PcSmoothStarParticles() { free_material(material); }

// 0x004C3EC0
void PcSmoothStarParticles::render()
{
    if (particle_count() <= 0)
        return;
    maths::Vec4 r;
    maths::Vec4 u;
    camera_axes(r, u);
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader95c), material->material());
    core::Cursor<const FlexibleParticle> particle(particle_list());
    core::Cursor<const float> place(position_array());
    int index = 0;
    int remaining = segments() * particle_count() * kSmoothStarVertices;
    constexpr float kDegrees = 0.0174532924f; // (0x00645464)
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        auto vertex = os_layer::lock_vertices<TrailVertex>(remaining, &buffer, &first_vertex, &room);
        if (index < particle_count()) {
            auto sin0 = static_cast<float>(std::sin(0.0));
            auto cos0 = static_cast<float>(std::cos(0.0));
            for (;;) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                const double across = (static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                      core::at(particle->start_size_, 0);
                const double up_size = (static_cast<double>(core::at(particle->end_size_, 1)) - core::at(particle->start_size_, 1)) * life +
                                       core::at(particle->start_size_, 1);
                std::array<float, 3> p{};
                if (emitter_independent()) {
                    core::at(p, 0) = place[0], core::at(p, 1) = place[1], core::at(p, 2) = place[2];
                } else {
                    core::at(p, 0) = static_cast<float>(static_cast<double>(at_point().x) + place[0]);
                    core::at(p, 1) = static_cast<float>(static_cast<double>(place[1]) + at_point().y);
                    core::at(p, 2) = static_cast<float>(static_cast<double>(place[2]) + at_point().z);
                }
                const std::array<float, 3> rv = {{static_cast<float>(r.x * across), static_cast<float>(r.y * across),
                                     static_cast<float>(r.z * across)}};
                const std::array<float, 3> uv = {{static_cast<float>(u.x * up_size), static_cast<float>(u.y * up_size),
                                     static_cast<float>(u.z * up_size)}};
                // The centre's, the points' and the edge's colours.
                const ColourSpan span = colour_span(*this, life);
                std::array<uint32_t, 3> colours{};
                for (int k = 0; k < 3; ++k)
                    core::at(colours, k) = get_colour(*this, span.from[k], span.to[k], span.t);
                const double w = star_extent();
                // The outer point at angle 0 (the previous one for the first segment).
                std::array<float, 3> outer{};
                {
                    const double x = static_cast<double>(core::at(rv, 0)) * sin0;
                    const double y = static_cast<double>(core::at(rv, 1)) * sin0;
                    auto z = static_cast<float>(static_cast<double>(core::at(rv, 2)) * sin0);
                    auto ox = static_cast<float>(static_cast<double>(static_cast<float>(x * w)) + core::at(p, 0));
                    auto oy = static_cast<float>(y * w + core::at(p, 1));
                    const double oz = static_cast<double>(z) * w + core::at(p, 2);
                    const double ux = static_cast<double>(cos0) * core::at(uv, 0);
                    const double uy = static_cast<double>(core::at(uv, 1)) * cos0;
                    auto uz = static_cast<float>(static_cast<double>(core::at(uv, 2)) * cos0);
                    core::at(outer, 0) = static_cast<float>(static_cast<double>(static_cast<float>(ux * w)) + ox);
                    core::at(outer, 1) = static_cast<float>(uy * w + oy);
                    core::at(outer, 2) =
                        static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(uz) * w)) + oz);
                }
                for (int i = 0, k = 1; i < segments(); ++i, k += 2) {
                    auto step = static_cast<float>(kFullCircleDegrees / (static_cast<double>(segments()) + segments()));
                    // The point between this segment's two outer points, out at the star's width.
                    const double angle1 = static_cast<double>(k) * step * kDegrees;
                    const double s1 = std::sin(angle1);
                    const double c1 = std::cos(angle1);
                    auto s1f = static_cast<float>(s1);
                    std::array<float, 3> tip{};
                    {
                        const double x = core::at(rv, 0) * s1 * w;
                        const double y = static_cast<double>(static_cast<float>(core::at(rv, 1) * s1)) * w;
                        auto z = static_cast<float>(static_cast<double>(static_cast<float>(core::at(rv, 2) * s1)) * w);
                        core::at(tip, 0) = static_cast<float>(x + core::at(p, 0));
                        core::at(tip, 1) = static_cast<float>(y + core::at(p, 1));
                        core::at(tip, 2) = static_cast<float>(static_cast<double>(z) + core::at(p, 2));
                        const double ux = core::at(uv, 0) * c1 * w;
                        const double uy = static_cast<double>(static_cast<float>(core::at(uv, 1) * c1)) * w;
                        auto uz = static_cast<float>(static_cast<double>(static_cast<float>(core::at(uv, 2) * c1)) * w);
                        core::at(tip, 0) = static_cast<float>(ux + core::at(tip, 0));
                        core::at(tip, 1) = static_cast<float>(uy + core::at(tip, 1));
                        core::at(tip, 2) = static_cast<float>(static_cast<double>(uz) + core::at(tip, 2));
                    }
                    // The next outer point.
                    const double angle2 = static_cast<double>(k + 1) * step * kDegrees;
                    const double s2 = std::sin(angle2);
                    const double c2 = std::cos(angle2);
                    std::array<float, 3> next{};
                    double next_y = 0.0;
                    {
                        const double x = core::at(rv, 0) * s2 * w;
                        const double y = static_cast<double>(static_cast<float>(core::at(rv, 1) * s2)) * w;
                        auto z = static_cast<float>(static_cast<double>(static_cast<float>(core::at(rv, 2) * s2)) * w);
                        auto nx = static_cast<float>(x + core::at(p, 0));
                        const double ny = y + core::at(p, 1);
                        auto nz = static_cast<float>(static_cast<double>(z) + core::at(p, 2));
                        const double ux = core::at(uv, 0) * c2 * w;
                        auto uy = static_cast<float>(static_cast<double>(static_cast<float>(core::at(uv, 1) * c2)) * w);
                        auto uz = static_cast<float>(static_cast<double>(static_cast<float>(core::at(uv, 2) * c2)) * w);
                        core::at(next, 0) = static_cast<float>(ux + nx);
                        next_y = static_cast<double>(uy) + ny;
                        core::at(next, 1) = static_cast<float>(next_y);
                        core::at(next, 2) = static_cast<float>(static_cast<double>(uz) + nz);
                    }
                    // The inner point at angle 1 (unscaled).
                    std::array<float, 3> inner{};
                    {
                        const double x = static_cast<double>(core::at(rv, 0)) * s1f + core::at(p, 0);
                        auto ry = static_cast<float>(static_cast<double>(core::at(rv, 1)) * s1f);
                        auto rz = static_cast<float>(static_cast<double>(core::at(rv, 2)) * s1f);
                        auto iy = static_cast<float>(static_cast<double>(ry) + core::at(p, 1));
                        auto iz = static_cast<float>(static_cast<double>(rz) + core::at(p, 2));
                        auto ux = static_cast<float>(core::at(uv, 0) * c1);
                        auto uy = static_cast<float>(core::at(uv, 1) * c1);
                        auto uz = static_cast<float>(core::at(uv, 2) * c1);
                        core::at(inner, 0) = static_cast<float>(static_cast<double>(ux) + x);
                        core::at(inner, 1) = static_cast<float>(static_cast<double>(uy) + iy);
                        core::at(inner, 2) = static_cast<float>(static_cast<double>(uz) + iz);
                    }
                    vertex[0] = {.x = core::at(tip, 0), .y = core::at(tip, 1), .z = core::at(tip, 2), .colour = core::at(colours, 1)};
                    vertex[1] = {.x = core::at(p, 0), .y = core::at(p, 1), .z = core::at(p, 2), .colour = core::at(colours, 0)};
                    vertex[2] = {.x = core::at(next, 0), .y = core::at(next, 1), .z = core::at(next, 2), .colour = core::at(colours, 2)};
                    vertex[3] = {.x = core::at(inner, 0), .y = core::at(inner, 1), .z = core::at(inner, 2), .colour = core::at(colours, 2)};
                    vertex[4] = {.x = core::at(tip, 0), .y = core::at(tip, 1), .z = core::at(tip, 2), .colour = core::at(colours, 1)};
                    vertex[kSixthVertex] = {.x = core::at(p, 0), .y = core::at(p, 1), .z = core::at(p, 2), .colour = core::at(colours, 0)};
                    vertex[kSeventhVertex] = {.x = core::at(outer, 0), .y = core::at(outer, 1), .z = core::at(outer, 2), .colour = core::at(colours, 2)};
                    vertex[kEighthVertex] = {.x = core::at(inner, 0), .y = core::at(inner, 1), .z = core::at(inner, 2), .colour = core::at(colours, 2)};
                    vertex += kSmoothStarVertices;
                    std::memcpy(outer.data(), next.data(), sizeof(next));
                    vertices += kSmoothStarVertices;
                    triangles += 4;
                    if (vertices >= room - kSmoothStarVertices)
                        break;
                }
                if (vertices >= room - kSmoothStarVertices)
                    break;
                ++index;
                ++particle;
                place += 3;
                if (index >= particle_count())
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - kSmoothStarVertices)
            break;
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        renderer().d3d_device()->SetIndices(quad_index_buffer());
        renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        renderer().count_draw(triangles);
    }
}

// --- star ---

// 0x004C48C0
PcStarParticles::PcStarParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode,
                                 int fixed_alpha, int segment_count, float unknown)
    : StarParticles({.name = name, .particle_cap_ = particle_cap, .texture = texture, .blend_mode_ = blend_mode,
                     .fixed_alpha_ = fixed_alpha, .segments = segment_count, .unknown = unknown, .history = 0})
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, kSmoothStarMaterialKind, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha,
                                                             segment_count, 0)
                      : nullptr;
}

// 0x004C4990
PcStarParticles::~PcStarParticles() { free_material(material); }

// 0x004C4A00
void PcStarParticles::render()
{
    if (particle_count() <= 0)
        return;
    maths::Vec4 r;
    maths::Vec4 u;
    camera_axes(r, u);
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader95c), material->material());
    core::Cursor<const FlexibleParticle> particle(particle_list());
    core::Cursor<const float> place(position_array());
    int index = 0;
    int remaining = segments() * particle_count() * 4;
    constexpr float kDegrees = 0.0174532924f; // (0x00645464)
    while (remaining != 0) {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint32_t first_vertex = 0;
        uint32_t room = 0;
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        auto vertex = os_layer::lock_vertices<TrailVertex>(remaining, &buffer, &first_vertex, &room);
        if (index < particle_count()) {
            auto sin0 = static_cast<float>(std::sin(0.0));
            auto cos0 = static_cast<float>(std::cos(0.0));
            for (;;) {
                auto life = static_cast<float>(static_cast<double>(particle->age) / particle->lifetime);
                const double across = (static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                      core::at(particle->start_size_, 0);
                const double up_size = (static_cast<double>(core::at(particle->end_size_, 1)) - core::at(particle->start_size_, 1)) * life +
                                       core::at(particle->start_size_, 1);
                std::array<float, 3> p{};
                if (emitter_independent()) {
                    core::at(p, 0) = place[0], core::at(p, 1) = place[1], core::at(p, 2) = place[2];
                } else {
                    core::at(p, 0) = static_cast<float>(static_cast<double>(at_point().x) + place[0]);
                    core::at(p, 1) = static_cast<float>(static_cast<double>(place[1]) + at_point().y);
                    core::at(p, 2) = static_cast<float>(static_cast<double>(place[2]) + at_point().z);
                }
                const std::array<float, 3> rv = {{static_cast<float>(r.x * across), static_cast<float>(r.y * across),
                                     static_cast<float>(r.z * across)}};
                const std::array<float, 3> uv = {{static_cast<float>(u.x * up_size), static_cast<float>(u.y * up_size),
                                     static_cast<float>(u.z * up_size)}};
                const ColourSpan span = colour_span(*this, life);
                std::array<uint32_t, 3> colours{};
                for (int k = 0; k < 3; ++k)
                    core::at(colours, k) = get_colour(*this, span.from[k], span.to[k], span.t);
                const double w = star_extent();
                // The outer point at angle 0 (the previous one for the first segment).
                std::array<float, 3> outer{};
                {
                    const double x = static_cast<double>(core::at(rv, 0)) * sin0;
                    const double y = static_cast<double>(core::at(rv, 1)) * sin0;
                    auto z = static_cast<float>(static_cast<double>(core::at(rv, 2)) * sin0);
                    auto ox = static_cast<float>(static_cast<double>(static_cast<float>(x * w)) + core::at(p, 0));
                    auto oy = static_cast<float>(y * w + core::at(p, 1));
                    const double oz = static_cast<double>(z) * w + core::at(p, 2);
                    const double ux = static_cast<double>(cos0) * core::at(uv, 0);
                    const double uy = static_cast<double>(core::at(uv, 1)) * cos0;
                    auto uz = static_cast<float>(static_cast<double>(core::at(uv, 2)) * cos0);
                    core::at(outer, 0) = static_cast<float>(static_cast<double>(static_cast<float>(ux * w)) + ox);
                    core::at(outer, 1) = static_cast<float>(uy * w + oy);
                    core::at(outer, 2) =
                        static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(uz) * w)) + oz);
                }
                for (int k = 1; k <= segments(); ++k) {
                    // The next outer point (out at the width) and the inner one between (unscaled).
                    const double angle_a = 360.0f / static_cast<double>(segments()) * k * kDegrees;
                    const double a = std::sin(angle_a);
                    const double c_a = std::cos(angle_a);
                    std::array<float, 3> next{};
                    {
                        const double x = core::at(rv, 0) * a * w;
                        const double y = static_cast<double>(static_cast<float>(core::at(rv, 1) * a)) * w;
                        auto z = static_cast<float>(static_cast<double>(static_cast<float>(core::at(rv, 2) * a)) * w);
                        auto nx = static_cast<float>(x + core::at(p, 0));
                        const double ny = y + core::at(p, 1);
                        auto nz = static_cast<float>(static_cast<double>(z) + core::at(p, 2));
                        const double ux = core::at(uv, 0) * c_a * w;
                        auto uy = static_cast<float>(static_cast<double>(static_cast<float>(core::at(uv, 1) * c_a)) * w);
                        auto uz = static_cast<float>(static_cast<double>(static_cast<float>(core::at(uv, 2) * c_a)) * w);
                        core::at(next, 0) = static_cast<float>(ux + nx);
                        core::at(next, 1) = static_cast<float>(static_cast<double>(uy) + ny);
                        core::at(next, 2) = static_cast<float>(static_cast<double>(uz) + nz);
                    }
                    const double angle_b =
                        360.0f / (static_cast<double>(segments()) + segments()) * (2 * k - 1) * kDegrees;
                    const double b = std::sin(angle_b);
                    const double c_b = std::cos(angle_b);
                    std::array<float, 3> inner{};
                    {
                        const double x = core::at(rv, 0) * b + core::at(p, 0);
                        auto iy = static_cast<float>(static_cast<double>(static_cast<float>(core::at(rv, 1) * b)) + core::at(p, 1));
                        auto iz = static_cast<float>(static_cast<double>(static_cast<float>(core::at(rv, 2) * b)) + core::at(p, 2));
                        auto ux = static_cast<float>(core::at(uv, 0) * c_b);
                        auto uy = static_cast<float>(core::at(uv, 1) * c_b);
                        auto uz = static_cast<float>(core::at(uv, 2) * c_b);
                        core::at(inner, 0) = static_cast<float>(static_cast<double>(ux) + x);
                        core::at(inner, 1) = static_cast<float>(static_cast<double>(uy) + iy);
                        core::at(inner, 2) = static_cast<float>(static_cast<double>(uz) + iz);
                    }
                    vertex[0] = {.x = core::at(p, 0), .y = core::at(p, 1), .z = core::at(p, 2), .colour = core::at(colours, 0)};
                    vertex[1] = {.x = core::at(outer, 0), .y = core::at(outer, 1), .z = core::at(outer, 2), .colour = core::at(colours, 1)};
                    vertex[2] = {.x = core::at(inner, 0), .y = core::at(inner, 1), .z = core::at(inner, 2), .colour = core::at(colours, 2)};
                    vertex[3] = {.x = core::at(next, 0), .y = core::at(next, 1), .z = core::at(next, 2), .colour = core::at(colours, 1)};
                    vertex += 4;
                    std::memcpy(outer.data(), next.data(), sizeof(next));
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
                if (index >= particle_count())
                    break;
            }
        }
        remaining -= static_cast<int>(vertices);
        os_layer::unlock_dynamic_vertices(vertices, sizeof(TrailVertex));
        if (vertices >= room - 4)
            break;
        renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
        renderer().d3d_device()->SetIndices(quad_index_buffer());
        renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, vertices, 0,
                                           triangles);
        renderer().count_draw(triangles);
    }
}

// --- glowribbontrail ---

// The colour `t` of the way from `from` to `to` in 4096ths (RGBA bytes in, BGRA out), not scaled.
uint32_t PcGlowRibbonTrailParticles::fine_colour(uint32_t from, uint32_t to, double t)
{
    const colour::Bytes f = colour::bytes_of(from);
    const colour::Bytes g = colour::bytes_of(to);
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(truncate((static_cast<double>(core::at(g, k) - core::at(f, k)) * t +
                                              (core::at(f, k) << kFixedPointShift)) * (1.0f / kFixedPointScale)));
    };
    std::array<uint8_t, 4> bytes = {{lerp(2), lerp(1), lerp(0), lerp(3)}};
    uint32_t colour = 0;
    std::memcpy(&colour, bytes.data(), 4);
    return colour;
}

// 0x004BF720
PcGlowRibbonTrailParticles::PcGlowRibbonTrailParticles(uint32_t name, int particle_cap, uint32_t texture,
                                                       uint32_t blend_mode, int fixed_alpha, int segment_count,
                                                       float unknown, int history)
    : GlowRibbonTrailParticles({.name = name, .particle_cap_ = particle_cap, .texture = texture, .blend_mode_ = blend_mode,
                                .fixed_alpha_ = fixed_alpha, .segments = segment_count, .unknown = unknown,
                                .history = history})
{
    void* memory = memory::alloc(sizeof(SimpleMaterial));
    material = memory ? memory::construct_in<SimpleMaterial>(memory, particle_cap, kGlowRibbonTrailMaterialKind, SimpleMaterial::MaterialNames{.texture = texture, .blend = blend_mode}, fixed_alpha,
                                                             segment_count, history)
                      : nullptr;
}

// 0x004C0940
PcGlowRibbonTrailParticles::~PcGlowRibbonTrailParticles() { free_material(material); }

// 0x004BF7F0
void PcGlowRibbonTrailParticles::render()
{
    if (particle_count() <= 0)
        return;
    const double a = core::at(renderer().view_matrix().row, 0).z;
    const double b = core::at(renderer().view_matrix().row, 1).z;
    const double c = core::at(renderer().view_matrix().row, 2).z;
    // The camera's across and up directions, not normalised.
    const double b0 = b * 0.0f;
    const std::array<float, 3> r = {{static_cast<float>(b0 - c), static_cast<float>(c * 0.0f - a * 0.0f),
                        static_cast<float>(a - b0)}};
    const std::array<float, 3> u = {{static_cast<float>(core::at(r, 1) * c - core::at(r, 2) * b), static_cast<float>(a * core::at(r, 2) - core::at(r, 0) * c),
                        static_cast<float>(core::at(r, 0) * b - a * core::at(r, 1))}};
    const uint32_t per_particle = segments() * 9 * 4 + (history_length() - 1) * 3 * 8;
    uint32_t remaining = particle_count() * per_particle;
    set_material(material->material());
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader95c), material->material());
    IDirect3DVertexBuffer9* buffer = nullptr;
    uint32_t first_vertex = 0;
    uint32_t room = 0;
    const uint32_t total_vertices = (segments() * 9 + (history_length() * 3 - 3) * 2) * particle_count();
    const uint32_t total_triangles = (segments() * 3 - 2 + history_length() * 2) * particle_count();
    auto vertex = os_layer::lock_vertices<TrailVertex>(total_vertices, &buffer, &first_vertex, &room);
    core::Cursor<const FlexibleParticle> particle(particle_list());
    int index = 0;
    while (remaining > 0) {
        for (; index < particle_count(); ++index, ++particle) {
            const double life = static_cast<double>(particle->age) / particle->lifetime;
            const core::Cursor<const float> p(position_array(), 3 * index);
            std::array<float, 3> p0 = {{p[0], p[1], p[2]}};
            std::array<float, 3> p1{};
            const core::Cursor<const float> q(position_array(), 3 * (max_particles() + index));
            core::at(p1, 0) = q[0], core::at(p1, 1) = q[1], core::at(p1, 2) = q[2];
            // Across the trail's first segment, normalised (unrounded).
            std::array<double, 3> n{};
            {
                auto dx = static_cast<float>(static_cast<double>(core::at(p1, 0)) - core::at(p0, 0));
                auto dy = static_cast<float>(static_cast<double>(core::at(p1, 1)) - core::at(p0, 1));
                auto dz = static_cast<float>(static_cast<double>(core::at(p1, 2)) - core::at(p0, 2));
                core::at(n, 0) = dy * c - dz * b;
                core::at(n, 1) = a * dz - dx * c;
                core::at(n, 2) = dx * b - a * dy;
                const double length = std::sqrt(core::at(n, 0) * core::at(n, 0) + core::at(n, 2) * core::at(n, 2) + core::at(n, 1) * core::at(n, 1));
                if (length > 0.0f) {
                    const double inverse = 1.0f / length;
                    core::at(n, 0) *= inverse;
                    core::at(n, 1) *= inverse;
                    core::at(n, 2) *= inverse;
                }
            }
            auto size0 =
                static_cast<float>((static_cast<double>(core::at(particle->end_size_, 0)) - core::at(particle->start_size_, 0)) * life +
                                   core::at(particle->start_size_, 0));
            auto size1 =
                static_cast<float>((static_cast<double>(core::at(particle->end_size_, 1)) - core::at(particle->start_size_, 1)) * life +
                                   core::at(particle->start_size_, 1));
            // The colours (unrounded fraction): the trail's from index 3 on.
            core::Cursor<const uint32_t> from(start_colour());
            core::Cursor<const uint32_t> to(end_colour());
            double t = life;
            if (colour_mid() >= 0.0f) {
                if (life < colour_mid()) {
                    t = life / colour_mid();
                    to = core::Cursor<const uint32_t>(mid_colour());
                } else {
                    t = (life - colour_mid()) / (1.0f - static_cast<double>(colour_mid()));
                    from = core::Cursor<const uint32_t>(mid_colour());
                }
            }
            uint32_t previous_colour = fine_colour(from[3], to[3], t);
            from += 4;
            to += 4;
            const double w = span();
            // The head end's edge points.
            std::array<float, 3> previous_a{};
            std::array<float, 3> previous_b{};
            {
                const double n0s = core::at(n, 0) * size0;
                const double n1s = core::at(n, 1) * size0;
                auto f0 = static_cast<float>(n0s);
                auto f2 = static_cast<float>(core::at(n, 2) * size0);
                auto o0 = static_cast<float>(static_cast<double>(f0) * w);
                auto o1 = static_cast<float>(w * n1s);
                auto o2 = static_cast<float>(w * static_cast<double>(f2));
                core::at(previous_a, 0) = static_cast<float>(static_cast<double>(o0) + core::at(p0, 0));
                core::at(previous_a, 1) = static_cast<float>(static_cast<double>(o1) + core::at(p0, 1));
                core::at(previous_a, 2) = static_cast<float>(static_cast<double>(o2) + core::at(p0, 2));
                core::at(previous_b, 0) = static_cast<float>(static_cast<double>(core::at(p0, 0)) - o0);
                core::at(previous_b, 1) = static_cast<float>(static_cast<double>(core::at(p0, 1)) - o1);
                core::at(previous_b, 2) = static_cast<float>(static_cast<double>(core::at(p0, 2)) - o2);
            }
            for (int k = 1; k < history_length(); ++k) {
                const uint32_t colour = fine_colour(*from++, *to++, t);
                if (k > 1) {
                    const core::Cursor<const float> next(position_array(), 3 * (max_particles() * k + index));
                    core::at(p1, 0) = next[0], core::at(p1, 1) = next[1], core::at(p1, 2) = next[2];
                    const double dx = static_cast<double>(core::at(p1, 0)) - core::at(p0, 0);
                    const double dy = static_cast<double>(core::at(p1, 1)) - core::at(p0, 1);
                    const double dz = static_cast<double>(core::at(p1, 2)) - core::at(p0, 2);
                    auto m0 = static_cast<float>(dy * c - dz * b);
                    auto m1 = static_cast<float>(a * dz - dx * c);
                    auto m2 = static_cast<float>(dx * b - a * dy);
                    const double length = std::sqrt(static_cast<double>(m2) * m2 + static_cast<double>(m1) * m1 +
                                                    static_cast<double>(m0) * m0);
                    if (length > 0.0f) {
                        auto inverse = static_cast<float>(1.0f / static_cast<double>(static_cast<float>(length)));
                        core::at(n, 0) = static_cast<double>(m0) * inverse;
                        core::at(n, 1) = static_cast<double>(inverse) * m1;
                        core::at(n, 2) = static_cast<double>(inverse) * m2;
                    } else {
                        core::at(n, 0) = m0, core::at(n, 1) = m1, core::at(n, 2) = m2;
                    }
                }
                const double n0s = core::at(n, 0) * size0;
                const double n1s = core::at(n, 1) * size0;
                auto f0 = static_cast<float>(n0s);
                auto f2 = static_cast<float>(core::at(n, 2) * size0);
                auto o0 = static_cast<float>(static_cast<double>(f0) * w);
                auto o1 = static_cast<float>(w * n1s);
                auto o2 = static_cast<float>(w * static_cast<double>(f2));
                std::array<float, 3> next_c = {{static_cast<float>(static_cast<double>(o0) + core::at(p1, 0)),
                                   static_cast<float>(static_cast<double>(o1) + core::at(p1, 1)),
                                   static_cast<float>(w * static_cast<double>(f2) + core::at(p1, 2))}};
                std::array<float, 3> next_d = {{static_cast<float>(static_cast<double>(core::at(p1, 0)) - o0),
                                   static_cast<float>(static_cast<double>(core::at(p1, 1)) - o1),
                                   static_cast<float>(static_cast<double>(core::at(p1, 2)) - o2)}};
                vertex[0] = {.x = core::at(previous_a, 0), .y = core::at(previous_a, 1), .z = core::at(previous_a, 2), .colour = previous_colour};
                vertex[1] = {.x = core::at(previous_b, 0), .y = core::at(previous_b, 1), .z = core::at(previous_b, 2), .colour = previous_colour};
                vertex[2] = {.x = core::at(next_c, 0), .y = core::at(next_c, 1), .z = core::at(next_c, 2), .colour = colour};
                vertex[3] = {.x = core::at(previous_b, 0), .y = core::at(previous_b, 1), .z = core::at(previous_b, 2), .colour = previous_colour};
                vertex[4] = {.x = core::at(next_d, 0), .y = core::at(next_d, 1), .z = core::at(next_d, 2), .colour = colour};
                vertex[kSixthVertex] = {.x = core::at(next_c, 0), .y = core::at(next_c, 1), .z = core::at(next_c, 2), .colour = colour};
                vertex += kGlowRibbonVerticesPerSegment;
                std::memcpy(previous_a.data(), next_c.data(), sizeof(next_c));
                std::memcpy(previous_b.data(), next_d.data(), sizeof(next_d));
                std::memcpy(p0.data(), p1.data(), sizeof(p1));
                previous_colour = colour;
            }

            // The glow about the head: its colours from the colour fraction gone through the selection again.
            std::copy_n(p.rest().begin(), p0.size(), p0.begin());
            const std::array<float, 3> rr = {{static_cast<float>(static_cast<double>(core::at(r, 0)) * size0),
                                 static_cast<float>(static_cast<double>(core::at(r, 1)) * size0),
                                 static_cast<float>(static_cast<double>(core::at(r, 2)) * size0)}};
            const std::array<float, 3> uu = {{static_cast<float>(static_cast<double>(core::at(u, 0)) * size1),
                                 static_cast<float>(static_cast<double>(core::at(u, 1)) * size1),
                                 static_cast<float>(static_cast<double>(core::at(u, 2)) * size1)}};
            core::Cursor<const uint32_t> glow_from(start_colour());
            core::Cursor<const uint32_t> glow_to(end_colour());
            if (colour_mid() >= 0.0f) {
                if (t < colour_mid()) {
                    t = t / colour_mid();
                    glow_to = core::Cursor<const uint32_t>(mid_colour());
                } else {
                    t = (t - colour_mid()) / (1.0f - static_cast<double>(colour_mid()));
                    glow_from = core::Cursor<const uint32_t>(mid_colour());
                }
            }
            std::array<uint32_t, 3> glow{};
            for (int k = 0; k < 3; ++k)
                core::at(glow, k) = fine_colour(glow_from[k], glow_to[k], t);
            std::array<float, 3> outer{};
            std::array<float, 3> inner{};
            {
                const double r0 = static_cast<double>(core::at(rr, 0)) * 0.0f;
                const double r1 = static_cast<double>(core::at(rr, 1)) * 0.0f;
                const double r2 = static_cast<double>(core::at(rr, 2)) * 0.0f;
                const double x =
                    static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(r0)) * w)) + core::at(p0, 0);
                core::at(outer, 1) = static_cast<float>(static_cast<double>(static_cast<float>(w * r1)) + core::at(p0, 1));
                core::at(outer, 2) = static_cast<float>(w * r2 + core::at(p0, 2));
                core::at(outer, 0) = static_cast<float>(x + static_cast<float>(static_cast<double>(core::at(uu, 0)) * w));
                core::at(outer, 1) = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(core::at(uu, 1)) * w)) +
                                              core::at(outer, 1));
                core::at(outer, 2) = static_cast<float>(static_cast<double>(static_cast<float>(w * core::at(uu, 2))) + core::at(outer, 2));
                core::at(inner, 0) = static_cast<float>(static_cast<double>(core::at(p0, 0)) + static_cast<float>(r0));
                core::at(inner, 1) = static_cast<float>(r1 + core::at(p0, 1));
                const double z = static_cast<double>(static_cast<float>(r2)) + core::at(p0, 2);
                core::at(inner, 0) = static_cast<float>(static_cast<double>(core::at(uu, 0)) + core::at(inner, 0));
                core::at(inner, 1) = static_cast<float>(static_cast<double>(core::at(uu, 1)) + core::at(inner, 1));
                core::at(inner, 2) = static_cast<float>(static_cast<double>(core::at(uu, 2)) + z);
            }
            for (int i = 1; i <= segments(); ++i) {
                const double angle = static_cast<double>(i) * glow_step_ * 0.0174532924f;
                auto s = static_cast<float>(std::sin(angle));
                auto cs = static_cast<float>(std::cos(angle));
                const double r0s = static_cast<double>(core::at(rr, 0)) * s;
                const double r1s = static_cast<double>(s) * core::at(rr, 1);
                const double r2s = static_cast<double>(s) * core::at(rr, 2);
                std::array<float, 3> next_outer{};
                std::array<float, 3> next_inner{};
                const double x =
                    static_cast<double>(core::at(p0, 0)) + static_cast<float>(static_cast<double>(static_cast<float>(r0s)) * w);
                core::at(next_outer, 1) = static_cast<float>(static_cast<double>(static_cast<float>(r1s * w)) + core::at(p0, 1));
                core::at(next_outer, 2) = static_cast<float>(w * r2s + core::at(p0, 2));
                const double u0c = static_cast<double>(core::at(uu, 0)) * cs;
                const double u1c = static_cast<double>(cs) * core::at(uu, 1);
                auto u2c = static_cast<float>(static_cast<double>(cs) * core::at(uu, 2));
                core::at(next_outer, 0) = static_cast<float>(x + static_cast<float>(u0c * w));
                core::at(next_outer, 1) = static_cast<float>(static_cast<double>(static_cast<float>(u1c * w)) + core::at(next_outer, 1));
                core::at(next_outer, 2) = static_cast<float>(
                    static_cast<double>(static_cast<float>(static_cast<double>(u2c) * w)) + core::at(next_outer, 2));
                auto ix = static_cast<float>(static_cast<double>(core::at(p0, 0)) + static_cast<float>(r0s));
                auto iy = static_cast<float>(r1s + core::at(p0, 1));
                const double iz = static_cast<double>(static_cast<float>(r2s)) + core::at(p0, 2);
                core::at(next_inner, 0) = static_cast<float>(static_cast<double>(static_cast<float>(u0c)) + ix);
                core::at(next_inner, 1) = static_cast<float>(static_cast<double>(static_cast<float>(u1c)) + iy);
                core::at(next_inner, 2) = static_cast<float>(static_cast<double>(u2c) + iz);
                vertex[0] = {.x = core::at(p0, 0), .y = core::at(p0, 1), .z = core::at(p0, 2), .colour = core::at(glow, 0)};
                vertex[1] = {.x = core::at(outer, 0), .y = core::at(outer, 1), .z = core::at(outer, 2), .colour = core::at(glow, 1)};
                vertex[2] = {.x = core::at(next_outer, 0), .y = core::at(next_outer, 1), .z = core::at(next_outer, 2), .colour = core::at(glow, 1)};
                vertex[3] = {.x = core::at(outer, 0), .y = core::at(outer, 1), .z = core::at(outer, 2), .colour = core::at(glow, 1)};
                vertex[4] = {.x = core::at(next_outer, 0), .y = core::at(next_outer, 1), .z = core::at(next_outer, 2), .colour = core::at(glow, 1)};
                vertex[kSixthVertex] = {.x = core::at(inner, 0), .y = core::at(inner, 1), .z = core::at(inner, 2), .colour = core::at(glow, 2)};
                vertex[kSeventhVertex] = {.x = core::at(next_outer, 0), .y = core::at(next_outer, 1), .z = core::at(next_outer, 2), .colour = core::at(glow, 1)};
                vertex[kEighthVertex] = {.x = core::at(inner, 0), .y = core::at(inner, 1), .z = core::at(inner, 2), .colour = core::at(glow, 2)};
                vertex[kNinthVertex] = {.x = core::at(next_inner, 0), .y = core::at(next_inner, 1), .z = core::at(next_inner, 2), .colour = core::at(glow, 2)};
                vertex += kGlowRibbonEndVertices;
                std::memcpy(outer.data(), next_outer.data(), sizeof(outer));
                std::memcpy(inner.data(), next_inner.data(), sizeof(inner));
            }
            remaining -= per_particle;
        }
    }
    renderer().d3d_device()->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    os_layer::unlock_dynamic_vertices(total_vertices, sizeof(TrailVertex));
    renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(TrailVertex));
    renderer().d3d_device()->DrawPrimitive(D3DPT_TRIANGLELIST, first_vertex, total_triangles);
    renderer().d3d_device()->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    renderer().count_draw(total_triangles);
}

} // namespace render
