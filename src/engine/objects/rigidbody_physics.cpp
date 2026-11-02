// The rigid body component's physics: the frame's update, integration, collisions with the world, contacts and
// springs (engine/objects/rigidbody_component.h).
//
// The original's arithmetic is x87 code at 53-bit precision: what it keeps on the FPU stack is a double here, what
// it stores to memory a float - each value is rounded where the original rounds it, and sums keep its grouping.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "engine/gfx/debug_lines.h"
#include "engine/maths/maths.h"
#include "engine/maths/quat_rotation.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/objects/sound_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "platform/stopwatch.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/objects/shared_state.h"

namespace objects {

RigidbodyPhysicsShared& rigidbody_physics_shared()
{
    static core::NoDestroy<RigidbodyPhysicsShared> state;
    return state.get();
}

namespace {

constexpr float kZero = 0.0f;               // 0x0066D520 (rotational w changes by 0 * mass_over_moment)
constexpr float kSpringMinSpeed = 0.1f;     // 0x00645D74
constexpr float kSpringScale = -10.0f;      // 0x006463E0
constexpr float kHalf = 0.5f;               // 0x006453BC
constexpr float kTurnRate = -0.5f;          // 0x0064642C
constexpr float kMinLength = 0.001f;        // 0x006461D8
constexpr float kHitBackOff = 6.0f;         // 0x00646284
constexpr float kDebugImpulseScale = 60.0f; // 0x00645EA0
constexpr int kMaxRounds = 20;
constexpr int kMaxSteps = 5;

double get_wide(float value) { return value; }
float get_narrow(double value) { return static_cast<float>(value); }
// Two floats added / subtracted on the x87 (at double precision, then stored: rounded twice).
float append_one(float a, float b) { return get_narrow(get_wide(a) + b); }
float get_sub(float a, float b) { return get_narrow(get_wide(a) - b); }

// Debug drawing switches (0x006939D4: the shape each frame; 0x006939D5: the centre, the contacts' pushes).
bool draw_shape() { return rigidbody_physics_shared().rigid_body_draw_shape_; }
bool draw_details() { return rigidbody_physics_shared().rigid_body_draw_details_; }

// Quaternions: `quat` times `other`, in place (0x00420140); the matrix of one.
void quat_multiply(maths::QuatRotation* quat, const maths::QuatRotation* other) { *quat *= *other; }
void quat_to_matrix(const maths::QuatRotation* quat, maths::Mat4* matrix) { quat->fetch_mat(matrix); }

void flag_as_dead(GameEntity* object) { object->flag_as_dead(); }

// The matrix's element at row `r`, column `c`.
double get_at(const maths::Mat4& m, int r, int c) { return core::at(m.row, r)[c]; }

} // namespace

// 0x0042CEE0: 1 / (n . (n + (m (r x n)) x r)) - m the mass over the moment.
double BodyDynamicsModule::impulse_scale(const maths::Vec4* normal, const maths::Vec4* offset) const
{
    const maths::Vec4& n = *normal;
    const maths::Vec4& r = *offset;
    const double m = mass_over_moment_;
    // a = m (r x n): x rounded, y and z not (z from its rounded cross product)
    const float ax = get_narrow((get_wide(r.y) * n.z - get_wide(r.z) * n.y) * m);
    const double ay = (get_wide(n.x) * r.z - get_wide(r.x) * n.z) * m;
    const double az = get_narrow(get_wide(r.x) * n.y - get_wide(n.x) * r.y) * m;
    // b = a x r, plus n
    const float sx = get_narrow(get_narrow(ay * r.z - az * r.y) + get_wide(n.x));
    const double sy = (az * r.x - ax * get_wide(r.z)) + n.y;
    const double sz = get_wide(get_narrow(ax * get_wide(r.y) - ay * r.x)) + n.z;
    return 1.0 / ((sx * get_wide(n.x) + sz * n.z) + sy * n.y);
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
            if (!core::at(spring_run(), i).active)
                continue;
            const maths::Vec4& r = core::at(world, i);
            // its velocity: vel + rotvel x r
            const double cx = get_wide(r.z) * rot_vel_.y - get_wide(r.y) * rot_vel_.z;
            const double cy = get_wide(rot_vel_.z) * r.x - get_wide(r.z) * rot_vel_.x;
            const float cz = get_narrow(get_wide(r.y) * rot_vel_.x - get_wide(rot_vel_.y) * r.x);
            const maths::Vec4 v = {.x = get_narrow(cx + vel_.x), .y = get_narrow(cy + vel_.y), .z = get_narrow(get_wide(cz) + vel_.z),
                                   .w = vel_.w};
            const double length = std::sqrt((get_wide(v.z) * v.z + get_wide(v.y) * v.y) + get_wide(v.x) * v.x);
            const float length_f = get_narrow(length);
            if (length < kSpringMinSpeed)
                continue;
            settled = false;
            const double m = mass_over_moment_;
            const double inverse = 1.0 / length_f;
            const double nx = v.x * inverse;
            const float ny = get_narrow(v.y * inverse);
            const float nz = get_narrow(v.z * inverse);
            const float nw = get_narrow(v.w * inverse);
            // the impulse that stops it (as impulse_scale, inlined with the normal's x unrounded)
            const float ax = get_narrow((get_wide(nz) * r.y - get_wide(ny) * r.z) * m);
            const double ay = (nx * r.z - get_wide(nz) * r.x) * m;
            const double az = get_narrow(get_wide(ny) * r.x - nx * r.y) * m;
            const float sx = get_narrow(get_wide(get_narrow(ay * r.z - az * r.y)) + nx);
            const double sy = (az * r.x - ax * get_wide(r.z)) + ny;
            const double sz = get_wide(get_narrow(ax * get_wide(r.y) - ay * r.x)) + nz;
            const double scale = 1.0 / ((sx * nx + sz * nz) + sy * ny);
            const float j = get_narrow(-(scale * length_f));
            const double jx = nx * j;
            const double jy = get_wide(ny) * j;
            const double jz = get_wide(nz) * j;
            const double jw = get_wide(nw) * j;
            vel_ = {.x = get_narrow(jx + vel_.x), .y = get_narrow(jy + vel_.y), .z = get_narrow(jz + vel_.z), .w = get_narrow(jw + vel_.w)};
            const float tx = get_narrow(jz * r.y - jy * r.z);
            const float ty = get_narrow(jx * r.z - jz * r.x);
            const double tz = jy * r.x - jx * r.y;
            rot_vel_ = {.x = get_narrow(get_wide(get_narrow(tx * m)) + rot_vel_.x),
                        .y = get_narrow(get_wide(get_narrow(ty * m)) + rot_vel_.y),
                        .z = get_narrow(get_wide(get_narrow(tz * m)) + rot_vel_.z), .w = get_narrow(m * kZero + rot_vel_.w)};
        }
    }

    // Each point pulled towards its anchor.
    const float dt = get_narrow(frame_clock::get_slomo_frame_seconds());
    for (int i = spring_count_; i != 0;) {
        --i;
        const Spring& spring = core::at(spring_run(), i);
        if (!spring.active)
            continue;
        const maths::Vec4& r = core::at(world, i);
        const float px = get_narrow(get_wide(at_point_.x) + r.x);
        const double py = get_wide(at_point_.y) + r.y;
        const double pz = get_wide(at_point_.z) + r.z;
        const float pw = get_narrow(get_wide(at_point_.w) + r.w);
        const float dx = get_narrow(px - get_wide(spring.anchor.x));
        const float dy = get_narrow(py - spring.anchor.y);
        const double dz = pz - spring.anchor.z;
        const float dw = get_narrow(pw - get_wide(spring.anchor.w));
        const float length = get_narrow(std::sqrt((get_wide(dx) * dx + dz * dz) + get_wide(dy) * dy));
        if (length == kZero)
            continue;
        const double m = mass_over_moment_;
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
        const double tx = get_wide(fz) * r.y - fy * r.z;
        const double ty = fx * r.z - get_wide(fz) * r.x;
        const float tz = get_narrow(fy * r.x - fx * r.y);
        const maths::Vec4 turn = {.x = get_narrow(tx * m), .y = get_narrow(ty * m), .z = get_narrow(tz * m), .w = get_narrow(m * kZero)};
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
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (cur_phase_) {
    case kStateAsleep:
        colour = 0xFFC80000;
        break;
    case kStateAwake:
        colour = push_cooldown_ > kZero ? 0xFF00C800 : 0xFF0000C8;
        break;
    case kStateDisabled:
        colour = 0xFF640064;
        break;
    }
    auto at = [this](const maths::Vec4& p) {
        return maths::Vec4{.x = p.x + at_point_.x, .y = p.y + at_point_.y, .z = p.z + at_point_.z, .w = p.w + at_point_.w};
    };
    // Every pair of contact points.
    const auto& points = world_contact_points();
    for (int i = contact_count_ - 1; i > 0; --i)
        for (int j = 0; j < i; ++j) {
            const maths::Vec4 from = at(core::at(points, i));
            const maths::Vec4 to = at(core::at(points, j));
            gfx::push_diag_line(&from, &to, colour, 0, 1);
        }
    const auto& centers = world_sphere_centers();
    for (int i = sphere_count_; i != 0;) {
        --i;
        const maths::Vec4 center = at(core::at(centers, i));
        gfx::add_debug_sphere(&center, core::at(sphere_run(), i).radius, &basis_frame_, 12, colour, 1);
    }
    if (draw_details())
        gfx::push_diag_marker(at_point_, 36.0f, 0xFFC800C8, 1);
    const auto& springs = world_spring_points();
    for (int i = spring_count_; i != 0;) {
        --i;
        if (!core::at(spring_run(), i).active)
            continue;
        gfx::push_diag_marker(at(core::at(springs, i)), 24.0f, 0xFFC86400, 1);
        gfx::push_diag_marker(core::at(spring_run(), i).anchor, 24.0f, 0xFFC80064, 1);
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
                                      .z = get_narrow(get_wide(rot_vel_.z) * kTurnRate), .w = 0.0f};
    maths::QuatRotation change = orientation_;
    quat_multiply(&change, &spin);
    const float cz = get_narrow(get_wide(change.z) * dt);
    const float cw = get_narrow(get_wide(change.w) * dt);
    orientation_ = {.x = get_narrow(get_wide(change.x) * dt + orientation_.x),
                    .y = get_narrow(get_wide(change.y) * dt + orientation_.y), .z = append_one(cz, orientation_.z),
                    .w = append_one(cw, orientation_.w)};
    orientation_.make_unit(1.0f);
    quat_to_matrix(&orientation_, &basis_frame_);

    // The shape turned to the world (each set of points adds its terms in its own order).
    const maths::Mat4& t = basis_frame_;
    for (int i = contact_count_; i != 0;) {
        --i;
        const maths::Vec4& c = core::at(contact_run(), i);
        const double x = c.x, y = c.y, z = c.z;
        core::at(world_contact_points(), i) = {.x = get_narrow((y * get_at(t, 1, 0) + get_at(t, 2, 0) * z) + get_at(t, 0, 0) * x),
                                     .y = get_narrow((get_at(t, 0, 1) * x + get_at(t, 2, 1) * z) + y * get_at(t, 1, 1)),
                                     .z = get_narrow((get_at(t, 2, 2) * z + y * get_at(t, 1, 2)) + get_at(t, 0, 2) * x),
                                     .w = c.w};
    }
    for (int i = sphere_count_; i != 0;) {
        --i;
        const maths::Vec4& c = core::at(sphere_run(), i).center;
        const double x = c.x, y = c.y, z = c.z;
        core::at(world_sphere_centers(), i) = {.x = get_narrow((y * get_at(t, 1, 0) + get_at(t, 2, 0) * z) + x * get_at(t, 0, 0)),
                                     .y = get_narrow((x * get_at(t, 0, 1) + get_at(t, 2, 1) * z) + y * get_at(t, 1, 1)),
                                     .z = get_narrow((get_at(t, 2, 2) * z + x * get_at(t, 0, 2)) + y * get_at(t, 1, 2)),
                                     .w = c.w};
    }
    for (int i = spring_count_; i != 0;) {
        --i;
        if (!core::at(spring_run(), i).active)
            continue;
        const maths::Vec4& c = core::at(spring_run(), i).point;
        const double x = c.x, y = c.y, z = c.z;
        core::at(world_spring_points(), i) = {.x = get_narrow((get_at(t, 2, 0) * z + get_at(t, 1, 0) * y) + x * get_at(t, 0, 0)),
                                    .y = get_narrow((get_at(t, 2, 1) * z + get_at(t, 1, 1) * y) + get_at(t, 0, 1) * x),
                                    .z = get_narrow((get_at(t, 2, 2) * z + get_at(t, 1, 2) * y) + get_at(t, 0, 2) * x), .w = c.w};
    }
}

// 0x0042E980
void BodyDynamicsModule::on_bounce(float speed, const maths::Vec4* point)
{
    if (!(speed > bounce_callback_threshold_))
        return;
    if (bounce_script_) {
        gsl::owner<script::Struct*> params = script::spawn_param_bag();
        if (params)
            params = new (params) script::Struct;
        if (callback_params_ptr_)
            params->merge_structure(callback_params_ptr_); // (out of memory: through null, as the original)
        params->put_vec3(0x43F454D0, script::VectorValue{.x = point->x, .y = point->y, .z = point->z, .w = point->w});
        entity_->spawn_script(bounce_script_, params, 0, false)->tick_frame();
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
    if (audio_module_ && bounce_sfx_ && 48 - sound::count_playing_voices() > 12) {
        const float percent = maths::confine(get_narrow(get_wide(speed) / bounce_full_speed_ * 100.0f), 0.0f, 100.0f);
        bounce_sfx_->push_float(0x9E497FC6, percent); // Percent
        audio_module_->emit_sound(bounce_sfx_);
    }
}

// 0x00430140
void BodyDynamicsModule::resolve_contacts()
{
    const std::span<Contact> contacts = get_contacts();
    // The velocities before the impulses (friction works from these).
    maths::Vec4 vel0 = vel_, first_turn = rot_vel_;
    float fastest = 0.0f; // the fastest approach

    // Impulses along the normals until nothing approaches.
    for (int round = 0;;) {
        bool done = true;
        for (int i = int{contacts_hit_}; i != 0;) {
            Contact& c = core::at(contacts, --i);
            if (!c.active)
                continue;
            const maths::Vec4& r = c.offset;
            const maths::Vec4& n = c.normal;
            const double cx = get_wide(rot_vel_.y) * r.z - get_wide(rot_vel_.z) * r.y;
            const float cy = get_narrow(get_wide(rot_vel_.z) * r.x - get_wide(rot_vel_.x) * r.z);
            const float cz = get_narrow(get_wide(rot_vel_.x) * r.y - get_wide(rot_vel_.y) * r.x);
            const float vx = get_narrow(cx + vel_.x);
            const double vy = get_wide(cy) + vel_.y;
            const double vz = get_wide(cz) + vel_.z;
            const double along = (vz * n.z + get_wide(vx) * n.x) + vy * n.y;
            if (along > 0.0)
                continue;
            const float along_f = get_narrow(along);
            done = false;
            if (-along_f > fastest)
                fastest = -along_f;
            const float target = get_narrow(-(get_wide(along_f) * restitution_));
            if (!c.scaled) {
                c.impulse_scale_ = get_narrow(impulse_scale(&c.normal, &c.offset));
                c.scaled = true;
            }
            const double j = (get_wide(target) - along_f) * c.impulse_scale_;
            const float jx = get_narrow(get_wide(n.x) * j);
            const double jy = get_wide(n.y) * j;
            const double jz = get_wide(n.z) * j;
            const double jw = get_wide(n.w) * j;
            c.impulse = get_narrow(j + c.impulse);
            vel_ = {.x = append_one(jx, vel_.x), .y = get_narrow(jy + vel_.y), .z = get_narrow(jz + vel_.z), .w = get_narrow(jw + vel_.w)};
            const float tx = get_narrow(jz * r.y - jy * r.z);
            const float ty = get_narrow(get_wide(jx) * r.z - jz * r.x);
            const double tz = jy * r.x - get_wide(jx) * r.y;
            const double m = mass_over_moment_;
            rot_vel_ = {.x = append_one(get_narrow(tx * m), rot_vel_.x), .y = append_one(get_narrow(ty * m), rot_vel_.y),
                        .z = append_one(get_narrow(tz * m), rot_vel_.z), .w = get_narrow(m * kZero + rot_vel_.w)};
        }
        if (++round == kMaxRounds) {
            // Stuck: the script, or no velocity at all.
            if (script_d0_) {
                entity_->spawn_script(script_d0_, callback_params_ptr_, 0, false)->tick_frame();
                return;
            }
            vel_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
            rot_vel_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
            return;
        }
        if (contact_count_ == 1 || done)
            break;
    }

    // Friction: across the normal (along the directed friction where there is one), at most the normal impulse
    // times the friction.
    for (int i = int{contacts_hit_}; i != 0;) {
        const Contact& c = core::at(contacts, --i);
        if (!c.active)
            continue;
        const maths::Vec4& r = c.offset;
        const maths::Vec4& n = c.normal;
        const double cx = get_wide(first_turn.y) * r.z - get_wide(first_turn.z) * r.y;
        const double cy = get_wide(first_turn.z) * r.x - get_wide(first_turn.x) * r.z;
        const float cz = get_narrow(get_wide(first_turn.x) * r.y - get_wide(first_turn.y) * r.x);
        const float vx = get_narrow(cx + vel0.x);
        const double vy = cy + vel0.y;
        const double vz = get_wide(cz) + vel0.z;
        const double back = -((vz * n.z + get_wide(vx) * n.x) + vy * n.y);
        maths::Vec4 across = {.x = get_narrow(back * n.x + vx), .y = get_narrow(get_wide(get_narrow(back * n.y)) + vy),
                              .z = get_narrow(get_wide(get_narrow(back * n.z)) + vz),
                              .w = get_narrow(get_wide(get_narrow(back * n.w)) + vel0.w)};
        if (steer_friction_) {
            // (the original read the friction of a sphere's number past the shape's points, the words after the
            // table: here those spheres have none)
            const std::span<const DirectedFriction> frictions = steer_friction_run();
            const DirectedFriction* directed =
                static_cast<std::size_t>(c.friction) < frictions.size() ? &core::at(frictions, c.friction) : nullptr;
            if (directed && directed->active) {
                const DirectedFriction& friction = *directed;
                maths::Vec4 d{};
                basis_frame_.rotate_vector(d, friction.direction);
                const double dn = (get_wide(d.x) * n.x + get_wide(d.z) * n.z) + get_wide(d.y) * n.y;
                const float dny = get_narrow(dn * n.y);
                const float dnz = get_narrow(dn * n.z);
                const float dnw = get_narrow(dn * n.w);
                d = {.x = get_narrow(d.x - dn * n.x), .y = get_sub(d.y, dny), .z = get_sub(d.z, dnz), .w = get_sub(d.w, dnw)};
                const double length = std::sqrt((get_wide(d.z) * d.z + get_wide(d.y) * d.y) + get_wide(d.x) * d.x);
                if (length > kMinLength) {
                    const double inverse = 1.0 / length;
                    d = {.x = get_narrow(d.x * inverse), .y = get_narrow(d.y * inverse), .z = get_narrow(d.z * inverse),
                         .w = get_narrow(d.w * inverse)};
                    across.project_onto(d);
                }
            }
        }
        const double length =
            std::sqrt((get_wide(across.z) * across.z + get_wide(across.y) * across.y) + get_wide(across.x) * across.x);
        if (length < kMinLength)
            continue;
        const float length_f = get_narrow(length);
        const double inverse = 1.0 / length_f;
        across = {.x = get_narrow(across.x * inverse), .y = get_narrow(across.y * inverse), .z = get_narrow(across.z * inverse),
                  .w = get_narrow(across.w * inverse)};
        const double stop = impulse_scale(&across, &c.offset) * length_f;
        const float most = get_narrow(get_wide(c.impulse) * drag_factor_);
        const double f = -(stop > most ? most : stop);
        const double jx = across.x * f;
        const float jy = get_narrow(across.y * f);
        const float jz = get_narrow(across.z * f);
        const float jw = get_narrow(across.w * f);
        const double tx = get_wide(jz) * r.y - get_wide(jy) * r.z;
        const double ty = jx * r.z - get_wide(jz) * r.x;
        const float tz = get_narrow(get_wide(jy) * r.x - jx * r.y);
        const double m = mass_over_moment_;
        const maths::Vec4 turn = {.x = get_narrow(tx * m), .y = get_narrow(ty * m), .z = get_narrow(tz * m), .w = get_narrow(m * kZero)};
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
        const Contact& c = core::at(contacts, --i);
        if (!c.active)
            continue;
        const maths::Vec4& r = c.offset;
        const maths::Vec4& n = c.normal;
        const double k = -((get_wide(c.depth) * spring_const_) * dt);
        const maths::Vec4 push = {.x = get_narrow(k * n.x), .y = get_narrow(k * n.y), .z = get_narrow(k * n.z), .w = get_narrow(k * n.w)};
        const double tx = get_wide(push.z) * r.y - get_wide(push.y) * r.z;
        const double ty = get_wide(push.x) * r.z - get_wide(push.z) * r.x;
        const float tz = get_narrow(get_wide(push.y) * r.x - get_wide(push.x) * r.y);
        const double m = mass_over_moment_;
        vel_ = {.x = append_one(push.x, vel_.x), .y = append_one(push.y, vel_.y), .z = append_one(push.z, vel_.z),
                .w = append_one(push.w, vel_.w)};
        rot_vel_ = {.x = append_one(get_narrow(tx * m), rot_vel_.x), .y = get_narrow(ty * m + rot_vel_.y),
                    .z = get_narrow(get_wide(tz) * m + rot_vel_.z), .w = append_one(get_narrow(m * kZero), rot_vel_.w)};
        if (draw_details()) {
            const maths::Vec4 from = {.x = r.x + at_point_.x, .y = at_point_.y + r.y, .z = at_point_.z + r.z, .w = at_point_.w + r.w};
            const maths::Vec4 to = {.x = from.x + push.x * kDebugImpulseScale, .y = from.y + push.y * kDebugImpulseScale,
                                    .z = from.z + push.z * kDebugImpulseScale, .w = from.w + push.w * kDebugImpulseScale};
            gfx::push_diag_line(&to, &from, 0xFF00FFFF, 0, 1);
        }
    }

    // The bounce, at the last active contact.
    for (int i = int{contacts_hit_}; i != 0;) {
        const Contact& c = core::at(contacts, --i);
        if (c.active) {
            const maths::Vec4 point = {.x = append_one(c.offset.x, at_point_.x), .y = append_one(c.offset.y, at_point_.y),
                                       .z = append_one(c.offset.z, at_point_.z), .w = append_one(c.offset.w, at_point_.w)};
            on_bounce(fastest, &point);
            return;
        }
    }
}

// 0x00430BE0
void BodyDynamicsModule::collide(render::ProbeRay* feeler)
{
    const maths::Vec4& n = feeler->surface_dir_;
    const double along = (get_wide(vel_.z) * n.z + get_wide(vel_.y) * n.y) + get_wide(vel_.x) * n.x;
    // The bounce: -(1 + restitution) of the speed along the normal.
    const double k = -((get_wide(restitution_) + 1.0f) * along);
    const maths::Vec4 bounce = {.x = get_narrow(k * n.x), .y = get_narrow(k * n.y), .z = get_narrow(k * n.z), .w = 0.0f};
    const double bounce_w = k * n.w;
    // Friction against what's across the normal.
    const float tx = get_narrow(vel_.x - along * n.x);
    const double ty = vel_.y - along * n.y;
    const float tz = get_narrow(vel_.z - get_wide(get_narrow(along * n.z)));
    const double tw = vel_.w - get_wide(get_narrow(along * n.w));
    const double length = std::sqrt((get_wide(tz) * tz + ty * ty) + get_wide(tx) * tx);
    const float length_f = get_narrow(length);
    if (length > kMinLength) {
        const double inverse = 1.0 / length_f;
        const float ux = get_narrow(tx * inverse);
        const float uy = get_narrow(ty * inverse);
        const float uz = get_narrow(tz * inverse);
        const double uw = tw * inverse;
        const float most = get_narrow(
            std::sqrt((get_wide(bounce.z) * bounce.z + get_wide(bounce.y) * bounce.y) + get_wide(bounce.x) * bounce.x) *
            drag_factor_);
        const double f = -(length_f > most ? most : length_f);
        vel_ = {.x = get_narrow(ux * f + vel_.x), .y = append_one(get_narrow(uy * f), vel_.y),
                .z = append_one(get_narrow(uz * f), vel_.z), .w = append_one(get_narrow(uw * f), vel_.w)};
    }
    vel_ = {.x = append_one(bounce.x, vel_.x), .y = append_one(bounce.y, vel_.y), .z = append_one(bounce.z, vel_.z),
            .w = get_narrow(bounce_w + vel_.w)};
    on_bounce(get_narrow(-along), &feeler->contact_pos_);
}

// 0x00430E20
void BodyDynamicsModule::tick_frame()
{
    if (cur_phase_ == kStateDisabled)
        return;
    const float frame = get_narrow(frame_clock::get_slomo_frame_seconds());
    if (push_cooldown_ > kZero)
        push_cooldown_ = get_sub(push_cooldown_, frame);
    else if (!(flag_bits_ & kFlagNoPlayerCollision))
        feel_pushers();
    if (die_timer_ >= kZero) {
        if (die_timer_ <= frame)
            flag_as_dead(entity_);
        else
            die_timer_ = get_sub(die_timer_, frame);
    }
    if (cur_phase_ == kStateAsleep) {
        entity_->display_matrix_ = basis_frame_;
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
        feeler.line_.start = at_point_;
        feeler.line_.end = {
            .x = get_narrow(half * steady_accel_.x + (get_wide(get_narrow(vx)) + at_point_.x)),
            .y = get_narrow(get_wide(get_narrow(half * steady_accel_.y)) + (get_wide(get_narrow(vy)) + at_point_.y)),
            .z = get_narrow(get_wide(get_narrow(half * steady_accel_.z)) + (vz + at_point_.z)),
            .w = get_narrow(get_wide(get_narrow(half * steady_accel_.w)) + get_narrow(vw + at_point_.w))};
        const bool hit = feeler.get_hit_shape(false, false) != 0;
        if (hit) {
            const maths::Seg& line = feeler.line_;
            const double dx = get_wide(line.end.x) - line.start.x;
            const double dy = get_wide(line.end.y) - line.start.y;
            const double dz = get_wide(line.end.z) - line.start.z;
            const double path = std::sqrt((dx * dx + dy * dy) + dz * dz);
            const double speed =
                std::sqrt((get_wide(vel_.x) * vel_.x + get_wide(vel_.y) * vel_.y) + get_wide(vel_.z) * vel_.z);
            length = get_narrow((path * feeler.reach_ - kHitBackOff) / speed);
            if (length < kZero)
                length = 0.0f;
        }
        integrate(length);
        if (hit)
            collide(&feeler);
        remaining = get_sub(remaining, length);
        if (++step == kMaxSteps) {
            sleep();
            break;
        }
        if (!hit)
            break;
    }

    if (find_contacts())
        resolve_contacts();
    if (spring_count_)
        apply_springs();
    damp();
    check_sleep();

    maths::Vec4 offset{};
    basis_frame_.rotate_vector(offset, model_offset_);
    entity_->at_point_ = {.x = append_one(at_point_.x, offset.x), .y = append_one(at_point_.y, offset.y),
                          .z = append_one(at_point_.z, offset.z), .w = append_one(at_point_.w, offset.w)};
    entity_->basis_frame_ = basis_frame_;
    entity_->display_matrix_ = basis_frame_;
    entity_->vel_ = vel_;
    if (draw_shape())
        debug_draw();
}

} // namespace objects
