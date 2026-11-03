// The Vehicle component's collision and integration: the colliders against the world (contact points, impulses,
// depth correction, friction, steering against walls), moving the body without tunnelling, righting itself, turning
// in the air, and a whole sub-step. See vehicle_component.h. From the disassembly of 0x00436CF0-0x00437549,
// 0x00438770-0x00438A16, 0x00438BB0-0x00439D86, 0x00439D90-0x0043A066 and 0x0043BEE0-0x0043C19A. Float arithmetic
// follows the original's x87 code: double (53 bits) where it stays on the stack, rounded to float where stored.

#include "engine/objects/model_component.h"
#include "engine/objects/vehicle_component.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "engine/core/dbg.h"
#include "engine/gfx/debug_lines.h"
#include "engine/maths/rotation_blend.h"
#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/render/coll_rect.h"
#include "engine/objects/vehicle_internal.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/trigger_events.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

using namespace vehicle;

namespace {

const float kHalf = 0.5f;                                         // 0x006453BC
const float kMinusHalf = -0.5f;                                   // 0x0064642C
const float kShallowestLevel = std::bit_cast<float>(0x3C23D70Au); // 0x0064544C: 0.01
const float kSameEnd = std::bit_cast<float>(0x3DCCCCCDu);         // 0x00645D74: 0.1
const double kOpposite = -0.99;                                   // 0x00646730 (a double)
const float kBounce = 1.5f;                                       // 0x00646744
const float kDropTest = 4800.0f;                                  // 0x006461D0
const float kDeepestLevel = 2.0f;                                 // 0x00645448
const float kUpright = std::bit_cast<float>(0x3F34FDF4u);         // 0x006467BC: 0.707
const float kMinSlide = std::bit_cast<float>(0x3A83126Fu);        // 0x006461D8: 0.001
const float kRightingUp = std::bit_cast<float>(0x3F7FBE77u);      // 0x006467B8: 0.999
const float kRightingSpeed = 90000.0f;                            // 0x006467B4 (300 squared)
const float kRightingSpin = std::bit_cast<float>(0x3F1DE9E7u);    // 0x006467B0: 0.61685
const float kRightingTilt = 0.25f;                                // 0x006467AC
const float kRightingRate = 3.0f;                                 // 0x006467A8
const float kAlignFast = 122500.0f;                               // 0x006467C4 (350 squared)
const float kAlignSlowRate = std::bit_cast<float>(0x3E99999Au);   // 0x006467C0: 0.3

// The model component's 0x00420670 (not rewritten yet): its lighting from what a feeler hit.
void light_from_feeler(ObjectModule* model, render::ProbeRay* feeler, bool b, float c)
{
    core::down_cast<MeshInstanceModule>(model)->apply_brightness(feeler, b, c);
}

bool get_interrupted(const DrivableModule* vehicle)
{
    return vehicle->cut_short() || (vehicle->entity()->object_flags() & kObjectFlagDead);
}

} // namespace

// 0x00436CF0 (stdcall)
double DrivableModule::get_penetration(const Contact* contact, const Collider* collider)
{
    // The contact point's distance in from each of the face's four edges; the nearest.
    const double dx = static_cast<double>(contact->point.x) - collider->world_corner_.x;
    const double dy = static_cast<double>(contact->point.y) - collider->world_corner_.y;
    const double dz = static_cast<double>(contact->point.z) - collider->world_corner_.z;
    const maths::Vec4& a = core::at(collider->directions, 0);
    const maths::Vec4& b = core::at(collider->directions, 1);
    int side = 0;
    auto nearest = static_cast<float>((dz * a.z + dy * a.y) + dx * a.x);
    double distance = static_cast<double>(core::at(collider->lengths, 0)) - nearest;
    if (distance < nearest) {
        nearest = static_cast<float>(distance);
        side = 1;
    }
    distance = (dz * b.z + dy * b.y) + dx * b.x;
    if (distance < nearest) {
        nearest = static_cast<float>(distance);
        side = 2;
    }
    distance = core::at(collider->lengths, 1) - distance;
    if (distance < nearest) {
        nearest = static_cast<float>(distance);
        side = 3;
    }
    if (nearest < kShallowestLevel)
        return 0.0;
    // That distance along the contact's normal.
    const maths::Vec4& n = contact->normal;
    switch (side) {
    case 0:
        return nearest /
               ((static_cast<double>(a.z) * n.z + static_cast<double>(a.y) * n.y) + static_cast<double>(n.x) * a.x);
    case 1:
        return -(nearest /
                 ((static_cast<double>(a.z) * n.z + static_cast<double>(a.y) * n.y) + static_cast<double>(a.x) * n.x));
    case 2:
        return nearest /
               ((static_cast<double>(b.z) * n.z + static_cast<double>(b.y) * n.y) + static_cast<double>(b.x) * n.x);
    default:
        return -(nearest /
                 ((static_cast<double>(b.z) * n.z + static_cast<double>(b.y) * n.y) + static_cast<double>(b.x) * n.x));
    }
}

// 0x00436E50
bool DrivableModule::find_edge_contact(Contact* contact, render::BoxProbe* rect, const Collider* collider, int hit,
                                       int end)
{
    // Another joined hit with an end at this point (within 0.1) facing the opposite way.
    for (int k = rect->joined_count() - 1; k >= 0; --k) {
        if (k == hit)
            continue;
        const render::RectHit& other = core::at(rect->joined(), k);
        for (int e = 1; e >= 0; --e) {
            const maths::Vec4& p = core::at(other.ends, e).point;
            if (!(std::fabs(static_cast<double>(contact->point.x) - p.x) < kSameEnd))
                continue;
            if (!(std::fabs(static_cast<double>(contact->point.y) - p.y) < kSameEnd))
                continue;
            if (!(std::fabs(static_cast<double>(contact->point.z) - p.z) < kSameEnd))
                continue;
            const maths::Vec4& n = contact->normal;
            if (!((static_cast<double>(n.z) * other.normal.z + static_cast<double>(n.y) * other.normal.y) +
                      static_cast<double>(n.x) * other.normal.x <
                  kOpposite))
                continue;
            // On the edge between them: the normal across the hit's triangle edge, and the depth.
            const maths::Vec4& edge = core::at(core::at(rect->joined(), hit).ends, end).edge;
            const double x = static_cast<double>(edge.y) * n.z - static_cast<double>(edge.z) * n.y;
            const double y = static_cast<double>(edge.z) * n.x - static_cast<double>(edge.x) * n.z;
            contact->normal.w = 0.0f;
            contact->normal.z =
                static_cast<float>(static_cast<double>(edge.x) * n.y - static_cast<double>(edge.y) * n.x);
            contact->normal.x = static_cast<float>(x);
            contact->normal.y = static_cast<float>(y);
            contact->depth = static_cast<float>(get_penetration(contact, collider));
            if (debug_mode_) {
                const maths::Vec4 tip{
                    .x = static_cast<float>(static_cast<double>(contact->normal.x) * 72.0f + contact->point.x),
                    .y = static_cast<float>(static_cast<double>(contact->normal.y) * 72.0f + contact->point.y),
                    .z = static_cast<float>(static_cast<double>(static_cast<float>(contact->normal.z * 72.0f)) +
                                       contact->point.z),
                    .w = static_cast<float>(static_cast<double>(static_cast<float>(contact->normal.w * 72.0f)) +
                                       contact->point.w)};
                gfx::push_diag_line(&contact->point, &tip, 0xFFFF64FF, 0xFFFF64FF, 1);
            }
            return true;
        }
    }
    return false;
}

// 0x00437060
bool DrivableModule::normal_faces_away(const Contact* contact)
{
    const maths::Vec4& p = contact->point;
    const maths::Vec4& n = contact->normal;
    const double along =
        ((static_cast<double>(p.z) - at_point_.z) * n.z + (static_cast<double>(p.y) - at_point_.y) * n.y) +
        (static_cast<double>(p.x) - at_point_.x) * n.x;
    const bool away = along >= 0.0;
    if (debug_mode_ && away) {
        const maths::Vec4 tip{.x = static_cast<float>(static_cast<double>(n.x) * 48.0f + p.x),
                              .y = static_cast<float>(static_cast<double>(n.y) * 48.0f + p.y),
                              .z = static_cast<float>(static_cast<double>(static_cast<float>(n.z * 48.0f)) + p.z),
                              .w = static_cast<float>(static_cast<double>(static_cast<float>(n.w * 48.0f)) + p.w)};
        gfx::push_diag_line(&p, &tip, 0xFF000000, 0xFF000000, 1);
    }
    return away;
}

// 0x00437120
void DrivableModule::move_by(const maths::Vec4* move)
{
    const maths::Vec4 saved = at_point_;
    const maths::Vec4 end{.x = static_cast<float>(static_cast<double>(move->x) + at_point_.x),
                          .y = static_cast<float>(static_cast<double>(at_point_.y) + move->y),
                          .z = static_cast<float>(static_cast<double>(at_point_.z) + move->z),
                          .w = static_cast<float>(static_cast<double>(at_point_.w) + move->w)};
    render::ProbeRay feeler(&at_point_, &end);
    if (feeler.get_hit_shape(false, false)) {
        // Going through something: put just off the surface, the rest of the move reflected, the momentum into it
        // turned back by half again.
        debug::printf(kDebugFormat, kDebugFile, 0xB10, "less than uber frig"); // "less than uber frig"
        const maths::Vec4& point = feeler.contact_pos();
        const maths::Vec4& n = feeler.surface_dir();
        at_point_.x = static_cast<float>(static_cast<double>(n.x) + point.x);
        at_point_.y = static_cast<float>(static_cast<double>(point.y) + n.y);
        at_point_.z = static_cast<float>(static_cast<double>(point.z) + n.z);
        at_point_.w = static_cast<float>(static_cast<double>(point.w) + n.w);
        const double distance = feeler.reach();
        const double vx = distance * move->x;
        const double vy = distance * move->y;
        const double vz = distance * move->z;
        const double vw = distance * move->w;
        double twice = (static_cast<double>(n.x) * vx + static_cast<double>(n.y) * vy) + static_cast<double>(n.z) * vz;
        twice = twice + twice;
        auto kw = static_cast<float>(static_cast<double>(n.w) * twice);
        auto kz = static_cast<float>(static_cast<double>(n.z) * twice);
        auto ky = static_cast<float>(static_cast<double>(n.y) * twice);
        const double kx = twice * n.x;
        auto rx = static_cast<float>(vx - kx);
        auto ry = static_cast<float>(vy - ky);
        const double rz = vz - kz;
        auto rw = static_cast<float>(vw - kw);
        at_point_.x = static_cast<float>(static_cast<double>(rx) + at_point_.x);
        at_point_.y = static_cast<float>(static_cast<double>(ry) + at_point_.y);
        at_point_.z = static_cast<float>(rz + at_point_.z);
        at_point_.w = static_cast<float>(static_cast<double>(rw) + at_point_.w);
        const double into =
            ((static_cast<double>(n.z) * linear_momentum_.z + static_cast<double>(n.y) * linear_momentum_.y) +
             static_cast<double>(n.x) * linear_momentum_.x) *
            kBounce;
        linear_momentum_.x = static_cast<float>(linear_momentum_.x - static_cast<double>(n.x) * into);
        linear_momentum_.y = static_cast<float>(linear_momentum_.y - static_cast<double>(n.y) * into);
        linear_momentum_.z = static_cast<float>(linear_momentum_.z - static_cast<double>(n.z) * into);
        linear_momentum_.w = static_cast<float>(linear_momentum_.w - static_cast<double>(n.w) * into);
    } else {
        at_point_.x = static_cast<float>(static_cast<double>(at_point_.x) + move->x);
        at_point_.y = static_cast<float>(static_cast<double>(move->y) + at_point_.y);
        at_point_.z = static_cast<float>(static_cast<double>(at_point_.z) + move->z);
        at_point_.w = static_cast<float>(static_cast<double>(at_point_.w) + move->w);
    }
    // Nothing below at all (4800 down): through the world - put back where it was. Else the model lit from the
    // ground.
    feeler.line().start = at_point_;
    feeler.line().end = at_point_;
    feeler.line().end.y = static_cast<float>(static_cast<double>(at_point_.y) - kDropTest);
    if (!feeler.get_hit_shape(false, false)) {
        debug::printf(kDebugFormat, kDebugFile, 0xB26, "uber frig"); // "uber frig"
        at_point_ = saved;
    } else {
        light_from_feeler(mesh_instance_, &feeler, true, 1.0f);
    }
}

// 0x00437450
double DrivableModule::get_impulse_denominator(const maths::Vec4* offset, const maths::Vec4* direction)
{
    // 1 / (n . (n / m + (I^-1 (r x n)) x r)).
    const maths::Vec4& r = *offset;
    const maths::Vec4& n = *direction;
    const float inv_mass = inv_mass_;
    maths::Vec4 cross{};
    cross.x = static_cast<float>(static_cast<double>(n.z) * r.y - static_cast<double>(r.z) * n.y);
    cross.y = static_cast<float>(static_cast<double>(r.z) * n.x - static_cast<double>(r.x) * n.z);
    cross.z = static_cast<float>(static_cast<double>(n.y) * r.x - static_cast<double>(n.x) * r.y);
    cross.w = 0.0f;
    auto linear_x = static_cast<float>(static_cast<double>(n.x) * inv_mass);
    auto linear_y = static_cast<float>(static_cast<double>(inv_mass) * n.y);
    auto linear_z = static_cast<float>(static_cast<double>(n.z) * inv_mass);
    maths::Vec4 w{};
    inv_inertia_.rotate_vector(w, cross);
    const double tx = static_cast<double>(w.y) * r.z - static_cast<double>(w.z) * r.y;
    const double ty = static_cast<double>(w.z) * r.x - static_cast<double>(w.x) * r.z;
    auto tz = static_cast<float>(static_cast<double>(w.x) * r.y - static_cast<double>(w.y) * r.x);
    auto ux = static_cast<float>(tx + linear_x);
    const double uy = ty + linear_y;
    const double uz = static_cast<double>(tz) + linear_z;
    return 1.0 / ((uy * n.y + uz * n.z) + static_cast<double>(ux) * n.x);
}

// 0x00438770
void DrivableModule::update_righting()
{
    quat_to_matrix(&orientation_, &basis_frame_);
    core::at(basis_frame_.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    if (core::at(basis_frame_.row, 1).y > kRightingUp) {
        righting_ = false;
        return;
    }
    // Upside down (or on its side), still, after a collision, with the throttle held: turn upright.
    const double vx = vel_.x;
    const double vy = vel_.y;
    const double vz = vel_.z;
    const double sx = rotvel_.x;
    const double sy = rotvel_.y;
    const double sz = rotvel_.z;
    const bool stuck = (vx * vx + vy * vy) + vz * vz < kRightingSpeed &&
                       (sx * sx + sy * sy) + sz * sz < kRightingSpin && collision_impulse_ > 0.0f &&
                       core::at(basis_frame_.row, 1).y < kRightingTilt &&
                       (wheels_in_contact_ == 0 || core::at(basis_frame_.row, 1).y < 0.0f) && throttle_;
    if (!righting_) {
        if (!stuck)
            return;
        linear_momentum_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
        rotmom_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
        righting_ = true;
        righting_start_ = frame_clock::game_clock_elapsed() + clock_offset();
    }
    int start = righting_start_;
    if (static_cast<uint32_t>(frame_clock::game_clock_elapsed()) +
            (static_cast<uint32_t>(clock_offset()) - static_cast<uint32_t>(start)) >
        5300) {
        righting_ = false;
        return;
    }
    start = righting_start_;
    if (static_cast<uint32_t>(frame_clock::game_clock_elapsed()) +
            (static_cast<uint32_t>(clock_offset()) - static_cast<uint32_t>(start)) <
        300)
        return;
    // After 300 ms, turned towards upright facing the same way.
    maths::Vec4 forward{.x = core::at(basis_frame_.row, 2).x, .y = 0.0f, .z = core::at(basis_frame_.row, 2).z, .w = core::at(basis_frame_.row, 2).w};
    forward.make_unit();
    maths::Mat4 target;
    core::at(target.row, 0) = {.x = forward.z, .y = 0.0f, .z = -forward.x, .w = 1.0f};
    core::at(target.row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};
    core::at(target.row, 2) = forward;
    core::at(target.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}; // (the original leaves its stack there)
    maths::ArcRotationBlender blend{}; // (on the stack without a constructor in the original: zeroed here)
    maths::ArcRotationBlender* slerp = &blend;
    slerp->set_matrices(basis_frame_, target);
    const double t = frame_clock::get_slomo_frame_seconds() / slerp->angle() * kRightingRate;
    slerp->fetch_mat(basis_frame_, t > 1.0 ? 1.0f : static_cast<float>(t));
    maths::Vec4 quat{};
    matrix_to_quat(&quat, &basis_frame_);
    orientation_ = quat;
}

// 0x00439D90
void DrivableModule::align_in_air()
{
    if (righting_)
        return;
    if (kick_time_ > 0.0f)
        return;
    double vx = vel_.x, vz = vel_.z;
    const double align = align_speed_;
    const bool slow = align * align > vx * vx + vz * vz;
    if (slow && !align_when_slow_)
        return;
    quat_to_matrix(&orientation_, &basis_frame_);
    core::at(basis_frame_.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    // Slow: towards its facing (level); fast: towards where it's going.
    maths::Vec4 at{};
    if (slow)
        at = {.x = core::at(basis_frame_.row, 2).x, .y = 0.0f, .z = core::at(basis_frame_.row, 2).z, .w = core::at(basis_frame_.row, 2).w};
    else
        at = vel_;
    at.make_unit();
    if ((static_cast<double>(at.z) * core::at(basis_frame_.row, 2).z + static_cast<double>(at.y) * core::at(basis_frame_.row, 2).y) +
            static_cast<double>(at.x) * core::at(basis_frame_.row, 2).x <
        0.0) {
        at.x = at.x * -1.0f;
        at.y = at.y * -1.0f;
        at.z = at.z * -1.0f;
        at.w = at.w * -1.0f;
    }
    const maths::Vec4 up{.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};
    maths::Mat4 target;
    core::at(target.row, 0) = maths::cross_vec(up, at);
    core::at(target.row, 1) = maths::cross_vec(at, core::at(target.row, 0));
    core::at(target.row, 2) = at;
    core::at(target.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    maths::ArcRotationBlender blend{}; // (on the stack without a constructor in the original: zeroed here)
    maths::ArcRotationBlender* slerp = &blend;
    slerp->set_matrices(basis_frame_, target);
    // The turn ramps in over alignTime of free fall; slower under 350.
    vx = vel_.x;
    vz = vel_.z;
    const bool fast = vx * vx + vz * vz > kAlignFast;
    const float time = align_time_;
    const float falling = free_fall_time_;
    const double ramp = falling < time ? falling : time;
    const double rate = fast ? static_cast<double>(align_rate_) * step_length_
                             : static_cast<double>(align_rate_) * step_length_ * kAlignSlowRate;
    slerp->fetch_mat(basis_frame_, static_cast<float>(ramp / time * rate));
    maths::Vec4 quat{};
    matrix_to_quat(&quat, &basis_frame_);
    orientation_ = quat;
}

// 0x0043BEE0
void DrivableModule::integrate()
{
    // The accelerations.
    const double inv_mass = inv_mass_;
    auto accel_x = static_cast<float>(inv_mass * force_.x), accel_y = static_cast<float>(inv_mass * force_.y);
    auto accel_z = static_cast<float>(inv_mass * force_.z), accel_w = static_cast<float>(inv_mass * force_.w);
    maths::Vec4 spin_accel{};
    inv_inertia_.rotate_vector(spin_accel, twist_force_);
    // The momentum.
    double dt = step_length_;
    auto dy = static_cast<float>(dt * force_.y), dz = static_cast<float>(dt * force_.z);
    auto dw = static_cast<float>(dt * force_.w);
    linear_momentum_.x = static_cast<float>(dt * force_.x + linear_momentum_.x);
    linear_momentum_.y = static_cast<float>(static_cast<double>(dy) + linear_momentum_.y);
    linear_momentum_.z = static_cast<float>(static_cast<double>(dz) + linear_momentum_.z);
    linear_momentum_.w = static_cast<float>(static_cast<double>(dw) + linear_momentum_.w);
    // The move: (velocity + half the acceleration over the step) over the step.
    const float timestep = step_length_;
    double half = static_cast<double>(step_length_) * kHalf;
    auto hy = static_cast<float>(accel_y * half), hz = static_cast<float>(accel_z * half);
    auto hw = static_cast<float>(accel_w * half);
    const double vx = accel_x * half + vel_.x;
    const double vy = static_cast<double>(hy) + vel_.y;
    auto vz = static_cast<float>(static_cast<double>(hz) + vel_.z);
    auto vw = static_cast<float>(static_cast<double>(hw) + vel_.w);
    const maths::Vec4 move{.x = static_cast<float>(vx * timestep), .y = static_cast<float>(vy * timestep),
                           .z = static_cast<float>(static_cast<double>(vz) * timestep),
                           .w = static_cast<float>(static_cast<double>(vw) * timestep)};
    move_by(&move);
    // The orientation: q += q * (-dt / 2) * (angular velocity + half the angular acceleration over the step).
    half = static_cast<double>(step_length_) * kHalf;
    maths::Vec4 quat = orientation_;
    const double wx = spin_accel.x * half + rotvel_.x;
    auto wy = static_cast<float>(static_cast<double>(static_cast<float>(spin_accel.y * half)) + rotvel_.y);
    auto wz = static_cast<float>(static_cast<double>(static_cast<float>(spin_accel.z * half)) + rotvel_.z);
    auto scale = static_cast<float>(static_cast<double>(step_length_) * kMinusHalf);
    const maths::Vec4 turn{.x = static_cast<float>(wx * scale), .y = static_cast<float>(static_cast<double>(wy) * scale),
                           .z = static_cast<float>(static_cast<double>(wz) * scale), .w = 0.0f};
    quat_multiply(&quat, &turn);
    orientation_.x = static_cast<float>(static_cast<double>(quat.x) + orientation_.x);
    orientation_.y = static_cast<float>(static_cast<double>(quat.y) + orientation_.y);
    orientation_.z = static_cast<float>(static_cast<double>(quat.z) + orientation_.z);
    orientation_.w = static_cast<float>(static_cast<double>(quat.w) + orientation_.w);
    quat_normalize(&orientation_, 1.0f);
    // The angular momentum.
    dt = step_length_;
    dy = static_cast<float>(dt * twist_force_.y);
    dz = static_cast<float>(dt * twist_force_.z);
    dw = static_cast<float>(dt * twist_force_.w);
    rotmom_.x = static_cast<float>(dt * twist_force_.x + rotmom_.x);
    rotmom_.y = static_cast<float>(static_cast<double>(dy) + rotmom_.y);
    rotmom_.z = static_cast<float>(static_cast<double>(dz) + rotmom_.z);
    rotmom_.w = static_cast<float>(static_cast<double>(dw) + rotmom_.w);
    update_wheel_rotations();
    if (wheels_in_contact_ == 0 && collision_impulse_ == 0.0f)
        align_in_air();
    update_righting();
    update_dependents();
}

// 0x00438BB0
void DrivableModule::collide()
{
    collision_impulse_ = 0.0f;
    render::BoxProbe rect;
    render::ProbeRay feeler;
    rect.set_hit_cache(&contact_memo_);
    feeler.set_hit_cache(&contact_memo_);
    contact_count_ = 0;
    const std::span<Contact> contacts = get_contacts();
    // Each collider's face against the world: where it crosses the world's faces, and lines from the body's
    // position to its four corners.
    for (int i = 1; i >= 0; --i) {
        const Collider& collider = core::at(colliders_, i);
        rect.rect().origin = collider.world_corner_;
        rect.rect().side_a_ = core::at(collider.world_edges_, 0);
        rect.rect().side_b_ = core::at(collider.world_edges_, 1);
        if (rect.get_query(0)) {
            trigger_rect(trigger_ptr_, kCrossingEvents, &rect);
            if (get_interrupted(this))
                return;
            rect.join_hits();
            for (int k = rect.joined_count() - 1; k >= 0; --k) {
                const render::RectHit& hit = core::at(rect.joined(), k);
                for (int e = 1; e >= 0; --e) {
                    Contact& contact = core::at(contacts, contact_count_);
                    contact.point = core::at(hit.ends, e).point;
                    contact.normal = hit.normal;
                    contact.from_feeler_ = false;
                    const bool on_edge = core::at(hit.ends, e).on_triangle_edge_;
                    if (!normal_faces_away(&contact)) {
                        contact.depth = on_edge ? static_cast<float>(get_penetration(&contact, &collider)) : 0.0f;
                        ++contact_count_;
                    } else if (on_edge && find_edge_contact(&contact, &rect, &collider, k, e)) {
                        ++contact_count_;
                    }
                }
            }
        }
        if (debug_mode_)
            rect.debug_draw(0xFF, 0xFF, 0xFF, 1);
        feeler.line().start = at_point_;
        feeler.line().end = rect.rect().origin;
        for (int corner = 0; corner < 4; ++corner) {
            if (corner == 1) {
                feeler.line().end = {.x = static_cast<float>(static_cast<double>(feeler.line().end.x) + rect.rect().side_a_.x),
                                    .y = static_cast<float>(static_cast<double>(rect.rect().side_a_.y) + feeler.line().end.y),
                                    .z = static_cast<float>(static_cast<double>(rect.rect().side_a_.z) + feeler.line().end.z),
                                    .w = static_cast<float>(static_cast<double>(rect.rect().side_a_.w) + feeler.line().end.w)};
            } else if (corner == 2) {
                feeler.line().end = {.x = static_cast<float>(static_cast<double>(feeler.line().end.x) + rect.rect().side_b_.x),
                                    .y = static_cast<float>(static_cast<double>(rect.rect().side_b_.y) + feeler.line().end.y),
                                    .z = static_cast<float>(static_cast<double>(rect.rect().side_b_.z) + feeler.line().end.z),
                                    .w = static_cast<float>(static_cast<double>(rect.rect().side_b_.w) + feeler.line().end.w)};
            } else if (corner == 3) {
                feeler.line().end = {.x = static_cast<float>(static_cast<double>(feeler.line().end.x) - rect.rect().side_a_.x),
                                    .y = static_cast<float>(static_cast<double>(feeler.line().end.y) - rect.rect().side_a_.y),
                                    .z = static_cast<float>(static_cast<double>(feeler.line().end.z) - rect.rect().side_a_.z),
                                    .w = static_cast<float>(static_cast<double>(feeler.line().end.w) - rect.rect().side_a_.w)};
            }
            if (feeler.get_hit_shape(false, false)) {
                trigger_feeler(trigger_ptr_, kCrossingEvents, &feeler);
                if (get_interrupted(this))
                    return;
                Contact& contact = core::at(contacts, contact_count_);
                contact.point = feeler.contact_pos();
                contact.normal = feeler.surface_dir();
                if (!normal_faces_away(&contact)) {
                    // How far in: the rest of the line, along the normal.
                    const maths::Seg& line = feeler.line();
                    const double dz = static_cast<double>(line.start.z) - line.end.z;
                    const double dy = static_cast<double>(line.start.y) - line.end.y;
                    const double dx = static_cast<double>(line.start.x) - line.end.x;
                    contact.depth = static_cast<float>(
                        (1.0 - feeler.reach()) *
                        ((dz * feeler.surface_dir().z + dy * feeler.surface_dir().y) + dx * feeler.surface_dir().x));
                    ++contact_count_;
                    // (the original marks the next record, not this one - the one past the table when it is full)
                    if (static_cast<std::size_t>(contact_count_) < contacts.size())
                        core::at(contacts, contact_count_).from_feeler_ = true;
                }
            }
            if (debug_mode_)
                feeler.debug_draw(0xFF, 0xFF, 0xFF, 1);
        }
    }
    if (contact_count_ == 0)
        return;
    for (int n = contact_count_ - 1; n >= 0; --n)
        core::at(contacts, n).impulse = 0.0f;
    maths::Vec4 mom = linear_momentum_, rotmom = rotmom_, vel = vel_;
    maths::Vec4 rotvel = rotvel_;

    // Impulses at the contacts closing in (with restitution), up to six passes until none is.
    for (int pass = 0;;) {
        bool separating = true;
        for (int n = contact_count_ - 1; n >= 0; --n) {
            Contact& contact = core::at(contacts, n);
            const maths::Vec4 r{.x = static_cast<float>(static_cast<double>(contact.point.x) - at_point_.x),
                                .y = static_cast<float>(static_cast<double>(contact.point.y) - at_point_.y),
                                .z = static_cast<float>(static_cast<double>(contact.point.z) - at_point_.z),
                                .w = static_cast<float>(static_cast<double>(contact.point.w) - at_point_.w)};
            maths::Vec4 v{};
            get_point_velocity(&v, &r);
            const maths::Vec4& nrm = contact.normal;
            const double closing_exact = (static_cast<double>(v.z) * nrm.z + static_cast<double>(v.x) * nrm.x) +
                                         static_cast<double>(v.y) * nrm.y;
            auto closing = static_cast<float>(closing_exact);
            if (closing_exact > 0.0)
                continue;
            separating = false;
            auto bounce = static_cast<float>(-(static_cast<double>(closing) * restitution_));
            const double denominator = get_impulse_denominator(&r, &nrm);
            auto impulse = static_cast<float>((static_cast<double>(bounce) - closing) * denominator);
            contact.impulse = static_cast<float>(static_cast<double>(impulse) + contact.impulse);
            const maths::Vec4 push{.x = static_cast<float>(static_cast<double>(impulse) * nrm.x),
                                   .y = static_cast<float>(static_cast<double>(impulse) * nrm.y),
                                   .z = static_cast<float>(static_cast<double>(impulse) * nrm.z),
                                   .w = static_cast<float>(static_cast<double>(impulse) * nrm.w)};
            apply_impulse_at_offset(&push, &r);
        }
        ++pass;
        if (pass > 5 || separating)
            break;
    }

    // Pushed out by how deep each contact is (at most 2).
    for (int n = contact_count_ - 1; n >= 0; --n) {
        Contact& contact = core::at(contacts, n);
        if (contact.depth < 0.0f || contact.depth == 0.0f)
            continue;
        if (contact.depth > kDeepestLevel)
            contact.depth = kDeepestLevel;
        const double push = static_cast<double>(depth_correction_) * contact.depth;
        auto push_rounded = static_cast<float>(push);
        const maths::Vec4& nrm = contact.normal;
        const maths::Vec4 force{.x = static_cast<float>(static_cast<double>(push_rounded) * nrm.x),
                                .y = static_cast<float>(static_cast<double>(push_rounded) * nrm.y),
                                .z = static_cast<float>(static_cast<double>(push_rounded) * nrm.z),
                                .w = static_cast<float>(push * nrm.w)};
        const maths::Vec4 r{.x = static_cast<float>(static_cast<double>(contact.point.x) - at_point_.x),
                            .y = static_cast<float>(static_cast<double>(contact.point.y) - at_point_.y),
                            .z = static_cast<float>(static_cast<double>(contact.point.z) - at_point_.z),
                            .w = static_cast<float>(static_cast<double>(contact.point.w) - at_point_.w)};
        add_force_at_offset(&force, &r);
    }

    // Friction against the sliding at each contact that took an impulse (on the velocities as they were, updated
    // by each friction impulse in turn); at most the friction coefficient times that impulse.
    const float friction = core::at(basis_frame_.row, 1).y > kUpright ? drag_factor_ : tipped_friction_;
    for (int n = contact_count_ - 1; n >= 0; --n) {
        const Contact& contact = core::at(contacts, n);
        if (contact.impulse == 0.0f)
            continue;
        const maths::Vec4& nrm = contact.normal;
        auto rz = static_cast<float>(static_cast<double>(contact.point.z) - at_point_.z);
        auto ry = static_cast<float>(static_cast<double>(contact.point.y) - at_point_.y);
        const maths::Vec4 r{.x = static_cast<float>(static_cast<double>(contact.point.x) - at_point_.x), .y = ry, .z = rz,
                            .w = static_cast<float>(static_cast<double>(contact.point.w) - at_point_.w)};
        const double cz = static_cast<double>(ry) * rotvel.x - static_cast<double>(r.x) * rotvel.y;
        const double cy = static_cast<double>(r.x) * rotvel.z - static_cast<double>(rz) * rotvel.x;
        auto cx = static_cast<float>(static_cast<double>(rz) * rotvel.y - static_cast<double>(ry) * rotvel.z);
        auto pvz = static_cast<float>(cz + vel.z);
        const double pvy = cy + vel.y;
        const double pvx = static_cast<double>(cx) + vel.x;
        const double along = (static_cast<double>(pvz) * nrm.z + pvx * nrm.x) + pvy * nrm.y;
        auto tz = static_cast<float>(along * nrm.z);
        auto ty = static_cast<float>(along * nrm.y);
        auto tx = static_cast<float>(along * nrm.x);
        auto slide_w = static_cast<float>(vel.w - along * nrm.w);
        auto slide_z = static_cast<float>(static_cast<double>(pvz) - tz);
        auto slide_y = static_cast<float>(pvy - ty);
        const double slide_x = pvx - tx;
        const double speed = std::sqrt((slide_x * slide_x + static_cast<double>(slide_y) * slide_y) +
                                       static_cast<double>(slide_z) * slide_z);
        auto speed_rounded = static_cast<float>(speed);
        if (speed < kMinSlide)
            continue;
        const double inverse = 1.0 / speed_rounded;
        const maths::Vec4 slide{.x = static_cast<float>(slide_x * inverse), .y = static_cast<float>(slide_y * inverse),
                                .z = static_cast<float>(slide_z * inverse), .w = static_cast<float>(slide_w * inverse)};
        double stop = get_impulse_denominator(&r, &slide) * speed_rounded;
        auto most = static_cast<float>(static_cast<double>(friction) * contact.impulse);
        if (stop > most)
            stop = most;
        const double against = -stop;
        const maths::Vec4 push{.x = static_cast<float>(static_cast<double>(slide.x) * against),
                               .y = static_cast<float>(static_cast<double>(slide.y) * against),
                               .z = static_cast<float>(static_cast<double>(slide.z) * against),
                               .w = static_cast<float>(static_cast<double>(slide.w) * against)};
        apply_impulse_at_offset(&push, &r);
        // The copies of the momenta and velocities followed along.
        mom.x = static_cast<float>(static_cast<double>(push.x) + mom.x);
        mom.y = static_cast<float>(static_cast<double>(push.y) + mom.y);
        mom.z = static_cast<float>(static_cast<double>(push.z) + mom.z);
        mom.w = static_cast<float>(static_cast<double>(push.w) + mom.w);
        const double az = static_cast<double>(push.y) * r.x - static_cast<double>(push.x) * ry;
        const double ay = static_cast<double>(push.x) * rz - static_cast<double>(push.z) * r.x;
        const double ax = static_cast<double>(push.z) * ry - static_cast<double>(push.y) * rz;
        rotmom.x = static_cast<float>(rotmom.x + ax);
        rotmom.y = static_cast<float>(ay + rotmom.y);
        rotmom.z = static_cast<float>(az + rotmom.z);
        const double inv_mass = inv_mass_;
        vel = {.x = static_cast<float>(inv_mass * mom.x), .y = static_cast<float>(inv_mass * mom.y),
               .z = static_cast<float>(inv_mass * mom.z), .w = static_cast<float>(inv_mass * mom.w)};
        maths::Vec4 spin{};
        inv_inertia_.rotate_vector(spin, rotmom);
        rotvel.x = spin.x;
        rotvel.y = spin.y;
        rotvel.z = spin.z;
    }

    for (int n = contact_count_ - 1; n >= 0; --n) {
        const float impulse = core::at(contacts, n).impulse;
        if (impulse > collision_impulse_)
            collision_impulse_ = impulse;
    }

    // Steering against a wall: a push along it, by how much the wall faces the vehicle's front.
    if (steering_ == 0.0f || static_cast<uint32_t>(wheels_in_contact_) < 3 || handbrake_)
        return;
    for (int n = contact_count_ - 1; n >= 0; --n) {
        const Contact& contact = core::at(contacts, n);
        const maths::Vec4 r{.x = static_cast<float>(static_cast<double>(contact.point.x) - at_point_.x),
                            .y = static_cast<float>(static_cast<double>(contact.point.y) - at_point_.y),
                            .z = static_cast<float>(static_cast<double>(contact.point.z) - at_point_.z),
                            .w = static_cast<float>(static_cast<double>(contact.point.w) - at_point_.w)};
        const maths::Vec4& nrm = contact.normal;
        const maths::Vec4& at = core::at(basis_frame_.row, 2);
        auto strength = static_cast<float>(((static_cast<double>(at.x) * nrm.x + static_cast<double>(at.z) * nrm.z) +
                                            static_cast<double>(nrm.y) * at.y) *
                                           contact.impulse * wall_steering_ * steering_);
        const maths::Vec4 push{.x = static_cast<float>(static_cast<double>(strength) * -nrm.z),
                               .y = static_cast<float>(static_cast<double>(strength) * 0.0f),
                               .z = static_cast<float>(static_cast<double>(strength) * nrm.x), .w = strength};
        apply_impulse_at_offset(&push, &r);
    }
}

} // namespace objects
