#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/pager_message.h"

// The engine's particle systems of the particle component (Nx; the PC class over them is PcParticleSystem,
// pc_particles.h): a system made from a block of parameters, its bounding box and sphere worked out from the
// paths its particles take; and their manager (0x006FA7C4), which keeps them in a list and renders, updates,
// colours and destroys them. (The flexible particle component's systems are another family: particle_system.h.)
namespace render {

// The parameters a particle system is made from (0x15C bytes; kept as the system's +4). Only what the particle
// component fills is named; names from the script parameters where known.
struct SparkleConfig {
    uint32_t label_;          // +0x000 (0xA1DC81F9, required; the manager finds a system by it)
    uint32_t unknown04_;     // +0x004 (0x7321A8D6)
    uint32_t group_id_;       // +0x008 GroupID
    float extent_[3];          // +0x00C (0x3BEE67C8, 0x8E39913D, 0x3F243A3C; scaled)
    float radius_[3];        // +0x018 (0x05400A25, 0xFF7D9AA9, 0x5539FCEE; scaled)
    int unknown24_;          // +0x024 (0x9E52A20F)
    float unknown28_;        // +0x028 (0x35B65BD4; the rate)
    float unknown2c_;        // +0x02C (0xC218CF77; the lifetime, seconds)
    float unknown30_;        // +0x030 (0x520682C6, then 0x59463C93; when the mid is, a percentage of the lifetime)
    float unknown34_;        // +0x034
    float colour_scale_[4];   // +0x038 (r, g, b, a: the manager's colour)
    uint32_t emit_script_;    // +0x048 (0x092276DB: the component's +0x18)
    uint32_t entity_key_;      // +0x04C its object's id (with the above)
    uint8_t unknown50[4];
    maths::Mat4 orientation_turn_;   // +0x054 (0x9D2D0915's angles: X, then Y, then Z)
    maths::Vec4 at_point_[3];     // +0x094 start / mid / end (scaled, oriented)
    maths::Vec4 box_[3];     // +0x0C4 (scaled, oriented)
    maths::Vec4 offset_pos_[3]; // +0x0F4 the positions again (0xB7B7240D: they move with the object)
    float unknown124_;       // +0x124
    uint8_t shade_[3][4];   // +0x128 r, g, b (three arrays), a (three integers, at most 128)
    uint32_t unknown134_;    // +0x134 (0xDD6BB3D5)
    int unknown138_;         // +0x138 (0x13D0BE1D)
    int unknown13c_;         // +0x13C (0x9F677D4E)
    uint32_t unknown140_;    // +0x140 (0x7D99F28D)
    int unknown144_;         // +0x144 (0x4EEDFAE7)
    int unknown148_;         // +0x148 (0xD7E4AB5D)
    int unknown14c_;         // +0x14C (0xE4279BA2)
    uint8_t unknown150_;     // +0x150 (0x9D5923D8's low byte)
    bool flag151_;           // +0x151 (0xA7A5BFFB; the paths go through the mid)
    bool local_positions_;    // +0x152 (0xB7B7240D)
    bool flag153_;           // +0x153 (0x454962EF)
    bool orient_with_object_;  // +0x154 (0xA2487CC3)
    uint8_t unknown155[2];
    bool flag157_;           // +0x157 (0x5446AF14)
    bool flag158_;           // +0x158 (0xA0E585B0: takes the manager's colour)
    uint8_t unknown159[3];

    // 0x00496240: the defaults (sizes 1, no spread, boxes 20 wide, grey colours, rate 300, lifetime 4, mid at 50%,
    // ...). Returns this.
    SparkleConfig* make_new();
    // 0x00495C80: a copy of `other` (its first 0x159 bytes: the last three are left). Returns this.
    SparkleConfig* get_assign(const SparkleConfig& other);
};
static_assert(offsetof(SparkleConfig, colour_scale_) == 0x38 && offsetof(SparkleConfig, emit_script_) == 0x48 &&
                  offsetof(SparkleConfig, orientation_turn_) == 0x54 && offsetof(SparkleConfig, at_point_) == 0x94 &&
                  offsetof(SparkleConfig, box_) == 0xC4 && offsetof(SparkleConfig, offset_pos_) == 0xF4 &&
                  offsetof(SparkleConfig, unknown124_) == 0x124 && offsetof(SparkleConfig, shade_) == 0x128 &&
                  offsetof(SparkleConfig, unknown150_) == 0x150 && offsetof(SparkleConfig, flag158_) == 0x158 &&
                  sizeof(SparkleConfig) == 0x15C,
              "layout must match the original");

// A particle system of the particle component (0x190 bytes; vtable 0x00649380: 0 the scalar deleting destructor
// (0x00495230), 1 Initialise, 2 Release, 3 visibility changed, 4 Render(advance), 5 Update, 6 colour changed,
// 7 IsEmitting - the engine's own empty or constant; the PC class's real ones).
struct Sparkle {
    void** vtable;
    SparkleConfig args_; // +0x004
    maths::BBox extent_box_;         // +0x160: of every particle's path
    maths::Vec4 bound_ball_;     // +0x180: around the box (centre, radius)

    // 0x00496360: the vtable, the default parameters, an empty box. Returns this.
    Sparkle* make_new();
    // 0x00496380: made from `params` (copied): the bounds worked out, then Initialise (vtable slot 1).
    void prepare_all(const SparkleConfig* params);
    // 0x00495260: extent_box_ and bound_ball_ from the paths: each of the start, mid and end positions with its box and
    // size (the mid only through the extremes of the curve through them when the paths go through the mid).
    void update_bounds();
    // 0x00496030 / 0x00496160: position / box `which` (0 start, 1 mid, 2 end).
    void change_place(int which, const maths::Vec4* pos);
    void set_box(int which, const maths::Vec4* box);
    // 0x00496060: position `which` (relative to the start) and box `which` turned by `matrix`; the parameters'
    // rotation becomes `matrix`.
    void spin_by(int which, const maths::Mat4* matrix);
    // 0x00496190: with the manager's colour (flag158_), its colour scale from `rgba` (bytes, in 128ths), then
    // vtable slot 6. Null: only slot 6.
    void change_colour(const uint8_t* rgba);
    // 0x00496210: with the manager's colour (flag158_), its colour scale = `rgba`, then vtable slot 6.
    void set_colour_scale(const float* rgba);
    // 0x004963B0: the emit script: the old one's spawned scripts killed (by its name), then `script` spawned with
    // {0x40C698AF: objectId}.
    void set_emit_script(uint32_t script, uint32_t object_id);

    // The engine's, not yet rewritten (0x00494E00, 0x00494E20, 0x00494E40): rendered (unless hidden or suspended:
    // +0x159, +0x15A), updated, released (the emit script's scripts killed, vtable slot 2).
    void render(bool advance);
    void tick_frame();
    void get_release();
};
static_assert(offsetof(Sparkle, args_) == 0x4 && offsetof(Sparkle, extent_box_) == 0x160 &&
                  offsetof(Sparkle, bound_ball_) == 0x180 && sizeof(Sparkle) == 0x190,
              "layout must match the original");

// The particle component's particle manager (0x1C bytes, vtable 0x006493A0: 0 the scalar deleting destructor,
// 1 Create - the PC manager's, PcParticleManager); one, at 0x006FA7C4.
struct EffectsDirector {
    void** vtable;
    objects::PointerList systems_; // +0x04: Sparkle*
    float shade_[4];          // +0x0C: given to the systems that take it

    // 0x00496770: the vtable, an empty list, white. Returns this.
    EffectsDirector* make_new();
    // 0x004967D0: the list emptied (twice, as the original) and its sentinel freed.
    void destroy_body();
    // 0x00496860 (vtable slot 0): destroyed, and freed with flags & 1. Returns this.
    EffectsDirector* teardown(unsigned int flags);
    // 0x00496510: a new system (vtable slot 1) made from `params`, given the manager's colour if it takes it, onto
    // the end of the list. Null if there's no memory.
    Sparkle* spawn_particle(const SparkleConfig* params);
    // 0x00496590: `particle`'s emit script's scripts killed; then the first system of its name released, deleted and
    // taken off the list.
    void retire_particle(Sparkle* particle);
    // 0x00496640: with any, each rendered - advanced unless the game is paused (0x006FA7D4).
    void draw_particles();
    // 0x00496680: each updated.
    void tick_particles();
    // 0x004966B0: each given `rgba` (set_colour_scale), and the manager's colour = `rgba`.
    void change_colour(const float* rgba);
    // 0x00496700: each released and deleted, the list emptied.
    void kill_every_particle();
};
static_assert(offsetof(EffectsDirector, systems_) == 0x4 && offsetof(EffectsDirector, shade_) == 0xC &&
                  sizeof(EffectsDirector) == 0x1C,
              "layout must match the original");

} // namespace Nx
