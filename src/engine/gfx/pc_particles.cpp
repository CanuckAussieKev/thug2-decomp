// The PC particle systems: see pc_particles.h.

#include "engine/gfx/pc_particles.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_flexible_particles.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "engine/gfx/texture_dict.h"
#include "platform/platform_d3d.h"
#include "platform/stopwatch.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/core/owner.h"

namespace render {

static uint32_t g_particle_random_b{0x12B9B0A1};
static uint32_t g_particle_random_c{0xAA2FB3F};
static uint32_t g_particle_random_a;

namespace {

constexpr float kRandomScale = 1.0f / 16384.0f; // 0x006487C8

IDirect3DDevice9* input_port() { return g_d3d_device; }
IDirect3DIndexBuffer9* quad_indices() { return g_quad_indices; }
int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The materials' code (0x004D1A90-0x004D3390).
MaterialRef* construct_material(void* memory) { return new (memory) MaterialRef; }
void destroy_material(MaterialRef* material) { material->~MaterialRef(); }
void set_material_states(MaterialRef* material) { material->submit_states(); }
void change_material_layer(MaterialRef* material, int pass, int flags)
{
    material->submit_pass(pass, reinterpret_cast<const float*>(static_cast<intptr_t>(flags)));
}
// The skater's texture dictionary's texture by checksum, its key (+0x10, through one more pointer with flag 1 of +0x08).
const uint8_t* find_texture(uint32_t checksum)
{
    return reinterpret_cast<const uint8_t*>(g_particle_texture_dict->fetch_skin(checksum));
}
// (the texture entry's +0x10: its PC texture, or with flag 1 a pointer to it)
uint8_t* texture_key(const uint8_t* texture)
{
    auto* const* key = reinterpret_cast<uint8_t* const*>(texture + 0x10);
    return texture[8] & 1 ? *reinterpret_cast<uint8_t* const*>(*key) : *key;
}

// The random sequence the particles' spreads come from (three words; four numbers a particle).
uint32_t& random_a() { return g_particle_random_a; }
uint32_t& random_b() { return g_particle_random_b; }
uint32_t& random_c() { return g_particle_random_c; }
// A step: a' = b a + c, b' = (b ^ a') + (a' >> 4), c' = c + (a' >> 3) - 0x10101010 (arithmetic shifts).
void random_step(uint32_t& a, uint32_t& b, uint32_t& c)
{
    a = b * a + c;
    b = (b ^ a) + static_cast<uint32_t>(static_cast<int32_t>(a) >> 4);
    c = c + static_cast<uint32_t>(static_cast<int32_t>(a) >> 3) - 0x10101010u;
}
// The number a step makes: 1 to 2 (a's low 16 bits, their top 14).
double random_scale(uint32_t a)
{
    int value = static_cast<int32_t>((a & 0xFFFF) << 14) >> 16;
    return static_cast<double>(value) * kRandomScale + 1.0f;
}

ParticleBurst* get_next(const PcParticleSystem* system, ParticleBurst* burst)
{
    ++burst;
    return burst == system->bursts + system->capacity ? system->bursts : burst;
}

// A quadratic through three points (x, y, z, w) at 0, `mid` and `end` (or a straight line from the first to the
// last), with the original's roundings. Two copies of it (the spread's and the path's) keep different products
// unrounded; `spread_rounding` picks the spread's.
struct Fit {
    double bx; // (unrounded: the path uses it so)
    float by, bz, bw;
    float c[4];
};
Fit get_fit(const float* p0, const float* p1, const float* p2, float end, float mid, bool use_mid, bool spread_rounding)
{
    Fit fit{};
    double dx2 = static_cast<double>(p2[0]) - p0[0];
    double dy2 = static_cast<double>(p2[1]) - p0[1];
    auto z2 = static_cast<float>(static_cast<double>(p2[2]) - p0[2]);
    auto w2 = static_cast<float>(static_cast<double>(p2[3]) - p0[3]);
    if (!use_mid) {
        auto inverse = static_cast<float>(1.0f / static_cast<double>(end));
        fit.bx = dx2 * inverse;
        fit.by = static_cast<float>(static_cast<double>(static_cast<float>(dy2)) * inverse);
        fit.bz = static_cast<float>(static_cast<double>(z2) * inverse);
        fit.bw = static_cast<float>(static_cast<double>(w2) * inverse);
        return fit;
    }
    auto dx2f = static_cast<float>(dx2), dy2f = static_cast<float>(dy2);
    auto mid2 = static_cast<float>(static_cast<double>(mid) * mid);
    auto dx2_mid2 = static_cast<float>(dx2 * mid2);
    double dy2_mid2 = dy2 * mid2;
    double z2_mid2 = static_cast<double>(z2) * mid2;
    auto w2_mid2 = static_cast<float>(static_cast<double>(w2) * mid2);
    double dx1 = static_cast<double>(p1[0]) - p0[0];
    double dy1 = static_cast<double>(p1[1]) - p0[1];
    auto z1 = static_cast<float>(static_cast<double>(p1[2]) - p0[2]);
    auto w1 = static_cast<float>(static_cast<double>(p1[3]) - p0[3]);
    auto dx1f = static_cast<float>(dx1), dy1f = static_cast<float>(dy1);
    auto end2 = static_cast<float>(static_cast<double>(end) * end);
    auto dx1_end2 = static_cast<float>(dx1 * end2);
    double dy1_end2 = dy1 * end2;
    auto z1_end2 = static_cast<float>(static_cast<double>(z1) * end2);
    auto w1_end2 = static_cast<float>(static_cast<double>(w1) * end2);
    auto ax = static_cast<float>(static_cast<double>(dx1_end2) - dx2_mid2);
    auto ay = static_cast<float>(dy1_end2 - dy2_mid2);
    double az = static_cast<double>(z1_end2) - z2_mid2;
    auto aw = static_cast<float>(static_cast<double>(w1_end2) - w2_mid2);
    auto inverse = static_cast<float>(1.0f / ((static_cast<double>(end) - mid) * end * mid));
    fit.bx = static_cast<double>(ax) * inverse;
    fit.by = static_cast<float>(static_cast<double>(ay) * inverse);
    fit.bz = static_cast<float>(az * inverse);
    fit.bw = static_cast<float>(static_cast<double>(aw) * inverse);
    auto dx1_end = static_cast<float>(static_cast<double>(dx1f) * end);
    double dy1_end = static_cast<double>(dy1f) * end;
    double z1_end = static_cast<double>(z1) * end;
    auto w1_end = static_cast<float>(static_cast<double>(w1) * end);
    auto dx2_mid = static_cast<float>(static_cast<double>(dx2f) * mid);
    auto w2_mid = static_cast<float>(static_cast<double>(w2) * mid);
    float cx = static_cast<float>(static_cast<double>(dx2_mid) - dx1_end), cy;
    double cz;
    if (spread_rounding) {
        cy = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dy2f) * mid)) - dy1_end);
        cz = static_cast<double>(z2) * mid - z1_end;
    } else {
        cy = static_cast<float>(static_cast<double>(dy2f) * mid - dy1_end);
        cz = static_cast<double>(static_cast<float>(static_cast<double>(z2) * mid)) - z1_end;
    }
    auto cw = static_cast<float>(static_cast<double>(w2_mid) - w1_end);
    fit.c[0] = static_cast<float>(static_cast<double>(cx) * inverse);
    fit.c[1] = static_cast<float>(static_cast<double>(cy) * inverse);
    fit.c[2] = static_cast<float>(cz * inverse);
    fit.c[3] = static_cast<float>(static_cast<double>(cw) * inverse);
    return fit;
}

float get_mid_time(const PcParticleSystem* system)
{
    return static_cast<float>(static_cast<double>(system->args_.mid_percent_) * system->args_.lifetime_ * 0.01f);
}

// The camera's across and up directions from the view matrix's third column (a, b, c): r = (a, b, c) x (0, 1, 0)
// and u = r x (a, b, c), each normalised when it has a length.
void camera_axes(float r[3], float u[3])
{
    double a = g_view_matrix.row[0].z;
    double b = g_view_matrix.row[1].z;
    double c = g_view_matrix.row[2].z;
    double b0 = b * 0.0f;
    r[0] = static_cast<float>(b0 - c);
    r[1] = static_cast<float>(c * 0.0f - a * 0.0f);
    r[2] = static_cast<float>(a - b0);
    u[0] = static_cast<float>(r[1] * c - r[2] * b);
    u[1] = static_cast<float>(r[2] * a - c * r[0]);
    u[2] = static_cast<float>(b * r[0] - r[1] * a);
    for (float* w : {r, u}) {
        double length = std::sqrt(static_cast<double>(w[0]) * w[0] + static_cast<double>(w[2]) * w[2] +
                                  static_cast<double>(w[1]) * w[1]);
        if (length > 0.0f) {
            double inverse = 1.0f / length;
            for (int i = 0; i < 3; ++i)
                w[i] = static_cast<float>(w[i] * inverse);
        }
    }
}

// A particle's quad vertex: position, colour (ARGB), texture coordinates.
struct ParticleVertex {
    float x, y, z;
    uint32_t colour;
    float u, v;
};
static_assert(sizeof(ParticleVertex) == 0x18, "layout must match the original");

} // namespace

// 0x004BB040
void ParticleBurst::get_skip(int particles)
{
    uint32_t a = seed[0], b = seed[1], c = seed[2];
    random_a() = a;
    random_b() = b;
    random_c() = c;
    int steps = particles * 4;
    if (steps > 0) {
        for (; steps; --steps)
            random_step(a, b, c);
        random_c() = c;
        random_b() = b;
        random_a() = a;
    }
    seed[2] = c;
    seed[0] = a;
    seed[1] = b;
}

// 0x004BCE90
memory::owner<Sparkle*> PcParticleManager::create_system() { return new PcParticleSystem; }

// 0x004BC250
void PcParticleSystem::initialise()
{
    args_.rate_ = static_cast<float>(static_cast<double>(args_.rate_) * 0.75f); // (0x0064A4EC)
    capacity = 5;
    live = 0;
    bursts = static_cast<ParticleBurst*>(memory::alloc(0x8C));
    newest = bursts + capacity - 1;
    oldest = bursts;
    emitting = false;

    void* memory = memory::alloc(sizeof(MaterialRef));
    material = memory ? construct_material(memory) : nullptr;
    std::memset(material, 0, sizeof(MaterialRef));
    material->pass_flags_[0] = 0x44; // (its flags)
    material->pass_count_ = 1;       // (a pass)
    material->alpha_ref_ = 1;
    material->two_sided_ = 1;
    for (int k = 0; k < 3; ++k)
        material->pass_color_[0][k] = 0.5f;
    material->pass_color_[0][3] = static_cast<float>(static_cast<double>(args_.alpha_cut_off_) * (1.0f / 128.0f));
    material->address_modes_[0] = 0x10001;
    material->unknownc0_[0] = 0x10001;
    material->blend_modes_[0] = blend_mode_from_name(args_.blend_mode_);
    material->textures[0] = nullptr;
    if (const uint8_t* found = find_texture(args_.texture_))
        material->textures[0] = texture_key(found);

    // The spread: the boxes and their radii.
    float start[4] = {args_.box_[0].x, args_.box_[0].y, args_.box_[0].z, args_.radius_[0]};
    float middle[4] = {args_.box_[1].x, args_.box_[1].y, args_.box_[1].z, args_.radius_[1]};
    float last[4] = {args_.box_[2].x, args_.box_[2].y, args_.box_[2].z, args_.radius_[2]};
    Fit fit = get_fit(start, middle, last, args_.lifetime_, get_mid_time(this), args_.use_mid_, true);
    spread.a = {start[0], start[1], start[2], start[3]};
    spread.b = {static_cast<float>(fit.bx), fit.by, fit.bz, fit.bw};
    spread.c = {fit.c[0], fit.c[1], fit.c[2], fit.c[3]};
    // The path (the original works it out here as well, then again in update_path to the same values).
    update_path();
    std::memset(&matrix, 0, sizeof(matrix));
    matrix.row[0].x = matrix.row[1].y = matrix.row[2].z = matrix.row[3].w = 1.0f;
}

// 0x004BBD20
void PcParticleSystem::update_path()
{
    float start[4] = {args_.at_point_[0].x, args_.at_point_[0].y, args_.at_point_[0].z, args_.extent_[0]};
    float middle[4] = {args_.at_point_[1].x, args_.at_point_[1].y, args_.at_point_[1].z, args_.extent_[1]};
    float last[4] = {args_.at_point_[2].x, args_.at_point_[2].y, args_.at_point_[2].z, args_.extent_[2]};
    Fit fit = get_fit(start, middle, last, args_.lifetime_, get_mid_time(this), args_.use_mid_, false);
    // Less 1.5 times the spread: its x and w products unrounded, its y and z rounded first.
    auto less = [](double value, float spread_value, bool rounded) {
        double offset = static_cast<double>(spread_value) * 1.5f;
        return static_cast<float>(value - (rounded ? static_cast<double>(static_cast<float>(offset)) : offset));
    };
    path.a = {less(start[0], spread.a.x, false), less(start[1], spread.a.y, true), less(start[2], spread.a.z, true),
              less(start[3], spread.a.w, false)};
    path.b = {less(fit.bx, spread.b.x, false), less(fit.by, spread.b.y, true), less(fit.bz, spread.b.z, true),
              less(fit.bw, spread.b.w, false)};
    path.c = {less(fit.c[0], spread.c.x, false), less(fit.c[1], spread.c.y, true), less(fit.c[2], spread.c.z, true),
              less(fit.c[3], spread.c.w, false)};
}

// 0x004BC240
void PcParticleSystem::update()
{
    if (args_.local_positions_)
        update_path();
}

// 0x004BB0B0
bool PcParticleSystem::is_emitting() { return live > 0; }

// 0x004BCE10
void PcParticleSystem::free_resources()
{
    if (bursts)
        memory::Hub::get_pull()->dealloc(bursts);
    if (MaterialRef* own = material) {
        destroy_material(own);
        memory::Hub::get_pull()->dealloc(own);
    }
}

// 0x004BB0C0
void PcParticleSystem::draw(bool advance)
{
    // A new burst when the rate changed (or stopping when it's 0).
    if (args_.rate_ == 0.0f || (emitting && args_.rate_ == newest->rate)) {
        emitting = !(args_.rate_ == 0.0f);
    } else if (live < capacity) {
        ++live;
        newest = get_next(this, newest);
        newest->rate = args_.rate_;
        newest->period = static_cast<float>(1.0f / static_cast<double>(args_.rate_));
        newest->age = 0.0f;
        newest->count = 0;
        newest->seed[0] = static_cast<uint32_t>(get_rand());
        newest->seed[1] = 0x12B9B0A1;
        newest->seed[2] = 0x0AA2FB3F;
        emitting = true;
    } else {
        emitting = false;
    }
    if (live == 0)
        return;
    if (advance) {
        ParticleBurst* burst = oldest;
        for (int i = 0; i < live; ++i) {
            burst->age = static_cast<float>(frame_clock::get_slomo_frame_seconds() + burst->age);
            burst = get_next(this, burst);
        }
    }
    if (emitting)
        newest->count = truncate(static_cast<double>(newest->age) * newest->rate + 1.0f);
    ParticleBurst* first = oldest;
    if (first->age > args_.lifetime_) {
        int gone = truncate((static_cast<double>(first->age) - args_.lifetime_) * first->rate + 1.0f);
        first->count -= gone;
        if (oldest->count <= 0 && (live != 1 || !emitting)) {
            --live;
            oldest = get_next(this, oldest);
            if (live == 0)
                return;
        } else {
            ParticleBurst* burst = oldest;
            burst->age = static_cast<float>(burst->age - static_cast<double>(gone) * burst->period);
            oldest->get_skip(gone);
        }
    }
    if (!args_.local_positions_) {
        maths::Vec4 centre = {bound_ball_.x, bound_ball_.y, bound_ball_.z, 0.0f};
        bool visible = args_.cull_outside_ ? sphere_outside_view(&centre, bound_ball_.w) != 0
                                           : sphere_in_view(&centre, bound_ball_.w);
        if (!visible)
            return;
    }

    set_material_states(material);
    change_material_layer(material, 0, 0);
    float r[3], u[3];
    camera_axes(r, u);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    set_pixel_shader(g_pixel_shaders[kPixelShader964], nullptr);
    float colour_table[3][4];
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 4; ++k)
            colour_table[i][k] = static_cast<float>(static_cast<double>(args_.shade_[i][k]) * (1.0f / 255.0f));

    ParticleBurst* burst = oldest;
    for (int b = 0; b < live; ++b, ++burst) {
        if (burst == bursts + capacity)
            burst = bursts;
        auto colour_mid = static_cast<float>(static_cast<double>(args_.colour_mid_percent_) * 0.01f * args_.lifetime_);
        float age = burst->age;
        random_a() = burst->seed[0];
        random_b() = burst->seed[1];
        random_c() = burst->seed[2];
        int remaining = burst->count;
        int index = 0;
        while (remaining != 0) {
            IDirect3DVertexBuffer9* buffer = nullptr;
            uint32_t first_vertex = 0, room = 0;
            auto* vertex = static_cast<ParticleVertex*>(
                os_layer::lock_dynamic_vertices(remaining * 4, sizeof(ParticleVertex), &buffer, &first_vertex, &room));
            room >>= 2;
            if (room > 0x1000)
                room = 0x1000;
            uint32_t quads = 0;
            if (index < burst->count) {
                double t = age;
                for (;;) {
                    auto t2 = static_cast<float>(t * t);
                    uint32_t a = random_a(), bb = random_b(), c = random_c();
                    random_step(a, bb, c);
                    double s1 = random_scale(a);
                    random_step(a, bb, c);
                    auto s2 = static_cast<float>(random_scale(a));
                    random_step(a, bb, c);
                    double s3 = random_scale(a);
                    random_step(a, bb, c);
                    auto s4 = static_cast<float>(random_scale(a));
                    random_a() = a;
                    random_b() = bb;
                    random_c() = c;

                    // The colour: start to end, or through the mid.
                    const float* from = colour_table[0];
                    const float* to = colour_table[2];
                    double fraction;
                    if (args_.colour_use_mid_) {
                        if (t > colour_mid) {
                            fraction = (t - colour_mid) / (static_cast<double>(args_.lifetime_) - colour_mid);
                            from = colour_table[1];
                        } else {
                            fraction = t / colour_mid;
                            to = colour_table[1];
                        }
                    } else {
                        fraction = t / args_.lifetime_;
                    }
                    auto channel = [&](int k) {
                        return static_cast<uint32_t>(
                                   truncate(((static_cast<double>(to[k]) - from[k]) * fraction + from[k]) * 255.0f)) &
                               0xFF;
                    };
                    uint32_t alpha = channel(3), red = channel(0), green = channel(1), blue = channel(2);
                    uint32_t colour = alpha << 24 | red << 16 | green << 8 | blue;

                    // The spread at t, times its random numbers.
                    auto spread_x =
                        static_cast<float>(static_cast<double>(static_cast<float>(t * spread.b.x + spread.a.x)) +
                                           static_cast<double>(t2) * spread.c.x);
                    double spread_y = t * spread.b.y + spread.a.y + t2 * static_cast<double>(spread.c.y);
                    double spread_z = static_cast<double>(static_cast<float>(
                                          static_cast<double>(static_cast<float>(t * spread.b.z)) + spread.a.z)) +
                                      static_cast<float>(t2 * static_cast<double>(spread.c.z));
                    auto spread_w =
                        static_cast<float>(static_cast<double>(static_cast<float>(
                                               static_cast<double>(static_cast<float>(t * spread.b.w)) + spread.a.w)) +
                                           static_cast<float>(t2 * static_cast<double>(spread.c.w)));
                    auto offset_x = static_cast<float>(spread_x * s1);
                    auto offset_y = static_cast<float>(spread_y * s2);
                    double offset_z = spread_z * s3;
                    double offset_w = static_cast<double>(spread_w) * s4;
                    // The path at t.
                    auto path_x = static_cast<float>(static_cast<double>(static_cast<float>(t * path.b.x + path.a.x)) +
                                                     static_cast<double>(t2) * path.c.x);
                    double path_y = t * path.b.y + path.a.y + t2 * static_cast<double>(path.c.y);
                    double path_z = static_cast<double>(static_cast<float>(
                                        static_cast<double>(static_cast<float>(t * path.b.z)) + path.a.z)) +
                                    static_cast<float>(t2 * static_cast<double>(path.c.z));
                    auto path_w =
                        static_cast<float>(static_cast<double>(static_cast<float>(
                                               static_cast<double>(static_cast<float>(t * path.b.w)) + path.a.w)) +
                                           static_cast<float>(t2 * static_cast<double>(path.c.w)));
                    auto centre_x = static_cast<float>(static_cast<double>(path_x) + offset_x);
                    auto centre_y = static_cast<float>(path_y + offset_y);
                    auto centre_z = static_cast<float>(path_z + offset_z);
                    double size = static_cast<double>(path_w) + offset_w;

                    // The quad: (centre - r - ... ) corners, its texture 0..1.
                    double across_x = r[0] * size;
                    auto across_y = static_cast<float>(r[1] * size), across_z = static_cast<float>(r[2] * size);
                    auto up_x = static_cast<float>(u[0] * size), up_y = static_cast<float>(u[1] * size),
                         up_z = static_cast<float>(u[2] * size);
                    auto left_x = static_cast<float>(centre_x - across_x);
                    auto left_y = static_cast<float>(static_cast<double>(centre_y) - across_y);
                    auto left_z = static_cast<float>(static_cast<double>(centre_z) - across_z);
                    vertex[0] = {static_cast<float>(static_cast<double>(up_x) + left_x),
                                 static_cast<float>(static_cast<double>(up_y) + left_y),
                                 static_cast<float>(static_cast<double>(up_z) + left_z),
                                 colour,
                                 0.0f,
                                 0.0f};
                    vertex[1] = {static_cast<float>(static_cast<double>(centre_x) + up_x + across_x),
                                 static_cast<float>(static_cast<double>(centre_y) + up_y + across_y),
                                 static_cast<float>(static_cast<double>(centre_z) + up_z + across_z),
                                 colour,
                                 1.0f,
                                 0.0f};
                    vertex[2] = {static_cast<float>(static_cast<double>(centre_x) + across_x - up_x),
                                 static_cast<float>(static_cast<double>(centre_y) + across_y - up_y),
                                 static_cast<float>(static_cast<double>(centre_z) + across_z - up_z),
                                 colour,
                                 1.0f,
                                 1.0f};
                    vertex[3] = {static_cast<float>(static_cast<double>(left_x) - up_x),
                                 static_cast<float>(static_cast<double>(left_y) - up_y),
                                 static_cast<float>(static_cast<double>(left_z) - up_z),
                                 colour,
                                 0.0f,
                                 1.0f};
                    vertex += 4;
                    t = t - burst->period;
                    ++quads;
                    ++index;
                    if (quads >= room - 1 || index >= burst->count)
                        break;
                }
                age = static_cast<float>(t);
            }
            remaining -= static_cast<int>(quads);
            os_layer::unlock_dynamic_vertices(quads * 4, sizeof(ParticleVertex));
            input_port()->SetStreamSource(0, buffer, 0, sizeof(ParticleVertex));
            input_port()->SetIndices(quad_indices());
            input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, quads * 4, 0,
                                               quads * 2);
        }
    }
}

} // namespace render
