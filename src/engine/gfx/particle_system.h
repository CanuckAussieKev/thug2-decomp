#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/core/packed_colour.h"

#include "engine/core/arena_alloc.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/smtptr.h"
#include <gsl/pointers>
#include "engine/core/checked_index.h"
#include "engine/maths/vec2.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The engine's flexible particle systems (Nx; the particle manager 0x006FA7C0's - the flexible particle component's
// systems): particles emitted from a box about the emitter in a cone about a direction, each with a speed, a force, a
// size from start to end and a life; their positions kept for a number of history steps (the ribbons' trails); the
// colours from start through mid to end. Each type is a class of its own over the base (newflat, a flat ribbon, glow,
// glowribbontrail, line, ...), with a PC class over that which draws it (pc_flexible_particles.h).
namespace render {

class RenderModel;

// A particle (0x30 bytes).
struct FlexibleParticle {
    std::array<float, 3> velocity;    // +0x00
    std::array<float, 3> force;       // +0x0C: added to the velocity each second (the system's when it was emitted)
    std::array<float, 2> start_size_; // +0x18: across, up
    std::array<float, 2> end_size_;   // +0x20
    float age;            // +0x28
    float lifetime;       // +0x2C
};

// What a system's constructor is given: (name, max, texture, blend, alpha, segments, unknown), and some also the
// history; the shaded class only the first five; the engine's model class its model name instead of the texture.
struct ConstructArgs {
    uint32_t name{};
    int particle_cap_{};
    uint32_t texture{};
    uint32_t blend_mode_{};
    int fixed_alpha_{};
    int segments{};
    float unknown{};
    int history{};
    const char* model{};
};

// The base of every type (0x134 bytes; vtable 0x00649428). Smart pointers point at it (the flexible particle
// component's): a SharedBase. Its list node at +0x0C (the particle manager's list; the original's node had a table of
// its own, the "second vtable" - the node here keeps its size: the types' fields are at the original's offsets).
struct EmitterInstance : objects::SharedBase {
    EmitterInstance(const EmitterInstance&) = delete;
    EmitterInstance& operator=(const EmitterInstance&) = delete;
    // On the current heap, zeroed, with the particle manager's assert flag (kAllocAssertFlag); null when it's full.
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* memory);

    // 0x00496F10: no smart pointers, the node (not linked), no flags, then Reset.
    EmitterInstance();
    // 0x00498690 (0x00498F80 the scalar deleting destructor, vtable slot 0): the scripts, the parameters, the colours,
    // the positions and the particles freed; the particle manager told (0x0049AD60); the node unlinked; its smart
    // pointers cleared.
    ~EmitterInstance() override;
    // Vtable slot 1: the particles drawn. The engine's (the types' too, but the model type's) prints "STUB:
    // plat_render" (0x004A1040, shared with the weather's).
    virtual void render();
    // Slot 2: the vertex lists - history steps - a particle has. 0x004968A0 (not a Ghidra function): "STUB:
    // plat_get_num_vertex_lists" printed, 0.
    virtual int get_num_vertex_lists();
    // Slot 3: the colours a particle has. 0x00496890: "STUB: plat_get_num_particle_colors" printed, 0.
    virtual int get_num_colours();

    // 0x0041D9C0 (thiscall; flexible_particle.cpp): flag bit `bit` of flag_bits_ set or cleared.
    void set_flag(int bit, bool on);

    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    [[nodiscard]] const maths::Vec4& at_point() const { return at_point_; }
    void set_at_point(const maths::Vec4& value) { at_point_ = value; }
    maths::Vec4& align_to() { return align_to_; }
    [[nodiscard]] const maths::Vec4& align_to() const { return align_to_; }
    std::array<float, 4>& colour_scale() { return colour_scale_; }
    [[nodiscard]] const std::array<float, 4>& colour_scale() const { return colour_scale_; }
    [[nodiscard]] float emit_rate() const { return emit_rate_; }
    void set_emit_rate(float value) { emit_rate_ = value; }
    void set_unknown78(uint32_t value) { unknown78_ = value; }
    [[nodiscard]] int max_particles() const { return max_particles_; }
    void set_max_particles(int value) { max_particles_ = value; }
    [[nodiscard]] int particle_count() const { return particle_count_; }
    void set_particle_count(int value) { particle_count_ = value; }
    [[nodiscard]] const std::array<float, 4>& extent() const { return extent_; }
    [[nodiscard]] float size_scale() const { return size_scale_; }
    void set_xform_ref(maths::Mat4* value) { xform_ref_ = value; }
    [[nodiscard]] float brightness() const { return brightness_; }
    void set_brightness(float value) { brightness_ = value; }
    [[nodiscard]] uint32_t label() const { return label_; }
    void set_label(uint32_t value) { label_ = value; }
    [[nodiscard]] uint32_t group_id() const { return group_id_; }
    void set_group_id(uint32_t value) { group_id_ = value; }
    std::span<FlexibleParticle>& particle_list() { return particle_list_; }
    [[nodiscard]] const std::span<FlexibleParticle>& particle_list() const { return particle_list_; }
    [[nodiscard]] script::Struct* script_args() const { return static_cast<script::Struct*>(script_args_); } // (a view: it stays the owner's)
    void set_script_args(gsl::owner<script::Struct*> value) { script_args_ = value; }
    [[nodiscard]] float colour_mid() const { return colour_mid_; }
    void set_colour_mid(float value) { colour_mid_ = value; }
    std::span<uint32_t>& start_colour() { return start_colour_; }
    [[nodiscard]] const std::span<uint32_t>& start_colour() const { return start_colour_; }
    std::span<uint32_t>& mid_colour() { return mid_colour_; }
    [[nodiscard]] const std::span<uint32_t>& mid_colour() const { return mid_colour_; }
    std::span<uint32_t>& end_colour() { return end_colour_; }
    [[nodiscard]] const std::span<uint32_t>& end_colour() const { return end_colour_; }
    [[nodiscard]] int history_length() const { return history_length_; }
    void set_history_length(int value) { history_length_ = value; }
    std::span<float>& position_array() { return position_array_; }
    [[nodiscard]] const std::span<float>& position_array() const { return position_array_; }
    uint32_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] uint32_t flag_bits() const { return flag_bits_; }
    [[nodiscard]] uint8_t rank() const { return rank_; }
    void set_rank(uint8_t value) { rank_ = value; }
    void set_fading_out(bool value) { fading_out_ = value; }
    [[nodiscard]] bool emitter_independent() const { return emitter_independent_; }
    void set_emitter_independent(bool value) { emitter_independent_ = value; }
    [[nodiscard]] bool aligned() const { return aligned_; }
    void set_aligned(bool value) { aligned_ = value; }
    [[nodiscard]] bool wants_time() const { return wants_time_; }
    [[nodiscard]] bool spewing() const { return spewing_; }
    void set_spewing(bool value) { spewing_ = value; }
    [[nodiscard]] uint8_t unknown12e() const { return unknown12e_; }
    void set_unknown12e(uint8_t value) { unknown12e_ = value; }
    void set_unknown12f(bool value) { unknown12f_ = value; }
    void set_unknown130(bool value) { unknown130_ = value; }
    void set_fill_path(bool value) { fill_path_ = value; }
    void set_update_brightness(bool value) { update_brightness_ = value; }
    void set_unknown133(bool value) { unknown133_ = value; }

private:
    uint32_t unknown08{};
    memory::ListNode list_link_{};      // +0x0C: in the particle manager's list (data: the system)
    maths::Vec4 at_point_{};            // +0x20: the emitter
    maths::Vec4 last_pos_{};            // +0x30: where it was last update (FillPath emits along the way)
    maths::Vec4 align_to_{};            // +0x40 align_to
    maths::Vec4 bounds_{};              // +0x50: the bounding sphere (centre, radius)
    std::array<float, 4> colour_scale_{};           // +0x60: the level's colour (with 0x10000 set)
    float emit_rate_{};                 // +0x70 emit_rate (particles a second)
    float emit_owed_{};                 // +0x74: particles owed, a fraction
    uint32_t unknown78_{};              // +0x78 (the component's 0x184197D5: an object's id - the system dies with it)
    int max_particles_{};               // +0x7C
    int lod_min_particles_{};           // +0x80 setLODParams min_particles
    float lod_near_{};                  // +0x84 lod_dist1, squared
    float lod_far_{};                   // +0x88 lod_dist2, squared
    int particle_count_{};              // +0x8C: live particles (empty_system: 0)
    std::array<float, 2> speed_{};                  // +0x90 setSpeedRange min, max
    std::array<float, 3> emit_range_{};             // +0x98 setEmitRange width, height, depth
    float angle_spread_{};              // +0xA4 setAngleSpread spread
    std::array<float, 3> force_{};                  // +0xA8 setForce
    std::array<float, 4> extent_{};                 // +0xB4 setParticleSize sw, sh, ew, eh
    float size_scale_{};                // +0xC4 SetParticleSizeScale
    std::array<float, 2> age_left_{};               // +0xC8 setLife min, max
    std::array<float, 3> emit_target_{};            // +0xD0 setEmitTarget (a direction)
    maths::Mat4* xform_ref_{};          // +0xDC the matrix it follows (its object's display matrix)
    std::array<float, 3> orientation_turn_{};       // +0xE0 (x, y, z angles: the matrix of build_emitter_orientation; setEmitAngle)
    float brightness_{};                // +0xEC (a model's +0x22C, update_Brightness; the ground's brightness)
    uint32_t label_{};                  // +0xF0
    uint32_t group_id_{};               // +0xF4 groupID
    std::span<FlexibleParticle> particle_list_; // +0xF8 (max_particles_ of them, after their count)
    script::Script* emit_script_ptr_{}; // +0xFC emitscript (run each emit... update)
    script::Script* tick_script_{};     // +0x100 UpdateScript
    gsl::owner<script::Struct*> script_args_{};     // +0x104 their params
    float colour_mid_{};       // +0x108 setColor midTime: when the mid colour is, a fraction of a life (below 0: none)
    std::span<uint32_t> start_colour_; // +0x10C RGBA words (r the low byte; the types with segments: one a segment and three more)
    std::span<uint32_t> mid_colour_; // +0x110
    std::span<uint32_t> end_colour_; // +0x114
    int history_length_{};     // +0x118 (the glow ribbon trail's history steps)
    std::span<float> position_array_;  // +0x11C: three floats a particle a history step (step-major)
    uint32_t flag_bits_{0};    // +0x120 (bit 0 while the component updates it; 4 the system's set; 0x10 random
                               // spread (0xDB8621F7), 0x100 setCircularEmit; 0x4000 UseRandomUVQuadrants, 0x8000
                               // 0x5446AF14, 0x10000 takes the level's colour)
    uint8_t rank_{};           // +0x124 priority
    bool fading_out_{};        // +0x125: emits no more, dies once empty
    bool emitter_independent_{}; // +0x126 setEmitterIndependent: positions in the world, not about the emitter
    bool aligned_{};             // +0x127 align_to given
    bool random_direction_{};    // +0x128 (0xDB8621F7 random)
    bool circular_emit_{};       // +0x129 setCircularEmit circular
    bool wants_time_{};          // +0x12A (Update is given the component's time)
    bool spewing_{};             // +0x12B
    bool moving_{};              // +0x12C (setLife: particles get a direction and speed, and move)
    bool end_colour_set_{};      // +0x12D (setColor's end colour given: the start no longer copies over it)
    uint8_t unknown12e_{};
    bool unknown12f_{};        // +0x12F (set while the component updates it, cleared while its object is hidden;
                               // Hide sets it to its argument)
    bool unknown130_{};        // +0x130 (cleared while its object is hidden; set: updated this frame)
    bool fill_path_{};         // +0x131 FillPath
    bool update_brightness_{}; // +0x132
    bool unknown133_{};        // +0x133 (cleared by 0xDE45A90D: not culled)

public:

    // 0x00496D70: the defaults (no scripts, no matrix, rate 0, the emitter at (2048, 144, -928), speed 1 - 2, emit
    // range (1, 1, 0), spread 0.5, force (0, -0.02, 0), sizes 2, life 2, target (0, 1, 0), white, ...).
    void get_reset();

    // 0x004968B0 / 0x004968E0 / 0x00496910 / 0x00496940: emit_range_ / m_force / emit_target_ / orientation_turn_.
    void set_emit_range(const maths::Vec4& range);
    void set_force(const maths::Vec4& force);
    void set_emit_target(const maths::Vec4& target);
    void change_spin(const maths::Vec4& spin);
    // 0x00496970 / 0x004969B0: particle `particle`'s position at history step `step` read / written.
    // (x, y, z; w 0)
    maths::Vec4 fetch_place(int particle, int step);
    void change_place(int particle, int step, const maths::Vec4& place);
    // 0x004969F0: the matrix particles are emitted with: the one it follows, else rows from the emit target (the
    // third row; the first across it and up - or along x when the target is nearly up -, the second their cross).
    void get_emit_matrix(maths::Mat4* out);
    // 0x00496B40: `count`, or fewer with LOD (near < far, at most one viewport, a camera): between the distances
    // (squared) the number falls from the maximum to lod_min_particles_; beyond them lod_min_particles_.
    int lod_particle_count(int count);
    // 0x00496C30: the bounding sphere: about the emitter, as far as the largest emit range plus the farthest a
    // particle goes in its longest life (the largest force and the top speed).
    void update_bounds();
    // 0x00496CE0: with 0x10000 (it takes the level's colour), the colour scale = `rgba`.
    void set_colour_scale(std::span<const float, 4> rgba);
    // 0x00496F60: the emit script run on; once it ends, deleted.
    void run_emit_script();
    // 0x00496FA0: when shown and updated this frame (unknown12f_, unknown130_), or when `force`: render, and flag 1
    // and unknown12f_ cleared.
    void render_if_visible(bool force);
    // 0x00497790 / 0x00497900: the emit / update script: `script` (on a CScript of its own) with the shared
    // parameters plus `params`, on `object`; 0: the script deleted.
    void set_emit_script(uint32_t script, script::Struct* params, void* object);
    void set_update_script(uint32_t script, script::Struct* params, void* object);
    // 0x00497A70: flag 0x10000 and wants_time_ = `on` (the byte as given).
    void set_wants_time(uint8_t on);
    // 0x00497AB0: the matrix of orientation_turn_ (init_from_angles) into `out`. Returns out.
    maths::Mat4* build_emitter_orientation(maths::Mat4* out);

    // (particle_system_update.cpp)
    // 0x00496FE0: `count` particles emitted (no more than there's room for; none once dying). Moving ones: a
    // direction in a cone about the emit matrix's third row (the spread a fraction of pi; random or circular
    // spreads of the emit box as set), a speed and a life between their limits, the force and sizes (scaled), a
    // position in the emit box (FillPath: somewhere along the way from the last position); else only sizes and a life,
    // at the emitter. Every history step starts at the position (in the world when emitter independent).
    void get_emit(int count);
    // 0x00497B00: a member function the flexible particle component passes on: setEmitAngle, setpos,
    // setParticleSize, SetParticleSizeScale, EmitRate, setColor (corner; sr ... ea, mr ... ma, er ... ea; midtime),
    // 0x482482C6 (no particles), setSpeedRange, emit, setAngleSpread, ApplyEnvBrightness (with 0x652F5D69 every
    // update, else now: the ground's brightness under the emitter), setEmitRange, setForce, setEmitTarget (only
    // when not (0, 1, 0)), setLife (the particles move), setEmitterIndependent, 0xDB8621F7 (random), setCircularEmit,
    // setLODParams. True.
    bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);
    // 0x004987F0: a frame of `time` seconds: dying and empty, or following an object that's gone: deleted. Unless
    // `skip` or not shown: the emit script run, the bounds, emission at the rate (none while out of view with a
    // rate of 0: then the particles only age and expire), the update script; with moving particles and time, the
    // brightness, each particle aged, sped up and moved (its history shifted), the expired and those past the LOD
    // count taken out (the last moved into the gap), the last position kept.
    void tick_frame(float time, bool skip);
    // The brightness of the ground under the emitter (a line from 1 above to 4800 below; `movables` too) into
    // brightness_, doubled; left as it was without a hit.
    void update_ground_brightness(bool movables);
    // Particle `index` taken out: the last moved into its place (each history step's position too), the last's
    // positions set far away (1024, 1024, 1024).
    void remove_particle(int index, bool through_accessors);
};

// The types' engine classes: constructed with (name, max particles, texture, blend mode, fixed alpha, segments, a
// float, history) - each reads what it uses -, the particles, positions and colours allocated (each step stops at
// the first that fails), the colours grey, no mid colour, flag 4. Only their PC classes are ever made
// (pc_flexible_particles.h); the model type is the exception. The original's slot 2 / 3 bodies that return a
// constant or a field are shared between classes (0x0047DF90 "return 0", 0x0049B8C0 2, 0x0049BB70 4, 0x0049C0F0 3,
// 0x0049BB80 the int at +0x134).

// "newflat" (vtable 0x0064943C): a position and one colour each.
struct NewFlatParticles : EmitterInstance {
    // 0x00499010
    NewFlatParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                     int segments, float unknown, int history);
    // 0x00498FB0 (0x00498FE0 the scalar deleting destructor; 0x00498FD0 through the node): the base's.
    ~NewFlatParticles() override = default;
    int get_num_vertex_lists() override { return 0; }
    int get_num_colours() override { return 0; }
};

// The flat ribbon (0x3D56A7A6; vtable 0x00649450): history + 1 positions each, a colour a segment and three more.
struct FlatRibbonParticles : EmitterInstance {
    FlatRibbonParticles(const FlatRibbonParticles&) = delete;
    FlatRibbonParticles& operator=(const FlatRibbonParticles&) = delete;
    // The fields other code reads and sets.
    [[nodiscard]] int segments() const { return segments_; }

private:
    int segments_; // +0x134: history + 1

public:

    // 0x004992C0
    FlatRibbonParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha,
                        int segments, float unknown, int history);
    // 0x00499200 (0x00499290 the scalar deleting destructor; 0x004991F0 through the node): the particles freed (and
    // forgotten), the base's.
    ~FlatRibbonParticles() override;
    int get_num_vertex_lists() override { return segments_; }
    // 0x004991E0: segments_ + 3.
    int get_num_colours() override;
};

// "glow" (vtable 0x00649464): three colours.
struct GlowParticles : EmitterInstance {
    // The fields other code reads and sets.
    [[nodiscard]] int segments() const { return segments_; }
    [[nodiscard]] float glow_width() const { return glow_width_; }

private:
    int segments_;     // +0x134
    float glow_width_; // +0x138

public:

    // 0x004995B0 (`unknown`: the glow's width)
    explicit GlowParticles(const ConstructArgs& args);
    // 0x00499550 (0x00499580 the scalar deleting destructor; 0x00499570 through the node): the base's.
    ~GlowParticles() override = default;
    int get_num_vertex_lists() override { return 0; }
    int get_num_colours() override { return 3; }
};

// "glowribbontrail" (vtable 0x00649478): history + 1 positions each (history_length_), colours as the flat ribbon.
struct GlowRibbonTrailParticles : EmitterInstance {
    // The fields other code reads and sets.
    [[nodiscard]] int segments() const { return segments_; }
    [[nodiscard]] float span() const { return span_; }

private:
    int segments_; // +0x134: the glow's
    float span_;   // +0x138

public:

    // 0x00499880 (`unknown`: the trail's width)
    explicit GlowRibbonTrailParticles(const ConstructArgs& args);
    // 0x00499800 (0x00499850 the scalar deleting destructor; 0x00499840 through the node): the base's.
    ~GlowRibbonTrailParticles() override = default;
    // 0x00499830: history_length_. 0x00499820: history_length_ + 3.
    int get_num_vertex_lists() override;
    int get_num_colours() override;
};

// "line" (vtable 0x0064948C): history + 1 positions each, a colour a step.
struct LineParticles : EmitterInstance {
private:
    int steps_; // +0x134: history + 1

public:

    // 0x00499B80
    LineParticles(uint32_t name, int particle_cap, uint32_t texture, uint32_t blend_mode, int fixed_alpha, int segments,
                  float unknown, int history);
    // 0x00499B20 (0x00499B50 the scalar deleting destructor; 0x00499B40 through the node): the base's.
    ~LineParticles() override = default;
    int get_num_vertex_lists() override { return steps_; }
    int get_num_colours() override { return 2; }
};

// The model type (0x286A8D26; vtable 0x006494A0; constructed by 0x0049A0C0): models as particles - the one engine type
// made without a PC class.
struct ModelParticles final : EmitterInstance {
    ModelParticles(const ModelParticles&) = delete;
    ModelParticles& operator=(const ModelParticles&) = delete;
private:
    unsigned int model_count_;  // +0x134
    unsigned int drawn_models_; // +0x138: the models Render goes through (the same count)
    void* unknown_13c_ptr_{
        nullptr}; // +0x13C (freed): a rotation (maths::QuatRotation) a model, turned by its particle's velocity
    std::array<uint8_t, 4> unknown140{};
    RenderModel** model_list_{nullptr}; // +0x144: model_count_ of them

public:

    // 0x0049A0C0 (model_particles.cpp): constructed with (name, max particles, model file name, ...): the base's,
    // `particleCap` particles, positions and models - each from "<model>.mdl" (lower case) at a random scale 1 - 2.98 -
    // unless the memory left is under 0x1194 bytes a model; a random rotation each (only the first ever normalised, as
    // the original); one colour of each kind (not set); no mid colour, flag 4. Each step stops at the first that fails.
    explicit ModelParticles(const ConstructArgs& args);
    // 0x00499E10 (0x0049A090 the scalar deleting destructor; 0x00499E00 through the node): +0x13C freed, the models
    // deleted and their array freed (each forgotten), the base's.
    ~ModelParticles() override;
    // 0x0049A4C0 (vtable slot 1): with no particles every model hidden; else from the last particle's model on hidden,
    // and each particle's model shown, coloured (its colour through its life), turned (its rotation moved by its
    // velocity and normalised), scaled (its start size less a quarter for each of its index's lowest two bits) and
    // drawn at its position.
    void render() override;
    int get_num_vertex_lists() override { return 0; }
    int get_num_colours() override { return 0; }

    // 0x00499EE0: the colour `t` of the way from `from` to `to` (RGBA bytes, each channel wrapping as a byte), its red,
    // green and blue then scaled by the colour scale times the brightness (clamped to 0-255; the blue's scale rounded
    // to a float first, as the original) - into *out, RGBA. Returns out.
    uint32_t* lerp_colour(uint32_t* out, const colour::Bytes& from, const colour::Bytes& to, float t);
};

inline constexpr uint8_t kAllocAssertFlag{0};

// A particle's place (three floats) at a history step in the step-major positions: checked against the array's extent.
inline std::span<float, 3> place_at(std::span<float> positions, int max_particles, int step, int particle)
{
    const auto first = static_cast<std::size_t>(3 * (max_particles * step + particle));
    (void)core::at(positions, first + 2);
    return positions.subspan(first, 3).first<3>();
}

} // namespace render
