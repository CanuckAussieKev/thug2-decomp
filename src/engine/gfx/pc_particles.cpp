// The PC particle systems: see pc_particles.h.

#include "engine/gfx/pc_particles.h"

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>

#include "engine/core/cursor.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_flexible_particles.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "engine/gfx/texture_dict.h"
#include "platform/platform_d3d.h"
#include "platform/stopwatch.h"
#include "engine/gfx/shader_slots.h"
#include "platform/crt.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_texture.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"
#include "engine/maths/vec4.h"
#include "engine/core/raw_view.h"

namespace render {

constexpr uint32_t kParticleSeedB = 0x12B9B0A1;
constexpr uint32_t kParticleSeedC = 0x0AA2FB3F;
constexpr uint32_t kBrightnessBias = 0x10101010U;
constexpr float kPercentToFraction = 0.01F;
constexpr float kBurstRateScale = 0.75F;
constexpr int kBurstCapacity = 5;
constexpr std::size_t kBurstArrayBytes = 0x8C;
constexpr int kParticlePassFlags = 0x44;
constexpr float kParticleGrey = 0.5F;
constexpr float kAlphaCutOffScale = 128.0F;
constexpr float kByteMaxFloat = 255.0F;
constexpr int kByteMask = 0xFF;
constexpr int kMaxRoom = 0x1000;

namespace {
// NOT ORIGINAL grouping - pc_particles.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_particles_state()).
struct PcParticlesState {
    uint32_t particle_random_b_{kParticleSeedB};
    uint32_t particle_random_c_{kParticleSeedC};
    uint32_t particle_random_a_{};
};

PcParticlesState& pc_particles_state()
{
    static core::NoDestroy<PcParticlesState> state;
    return state.get();
}

constexpr float kRandomScale = 1.0F / 16384.0F; // 0x006487C8

int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The materials' code (0x004D1A90-0x004D3390).
gsl::owner<MaterialRef*> construct_material(void* memory) { return new (memory) MaterialRef; }
void destroy_material(MaterialRef* material) { material->~MaterialRef(); }
void set_material_states(MaterialRef* material) { material->submit_states(); }
void change_material_layer(MaterialRef* material, int pass)
{
    material->submit_pass(pass, nullptr); // (the original passed 0 as the colour: not read)
}
// The skater's texture dictionary's texture by checksum, its key (+0x10, through one more pointer with flag 1 of +0x08).
const BitmapTexture* find_texture(uint32_t checksum) { return engine_parts().particle_texture_dict()->fetch_skin(checksum); }

// The random sequence the particles' spreads come from (three words; four numbers a particle).
uint32_t& random_a() { return pc_particles_state().particle_random_a_; }
uint32_t& random_b() { return pc_particles_state().particle_random_b_; }
uint32_t& random_c() { return pc_particles_state().particle_random_c_; }
// A step: a' = b a + c, b' = (b ^ a') + (a' >> 4), c' = c + (a' >> 3) - 0x10101010 (arithmetic shifts).
void random_step(uint32_t& a, uint32_t& b, uint32_t& seed_c)
{
    a = (b * a) + seed_c;
    b = (b ^ a) + static_cast<uint32_t>(static_cast<int32_t>(a) >> 4);
    seed_c = seed_c + static_cast<uint32_t>(static_cast<int32_t>(a) >> 3) - kBrightnessBias;
}
// The number a step makes: 1 to 2 (a's low 16 bits, their top 14).
double random_scale(uint32_t a)
{
    const int value = static_cast<int32_t>((a & 0xFFFF) << 14) >> 16;
    return (static_cast<double>(value) * kRandomScale) + 1.0F;
}

ParticleBurst* get_next(const PcParticleSystem* system, ParticleBurst* burst)
{
    const std::span<ParticleBurst> ring = system->burst_ring();
    const auto after = static_cast<std::size_t>(burst - ring.data()) + 1;
    return &core::at(ring, after == ring.size() ? 0 : after);
}

// A quadratic through three points (x, y, z, w) at 0, `mid` and `end` (or a straight line from the first to the
// last), with the original's roundings. Two copies of it (the spread's and the path's) keep different products
// unrounded; `spread_rounding` picks the spread's.
struct Fit {
    double bx; // (unrounded: the path uses it so)
    float by, bz, bw;
    std::array<float, 4> c;
};
Fit get_fit(std::span<const float, 4> p0, std::span<const float, 4> p1, std::span<const float, 4> p2, float end, float mid, bool use_mid, bool spread_rounding)
{
    Fit fit{};
    const double dx2 = static_cast<double>(core::at(p2, 0)) - core::at(p0, 0);
    const double dy2 = static_cast<double>(core::at(p2, 1)) - core::at(p0, 1);
    auto z2 = static_cast<float>(static_cast<double>(core::at(p2, 2)) - core::at(p0, 2));
    auto w2 = static_cast<float>(static_cast<double>(core::at(p2, 3)) - core::at(p0, 3));
    if (!use_mid) {
        auto inverse = static_cast<float>(1.0F / static_cast<double>(end));
        fit.bx = dx2 * inverse;
        fit.by = static_cast<float>(static_cast<double>(static_cast<float>(dy2)) * inverse);
        fit.bz = static_cast<float>(static_cast<double>(z2) * inverse);
        fit.bw = static_cast<float>(static_cast<double>(w2) * inverse);
        return fit;
    }
    auto dx2f = static_cast<float>(dx2);
    auto dy2f = static_cast<float>(dy2);
    auto mid2 = static_cast<float>(static_cast<double>(mid) * mid);
    auto dx2_mid2 = static_cast<float>(dx2 * mid2);
    const double dy2_mid2 = dy2 * mid2;
    const double z2_mid2 = static_cast<double>(z2) * mid2;
    auto w2_mid2 = static_cast<float>(static_cast<double>(w2) * mid2);
    const double dx1 = static_cast<double>(core::at(p1, 0)) - core::at(p0, 0);
    const double dy1 = static_cast<double>(core::at(p1, 1)) - core::at(p0, 1);
    auto z1 = static_cast<float>(static_cast<double>(core::at(p1, 2)) - core::at(p0, 2));
    auto w1 = static_cast<float>(static_cast<double>(core::at(p1, 3)) - core::at(p0, 3));
    auto dx1f = static_cast<float>(dx1);
    auto dy1f = static_cast<float>(dy1);
    auto end2 = static_cast<float>(static_cast<double>(end) * end);
    auto dx1_end2 = static_cast<float>(dx1 * end2);
    const double dy1_end2 = dy1 * end2;
    auto z1_end2 = static_cast<float>(static_cast<double>(z1) * end2);
    auto w1_end2 = static_cast<float>(static_cast<double>(w1) * end2);
    auto ax = static_cast<float>(static_cast<double>(dx1_end2) - dx2_mid2);
    auto ay = static_cast<float>(dy1_end2 - dy2_mid2);
    const double az = static_cast<double>(z1_end2) - z2_mid2;
    auto aw = static_cast<float>(static_cast<double>(w1_end2) - w2_mid2);
    auto inverse = static_cast<float>(1.0F / ((static_cast<double>(end) - mid) * end * mid));
    fit.bx = static_cast<double>(ax) * inverse;
    fit.by = static_cast<float>(static_cast<double>(ay) * inverse);
    fit.bz = static_cast<float>(az * inverse);
    fit.bw = static_cast<float>(static_cast<double>(aw) * inverse);
    auto dx1_end = static_cast<float>(static_cast<double>(dx1f) * end);
    const double dy1_end = static_cast<double>(dy1f) * end;
    const double z1_end = static_cast<double>(z1) * end;
    auto w1_end = static_cast<float>(static_cast<double>(w1) * end);
    auto dx2_mid = static_cast<float>(static_cast<double>(dx2f) * mid);
    auto w2_mid = static_cast<float>(static_cast<double>(w2) * mid);
    const auto cx = static_cast<float>(static_cast<double>(dx2_mid) - dx1_end);
    float cy = 0.0F;
    double cz = 0.0;
    if (spread_rounding) {
        cy = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dy2f) * mid)) - dy1_end);
        cz = (static_cast<double>(z2) * mid) - z1_end;
    } else {
        cy = static_cast<float>((static_cast<double>(dy2f) * mid) - dy1_end);
        cz = static_cast<double>(static_cast<float>(static_cast<double>(z2) * mid)) - z1_end;
    }
    auto cw = static_cast<float>(static_cast<double>(w2_mid) - w1_end);
    core::at(fit.c, 0) = static_cast<float>(static_cast<double>(cx) * inverse);
    core::at(fit.c, 1) = static_cast<float>(static_cast<double>(cy) * inverse);
    core::at(fit.c, 2) = static_cast<float>(cz * inverse);
    core::at(fit.c, 3) = static_cast<float>(static_cast<double>(cw) * inverse);
    return fit;
}

float get_mid_time(const PcParticleSystem* system)
{
    return static_cast<float>(static_cast<double>(system->args().mid_percent()) * system->args().lifetime() * kPercentToFraction);
}

// The camera's across and up directions from the view matrix's third column (a, b, c): r = (a, b, c) x (0, 1, 0)
// and u = r x (a, b, c), each normalised when it has a length.
void camera_axes(std::span<float, 3> right, std::span<float, 3> u)
{
    const double row0_z = core::at(renderer().view_matrix().row, 0).z;
    const double row1_z = core::at(renderer().view_matrix().row, 1).z;
    const double row2_z = core::at(renderer().view_matrix().row, 2).z;
    const double b0 = row1_z * 0.0F;
    core::at(right, 0) = static_cast<float>(b0 - row2_z);
    core::at(right, 1) = static_cast<float>((row2_z * 0.0F) - (row0_z * 0.0F));
    core::at(right, 2) = static_cast<float>(row0_z - b0);
    core::at(u, 0) = static_cast<float>((core::at(right, 1) * row2_z) - (core::at(right, 2) * row1_z));
    core::at(u, 1) = static_cast<float>((core::at(right, 2) * row0_z) - (row2_z * core::at(right, 0)));
    core::at(u, 2) = static_cast<float>((row1_z * core::at(right, 0)) - (core::at(right, 1) * row0_z));
    for (const std::span<float, 3> w : {right, u}) {
        const double length = std::sqrt((static_cast<double>(core::at(w, 0)) * core::at(w, 0)) +
                                        (static_cast<double>(core::at(w, 2)) * core::at(w, 2)) +
                                        (static_cast<double>(core::at(w, 1)) * core::at(w, 1)));
        if (length > 0.0F) {
            const double inverse = 1.0F / length;
            for (int i = 0; i < 3; ++i) {
                core::at(w, i) = static_cast<float>(core::at(w, i) * inverse);
            }
        }
    }
}

// A particle's quad vertex: position, colour (ARGB), texture coordinates.
struct ParticleVertex {
    float x, y, z;
    uint32_t colour;
    float u, v;
};
constexpr std::size_t kParticleVertexBytes = 0x18;

static_assert(sizeof(ParticleVertex) == kParticleVertexBytes, "layout must match the original");

} // namespace

// 0x004BB040
void get_skip(ParticleBurst& burst, int particles)
{
    uint32_t seed_a = core::at(burst.seed, 0);
    uint32_t seed_b = core::at(burst.seed, 1);
    uint32_t seed_c = core::at(burst.seed, 2);
    random_a() = seed_a;
    random_b() = seed_b;
    random_c() = seed_c;
    int steps = particles * 4;
    if (steps > 0) {
        for (; steps != 0; --steps) {
            random_step(seed_a, seed_b, seed_c);
        }
        random_c() = seed_c;
        random_b() = seed_b;
        random_a() = seed_a;
    }
    core::at(burst.seed, 2) = seed_c;
    core::at(burst.seed, 0) = seed_a;
    core::at(burst.seed, 1) = seed_b;
}

// 0x004BCE90
gsl::owner<Sparkle*> PcParticleManager::create_system() { return new PcParticleSystem; }

// 0x004BC250
void PcParticleSystem::initialise()
{
    args().rate() = static_cast<float>(static_cast<double>(args().rate()) * kBurstRateScale); // (0x0064A4EC)
    capacity = kBurstCapacity;
    live = 0;
    bursts = static_cast<ParticleBurst*>(memory::alloc(kBurstArrayBytes));
    newest = &core::at(burst_ring(), capacity - 1);
    oldest = bursts;
    emitting = false;

    void* memory = memory::alloc(sizeof(MaterialRef));
    material = memory ? construct_material(memory) : nullptr;
    raw::zero_object(material);
    core::at(material->pass_flags(), 0) = kParticlePassFlags; // (its flags)
    material->set_pass_count(1);       // (a pass)
    material->set_alpha_ref(1);
    material->set_two_sided(1);
    for (int k = 0; k < 3; ++k) {
        core::at(core::at(material->pass_color(), 0), k) = kParticleGrey;
    }
    core::at(core::at(material->pass_color(), 0), 3) =
        static_cast<float>(static_cast<double>(args().alpha_cut_off()) * (1.0F / kAlphaCutOffScale));
    core::at(material->address_modes(), 0) = kClampBothWays;
    core::at(material->unknownc0(), 0) = kClampBothWays;
    core::at(material->blend_modes(), 0) = blend_mode_from_name(args().blend_mode());
    core::at(material->textures(), 0) = nullptr;
    if (const BitmapTexture* found = find_texture(args().texture())) {
        core::at(material->textures(), 0) = image_of(found);
    }

    // The spread: the boxes and their radii.
    std::array<float, 4> start = {{core::at(args().box(), 0).x, core::at(args().box(), 0).y, core::at(args().box(), 0).z, core::at(args().radius(), 0)}};
    std::array<float, 4> middle = {{core::at(args().box(), 1).x, core::at(args().box(), 1).y, core::at(args().box(), 1).z, core::at(args().radius(), 1)}};
    std::array<float, 4> last = {{core::at(args().box(), 2).x, core::at(args().box(), 2).y, core::at(args().box(), 2).z, core::at(args().radius(), 2)}};
    const Fit fit = get_fit(start, middle, last, args().lifetime(), get_mid_time(this), args().use_mid(), true);
    spread.a = {.x = core::at(start, 0), .y = core::at(start, 1), .z = core::at(start, 2), .w = core::at(start, 3)};
    spread.b = {.x = static_cast<float>(fit.bx), .y = fit.by, .z = fit.bz, .w = fit.bw};
    spread.c = {.x = core::at(fit.c, 0), .y = core::at(fit.c, 1), .z = core::at(fit.c, 2), .w = core::at(fit.c, 3)};
    // The path (the original works it out here as well, then again in update_path to the same values).
    update_path();
    std::memset(&matrix, 0, sizeof(matrix));
    core::at(matrix.row, 0).x = core::at(matrix.row, 1).y = core::at(matrix.row, 2).z = core::at(matrix.row, 3).w =
        1.0F;
}

// 0x004BBD20
void PcParticleSystem::update_path()
{
    std::array<float, 4> start = {{core::at(args().at_point(), 0).x, core::at(args().at_point(), 0).y, core::at(args().at_point(), 0).z, core::at(args().extent(), 0)}};
    std::array<float, 4> middle = {{core::at(args().at_point(), 1).x, core::at(args().at_point(), 1).y, core::at(args().at_point(), 1).z, core::at(args().extent(), 1)}};
    std::array<float, 4> last = {{core::at(args().at_point(), 2).x, core::at(args().at_point(), 2).y, core::at(args().at_point(), 2).z, core::at(args().extent(), 2)}};
    const Fit fit = get_fit(start, middle, last, args().lifetime(), get_mid_time(this), args().use_mid(), false);
    // Less 1.5 times the spread: its x and w products unrounded, its y and z rounded first.
    // (1.5 spreads, rounded to a float on the axes the original stored it for)
    auto offset_of = [](float spread_value, bool rounded) {
        const double offset = static_cast<double>(spread_value) * 1.5F;
        return rounded ? static_cast<double>(static_cast<float>(offset)) : offset;
    };
    path.a = {.x = static_cast<float>(core::at(start, 0) - offset_of(spread.a.x, false)),
              .y = static_cast<float>(core::at(start, 1) - offset_of(spread.a.y, true)),
              .z = static_cast<float>(core::at(start, 2) - offset_of(spread.a.z, true)),
              .w = static_cast<float>(core::at(start, 3) - offset_of(spread.a.w, false))};
    path.b = {.x = static_cast<float>(fit.bx - offset_of(spread.b.x, false)),
              .y = static_cast<float>(fit.by - offset_of(spread.b.y, true)),
              .z = static_cast<float>(fit.bz - offset_of(spread.b.z, true)),
              .w = static_cast<float>(fit.bw - offset_of(spread.b.w, false))};
    path.c = {.x = static_cast<float>(core::at(fit.c, 0) - offset_of(spread.c.x, false)),
              .y = static_cast<float>(core::at(fit.c, 1) - offset_of(spread.c.y, true)),
              .z = static_cast<float>(core::at(fit.c, 2) - offset_of(spread.c.z, true)),
              .w = static_cast<float>(core::at(fit.c, 3) - offset_of(spread.c.w, false))};
}

// 0x004BC240
void PcParticleSystem::update()
{
    if (args().local_positions()) {
        update_path();
    }
}

// 0x004BB0B0
bool PcParticleSystem::is_emitting() { return live > 0; }

// 0x004BCE10
void PcParticleSystem::free_resources()
{
    if (bursts) {
        memory::Hub::dealloc(bursts);
    }
    if (MaterialRef* own = material) {
        destroy_material(own);
        memory::Hub::dealloc(own);
    }
}

// 0x004BB0C0
void PcParticleSystem::draw(bool advance)
{
    // A new burst when the rate changed (or stopping when it's 0).
    if (args().rate() == 0.0F || (emitting && args().rate() == newest->rate)) {
        emitting = !(args().rate() == 0.0F);
    } else if (live < capacity) {
        ++live;
        newest = get_next(this, newest);
        newest->rate = args().rate();
        newest->period = static_cast<float>(1.0F / static_cast<double>(args().rate()));
        newest->age = 0.0F;
        newest->count = 0;
        core::at(newest->seed, 0) = static_cast<uint32_t>(get_rand());
        core::at(newest->seed, 1) = kParticleSeedB;
        core::at(newest->seed, 2) = kParticleSeedC;
        emitting = true;
    } else {
        emitting = false;
    }
    if (live == 0) {
        return;
    }
    if (advance) {
        ParticleBurst* burst = oldest;
        for (int i = 0; i < live; ++i) {
            burst->age = static_cast<float>(frame_clock::get_slomo_frame_seconds() + burst->age);
            burst = get_next(this, burst);
        }
    }
    if (emitting) {
        newest->count = truncate((static_cast<double>(newest->age) * newest->rate) + 1.0F);
    }
    ParticleBurst* first = oldest;
    if (first->age > args().lifetime()) {
        const int gone = truncate(((static_cast<double>(first->age) - args().lifetime()) * first->rate) + 1.0F);
        first->count -= gone;
        if (oldest->count <= 0 && (live != 1 || !emitting)) {
            --live;
            oldest = get_next(this, oldest);
            if (live == 0) {
                return;
            }
        } else {
            ParticleBurst* burst = oldest;
            burst->age = static_cast<float>(burst->age - (static_cast<double>(gone) * burst->period));
            get_skip(*oldest, gone);
        }
    }
    if (!args().local_positions()) {
        const maths::Vec4 centre = {.x = bound_ball().x, .y = bound_ball().y, .z = bound_ball().z, .w = 0.0F};
        const bool visible = args().cull_outside() ? sphere_outside_view(&centre, bound_ball().w) != 0
                                                 : sphere_in_view(&centre, bound_ball().w);
        if (!visible) {
            return;
        }
    }

    set_material_states(material);
    change_material_layer(material, 0);
    std::array<float, 3> right{};
    std::array<float, 3> u{};
    camera_axes(right, u);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    set_pixel_shader(core::at(pixel_shaders(), kPixelShader964), nullptr);
    std::array<std::array<float, 4>, 3> colour_table{};
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 4; ++k) {
            core::at(core::at(colour_table, i), k) = static_cast<float>(
                static_cast<double>(core::at(core::at(args().shade(), i), k)) * (1.0F / kByteMaxFloat));
        }
    }

    const std::span<ParticleBurst> ring = burst_ring();
    auto place = static_cast<std::size_t>(oldest - ring.data());
    for (int particle = 0; particle < live; ++particle, ++place) {
        if (place == ring.size()) {
            place = 0;
        }
        const ParticleBurst* burst = &core::at(ring, place);
        auto colour_mid = static_cast<float>(static_cast<double>(args().colour_mid_percent()) * kPercentToFraction * args().lifetime());
        float age = burst->age;
        random_a() = core::at(burst->seed, 0);
        random_b() = core::at(burst->seed, 1);
        random_c() = core::at(burst->seed, 2);
        int remaining = burst->count;
        int index = 0;
        while (remaining != 0) {
            IDirect3DVertexBuffer9* buffer = nullptr;
            uint32_t first_vertex = 0;
            uint32_t room = 0;
            auto* locked = static_cast<ParticleVertex*>(
                os_layer::lock_dynamic_vertices(remaining * 4, sizeof(ParticleVertex), &buffer, {.first_vertex_ = &first_vertex, .room = &room}));
            core::Cursor<ParticleVertex> vertex(std::span<ParticleVertex>(locked, room));
            room >>= 2;
            room = std::min<uint32_t>(room, kMaxRoom);
            uint32_t quads = 0;
            if (index < burst->count) {
                double age_now = age;
                for (;;) {
                    auto t2 = static_cast<float>(age_now * age_now);
                    uint32_t seed_a = random_a();
                    uint32_t bb = random_b();
                    uint32_t seed_c = random_c();
                    random_step(seed_a, bb, seed_c);
                    const double s1 = random_scale(seed_a);
                    random_step(seed_a, bb, seed_c);
                    auto s2 = static_cast<float>(random_scale(seed_a));
                    random_step(seed_a, bb, seed_c);
                    const double s3 = random_scale(seed_a);
                    random_step(seed_a, bb, seed_c);
                    auto s4 = static_cast<float>(random_scale(seed_a));
                    random_a() = seed_a;
                    random_b() = bb;
                    random_c() = seed_c;

                    // The colour: start to end, or through the mid.
                    const std::array<float, 4>* from = &core::at(colour_table, 0);
                    const std::array<float, 4>* to = &core::at(colour_table, 2);
                    double fraction = 0.0;
                    if (args().colour_use_mid()) {
                        if (age_now > colour_mid) {
                            fraction = (age_now - colour_mid) / (static_cast<double>(args().lifetime()) - colour_mid);
                            from = &core::at(colour_table, 1);
                        } else {
                            fraction = age_now / colour_mid;
                            to = &core::at(colour_table, 1);
                        }
                    } else {
                        fraction = age_now / args().lifetime();
                    }
                    auto channel = [&](int component) {
                        return static_cast<uint32_t>(truncate(
                                   (((static_cast<double>(core::at(*to, component)) - core::at(*from, component)) *
                                     fraction) +
                                    core::at(*from, component)) *
                                   kByteMaxFloat)) &
                               kByteMask;
                    };
                    const uint32_t alpha = channel(3);
                    const uint32_t red = channel(0);
                    const uint32_t green = channel(1);
                    const uint32_t blue = channel(2);
                    const uint32_t colour = alpha << 24 | red << 16 | green << 8 | blue;

                    // The spread at t, times its random numbers.
                    auto spread_x = static_cast<float>(
                        static_cast<double>(static_cast<float>((age_now * spread.b.x) + spread.a.x)) +
                        (static_cast<double>(t2) * spread.c.x));
                    const double spread_y =
                        (age_now * spread.b.y) + spread.a.y + (t2 * static_cast<double>(spread.c.y));
                    const double spread_z =
                        static_cast<double>(static_cast<float>(
                            static_cast<double>(static_cast<float>(age_now * spread.b.z)) + spread.a.z)) +
                        static_cast<float>(t2 * static_cast<double>(spread.c.z));
                    auto spread_w = static_cast<float>(
                        static_cast<double>(static_cast<float>(
                            static_cast<double>(static_cast<float>(age_now * spread.b.w)) + spread.a.w)) +
                        static_cast<float>(t2 * static_cast<double>(spread.c.w)));
                    auto offset_x = static_cast<float>(spread_x * s1);
                    auto offset_y = static_cast<float>(spread_y * s2);
                    const double offset_z = spread_z * s3;
                    const double offset_w = static_cast<double>(spread_w) * s4;
                    // The path at t.
                    auto path_x =
                        static_cast<float>(static_cast<double>(static_cast<float>((age_now * path.b.x) + path.a.x)) +
                                           (static_cast<double>(t2) * path.c.x));
                    const double path_y = (age_now * path.b.y) + path.a.y + (t2 * static_cast<double>(path.c.y));
                    const double path_z = static_cast<double>(static_cast<float>(
                                              static_cast<double>(static_cast<float>(age_now * path.b.z)) + path.a.z)) +
                                          static_cast<float>(t2 * static_cast<double>(path.c.z));
                    auto path_w = static_cast<float>(
                        static_cast<double>(static_cast<float>(
                            static_cast<double>(static_cast<float>(age_now * path.b.w)) + path.a.w)) +
                        static_cast<float>(t2 * static_cast<double>(path.c.w)));
                    auto centre_x = static_cast<float>(static_cast<double>(path_x) + offset_x);
                    auto centre_y = static_cast<float>(path_y + offset_y);
                    auto centre_z = static_cast<float>(path_z + offset_z);
                    const double size = static_cast<double>(path_w) + offset_w;

                    // The quad: (centre - r - ... ) corners, its texture 0..1.
                    const double across_x = core::at(right, 0) * size;
                    auto across_y = static_cast<float>(core::at(right, 1) * size);
                    auto across_z = static_cast<float>(core::at(right, 2) * size);
                    auto up_x = static_cast<float>(core::at(u, 0) * size);
                    auto up_y = static_cast<float>(core::at(u, 1) * size);
                    auto up_z = static_cast<float>(core::at(u, 2) * size);
                    auto left_x = static_cast<float>(centre_x - across_x);
                    auto left_y = static_cast<float>(static_cast<double>(centre_y) - across_y);
                    auto left_z = static_cast<float>(static_cast<double>(centre_z) - across_z);
                    vertex[0] = {.x = static_cast<float>(static_cast<double>(up_x) + left_x),
                                 .y = static_cast<float>(static_cast<double>(up_y) + left_y),
                                 .z = static_cast<float>(static_cast<double>(up_z) + left_z),
                                 .colour = colour,
                                 .u = 0.0F,
                                 .v = 0.0F};
                    vertex[1] = {.x = static_cast<float>(static_cast<double>(centre_x) + up_x + across_x),
                                 .y = static_cast<float>(static_cast<double>(centre_y) + up_y + across_y),
                                 .z = static_cast<float>(static_cast<double>(centre_z) + up_z + across_z),
                                 .colour = colour,
                                 .u = 1.0F,
                                 .v = 0.0F};
                    vertex[2] = {.x = static_cast<float>(static_cast<double>(centre_x) + across_x - up_x),
                                 .y = static_cast<float>(static_cast<double>(centre_y) + across_y - up_y),
                                 .z = static_cast<float>(static_cast<double>(centre_z) + across_z - up_z),
                                 .colour = colour,
                                 .u = 1.0F,
                                 .v = 1.0F};
                    vertex[3] = {.x = static_cast<float>(static_cast<double>(left_x) - up_x),
                                 .y = static_cast<float>(static_cast<double>(left_y) - up_y),
                                 .z = static_cast<float>(static_cast<double>(left_z) - up_z),
                                 .colour = colour,
                                 .u = 0.0F,
                                 .v = 1.0F};
                    vertex += 4;
                    age_now = age_now - burst->period;
                    ++quads;
                    ++index;
                    if (quads >= room - 1 || index >= burst->count) {
                        break;
                    }
                }
                age = static_cast<float>(age_now);
            }
            remaining -= static_cast<int>(quads);
            os_layer::unlock_dynamic_vertices(quads * 4, sizeof(ParticleVertex));
            renderer().d3d_device()->SetStreamSource(0, buffer, 0, sizeof(ParticleVertex));
            renderer().d3d_device()->SetIndices(quad_index_buffer());
            renderer().d3d_device()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, quads * 4, 0,
                                               quads * 2);
        }
    }
}

} // namespace render
