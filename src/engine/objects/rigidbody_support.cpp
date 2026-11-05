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

#include "base/core/checksums.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/sector.h"
#include "base/maths/random.h"
#include "base/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/sound_component.h"
#include "engine/script/cscript.h"
#include "engine/script/host.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "engine/sys/stopwatch.h"
#include "base/core/checked_index.h"
#include "base/core/down_cast.h"
#include "engine/objects/objects_shared.h"

namespace objects {
namespace {
constexpr int kMatrixRowBytes = 16;                // four floats
constexpr int kLastContact = 7;
constexpr uint32_t kRigidBodyIgnoreSkater = 0xCC12CF87; // RigidBody_IgnoreSkater
constexpr uint32_t kRigidBody = 0xFFB39248; // RigidBody
constexpr uint32_t kRigidBodyWake = 0x9599C10F; // RigidBody_Wake
constexpr uint32_t kRigidBodySleep = 0xCD5BA67B; // RigidBody_Sleep
constexpr uint32_t kRigidBodyKick = 0xAE24DF9F; // RigidBody_Kick
constexpr uint32_t kRigidBodySetVelocities = 0xE8B73AB3; // RigidBody_SetVelocities
constexpr uint32_t kRigidbodyDisable = 0x8A3A6B5F; // rigidbody_disable (letter case unknown)
constexpr uint32_t kRigidBodyEnable = 0x39D708AC; // RigidBody_Enable
constexpr uint32_t kRigidBodyReset = 0x92F5DB9A; // RigidBody_Reset
constexpr uint32_t kRigidbodyDisablePlayerCollision = 0x0A2BBB3A; // Rigidbody_DisablePlayerCollision
constexpr uint32_t kRigidBodyEnablePlayerCollision = 0x49F44585; // RigidBody_EnablePlayerCollision
constexpr uint32_t kRigidbodyGetCenterOfMassPos = 0xD4A0946B; // Rigidbody_GetCenterOfMassPos (letter case unknown)
constexpr uint32_t kRigidbodyMatchVelocityTo = 0x949BE8A9; // Rigidbody_MatchVelocityTo
} // namespace

std::array<script::MemberFunctionEntry, kRigidBodyMemberFunctionCount>& rigid_body_member_functions()
{
    static std::array<script::MemberFunctionEntry, kRigidBodyMemberFunctionCount> rigid_body_member_functions{{
        {.callsign_ = kRigidBodyIgnoreSkater, .owner_kind_ = kRigidBody}, {.callsign_ = kRigidBodyWake, .owner_kind_ = kRigidBody}, {.callsign_ = kRigidBodySleep, .owner_kind_ = kRigidBody}, {.callsign_ = kRigidBodyKick, .owner_kind_ = kRigidBody},
        {.callsign_ = kRigidBodySetVelocities, .owner_kind_ = kRigidBody}, {.callsign_ = kRigidbodyDisable, .owner_kind_ = kRigidBody}, {.callsign_ = kRigidBodyEnable, .owner_kind_ = kRigidBody}, {.callsign_ = kRigidBodyReset, .owner_kind_ = kRigidBody},
        {.callsign_ = kRigidbodyDisablePlayerCollision, .owner_kind_ = kRigidBody}, {.callsign_ = kRigidBodyEnablePlayerCollision, .owner_kind_ = kRigidBody}, {.callsign_ = kRigidbodyGetCenterOfMassPos, .owner_kind_ = kRigidBody}, {.callsign_ = kRigidbodyMatchVelocityTo, .owner_kind_ = kRigidBody},
        {.callsign_ = 0, .owner_kind_ = kRigidBody}}};
    return rigid_body_member_functions;
}

namespace {

constexpr uint32_t kSound = 0x07713C7B;        // the sound component
constexpr uint32_t kFrames = 0x019176C5;       // frames
constexpr uint32_t kFrame = 0x4A07C332;        // frame
constexpr float kFrameSeconds = 1.0F / 60;     // 0x006463B8
constexpr float kSettledDistanceSqr = 2304.0F; // 0x006463DC (48 squared)
constexpr float kDieAfterSettling = 1.5F;      // (an immediate)

double get_wide(float value) { return value; }
float get_narrow(double value) { return static_cast<float>(value); }

// The debug drawing switches (rigidbody_physics.cpp reads them): the shapes, the details.

// The 8 objects rigid bodies feel (smart pointers, 0x00695150) and each one's extra reach (0x006939B0).

// The matrix's element at byte offset `offset` from its start (the original's: row offset / 16, column 4 bytes each).
double get_m(const maths::Mat4& matrix, int offset)
{
    return core::at(matrix.row, offset / kMatrixRowBytes)[(offset % kMatrixRowBytes) / 4];
}

} // namespace

// 0x0042BAC0
bool cfunc_toggle_rigid_body_debug(script::Struct* /*params*/, script::Context /*context*/)
{
    if (!objects_shared().rigid_body_draw_shape()) {
        objects_shared().set_rigid_body_draw_shape(true);
    } else if (!objects_shared().rigid_body_draw_details()) {
        objects_shared().set_rigid_body_draw_details(true);
    } else {
        objects_shared().set_rigid_body_draw_details(false);
        objects_shared().set_rigid_body_draw_shape(false);
    }
    return true;
}

// 0x0042BB00
void BodyDynamicsModule::publish_method_table()
{
    script::register_member_function_table(rigid_body_member_functions());
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
bool BodyDynamicsModule::rigid_body_ignore_skater(script::Struct* params, script::Script* /*script*/)
{
    params->fetch_float(0U, &push_cooldown_, 1);
    if (params->has_bare_flag(kFrames) || params->has_bare_flag(kFrame)) {
        push_cooldown_ = get_narrow(get_wide(push_cooldown_) * kFrameSeconds);
    }
    return true;
}

// 0x0042BBB0
bool BodyDynamicsModule::rigid_body_disable(script::Struct* /*params*/, script::Script* /*script*/)
{
    cur_phase_ = BodyDynamicsModule::State::kDisabled;
    return true;
}

// 0x0042BBD0
bool BodyDynamicsModule::rigid_body_reset(script::Struct* params, script::Script* /*script*/)
{
    configure_from_data(params);
    return true;
}

// 0x0042BD60
void set_rigid_body_pusher_reach(GameEntity* object, float reach)
{
    for (int i = kLastContact; i >= 0; --i) {
        if (core::at(objects_shared().rigid_body_pushers(), i).get_pull() == object) {
            core::at(objects_shared().rigid_body_pusher_reach(), i) = reach;
            return;
        }
    }
}

// 0x0042BD90
void BodyDynamicsModule::seal()
{
    audio_module_ = core::down_cast<AudioEmitterModule>(entity()->fetch_module(kSound));
    const maths::Mat4& basis = basis_frame_;
    for (int i = 0; std::cmp_less(i, spring_count_); ++i) {
        Spring& spring = core::at(spring_run(), i);
        const maths::Vec4& point = spring.point;
        const double x = ((get_wide(point.y) * get_m(basis, 0x10)) + (get_wide(point.x) * get_m(basis, 0x00))) +
                         (get_wide(point.z) * get_m(basis, 0x20));
        const float y =
            get_narrow(((get_wide(point.x) * get_m(basis, 0x04)) + (get_wide(point.z) * get_m(basis, 0x24))) +
                       (get_wide(point.y) * get_m(basis, 0x14)));
        const float z =
            get_narrow(((get_wide(point.x) * get_m(basis, 0x08)) + (get_wide(point.z) * get_m(basis, 0x28))) +
                       (get_wide(point.y) * get_m(basis, 0x18)));
        spring.anchor.z = get_narrow(get_wide(z) + at_point_.z);
        spring.anchor.w = get_narrow(get_wide(point.w) + at_point_.w);
        spring.anchor.x = get_narrow(x + at_point_.x);
        spring.anchor.y = get_narrow(get_wide(y) + at_point_.y);
    }
}

// 0x0042BE90
void BodyDynamicsModule::sleep()
{
    if (settle_script_ != 0U) {
        script::host().tick_frame(entity()->spawn_script(settle_script_, callback_params_ptr_, 0, false));
    }
    vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    rot_vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    cur_phase_ = BodyDynamicsModule::State::kAsleep;
}

// 0x0042BF00 (each array from its last element back)
void BodyDynamicsModule::update_world_points()
{
    const maths::Mat4& basis = basis_frame_;
    for (int i = contact_count_ - 1; i >= 0; --i) {
        const maths::Vec4& point = core::at(contact_run(), i);
        maths::Vec4& out = core::at(world_contact_points(), i);
        const double x = ((get_wide(point.y) * get_m(basis, 0x10)) + (get_wide(point.z) * get_m(basis, 0x20))) +
                         (get_wide(point.x) * get_m(basis, 0x00));
        const double y = ((get_wide(point.x) * get_m(basis, 0x04)) + (get_wide(point.z) * get_m(basis, 0x24))) +
                         (get_wide(point.y) * get_m(basis, 0x14));
        const double z = ((get_wide(point.z) * get_m(basis, 0x28)) + (get_wide(point.y) * get_m(basis, 0x18))) +
                         (get_wide(point.x) * get_m(basis, 0x08));
        out = {.x = get_narrow(x), .y = get_narrow(y), .z = get_narrow(z), .w = point.w};
    }
    for (int i = sphere_count_ - 1; i >= 0; --i) {
        const maths::Vec4& point = core::at(sphere_run(), i).center;
        maths::Vec4& out = core::at(world_sphere_centers(), i);
        const double x = ((get_wide(point.y) * get_m(basis, 0x10)) + (get_wide(point.z) * get_m(basis, 0x20))) +
                         (get_wide(point.x) * get_m(basis, 0x00));
        const double y = ((get_wide(point.x) * get_m(basis, 0x04)) + (get_wide(point.z) * get_m(basis, 0x24))) +
                         (get_wide(point.y) * get_m(basis, 0x14));
        const double z = ((get_wide(point.x) * get_m(basis, 0x08)) + (get_wide(point.z) * get_m(basis, 0x28))) +
                         (get_wide(point.y) * get_m(basis, 0x18));
        out = {.x = get_narrow(x), .y = get_narrow(y), .z = get_narrow(z), .w = point.w};
    }
    for (int i = spring_count_ - 1; i >= 0; --i) {
        const Spring& spring = core::at(spring_run(), i);
        if (!spring.active) {
            continue;
        }
        const maths::Vec4& point = spring.point;
        maths::Vec4& out = core::at(world_spring_points(), i);
        const double x = ((get_wide(point.z) * get_m(basis, 0x20)) + (get_wide(point.y) * get_m(basis, 0x10))) +
                         (get_wide(point.x) * get_m(basis, 0x00));
        const double y = ((get_wide(point.z) * get_m(basis, 0x24)) + (get_wide(point.y) * get_m(basis, 0x14))) +
                         (get_wide(point.x) * get_m(basis, 0x04));
        const double z = ((get_wide(point.z) * get_m(basis, 0x28)) + (get_wide(point.y) * get_m(basis, 0x18))) +
                         (get_wide(point.x) * get_m(basis, 0x08));
        out = {.x = get_narrow(x), .y = get_narrow(y), .z = get_narrow(z), .w = point.w};
    }
}

namespace {

// A velocity slowed by `drag` (a speed; 0: left alone): stopped (w 1) at or below it, else each component, w too,
// less its share (a not-a-number speed or drag: that).
void get_slow(maths::Vec4& v, float drag)
{
    if (drag == 0.0F) {
        return;
    }
    const double speed = std::sqrt(((get_wide(v.x) * v.x) + (get_wide(v.y) * v.y)) + (get_wide(v.z) * v.z));
    if (speed <= drag) {
        v = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        return;
    }
    const double scale = -(drag / speed);
    const double dx = scale * v.x;
    const float dy = get_narrow(scale * v.y);
    const float dz = get_narrow(scale * v.z);
    const float dw = get_narrow(scale * v.w);
    v.x = get_narrow(dx + v.x);
    v.y = get_narrow(get_wide(dy) + v.y);
    v.z = get_narrow(get_wide(dz) + v.z);
    v.w = get_narrow(get_wide(dw) + v.w);
}

// x * x + y * y + z * z, as the original sums it.
double magnitude_squared(const maths::Vec4& v)
{
    return ((get_wide(v.x) * v.x) + (get_wide(v.y) * v.y)) + (get_wide(v.z) * v.z);
}

} // namespace

// 0x0042C6D0
void BodyDynamicsModule::damp()
{
    get_slow(vel_, damping_.linear);
    get_slow(rot_vel_, damping_.angular);
}

// 0x0042C890 (the comparisons let a not-a-number speed through)
void BodyDynamicsModule::check_sleep()
{
    if (unknown191_ < 3) {
        return;
    }
    if (magnitude_squared(vel_) > linear_sleep_squared_) {
        return;
    }
    if (magnitude_squared(rot_vel_) > angular_sleep_squared_) {
        return;
    }
    sleep();
    if (((flag_bits_ & kFlagDieUponSettling) != 0U) &&
        !(maths::get_distance_squared(at_point_, wake_pos_) < kSettledDistanceSqr)) {
        die_timer_ = kDieAfterSettling;
    }
}

namespace {

constexpr float kSlow = 0.01F;            // 0x0064544C: a relative speed this small (each axis) is none
constexpr float kUpShare = 1.0F / 3;      // 0x006463D8: the vertical speed's share of the push
constexpr float kHardPush = 100.0F;       // 0x006463D4
constexpr float kHardPushAdd = 51.0F;     // 0x006463D0
constexpr float kMidPush = 50.0F;         // 0x006463CC
constexpr float kBelowFeet = -12.0F;      // 0x006463C8: where a push from below lands (y, from the centre)
constexpr float kTurnShare = 0.6F;        // 0x006463C4
constexpr float kSoundScale = 0.1F;       // 0x00645D74: the push's size to the collide sound's percent
constexpr uint32_t kPercent = 0x9E497FC6; // percent

// The pushers' height (0x006939D0: 77, set by configure_from_data) and the time the next collide sound of any rigid
// body may play (0x006939D8).

int& next_collide_sound_time()
{
    static int next_collide_sound_time{};
    return next_collide_sound_time;
}

// The scene's sector named `checksum` hidden (0x0048C770 the first scene - the name pushed as its argument slot,
// which 0x0049CB00 then takes as its own; neither rewritten yet).
void hide_sector(uint32_t checksum)
{
    render::WorldScene* scene = render::get_main_scene();
    void* sector = scene->look_up_sector(checksum);
    if (sector) {
        static_cast<render::WorldCell*>(sector)->change_live(false);
    }
}

// A mute delay: 50-149 % of `delay` milliseconds from now (the product divided unsigned).
int mute_until(int delay)
{
    const uint32_t scaled = static_cast<uint32_t>((maths::rnd(100) + 50) * delay) / 100U;
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
    for (int i = kLastContact; i >= 0; --i) {
        const GameEntity* pusher = core::at(objects_shared().rigid_body_pushers(), i).get_pull();
        if (!pusher) {
            continue;
        }
        const float reach = core::at(objects_shared().rigid_body_pusher_reach(), i);
        const maths::Vec4& at = pusher->at_point();
        const double outer = get_wide(reach) + skater_radius_;
        const double dz = get_wide(at_point_.z) - at.z;
        const double dx = get_wide(at_point_.x) - at.x;
        if (outer * outer < (dx * dx) + (dz * dz)) {
            continue;
        }
        const float dy = get_narrow(get_wide(at.y) - at_point_.y);
        if (get_wide(reach) + skater_application_radius_ < dy) {
            continue;
        }
        if (-((get_wide(objects_shared().rigid_body_pusher_height()) + skater_application_radius_) + reach) > dy) {
            continue;
        }

        // Away from the pusher (horizontally), and how they move relative to each other.
        const float ex = get_narrow(get_wide(at_point_.x) - at.x);
        const double ez = get_wide(at_point_.z) - at.z;
        const double inverse = 1.0 / std::sqrt((ez * ez) + (get_wide(ex) * ex));
        const float nx = get_narrow(get_wide(ex) * inverse);
        const float nz = get_narrow(ez * inverse);
        const float rx = get_narrow(get_wide(pusher->vel().x) - vel_.x);
        const double ry = get_wide(pusher->vel().y) - vel_.y;
        const float rz = get_narrow(get_wide(pusher->vel().z) - vel_.z);
        if (std::fabs(rx) <= kSlow && std::fabs(ry) <= kSlow && std::fabs(rz) <= kSlow) {
            return;
        }
        const double approach = (std::fabs(ry * kUpShare) + (get_wide(rx) * nx)) + (get_wide(rz) * nz);
        if (approach <= 0.0) {
            continue;
        }
        double push = 0.0;
        if (approach > kHardPush) {
            push = approach + kHardPushAdd;
        } else if (approach > kMidPush) {
            push = (approach + (approach - kMidPush)) + kMidPush;
        } else {
            push = approach + approach;
        }

        // The direction: the relative motion's (horizontal, unit) plus the normal, made unit, tilted up.
        double horizontal_r = std::sqrt((get_wide(rx) * rx) + (get_wide(rz) * rz));
        if (horizontal_r == 0.0) {
            horizontal_r = kSlow;
        }
        const double ih = 1.0 / horizontal_r;
        const float ax = get_narrow((get_wide(rx) * ih) + nx);
        const double az = (get_wide(rz) * ih) + nz;
        double horizontal_a = std::sqrt((get_wide(ax) * ax) + (az * az));
        if (horizontal_a == 0.0) {
            horizontal_a = kSlow;
        }
        const double ig = 1.0 / horizontal_a;
        float jz = get_narrow(az * ig);
        const float jx = get_narrow(get_wide(ax) * ig * skater_assent_cos_);
        jz = get_narrow(get_wide(jz) * skater_assent_cos_);
        const double size = push * skater_impulse_factor_;
        const maths::Vec4 impulse = {.x = get_narrow(get_wide(jx) * size),
                                     .y = get_narrow(get_wide(skater_assent_sin_) * size),
                                     .z = get_narrow(get_wide(jz) * size),
                                     .w = 0.0F};

        // Where it's applied (from the centre).
        double px = get_wide(at.x) - at_point_.x;
        float pz = get_narrow(get_wide(at.z) - at_point_.z);
        const double across = std::sqrt((px * px) + (get_wide(pz) * pz));
        if (across > 0.0) {
            const double scale = skater_application_radius_ / across;
            px = px * scale;
            pz = get_narrow(get_wide(pz) * scale);
        }
        double py = 0.0;
        if (dy > 0.0F) {
            py = get_wide(at.y) - at_point_.y;
        } else if (-objects_shared().rigid_body_pusher_height() > dy) {
            py = (get_wide(objects_shared().rigid_body_pusher_height()) + at.y) - at_point_.y;
        } else {
            py = kBelowFeet;
        }

        vel_.x = get_narrow(get_wide(impulse.x) + vel_.x);
        vel_.y = get_narrow(get_wide(impulse.y) + vel_.y);
        vel_.z = get_narrow(get_wide(impulse.z) + vel_.z);
        vel_.w = get_narrow(size + vel_.w);
        const float tx = get_narrow((get_wide(impulse.z) * py) - (get_wide(pz) * impulse.y));
        const float ty = get_narrow((get_wide(pz) * impulse.x) - (get_wide(impulse.z) * px));
        const double tz = (get_wide(impulse.y) * px) - (py * impulse.x);
        const double turn = (get_wide(skater_rotation_factor_) * mass_over_moment_) * kTurnShare;
        const float wx = get_narrow(get_wide(tx) * turn);
        const float wy = get_narrow(get_wide(ty) * turn);
        const float wz = get_narrow(tz * turn);
        rot_vel_.x = get_narrow(get_wide(wx) + rot_vel_.x);
        rot_vel_.y = get_narrow(get_wide(wy) + rot_vel_.y);
        rot_vel_.z = get_narrow(get_wide(wz) + rot_vel_.z);
        rot_vel_.w = get_narrow((turn * 0.0F) + rot_vel_.w);

        wake();
        push_cooldown_ = push_cooldown_time_;
        if (collide_script_ != 0U) {
            script::host().tick_frame(entity()->spawn_script(collide_script_, callback_params_ptr_, 0, false));
        }
        hide_sector(checksums::fold_text_into_checksum(entity()->ident(), "_Shadow"));
        const int now = static_cast<int>(frame_clock::get_time());
        if (now - static_cast<int>(unknownf0_) > 0 && now - next_collide_sound_time() > 0 && audio_module_ &&
            collide_sound_ptr_) {
            const double length = std::sqrt(((get_wide(impulse.x) * impulse.x) + (get_wide(impulse.z) * impulse.z)) +
                                            (get_wide(impulse.y) * impulse.y));
            collide_sound_ptr_->push_float(checksums::Name{kPercent}, get_narrow(length * kSoundScale));
            audio_module_->emit_sound(collide_sound_ptr_);
            unknownf0_ = static_cast<uint32_t>(mute_until(impact_quiet_ms_));
            next_collide_sound_time() = mute_until(global_collide_mute_delay_);
        }
        return;
    }
}

} // namespace objects

