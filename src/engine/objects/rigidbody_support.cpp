// The rigid body component's code before its life and physics (engine/objects/rigidbody_component.h): the debug switch,
// the member function table and three of its members, the pushers' reach, Finalize, Sleep, the world points, the
// damping and the check for sleep; and the components' shared vtable slot 11 (dispatch_script_method).
//
// The original's arithmetic is x87 code at 53-bit precision: what it keeps on the FPU stack is a double here, what
// it stores to memory a float - each value rounded where the original rounds it, and sums keep its grouping.

#include <cmath>
#include <cstdint>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/sector.h"
#include "engine/maths/random.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/sound_component.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"

namespace objects {

std::array<script::MemberFunctionEntry, 13> g_rigid_body_member_functions{{
    {0xCC12CF87, 0xFFB39248}, {0x9599C10F, 0xFFB39248}, {0xCD5BA67B, 0xFFB39248}, {0xAE24DF9F, 0xFFB39248},
    {0xE8B73AB3, 0xFFB39248}, {0x8A3A6B5F, 0xFFB39248}, {0x39D708AC, 0xFFB39248}, {0x92F5DB9A, 0xFFB39248},
    {0x0A2BBB3A, 0xFFB39248}, {0x49F44585, 0xFFB39248}, {0xD4A0946B, 0xFFB39248}, {0x949BE8A9, 0xFFB39248},
    {0, 0xFFB39248}}};
namespace {
int g_next_collide_sound_time;

constexpr uint32_t kSound = 0x07713C7B;        // the sound component
constexpr uint32_t kFrames = 0x019176C5;       // frames
constexpr uint32_t kFrame = 0x4A07C332;        // frame
constexpr float kFrameSeconds = 1.0f / 60;     // 0x006463B8
constexpr float kSettledDistanceSqr = 2304.0f; // 0x006463DC (48 squared)
constexpr float kDieAfterSettling = 1.5f;      // (an immediate)

double get_wide(float value) { return value; }
float get_narrow(double value) { return static_cast<float>(value); }

// The debug drawing switches (rigidbody_physics.cpp reads them): the shapes, the details.
bool& draw_shape() { return g_rigid_body_draw_shape; }
bool& draw_details() { return g_rigid_body_draw_details; }

// The 8 objects rigid bodies feel (smart pointers, 0x00695150) and each one's extra reach (0x006939B0).
SharedRef<GameEntity>* get_pushers() { return g_rigid_body_pushers.data(); }
float* pusher_reach() { return g_rigid_body_pusher_reach.data(); }

// The matrix's element at byte offset `offset` from its start.
double get_m(const maths::Mat4& m, int offset)
{
    return *raw::field_at<const float>(&m, offset);
}

} // namespace

// 0x0042BAC0
bool cfunc_toggle_rigid_body_debug(script::Struct*, script::Script*)
{
    if (!draw_shape()) {
        draw_shape() = true;
    } else if (!draw_details()) {
        draw_details() = true;
    } else {
        draw_details() = false;
        draw_shape() = false;
    }
    return true;
}

// 0x0042BB00
void BodyDynamicsModule::publish_method_table()
{
    script::register_member_function_table(g_rigid_body_member_functions);
}

// 0x0042BB20
int ObjectModule::dispatch_script_method(const script::MemberFunctionEntry* entry, script::Struct* params,
                                         script::Script* script)
{
    if (entry->method_code_) {
        return entry->method_code_(this, entry->callsign_, params, script); // (this_adjustment_ is always 0)
    }
    return invoke_method_by_name(entry->callsign_, params, script);
}

// 0x0042BB60
bool BodyDynamicsModule::rigid_body_ignore_skater(script::Struct* params, script::Script*)
{
    params->fetch_float(0u, &push_cooldown_, 1);
    if (params->has_bare_flag(kFrames) || params->has_bare_flag(kFrame))
        push_cooldown_ = get_narrow(get_wide(push_cooldown_) * kFrameSeconds);
    return true;
}

// 0x0042BBB0
bool BodyDynamicsModule::rigid_body_disable(script::Struct*, script::Script*)
{
    cur_phase_ = kStateDisabled;
    return true;
}

// 0x0042BBD0
bool BodyDynamicsModule::rigid_body_reset(script::Struct* params, script::Script*)
{
    configure_from_data(params);
    return true;
}

// 0x0042BD60
void set_rigid_body_pusher_reach(GameEntity* object, float reach)
{
    for (int i = 7; i >= 0; --i) {
        if (get_pushers()[i].get_pull() == object) {
            pusher_reach()[i] = reach;
            return;
        }
    }
}

// 0x0042BD90
void BodyDynamicsModule::seal()
{
    audio_module_ = core::down_cast<AudioEmitterModule>(entity_->fetch_module(kSound));
    const maths::Mat4& m = basis_frame_;
    for (int i = 0; std::cmp_less(i, spring_count_); ++i) {
        Spring& spring = springs_[i];
        const maths::Vec4& p = spring.point;
        const double x =
            (get_wide(p.y) * get_m(m, 0x10) + get_wide(p.x) * get_m(m, 0x00)) + get_wide(p.z) * get_m(m, 0x20);
        const float y = get_narrow((get_wide(p.x) * get_m(m, 0x04) + get_wide(p.z) * get_m(m, 0x24)) +
                                   get_wide(p.y) * get_m(m, 0x14));
        const float z = get_narrow((get_wide(p.x) * get_m(m, 0x08) + get_wide(p.z) * get_m(m, 0x28)) +
                                   get_wide(p.y) * get_m(m, 0x18));
        spring.anchor.z = get_narrow(get_wide(z) + at_point_.z);
        spring.anchor.w = get_narrow(get_wide(p.w) + at_point_.w);
        spring.anchor.x = get_narrow(x + at_point_.x);
        spring.anchor.y = get_narrow(get_wide(y) + at_point_.y);
    }
}

// 0x0042BE90
void BodyDynamicsModule::sleep()
{
    if (settle_script_)
        entity_->spawn_script(settle_script_, callback_params_ptr_, 0, false)->tick_frame();
    vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    rot_vel_ = {0.0f, 0.0f, 0.0f, 1.0f};
    cur_phase_ = kStateAsleep;
}

// 0x0042BF00 (each array from its last element back)
void BodyDynamicsModule::update_world_points()
{
    const maths::Mat4& m = basis_frame_;
    for (int i = contact_count_ - 1; i >= 0; --i) {
        const maths::Vec4& p = contacts_[i];
        maths::Vec4& out = world_contact_points()[i];
        const double x =
            (get_wide(p.y) * get_m(m, 0x10) + get_wide(p.z) * get_m(m, 0x20)) + get_wide(p.x) * get_m(m, 0x00);
        const double y =
            (get_wide(p.x) * get_m(m, 0x04) + get_wide(p.z) * get_m(m, 0x24)) + get_wide(p.y) * get_m(m, 0x14);
        const double z =
            (get_wide(p.z) * get_m(m, 0x28) + get_wide(p.y) * get_m(m, 0x18)) + get_wide(p.x) * get_m(m, 0x08);
        out = {get_narrow(x), get_narrow(y), get_narrow(z), p.w};
    }
    for (int i = sphere_count_ - 1; i >= 0; --i) {
        const maths::Vec4& p = spheres_[i].center;
        maths::Vec4& out = world_sphere_centers()[i];
        const double x =
            (get_wide(p.y) * get_m(m, 0x10) + get_wide(p.z) * get_m(m, 0x20)) + get_wide(p.x) * get_m(m, 0x00);
        const double y =
            (get_wide(p.x) * get_m(m, 0x04) + get_wide(p.z) * get_m(m, 0x24)) + get_wide(p.y) * get_m(m, 0x14);
        const double z =
            (get_wide(p.x) * get_m(m, 0x08) + get_wide(p.z) * get_m(m, 0x28)) + get_wide(p.y) * get_m(m, 0x18);
        out = {get_narrow(x), get_narrow(y), get_narrow(z), p.w};
    }
    for (int i = spring_count_ - 1; i >= 0; --i) {
        const Spring& spring = springs_[i];
        if (!spring.active)
            continue;
        const maths::Vec4& p = spring.point;
        maths::Vec4& out = world_spring_points()[i];
        const double x =
            (get_wide(p.z) * get_m(m, 0x20) + get_wide(p.y) * get_m(m, 0x10)) + get_wide(p.x) * get_m(m, 0x00);
        const double y =
            (get_wide(p.z) * get_m(m, 0x24) + get_wide(p.y) * get_m(m, 0x14)) + get_wide(p.x) * get_m(m, 0x04);
        const double z =
            (get_wide(p.z) * get_m(m, 0x28) + get_wide(p.y) * get_m(m, 0x18)) + get_wide(p.x) * get_m(m, 0x08);
        out = {get_narrow(x), get_narrow(y), get_narrow(z), p.w};
    }
}

namespace {

// A velocity slowed by `drag` (a speed; 0: left alone): stopped (w 1) at or below it, else each component, w too,
// less its share (a not-a-number speed or drag: that).
void get_slow(maths::Vec4& v, float drag)
{
    if (drag == 0.0f)
        return;
    const double speed = std::sqrt((get_wide(v.x) * v.x + get_wide(v.y) * v.y) + get_wide(v.z) * v.z);
    if (speed <= drag) {
        v = {0.0f, 0.0f, 0.0f, 1.0f};
        return;
    }
    const double f = -(drag / speed);
    const double dx = f * v.x;
    const float dy = get_narrow(f * v.y);
    const float dz = get_narrow(f * v.z);
    const float dw = get_narrow(f * v.w);
    v.x = get_narrow(dx + v.x);
    v.y = get_narrow(get_wide(dy) + v.y);
    v.z = get_narrow(get_wide(dz) + v.z);
    v.w = get_narrow(get_wide(dw) + v.w);
}

// x * x + y * y + z * z, as the original sums it.
double magnitude_squared(const maths::Vec4& v)
{
    return (get_wide(v.x) * v.x + get_wide(v.y) * v.y) + get_wide(v.z) * v.z;
}

} // namespace

// 0x0042C6D0
void BodyDynamicsModule::damp()
{
    get_slow(vel_, unknown38_);
    get_slow(rot_vel_, unknown3c_);
}

// 0x0042C890 (the comparisons let a not-a-number speed through)
void BodyDynamicsModule::check_sleep()
{
    if (unknown191_ < 3)
        return;
    if (magnitude_squared(vel_) > linear_sleep_squared_)
        return;
    if (magnitude_squared(rot_vel_) > angular_sleep_squared_)
        return;
    sleep();
    if ((flag_bits_ & kFlagDieUponSettling) &&
        !(maths::get_distance_squared(at_point_, wake_pos_) < kSettledDistanceSqr))
        die_timer_ = kDieAfterSettling;
}

namespace {

constexpr float kSlow = 0.01f;            // 0x0064544C: a relative speed this small (each axis) is none
constexpr float kUpShare = 1.0f / 3;      // 0x006463D8: the vertical speed's share of the push
constexpr float kHardPush = 100.0f;       // 0x006463D4
constexpr float kHardPushAdd = 51.0f;     // 0x006463D0
constexpr float kMidPush = 50.0f;         // 0x006463CC
constexpr float kBelowFeet = -12.0f;      // 0x006463C8: where a push from below lands (y, from the centre)
constexpr float kTurnShare = 0.6f;        // 0x006463C4
constexpr float kSoundScale = 0.1f;       // 0x00645D74: the push's size to the collide sound's percent
constexpr uint32_t kPercent = 0x9E497FC6; // percent

// The pushers' height (0x006939D0: 77, set by configure_from_data) and the time the next collide sound of any rigid
// body may play (0x006939D8).
float pusher_height() { return g_rigid_body_pusher_height; }
int& next_collide_sound_time() { return g_next_collide_sound_time; }

// The scene's sector named `checksum` hidden (0x0048C770 the first scene - the name pushed as its argument slot,
// which 0x0049CB00 then takes as its own; neither rewritten yet).
void hide_sector(uint32_t checksum)
{
    render::WorldScene* scene = render::get_main_scene();
    void* sector = scene->look_up_sector(checksum);
    if (sector)
        static_cast<render::WorldCell*>(sector)->change_live(false);
}

// A mute delay: 50-149 % of `delay` milliseconds from now (the product divided unsigned).
int mute_until(int delay)
{
    const uint32_t scaled = static_cast<uint32_t>((maths::rnd(100) + 50) * delay) / 100u;
    return static_cast<int>(scaled + frame_clock::get_time());
}

} // namespace

// 0x0042C140: the first pusher (from the last place back) inside the skater collision radius - plus its reach - and
// within the band from (its height + the application radius + reach) below to (the application radius + reach)
// above, moving at it (the horizontal approach plus a third of the vertical speed, boosted), pushes it: an impulse
// up and away at the assent angle, applied at the application radius from the centre (y: the pusher's height, or
// its feet, or 12 below); then it wakes, ignores pushers for a while, runs CollideScript, hides its shadow sector
// and plays the collide sound (mute delays allowing). A pusher not moving relative to it (0.01 on each axis) ends
// the look at once - the pushers after it aren't felt.
void BodyDynamicsModule::feel_pushers()
{
    for (int i = 7; i >= 0; --i) {
        const GameEntity* pusher = get_pushers()[i].get_pull();
        if (!pusher)
            continue;
        const float reach = pusher_reach()[i];
        const maths::Vec4& at = pusher->at_point_;
        const double outer = get_wide(reach) + skater_radius_;
        const double dz = get_wide(at_point_.z) - at.z;
        const double dx = get_wide(at_point_.x) - at.x;
        if (outer * outer < dx * dx + dz * dz)
            continue;
        const float dy = get_narrow(get_wide(at.y) - at_point_.y);
        if (get_wide(reach) + skater_application_radius_ < dy)
            continue;
        if (-((get_wide(pusher_height()) + skater_application_radius_) + reach) > dy)
            continue;

        // Away from the pusher (horizontally), and how they move relative to each other.
        const float ex = get_narrow(get_wide(at_point_.x) - at.x);
        const double ez = get_wide(at_point_.z) - at.z;
        const double inverse = 1.0 / std::sqrt(ez * ez + get_wide(ex) * ex);
        const float nx = get_narrow(get_wide(ex) * inverse);
        const float nz = get_narrow(ez * inverse);
        const float rx = get_narrow(get_wide(pusher->vel_.x) - vel_.x);
        const double ry = get_wide(pusher->vel_.y) - vel_.y;
        const float rz = get_narrow(get_wide(pusher->vel_.z) - vel_.z);
        if (std::fabs(rx) <= kSlow && std::fabs(ry) <= kSlow && std::fabs(rz) <= kSlow)
            return;
        const double approach = (std::fabs(ry * kUpShare) + get_wide(rx) * nx) + get_wide(rz) * nz;
        if (approach <= 0.0)
            continue;
        double push = 0.0;
        if (approach > kHardPush)
            push = approach + kHardPushAdd;
        else if (approach > kMidPush)
            push = (approach + (approach - kMidPush)) + kMidPush;
        else
            push = approach + approach;

        // The direction: the relative motion's (horizontal, unit) plus the normal, made unit, tilted up.
        double h = std::sqrt(get_wide(rx) * rx + get_wide(rz) * rz);
        if (h == 0.0)
            h = kSlow;
        const double ih = 1.0 / h;
        const float ax = get_narrow(get_wide(rx) * ih + nx);
        const double az = get_wide(rz) * ih + nz;
        double g = std::sqrt(get_wide(ax) * ax + az * az);
        if (g == 0.0)
            g = kSlow;
        const double ig = 1.0 / g;
        float jz = get_narrow(az * ig);
        const float jx = get_narrow(get_wide(ax) * ig * skater_assent_cos_);
        jz = get_narrow(get_wide(jz) * skater_assent_cos_);
        const double size = push * skater_impulse_factor_;
        const maths::Vec4 impulse = {get_narrow(get_wide(jx) * size), get_narrow(get_wide(skater_assent_sin_) * size),
                                     get_narrow(get_wide(jz) * size), 0.0f};

        // Where it's applied (from the centre).
        double px = get_wide(at.x) - at_point_.x;
        float pz = get_narrow(get_wide(at.z) - at_point_.z);
        const double across = std::sqrt(px * px + get_wide(pz) * pz);
        if (across > 0.0) {
            const double scale = skater_application_radius_ / across;
            px = px * scale;
            pz = get_narrow(get_wide(pz) * scale);
        }
        double py = 0.0;
        if (dy > 0.0f)
            py = get_wide(at.y) - at_point_.y;
        else if (-pusher_height() > dy)
            py = (get_wide(pusher_height()) + at.y) - at_point_.y;
        else
            py = kBelowFeet;

        vel_.x = get_narrow(get_wide(impulse.x) + vel_.x);
        vel_.y = get_narrow(get_wide(impulse.y) + vel_.y);
        vel_.z = get_narrow(get_wide(impulse.z) + vel_.z);
        vel_.w = get_narrow(size + vel_.w);
        const float tx = get_narrow(get_wide(impulse.z) * py - get_wide(pz) * impulse.y);
        const float ty = get_narrow(get_wide(pz) * impulse.x - get_wide(impulse.z) * px);
        const double tz = get_wide(impulse.y) * px - py * impulse.x;
        const double turn = (get_wide(skater_rotation_factor_) * mass_over_moment_) * kTurnShare;
        const float wx = get_narrow(get_wide(tx) * turn);
        const float wy = get_narrow(get_wide(ty) * turn);
        const float wz = get_narrow(tz * turn);
        rot_vel_.x = get_narrow(get_wide(wx) + rot_vel_.x);
        rot_vel_.y = get_narrow(get_wide(wy) + rot_vel_.y);
        rot_vel_.z = get_narrow(get_wide(wz) + rot_vel_.z);
        rot_vel_.w = get_narrow(turn * 0.0f + rot_vel_.w);

        wake();
        push_cooldown_ = push_cooldown_time_;
        if (collide_script_)
            entity_->spawn_script(collide_script_, callback_params_ptr_, 0, false)->tick_frame();
        hide_sector(checksums::fold_text_into_checksum(entity_->ident_, "_Shadow"));
        const int now = static_cast<int>(frame_clock::get_time());
        if (now - static_cast<int>(unknownf0_) > 0 && now - next_collide_sound_time() > 0 && audio_module_ &&
            collide_sound_ptr_) {
            const double length = std::sqrt((get_wide(impulse.x) * impulse.x + get_wide(impulse.z) * impulse.z) +
                                            get_wide(impulse.y) * impulse.y);
            collide_sound_ptr_->push_float(kPercent, get_narrow(length * kSoundScale));
            audio_module_->emit_sound(collide_sound_ptr_);
            unknownf0_ = static_cast<uint32_t>(mute_until(impact_quiet_ms_));
            next_collide_sound_time() = mute_until(global_collide_mute_delay_);
        }
        return;
    }
}

} // namespace objects

