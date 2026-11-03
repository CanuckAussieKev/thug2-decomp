#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/core/mem.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/pager_message.h"
#include "engine/lists/pointer_ring.h"
#include "engine/core/checksums.h"

// The engine's particle systems of the particle component (Nx; the PC class over them is PcParticleSystem,
// pc_particles.h): a system made from a block of parameters, its bounding box and sphere worked out from the
// paths its particles take; and their manager (0x006FA7C4), which keeps them in a list and renders, updates,
// colours and destroys them. (The flexible particle component's systems are another family: particle_system.h.)
namespace render {

// A colour as four bytes: r, g, b, a.
using Rgba = std::array<uint8_t, 4>;

constexpr std::size_t kSparkleConfigColourScaleOffset = 0x38;
constexpr std::size_t kSparkleConfigEmitScriptOffset = 0x48;
constexpr std::size_t kSparkleConfigOrientationTurnOffset = 0x54;
constexpr std::size_t kSparkleConfigAtPointOffset = 0x94;
constexpr std::size_t kSparkleConfigBoxOffset = 0xC4;
constexpr std::size_t kSparkleConfigOffsetPosOffset = 0xF4;
constexpr std::size_t kSparkleConfigColourMidPercentOffset = 0x124;
constexpr std::size_t kSparkleConfigShadeOffset = 0x128;
constexpr std::size_t kSparkleConfigUnknown150Offset = 0x150;
constexpr std::size_t kSparkleConfigFlag158Offset = 0x158;
constexpr std::size_t kSparkleConfigBytes = 0x15C;

// The parameters a particle system is made from (0x15C bytes; kept as the system's +4). Only what the particle
// component fills is named; names from the script parameters where known.
struct SparkleConfig {
    // The fields other code reads and sets.
    uint32_t& label() { return label_; }
    [[nodiscard]] uint32_t label() const { return label_; }
    uint32_t& unknown04() { return unknown04_; }
    [[nodiscard]] uint32_t unknown04() const { return unknown04_; }
    uint32_t& group_id() { return group_id_; }
    [[nodiscard]] uint32_t group_id() const { return group_id_; }
    std::array<float, 3>& extent() { return extent_; }
    [[nodiscard]] const std::array<float, 3>& extent() const { return extent_; }
    std::array<float, 3>& radius() { return radius_; }
    [[nodiscard]] const std::array<float, 3>& radius() const { return radius_; }
    int& unknown24() { return unknown24_; }
    [[nodiscard]] int unknown24() const { return unknown24_; }
    float& rate() { return rate_; }
    [[nodiscard]] float rate() const { return rate_; }
    float& lifetime() { return lifetime_; }
    [[nodiscard]] float lifetime() const { return lifetime_; }
    float& mid_percent() { return mid_percent_; }
    [[nodiscard]] float mid_percent() const { return mid_percent_; }
    void set_brightness(float value) { brightness_ = value; }
    std::array<float, 4>& colour_scale() { return colour_scale_; }
    [[nodiscard]] const std::array<float, 4>& colour_scale() const { return colour_scale_; }
    [[nodiscard]] uint32_t emit_script() const { return emit_script_; }
    void set_emit_script(uint32_t value) { emit_script_ = value; }
    [[nodiscard]] uint32_t entity_key() const { return entity_key_; }
    void set_entity_key(uint32_t value) { entity_key_ = value; }
    void set_unknown50(uint32_t value) { unknown50_ = value; }
    maths::Mat4& orientation_turn() { return orientation_turn_; }
    [[nodiscard]] const maths::Mat4& orientation_turn() const { return orientation_turn_; }
    std::array<maths::Vec4, 3>& at_point() { return at_point_; }
    [[nodiscard]] const std::array<maths::Vec4, 3>& at_point() const { return at_point_; }
    std::array<maths::Vec4, 3>& box() { return box_; }
    [[nodiscard]] const std::array<maths::Vec4, 3>& box() const { return box_; }
    std::array<maths::Vec4, 3>& offset_pos() { return offset_pos_; }
    [[nodiscard]] const std::array<maths::Vec4, 3>& offset_pos() const { return offset_pos_; }
    [[nodiscard]] float colour_mid_percent() const { return colour_mid_percent_; }
    void set_colour_mid_percent(float value) { colour_mid_percent_ = value; }
    std::array<std::array<uint8_t, 4>, 3>& shade() { return shade_; }
    [[nodiscard]] const std::array<std::array<uint8_t, 4>, 3>& shade() const { return shade_; }
    uint32_t& blend_mode() { return blend_mode_; }
    [[nodiscard]] uint32_t blend_mode() const { return blend_mode_; }
    int& alpha_cut_off() { return alpha_cut_off_; }
    [[nodiscard]] int alpha_cut_off() const { return alpha_cut_off_; }
    int& unknown13c() { return unknown13c_; }
    [[nodiscard]] int unknown13c() const { return unknown13c_; }
    uint32_t& texture() { return texture_; }
    [[nodiscard]] uint32_t texture() const { return texture_; }
    int& unknown144() { return unknown144_; }
    [[nodiscard]] int unknown144() const { return unknown144_; }
    int& unknown148() { return unknown148_; }
    [[nodiscard]] int unknown148() const { return unknown148_; }
    int& unknown14c() { return unknown14c_; }
    [[nodiscard]] int unknown14c() const { return unknown14c_; }
    void set_unknown150(uint8_t value) { unknown150_ = value; }
    [[nodiscard]] bool use_mid() const { return use_mid_; }
    void set_use_mid(bool value) { use_mid_ = value; }
    [[nodiscard]] bool local_positions() const { return local_positions_; }
    void set_local_positions(bool value) { local_positions_ = value; }
    [[nodiscard]] bool colour_use_mid() const { return colour_use_mid_; }
    void set_colour_use_mid(bool value) { colour_use_mid_ = value; }
    [[nodiscard]] bool orient_with_object() const { return orient_with_object_; }
    void set_orient_with_object(bool value) { orient_with_object_ = value; }
    [[nodiscard]] bool hidden() const { return hidden_; }
    void set_hidden(bool value) { hidden_ = value; }
    [[nodiscard]] bool held_back() const { return held_back_; }
    [[nodiscard]] bool cull_outside() const { return cull_outside_; }
    void set_cull_outside(bool value) { cull_outside_ = value; }
    [[nodiscard]] bool flag158() const { return flag158_; }
    void set_flag158(bool value) { flag158_ = value; }

private:
    uint32_t label_{};      // +0x000 (0xA1DC81F9, required; the manager finds a system by it)
    uint32_t unknown04_{0xDEDFC057}; // +0x004 (0x7321A8D6)
    uint32_t group_id_{};   // +0x008 GroupID
    std::array<float, 3> extent_{};     // +0x00C (0x3BEE67C8, 0x8E39913D, 0x3F243A3C; scaled)
    std::array<float, 3> radius_{};     // +0x018 (0x05400A25, 0xFF7D9AA9, 0x5539FCEE; scaled)
    int unknown24_;         // +0x024 (0x9E52A20F)
    float rate_;            // +0x028 (0x35B65BD4; the rate)
    float lifetime_;        // +0x02C (0xC218CF77; the lifetime, seconds)
    float mid_percent_;     // +0x030 (0x520682C6, then 0x59463C93; when the mid is, a percentage of the lifetime)
    float brightness_;      // +0x034
    std::array<float, 4> colour_scale_{}; // +0x038 (r, g, b, a: the manager's colour)
    uint32_t emit_script_{};  // +0x048 (0x092276DB: the component's +0x18)
    uint32_t entity_key_;   // +0x04C its object's id (with the above)
    uint32_t unknown50_{};  // +0x050 (0x7E4813A7; 0xE3714DC1 when not given)
    maths::Mat4 orientation_turn_{}; // +0x054 (0x9D2D0915's angles: X, then Y, then Z)
    std::array<maths::Vec4, 3> at_point_{};      // +0x094 start / mid / end (scaled, oriented)
    std::array<maths::Vec4, 3> box_{};           // +0x0C4 (scaled, oriented)
    std::array<maths::Vec4, 3> offset_pos_{};    // +0x0F4 the positions again (0xB7B7240D: they move with the object)
    float colour_mid_percent_;     // +0x124
    std::array<std::array<uint8_t, 4>, 3> shade_{};        // +0x128 r, g, b (three arrays), a (three integers, at most 128)
    uint32_t blend_mode_;          // +0x134 (0xDD6BB3D5)
    int alpha_cut_off_;            // +0x138 (0x13D0BE1D)
    int unknown13c_;               // +0x13C (0x9F677D4E)
    uint32_t texture_;             // +0x140 (0x7D99F28D)
    int unknown144_;               // +0x144 (0x4EEDFAE7)
    int unknown148_;               // +0x148 (0xD7E4AB5D)
    int unknown14c_;               // +0x14C (0xE4279BA2)
    uint8_t unknown150_{};         // +0x150 (0x9D5923D8's low byte)
    bool use_mid_{true};           // +0x151 (0xA7A5BFFB; the paths go through the mid)
    bool local_positions_{false};  // +0x152 (0xB7B7240D)
    bool colour_use_mid_;          // +0x153 (0x454962EF)
    bool orient_with_object_{false}; // +0x154 (0xA2487CC3)
    bool hidden_;                  // +0x155 (Hide: nothing drawn or updated)
    bool held_back_;               // +0x156 (nothing drawn or updated)
    bool cull_outside_{};          // +0x157 (0x5446AF14)
    bool flag158_;                 // +0x158 (0xA0E585B0: takes the manager's colour)
    std::array<uint8_t, 3> unknown159{};

public:

    // 0x00496240: the defaults (sizes 1, no spread, boxes 20 wide, grey colours, rate 300, lifetime 4, mid at 50%,
    // ...).
    SparkleConfig();
    // 0x00495C80: a copy of `other` (its first 0x159 bytes: the last three are left). Returns this.
    SparkleConfig* get_assign(const SparkleConfig& other);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(SparkleConfig, colour_scale_) == kSparkleConfigColourScaleOffset && offsetof(SparkleConfig, emit_script_) == kSparkleConfigEmitScriptOffset &&
                          offsetof(SparkleConfig, orientation_turn_) == kSparkleConfigOrientationTurnOffset && offsetof(SparkleConfig, at_point_) == kSparkleConfigAtPointOffset &&
                          offsetof(SparkleConfig, box_) == kSparkleConfigBoxOffset && offsetof(SparkleConfig, offset_pos_) == kSparkleConfigOffsetPosOffset &&
                          offsetof(SparkleConfig, colour_mid_percent_) == kSparkleConfigColourMidPercentOffset && offsetof(SparkleConfig, shade_) == kSparkleConfigShadeOffset &&
                          offsetof(SparkleConfig, unknown150_) == kSparkleConfigUnknown150Offset && offsetof(SparkleConfig, flag158_) == kSparkleConfigFlag158Offset &&
                          sizeof(SparkleConfig) == kSparkleConfigBytes,
                      "layout must match the original");
    }
};

constexpr std::size_t kSparkleExtentBoxOffset = 0x160;
constexpr std::size_t kSparkleBoundBallOffset = 0x180;
constexpr std::size_t kSparkleBytes = 0x190;

// A particle system of the particle component (0x190 bytes; vtable 0x00649380: the engine's own empty or constant
// virtual functions; the PC class's real ones - PcParticleSystem, pc_particles.h).
struct Sparkle : memory::HeapObject {
    // 0x00496360: the default parameters, an empty box.
    Sparkle();
    // 0x00494DF0 (0x00495230 the scalar deleting destructor, vtable slot 0)
    virtual ~Sparkle() = default;

    // Vtable slot 1: set up from the parameters (prepare_all's last step).
    virtual void initialise() {}
    // Slot 2: what initialise made freed (get_release's last step).
    virtual void free_resources() {}
    // Slot 3: told it was hidden or shown (conceal).
    virtual void on_visibility_changed(bool /*hidden*/) {}
    // Slot 4: drawn - with `advance` its particles aged a frame (render).
    virtual void draw(bool /*advance*/) {}
    // Slot 5: updated (tick_frame).
    virtual void update() {}
    // Slot 6: told its colours changed.
    virtual void on_colour_changed() {}
    // Slot 7: whether it's emitting (the particle component's IsEmitting); the engine's: true.
    virtual bool is_emitting() { return true; }

    // The fields other code reads and sets.
    SparkleConfig& args() { return args_; }
    [[nodiscard]] const SparkleConfig& args() const { return args_; }
    [[nodiscard]] const maths::Vec4& bound_ball() const { return bound_ball_; }

private:
    SparkleConfig args_;     // +0x004
    maths::BBox extent_box_; // +0x160: of every particle's path
    maths::Vec4 bound_ball_{}; // +0x180: around the box (centre, radius)

public:

    // 0x00496380: made from `params` (copied): the bounds worked out, then initialise.
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
    // on_colour_changed. Null: only on_colour_changed.
    void change_colour(const Rgba* rgba);
    // 0x00496210: with the manager's colour (flag158_), its colour scale = `rgba`, then on_colour_changed.
    void set_colour_scale(std::span<const float, 4> rgba);
    // 0x004963B0: the emit script: the old one's spawned scripts killed (by its name), then `script` spawned with
    // {0x40C698AF: objectId}.
    void set_emit_script(checksums::Name script_name, uint32_t object_id);

    // (engine_particles.cpp)
    // 0x00494E00 / 0x00494E20: drawn / updated unless hidden or held back.
    void render(bool advance);
    void tick_frame();
    // 0x00494E40: its emit script's spawned scripts killed, then free_resources.
    void get_release();
    // 0x00494E70 (not a Ghidra function): hidden or shown - on_visibility_changed when it changes.
    void conceal(bool hide);
    // 0x00494E90 / 0x00494EA0: the rate and the lifetime set.
    void set_rate(float rate);
    void set_lifespan(float lifetime);
    // 0x00494EB0: size `which` (start, mid, end) set.
    void change_extent(int which, float size);
    // 0x00494EC0 / 0x00494EE0 / 0x00494EF0: colour `which`, the brightness and the colour mid percentage set - then
    // on_colour_changed.
    void change_colour(int which, const Rgba& colour);
    void set_brightness(float value);
    void set_colour_mid_percent(float percent);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(Sparkle, args_) == 0x4 && offsetof(Sparkle, extent_box_) == kSparkleExtentBoxOffset &&
                          offsetof(Sparkle, bound_ball_) == kSparkleBoundBallOffset && sizeof(Sparkle) == kSparkleBytes,
                      "layout must match the original");
    }
};

constexpr std::size_t kEffectsDirectorShadeOffset = 0xC;
constexpr std::size_t kEffectsDirectorBytes = 0x1C;

// The particle component's particle manager (0x1C bytes, vtable 0x006493A0); one, at 0x006FA7C4 - the PC's,
// PcParticleManager.
struct EffectsDirector : memory::HeapObject {
    EffectsDirector(const EffectsDirector&) = delete;
    EffectsDirector& operator=(const EffectsDirector&) = delete;
    // 0x00496770: an empty list, white.
    EffectsDirector();
    // 0x004967D0: the list emptied (twice, as the original) and its sentinel freed. (0x00496860 the scalar deleting
    // destructor, vtable slot 0.)
    virtual ~EffectsDirector();
    // Vtable slot 1: a new system (the engine's: none).
    virtual Sparkle* create_system() { return nullptr; }

private:
    lists::PointerRing<Sparkle> systems_{}; // +0x04
    std::array<float, 4> shade_{};               // +0x0C: given to the systems that take it

public:

    // 0x00496510: a new system (create_system) made from `params`, given the manager's colour if it takes it, onto
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
    void change_colour(std::span<const float, 4> rgba);
    // 0x00496700: each released and deleted, the list emptied.
    void kill_every_particle();
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(EffectsDirector, systems_) == 0x4 && offsetof(EffectsDirector, shade_) == kEffectsDirectorShadeOffset &&
                          sizeof(EffectsDirector) == kEffectsDirectorBytes,
                      "layout must match the original");
    }
};

} // namespace render
