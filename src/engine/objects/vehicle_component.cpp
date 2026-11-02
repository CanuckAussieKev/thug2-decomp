// The Vehicle component: its life (creation, parameters, the update, the model's bones), its small methods and
// script member functions. See vehicle_component.h (physics: vehicle_physics.cpp; collision: vehicle_collision.cpp). From the disassembly of 0x004351C0-0x0043C536 (the member
// functions 0x004353F0, 0x00435480, 0x00435490, 0x004354B0 and 0x0043B670, the helper 0x0043AA90 and the wheel's
// destructor 0x00437CF0 aren't Ghidra functions). Float arithmetic follows the original's x87 code: double (53 bits)
// where it stays on the stack, rounded to float where it is stored.

#include "engine/objects/vehicle_component.h"
#include "engine/gfx/model.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/viewport.h"
#include "game/skate/skate_module.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <gsl/pointers>
#include <array>
#include <span>
#include <vector>

#include "engine/core/dbg.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/debug_lines.h"
#include "engine/maths/mat4.h"
#include "engine/maths/random.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/proximity.h"
#include "engine/objects/vehicle_internal.h"
#include "engine/objects/vehicle_wheel.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "game/park_editing/park_editor.h"
#include "engine/gfx/engine.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/script/cscript.h"
#include "engine/objects/model_component.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/input_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/scene_load.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"

namespace objects {

namespace {
// The contacts of the colliders with the world (one array for every vehicle), up to the lens flare's globals at
// 0x00696878: 132 of them.
std::array<DrivableModule::Contact, 132>& vehicle_contacts()
{
    static core::NoDestroy<std::array<DrivableModule::Contact, 132>> vehicle_contacts;
    return vehicle_contacts.get();
}
} // namespace
std::span<DrivableModule::Contact> DrivableModule::get_contacts() { return vehicle_contacts(); }

using namespace vehicle;

namespace {

constexpr uint32_t kPercent = 0x9E497FC6;
constexpr uint32_t kDuration = 0x79A07F3F;
constexpr uint32_t kOffset = 0xA6F5352F;

const float kHalf = 0.5f;                // 0x006453BC
const float kHundredPi = 314.159271f;    // 0x006466A8 (the wheels' roll wraps at 200 pi by 100 pi)
const float kTwoHundredPi = 628.318542f; // 0x006466AC
const float kHundredth = 0.00999999978f; // 0x0064544C

} // namespace

// 0x004351C0
void DrivableModule::update_wheel_rotations()
{
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        const double rotation =
            (static_cast<double>(wheel.spin) - static_cast<double>(wheel.torque) * step_length_ * kHalf) *
                step_length_ +
            wheel.rotation;
        wheel.rotation = static_cast<float>(rotation);
        if (rotation > kTwoHundredPi)
            wheel.rotation = static_cast<float>(rotation - kHundredPi);
        else if (rotation < 0.0)
            wheel.rotation = static_cast<float>(rotation + kHundredPi);
    }
}

// 0x00435250
double DrivableModule::get_tyre_friction(Wheel* wheel, float slip)
{
    if (handbrake_) {
        double friction = 0.0;
        if (throttle_) {
            friction = wheel->parking_grip_powered_;
            wheel->state = kWheelHandbrakeThrottle;
        } else {
            friction = wheel->parking_grip_locked_;
            wheel->state = kWheelHandbrakeLocked;
        }
        if (slip < wheel->min_static_grip_vel_) {
            wheel->state = kWheelStaticSlow;
            return friction / wheel->min_static_grip_vel_ * slip;
        }
        return friction;
    }
    const double scale = brake_ ? 2.0 : 1.0; // (0x00645448 / 0x006453A8)
    if (slip < wheel->min_static_grip_vel_) {
        wheel->state = kWheelStaticSlow;
        return static_cast<double>(wheel->static_friction_) / wheel->min_static_grip_vel_ * slip;
    }
    if (slip < wheel->max_static_grip_vel_) {
        wheel->state = kWheelStatic;
        return scale * wheel->static_friction_;
    }
    if (slip < wheel->min_dynamic_grip_vel_) {
        wheel->state = kWheelStaticToDynamic;
        const double blend = (static_cast<double>(wheel->static_friction_) - wheel->dynamic_friction_) *
                             (static_cast<double>(slip) - wheel->max_static_grip_vel_) /
                             (static_cast<double>(wheel->min_dynamic_grip_vel_) - wheel->max_static_grip_vel_);
        return scale * wheel->dynamic_friction_ + blend;
    }
    wheel->state = kWheelDynamic;
    return scale * wheel->dynamic_friction_;
}

// 0x00435380
int DrivableModule::get_gear(float spin)
{
    const float upshift = upshift_speed_;
    int gear = 0;
    if (!(static_cast<double>(core::at(transmission_ratios_, 0)) * final_drive_ratio_ * spin > upshift))
        return gear;
    while (gear < gear_count_ - 1) {
        const double engine = static_cast<double>(core::at(transmission_ratios_, gear + 1)) * final_drive_ratio_ * spin;
        ++gear;
        if (!(engine > upshift))
            break;
    }
    return gear;
}

// 0x00435520
void DrivableModule::step_vehicle_speeds()
{
    const double inv_mass = inv_mass_;
    vel_.z = static_cast<float>(inv_mass * linear_momentum_.z);
    vel_.w = static_cast<float>(inv_mass * linear_momentum_.w);
    vel_.x = static_cast<float>(inv_mass * linear_momentum_.x);
    vel_.y = static_cast<float>(inv_mass * linear_momentum_.y);
    vel_.w = 0.0f;
    maths::Vec4 rotvel;
    rotvel_ = inv_inertia_.rotate_vector(rotvel, rotmom_);
}

// 0x004355B0
maths::Vec4* DrivableModule::get_point_velocity(maths::Vec4* out, const maths::Vec4* offset)
{
    // The angular velocity across the offset, plus the velocity.
    auto x =
        static_cast<float>(static_cast<double>(offset->z) * rotvel_.y - static_cast<double>(offset->y) * rotvel_.z);
    auto y =
        static_cast<float>(static_cast<double>(rotvel_.z) * offset->x - static_cast<double>(rotvel_.x) * offset->z);
    auto z =
        static_cast<float>(static_cast<double>(rotvel_.x) * offset->y - static_cast<double>(rotvel_.y) * offset->x);
    const float w = vel_.w;
    out->x = static_cast<float>(static_cast<double>(x) + vel_.x);
    out->y = static_cast<float>(static_cast<double>(y) + vel_.y);
    out->z = static_cast<float>(static_cast<double>(z) + vel_.z);
    out->w = w;
    return out;
}

// 0x00435640
void DrivableModule::add_force_at_point(const maths::Vec4* force, const maths::Vec4* point, uint32_t)
{
    force_.x = static_cast<float>(static_cast<double>(force_.x) + force->x);
    force_.y = static_cast<float>(static_cast<double>(force->y) + force_.y);
    force_.z = static_cast<float>(static_cast<double>(force->z) + force_.z);
    force_.w = static_cast<float>(static_cast<double>(force->w) + force_.w);
    // The lever from the centre of mass; its torque's x and y rounded, z not.
    const double rx = static_cast<double>(point->x) - com_offset_.x;
    const double ry = static_cast<double>(point->y) - com_offset_.y;
    const double rz = static_cast<double>(point->z) - com_offset_.z;
    auto tx = static_cast<float>(ry * force->z - rz * force->y);
    auto ty = static_cast<float>(rz * force->x - rx * force->z);
    const double tz = rx * force->y - ry * force->x;
    twist_force_.x = static_cast<float>(static_cast<double>(tx) + twist_force_.x);
    twist_force_.y = static_cast<float>(static_cast<double>(ty) + twist_force_.y);
    twist_force_.z = static_cast<float>(tz + twist_force_.z);
    // (the colour was for a debug line, compiled out)
}

// 0x00435710
void DrivableModule::add_force_at_offset(const maths::Vec4* force, const maths::Vec4* offset)
{
    force_.x = static_cast<float>(static_cast<double>(force_.x) + force->x);
    force_.y = static_cast<float>(static_cast<double>(force->y) + force_.y);
    force_.z = static_cast<float>(static_cast<double>(force->z) + force_.z);
    force_.w = static_cast<float>(static_cast<double>(force->w) + force_.w);
    // The torque: x and y unrounded, z rounded.
    const double tx = static_cast<double>(offset->y) * force->z - static_cast<double>(force->y) * offset->z;
    const double ty = static_cast<double>(offset->z) * force->x - static_cast<double>(force->z) * offset->x;
    auto tz = static_cast<float>(static_cast<double>(force->y) * offset->x - static_cast<double>(offset->y) * force->x);
    twist_force_.x = static_cast<float>(tx + twist_force_.x);
    twist_force_.y = static_cast<float>(ty + twist_force_.y);
    twist_force_.z = static_cast<float>(static_cast<double>(tz) + twist_force_.z);
}

// 0x004357C0
void DrivableModule::apply_impulse_at_offset(const maths::Vec4* impulse, const maths::Vec4* offset)
{
    linear_momentum_.x = static_cast<float>(static_cast<double>(linear_momentum_.x) + impulse->x);
    linear_momentum_.y = static_cast<float>(static_cast<double>(impulse->y) + linear_momentum_.y);
    linear_momentum_.z = static_cast<float>(static_cast<double>(impulse->z) + linear_momentum_.z);
    linear_momentum_.w = static_cast<float>(static_cast<double>(impulse->w) + linear_momentum_.w);
    const double tx = static_cast<double>(offset->y) * impulse->z - static_cast<double>(impulse->y) * offset->z;
    const double ty = static_cast<double>(offset->z) * impulse->x - static_cast<double>(impulse->z) * offset->x;
    auto tz =
        static_cast<float>(static_cast<double>(impulse->y) * offset->x - static_cast<double>(offset->y) * impulse->x);
    rotmom_.x = static_cast<float>(tx + rotmom_.x);
    rotmom_.y = static_cast<float>(ty + rotmom_.y);
    rotmom_.z = static_cast<float>(static_cast<double>(tz) + rotmom_.z);
    step_vehicle_speeds();
}

// 0x00435850
bool DrivableModule::is_kicked() { return kick_time_ > 0.0f; }

// 0x00435890
void DrivableModule::kick_randomly_along(const maths::Vec4* along)
{
    // The direction flattened (y 0) and made unit (w kept), and its sideways (-z, 0, x).
    float dx = along->x, dy = 0.0f, dz = along->z, dw = along->w;
    const double length = std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dx) * dx);
    if (length > 0.0) {
        const double scale = 1.0 / length;
        dx = static_cast<float>(dx * scale);
        dy = static_cast<float>(0.0 * scale);
        dz = static_cast<float>(dz * scale);
    }
    const float sideways = -dz;

    // The push: along the direction (0x3DB3FA83 * (1 + +-0.5)), sideways (0x39E99FEC * +-1.5), up
    // (0x738500FD * (1 + +-0.2)).
    auto forward_scale = static_cast<float>(maths::signed_spread(0.5f) + 1.0);
    auto forward = static_cast<float>(script::fetch_float(0x3DB3FA83) * static_cast<double>(forward_scale));
    const float side_scale = script::fetch_float(0x39E99FEC);
    const double side = static_cast<double>(maths::signed_spread(1.5f)) * side_scale;
    const double side_x = sideways * side;
    auto side_y = static_cast<float>(side * 0.0);
    auto side_z = static_cast<float>(dx * side);
    auto side_w = static_cast<float>(side);
    auto push_x = static_cast<float>(static_cast<double>(dx) * forward + side_x);
    auto push_y =
        static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dy) * forward)) + side_y);
    auto push_z =
        static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dz) * forward)) + side_z);
    auto push_w =
        static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(dw) * forward)) + side_w);
    auto up_scale = static_cast<float>(maths::signed_spread(0.2f) + 1.0);
    push_y = static_cast<float>(static_cast<double>(push_y) +
                                script::fetch_float(0x738500FD) * static_cast<double>(up_scale));

    // The spin: about the up axis (0x14DDB3EF * +-1.5) and about the sideways one (0x4088E6EC * (1 + +-0.8)).
    const float yaw_scale = script::fetch_float(0x14DDB3EF);
    auto yaw = static_cast<float>(static_cast<double>(maths::signed_spread(1.5f)) * yaw_scale);
    auto roll_scale = static_cast<float>(maths::signed_spread(0.8f) + 1.0);
    const double roll = static_cast<double>(script::fetch_float(0x4088E6EC)) * roll_scale;
    const double spin_x = sideways * roll;
    auto spin_y = static_cast<float>(roll * 0.0 + yaw);
    auto spin_z = static_cast<float>(dx * roll);
    auto spin_w = static_cast<float>(roll);

    // Into the momenta: the push times the mass, the spin times the first moment of inertia (for every axis).
    const double mass = 1.0 / inv_mass_;
    auto mom_y = static_cast<float>(push_y * mass), mom_z = static_cast<float>(push_z * mass);
    auto mom_w = static_cast<float>(push_w * mass);
    linear_momentum_.x = static_cast<float>(push_x * mass + linear_momentum_.x);
    linear_momentum_.y = static_cast<float>(static_cast<double>(mom_y) + linear_momentum_.y);
    linear_momentum_.z = static_cast<float>(static_cast<double>(mom_z) + linear_momentum_.z);
    linear_momentum_.w = static_cast<float>(static_cast<double>(mom_w) + linear_momentum_.w);
    auto moment = static_cast<float>(1.0 / core::at(inv_moments_, 0));
    auto turn_z = static_cast<float>(static_cast<double>(spin_z) * moment);
    auto rot_w = static_cast<float>(static_cast<double>(spin_w) * moment);
    rotmom_.x = static_cast<float>(spin_x * moment + rotmom_.x);
    rotmom_.y = static_cast<float>(static_cast<double>(spin_y) * moment + rotmom_.y);
    rotmom_.z = static_cast<float>(static_cast<double>(turn_z) + rotmom_.z);
    rotmom_.w = static_cast<float>(static_cast<double>(rot_w) + rotmom_.w);
    step_vehicle_speeds();
    kick_time_ = script::fetch_float(0x771922A6);
    awake_ = 1;
    still_frames_ = 0;
}

// 0x004354C0 (cdecl)
maths::Vec4* quat_product(maths::Vec4* out, const maths::Vec4* a, const maths::Vec4* b)
{
    maths::Vec4 product = *a;
    quat_multiply(&product, b);
    *out = product;
    return out;
}

// --- script member functions ---

// 0x00437A60
bool DrivableModule::get_kick(script::Struct* params, script::Script*)
{
    awake_ = 1;
    still_frames_ = 0;
    // (Without linear or angular the original uses whatever was on its stack.)
    maths::Vec4 linear, angular, turned;
    params->fetch_vec("linear", &linear, 0);
    params->fetch_vec("angular", &angular, 0);
    linear = basis_frame_.rotate_vector(turned, linear);
    angular = basis_frame_.rotate_vector(turned, angular);
    const double mass = 1.0 / inv_mass_;
    auto mom_y = static_cast<float>(linear.y * mass), mom_z = static_cast<float>(linear.z * mass);
    auto mom_w = static_cast<float>(linear.w * mass);
    linear_momentum_.x = static_cast<float>(linear.x * mass + linear_momentum_.x);
    linear_momentum_.y = static_cast<float>(static_cast<double>(mom_y) + linear_momentum_.y);
    linear_momentum_.z = static_cast<float>(static_cast<double>(mom_z) + linear_momentum_.z);
    linear_momentum_.w = static_cast<float>(static_cast<double>(mom_w) + linear_momentum_.w);
    const double moment = 1.0 / core::at(inv_moments_, 0);
    auto spin_about_y = static_cast<float>(angular.y * moment), turn_z = static_cast<float>(angular.z * moment);
    auto rot_w = static_cast<float>(angular.w * moment);
    rotmom_.x = static_cast<float>(angular.x * moment + rotmom_.x);
    rotmom_.y = static_cast<float>(static_cast<double>(spin_about_y) + rotmom_.y);
    rotmom_.z = static_cast<float>(static_cast<double>(turn_z) + rotmom_.z);
    rotmom_.w = static_cast<float>(static_cast<double>(rot_w) + rotmom_.w);
    step_vehicle_speeds();
    return true;
}

// 0x004353F0
bool DrivableModule::wake(script::Struct*, script::Script*)
{
    awake_ = 1;
    still_frames_ = 0;
    return true;
}

// 0x00435410
bool DrivableModule::adjust_gravity(script::Struct* params, script::Script*)
{
    params->fetch_float(kPercent, &gravity_scale_, 1);
    gravity_scale_ = static_cast<float>(static_cast<double>(gravity_scale_) * kHundredth);
    params->fetch_float(kDuration, &gravity_time_, 1);
    debug::printf(kDebugFormat, kDebugFile, 0xD78,
                  "Initiating vehicle gravity override"); // "Initiating vehicle gravity override"
    return true;
}

// 0x00435480
bool DrivableModule::handbrake_active(script::Struct*, script::Script*) { return handbrake_active_; }

// 0x00435490
bool DrivableModule::all_wheels_are_in_contact(script::Struct*, script::Script*)
{
    return wheels_in_contact_ == wheel_count_;
}

// 0x00437BC0
bool DrivableModule::kick_randomly(script::Struct* params, script::Script*)
{
    maths::Vec4 along;
    params->fetch_vec(kOffset, &along, 1);
    kick_randomly_along(&along);
    return true;
}

// 0x004354B0
bool DrivableModule::is_rider_shown(script::Struct*, script::Script*) { return rider_shown_; }

// --- the component's life ---

namespace {

constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kTrigger = 0xE594F0A2;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kParkEditor = 0xD443A2BC;                     // the park editor's level (its +0x40)
constexpr uint32_t kSkater = 0x2ED67657;                         // a skater is shown driving
const float kToRadians = std::bit_cast<float>(0x3C8EFA35u);      // 0x00645464
const float kFeet = 12.0f;                                       // 0x0064546C
const float kRpmToRadians = std::bit_cast<float>(0x3DD67770u);   // 0x006467A4: 2 pi / 60
const float kThird = std::bit_cast<float>(0x3EAAAAABu);          // 0x006463D8
const float kCameraBack = 48.0f;                                 // 0x006461CC
const float kQuarter = 0.25f;                                    // 0x006467AC
const float kAbove = 24.0f;                                      // 0x00645EE8
const float kBelow = 240.0f;                                     // 0x00646558
const float kMaxStep = std::bit_cast<float>(0x3D088889u);        // 0x00646430: 1/30
const float kStepsPerSecond = std::bit_cast<float>(0x426FFFFFu); // 0x006467D0: 59.999996
const float kRimHalfWidth = std::bit_cast<float>(0x40766666u);   // 0x006467CC: 3.85
const float kSpokeAngle = std::bit_cast<float>(0x3F060A90u);     // 0x006467C8: pi / 6
const double kTwoTo32 = 4294967296.0;                            // 0x006453B4

// A count as the original loads it (FILD, then 2^32 added when negative: unsigned).
double as_unsigned(int value) { return value < 0 ? value + kTwoTo32 : value; }

// 0x00434F20 (the previous range; stdcall): a wheel's parameters from a structure.
void read_wheel(DrivableModule::Wheel* wheel, script::Struct* params) { read_wheel_settings(wheel, params); }
// The skate module's local skater (0x00566520) and a restart node's name (0x00566100).
GameEntity* local_skater() { return static_cast<GameEntity*>(modules::Skate::sole()->get_home_rider()); }
uint32_t skate_restart(int index) { return modules::Skate::sole()->warp_to_restart(static_cast<uint32_t>(index)); }
// The model's geometry (0x00493040 on the model's +0x208): its matrix at +0x60, the wheels' places (z up) at +0xE0 +
// 0x50 each.
std::span<const render::HierarchyNode> model_geometry(ObjectModule* model)
{
    return core::down_cast<objects::MeshInstanceModule>(model)->mesh_instance_->asset_hierarchy_run();
}
// Wheel `i`'s place: hierarchy node 2 + i's translation.
const maths::Vec4& wheel_place(std::span<const render::HierarchyNode> geometry, int i)
{
    return core::at(core::at(geometry, 2 + i).matrix.row, 3);
}
// The skeleton (0x005480F0): its bone matrices (0x005480C0) and how many (0x004A2BA0).
void* skeleton_of(ObjectModule* skeleton) { return core::down_cast<objects::BoneRigModule>(skeleton)->bone_rig_; }
// The viewport's camera (0x004A09C0; null if none), its position (0x00485A00) and matrix (0x00485A10).
void* viewport_camera(int viewport) { return render::fetch_live_camera(viewport); }
// 0x0041A730 (not rewritten yet): the rotation of a node's angles.
void matrix_from_angles(maths::Mat4* matrix, const maths::Vec4* angles) { matrix->init_from_angles(*angles); }

void clear_to_w1(maths::Vec4& v) { v = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}; }

} // namespace

// 0x0043AA30
gsl::owner<ObjectModule*> DrivableModule::fabricate() { return new DrivableModule; }

// 0x00437C00
DrivableModule::DrivableModule()
{
    clear_to_w1(orientation_);
    // (the colliders' vectors' constructors do nothing: 0x005902E0; contact_memo_ registered by its constructor)
    kind_tag_ = kType;
    clear_to_w1(skater_pos_);
}

// 0x0043B990 (0x0043C1A0 the deleting one)
DrivableModule::~DrivableModule()
{
    wheels_.clear(); // (as `delete[]`: the wheels destroyed first in the destructor)
    // (then contact_memo_ unregistered by its destructor)
}

// 0x00437D10
void DrivableModule::configure_from_data(script::Struct* params)
{
    // debug / 0xE6B5FCD9 / 0x751DA48B: the debug drawing's next mode, the previous one, off.
    if (params->has_bare_flag(0x935AB858))
        debug_mode_ = (debug_mode_ + 1) % 3;
    else if (params->has_bare_flag(0xE6B5FCD9))
        debug_mode_ = (debug_mode_ + 2) % 3;
    else if (params->has_bare_flag(0x751DA48B))
        debug_mode_ = 0;
    if (params->has_entry_named(0x93346B8C)) {
        // (read into the original's argument slot: missing, the height is that pointer's bits)
        float height = 0.0f; // (missing: 0 - the original read an argument's stack slot, an address)
        params->fetch_float(0x93346B8C, &height, 0);
        balance_point_ = {.x = 0.0f, .y = height, .z = 0.0f, .w = 1.0f};
    }
    if (params->has_entry_named(0x93FCA499)) { // mass
        params->fetch_float(0x93FCA499, &inv_mass_, 0);
        inv_mass_ = static_cast<float>(static_cast<double>(kGravity) / inv_mass_);
    }
    if (params->has_entry_named(0x8C6473EC)) { // moment_of_inertia (its w into +0x168)
        maths::Vec4 moments{.x = core::at(inv_moments_, 0), .y = core::at(inv_moments_, 1), .z = core::at(inv_moments_, 2), .w = moment_w_};
        params->fetch_vec(0x8C6473EC, &moments, 0);
        inv_moments_ = {{moments.x, moments.y, moments.z}};
        moment_w_ = moments.w;
        for (float& moment : inv_moments_)
            moment = static_cast<float>(static_cast<double>(kGravity) / moment);
    }
    params->fetch_float(0x79C3B862, &restitution_, 0);
    params->fetch_float(0xF476273C, &drag_factor_, 0);
    params->fetch_float(0x4A23A53C, &tipped_friction_, 0);
    params->fetch_float(0x01DD4890, &depth_correction_, 0);
    params->fetch_float(0xB9F026ED, &wall_steering_, 0);
    params->fetch_float(0x8C14709E, &align_speed_, 0);
    params->fetch_float(0x2C91E49B, &align_time_, 0);
    params->fetch_float(0x280F0E0B, &align_rate_, 0);
    align_when_slow_ = params->has_bare_flag(0x5341806A);
    if (params->has_entry_named(0xC1E5ABDD)) { // max_steering_angle (degrees)
        params->fetch_float(0xC1E5ABDD, &max_steering_angle_, 0);
        max_steering_angle_ = static_cast<float>(static_cast<double>(max_steering_angle_) * kToRadians);
    }
    params->fetch_float(0x386837DB, &yaw_damping_, 0);
    params->fetch_float(0x469CF79A, &spin_damping_, 0);
    can_dismount_ = params->has_bare_flag(0xC2A136CC);
    handbrake_disabled_ = params->has_bare_flag(0xAD6C0A46);

    // colliders: each three points, a corner and the ends of its two edges (the edges kept relative to the corner -
    // their w less the corner's, which nothing sets). The points' average distance from the body's origin, less 48,
    // goes to the skater (Rider 0x005802C0).
    if (params->has_entry_named(0xFC8C4AC6)) {
        script::Array* colliders = nullptr;
        params->fetch_list(0xFC8C4AC6, &colliders, 1);
        const int count = static_cast<int>(colliders->extent_);
        float sum = 0.0f;
        for (int k = count - 1; k >= 0; --k) {
            auto* points = colliders->array_at(static_cast<uint32_t>(k));
            Collider& c = core::at(colliders_, k);
            auto point = [points](int i) { return points->vector_at(static_cast<uint32_t>(i)); }; // (x, y, z)
            const script::Vector* p = point(0);
            c.corner.x = p->x;
            c.corner.y = p->y;
            c.corner.z = p->z;
            for (int e = 0; e < 2; ++e) {
                p = point(1 + e);
                maths::Vec4& edge = core::at(c.edges, e);
                edge.x = p->x;
                edge.y = p->y;
                edge.z = p->z;
                edge.x = static_cast<float>(static_cast<double>(edge.x) - c.corner.x);
                edge.y = static_cast<float>(static_cast<double>(edge.y) - c.corner.y);
                edge.z = static_cast<float>(static_cast<double>(edge.z) - c.corner.z);
                edge.w = static_cast<float>(static_cast<double>(edge.w) - c.corner.w);
            }
            auto length_squared = [](double x, double y, double z) { return (x * x + y * y) + z * z; };
            const maths::Vec4& o = c.corner;
            const maths::Vec4& a = core::at(c.edges, 0);
            const maths::Vec4& b = core::at(c.edges, 1);
            const double s0 = length_squared(o.x, o.y, o.z);
            const double s1 = length_squared(static_cast<double>(o.x) + a.x, static_cast<double>(a.y) + o.y,
                                             static_cast<double>(a.z) + o.z);
            auto s2 = static_cast<float>(length_squared(static_cast<double>(o.x) + b.x, static_cast<double>(b.y) + o.y,
                                                        static_cast<double>(b.z) + o.z));
            sum = static_cast<float>(((std::sqrt(s0) + sum) + std::sqrt(s1)) + std::sqrt(static_cast<double>(s2)));
            core::at(c.lengths, 0) = static_cast<float>(std::sqrt(length_squared(a.y, a.z, a.x)));
            core::at(c.lengths, 1) = static_cast<float>(std::sqrt(length_squared(b.y, b.z, b.x)));
        }
        auto distance = static_cast<float>(static_cast<double>(sum) * kThird / count - kCameraBack);
        core::down_cast<Rider>(local_skater())->set_42b_d60(distance);
    }

    // engine: drive_torque and 0xB2268648 in foot-pounds, upshift_rpm, differential_ratio, reverse_torque_ratio,
    // gear_ratios (as many as there are - room for six).
    if (params->has_entry_named(0x1757E572)) {
        script::Struct* engine = nullptr;
        params->read_sub_struct(0x1757E572, &engine, 1);
        if (engine->has_entry_named(0x9AA9FAEE)) {
            engine->fetch_float(0x9AA9FAEE, &engine_torque_, 0);
            engine_torque_ = static_cast<float>(static_cast<double>(engine_torque_) * kFeet);
        }
        if (engine->has_entry_named(0xB2268648)) {
            engine->fetch_float(0xB2268648, &engine_braking_, 0);
            engine_braking_ = static_cast<float>(static_cast<double>(engine_braking_) * kFeet);
        }
        if (engine->has_entry_named(0x33921583)) {
            engine->fetch_float(0x33921583, &upshift_speed_, 0);
            upshift_speed_ = static_cast<float>(static_cast<double>(upshift_speed_) * kRpmToRadians);
        }
        engine->fetch_float(0x94EA6601, &final_drive_ratio_, 0);
        engine->fetch_float(0x82BCBB10, &reverse_ratio_, 0);
        if (engine->has_entry_named(0xC023FA75)) {
            script::Array* ratios = nullptr;
            engine->fetch_list(0xC023FA75, &ratios, 1);
            gear_count_ = static_cast<int>(ratios->extent_);
            for (int i = 0; i < gear_count_; ++i)
                core::at(transmission_ratios_, i) = ratios->fetch_float(static_cast<uint32_t>(i));
        }
    }

    // Wheels: made the first time (as `new[]`: the count before them), each read from its structure; all_wheels
    // for every one.
    if (params->has_entry_named(0xB3F8557E)) {
        script::Array* wheels = nullptr;
        params->fetch_list(0xB3F8557E, &wheels, 1);
        const int count = static_cast<int>(wheels->extent_);
        wheel_count_ = count;
        if (wheels_.empty()) {
            // (the original's new[] took them from the memory manager's current heap, default-initialised: only the
            // feeler set up - a vector here, value-initialised)
            wheels_ = std::vector<Wheel>(static_cast<size_t>(count));
        }
        for (int i = wheel_count_ - 1; i >= 0; --i)
            read_wheel(&core::at(wheels_, i), wheels->struct_at(static_cast<uint32_t>(i)));
    }
    if (params->has_entry_named(0x371BADFF)) {
        script::Struct* all = nullptr;
        params->read_sub_struct(0x371BADFF, &all, 1);
        for (int i = wheel_count_ - 1; i >= 0; --i)
            read_wheel(&core::at(wheels_, i), all);
    }
    drive_wheel_count_ = 0;
    for (int i = wheel_count_ - 1; i >= 0; --i)
        if (core::at(wheels_, i).drive)
            ++drive_wheel_count_;

    // The orientation from the object's matrix (or Save's Orientation: the object's matrix from it); the position
    // from the object's (or Pos: the object's too).
    maths::Vec4 quat;
    matrix_to_quat(&quat, &entity_->basis_frame_);
    orientation_ = quat;
    quat_normalize(&orientation_, 1.0f);
    if (params->has_entry_named(0xAA99C521)) {
        maths::Vec4 saved{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
        params->fetch_vec(0xC97F3AA9, &saved, 0);
        orientation_.x = saved.x;
        orientation_.y = saved.y;
        orientation_.z = saved.z;
        quat_normalize(&orientation_, 1.0f);
        quat_to_matrix(&orientation_, &basis_frame_);
        clear_to_w1(core::at(basis_frame_.row, 3));
        entity_->basis_frame_ = basis_frame_;
        entity_->display_matrix_ = basis_frame_;
    }
    at_point_ = entity_->at_point_;
    if (params->has_entry_named(0x7F261953)) {
        params->fetch_vec(0x7F261953, &at_point_, 0);
        entity_->at_point_ = at_point_;
    }
    rider_shown_ = rider_shown_ || params->has_bare_flag(kSkater);
    if (params->has_bare_flag(kSkater)) {
        params->fetch_vec(0xEC86EF7A, &skater_pos_, 1); // skater_pos
        params->read_hash_value(0xDA75A33E, &rider_anim_name_, 1);                              // skater_anim
    }
    params->read_hash_value(0x0EDCF90E, &sounds_name_, 0); // sounds

    // At rest: no momenta or forces; the wheels in the air at their rest extension, each with an even share of the
    // weight.
    clear_to_w1(linear_momentum_);
    clear_to_w1(rotmom_);
    clear_to_w1(force_);
    clear_to_w1(twist_force_);
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        wheel.y = wheel.rest_y_;
        wheel.state = kWheelInAir;
        wheel.rotation = 0.0f;
        wheel.spin = 0.0f;
        wheel.steering = 0.0f;
        wheel.display_steering_ = 0.0f;
        wheel.torque = 0.0f;
        for (int k = 9; k >= 0; --k)
            core::at(wheel.load_history_, k) =
                static_cast<float>(static_cast<double>(kGravity) / inv_mass_ / as_unsigned(wheel_count_));
    }
    load_index_ = 0;
    gravity_time_ = 0.0f;
    gravity_scale_ = 1.0f;
    righting_ = false;
    bone_rig_ = entity_->fetch_module(kSkeleton);
}

// 0x0043BA20
void DrivableModule::seal()
{
    input_ptr_ = core::down_cast<PadInputModule>(entity_->fetch_module(kInput));
    mesh_instance_ = entity_->fetch_module(kModel);
    const std::span<const render::HierarchyNode> geometry = model_geometry(entity_->fetch_module(kModel));
    // The wheels' places from the model (z up there; always four).
    for (int i = 3; i >= 0; --i) {
        const maths::Vec4& place = wheel_place(geometry, i);
        core::at(wheels_, i).offset = {.x = -place.x, .y = place.z, .z = -place.y, .w = 1.0f};
    }
    const maths::Mat4 bone = core::at(geometry, 1).matrix;
    skater_bone_ = {.x = -core::at(bone.row, 3).x, .y = core::at(bone.row, 3).z, .z = -core::at(bone.row, 3).y, .w = 1.0f};
    // The wheelbase (a left wheel to a fixed one) and the track (left to right), from the lowest-numbered wheel of
    // each kind (-1 when there's none - read before the first, as the original).
    int fixed = -1, left = -1, right = -1;
    for (int i = 3; i >= 0; --i) {
        switch (core::at(wheels_, i).steering_side_) {
        case 0:
            fixed = i;
            break;
        case 1:
            left = i;
            break;
        case 2:
            right = i;
            break;
        default:
            break;
        }
    }
    wheelbase_ = static_cast<float>(
        std::fabs(static_cast<double>(wheel_place(geometry, left).y) - wheel_place(geometry, fixed).y));
    track_ = static_cast<float>(
        std::fabs(static_cast<double>(wheel_place(geometry, left).x) - wheel_place(geometry, right).x));
    // The suspension: the wheel's place raised by its diameter; its rest extension down from there, sagged by its
    // share of the weight.
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        Wheel& wheel = core::at(wheels_, i);
        const double y = wheel.offset.y;
        const double raised = (static_cast<double>(wheel.radius) + wheel.radius) + y;
        wheel.offset.y = static_cast<float>(raised);
        wheel.rest_y_ =
            static_cast<float>((y - raised) - static_cast<double>(kGravity) /
                                                  (as_unsigned(wheel_count_) * wheel.spring_rate_ * inv_mass_));
    }
    // The colliders' lowest point.
    float lowest = core::at(colliders_, 0).corner.y;
    for (int i = 1; i >= 0; --i) {
        const Collider& c = core::at(colliders_, i);
        if (c.corner.y < lowest)
            lowest = c.corner.y;
        for (const maths::Vec4& edge : c.edges) {
            const double end = static_cast<double>(edge.y) + c.corner.y;
            if (end < lowest)
                lowest = static_cast<float>(end);
        }
    }
    // The skater's components (hidden unless shown driving).
    rider_ref_ = local_skater();
    skater_core_physics_ = rider_ref_->fetch_module(kSkaterCorePhysics);
    trigger_ptr_ = rider_ref_->fetch_module(kTrigger);
    if (!rider_shown_)
        rider_ref_->conceal(true);
    else
        anim_blender_ptr_ = rider_ref_->fetch_module(kAnimBlender);
    // Everything made relative to the wheels' centre (x, z) and the colliders' lowest point.
    double sum_x = 0.0, sum_z = 0.0;
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        sum_x += core::at(wheels_, i).offset.x;
        sum_z += core::at(wheels_, i).offset.z;
    }
    const double share = 1.0 / as_unsigned(wheel_count_);
    auto centre_x = static_cast<float>(sum_x * share);
    const double centre_z = sum_z * share;
    auto recentre = [centre_x, centre_z, lowest](maths::Vec4& v) {
        v.x = static_cast<float>(static_cast<double>(v.x) - centre_x);
        v.y = static_cast<float>(static_cast<double>(v.y) - lowest);
        v.z = static_cast<float>(v.z - centre_z);
    };
    for (int i = wheel_count_ - 1; i >= 0; --i)
        recentre(core::at(wheels_, i).offset);
    for (int i = 1; i >= 0; --i)
        recentre(core::at(colliders_, i).corner);
    recentre(skater_bone_);
    recentre(skater_pos_);
    update_dependents();
    entity_->at_point_ = at_point_;
    entity_->vel_ = vel_;
    entity_->basis_frame_ = basis_frame_;
    entity_->display_matrix_ = entity_->basis_frame_;
    refresh_bone_rig();
}

// 0x0043C1D0
void DrivableModule::tick_frame()
{
    cut_short_ = false;
    if (!passive_) {
        read_controls();
    } else {
        brake_ = false;
        handbrake_ = false;
        throttle_ = false;
        reverse_ = false;
        steering_ = 0.0f;
    }
    if (awake_ == 0) {
        // Asleep: only the steering, the wheels' roll (stopped by the brake), the model and the skater.
        entity_->display_matrix_ = entity_->basis_frame_;
        update_steering();
        if (brake_)
            for (int i = wheel_count_ - 1; i >= 0; --i)
                core::at(wheels_, i).spin = 0.0f;
        update_wheel_rotations();
        refresh_bone_rig();
        update_skater();
        return;
    }
    auto frame = static_cast<float>(frame_clock::get_slomo_frame_seconds());
    if (kick_time_ > 0.0f)
        kick_time_ = static_cast<float>(static_cast<double>(kick_time_) - frame);
    if (!(gravity_time_ == 0.0f)) {
        // Vehicle_AdjustGravity's override: over when its time is up or two wheels touch.
        const double left = static_cast<double>(gravity_time_) - frame;
        gravity_time_ = static_cast<float>(left);
        if (left <= 0.0 || static_cast<uint32_t>(wheels_in_contact_) > 1) {
            debug::printf(kDebugFormat, kDebugFile, 0x2A0,
                          "Ending vehicle gravity override"); // "Ending vehicle gravity override"
            gravity_time_ = 0.0f;
            gravity_scale_ = 1.0f;
        }
    }
    // Equal steps of at most 1/30 s - six at most.
    int steps = 1;
    if (!(frame >= kMaxStep)) {
        step_length_ = frame;
    } else {
        steps = static_cast<int>(static_cast<int64_t>(std::ceil(static_cast<double>(frame) * kStepsPerSecond)));
        if (steps > 6)
            steps = 6;
        step_length_ = static_cast<float>(frame / static_cast<double>(steps));
    }
    while (steps != 0) {
        --steps;
        integrate();
        clear_to_w1(force_);
        clear_to_w1(twist_force_);
        for (int i = wheel_count_ - 1; i >= 0; --i)
            core::at(wheels_, i).torque = 0.0f;
        if (!passive_) {
            collide();
            if (cut_short_ || (entity_->object_flags_ & kObjectFlagDead))
                return;
        }
        update_wheel_contacts();
        if (cut_short_ || (entity_->object_flags_ & kObjectFlagDead))
            return;
        update_steering();
        apply_damping();
        apply_wheel_forces();
        if (wheels_in_contact_ != 0)
            air_time_ = 0.0f;
        else
            air_time_ = static_cast<float>(static_cast<double>(step_length_) + air_time_);
        if (wheels_in_contact_ == 0 && !(collision_impulse_ > 0.0f))
            free_fall_time_ = static_cast<float>(static_cast<double>(step_length_) + free_fall_time_);
        else
            free_fall_time_ = 0.0f;
    }
    if (passive_)
        return;
    debug_draw();
    entity_->at_point_ = at_point_;
    entity_->vel_ = vel_;
    entity_->basis_frame_ = basis_frame_;
    entity_->display_matrix_ = entity_->basis_frame_;
    check_sleep();
    refresh_bone_rig();
    update_skater();
}

// 0x0043A070
void DrivableModule::refresh_bone_rig()
{
    if (render::engine_parts().suppress_draw()) // (rendering off)
        return;
    // (the skeleton's +0x04, through the getter folded at 0x005480C0)
    const std::span<maths::Mat4> bones = static_cast<gfx::Rig*>(skeleton_of(bone_rig_))->matrices();
    const int count = static_cast<gfx::Rig*>(skeleton_of(bone_rig_))->get_num_bones();
    // Bone 0 the body (left as nothing), 1 the skater's place, 2 on the wheels: steered and rolled, at their place
    // with the suspension's extension, but no lower than max_drawn_y_. In debug mode 2 all of them nothing.
    for (int b = count - 1; b >= 0; --b) {
        maths::Mat4& bone = core::at(bones, b);
        std::memset(&bone, 0, sizeof(bone));
        if (debug_mode_ == 2)
            continue;
        if (b == 0)
            return;
        if (b == 1) {
            core::at(bone.row, 0).x = 1.0f;
            core::at(bone.row, 1).z = -1.0f;
            core::at(bone.row, 2).y = 1.0f;
            core::at(bone.row, 3) = skater_bone_;
            continue;
        }
        const Wheel& wheel = core::at(wheels_, b - 2);
        core::at(bone.row, 0).x = -1.0f;
        core::at(bone.row, 1).z = 1.0f;
        core::at(bone.row, 2).y = 1.0f;
        maths::Mat4 turn;
        matrix_multiply(&bone, &maths::make_roll_matrix(turn, wheel.display_steering_), &bone);
        matrix_multiply(&bone, &maths::make_pitch_matrix(turn, -wheel.rotation), &bone);
        core::at(bone.row, 3) = wheel.offset;
        const float drop = wheel.y > wheel.max_drawn_y_ ? wheel.max_drawn_y_ : wheel.y;
        core::at(bone.row, 3).y = static_cast<float>(static_cast<double>(drop) + core::at(bone.row, 3).y);
        core::at(bone.row, 3).w = 1.0f;
    }
}

// 0x0043A2C0
maths::Vec4* DrivableModule::get_wheel_rim_point(maths::Vec4* out, const Wheel* wheel, int spoke, bool outer)
{
    maths::Vec4 point;
    point.x = static_cast<float>((outer ? 1.0f : -1.0f) * static_cast<double>(kRimHalfWidth));
    const double angle = static_cast<double>(spoke) * kSpokeAngle + wheel->rotation;
    point.y = static_cast<float>(std::cos(angle) * wheel->radius);
    point.z = static_cast<float>(std::sin(angle) * wheel->radius);
    point.w = 1.0f;
    const maths::Vec4 up{.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};
    maths::Mat4 steer;
    maths::build_axis_angle_matrix(steer, up, wheel->steering);
    maths::Vec4 turned;
    steer.rotate_vector(turned, point);
    const maths::Vec4 placed{.x = static_cast<float>(static_cast<double>(turned.x) + wheel->offset.x),
                             .y = static_cast<float>((static_cast<double>(turned.y) + wheel->y) + wheel->offset.y),
                             .z = static_cast<float>(static_cast<double>(turned.z) + wheel->offset.z),
                             .w = static_cast<float>(static_cast<double>(turned.w) + wheel->offset.w)};
    maths::Vec4 world;
    basis_frame_.rotate_vector(world, placed);
    out->x = static_cast<float>(static_cast<double>(at_point_.x) + world.x);
    out->y = static_cast<float>(static_cast<double>(at_point_.y) + world.y);
    out->z = static_cast<float>(static_cast<double>(at_point_.z) + world.z);
    out->w = static_cast<float>(static_cast<double>(at_point_.w) + world.w);
    return out;
}

// 0x0043A400
void DrivableModule::debug_draw()
{
    // Each wheel's tyre (two rims of twelve spokes and the tread between them, coloured by its grip state) and its
    // axle; then (two spheres, compiled out as well) the centre of mass and the position. Nothing else is touched.
    if (!debug_mode_)
        return;
    for (int i = wheel_count_ - 1; i >= 0; --i) {
        const Wheel& wheel = core::at(wheels_, i);
        uint32_t colour = 0;
        switch (wheel.state) {
        case kWheelStaticSlow:
            colour = 0xFF00FF00;
            break;
        case kWheelStatic:
            colour = 0xFF000000;
            break;
        case kWheelStaticToDynamic:
            colour = 0xFF00C8FF;
            break;
        case kWheelDynamic:
            colour = 0xFF0000FF;
            break;
        case kWheelHandbrakeThrottle:
            colour = 0xFF960096;
            break;
        case kWheelHandbrakeLocked:
            colour = 0xFF969600;
            break;
        default:
            colour = 0xFFFFFFFF;
            break;
        }
        for (int spoke = 11; spoke >= 0; --spoke) {
            maths::Vec4 a, b;
            for (const bool outer : {true, false}) {
                get_wheel_rim_point(&a, &wheel, spoke, outer);
                get_wheel_rim_point(&b, &wheel, spoke + 1, outer);
                gfx::push_diag_line(&a, &b, colour, colour, 1);
            }
            get_wheel_rim_point(&a, &wheel, spoke, true);
            get_wheel_rim_point(&b, &wheel, spoke, false);
            gfx::push_diag_line(&a, &b, colour, colour, 1);
            // The axle: 240 either side of the wheel along its steered x, in the world.
            const maths::Vec4 up{.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};
            maths::Mat4 steer;
            maths::build_axis_angle_matrix(steer, up, wheel.steering);
            std::array<maths::Vec4, 2> ends{};
            for (int e = 0; e < 2; ++e) {
                const float side = e ? kBelow : -kBelow;
                const maths::Vec4 local{.x = core::at(steer.row, 0).x * side + wheel.offset.x,
                                        .y = core::at(steer.row, 0).y * side + wheel.offset.y + wheel.y,
                                        .z = core::at(steer.row, 0).z * side + wheel.offset.z, .w = 1.0f};
                maths::Vec4 world;
                basis_frame_.rotate_vector(world, local);
                core::at(ends, e) = {.x = world.x + at_point_.x, .y = world.y + at_point_.y, .z = world.z + at_point_.z, .w = world.w + at_point_.w};
            }
            gfx::push_diag_line(&core::at(ends, 0), &core::at(ends, 1), 0xFF000000, 0xFF000000, 1);
        }
    }
}

// 0x0043AA90 (not a Ghidra function)
void DrivableModule::glide_to_node(script::Struct* node)
{
    maths::Vec4 pos, angles;
    level_nodes::fetch_place(node, &pos);
    level_nodes::read_turn_angles(node, &angles);
    maths::Mat4 matrix;
    matrix_from_angles(&matrix, &angles);
    // How far the body's origin sits above the ground: the wheels' rest heights (sagged by their share of the
    // weight) less their radii, averaged (always four wheels).
    const double count = as_unsigned(wheel_count_);
    double height = 0.0;
    for (int i = 3; i >= 0; --i) {
        const Wheel& wheel = core::at(wheels_, i);
        const double rest =
            static_cast<double>(kGravity) / (static_cast<double>(wheel.spring_rate_) * inv_mass_ * count) +
            wheel.offset.y + wheel.rest_y_;
        height = (height + rest) - wheel.radius;
    }
    auto above = static_cast<float>(height * kQuarter);
    // The ground under the node (from 24 above to 240 below).
    const maths::Vec4 start{.x = pos.x, .y = static_cast<float>(static_cast<double>(pos.y) + kAbove), .z = pos.z,
                            .w = static_cast<float>(static_cast<double>(pos.w) + 1.0)};
    const maths::Vec4 end{.x = pos.x, .y = static_cast<float>(static_cast<double>(pos.y) - kBelow), .z = pos.z, .w = start.w};
    render::ProbeRay feeler(&start, &end);
    if (feeler.get_hit_shape(false, false))
        pos.y = static_cast<float>(static_cast<double>(feeler.contact_pos_.y) - above);
    // The node marks the fixed axle (the first fixed wheel from the last): back along the node's facing by its place.
    int fixed = -1;
    for (int i = 3; i >= 0; --i) {
        if (core::at(wheels_, i).steering_side_ == 0) {
            fixed = i;
            break;
        }
    }
    const double back = core::at(wheels_, fixed).offset.z;
    const maths::Vec4& at = core::at(matrix.row, 2);
    at_point_.x = pos.x = static_cast<float>(pos.x - at.x * back);
    at_point_.y = pos.y = static_cast<float>(pos.y - at.y * back);
    at_point_.z = pos.z = static_cast<float>(pos.z - at.z * back);
    at_point_.w = static_cast<float>(pos.w - at.w * back);
    maths::Vec4 quat;
    matrix_to_quat(&quat, &matrix);
    orientation_ = quat;
    clear_to_w1(linear_momentum_);
    clear_to_w1(rotmom_);
    // Settled by sixty updates without input, collision or debug drawing, the wheels kept from turning.
    passive_ = true;
    for (int n = 60; n != 0; --n) {
        for (int i = wheel_count_ - 1; i >= 0; --i)
            core::at(wheels_, i).spin = 0.0f;
        tick_frame();
    }
    passive_ = false;
    clear_to_w1(linear_momentum_);
    clear_to_w1(rotmom_);
    cut_short_ = true;
    awake_ = 0;
    entity_->at_point_ = at_point_;
    entity_->vel_ = vel_;
    entity_->basis_frame_ = basis_frame_;
    entity_->display_matrix_ = entity_->basis_frame_;
    refresh_bone_rig();
    update_skater();
    entity_->mark_jumped_ahead(true);
    rider_ref_->mark_jumped_ahead(true);
}

// 0x0043B670 (not a Ghidra function)
bool DrivableModule::teleport_to_spawn(script::Struct* params, script::Script* script)
{
    // The node named; else the first restart linked from the script's node; else (outside the park editor) the skate
    // module's first restart.
    uint32_t name = 0;
    if (params->read_hash_value(0u, &name, 0)) {
        glide_to_node(level_nodes::fetch_vertex(name, true));
        return true;
    }
    const uint32_t from = script->list_link();
    const int links = static_cast<int>(level_nodes::get_link_count(from));
    for (int i = 0; i < links; ++i) {
        const uint32_t link = level_nodes::fetch_link(from, i);
        if (is_restart_node(link)) {
            glide_to_node(level_nodes::fetch_vertex(link, true));
            return true;
        }
    }
    if (park_editing::ParkBuilderMode::sole()->cur_phase_ == kParkEditor)
        return true;
    glide_to_node(level_nodes::fetch_vertex(skate_restart(0), true));
    return true;
}

// 0x0043B720
bool DrivableModule::place_before_camera(script::Struct*, script::Script*)
{
    void* camera = viewport_camera(0);
    if (!camera)
        return true;
    const maths::Vec4& eye = *static_cast<gfx::ViewCam*>(camera)->fetch_location();
    const maths::Mat4& view = *static_cast<gfx::ViewCam*>(camera)->fetch_mat();
    at_point_ = eye;
    // 144 along the camera's up, then 144 back along its at (12 feet each, scaled twice - as the original).
    const maths::Vec4& up = core::at(view.row, 1);
    const double up_x = static_cast<double>(up.x) * kFeet;
    const double up_y = static_cast<double>(up.y) * kFeet;
    auto up_z = static_cast<float>(static_cast<double>(up.z) * kFeet);
    auto up_w = static_cast<float>(static_cast<double>(up.w) * kFeet);
    at_point_.x = static_cast<float>(static_cast<double>(static_cast<float>(up_x * kFeet)) + at_point_.x);
    at_point_.y = static_cast<float>(up_y * kFeet + at_point_.y);
    at_point_.z = static_cast<float>(static_cast<double>(up_z) * kFeet + at_point_.z);
    at_point_.w =
        static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(up_w) * kFeet)) + at_point_.w);
    const maths::Vec4& at = core::at(view.row, 2);
    const double at_x = static_cast<double>(at.x) * kFeet;
    const double at_y = static_cast<double>(at.y) * kFeet;
    auto at_z = static_cast<float>(static_cast<double>(at.z) * kFeet);
    auto at_w = static_cast<float>(static_cast<double>(at.w) * kFeet);
    at_point_.x = static_cast<float>(static_cast<double>(at_point_.x) - static_cast<float>(at_x * kFeet));
    at_point_.y = static_cast<float>(at_point_.y - at_y * kFeet);
    at_point_.z = static_cast<float>(at_point_.z - static_cast<double>(at_z) * kFeet);
    at_point_.w =
        static_cast<float>(static_cast<double>(at_point_.w) - static_cast<float>(static_cast<double>(at_w) * kFeet));
    entity_->at_point_ = at_point_;
    // Facing away from the camera.
    core::at(basis_frame_.row, 0) = {.x = -core::at(view.row, 0).x, .y = -core::at(view.row, 0).y, .z = -core::at(view.row, 0).z, .w = -core::at(view.row, 0).w};
    core::at(basis_frame_.row, 1) = core::at(view.row, 1);
    core::at(basis_frame_.row, 2) = {.x = -core::at(view.row, 2).x, .y = -core::at(view.row, 2).y, .z = -core::at(view.row, 2).z, .w = -core::at(view.row, 2).w};
    maths::Vec4 quat;
    matrix_to_quat(&quat, &basis_frame_);
    orientation_ = quat;
    quat_normalize(&orientation_, 1.0f);
    quat_to_matrix(&orientation_, &basis_frame_);
    clear_to_w1(core::at(basis_frame_.row, 3));
    entity_->basis_frame_ = basis_frame_;
    update_dependents();
    return true;
}

} // namespace objects
