// The engine's effects: see engine_effects.h. The maths follows the original's x87 code (53-bit precision: double
// where it keeps values in registers, float where it stores them).

#include "engine/gfx/engine_effects.h"
#include "platform/d3dx.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <new>
#include <array>
#include <algorithm>
#include <span>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/flash.h"
#include "engine/gfx/pc_fog.h"
#include "engine/gfx/shatter.h"
#include "engine/gfx/spawned_polys.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/texture_splats.h"
#include "engine/lists/object_set.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/render/coll_grid.h"
#include "engine/net/buddy_status.h"
#include "engine/render/coll_obj.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/movie_manager.h"
#include "platform/crt.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/engine_texture.h"

namespace render {

maths::Vec4 g_glass_burst_velocity;
float g_shatter_speed;
lists::ObjectSet<TextureSplats> g_splat_sets;
lists::ObjectSet<SpawnedPoly> g_spawned_polys;
float g_shatter_velocity_scale{1.0f};
namespace {

float g_shatter_value_2e_1c{1.0f};
float g_shatter_value_c483_squared{360.0f};
int g_splat_reserve{128};
int g_splat_fade{8};
float g_fog_distance{3000.0f};
float g_fog_exponent{10.0f};
float g_glass_life_span{4.0f};
float g_shatter_floor{-10000.0f};
float g_glass_restitution{0.8f};
uint32_t g_fog_color{0x66806060};
uint32_t g_shatter_texture;
uint32_t g_shatter_value_af_1a;
int g_shatter_value_33d_f;
bool g_fog_active;
maths::Vec4 g_shatter_vector_210e;
std::array<float, 4> g_level_color;
std::array<void*, 128> g_line_sectors;
std::array<HitMesh*, 128> g_line_objects;

// A recent splat position (0x18 bytes, in 0x007004B4): where, the id it follows, when (frame_clock::GetTime).
struct RecentSplat {
    maths::Vec4 pos;
    uint32_t follow; // +0x10
    uint32_t time;   // +0x14
};

// The tables.
lists::ObjectSet<Flash> g_flashes;
lists::ObjectSet<RecentSplat> g_recent_splats;
lists::ObjectSet<Flash>& flashes() { return g_flashes; }
lists::ObjectSet<TextureSplats>& splat_sets() { return g_splat_sets; }
lists::ObjectSet<RecentSplat>& recent_splats() { return g_recent_splats; }
lists::ObjectSet<PcShatter>& shatters() { return g_shatters; }

bool get_paused() { return objects::g_simulation_halted; }

// The game's C runtime's rand() and its scales (0x0064930C: 2 / 32767, 0x00646510: 1 / 32767).
int get_rand() { return crt::get_rand(); }
constexpr double kPerRandTwice = std::bit_cast<float>(0x38800100u);
constexpr double kPerRand = std::bit_cast<float>(0x38000100u);

// The frame's time in milliseconds (truncated, the original's _ftol).
int frame_milliseconds()
{
    return static_cast<int>(static_cast<int64_t>(frame_clock::get_slomo_frame_seconds() * 1000.0f));
}

// The platform's fog exponent: compiled out on PC (the empty 0x005480A0).
void set_fog_density_curve(float /*exponent*/) {}
// Two compiled-out calls (the empty 0x005480A0) after the tables are made.
void compiled_out() {}


} // namespace

// 0x00490D80
void VertexStack::stack_off(uint8_t* out)
{
    used -= vertex_size_;
    std::memcpy(out, data.data() + used, vertex_size_);
}

// 0x00490E40
SplatParams::SplatParams()
{
    core::at(start, 0) = core::at(start, 1) = core::at(start, 2) = 0.0f;
    core::at(start, 3) = 1.0f;
    core::at(end, 0) = core::at(end, 1) = core::at(end, 2) = 0.0f;
    core::at(end, 3) = 1.0f;
}

// --- the engine's shatter ---

// 0x004912C0
Shatter::Shatter(int count)
    : lifetime(g_glass_life_span), floor(g_shatter_floor), bounce(g_glass_restitution),
      triangle_count_(static_cast<uint16_t>(count)), unknown28(g_shatter_value_af_1a)
{
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    centres = static_cast<maths::Vec4*>(memory::alloc(count << 4));
    velocities = static_cast<maths::Vec4*>(memory::alloc(count << 4));
    rotations = static_cast<maths::Mat4*>(memory::alloc(count << 6));
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x00490E80
Shatter::~Shatter()
{
    memory::Hub::get_pull()->dealloc(centres);
    memory::Hub::get_pull()->dealloc(velocities);
    memory::Hub::get_pull()->dealloc(rotations);
}

// 0x004913D0
void Shatter::get_step(int index, float step)
{
    maths::Vec4& velocity = velocities[index];
    maths::Vec4& centre = centres[index];
    const double move_x = static_cast<double>(step) * velocity.x;
    const double move_y = static_cast<double>(step) * velocity.y;
    auto move_z = static_cast<float>(static_cast<double>(step) * velocity.z);
    auto move_w = static_cast<float>(static_cast<double>(step) * velocity.w);
    centre.x = static_cast<float>(move_x + centre.x);
    centre.y = static_cast<float>(move_y + centre.y);
    centre.z = static_cast<float>(static_cast<double>(move_z) + centre.z);
    centre.w = static_cast<float>(static_cast<double>(move_w) + centre.w);
    if (centre.y < floor && velocity.y < 0.0f) {
        // A bounce: the height mirrored about the floor, the speed turned up, a new turn.
        centre.y = static_cast<float>((static_cast<double>(floor) - centre.y) + floor);
        velocity.y = static_cast<float>(-(static_cast<double>(bounce) * velocity.y));
        auto a1 = static_cast<float>(get_rand() * kPerRandTwice - 1.0f);
        auto a2 = static_cast<float>(get_rand() * kPerRandTwice - 1.0f);
        auto a3 = static_cast<float>(get_rand() * kPerRandTwice - 1.0f);
        maths::Vec4 axis = {a3, a2, a1, 1.0f};
        axis.make_unit();
        maths::Mat4& rotation = rotations[index];
        std::memset(&rotation, 0, sizeof(rotation));
        core::at(rotation.row, 0).x = core::at(rotation.row, 1).y = core::at(rotation.row, 2).z = core::at(rotation.row, 3).w = 1.0f;
        auto turn_angle = static_cast<float>(get_rand() * kPerRand * 0.1f); // (up to 0.1, 0x00645D74)
        maths::Mat4 turn{};
        maths::build_axis_angle_matrix(turn, axis, turn_angle);
        // D3DX (linked library code): rotation = rotation * turn
        d3dx::matrix_multiply(&rotation, &rotation, &turn);
    }
    velocity.y = static_cast<float>(velocity.y - static_cast<double>(step) * gravity);
}

namespace engine {

// --- settings ---

// 0x00490DE0
void enable_fog(bool on)
{
    g_fog_active = on;
    render::toggle_fog(on);
}

// 0x00490E00
void set_fog_distance(float distance)
{
    g_fog_distance = distance;
    render::set_fog_distance(distance);
}

// 0x00490E10
void set_fog_exponent(float exponent)
{
    g_fog_exponent = exponent;
    set_fog_density_curve(exponent);
}

// 0x00490E20
void set_fog_color(uint32_t rgba)
{
    g_fog_color = rgba;
    render::set_fog_color(rgba);
}

// 0x00491290
void set_level_color(std::span<const float, 4> color) { std::ranges::copy(color, g_level_color.begin()); }

// 0x00491110
void change_breakup_settings(const maths::Vec4* velocity, const maths::Vec4* vector_210e, float value_c483, float area,
                             float speed, float velocity_scale, float lifetime, float floor, float bounce,
                             float value_2e_1c, uint32_t texture, uint32_t value_af_1a, int value_33d_f)
{
    g_glass_burst_velocity = *velocity;
    g_shatter_vector_210e = *vector_210e;
    g_split_area_limit = area == 0.0f ? 82944.0f : static_cast<float>(static_cast<double>(area) * area);
    g_shatter_speed = speed == 0.0f ? 0.0f : speed;
    g_shatter_velocity_scale = velocity_scale == 0.0f ? 1.0f : velocity_scale;
    g_glass_life_span = lifetime == 0.0f ? 4.0f : lifetime;
    g_shatter_floor = floor == 0.0f ? -10000.0f : floor;
    g_glass_restitution = bounce == 0.0f ? 0.8f : bounce;
    g_shatter_value_2e_1c = value_2e_1c;
    g_shatter_value_c483_squared = static_cast<float>(static_cast<double>(value_c483) * value_c483);
    g_shatter_texture = texture;
    g_shatter_value_af_1a = value_af_1a;
    g_shatter_value_33d_f = value_33d_f;
}

// 0x00490DC0
void shatter_geom(void* geom)
{
    // (the PC's shatter_geom - shatter.cpp - only reads the geom: the original also looked up the particles' texture
    // 0x007004BC and passed it, unused)
    render::shatter_geom(static_cast<render::PcGeom*>(geom));
}

// --- texture splats ---

// 0x00490EC0
void age_texture_splats(TextureSplats* splats, int elapsed)
{
    int living = 0;
    splats->last = -1;
    for (int i = 0; i < 0x100; ++i) {
        int left = static_cast<int>(core::at(splats->alive, i));
        if (left < 0)
            continue;
        left -= elapsed;
        core::at(splats->alive, i) = static_cast<uint32_t>(left);
        if (left <= 0) {
            core::at(splats->alive, i) = 0;
            collapse_splat_triangle(splats, i);
        } else {
            splats->last = i;
            ++living;
        }
    }
    if (living < 0x100 - g_splat_reserve)
        return;
    // Nearly full: the triangle furthest through its life (of those with a time and a lifetime; the first one's
    // difference the bar to reach, whatever it is - the last of equals wins).
    int furthest = -1;
    int best = core::at(splats->correction_steps_, 0) - static_cast<int>(core::at(splats->alive, 0));
    for (int i = 0; i < 0x100; ++i) {
        if (core::at(splats->correction_steps_, i) != 0 && core::at(splats->alive, i) != 0) {
            const int done = core::at(splats->correction_steps_, i) - static_cast<int>(core::at(splats->alive, i));
            if (done >= best) {
                best = done;
                furthest = i;
            }
        }
    }
    if (furthest < 0)
        return;
    // Every triangle with the same time and lifetime (as that one has now) fades: its base colours' and its
    // vertices' alphas; once all six are 0 it's dead.
    for (int i = 0; i < 0x100; ++i) {
        if (core::at(splats->alive, i) != core::at(splats->alive, furthest) ||
            core::at(splats->correction_steps_, i) != core::at(splats->correction_steps_, furthest))
            continue;
        const int fade = g_splat_fade;
        auto& base = core::at(splats->base_colors_, i);
        auto& triangle = core::at(splats->vertices, i);
        const std::array<uint32_t*, 6> colours = {{&core::at(base, 0), &core::at(base, 1), &core::at(base, 2),
                                                   &core::at(triangle, 0).argb, &core::at(triangle, 1).argb,
                                                   &core::at(triangle, 2).argb}};
        int gone = 0;
        for (uint32_t* colour : colours) {
            const uint32_t alpha = *colour >> 24; // (the top byte: RGBA's alpha, ARGB's too)
            if (alpha == 0) {
                ++gone;
            } else {
                const int faded = static_cast<int>(alpha) - fade;
                *colour = (*colour & 0x00FFFFFFu) | (static_cast<uint32_t>(faded <= 0 ? 0 : faded) << 24);
            }
        }
        if (gone == 6)
            core::at(splats->alive, i) = 0;
    }
}

// 0x00491F70
void age_all_texture_splats()
{
    if (get_paused())
        return;
    const int elapsed = frame_milliseconds();
    splat_sets().for_each([elapsed](TextureSplats* splats) { age_texture_splats(splats, elapsed); });
}

// 0x00491F10
void light_all_texture_splats()
{
    splat_sets().for_each([](TextureSplats* splats) { light_texture_splats(splats); });
}

// 0x004915A0
void kill_all_texture_splats()
{
    recent_splats().for_each([](RecentSplat* recent) {
        recent_splats().erase(recent);
        memory::Hub::get_pull()->dealloc(recent);
    });
    render::kill_all_texture_splats();
}

// 0x00491630
void kill_texture_splats_in_box(const maths::BBox* box)
{
    recent_splats().for_each([box](RecentSplat* recent) {
        const maths::Vec4& at = recent->pos;
        if (at.x >= box->min.x && at.y >= box->min.y && at.z >= box->min.z && at.x <= box->max.x &&
            at.y <= box->max.y && at.z <= box->max.z)
            recent->time = 0; // (the original stored 0.0f: the same bits)
    });
    render::kill_texture_splats_in_box(box);
}

// 0x004916F0
void kill_texture_splats_of_texture(uint32_t texture)
{
    splat_sets().for_each([texture](TextureSplats* splats) {
        if (splats->texture == texture) {
            splat_sets().erase(splats);
            memory::Hub::get_pull()->dealloc(splats); // (its material isn't freed, as the original)
        }
    });
}

// 0x004928A0
bool create_texture_splat_at(const maths::Vec4* from, const maths::Vec4* to, float size, float depth, float angle,
                             float cone_angle, float lifetime, const char* image_label, uint32_t unknown38,
                             uint32_t follow, uint8_t unknown40, BitmapTexture* texture)
{
    g_splat_reserve = script::fetch_int_value(0x26FCB7BE); // (asserted)
    RecentSplat* recent = nullptr;
    if (follow) {
        // A recent position of the same id near enough: the splat is made from it. Old ones go.
        const uint32_t now = frame_clock::get_time();
        recent_splats().for_each([&](RecentSplat* candidate) {
            if (recent)
                return; // (the walk stopped at the one found)
            if (now < candidate->time || now - candidate->time > 500) {
                recent_splats().erase(candidate);
                memory::Hub::get_pull()->dealloc(candidate);
            } else if (candidate->follow == follow) {
                const double dx = static_cast<double>(from->x) - candidate->pos.x;
                const double dy = static_cast<double>(from->y) - candidate->pos.y;
                const double dz = static_cast<double>(from->z) - candidate->pos.z;
                if (dz * dz + dy * dy + dx * dx < 4356.0f)
                    recent = candidate;
            }
        });
        if (!recent) {
            // None: only remembered.
            auto* added = static_cast<RecentSplat*>(memory::alloc(sizeof(RecentSplat)));
            added->follow = follow;
            added->pos = *from;
            added->time = now;
            recent_splats().insert(added);
            return true;
        }
    }
    if (!texture) {
        auto* textures = g_particle_texture_dict; // (the particles' dictionary)
        texture = textures->fetch_skin(checksums::hash_from_text(image_label));
    }
    // The line's box grown by the size; the collision objects of the grids along the line that meet it.
    const maths::Seg line(*from, *to);
    maths::BBox line_box(line.start);
    line_box.append_spot(line.end);
    maths::BBox box;
    box.min = {static_cast<float>(static_cast<double>(line_box.min.x) - size),
               static_cast<float>(static_cast<double>(line_box.min.y) - size),
               static_cast<float>(static_cast<double>(line_box.min.z) - size), 1.0f};
    box.max = {static_cast<float>(static_cast<double>(line_box.max.x) + size),
               static_cast<float>(static_cast<double>(line_box.max.y) + size),
               static_cast<float>(static_cast<double>(line_box.max.z) + size), 1.0f};
    auto** grids = get_coll_grids_on_line(&line);
    if (!grids || !*grids)
        return false;
    auto& sectors = g_line_sectors;
    auto& objects = g_line_objects;
    int count = 0;
    for (CollGrid** grid = grids; *grid; ++grid) {
        HitMesh* const* in_box = (*grid)->objects_in_box(&box);
        if (!in_box)
            continue;
        for (; *in_box; ++in_box) {
            HitMesh* object = *in_box;
            // (its box in the world first; NaN doesn't rule it out)
            const maths::BBox* bounds = object->get_world_bounds();
            if (!(bounds->min.x > box.max.x) && !(box.min.x > bounds->max.x) && !(bounds->min.z > box.max.z) &&
                !(box.min.z > bounds->max.z) && !(bounds->min.y > box.max.y) && !(box.min.y > bounds->max.y)) {
                void* sector = render::engine::look_up_sector(object->checksum_);
                core::at(objects, count) = object;
                core::at(sectors, count) = sector;
                ++count;
            }
        }
    }
    if (count <= 0)
        return false;
    core::at(sectors, count) = nullptr;
    core::at(objects, count) = nullptr;
    bool made = false;
    if (follow && recent->pos.x == from->x && recent->pos.z == from->z) {
        made = false; // (it hasn't moved)
    } else {
        SplatParams params;
        std::memcpy(params.start.data(), from, 16);
        std::memcpy(params.end.data(), to, 16);
        params.angle = angle;
        params.texture = texture;
        params.size = size;
        params.depth = depth;
        params.cone_angle_ = cone_angle;
        params.lifetime = lifetime;
        params.unknown38 = unknown38;
        params.up = recent ? &recent->pos.x : nullptr;
        params.unknown40 = unknown40;
        TextureSplats* splats = render::create_texture_splat(sectors.data(), objects.data(), &params);
        made = splats != nullptr;
        if (!follow && splats) {
            age_texture_splats(splats, frame_milliseconds());
            light_texture_splats(splats);
            splats->texture = texture->checksum_;
        }
    }
    if (recent) {
        recent->pos = *from;
        recent->time = frame_clock::get_time();
    }
    return made;
}

// --- shatters ---

// 0x00492000
void update_shatters()
{
    if (get_paused())
        return;
    auto elapsed = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    shatters().for_each([elapsed](PcShatter* shatter) {
        const double left = static_cast<double>(shatter->lifetime) - elapsed;
        shatter->lifetime = static_cast<float>(left);
        if (left <= 0.0) {
            shatters().erase(shatter);
            gsl::owner<Shatter*> owned = memory::owned_element(shatter); // (the set's)
            delete owned;
        } else {
            update_shatter(shatter, elapsed);
        }
    });
}

// 0x004920E0
void draw_shatters()
{
    shatters().for_each([](PcShatter* shatter) { draw_shatter(shatter); });
}

// --- flashes ---

// 0x004923E0
void add_flash(int viewport, uint32_t start, uint32_t end, float duration, float z, uint32_t flags,
               const char* /*texture*/)
{
    auto* flash = static_cast<Flash*>(memory::alloc(sizeof(Flash)));
    std::memcpy(flash->start.data(), &start, 4);
    std::memcpy(flash->end.data(), &end, 4);
    flash->duration = duration;
    flash->time_left_ = duration;
    flash->z = z;
    flash->flags = flags;
    flash->texture = nullptr;
    flash->viewport = viewport;
    flashes().insert(flash);
}

// 0x00491790
void update_flashes()
{
    auto elapsed = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    flashes().for_each([elapsed](Flash* flash) {
        Flash* dead = nullptr;
        if (!get_paused() || (flash->flags & 8)) {
            const double left = static_cast<double>(flash->time_left_) - elapsed;
            flash->time_left_ = static_cast<float>(left);
            if (!(left > 0.0)) { // (NaN too)
                dead = flash;
            } else {
                const double t = left / flash->duration;
                uint8_t* now = &flash->red;
                for (int i = 0; i < 4; ++i) {
                    const int change = static_cast<int>(
                        static_cast<int64_t>(static_cast<double>(core::at(flash->start, i) - core::at(flash->end, i)) * t));
                    now[i] = static_cast<uint8_t>(core::at(flash->end, i) + change);
                }
            }
        }
        if (dead) {
            flashes().erase(dead);
            memory::Hub::get_pull()->dealloc(dead);
        }
    });
}

// 0x00491910
void draw_flashes(int viewport, uint32_t pass)
{
    flashes().for_each([viewport, pass](const Flash* flash) {
        if (flash->viewport == viewport && !((flash->flags ^ pass) & 1))
            draw_flash(flash);
    });
}

// --- the tables ---

// 0x00492150
void init_effects()
{
    // (the tables are made once, before the first call: lists::ObjectSet)
    compiled_out();
    compiled_out();
    auto& stack = g_vertex_stack;
    stack.used = 0;
    stack.vertex_size_ = 0;
    std::memset(stack.data.data(), 3, sizeof(stack.data));
}

// 0x00492340
void kill_all_effects()
{
    flashes().for_each([](Flash* flash) {
        flashes().erase(flash);
        memory::Hub::get_pull()->dealloc(flash);
    });
    kill_all_spawned_polys();
    kill_all_texture_splats();
    kill_all_shatters();
}

} // namespace engine

} // namespace render
