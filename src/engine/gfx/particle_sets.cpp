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

#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/particle_maths.h"
#include "engine/objects/movie_manager.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace render {

namespace {

constexpr float kByteToUnit = 1.0f / 255.0f;    // 0x00645D70 (the bit pattern 0x3B808081)
constexpr uint32_t kObjectIdParam = 0x40C698AF; // (the emit script's parameter: the object's id)

bool is_game_halted() { return objects::g_simulation_halted; }


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

float current_round(double value) { return static_cast<float>(value); }

} // namespace

// --- the parameters ---

// 0x00496240
SparkleConfig::SparkleConfig()
{
    for (int i = 0; i < 3; ++i) {
        core::at(extent_, i) = 1.0f;
        core::at(radius_, i) = 0.0f;
        core::at(at_point_, i) = maths::Vec4{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
        core::at(box_, i) = maths::Vec4{.x = 20.0f, .y = 20.0f, .z = 20.0f, .w = 0.0f};
        core::at(offset_pos_, i) = maths::Vec4{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
        for (int k = 0; k < 4; ++k)
            core::at(core::at(shade_, i), k) = 0x80;
    }
    colour_use_mid_ = false;
    texture_ = 0;
    alpha_cut_off_ = 0;
    mid_percent_ = 50.0f;
    unknown24_ = 2;
    rate_ = 300.0f;
    lifetime_ = 4.0f;
    brightness_ = 1.0f;
    colour_mid_percent_ = 50.0f;
    blend_mode_ = 0xA86285A1;
    unknown13c_ = 1;
    for (float& scale : colour_scale_)
        scale = 1.0f;
    unknown14c_ = 0;
    hidden_ = false;
    held_back_ = false;
    flag158_ = false;
    entity_key_ = 0;
    unknown144_ = 400;
    unknown148_ = 401;
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
    const SparkleConfig& p = args_;
    std::array<std::array<float, 3>, 3> hb{};
    std::array<float, 3> hr{};
    std::array<float, 3> s{};
    for (int i = 0; i < 3; ++i) {
        core::at(core::at(hb, i), 0) = core::at(p.box_, i).x * 0.5f;
        core::at(core::at(hb, i), 1) = core::at(p.box_, i).y * 0.5f;
        core::at(core::at(hb, i), 2) = core::at(p.box_, i).z * 0.5f;
        core::at(hr, i) = core::at(p.radius_, i) * 0.5f;
        core::at(s, i) = core::at(p.extent_, i);
    }
    std::array<maths::Vec4, 3> lo{};
    std::array<maths::Vec4, 3> hi{};
    for (int i = 0; i < 3; ++i) {
        const maths::Vec4& at = core::at(p.at_point_, i);
        core::at(lo, i).x = current_round(current_round(current_round(static_cast<double>(at.x) - core::at(core::at(hb, i), 0)) - core::at(s, i)) - core::at(hr, i));
        core::at(lo, i).y = current_round(current_round(current_round(static_cast<double>(at.y) - core::at(core::at(hb, i), 1)) - core::at(s, i)) - core::at(hr, i));
        core::at(lo, i).z = current_round(current_round(current_round(static_cast<double>(at.z) - core::at(core::at(hb, i), 2)) - core::at(s, i)) - core::at(hr, i));
        core::at(lo, i).w = current_round(current_round(current_round(static_cast<double>(core::at(s, i)) - core::at(hr, i)) - core::at(s, i)) - core::at(hr, i));
    }
    {
        const maths::Vec4& at = core::at(p.at_point_, 0);
        core::at(hi, 0).x =
            current_round((static_cast<double>(current_round(static_cast<double>(core::at(core::at(hb, 0), 0)) + at.x)) + core::at(s, 0)) + core::at(hr, 0));
        core::at(hi, 0).y = current_round(((static_cast<double>(at.y) + core::at(core::at(hb, 0), 1)) + core::at(s, 0)) + core::at(hr, 0));
        core::at(hi, 0).z = current_round(static_cast<double>(current_round(
                                    core::at(s, 0) + static_cast<double>(current_round(static_cast<double>(at.z) + core::at(core::at(hb, 0), 2))))) +
                                core::at(hr, 0));
        core::at(hi, 0).w = current_round(static_cast<double>(current_round(
                                    core::at(s, 0) + static_cast<double>(current_round(static_cast<double>(core::at(hr, 0)) + core::at(s, 0))))) +
                                core::at(hr, 0));
    }
    for (int i = 1; i < 3; ++i) {
        const maths::Vec4& at = core::at(p.at_point_, i);
        core::at(hi, i).x = current_round(current_round(current_round(static_cast<double>(core::at(core::at(hb, i), 0)) + at.x) + core::at(s, i)) + core::at(hr, i));
        core::at(hi, i).y = current_round(current_round(current_round(static_cast<double>(at.y) + core::at(core::at(hb, i), 1)) + core::at(s, i)) + core::at(hr, i));
        core::at(hi, i).z =
            current_round((core::at(s, i) + static_cast<double>(current_round(static_cast<double>(at.z) + core::at(core::at(hb, i), 2)))) + core::at(hr, i));
        core::at(hi, i).w = current_round(static_cast<double>(current_round(core::at(s, i) + (static_cast<double>(core::at(hr, i)) + core::at(s, i)))) + core::at(hr, i));
    }
    maths::Vec4 low{}, high{}, curve{};
    low = *min_of(&core::at(lo, 0), &core::at(lo, 2), &curve);
    high = *max_of(&core::at(hi, 0), &core::at(hi, 2), &curve);
    if (p.use_mid_) { // (the paths go through the mid: their extremes on the way)
        const float life = p.lifetime_;
        const float mid_time = current_round(static_cast<double>(life) * p.mid_percent_ * 0.01f);
        maths::Vec4 extreme{};
        curve_extremes(&extreme, core::at(lo, 0), core::at(lo, 1), core::at(lo, 2), mid_time, life);
        low = *min_of(&low, &extreme, &curve);
        curve_extremes(&extreme, core::at(hi, 0), core::at(hi, 1), core::at(hi, 2), mid_time, life);
        high = *max_of(&high, &extreme, &curve);
    }
    extent_box_.min = low;
    extent_box_.max = high;
    // the sphere about the box
    const float half_x = current_round((static_cast<double>(high.x) - low.x) * 0.5f);
    const double half_y = (static_cast<double>(high.y) - low.y) * 0.5f;
    const double half_z = static_cast<double>(current_round(static_cast<double>(high.z) - low.z)) * 0.5f;
    bound_ball_.w = low.w; // (overwritten below, as the original)
    bound_ball_.z = current_round(half_z + low.z);
    bound_ball_.x = current_round(static_cast<double>(half_x) + low.x);
    bound_ball_.y = current_round(half_y + low.y);
    bound_ball_.w =
        current_round(std::sqrt((static_cast<double>(half_x) * half_x + half_z * half_z) + half_y * half_y));
}

// 0x00496030
void Sparkle::change_place(int which, const maths::Vec4* pos) { core::at(args_.at_point_, which) = *pos; }

// 0x00496160
void Sparkle::set_box(int which, const maths::Vec4* box) { core::at(args_.box_, which) = *box; }

// 0x00496060
void Sparkle::spin_by(int which, const maths::Mat4* matrix)
{
    const maths::Vec4 start = core::at(args_.at_point_, 0);
    maths::Vec4& pos = core::at(args_.at_point_, which);
    pos.x -= start.x;
    pos.y -= start.y;
    pos.z -= start.z;
    pos.w -= start.w;
    maths::Vec4 turned{};
    pos = matrix->xform(turned, pos);
    maths::Vec4& box = core::at(args_.box_, which);
    box = matrix->xform(turned, box);
    pos.x = start.x + pos.x;
    pos.y = start.y + pos.y;
    pos.z = start.z + pos.z;
    pos.w = start.w + pos.w;
    args_.orientation_turn_ = *matrix;
}

// 0x00496190
void Sparkle::change_colour(const Rgba* rgba)
{
    if (!rgba) {
        on_colour_changed();
        return;
    }
    if (args_.flag158_) {
        for (int i = 0; i < 4; ++i)
            core::at(args_.colour_scale_, i) = static_cast<float>(core::at(*rgba, i)) * kByteToUnit;
        on_colour_changed();
    }
}

// 0x00496210
void Sparkle::set_colour_scale(std::span<const float, 4> rgba)
{
    if (args_.flag158_) {
        for (int i = 0; i < 4; ++i)
            core::at(args_.colour_scale_, i) = core::at(rgba, i);
        on_colour_changed();
    }
}

// 0x004963B0
void Sparkle::set_emit_script(uint32_t script, uint32_t object_id)
{
    if (args_.emit_script_)
        script::stop_scripts_by_tag(args_.emit_script_);
    args_.emit_script_ = script;
    args_.entity_key_ = object_id;
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(kObjectIdParam, args_.entity_key_); // (not checked for null, as the original)
    script::spawn_script(args_.emit_script_, params, 0, nullptr, 0, 0, false, false, false, false);
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
    for (float& c : shade_)
        c = 1.0f;
}

// 0x004967D0
EffectsDirector::~EffectsDirector()
{
    for (int pass = 0; pass < 2; ++pass)
        systems_.erase(systems_.first(), systems_.head);
    memory::dealloc(systems_.head);
}

// 0x00496510
Sparkle* EffectsDirector::spawn_particle(const SparkleConfig* params)
{
    Sparkle* particle = create_system();
    if (particle) {
        particle->prepare_all(params);
        if (particle->args_.flag158_) {
            for (int i = 0; i < 4; ++i)
                core::at(particle->args_.colour_scale_, i) = core::at(shade_, i);
            particle->set_colour_scale(shade_);
        }
        systems_.push_back(particle);
    }
    return particle;
}

// 0x00496590
void EffectsDirector::retire_particle(Sparkle* particle)
{
    if (particle->args_.emit_script_) {
        script::stop_scripts_by_tag(particle->args_.emit_script_);
        particle->args_.emit_script_ = 0;
    }
    auto* node = systems_.first();
    if (node == systems_.head)
        return;
    while (node->value->args_.label_ != particle->args_.label_) {
        node = node->next;
        if (node == systems_.head)
            return;
    }
    gsl::owner<Sparkle*> found = memory::adopt<Sparkle*>(node->value);
    found->get_release();
    delete found;
    if (node != systems_.head) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        memory::dealloc(node);
        --systems_.size;
    }
}

// 0x00496640
void EffectsDirector::draw_particles()
{
    if (!systems_.size)
        return;
    for (const auto* node = systems_.first(); node != systems_.head;
         node = node->next)
        node->value->render(!is_game_halted());
}

// 0x00496680
void EffectsDirector::tick_particles()
{
    for (const auto* node = systems_.first(); node != systems_.head;
         node = node->next)
        node->value->tick_frame();
}

// 0x004966B0
void EffectsDirector::change_colour(std::span<const float, 4> rgba)
{
    for (const auto* node = systems_.first(); node != systems_.head;
         node = node->next)
        node->value->set_colour_scale(rgba);
    for (int i = 0; i < 4; ++i)
        core::at(shade_, i) = core::at(rgba, i);
}

// 0x00496700
void EffectsDirector::kill_every_particle()
{
    for (const auto* node = systems_.first(); node != systems_.head;
         node = node->next) {
        gsl::owner<Sparkle*> particle = memory::adopt<Sparkle*>(node->value);
        particle->get_release();
        delete particle;
    }
    systems_.erase(systems_.first(), systems_.head);
}

} // namespace render

