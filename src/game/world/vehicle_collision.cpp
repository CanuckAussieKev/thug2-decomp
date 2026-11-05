// The Vehicle component's collision and integration: the colliders against the world (contact points, impulses,
// depth correction, friction, steering against walls), moving the body without tunnelling, righting itself, turning
// in the air, and a whole sub-step. See vehicle_component.h. From the disassembly of 0x00436CF0-0x00437549,
// 0x00438770-0x00438A16, 0x00438BB0-0x00439D86, 0x00439D90-0x0043A066 and 0x0043BEE0-0x0043C19A. Float arithmetic
// follows the original's x87 code: double (53 bits) where it stays on the stack, rounded to float where stored.

#include "engine/objects/model_component.h"
#include "game/world/vehicle_component.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "base/core/dbg.h"
#include "engine/gfx/debug_lines.h"
#include "base/maths/rotation_blend.h"
#include "base/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/render/coll_rect.h"
#include "game/world/vehicle_internal.h"
#include "engine/sys/game_clock.h"
#include "engine/sys/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/trigger_events.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"

namespace objects {

using namespace vehicle;

namespace {
constexpr int kSourceLineLessThanUberFrig = 0xB10;  // the source lines the original reports its messages from
constexpr int kSourceLineUberFrig = 0xB26;
constexpr int kPassMaxMs = 5300;
constexpr int kPassMinMs = 300;
constexpr int kDebugWhite = 0xFF;
constexpr int kMaxPasses = 5;
constexpr uint32_t kContactNormalColour = 0xFFFF64FF; // ARGB: a contact normal
constexpr uint32_t kSurfaceNormalColour = 0xFF000000; // ARGB: a surface normal

const float kHalf = 0.5F;                                         // 0x006453BC
const float kMinusHalf = -0.5F;                                   // 0x0064642C
const float kShallowestLevel = std::bit_cast<float>(0x3C23D70AU); // 0x0064544C: 0.01
const float kSameEnd = std::bit_cast<float>(0x3DCCCCCDU);         // 0x00645D74: 0.1
const double kOpposite = -0.99;                                   // 0x00646730 (a double)
const float kBounce = 1.5F;                                       // 0x00646744
const float kDropTest = 4800.0F;                                  // 0x006461D0
const float kDeepestLevel = 2.0F;                                 // 0x00645448
const float kUpright = std::bit_cast<float>(0x3F34FDF4U);         // 0x006467BC: 0.707
const float kMinSlide = std::bit_cast<float>(0x3A83126FU);        // 0x006461D8: 0.001
const float kRightingUp = std::bit_cast<float>(0x3F7FBE77U);      // 0x006467B8: 0.999
const float kRightingSpeed = 90000.0F;                            // 0x006467B4 (300 squared)
const float kRightingSpin = std::bit_cast<float>(0x3F1DE9E7U);    // 0x006467B0: 0.61685
const float kRightingTilt = 0.25F;                                // 0x006467AC
const float kRightingRate = 3.0F;                                 // 0x006467A8
const float kAlignFast = 122500.0F;                               // 0x006467C4 (350 squared)
const float kAlignSlowRate = std::bit_cast<float>(0x3E99999AU);   // 0x006467C0: 0.3

// The model component's 0x00420670 (not rewritten yet): its lighting from what a feeler hit.
void light_from_feeler(ObjectModule* model, render::ProbeRay* feeler, bool b, float brightness)
{
    core::down_cast<MeshInstanceModule>(model)->apply_brightness(feeler, b, brightness);
}

bool get_interrupted(const DrivableModule* vehicle)
{
    return vehicle->cut_short() || ((vehicle->entity()->object_flags() & kObjectFlagDead) != 0U);
}

} // namespace

// 0x00436CF0 (stdcall)
double DrivableModule::get_penetration(const Contact* contact, const Collider* collider)
{
    // The contact point's distance in from each of the face's four edges; the nearest.
    const double dx = static_cast<double>(contact->point.x) - collider->world_corner_.x;
    const double dy = static_cast<double>(contact->point.y) - collider->world_corner_.y;
    const double dz = static_cast<double>(contact->point.z) - collider->world_corner_.z;
    const maths::Vec4& direction_a = core::at(collider->directions, 0);
    const maths::Vec4& direction_b = core::at(collider->directions, 1);
    int side = 0;
    auto nearest = static_cast<float>(((dz * direction_a.z) + (dy * direction_a.y)) + (dx * direction_a.x));
    double distance = static_cast<double>(core::at(collider->lengths, 0)) - nearest;
    if (distance < nearest) {
        nearest = static_cast<float>(distance);
        side = 1;
    }
    distance = ((dz * direction_b.z) + (dy * direction_b.y)) + (dx * direction_b.x);
    if (distance < nearest) {
        nearest = static_cast<float>(distance);
        side = 2;
    }
    distance = core::at(collider->lengths, 1) - distance;
    if (distance < nearest) {
        nearest = static_cast<float>(distance);
        side = 3;
    }
    if (nearest < kShallowestLevel) {
        return 0.0;
    }
    // That distance along the contact's normal.
    const maths::Vec4& normal = contact->normal;
    switch (side) {
    case 0:
        return nearest /
               (((static_cast<double>(direction_a.z) * normal.z) + (static_cast<double>(direction_a.y) * normal.y)) +
                (static_cast<double>(normal.x) * direction_a.x));
    case 1:
        return -(nearest /
                 (((static_cast<double>(direction_a.z) * normal.z) + (static_cast<double>(direction_a.y) * normal.y)) +
                  (static_cast<double>(direction_a.x) * normal.x)));
    case 2:
        return nearest /
               (((static_cast<double>(direction_b.z) * normal.z) + (static_cast<double>(direction_b.y) * normal.y)) +
                (static_cast<double>(direction_b.x) * normal.x));
    default:
        return -(nearest /
                 (((static_cast<double>(direction_b.z) * normal.z) + (static_cast<double>(direction_b.y) * normal.y)) +
                  (static_cast<double>(direction_b.x) * normal.x)));
    }
}

// 0x00436E50
bool DrivableModule::find_edge_contact(Contact* contact, render::BoxProbe* rect, const Collider* collider, int hit,
                                       int end) const
{
    // Another joined hit with an end at this point (within 0.1) facing the opposite way.
    for (int k = rect->joined_count() - 1; k >= 0; --k) {
        if (k == hit) {
            continue;
        }
        const render::RectHit& other = core::at(rect->joined(), k);
        for (int end_index = 1; end_index >= 0; --end_index) {
            const maths::Vec4& point = core::at(other.ends, end_index).point;
            if (!(std::fabs(static_cast<double>(contact->point.x) - point.x) < kSameEnd)) {
                continue;
            }
            if (!(std::fabs(static_cast<double>(contact->point.y) - point.y) < kSameEnd)) {
                continue;
            }
            if (!(std::fabs(static_cast<double>(contact->point.z) - point.z) < kSameEnd)) {
                continue;
            }
            const maths::Vec4& normal = contact->normal;
            if (!(((static_cast<double>(normal.z) * other.normal.z) +
                   (static_cast<double>(normal.y) * other.normal.y)) +
                      (static_cast<double>(normal.x) * other.normal.x) <
                  kOpposite)) {
                continue;
            }
            // On the edge between them: the normal across the hit's triangle edge, and the depth.
            const maths::Vec4& edge = core::at(core::at(rect->joined(), hit).ends, end).edge;
            const double x = (static_cast<double>(edge.y) * normal.z) - (static_cast<double>(edge.z) * normal.y);
            const double y = (static_cast<double>(edge.z) * normal.x) - (static_cast<double>(edge.x) * normal.z);
            contact->normal.w = 0.0F;
            contact->normal.z =
                static_cast<float>((static_cast<double>(edge.x) * normal.y) - (static_cast<double>(edge.y) * normal.x));
            contact->normal.x = static_cast<float>(x);
            contact->normal.y = static_cast<float>(y);
            contact->depth = static_cast<float>(get_penetration(contact, collider));
            if (debug_mode_ != 0) {
                const maths::Vec4 tip{
                    .x = static_cast<float>((static_cast<double>(contact->normal.x) * 72.0F) + contact->point.x),
                    .y = static_cast<float>((static_cast<double>(contact->normal.y) * 72.0F) + contact->point.y),
                    .z = static_cast<float>(static_cast<double>(contact->normal.z * 72.0F) + contact->point.z),
                    .w = static_cast<float>(static_cast<double>(contact->normal.w * 72.0F) + contact->point.w)};
                gfx::push_diag_line(&contact->point, &tip, kContactNormalColour, kContactNormalColour, 1);
            }
            return true;
        }
    }
    return false;
}

// 0x00437060
bool DrivableModule::normal_faces_away(const Contact* contact) const
{
    const maths::Vec4& point = contact->point;
    const maths::Vec4& normal = contact->normal;
    const double along = (((static_cast<double>(point.z) - at_point_.z) * normal.z) +
                          ((static_cast<double>(point.y) - at_point_.y) * normal.y)) +
                         ((static_cast<double>(point.x) - at_point_.x) * normal.x);
    const bool away = along >= 0.0;
    if ((debug_mode_ != 0) && away) {
        const maths::Vec4 tip{
            .x = static_cast<float>((static_cast<double>(normal.x) * 48.0F) + point.x),
            .y = static_cast<float>((static_cast<double>(normal.y) * 48.0F) + point.y),
            .z = static_cast<float>(static_cast<double>(static_cast<float>(normal.z * 48.0F)) + point.z),
            .w = static_cast<float>(static_cast<double>(static_cast<float>(normal.w * 48.0F)) + point.w)};
        gfx::push_diag_line(&point, &tip, kSurfaceNormalColour, kSurfaceNormalColour, 1);
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
    if (feeler.get_hit_shape(false, false) != 0) {
        // Going through something: put just off the surface, the rest of the move reflected, the momentum into it
        // turned back by half again.
        debug::printf(kDebugFormat, kDebugFile, kSourceLineLessThanUberFrig, "less than uber frig"); // "less than uber frig"
        const maths::Vec4& point = feeler.contact_pos();
        const maths::Vec4& normal = feeler.surface_dir();
        at_point_.x = static_cast<float>(static_cast<double>(normal.x) + point.x);
        at_point_.y = static_cast<float>(static_cast<double>(point.y) + normal.y);
        at_point_.z = static_cast<float>(static_cast<double>(point.z) + normal.z);
        at_point_.w = static_cast<float>(static_cast<double>(point.w) + normal.w);
        const double distance = feeler.reach();
        const double vx = distance * move->x;
        const double vy = distance * move->y;
        const double vz = distance * move->z;
        const double vw = distance * move->w;
        double twice = ((static_cast<double>(normal.x) * vx) + (static_cast<double>(normal.y) * vy)) +
                       (static_cast<double>(normal.z) * vz);
        twice = twice + twice;
        auto kw = static_cast<float>(static_cast<double>(normal.w) * twice);
        auto kz = static_cast<float>(static_cast<double>(normal.z) * twice);
        auto ky = static_cast<float>(static_cast<double>(normal.y) * twice);
        const double kx = twice * normal.x;
        auto rx = static_cast<float>(vx - kx);
        auto ry = static_cast<float>(vy - ky);
        const double rz = vz - kz;
        auto rw = static_cast<float>(vw - kw);
        at_point_.x = static_cast<float>(static_cast<double>(rx) + at_point_.x);
        at_point_.y = static_cast<float>(static_cast<double>(ry) + at_point_.y);
        at_point_.z = static_cast<float>(rz + at_point_.z);
        at_point_.w = static_cast<float>(static_cast<double>(rw) + at_point_.w);
        const double into = (((static_cast<double>(normal.z) * linear_momentum_.z) +
                              (static_cast<double>(normal.y) * linear_momentum_.y)) +
                             (static_cast<double>(normal.x) * linear_momentum_.x)) *
                            kBounce;
        linear_momentum_.x = static_cast<float>(linear_momentum_.x - (static_cast<double>(normal.x) * into));
        linear_momentum_.y = static_cast<float>(linear_momentum_.y - (static_cast<double>(normal.y) * into));
        linear_momentum_.z = static_cast<float>(linear_momentum_.z - (static_cast<double>(normal.z) * into));
        linear_momentum_.w = static_cast<float>(linear_momentum_.w - (static_cast<double>(normal.w) * into));
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
    if (feeler.get_hit_shape(false, false) == 0) {
        debug::printf(kDebugFormat, kDebugFile, kSourceLineUberFrig, "uber frig"); // "uber frig"
        at_point_ = saved;
    } else {
        light_from_feeler(mesh_instance_, &feeler, true, 1.0F);
    }
}

// 0x00437450
double DrivableModule::get_impulse_denominator(ImpulseLine impulse_line)
{
    const auto* const offset = impulse_line.offset;
    const auto* const direction = impulse_line.direction;
    // 1 / (n . (n / m + (I^-1 (r x n)) x r)).
    const maths::Vec4& offset_ref = *offset;
    const maths::Vec4& normal = *direction;
    const float inv_mass = mass_.inverse;
    maths::Vec4 cross{};
    cross.x = static_cast<float>((static_cast<double>(normal.z) * offset_ref.y) -
                                 (static_cast<double>(offset_ref.z) * normal.y));
    cross.y = static_cast<float>((static_cast<double>(offset_ref.z) * normal.x) -
                                 (static_cast<double>(offset_ref.x) * normal.z));
    cross.z = static_cast<float>((static_cast<double>(normal.y) * offset_ref.x) -
                                 (static_cast<double>(normal.x) * offset_ref.y));
    cross.w = 0.0F;
    auto linear_x = static_cast<float>(static_cast<double>(normal.x) * inv_mass);
    auto linear_y = static_cast<float>(static_cast<double>(inv_mass) * normal.y);
    auto linear_z = static_cast<float>(static_cast<double>(normal.z) * inv_mass);
    maths::Vec4 w{};
    inv_inertia_.rotate_vector(w, cross);
    const double tx = (static_cast<double>(w.y) * offset_ref.z) - (static_cast<double>(w.z) * offset_ref.y);
    const double ty = (static_cast<double>(w.z) * offset_ref.x) - (static_cast<double>(w.x) * offset_ref.z);
    auto tz = static_cast<float>((static_cast<double>(w.x) * offset_ref.y) - (static_cast<double>(w.y) * offset_ref.x));
    auto ux = static_cast<float>(tx + linear_x);
    const double uy = ty + linear_y;
    const double uz = static_cast<double>(tz) + linear_z;
    return 1.0 / (((uy * normal.y) + (uz * normal.z)) + (static_cast<double>(ux) * normal.x));
}

// 0x00438770
void DrivableModule::update_righting()
{
    quat_to_matrix(&orientation_, &basis_frame_);
    core::at(basis_frame_.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
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
    const bool stuck = ((vx * vx) + (vy * vy)) + (vz * vz) < kRightingSpeed &&
                       ((sx * sx) + (sy * sy)) + (sz * sz) < kRightingSpin && collision_impulse_ > 0.0F &&
                       core::at(basis_frame_.row, 1).y < kRightingTilt &&
                       (wheels_in_contact_ == 0 || core::at(basis_frame_.row, 1).y < 0.0F) && controls_.throttle;
    if (!righting_) {
        if (!stuck) {
            return;
        }
        linear_momentum_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        rotmom_ = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        righting_ = true;
        righting_start_ = frame_clock::game_clock_elapsed() + clock_offset();
    }
    int start = righting_start_;
    if (static_cast<uint32_t>(frame_clock::game_clock_elapsed()) +
            (static_cast<uint32_t>(clock_offset()) - static_cast<uint32_t>(start)) >
        kPassMaxMs) {
        righting_ = false;
        return;
    }
    start = righting_start_;
    if (static_cast<uint32_t>(frame_clock::game_clock_elapsed()) +
            (static_cast<uint32_t>(clock_offset()) - static_cast<uint32_t>(start)) <
        kPassMinMs) {
        return;
    }
    // After 300 ms, turned towards upright facing the same way.
    maths::Vec4 forward{.x = core::at(basis_frame_.row, 2).x,
                        .y = 0.0F,
                        .z = core::at(basis_frame_.row, 2).z,
                        .w = core::at(basis_frame_.row, 2).w};
    forward.make_unit();
    maths::Mat4 target;
    core::at(target.row, 0) = {.x = forward.z, .y = 0.0F, .z = -forward.x, .w = 1.0F};
    core::at(target.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
    core::at(target.row, 2) = forward;
    core::at(target.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}; // (the original leaves its stack there)
    maths::ArcRotationBlender blend{}; // (on the stack without a constructor in the original: zeroed here)
    maths::ArcRotationBlender* slerp = &blend;
    slerp->set_matrices(basis_frame_, target);
    const double fraction = frame_clock::get_slomo_frame_seconds() / slerp->angle() * kRightingRate;
    slerp->fetch_mat(basis_frame_, fraction > 1.0 ? 1.0F : static_cast<float>(fraction));
    maths::Vec4 quat{};
    matrix_to_quat(&quat, &basis_frame_);
    orientation_ = quat;
}

// 0x00439D90
void DrivableModule::align_in_air()
{
    if (righting_) {
        return;
    }
    if (kick_time_ > 0.0F) {
        return;
    }
    double vx = vel_.x;
    double vz = vel_.z;
    const double align = air_align_.speed;
    const bool slow = align * align > (vx * vx) + (vz * vz);
    if (slow && !air_align_.when_slow) {
        return;
    }
    quat_to_matrix(&orientation_, &basis_frame_);
    core::at(basis_frame_.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    // Slow: towards its facing (level); fast: towards where it's going.
    maths::Vec4 at{};
    if (slow) {
        at = {.x = core::at(basis_frame_.row, 2).x,
              .y = 0.0F,
              .z = core::at(basis_frame_.row, 2).z,
              .w = core::at(basis_frame_.row, 2).w};
    } else {
        at = vel_;
    }
    at.make_unit();
    if (((static_cast<double>(at.z) * core::at(basis_frame_.row, 2).z) +
         (static_cast<double>(at.y) * core::at(basis_frame_.row, 2).y)) +
            (static_cast<double>(at.x) * core::at(basis_frame_.row, 2).x) <
        0.0) {
        at.x = at.x * -1.0F;
        at.y = at.y * -1.0F;
        at.z = at.z * -1.0F;
        at.w = at.w * -1.0F;
    }
    const maths::Vec4 up{.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
    maths::Mat4 target;
    core::at(target.row, 0) = maths::cross_vec(up, at);
    core::at(target.row, 1) = maths::cross_vec(at, core::at(target.row, 0));
    core::at(target.row, 2) = at;
    core::at(target.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    maths::ArcRotationBlender blend{}; // (on the stack without a constructor in the original: zeroed here)
    maths::ArcRotationBlender* slerp = &blend;
    slerp->set_matrices(basis_frame_, target);
    // The turn ramps in over alignTime of free fall; slower under 350.
    vx = vel_.x;
    vz = vel_.z;
    const bool fast = (vx * vx) + (vz * vz) > kAlignFast;
    const float time = air_align_.time;
    const float falling = free_fall_time_;
    const double ramp = falling < time ? falling : time;
    const double rate = fast ? static_cast<double>(air_align_.rate) * step_length_
                             : static_cast<double>(air_align_.rate) * step_length_ * kAlignSlowRate;
    slerp->fetch_mat(basis_frame_, static_cast<float>(ramp / time * rate));
    maths::Vec4 quat{};
    matrix_to_quat(&quat, &basis_frame_);
    orientation_ = quat;
}

// 0x0043BEE0
void DrivableModule::integrate()
{
    // The accelerations.
    const double inv_mass = mass_.inverse;
    auto accel_x = static_cast<float>(inv_mass * force_.x);
    auto accel_y = static_cast<float>(inv_mass * force_.y);
    auto accel_z = static_cast<float>(inv_mass * force_.z);
    auto accel_w = static_cast<float>(inv_mass * force_.w);
    maths::Vec4 spin_accel{};
    inv_inertia_.rotate_vector(spin_accel, twist_force_);
    // The momentum.
    double dt = step_length_;
    auto dy = static_cast<float>(dt * force_.y);
    auto dz = static_cast<float>(dt * force_.z);
    auto dw = static_cast<float>(dt * force_.w);
    linear_momentum_.x = static_cast<float>((dt * force_.x) + linear_momentum_.x);
    linear_momentum_.y = static_cast<float>(static_cast<double>(dy) + linear_momentum_.y);
    linear_momentum_.z = static_cast<float>(static_cast<double>(dz) + linear_momentum_.z);
    linear_momentum_.w = static_cast<float>(static_cast<double>(dw) + linear_momentum_.w);
    // The move: (velocity + half the acceleration over the step) over the step.
    const float timestep = step_length_;
    double half = static_cast<double>(step_length_) * kHalf;
    auto hy = static_cast<float>(accel_y * half);
    auto hz = static_cast<float>(accel_z * half);
    auto hw = static_cast<float>(accel_w * half);
    const double vx = (accel_x * half) + vel_.x;
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
    const double wx = (spin_accel.x * half) + rotvel_.x;
    auto wy = static_cast<float>(static_cast<double>(static_cast<float>(spin_accel.y * half)) + rotvel_.y);
    auto wz = static_cast<float>(static_cast<double>(static_cast<float>(spin_accel.z * half)) + rotvel_.z);
    auto scale = static_cast<float>(static_cast<double>(step_length_) * kMinusHalf);
    const maths::Vec4 turn{.x = static_cast<float>(wx * scale),
                           .y = static_cast<float>(static_cast<double>(wy) * scale),
                           .z = static_cast<float>(static_cast<double>(wz) * scale),
                           .w = 0.0F};
    quat_multiply(&quat, &turn);
    orientation_.x = static_cast<float>(static_cast<double>(quat.x) + orientation_.x);
    orientation_.y = static_cast<float>(static_cast<double>(quat.y) + orientation_.y);
    orientation_.z = static_cast<float>(static_cast<double>(quat.z) + orientation_.z);
    orientation_.w = static_cast<float>(static_cast<double>(quat.w) + orientation_.w);
    quat_normalize(&orientation_, 1.0F);
    // The angular momentum.
    dt = step_length_;
    dy = static_cast<float>(dt * twist_force_.y);
    dz = static_cast<float>(dt * twist_force_.z);
    dw = static_cast<float>(dt * twist_force_.w);
    rotmom_.x = static_cast<float>((dt * twist_force_.x) + rotmom_.x);
    rotmom_.y = static_cast<float>(static_cast<double>(dy) + rotmom_.y);
    rotmom_.z = static_cast<float>(static_cast<double>(dz) + rotmom_.z);
    rotmom_.w = static_cast<float>(static_cast<double>(dw) + rotmom_.w);
    update_wheel_rotations();
    if (wheels_in_contact_ == 0 && collision_impulse_ == 0.0F) {
        align_in_air();
    }
    update_righting();
    update_dependents();
}

// (Part of 0x00438BB0) A collider's face against the world: where it crosses the world's faces, and lines from
// the body's position to its four corners - as contacts. False when a trigger interrupted the collision.
bool DrivableModule::gather_contacts(const Collider& collider, render::BoxProbe& rect, render::ProbeRay& feeler)
{
    const std::span<Contact> contacts = get_contacts();
    rect.rect().origin = collider.world_corner_;
    rect.rect().side_a_ = core::at(collider.world_edges_, 0);
    rect.rect().side_b_ = core::at(collider.world_edges_, 1);
    if (rect.get_query(0)) {
        trigger_rect(trigger_ptr_, kCrossingEvents, &rect);
        if (get_interrupted(this)) {
            return false;
        }
        rect.join_hits();
        for (int k = rect.joined_count() - 1; k >= 0; --k) {
            const render::RectHit& hit = core::at(rect.joined(), k);
            for (int end_index = 1; end_index >= 0; --end_index) {
                Contact& contact = core::at(contacts, contact_count_);
                contact.point = core::at(hit.ends, end_index).point;
                contact.normal = hit.normal;
                contact.from_feeler_ = false;
                const bool on_edge = core::at(hit.ends, end_index).on_triangle_edge_;
                if (!normal_faces_away(&contact)) {
                    contact.depth = on_edge ? static_cast<float>(get_penetration(&contact, &collider)) : 0.0F;
                    ++contact_count_;
                } else if (on_edge && find_edge_contact(&contact, &rect, &collider, k, end_index)) {
                    ++contact_count_;
                }
            }
        }
    }
    if (debug_mode_ != 0) {
        rect.debug_draw(kDebugWhite, kDebugWhite, kDebugWhite, 1);
    }
    feeler.line().start = at_point_;
    feeler.line().end = rect.rect().origin;
    for (int corner = 0; corner < 4; ++corner) {
        if (corner == 1) {
            feeler.line().end = {
                .x = static_cast<float>(static_cast<double>(feeler.line().end.x) + rect.rect().side_a_.x),
                .y = static_cast<float>(static_cast<double>(rect.rect().side_a_.y) + feeler.line().end.y),
                .z = static_cast<float>(static_cast<double>(rect.rect().side_a_.z) + feeler.line().end.z),
                .w = static_cast<float>(static_cast<double>(rect.rect().side_a_.w) + feeler.line().end.w)};
        } else if (corner == 2) {
            feeler.line().end = {
                .x = static_cast<float>(static_cast<double>(feeler.line().end.x) + rect.rect().side_b_.x),
                .y = static_cast<float>(static_cast<double>(rect.rect().side_b_.y) + feeler.line().end.y),
                .z = static_cast<float>(static_cast<double>(rect.rect().side_b_.z) + feeler.line().end.z),
                .w = static_cast<float>(static_cast<double>(rect.rect().side_b_.w) + feeler.line().end.w)};
        } else if (corner == 3) {
            feeler.line().end = {
                .x = static_cast<float>(static_cast<double>(feeler.line().end.x) - rect.rect().side_a_.x),
                .y = static_cast<float>(static_cast<double>(feeler.line().end.y) - rect.rect().side_a_.y),
                .z = static_cast<float>(static_cast<double>(feeler.line().end.z) - rect.rect().side_a_.z),
                .w = static_cast<float>(static_cast<double>(feeler.line().end.w) - rect.rect().side_a_.w)};
        }
        if (feeler.get_hit_shape(false, false) != 0) {
            trigger_feeler(trigger_ptr_, kCrossingEvents, &feeler);
            if (get_interrupted(this)) {
                return false;
            }
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
                    (((dz * feeler.surface_dir().z) + (dy * feeler.surface_dir().y)) + (dx * feeler.surface_dir().x)));
                ++contact_count_;
                // (the original marks the next record, not this one - the one past the table when it is full)
                if (static_cast<std::size_t>(contact_count_) < contacts.size()) {
                    core::at(contacts, contact_count_).from_feeler_ = true;
                }
            }
        }
        if (debug_mode_ != 0) {
            feeler.debug_draw(kDebugWhite, kDebugWhite, kDebugWhite, 1);
        }
    }
    return true;
}

// 0x00438BB0
void DrivableModule::collide()
{
    collision_impulse_ = 0.0F;
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
        if (!gather_contacts(collider, rect, feeler)) {
            return;
        }
    }
    if (contact_count_ == 0) {
        return;
    }
    for (int contact_index = contact_count_ - 1; contact_index >= 0; --contact_index) {
        core::at(contacts, contact_index).impulse = 0.0F;
    }
    maths::Vec4 mom = linear_momentum_;
    maths::Vec4 rotmom = rotmom_;
    maths::Vec4 vel = vel_;
    maths::Vec4 rotvel = rotvel_;

    // Impulses at the contacts closing in (with restitution), up to six passes until none is.
    for (int pass = 0;;) {
        bool separating = true;
        for (int contact_index = contact_count_ - 1; contact_index >= 0; --contact_index) {
            Contact& contact = core::at(contacts, contact_index);
            const maths::Vec4 arm{.x = static_cast<float>(static_cast<double>(contact.point.x) - at_point_.x),
                                  .y = static_cast<float>(static_cast<double>(contact.point.y) - at_point_.y),
                                  .z = static_cast<float>(static_cast<double>(contact.point.z) - at_point_.z),
                                  .w = static_cast<float>(static_cast<double>(contact.point.w) - at_point_.w)};
            maths::Vec4 v{};
            get_point_velocity(&v, &arm);
            const maths::Vec4& nrm = contact.normal;
            const double closing_exact = ((static_cast<double>(v.z) * nrm.z) + (static_cast<double>(v.x) * nrm.x)) +
                                         (static_cast<double>(v.y) * nrm.y);
            auto closing = static_cast<float>(closing_exact);
            if (closing_exact > 0.0) {
                continue;
            }
            separating = false;
            auto bounce = static_cast<float>(-(static_cast<double>(closing) * response_.restitution));
            const double denominator = get_impulse_denominator({.offset = &arm, .direction = &nrm});
            auto impulse = static_cast<float>((static_cast<double>(bounce) - closing) * denominator);
            contact.impulse = static_cast<float>(static_cast<double>(impulse) + contact.impulse);
            const maths::Vec4 push{.x = static_cast<float>(static_cast<double>(impulse) * nrm.x),
                                   .y = static_cast<float>(static_cast<double>(impulse) * nrm.y),
                                   .z = static_cast<float>(static_cast<double>(impulse) * nrm.z),
                                   .w = static_cast<float>(static_cast<double>(impulse) * nrm.w)};
            apply_impulse_at_offset(&push, &arm);
        }
        ++pass;
        if (pass > kMaxPasses || separating) {
            break;
        }
    }

    // Pushed out by how deep each contact is (at most 2).
    for (int contact_index = contact_count_ - 1; contact_index >= 0; --contact_index) {
        Contact& contact = core::at(contacts, contact_index);
        if (contact.depth < 0.0F || contact.depth == 0.0F) {
            continue;
        }
        contact.depth = std::min(contact.depth, kDeepestLevel);
        const double push = static_cast<double>(response_.depth_correction) * contact.depth;
        auto push_rounded = static_cast<float>(push);
        const maths::Vec4& nrm = contact.normal;
        const maths::Vec4 force{.x = static_cast<float>(static_cast<double>(push_rounded) * nrm.x),
                                .y = static_cast<float>(static_cast<double>(push_rounded) * nrm.y),
                                .z = static_cast<float>(static_cast<double>(push_rounded) * nrm.z),
                                .w = static_cast<float>(push * nrm.w)};
        const maths::Vec4 arm{.x = static_cast<float>(static_cast<double>(contact.point.x) - at_point_.x),
                              .y = static_cast<float>(static_cast<double>(contact.point.y) - at_point_.y),
                              .z = static_cast<float>(static_cast<double>(contact.point.z) - at_point_.z),
                              .w = static_cast<float>(static_cast<double>(contact.point.w) - at_point_.w)};
        add_force_at_offset(&force, &arm);
    }

    // Friction against the sliding at each contact that took an impulse (on the velocities as they were, updated
    // by each friction impulse in turn); at most the friction coefficient times that impulse.
    const float friction = core::at(basis_frame_.row, 1).y > kUpright ? response_.friction : response_.tipped_friction;
    for (int contact_index = contact_count_ - 1; contact_index >= 0; --contact_index) {
        const Contact& contact = core::at(contacts, contact_index);
        if (contact.impulse == 0.0F) {
            continue;
        }
        const maths::Vec4& nrm = contact.normal;
        auto rz = static_cast<float>(static_cast<double>(contact.point.z) - at_point_.z);
        auto ry = static_cast<float>(static_cast<double>(contact.point.y) - at_point_.y);
        const maths::Vec4 arm{.x = static_cast<float>(static_cast<double>(contact.point.x) - at_point_.x),
                              .y = ry,
                              .z = rz,
                              .w = static_cast<float>(static_cast<double>(contact.point.w) - at_point_.w)};
        const double cz = (static_cast<double>(ry) * rotvel.x) - (static_cast<double>(arm.x) * rotvel.y);
        const double cy = (static_cast<double>(arm.x) * rotvel.z) - (static_cast<double>(rz) * rotvel.x);
        auto cx = static_cast<float>((static_cast<double>(rz) * rotvel.y) - (static_cast<double>(ry) * rotvel.z));
        auto pvz = static_cast<float>(cz + vel.z);
        const double pvy = cy + vel.y;
        const double pvx = static_cast<double>(cx) + vel.x;
        const double along = ((static_cast<double>(pvz) * nrm.z) + (pvx * nrm.x)) + (pvy * nrm.y);
        auto tz = static_cast<float>(along * nrm.z);
        auto ty = static_cast<float>(along * nrm.y);
        auto tx = static_cast<float>(along * nrm.x);
        auto slide_w = static_cast<float>(vel.w - (along * nrm.w));
        auto slide_z = static_cast<float>(static_cast<double>(pvz) - tz);
        auto slide_y = static_cast<float>(pvy - ty);
        const double slide_x = pvx - tx;
        const double speed = std::sqrt(((slide_x * slide_x) + (static_cast<double>(slide_y) * slide_y)) +
                                       (static_cast<double>(slide_z) * slide_z));
        auto speed_rounded = static_cast<float>(speed);
        if (speed < kMinSlide) {
            continue;
        }
        const double inverse = 1.0 / speed_rounded;
        const maths::Vec4 slide{.x = static_cast<float>(slide_x * inverse), .y = static_cast<float>(slide_y * inverse),
                                .z = static_cast<float>(slide_z * inverse), .w = static_cast<float>(slide_w * inverse)};
        double stop = get_impulse_denominator({.offset = &arm, .direction = &slide}) * speed_rounded;
        auto most = static_cast<float>(static_cast<double>(friction) * contact.impulse);
        stop = std::min<double>(stop, most);
        const double against = -stop;
        const maths::Vec4 push{.x = static_cast<float>(static_cast<double>(slide.x) * against),
                               .y = static_cast<float>(static_cast<double>(slide.y) * against),
                               .z = static_cast<float>(static_cast<double>(slide.z) * against),
                               .w = static_cast<float>(static_cast<double>(slide.w) * against)};
        apply_impulse_at_offset(&push, &arm);
        // The copies of the momenta and velocities followed along.
        mom.x = static_cast<float>(static_cast<double>(push.x) + mom.x);
        mom.y = static_cast<float>(static_cast<double>(push.y) + mom.y);
        mom.z = static_cast<float>(static_cast<double>(push.z) + mom.z);
        mom.w = static_cast<float>(static_cast<double>(push.w) + mom.w);
        const double az = (static_cast<double>(push.y) * arm.x) - (static_cast<double>(push.x) * ry);
        const double ay = (static_cast<double>(push.x) * rz) - (static_cast<double>(push.z) * arm.x);
        const double ax = (static_cast<double>(push.z) * ry) - (static_cast<double>(push.y) * rz);
        rotmom.x = static_cast<float>(rotmom.x + ax);
        rotmom.y = static_cast<float>(ay + rotmom.y);
        rotmom.z = static_cast<float>(az + rotmom.z);
        const double inv_mass = mass_.inverse;
        vel = {.x = static_cast<float>(inv_mass * mom.x), .y = static_cast<float>(inv_mass * mom.y),
               .z = static_cast<float>(inv_mass * mom.z), .w = static_cast<float>(inv_mass * mom.w)};
        maths::Vec4 spin{};
        inv_inertia_.rotate_vector(spin, rotmom);
        rotvel.x = spin.x;
        rotvel.y = spin.y;
        rotvel.z = spin.z;
    }

    for (int contact_index = contact_count_ - 1; contact_index >= 0; --contact_index) {
        const float impulse = core::at(contacts, contact_index).impulse;
        collision_impulse_ = std::max(collision_impulse_, impulse);
    }

    // Steering against a wall: a push along it, by how much the wall faces the vehicle's front.
    if (controls_.steering == 0.0F || static_cast<uint32_t>(wheels_in_contact_) < 3 || controls_.handbrake) {
        return;
    }
    for (int contact_index = contact_count_ - 1; contact_index >= 0; --contact_index) {
        const Contact& contact = core::at(contacts, contact_index);
        const maths::Vec4 arm{.x = static_cast<float>(static_cast<double>(contact.point.x) - at_point_.x),
                              .y = static_cast<float>(static_cast<double>(contact.point.y) - at_point_.y),
                              .z = static_cast<float>(static_cast<double>(contact.point.z) - at_point_.z),
                              .w = static_cast<float>(static_cast<double>(contact.point.w) - at_point_.w)};
        const maths::Vec4& nrm = contact.normal;
        const maths::Vec4& at = core::at(basis_frame_.row, 2);
        auto strength =
            static_cast<float>((((static_cast<double>(at.x) * nrm.x) + (static_cast<double>(at.z) * nrm.z)) +
                                (static_cast<double>(nrm.y) * at.y)) *
                               contact.impulse * response_.wall_steering * controls_.steering);
        const maths::Vec4 push{.x = static_cast<float>(static_cast<double>(strength) * -nrm.z),
                               .y = static_cast<float>(static_cast<double>(strength) * 0.0F),
                               .z = static_cast<float>(static_cast<double>(strength) * nrm.x),
                               .w = strength};
        apply_impulse_at_offset(&push, &arm);
    }
}

} // namespace objects
