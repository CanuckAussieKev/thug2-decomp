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
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/gfx/shader_slots.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"

namespace render {

uint32_t g_particle_random_b{0x12B9B0A1};
uint32_t g_particle_random_c{0xAA2FB3F};
uint32_t g_particle_random_a;

namespace {

extern void** const kSystemVtable;
constexpr float kRandomScale = 1.0f / 16384.0f; // 0x006487C8

IDirect3DDevice9* input_port() { return g_d3d_device; }
IDirect3DIndexBuffer9* quad_indices() { return g_quad_indices; }
int get_rand() { return crt::get_rand(); } // (the game's C runtime's)

// _ftol: truncated to a 64-bit integer, its low half.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The materials' code (0x004D1A90-0x004D3390).
uint8_t* construct_material(void* memory) { return reinterpret_cast<uint8_t*>(static_cast<MaterialRef*>(memory)->make_new()); }
void destroy_material(uint8_t* material) { reinterpret_cast<MaterialRef*>(material)->destroy_body(); }
void set_material_states(uint8_t* material) { reinterpret_cast<MaterialRef*>(material)->submit_states(); }
void change_material_layer(uint8_t* material, int pass, int flags)
{
    reinterpret_cast<MaterialRef*>(material)->submit_pass(pass, reinterpret_cast<const float*>(static_cast<intptr_t>(flags)));
}
// The skater's texture dictionary's texture by checksum, its key (+0x10, through one more pointer with flag 1 of +0x08).
const uint8_t* find_texture(uint32_t checksum)
{
    return reinterpret_cast<const uint8_t*>(static_cast<TextureSet*>(reinterpret_cast<void*>(g_particle_texture_dict))->fetch_skin(checksum));
}
uint32_t texture_key(const uint8_t* texture)
{
    const uint32_t* key = reinterpret_cast<const uint32_t*>(texture + 0x10);
    return texture[8] & 1 ? *reinterpret_cast<const uint32_t*>(*key) : *key;
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
    float z2 = static_cast<float>(static_cast<double>(p2[2]) - p0[2]);
    float w2 = static_cast<float>(static_cast<double>(p2[3]) - p0[3]);
    if (!use_mid) {
        float inverse = static_cast<float>(1.0f / static_cast<double>(end));
        fit.bx = dx2 * inverse;
        fit.by = static_cast<float>(static_cast<double>(static_cast<float>(dy2)) * inverse);
        fit.bz = static_cast<float>(static_cast<double>(z2) * inverse);
        fit.bw = static_cast<float>(static_cast<double>(w2) * inverse);
        return fit;
    }
    float dx2f = static_cast<float>(dx2), dy2f = static_cast<float>(dy2);
    float mid2 = static_cast<float>(static_cast<double>(mid) * mid);
    float dx2_mid2 = static_cast<float>(dx2 * mid2);
    double dy2_mid2 = dy2 * mid2;
    double z2_mid2 = static_cast<double>(z2) * mid2;
    float w2_mid2 = static_cast<float>(static_cast<double>(w2) * mid2);
    double dx1 = static_cast<double>(p1[0]) - p0[0];
    double dy1 = static_cast<double>(p1[1]) - p0[1];
    float z1 = static_cast<float>(static_cast<double>(p1[2]) - p0[2]);
    float w1 = static_cast<float>(static_cast<double>(p1[3]) - p0[3]);
    float dx1f = static_cast<float>(dx1), dy1f = static_cast<float>(dy1);
    float end2 = static_cast<float>(static_cast<double>(end) * end);
    float dx1_end2 = static_cast<float>(dx1 * end2);
    double dy1_end2 = dy1 * end2;
    float z1_end2 = static_cast<float>(static_cast<double>(z1) * end2);
    float w1_end2 = static_cast<float>(static_cast<double>(w1) * end2);
    float ax = static_cast<float>(static_cast<double>(dx1_end2) - dx2_mid2);
    float ay = static_cast<float>(dy1_end2 - dy2_mid2);
    double az = static_cast<double>(z1_end2) - z2_mid2;
    float aw = static_cast<float>(static_cast<double>(w1_end2) - w2_mid2);
    float inverse = static_cast<float>(1.0f / ((static_cast<double>(end) - mid) * end * mid));
    fit.bx = static_cast<double>(ax) * inverse;
    fit.by = static_cast<float>(static_cast<double>(ay) * inverse);
    fit.bz = static_cast<float>(az * inverse);
    fit.bw = static_cast<float>(static_cast<double>(aw) * inverse);
    float dx1_end = static_cast<float>(static_cast<double>(dx1f) * end);
    double dy1_end = static_cast<double>(dy1f) * end;
    double z1_end = static_cast<double>(z1) * end;
    float w1_end = static_cast<float>(static_cast<double>(w1) * end);
    float dx2_mid = static_cast<float>(static_cast<double>(dx2f) * mid);
    float w2_mid = static_cast<float>(static_cast<double>(w2) * mid);
    float cx = static_cast<float>(static_cast<double>(dx2_mid) - dx1_end), cy;
    double cz;
    if (spread_rounding) {
        cy = static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dy2f) * mid)) - dy1_end);
        cz = static_cast<double>(z2) * mid - z1_end;
    } else {
        cy = static_cast<float>(static_cast<double>(dy2f) * mid - dy1_end);
        cz = static_cast<double>(static_cast<float>(static_cast<double>(z2) * mid)) - z1_end;
    }
    float cw = static_cast<float>(static_cast<double>(w2_mid) - w1_end);
    fit.c[0] = static_cast<float>(static_cast<double>(cx) * inverse);
    fit.c[1] = static_cast<float>(static_cast<double>(cy) * inverse);
    fit.c[2] = static_cast<float>(cz * inverse);
    fit.c[3] = static_cast<float>(static_cast<double>(cw) * inverse);
    return fit;
}

float get_mid_time(const PcParticleSystem* system)
{
    return static_cast<float>(static_cast<double>(system->mid_percent_) * system->lifetime * 0.01f);
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
PcParticleSystem* __fastcall PcParticleSystem::fabricate(void*)
{
    void* memory = memory::alloc_zeroed(sizeof(PcParticleSystem));
    if (!memory)
        return nullptr;
    auto* system = reinterpret_cast<PcParticleSystem*>(static_cast<Sparkle*>(memory)->make_new());
    system->vtable = reinterpret_cast<void**>(kSystemVtable);
    return system;
}

// 0x004BCE80
void PcParticleSystem::destroy_body() { engine_destroy_body(); }

// 0x004BCE50
PcParticleSystem* PcParticleSystem::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004BC250
void PcParticleSystem::initialise()
{
    rate = static_cast<float>(static_cast<double>(rate) * 0.75f); // (0x0064A4EC)
    capacity = 5;
    live = 0;
    bursts = static_cast<ParticleBurst*>(memory::alloc(0x8C));
    newest = bursts + capacity - 1;
    oldest = bursts;
    emitting = false;

    void* memory = memory::alloc(0x110);
    material = memory ? construct_material(memory) : nullptr;
    std::memset(material, 0, 0x110);
    *reinterpret_cast<uint32_t*>(material + 0x20) = 0x44; // (its flags)
    *reinterpret_cast<uint32_t*>(material + 0x08) = 1;    // (a pass)
    material[0x10] = 1;
    material[0x0D] = 1;
    for (int k = 0; k < 3; ++k)
        *reinterpret_cast<float*>(material + 0x40 + 4 * k) = 0.5f;
    *reinterpret_cast<float*>(material + 0x4C) = static_cast<float>(static_cast<double>(alpha_cut_off_) * (1.0f / 128.0f));
    *reinterpret_cast<uint32_t*>(material + 0x90) = 0x10001;
    *reinterpret_cast<uint32_t*>(material + 0xC0) = 0x10001;
    *reinterpret_cast<uint32_t*>(material + 0x80) = blend_mode_from_name(blend_mode_);
    *reinterpret_cast<uint32_t*>(material + 0x30) = 0;
    if (const uint8_t* found = find_texture(texture))
        *reinterpret_cast<uint32_t*>(material + 0x30) = texture_key(found);

    // The spread: the boxes and their radii.
    float start[4] = {boxes[0].x, boxes[0].y, boxes[0].z, radii[0]};
    float middle[4] = {boxes[1].x, boxes[1].y, boxes[1].z, radii[1]};
    float last[4] = {boxes[2].x, boxes[2].y, boxes[2].z, radii[2]};
    Fit fit = get_fit(start, middle, last, lifetime, get_mid_time(this), use_mid_, true);
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
    float start[4] = {positions[0].x, positions[0].y, positions[0].z, sizes[0]};
    float middle[4] = {positions[1].x, positions[1].y, positions[1].z, sizes[1]};
    float last[4] = {positions[2].x, positions[2].y, positions[2].z, sizes[2]};
    Fit fit = get_fit(start, middle, last, lifetime, get_mid_time(this), use_mid_, false);
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
void PcParticleSystem::tick_frame()
{
    if (local_positions_)
        update_path();
}

// 0x004BB0B0
bool PcParticleSystem::is_emitting() { return live > 0; }

// 0x004BCE10
void PcParticleSystem::get_release()
{
    if (bursts)
        memory::Hub::get_pull()->dealloc(bursts);
    if (uint8_t* own = material) {
        destroy_material(own);
        memory::Hub::get_pull()->dealloc(own);
    }
}

// 0x004BB0C0
void PcParticleSystem::render(bool advance)
{
    // A new burst when the rate changed (or stopping when it's 0).
    if (rate == 0.0f || (emitting && rate == newest->rate)) {
        emitting = !(rate == 0.0f);
    } else if (live < capacity) {
        ++live;
        newest = get_next(this, newest);
        newest->rate = rate;
        newest->period = static_cast<float>(1.0f / static_cast<double>(rate));
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
    if (first->age > lifetime) {
        int gone = truncate((static_cast<double>(first->age) - lifetime) * first->rate + 1.0f);
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
    if (!local_positions_) {
        maths::Vec4 centre = {bounds.x, bounds.y, bounds.z, 0.0f};
        bool visible = cull_outside_ ? sphere_outside_view(&centre, bounds.w) != 0 : sphere_in_view(&centre, bounds.w);
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
            colour_table[i][k] = static_cast<float>(static_cast<double>(colours[i][k]) * (1.0f / 255.0f));

    ParticleBurst* burst = oldest;
    for (int b = 0; b < live; ++b, ++burst) {
        if (burst == bursts + capacity)
            burst = bursts;
        float colour_mid = static_cast<float>(static_cast<double>(colour_mid_percent_) * 0.01f * lifetime);
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
                    float t2 = static_cast<float>(t * t);
                    uint32_t a = random_a(), bb = random_b(), c = random_c();
                    random_step(a, bb, c);
                    double s1 = random_scale(a);
                    random_step(a, bb, c);
                    float s2 = static_cast<float>(random_scale(a));
                    random_step(a, bb, c);
                    double s3 = random_scale(a);
                    random_step(a, bb, c);
                    float s4 = static_cast<float>(random_scale(a));
                    random_a() = a;
                    random_b() = bb;
                    random_c() = c;

                    // The colour: start to end, or through the mid.
                    const float* from = colour_table[0];
                    const float* to = colour_table[2];
                    double fraction;
                    if (colour_use_mid_) {
                        if (t > colour_mid) {
                            fraction = (t - colour_mid) / (static_cast<double>(lifetime) - colour_mid);
                            from = colour_table[1];
                        } else {
                            fraction = t / colour_mid;
                            to = colour_table[1];
                        }
                    } else {
                        fraction = t / lifetime;
                    }
                    auto channel = [&](int k) {
                        return static_cast<uint32_t>(
                                   truncate(((static_cast<double>(to[k]) - from[k]) * fraction + from[k]) * 255.0f)) &
                               0xFF;
                    };
                    uint32_t alpha = channel(3), red = channel(0), green = channel(1), blue = channel(2);
                    uint32_t colour = alpha << 24 | red << 16 | green << 8 | blue;

                    // The spread at t, times its random numbers.
                    float spread_x = static_cast<float>(
                        static_cast<double>(static_cast<float>(t * spread.b.x + spread.a.x)) + static_cast<double>(t2) * spread.c.x);
                    double spread_y = t * spread.b.y + spread.a.y + t2 * static_cast<double>(spread.c.y);
                    double spread_z =
                        static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(t * spread.b.z)) + spread.a.z)) +
                        static_cast<float>(t2 * static_cast<double>(spread.c.z));
                    float spread_w = static_cast<float>(
                        static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(t * spread.b.w)) + spread.a.w)) +
                        static_cast<float>(t2 * static_cast<double>(spread.c.w)));
                    float offset_x = static_cast<float>(spread_x * s1);
                    float offset_y = static_cast<float>(spread_y * s2);
                    double offset_z = spread_z * s3;
                    double offset_w = static_cast<double>(spread_w) * s4;
                    // The path at t.
                    float path_x = static_cast<float>(
                        static_cast<double>(static_cast<float>(t * path.b.x + path.a.x)) + static_cast<double>(t2) * path.c.x);
                    double path_y = t * path.b.y + path.a.y + t2 * static_cast<double>(path.c.y);
                    double path_z =
                        static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(t * path.b.z)) + path.a.z)) +
                        static_cast<float>(t2 * static_cast<double>(path.c.z));
                    float path_w = static_cast<float>(
                        static_cast<double>(static_cast<float>(static_cast<double>(static_cast<float>(t * path.b.w)) + path.a.w)) +
                        static_cast<float>(t2 * static_cast<double>(path.c.w)));
                    float centre_x = static_cast<float>(static_cast<double>(path_x) + offset_x);
                    float centre_y = static_cast<float>(path_y + offset_y);
                    float centre_z = static_cast<float>(path_z + offset_z);
                    double size = static_cast<double>(path_w) + offset_w;

                    // The quad: (centre - r - ... ) corners, its texture 0..1.
                    double across_x = r[0] * size;
                    float across_y = static_cast<float>(r[1] * size), across_z = static_cast<float>(r[2] * size);
                    float up_x = static_cast<float>(u[0] * size), up_y = static_cast<float>(u[1] * size),
                          up_z = static_cast<float>(u[2] * size);
                    float left_x = static_cast<float>(centre_x - across_x);
                    float left_y = static_cast<float>(static_cast<double>(centre_y) - across_y);
                    float left_z = static_cast<float>(static_cast<double>(centre_z) - across_z);
                    vertex[0] = {static_cast<float>(static_cast<double>(up_x) + left_x),
                                 static_cast<float>(static_cast<double>(up_y) + left_y),
                                 static_cast<float>(static_cast<double>(up_z) + left_z), colour, 0.0f, 0.0f};
                    vertex[1] = {static_cast<float>(static_cast<double>(centre_x) + up_x + across_x),
                                 static_cast<float>(static_cast<double>(centre_y) + up_y + across_y),
                                 static_cast<float>(static_cast<double>(centre_z) + up_z + across_z), colour, 1.0f, 0.0f};
                    vertex[2] = {static_cast<float>(static_cast<double>(centre_x) + across_x - up_x),
                                 static_cast<float>(static_cast<double>(centre_y) + across_y - up_y),
                                 static_cast<float>(static_cast<double>(centre_z) + across_z - up_z), colour, 1.0f, 1.0f};
                    vertex[3] = {static_cast<float>(static_cast<double>(left_x) - up_x),
                                 static_cast<float>(static_cast<double>(left_y) - up_y),
                                 static_cast<float>(static_cast<double>(left_z) - up_z), colour, 0.0f, 1.0f};
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
            input_port()->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex), 0, quads * 4, 0, quads * 2);
        }
    }
}

namespace {
static VtableSlot g_system_vtable_slots[] = {&render::PcParticleSystem::teardown, &render::PcParticleSystem::initialise,
                &render::PcParticleSystem::get_release, stubs::ignore_argument, &render::PcParticleSystem::render,
                &render::PcParticleSystem::tick_frame, stubs::do_nothing, &render::PcParticleSystem::is_emitting};
void** const kSystemVtable = reinterpret_cast<void**>(g_system_vtable_slots);
} // namespace

} // namespace Nx
