// The particle component's particle systems and their manager (Nx): see particle_sets.h. From the disassembly of
// 0x00495260-0x004968AF.

#include "engine/gfx/particle_sets.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <array>
#include <span>

#include "engine/memory/mem.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/particle_maths.h"
#include "engine/objects/movie_manager.h"
#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include <gsl/pointers>

namespace render {

constexpr float kBoxDefaultHalfSize = 20.0F;
constexpr int kNeutralShade = 0x80;
constexpr float kMidPercentDefault = 50.0F;
constexpr float kRateDefault = 300.0F;
constexpr float kLifetimeDefault = 4.0F;
constexpr int kUnknown144Default = 400;
constexpr int kUnknown148Default = 401;
constexpr float kHalf = 0.5F;

// Checksums used in this file (the text each hashes).
constexpr uint32_t kFixadd = 0xA86285A1U; // fixadd

namespace {

constexpr float kByteToUnit = 1.0F / 255.0F;    // 0x00645D70 (the bit pattern 0x3B808081)
constexpr uint32_t kObjectIdParam = 0x40C698AF; // (the emit script's parameter: the object's id)



maths::Vec4* min_of(const maths::Vec4* a, const maths::Vec4* b, maths::Vec4* out) { return min_vector(out, a, b); }
maths::Vec4* max_of(const maths::Vec4* a, const maths::Vec4* b, maths::Vec4* out) { return max_vector(out, a, b); }
// 0x00494F10: per component, the extreme of the quadratic through `start` (at 0), `mid` (at `mid_time`) and `end`
// (at `life`); w 0. Returns out.
maths::Vec4* curve_extremes(maths::Vec4* destination, const maths::Vec4& start, const maths::Vec4& mid,
                            const maths::Vec4& end, float mid_time, float life)
{
    const QuadraticPoints points{.start = start, .p1 = mid, .p2 = end, .t1 = mid_time, .t2 = life};
    return quadratic_lowest(destination, &points);
}

float round_to_float(double value) { return static_cast<float>(value); }

} // namespace

// --- the parameters ---

// 0x00496240
SparkleConfig::SparkleConfig()
{
    for (int i = 0; i < 3; ++i) {
        core::at(extent_, i) = 1.0F;
        core::at(radius_, i) = 0.0F;
        core::at(at_point_, i) = maths::Vec4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        core::at(box_, i) =
            maths::Vec4{.x = kBoxDefaultHalfSize, .y = kBoxDefaultHalfSize, .z = kBoxDefaultHalfSize, .w = 0.0F};
        core::at(offset_pos_, i) = maths::Vec4{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
        for (int k = 0; k < 4; ++k) {
            core::at(core::at(shade_, i), k) = kNeutralShade;
        }
    }
    colour_use_mid_ = false;
    texture_ = 0;
    alpha_cut_off_ = 0;
    mid_percent_ = kMidPercentDefault;
    unknown24_ = 2;
    rate_ = kRateDefault;
    lifetime_ = kLifetimeDefault;
    brightness_ = 1.0F;
    colour_mid_percent_ = kMidPercentDefault;
    blend_mode_ = kFixadd;
    unknown13c_ = 1;
    for (float& scale : colour_scale_) {
        scale = 1.0F;
    }
    unknown14c_ = 0;
    hidden_ = false;
    held_back_ = false;
    flag158_ = false;
    entity_key_ = 0;
    unknown144_ = kUnknown144Default;
    unknown148_ = kUnknown148Default;
}

// 0x00495C80
SparkleConfig* SparkleConfig::get_assign(const SparkleConfig& other)
{
    std::memcpy(this, &other, offsetof(SparkleConfig, unknown159));
    return this;
}

// --- the system ---

// 0x00496360
Sparkle::Sparkle() = default; // (args_ made by its constructor)

// 0x00496380
void Sparkle::prepare_all(const SparkleConfig* params)
{
    args_.get_assign(*params);
    update_bounds();
    initialise();
}

// 0x00495260. The corners are (position, size) less / plus (half the box, half the radius), the size and half the
// radius; the original's x87 code rounds some of the sums to floats and keeps others at 53 bits - followed term by term.
void Sparkle::update_bounds()
{
    const SparkleConfig& config = args_;
    std::array<std::array<float, 3>, 3> hb{};
    std::array<float, 3> hr{};
    std::array<float, 3> size{};
    for (int i = 0; i < 3; ++i) {
        core::at(core::at(hb, i), 0) = core::at(config.box(), i).x * kHalf;
        core::at(core::at(hb, i), 1) = core::at(config.box(), i).y * kHalf;
        core::at(core::at(hb, i), 2) = core::at(config.box(), i).z * kHalf;
        core::at(hr, i) = core::at(config.radius(), i) * kHalf;
        core::at(size, i) = core::at(config.extent(), i);
    }
    std::array<maths::Vec4, 3> lo{};
    std::array<maths::Vec4, 3> hi{};
    for (int i = 0; i < 3; ++i) {
        const maths::Vec4& at = core::at(config.at_point(), i);
        core::at(lo, i).x =
            round_to_float(round_to_float(round_to_float(static_cast<double>(at.x) - core::at(core::at(hb, i), 0)) -
                                          core::at(size, i)) -
                           core::at(hr, i));
        core::at(lo, i).y =
            round_to_float(round_to_float(round_to_float(static_cast<double>(at.y) - core::at(core::at(hb, i), 1)) -
                                          core::at(size, i)) -
                           core::at(hr, i));
        core::at(lo, i).z =
            round_to_float(round_to_float(round_to_float(static_cast<double>(at.z) - core::at(core::at(hb, i), 2)) -
                                          core::at(size, i)) -
                           core::at(hr, i));
        core::at(lo, i).w =
            round_to_float(round_to_float(round_to_float(static_cast<double>(core::at(size, i)) - core::at(hr, i)) -
                                          core::at(size, i)) -
                           core::at(hr, i));
    }
    {
        const maths::Vec4& at = core::at(config.at_point(), 0);
        core::at(hi, 0).x = round_to_float(
            (static_cast<double>(round_to_float(static_cast<double>(core::at(core::at(hb, 0), 0)) + at.x)) +
             core::at(size, 0)) +
            core::at(hr, 0));
        core::at(hi, 0).y = round_to_float(
            ((static_cast<double>(at.y) + core::at(core::at(hb, 0), 1)) + core::at(size, 0)) + core::at(hr, 0));
        core::at(hi, 0).z = round_to_float(
            static_cast<double>(round_to_float(
                core::at(size, 0) +
                static_cast<double>(round_to_float(static_cast<double>(at.z) + core::at(core::at(hb, 0), 2))))) +
            core::at(hr, 0));
        core::at(hi, 0).w = round_to_float(
            static_cast<double>(round_to_float(
                core::at(size, 0) +
                static_cast<double>(round_to_float(static_cast<double>(core::at(hr, 0)) + core::at(size, 0))))) +
            core::at(hr, 0));
    }
    for (int i = 1; i < 3; ++i) {
        const maths::Vec4& at = core::at(config.at_point(), i);
        core::at(hi, i).x =
            round_to_float(round_to_float(round_to_float(static_cast<double>(core::at(core::at(hb, i), 0)) + at.x) +
                                          core::at(size, i)) +
                           core::at(hr, i));
        core::at(hi, i).y =
            round_to_float(round_to_float(round_to_float(static_cast<double>(at.y) + core::at(core::at(hb, i), 1)) +
                                          core::at(size, i)) +
                           core::at(hr, i));
        core::at(hi, i).z = round_to_float(
            (core::at(size, i) +
             static_cast<double>(round_to_float(static_cast<double>(at.z) + core::at(core::at(hb, i), 2)))) +
            core::at(hr, i));
        core::at(hi, i).w =
            round_to_float(static_cast<double>(round_to_float(
                               core::at(size, i) + (static_cast<double>(core::at(hr, i)) + core::at(size, i)))) +
                           core::at(hr, i));
    }
    maths::Vec4 low{};
    maths::Vec4 high{};
    maths::Vec4 curve{};
    low = *min_of(&core::at(lo, 0), &core::at(lo, 2), &curve);
    high = *max_of(&core::at(hi, 0), &core::at(hi, 2), &curve);
    if (config.use_mid()) { // (the paths go through the mid: their extremes on the way)
        const float life = config.lifetime();
        const float mid_time = round_to_float(static_cast<double>(life) * config.mid_percent() * 0.01F);
        maths::Vec4 extreme{};
        curve_extremes(&extreme, core::at(lo, 0), core::at(lo, 1), core::at(lo, 2), mid_time, life);
        low = *min_of(&low, &extreme, &curve);
        curve_extremes(&extreme, core::at(hi, 0), core::at(hi, 1), core::at(hi, 2), mid_time, life);
        high = *max_of(&high, &extreme, &curve);
    }
    extent_box_.min = low;
    extent_box_.max = high;
    // the sphere about the box
    const float half_x = round_to_float((static_cast<double>(high.x) - low.x) * 0.5F);
    const double half_y = (static_cast<double>(high.y) - low.y) * 0.5F;
    const double half_z = static_cast<double>(round_to_float(static_cast<double>(high.z) - low.z)) * 0.5F;
    bound_ball_.w = low.w; // (overwritten below, as the original)
    bound_ball_.z = round_to_float(half_z + low.z);
    bound_ball_.x = round_to_float(static_cast<double>(half_x) + low.x);
    bound_ball_.y = round_to_float(half_y + low.y);
    bound_ball_.w =
        round_to_float(std::sqrt(((static_cast<double>(half_x) * half_x) + (half_z * half_z)) + (half_y * half_y)));
}

// 0x00496030
void Sparkle::change_place(int which, const maths::Vec4* pos) { core::at(args_.at_point(), which) = *pos; }

// 0x00496160
void Sparkle::set_box(int which, const maths::Vec4* box) { core::at(args_.box(), which) = *box; }

// 0x00496060
void Sparkle::spin_by(int which, const maths::Mat4* matrix)
{
    const maths::Vec4 start = core::at(args_.at_point(), 0);
    maths::Vec4& pos = core::at(args_.at_point(), which);
    pos.x -= start.x;
    pos.y -= start.y;
    pos.z -= start.z;
    pos.w -= start.w;
    maths::Vec4 turned{};
    pos = matrix->xform(turned, pos);
    maths::Vec4& box = core::at(args_.box(), which);
    box = matrix->xform(turned, box);
    pos.x = start.x + pos.x;
    pos.y = start.y + pos.y;
    pos.z = start.z + pos.z;
    pos.w = start.w + pos.w;
    args_.orientation_turn() = *matrix;
}

// 0x00496190
void Sparkle::change_colour(const Rgba* rgba)
{
    if (!rgba) {
        on_colour_changed();
        return;
    }
    if (args_.flag158()) {
        for (int i = 0; i < 4; ++i) {
            core::at(args_.colour_scale(), i) = static_cast<float>(core::at(*rgba, i)) * kByteToUnit;
        }
        on_colour_changed();
    }
}

// 0x00496210
void Sparkle::set_colour_scale(std::span<const float, 4> rgba)
{
    if (args_.flag158()) {
        for (int i = 0; i < 4; ++i) {
            core::at(args_.colour_scale(), i) = core::at(rgba, i);
        }
        on_colour_changed();
    }
}

// 0x004963B0
void Sparkle::set_emit_script(checksums::Name script_name, uint32_t object_id)
{
    const uint32_t script = script_name.value();
    if (args_.emit_script() != 0U) {
        script::stop_scripts_by_tag(args_.emit_script());
    }
    args_.set_emit_script(script);
    args_.set_entity_key(object_id);
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(checksums::Name{kObjectIdParam}, args_.entity_key()); // (not checked for null, as the original)
    script::spawn_script(args_.emit_script(), params);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// --- the manager ---

// 0x00496770
EffectsDirector::EffectsDirector()
{
    systems_.make();
    for (float& channel : shade_) {
        channel = 1.0F;
    }
}

// 0x004967D0
EffectsDirector::~EffectsDirector()
{
    for (int pass = 0; pass < 2; ++pass) {
        systems_.erase(systems_.first(), systems_.head());
    }
    memory::dealloc(systems_.head());
}

// 0x00496510
Sparkle* EffectsDirector::spawn_particle(const SparkleConfig* params)
{
    Sparkle* particle = create_system();
    if (particle) {
        particle->prepare_all(params);
        if (particle->args().flag158()) {
            for (int i = 0; i < 4; ++i) {
                core::at(particle->args().colour_scale(), i) = core::at(shade_, i);
            }
            particle->set_colour_scale(shade_);
        }
        systems_.push_back(particle);
    }
    return particle;
}

// 0x00496590
void EffectsDirector::retire_particle(Sparkle* particle)
{
    if (particle->args().emit_script() != 0U) {
        script::stop_scripts_by_tag(particle->args().emit_script());
        particle->args().set_emit_script(0);
    }
    auto* node = systems_.first();
    if (node == systems_.head()) {
        return;
    }
    while (node->value->args().label() != particle->args().label()) {
        node = node->next;
        if (node == systems_.head()) {
            return;
        }
    }
    gsl::owner<Sparkle*> found = memory::adopt<Sparkle*>(node->value);
    found->get_release();
    delete found;
    if (node != systems_.head()) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        memory::dealloc(node);
        --systems_.size();
    }
}

// 0x00496640
void EffectsDirector::draw_particles()
{
    if (systems_.size() == 0) {
        return;
    }
    for (const auto* node = systems_.first(); node != systems_.head(); node = node->next) {
        node->value->render(!objects::simulation_halted());
    }
}

// 0x00496680
void EffectsDirector::tick_particles()
{
    for (const auto* node = systems_.first(); node != systems_.head(); node = node->next) {
        node->value->tick_frame();
    }
}

// 0x004966B0
void EffectsDirector::change_colour(std::span<const float, 4> rgba)
{
    for (const auto* node = systems_.first(); node != systems_.head(); node = node->next) {
        node->value->set_colour_scale(rgba);
    }
    for (int i = 0; i < 4; ++i) {
        core::at(shade_, i) = core::at(rgba, i);
    }
}

// 0x00496700
void EffectsDirector::kill_every_particle()
{
    for (const auto* node = systems_.first(); node != systems_.head();
         node = node->next) {
        gsl::owner<Sparkle*> particle = memory::adopt<Sparkle*>(node->value);
        particle->get_release();
        delete particle;
    }
    systems_.erase(systems_.first(), systems_.head());
}

} // namespace render

