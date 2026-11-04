// The rigid body component's physics: the frame's update, integration, collisions with the world, contacts and
// springs (engine/objects/rigidbody_component.h).
//
// The original's arithmetic is x87 code at 53-bit precision: what it keeps on the FPU stack is a double here, what
// it stores to memory a float - each value is rounded where the original rounds it, and sums keep its grouping.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <gsl/assert>

#include "engine/gfx/debug_lines.h"
#include "base/maths/maths.h"
#include "base/maths/quat_rotation.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/objects/sound_component.h"
#include "engine/script/host.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/sys/stopwatch.h"
#include "base/core/checked_index.h"
#include "engine/objects/objects_shared.h"
#include "base/core/checksums.h"
#include "base/maths/vec4.h"

namespace objects {

namespace {
constexpr int kSphereSegments = 12;
constexpr float kMarkerSize = 36.0F;
constexpr float kSpringMarkerSize = 24.0F;
constexpr int kVoiceLimit = 48;
constexpr int kVoiceReserve = 12;                  // voices left free for other sounds
constexpr uint32_t kAsleepColour = 0xFFC80000; // ARGB: a sleeping body
constexpr uint32_t kPushedColour = 0xFF00C800; // ARGB: an awake body that was just pushed
constexpr uint32_t kAwakeColour = 0xFF0000C8; // ARGB: an awake body
constexpr uint32_t kDisabledColour = 0xFF640064; // ARGB: a disabled body
constexpr uint32_t kDetailMarkerColour = 0xFFC800C8; // ARGB: the body marker
constexpr uint32_t kSpringPointColour = 0xFFC86400; // ARGB: a spring point
constexpr uint32_t kSpringAnchorColour = 0xFFC80064; // ARGB: a spring anchor
constexpr uint32_t kBouncePos = 0x43F454D0; // bouncepos (letter case unknown)
constexpr uint32_t kPercent = 0x9E497FC6; // Percent
constexpr uint32_t kImpulseLineColour = 0xFF00FFFF; // ARGB: the impulse lines

constexpr float kZero = 0.0F;               // 0x0066D520 (rotational w changes by 0 * mass_over_moment)
constexpr float kSpringMinSpeed = 0.1F;     // 0x00645D74
constexpr float kSpringScale = -10.0F;      // 0x006463E0
constexpr float kHalf = 0.5F;               // 0x006453BC
constexpr float kTurnRate = -0.5F;          // 0x0064642C
constexpr float kMinLength = 0.001F;        // 0x006461D8
constexpr float kHitBackOff = 6.0F;         // 0x00646284
constexpr float kDebugImpulseScale = 60.0F; // 0x00645EA0
constexpr int kMaxRounds = 20;
constexpr int kMaxSteps = 5;

double get_wide(float value) { return value; }
float get_narrow(double value) { return static_cast<float>(value); }
// Two floats added / subtracted on the x87 (at double precision, then stored: rounded twice).
float append_one(float a, float b) { return get_narrow(get_wide(a) + b); }
float get_sub(float a, float b) { return get_narrow(get_wide(a) - b); }

// Debug drawing switches (0x006939D4: the shape each frame; 0x006939D5: the centre, the contacts' pushes).

// Quaternions: `quat` times `other`, in place (0x00420140); the matrix of one.
void quat_multiply(maths::QuatRotation* quat, const maths::QuatRotation* other) { *quat *= *other; }
void quat_to_matrix(const maths::QuatRotation* quat, maths::Mat4* matrix) { quat->fetch_mat(matrix); }

void flag_as_dead(GameEntity* object) { object->flag_as_dead(); }

// The matrix's element at row `r`, column `c`.
double get_at(const maths::Mat4& matrix, int row_index, int column) { return core::at(matrix.row, row_index)[column]; }

} // namespace

// 0x0042CEE0: 1 / (n . (n + (m (r x n)) x r)) - m the mass over the moment.
double BodyDynamicsModule::impulse_scale(ImpulseAt impulse_at) const
{
    const auto* const normal = impulse_at.normal;
    const auto* const offset = impulse_at.offset;
    const maths::Vec4& normal_ref = *normal;
    const maths::Vec4& arm = *offset;
    const double inverse_moment = mass_over_moment_;
    // a = m (r x n): x rounded, y and z not (z from its rounded cross product)
    const float ax = get_narrow(((get_wide(arm.y) * normal_ref.z) - (get_wide(arm.z) * normal_ref.y)) * inverse_moment);
    const double ay = ((get_wide(normal_ref.x) * arm.z) - (get_wide(arm.x) * normal_ref.z)) * inverse_moment;
    const double az = get_narrow((get_wide(arm.x) * normal_ref.y) - (get_wide(normal_ref.x) * arm.y)) * inverse_moment;
    // b = a x r, plus n
    const float sx = get_narrow(get_narrow((ay * arm.z) - (az * arm.y)) + get_wide(normal_ref.x));
    const double sy = ((az * arm.x) - (ax * get_wide(arm.z))) + normal_ref.y;
    const double sz = get_wide(get_narrow((ax * get_wide(arm.y)) - (ay * arm.x))) + normal_ref.z;
    return 1.0 / (((sx * get_wide(normal_ref.x)) + (sz * normal_ref.z)) + (sy * normal_ref.y));
}

// 0x0042CF90
void BodyDynamicsModule::apply_springs()
{
    const auto& world = world_spring_points();
    // The springs' points held still: each moving point's velocity taken out by an impulse, round after round.
    int round = 0;
    bool settled = false;
    for (bool again = true; again; again = ++round != kMaxRounds && spring_count_ != 1 && !settled) {
        settled = true;
        for (int i = spring_count_; i != 0;) {
            --i;
            if (!core::at(spring_run(), i).active) {
                continue;
            }
            const maths::Vec4& world_point = core::at(world, i);
            // its velocity: vel + rotvel x r
            const double cx = (get_wide(world_point.z) * rot_vel_.y) - (get_wide(world_point.y) * rot_vel_.z);
            const double cy = (get_wide(rot_vel_.z) * world_point.x) - (get_wide(world_point.z) * rot_vel_.x);
            const float cz =
                get_narrow((get_wide(world_point.y) * rot_vel_.x) - (get_wide(rot_vel_.y) * world_point.x));
            const maths::Vec4 v = {.x = get_narrow(cx + vel_.x), .y = get_narrow(cy + vel_.y), .z = get_narrow(get_wide(cz) + vel_.z),
                                   .w = vel_.w};
            const double length = std::sqrt(((get_wide(v.z) * v.z) + (get_wide(v.y) * v.y)) + (get_wide(v.x) * v.x));
            const float length_f = get_narrow(length);
            if (length < kSpringMinSpeed) {
                continue;
            }
            settled = false;
            const double inverse_moment = mass_over_moment_;
            const double inverse = 1.0 / length_f;
            const double nx = v.x * inverse;
            const float ny = get_narrow(v.y * inverse);
            const float nz = get_narrow(v.z * inverse);
            const float nw = get_narrow(v.w * inverse);
            // the impulse that stops it (as impulse_scale, inlined with the normal's x unrounded)
            const float ax =
                get_narrow(((get_wide(nz) * world_point.y) - (get_wide(ny) * world_point.z)) * inverse_moment);
            const double ay = ((nx * world_point.z) - (get_wide(nz) * world_point.x)) * inverse_moment;
            const double az = get_narrow((get_wide(ny) * world_point.x) - (nx * world_point.y)) * inverse_moment;
            const float sx = get_narrow(get_wide(get_narrow((ay * world_point.z) - (az * world_point.y))) + nx);
            const double sy = ((az * world_point.x) - (ax * get_wide(world_point.z))) + ny;
            const double sz = get_wide(get_narrow((ax * get_wide(world_point.y)) - (ay * world_point.x))) + nz;
            const double scale = 1.0 / (((sx * nx) + (sz * nz)) + (sy * ny));
            const float impulse = get_narrow(-(scale * length_f));
            const double jx = nx * impulse;
            const double jy = get_wide(ny) * impulse;
            const double jz = get_wide(nz) * impulse;
            const double jw = get_wide(nw) * impulse;
            vel_ = {.x = get_narrow(jx + vel_.x), .y = get_narrow(jy + vel_.y), .z = get_narrow(jz + vel_.z), .w = get_narrow(jw + vel_.w)};
            const float tx = get_narrow((jz * world_point.y) - (jy * world_point.z));
            const float ty = get_narrow((jx * world_point.z) - (jz * world_point.x));
            const double tz = (jy * world_point.x) - (jx * world_point.y);
            rot_vel_ = {.x = get_narrow(get_wide(get_narrow(tx * inverse_moment)) + rot_vel_.x),
                        .y = get_narrow(get_wide(get_narrow(ty * inverse_moment)) + rot_vel_.y),
                        .z = get_narrow(get_wide(get_narrow(tz * inverse_moment)) + rot_vel_.z),
                        .w = get_narrow((inverse_moment * kZero) + rot_vel_.w)};
        }
    }

    // Each point pulled towards its anchor.
    const float dt = get_narrow(frame_clock::get_slomo_frame_seconds());
    for (int i = spring_count_; i != 0;) {
        --i;
        const Spring& spring = core::at(spring_run(), i);
        if (!spring.active) {
            continue;
        }
        const maths::Vec4& world_point = core::at(world, i);
        const float px = get_narrow(get_wide(at_point_.x) + world_point.x);
        const double py = get_wide(at_point_.y) + world_point.y;
        const double pz = get_wide(at_point_.z) + world_point.z;
        const float pw = get_narrow(get_wide(at_point_.w) + world_point.w);
        const float dx = get_narrow(px - get_wide(spring.anchor.x));
        const float dy = get_narrow(py - spring.anchor.y);
        const double dz = pz - spring.anchor.z;
        const float dw = get_narrow(pw - get_wide(spring.anchor.w));
        const float length = get_narrow(std::sqrt(((get_wide(dx) * dx) + (dz * dz)) + (get_wide(dy) * dy)));
        if (length == kZero) {
            continue;
        }
        const double inverse_moment = mass_over_moment_;
        const double inverse = 1.0 / length;
        const double nx = dx * inverse;
        const float ny = get_narrow(dy * inverse);
        const float nz = get_narrow(dz * inverse);
        const float nw = get_narrow(dw * inverse);
        const double force = ((get_wide(length) * spring_const_) * dt) * kSpringScale;
        const float force_f = get_narrow(force);
        const double fx = nx * force;
        const double fy = get_wide(ny) * force_f;
        const float fz = get_narrow(get_wide(nz) * force_f);
        const float fw = get_narrow(get_wide(nw) * force_f);
        const double tx = (get_wide(fz) * world_point.y) - (fy * world_point.z);
        const double ty = (fx * world_point.z) - (get_wide(fz) * world_point.x);
        const float tz = get_narrow((fy * world_point.x) - (fx * world_point.y));
        const maths::Vec4 turn = {.x = get_narrow(tx * inverse_moment),
                                  .y = get_narrow(ty * inverse_moment),
                                  .z = get_narrow(tz * inverse_moment),
                                  .w = get_narrow(inverse_moment * kZero)};
        vel_ = {.x = get_narrow(fx + vel_.x), .y = get_narrow(fy + vel_.y), .z = get_narrow(get_wide(fz) + vel_.z),
                .w = get_narrow(get_wide(fw) + vel_.w)};
        rot_vel_ = {.x = append_one(turn.x, rot_vel_.x), .y = append_one(turn.y, rot_vel_.y), .z = append_one(turn.z, rot_vel_.z),
                    .w = append_one(turn.w, rot_vel_.w)};
    }
}

// 0x0042D9D0
void BodyDynamicsModule::debug_draw()
{
    uint32_t colour = 0;
    switch (cur_phase_) {
    case BodyDynamicsModule::State::kAsleep:
        colour = kAsleepColour;
        break;
    case BodyDynamicsModule::State::kAwake:
        colour = push_cooldown_ > kZero ? kPushedColour : kAwakeColour;
        break;
    case BodyDynamicsModule::State::kDisabled:
        colour = kDisabledColour;
        break;
    }
    auto at = [this](const maths::Vec4& point) {
        return maths::Vec4{.x = point.x + at_point_.x,
                           .y = point.y + at_point_.y,
                           .z = point.z + at_point_.z,
                           .w = point.w + at_point_.w};
    };
    // Every pair of contact points.
    const auto& points = world_contact_points();
    for (int i = contact_count_ - 1; i > 0; --i) {
        for (int j = 0; j < i; ++j) {
            const maths::Vec4 from = at(core::at(points, i));
            const maths::Vec4 to = at(core::at(points, j));
            gfx::push_diag_line(&from, &to, colour, 0, 1);
        }
    }
    const auto& centers = world_sphere_centers();
    for (int i = sphere_count_; i != 0;) {
        --i;
        const maths::Vec4 center = at(core::at(centers, i));
        gfx::add_debug_sphere(&center, core::at(sphere_run(), i).radius, &basis_frame_, kSphereSegments, colour, 1);
    }
    if (objects_shared().rigid_body_draw_details()) {
        gfx::push_diag_marker(at_point_, kMarkerSize, kDetailMarkerColour, 1);
    }
    const auto& springs = world_spring_points();
    for (int i = spring_count_; i != 0;) {
        --i;
        if (!core::at(spring_run(), i).active) {
            continue;
        }
        gfx::push_diag_marker(at(core::at(springs, i)), kSpringMarkerSize, kSpringPointColour, 1);
        gfx::push_diag_marker(core::at(spring_run(), i).anchor, kSpringMarkerSize, kSpringAnchorColour, 1);
    }
}

// 0x0042E540
void BodyDynamicsModule::integrate(float dt)
{
    if (unknown191_ < 3) {
        // Moved by its velocity and half a step of acceleration, then accelerated.
        const double half = get_wide(dt) * kHalf;
        const double ax = half * steady_accel_.x;
        const float ay = get_narrow(half * steady_accel_.y);
        const float az = get_narrow(half * steady_accel_.z);
        const float aw = get_narrow(half * steady_accel_.w);
        const float mx = get_narrow(ax + vel_.x);
        const double my = get_wide(ay) + vel_.y;
        const double mz = get_wide(az) + vel_.z;
        const float mw = get_narrow(get_wide(aw) + vel_.w);
        const float px = get_narrow(get_wide(mx) * dt);
        const float py = get_narrow(my * dt);
        const double pz = mz * dt;
        const double pw = get_wide(mw) * dt;
        at_point_ = {.x = get_narrow(get_wide(px) + at_point_.x), .y = get_narrow(get_wide(py) + at_point_.y),
                     .z = get_narrow(pz + at_point_.z), .w = get_narrow(pw + at_point_.w)};
        const double vx = get_wide(dt) * steady_accel_.x;
        const double vy = get_wide(dt) * steady_accel_.y;
        const float vz = get_narrow(get_wide(dt) * steady_accel_.z);
        const float vw = get_narrow(get_wide(dt) * steady_accel_.w);
        vel_ = {.x = get_narrow(vx + vel_.x), .y = get_narrow(vy + vel_.y), .z = get_narrow(get_wide(vz) + vel_.z),
                .w = get_narrow(get_wide(vw) + vel_.w)};
    } else {
        const double px = get_wide(dt) * vel_.x;
        const double py = get_wide(dt) * vel_.y;
        const float pz = get_narrow(get_wide(dt) * vel_.z);
        const float pw = get_narrow(get_wide(dt) * vel_.w);
        at_point_ = {.x = get_narrow(px + at_point_.x), .y = get_narrow(py + at_point_.y), .z = get_narrow(get_wide(pz) + at_point_.z),
                     .w = get_narrow(get_wide(pw) + at_point_.w)};
    }

    // Turned: q += (q * (-rotvel / 2, 0)) dt, normalised.
    const maths::QuatRotation spin = {.x = get_narrow(get_wide(rot_vel_.x) * kTurnRate),
                                      .y = get_narrow(get_wide(rot_vel_.y) * kTurnRate),
                                      .z = get_narrow(get_wide(rot_vel_.z) * kTurnRate),
                                      .w = 0.0F};
    maths::QuatRotation change = orientation_;
    quat_multiply(&change, &spin);
    const float cz = get_narrow(get_wide(change.z) * dt);
    const float cw = get_narrow(get_wide(change.w) * dt);
    orientation_ = {.x = get_narrow((get_wide(change.x) * dt) + orientation_.x),
                    .y = get_narrow((get_wide(change.y) * dt) + orientation_.y),
                    .z = append_one(cz, orientation_.z),
                    .w = append_one(cw, orientation_.w)};
    orientation_.make_unit(1.0F);
    quat_to_matrix(&orientation_, &basis_frame_);

    // The shape turned to the world (each set of points adds its terms in its own order).
    const maths::Mat4& basis = basis_frame_;
    for (int i = contact_count_; i != 0;) {
        --i;
        const maths::Vec4& point = core::at(contact_run(), i);
        const double x = point.x;
        const double y = point.y;
        const double z = point.z;
        core::at(world_contact_points(), i) = {
            .x = get_narrow(((y * get_at(basis, 1, 0)) + (get_at(basis, 2, 0) * z)) + (get_at(basis, 0, 0) * x)),
            .y = get_narrow(((get_at(basis, 0, 1) * x) + (get_at(basis, 2, 1) * z)) + (y * get_at(basis, 1, 1))),
            .z = get_narrow(((get_at(basis, 2, 2) * z) + (y * get_at(basis, 1, 2))) + (get_at(basis, 0, 2) * x)),
            .w = point.w};
    }
    for (int i = sphere_count_; i != 0;) {
        --i;
        const maths::Vec4& point = core::at(sphere_run(), i).center;
        const double x = point.x;
        const double y = point.y;
        const double z = point.z;
        core::at(world_sphere_centers(), i) = {
            .x = get_narrow(((y * get_at(basis, 1, 0)) + (get_at(basis, 2, 0) * z)) + (x * get_at(basis, 0, 0))),
            .y = get_narrow(((x * get_at(basis, 0, 1)) + (get_at(basis, 2, 1) * z)) + (y * get_at(basis, 1, 1))),
            .z = get_narrow(((get_at(basis, 2, 2) * z) + (x * get_at(basis, 0, 2))) + (y * get_at(basis, 1, 2))),
            .w = point.w};
    }
    for (int i = spring_count_; i != 0;) {
        --i;
        if (!core::at(spring_run(), i).active) {
            continue;
        }
        const maths::Vec4& point = core::at(spring_run(), i).point;
        const double x = point.x;
        const double y = point.y;
        const double z = point.z;
        core::at(world_spring_points(), i) = {
            .x = get_narrow(((get_at(basis, 2, 0) * z) + (get_at(basis, 1, 0) * y)) + (x * get_at(basis, 0, 0))),
            .y = get_narrow(((get_at(basis, 2, 1) * z) + (get_at(basis, 1, 1) * y)) + (get_at(basis, 0, 1) * x)),
            .z = get_narrow(((get_at(basis, 2, 2) * z) + (get_at(basis, 1, 2) * y)) + (get_at(basis, 0, 2) * x)),
            .w = point.w};
    }
}

// 0x0042E980
void BodyDynamicsModule::on_bounce(float speed, const maths::Vec4* point)
{
    if (!(speed > bounce_callback_threshold_)) {
        return;
    }
    if (bounce_script_ != 0U) {
        gsl::owner<script::Struct*> params = script::spawn_param_bag();
        if (params) {
            params = new (params) script::Struct;
        }
        Expects(params != nullptr); // (out of memory: the original went on through null)
        if (callback_params_ptr_) {
            params->merge_structure(callback_params_ptr_);
        }
        params->put_vec3(checksums::Name{kBouncePos}, script::VectorValue{.x = point->x, .y = point->y, .z = point->z, .w = point->w});
        script::host().tick_frame(entity()->spawn_script(bounce_script_, params, 0, false));
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
    if (audio_module_ && bounce_sfx_ && kVoiceLimit - sound::count_playing_voices() > kVoiceReserve) {
        const float percent = maths::confine(get_narrow(get_wide(speed) / bounce_full_speed_ * 100.0F), 0.0F, 100.0F);
        bounce_sfx_->push_float(checksums::Name{kPercent}, percent); // Percent
        audio_module_->emit_sound(bounce_sfx_);
    }
}

// 0x00430140
void BodyDynamicsModule::resolve_contacts()
{
    const std::span<Contact> contacts = get_contacts();
    // The velocities before the impulses (friction works from these).
    maths::Vec4 vel0 = vel_;
    maths::Vec4 first_turn = rot_vel_;
    float fastest = 0.0F; // the fastest approach

    // Impulses along the normals until nothing approaches.
    for (int round = 0;;) {
        bool done = true;
        for (int i = int{contacts_hit_}; i != 0;) {
            Contact& contact = core::at(contacts, --i);
            if (!contact.active) {
                continue;
            }
            const maths::Vec4& arm = contact.offset;
            const maths::Vec4& normal_ref = contact.normal;
            const double cx = (get_wide(rot_vel_.y) * arm.z) - (get_wide(rot_vel_.z) * arm.y);
            const float cy = get_narrow((get_wide(rot_vel_.z) * arm.x) - (get_wide(rot_vel_.x) * arm.z));
            const float cz = get_narrow((get_wide(rot_vel_.x) * arm.y) - (get_wide(rot_vel_.y) * arm.x));
            const float vx = get_narrow(cx + vel_.x);
            const double vy = get_wide(cy) + vel_.y;
            const double vz = get_wide(cz) + vel_.z;
            const double along = ((vz * normal_ref.z) + (get_wide(vx) * normal_ref.x)) + (vy * normal_ref.y);
            if (along > 0.0) {
                continue;
            }
            const float along_f = get_narrow(along);
            done = false;
            fastest = std::max(fastest, -along_f);
            const float target = get_narrow(-(get_wide(along_f) * restitution_));
            if (!contact.scaled) {
                contact.impulse_scale_ =
                    get_narrow(impulse_scale({.normal = &contact.normal, .offset = &contact.offset}));
                contact.scaled = true;
            }
            const double impulse = (get_wide(target) - along_f) * contact.impulse_scale_;
            const float jx = get_narrow(get_wide(normal_ref.x) * impulse);
            const double jy = get_wide(normal_ref.y) * impulse;
            const double jz = get_wide(normal_ref.z) * impulse;
            const double jw = get_wide(normal_ref.w) * impulse;
            contact.impulse = get_narrow(impulse + contact.impulse);
            vel_ = {.x = append_one(jx, vel_.x), .y = get_narrow(jy + vel_.y), .z = get_narrow(jz + vel_.z), .w = get_narrow(jw + vel_.w)};
            const float tx = get_narrow((jz * arm.y) - (jy * arm.z));
            const float ty = get_narrow((get_wide(jx) * arm.z) - (jz * arm.x));
            const double tz = (jy * arm.x) - (get_wide(jx) * arm.y);
            const double inverse_moment = mass_over_moment_;
            rot_vel_ = {.x = append_one(get_narrow(tx * inverse_moment), rot_vel_.x),
                        .y = append_one(get_narrow(ty * inverse_moment), rot_vel_.y),
                        .z = append_one(get_narrow(tz * inverse_moment), rot_vel_.z),
                        .w = get_narrow((inverse_moment * kZero) + rot_vel_.w)};
        }
        if (++round == kMaxRounds) {
            // Stuck: the script, or no velocity at all.
            if (script_d0_ != 0U) {
                script::host().tick_frame(entity()->spawn_script(script_d0_, callback_params_ptr_, 0, false));
                return;
            }
            vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            rot_vel_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            return;
        }
        if (contact_count_ == 1 || done) {
            break;
        }
    }

    // Friction: across the normal (along the directed friction where there is one), at most the normal impulse
    // times the friction.
    for (int i = int{contacts_hit_}; i != 0;) {
        const Contact& contact = core::at(contacts, --i);
        if (!contact.active) {
            continue;
        }
        const maths::Vec4& arm = contact.offset;
        const maths::Vec4& normal_ref = contact.normal;
        const double cx = (get_wide(first_turn.y) * arm.z) - (get_wide(first_turn.z) * arm.y);
        const double cy = (get_wide(first_turn.z) * arm.x) - (get_wide(first_turn.x) * arm.z);
        const float cz = get_narrow((get_wide(first_turn.x) * arm.y) - (get_wide(first_turn.y) * arm.x));
        const float vx = get_narrow(cx + vel0.x);
        const double vy = cy + vel0.y;
        const double vz = get_wide(cz) + vel0.z;
        const double back = -(((vz * normal_ref.z) + (get_wide(vx) * normal_ref.x)) + (vy * normal_ref.y));
        maths::Vec4 across = {.x = get_narrow((back * normal_ref.x) + vx),
                              .y = get_narrow(get_wide(get_narrow(back * normal_ref.y)) + vy),
                              .z = get_narrow(get_wide(get_narrow(back * normal_ref.z)) + vz),
                              .w = get_narrow(get_wide(get_narrow(back * normal_ref.w)) + vel0.w)};
        if (steer_friction_) {
            // (the original read the friction of a sphere's number past the shape's points, the words after the
            // table: here those spheres have none)
            const std::span<const DirectedFriction> frictions = steer_friction_run();
            const DirectedFriction* directed = static_cast<std::size_t>(contact.friction) < frictions.size()
                                                   ? &core::at(frictions, contact.friction)
                                                   : nullptr;
            if (directed && directed->active) {
                const DirectedFriction& friction = *directed;
                maths::Vec4 direction{};
                basis_frame_.rotate_vector(direction, friction.direction);
                const double dn = ((get_wide(direction.x) * normal_ref.x) + (get_wide(direction.z) * normal_ref.z)) +
                                  (get_wide(direction.y) * normal_ref.y);
                const float dny = get_narrow(dn * normal_ref.y);
                const float dnz = get_narrow(dn * normal_ref.z);
                const float dnw = get_narrow(dn * normal_ref.w);
                direction = {.x = get_narrow(direction.x - (dn * normal_ref.x)),
                             .y = get_sub(direction.y, dny),
                             .z = get_sub(direction.z, dnz),
                             .w = get_sub(direction.w, dnw)};
                const double length =
                    std::sqrt(((get_wide(direction.z) * direction.z) + (get_wide(direction.y) * direction.y)) +
                              (get_wide(direction.x) * direction.x));
                if (length > kMinLength) {
                    const double inverse = 1.0 / length;
                    direction = {.x = get_narrow(direction.x * inverse),
                                 .y = get_narrow(direction.y * inverse),
                                 .z = get_narrow(direction.z * inverse),
                                 .w = get_narrow(direction.w * inverse)};
                    across.project_onto(direction);
                }
            }
        }
        const double length = std::sqrt(((get_wide(across.z) * across.z) + (get_wide(across.y) * across.y)) +
                                        (get_wide(across.x) * across.x));
        if (length < kMinLength) {
            continue;
        }
        const float length_f = get_narrow(length);
        const double inverse = 1.0 / length_f;
        across = {.x = get_narrow(across.x * inverse), .y = get_narrow(across.y * inverse), .z = get_narrow(across.z * inverse),
                  .w = get_narrow(across.w * inverse)};
        const double stop = impulse_scale({.normal = &across, .offset = &contact.offset}) * length_f;
        const float most = get_narrow(get_wide(contact.impulse) * drag_factor_);
        const double push_scale = -(stop > most ? most : stop);
        const double jx = across.x * push_scale;
        const float jy = get_narrow(across.y * push_scale);
        const float jz = get_narrow(across.z * push_scale);
        const float jw = get_narrow(across.w * push_scale);
        const double tx = (get_wide(jz) * arm.y) - (get_wide(jy) * arm.z);
        const double ty = (jx * arm.z) - (get_wide(jz) * arm.x);
        const float tz = get_narrow((get_wide(jy) * arm.x) - (jx * arm.y));
        const double inverse_moment = mass_over_moment_;
        const maths::Vec4 turn = {.x = get_narrow(tx * inverse_moment),
                                  .y = get_narrow(ty * inverse_moment),
                                  .z = get_narrow(tz * inverse_moment),
                                  .w = get_narrow(inverse_moment * kZero)};
        vel_ = {.x = get_narrow(jx + vel_.x), .y = append_one(jy, vel_.y), .z = append_one(jz, vel_.z), .w = append_one(jw, vel_.w)};
        rot_vel_ = {.x = append_one(turn.x, rot_vel_.x), .y = append_one(turn.y, rot_vel_.y), .z = append_one(turn.z, rot_vel_.z),
                    .w = append_one(turn.w, rot_vel_.w)};
        vel0 = {.x = get_narrow(jx + vel0.x), .y = append_one(jy, vel0.y), .z = append_one(jz, vel0.z), .w = append_one(jw, vel0.w)};
        first_turn = {.x = append_one(turn.x, first_turn.x), .y = append_one(turn.y, first_turn.y),
                      .z = append_one(turn.z, first_turn.z), .w = append_one(turn.w, first_turn.w)};
    }

    // Pushed out of the world by each contact's depth.
    const float dt = get_narrow(frame_clock::get_slomo_frame_seconds());
    for (int i = int{contacts_hit_}; i != 0;) {
        const Contact& contact = core::at(contacts, --i);
        if (!contact.active) {
            continue;
        }
        const maths::Vec4& arm = contact.offset;
        const maths::Vec4& normal_ref = contact.normal;
        const double spring_push = -((get_wide(contact.depth) * spring_const_) * dt);
        const maths::Vec4 push = {.x = get_narrow(spring_push * normal_ref.x),
                                  .y = get_narrow(spring_push * normal_ref.y),
                                  .z = get_narrow(spring_push * normal_ref.z),
                                  .w = get_narrow(spring_push * normal_ref.w)};
        const double tx = (get_wide(push.z) * arm.y) - (get_wide(push.y) * arm.z);
        const double ty = (get_wide(push.x) * arm.z) - (get_wide(push.z) * arm.x);
        const float tz = get_narrow((get_wide(push.y) * arm.x) - (get_wide(push.x) * arm.y));
        const double inverse_moment = mass_over_moment_;
        vel_ = {.x = append_one(push.x, vel_.x), .y = append_one(push.y, vel_.y), .z = append_one(push.z, vel_.z),
                .w = append_one(push.w, vel_.w)};
        rot_vel_ = {.x = append_one(get_narrow(tx * inverse_moment), rot_vel_.x),
                    .y = get_narrow((ty * inverse_moment) + rot_vel_.y),
                    .z = get_narrow((get_wide(tz) * inverse_moment) + rot_vel_.z),
                    .w = append_one(get_narrow(inverse_moment * kZero), rot_vel_.w)};
        if (objects_shared().rigid_body_draw_details()) {
            const maths::Vec4 from = {
                .x = arm.x + at_point_.x, .y = at_point_.y + arm.y, .z = at_point_.z + arm.z, .w = at_point_.w + arm.w};
            const maths::Vec4 to = {.x = from.x + (push.x * kDebugImpulseScale),
                                    .y = from.y + (push.y * kDebugImpulseScale),
                                    .z = from.z + (push.z * kDebugImpulseScale),
                                    .w = from.w + (push.w * kDebugImpulseScale)};
            gfx::push_diag_line(&to, &from, kImpulseLineColour, 0, 1);
        }
    }

    // The bounce, at the last active contact.
    for (int i = int{contacts_hit_}; i != 0;) {
        const Contact& contact = core::at(contacts, --i);
        if (contact.active) {
            const maths::Vec4 point = {.x = append_one(contact.offset.x, at_point_.x),
                                       .y = append_one(contact.offset.y, at_point_.y),
                                       .z = append_one(contact.offset.z, at_point_.z),
                                       .w = append_one(contact.offset.w, at_point_.w)};
            on_bounce(fastest, &point);
            return;
        }
    }
}

// 0x00430BE0
void BodyDynamicsModule::collide(render::ProbeRay* feeler)
{
    const maths::Vec4& normal_ref = feeler->surface_dir();
    const double along =
        ((get_wide(vel_.z) * normal_ref.z) + (get_wide(vel_.y) * normal_ref.y)) + (get_wide(vel_.x) * normal_ref.x);
    // The bounce: -(1 + restitution) of the speed along the normal.
    const double spring_push = -((get_wide(restitution_) + 1.0F) * along);
    const maths::Vec4 bounce = {.x = get_narrow(spring_push * normal_ref.x),
                                .y = get_narrow(spring_push * normal_ref.y),
                                .z = get_narrow(spring_push * normal_ref.z),
                                .w = 0.0F};
    const double bounce_w = spring_push * normal_ref.w;
    // Friction against what's across the normal.
    const float tx = get_narrow(vel_.x - (along * normal_ref.x));
    const double ty = vel_.y - (along * normal_ref.y);
    const float tz = get_narrow(vel_.z - get_wide(get_narrow(along * normal_ref.z)));
    const double tw = vel_.w - get_wide(get_narrow(along * normal_ref.w));
    const double length = std::sqrt(((get_wide(tz) * tz) + (ty * ty)) + (get_wide(tx) * tx));
    const float length_f = get_narrow(length);
    if (length > kMinLength) {
        const double inverse = 1.0 / length_f;
        const float ux = get_narrow(tx * inverse);
        const float uy = get_narrow(ty * inverse);
        const float uz = get_narrow(tz * inverse);
        const double uw = tw * inverse;
        const float most = get_narrow(std::sqrt(((get_wide(bounce.z) * bounce.z) + (get_wide(bounce.y) * bounce.y)) +
                                                (get_wide(bounce.x) * bounce.x)) *
                                      drag_factor_);
        const double push_scale = -(length_f > most ? most : length_f);
        vel_ = {.x = get_narrow((ux * push_scale) + vel_.x),
                .y = append_one(get_narrow(uy * push_scale), vel_.y),
                .z = append_one(get_narrow(uz * push_scale), vel_.z),
                .w = append_one(get_narrow(uw * push_scale), vel_.w)};
    }
    vel_ = {.x = append_one(bounce.x, vel_.x), .y = append_one(bounce.y, vel_.y), .z = append_one(bounce.z, vel_.z),
            .w = get_narrow(bounce_w + vel_.w)};
    on_bounce(get_narrow(-along), &feeler->contact_pos());
}

// 0x00430E20
void BodyDynamicsModule::tick_frame()
{
    if (cur_phase_ == BodyDynamicsModule::State::kDisabled) {
        return;
    }
    const float frame = get_narrow(frame_clock::get_slomo_frame_seconds());
    if (push_cooldown_ > kZero) {
        push_cooldown_ = get_sub(push_cooldown_, frame);
    } else if ((flag_bits_ & kFlagNoPlayerCollision) == 0U) {
        feel_pushers();
    }
    if (die_timer_ >= kZero) {
        if (die_timer_ <= frame) {
            flag_as_dead(entity());
        } else {
            die_timer_ = get_sub(die_timer_, frame);
        }
    }
    if (cur_phase_ == BodyDynamicsModule::State::kAsleep) {
        entity()->display_matrix() = basis_frame_;
        return;
    }

    update_world_points();
    // The frame in steps: each to the frame's end, or to just before what the path hits.
    float remaining = frame;
    for (int step = 0;;) {
        float length = remaining;
        render::ProbeRay feeler;
        const double vx = get_wide(remaining) * vel_.x;
        const double vy = get_wide(remaining) * vel_.y;
        const double vz = get_wide(remaining) * vel_.z;
        const double vw = get_wide(remaining) * vel_.w;
        const double half = (get_wide(remaining) * remaining) * kHalf;
        feeler.line().start = at_point_;
        feeler.line().end = {
            .x = get_narrow((half * steady_accel_.x) + (get_wide(get_narrow(vx)) + at_point_.x)),
            .y = get_narrow(get_wide(get_narrow(half * steady_accel_.y)) + (get_wide(get_narrow(vy)) + at_point_.y)),
            .z = get_narrow(get_wide(get_narrow(half * steady_accel_.z)) + (vz + at_point_.z)),
            .w = get_narrow(get_wide(get_narrow(half * steady_accel_.w)) + get_narrow(vw + at_point_.w))};
        const bool hit = feeler.get_hit_shape(false, false) != 0;
        if (hit) {
            const maths::Seg& line = feeler.line();
            const double dx = get_wide(line.end.x) - line.start.x;
            const double dy = get_wide(line.end.y) - line.start.y;
            const double dz = get_wide(line.end.z) - line.start.z;
            const double path = std::sqrt(((dx * dx) + (dy * dy)) + (dz * dz));
            const double speed =
                std::sqrt(((get_wide(vel_.x) * vel_.x) + (get_wide(vel_.y) * vel_.y)) + (get_wide(vel_.z) * vel_.z));
            length = get_narrow(((path * feeler.reach()) - kHitBackOff) / speed);
            if (length < kZero) {
                length = 0.0F;
            }
        }
        integrate(length);
        if (hit) {
            collide(&feeler);
        }
        remaining = get_sub(remaining, length);
        if (++step == kMaxSteps) {
            sleep();
            break;
        }
        if (!hit) {
            break;
        }
    }

    if (find_contacts()) {
        resolve_contacts();
    }
    if (spring_count_ != 0U) {
        apply_springs();
    }
    damp();
    check_sleep();

    maths::Vec4 offset{};
    basis_frame_.rotate_vector(offset, model_offset_);
    entity()->at_point() = {.x = append_one(at_point_.x, offset.x), .y = append_one(at_point_.y, offset.y),
                          .z = append_one(at_point_.z, offset.z), .w = append_one(at_point_.w, offset.w)};
    entity()->basis_frame() = basis_frame_;
    entity()->display_matrix() = basis_frame_;
    entity()->vel() = vel_;
    if (objects_shared().rigid_body_draw_shape()) {
        debug_draw();
    }
}

} // namespace objects
