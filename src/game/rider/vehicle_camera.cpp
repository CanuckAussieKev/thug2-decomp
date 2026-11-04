// The VehicleCamera component (GameObjects::CarView): see vehicle_camera.h. From the disassembly of
// 0x00524420-0x00524C1E (0x00524420 isn't a Ghidra function).

#include "game/rider/vehicle_camera.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/gfx/view_cam.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/camera_util.h"
#include "engine/objects/compositemgr.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "base/maths/trig.h"
#include "engine/sys/stopwatch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_component.h"
#include "engine/objects/vehicle_component.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"

namespace objects {
namespace {
constexpr uint32_t kVehicleCameraReset = 0x000469FD; // VehicleCamera_Reset
constexpr uint32_t kVehicleCameraName = 0x2C747D8A; // VehicleCamera
} // namespace

std::array<script::MemberFunctionEntry, 2>& vehicle_camera_member_functions()
{
    static std::array<script::MemberFunctionEntry, 2> vehicle_camera_member_functions{{
        {.callsign_ = kVehicleCameraReset, .owner_kind_ = kVehicleCameraName}, {.callsign_ = 0, .owner_kind_ = kVehicleCameraName}}};
    return vehicle_camera_member_functions;
}

namespace {

constexpr uint32_t kVehicleCamera = 0x2C747D8A;
constexpr uint32_t kVehicle = 0xE47F1B79;
constexpr uint32_t kCamera = 0xC4E311FA;
constexpr uint32_t kCameraLookAround = 0x81C7122D;
constexpr uint32_t kSubject = 0x0431C185, kLerp = 0x9213625F, kHeight = 0x14849B6D, kDistance = 0xBD3D3CA9,
                   kAngle = 0xFF7EBAF6;

constexpr float kSpeedScale = 0.0037878789007663727F; // 0x0064C5D4
constexpr float kMinFlatSpeed = 88000000.0F;          // 0x0064C5D8
constexpr float kHalfPi = 1.5707963705062866F;        // 0x00645484
constexpr float kDegrees = 0.01745329238474369F;      // 0x00645464 (degrees to radians)

// Its Camera component's camera (+0x18).
gfx::ViewCam* camera_of(GameEntity* object)
{
    return core::down_cast<objects::ViewpointModule>(object->fetch_module(kCamera))->view_camera().get();
}


// The camera position the collision check pulls toward: `distance` along the orientation's at-row before
// `position` (x with the product unrounded, as the original).
maths::Vec4 get_behind(const maths::Vec4& position, float distance, const maths::Mat4& orientation)
{
    const maths::Vec4& at = core::at(orientation.row, 2);
    const double x = static_cast<double>(distance) * at.x;
    const float y = distance * at.y;
    const float z = distance * at.z;
    const float w = distance * at.w;
    return {.x = static_cast<float>(position.x - x), .y = position.y - y, .z = position.z - z, .w = position.w - w};
}

} // namespace

// 0x005248C0
gsl::owner<ObjectModule*> CarView::fabricate() { return new CarView; }

// (the constructor: inlined in fabricate in the original)
CarView::CarView() { set_kind_tag(kVehicleCamera); }

// 0x00524420
void CarView::publish_method_table()
{
    script::register_member_function_table(vehicle_camera_member_functions());
}

// 0x00524440 (0x00524470 the deleting one)
CarView::~CarView() = default;

// 0x005244A0
void CarView::configure_from_data(script::Struct* params)
{
    if (params->has_entry_named(kSubject)) {
        uint32_t id = 0;
        params->read_hash_value(kSubject, &id, 1);
        subject_ = core::down_cast<GameEntity>(objects::EntityHub::look_up_entity(id));
        vehicle_ = core::down_cast<objects::DrivableModule>(subject_->fetch_module(kVehicle));
    }
    params->fetch_float(kLerp, &lerp_, 0);
    params->fetch_float(kHeight, &level_y_, 0);
    params->fetch_float(kDistance, &reach_, 0);
    params->fetch_float(kAngle, &angle_, 0);
}

// 0x00524C00
void CarView::seal()
{
    free_look_ = core::down_cast<FreeLookModule>(entity()->fetch_module(kCameraLookAround));
    get_reset();
}

// 0x00524450
bool CarView::vehicle_camera_reset(script::Struct* params, script::Script* /*script*/)
{
    reapply_from_data(params);
    seal();
    return true;
}

// 0x00524550
void CarView::update_target_direction()
{
    const auto& velocity = vehicle_->vel();
    float x = -velocity[0];
    float y = 0.0F;
    float z = -velocity[2];
    const float w = -velocity[3];
    const double length = std::sqrt((static_cast<double>(z) * z) + (static_cast<double>(x) * x));
    auto speed = static_cast<float>(length);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        x = static_cast<float>(x * inverse);
        y = static_cast<float>(0.0F * inverse);
        z = static_cast<float>(z * inverse);
    }
    if (vehicle_->wheels_in_contact() < 2) {
        if (!(speed < kMinFlatSpeed)) {
            target_direction_ = {.x = x, .y = y, .z = z, .w = w};
        }
        return;
    }
    double scale = static_cast<double>(speed) * kSpeedScale;
    scale = std::min(scale, 1.0);
    const maths::Vec4& at = core::at(subject_->basis_frame().row, 2);
    const double back_x = -at.x;
    const double back_y = -at.y;
    const double back_z = -at.z;
    auto along = static_cast<float>(((back_z * z) + (back_y * y) + (back_x * x)) * scale);
    target_direction_ = {.x = -at.x, .y = -at.y, .z = -at.z, .w = -at.w};
    target_direction_.y = 0.0F;
    target_direction_.make_unit();
    if (along > 0.0F) {
        target_direction_.x = static_cast<float>((static_cast<double>(x) * along) + target_direction_.x);
        target_direction_.y = static_cast<float>((static_cast<double>(y) * along) + target_direction_.y);
        target_direction_.z = (z * along) + target_direction_.z;
        target_direction_.w = (w * along) + target_direction_.w;
        target_direction_.make_unit();
    }
}

// 0x00524720
void CarView::apply_to_camera()
{
    anchor_point_.w = 1.0F;
    core::at(orientation_.row, 0).w = 0.0F;
    core::at(orientation_.row, 1).w = 0.0F;
    core::at(orientation_.row, 2).w = 0.0F;
    entity()->at_point() = anchor_point_;
    entity()->basis_frame() = orientation_;
    entity()->display_matrix() = orientation_;
}

// 0x00524780
void CarView::update_orientation()
{
    const double heading = free_look_->heading();
    double rotated_x = 0.0;
    double rotated_z = 0.0;
    maths::spin_by(heading, direction_.x, direction_.z, &rotated_x, &rotated_z);
    auto right = static_cast<float>(rotated_z);
    auto forward = static_cast<float>(rotated_x);
    core::at(orientation_.row, 0) = {.x = right, .y = 0.0F, .z = -forward, .w = 1.0F};
    core::at(orientation_.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
    core::at(orientation_.row, 2) = {.x = forward, .y = direction_.y, .z = right, .w = direction_.w};
    core::at(orientation_.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    const maths::Vec4& subject = subject_->at_point();
    const float back = right * reach_;
    anchor_point_.x = static_cast<float>((static_cast<double>(forward) * reach_) + subject.x);
    anchor_point_.y = level_y_ + subject.y;
    anchor_point_.z = back + subject.z;
    anchor_point_.w = subject.w + 1.0F;
    maths::Mat4 tilt{};
    maths::make_roll_matrix(tilt, angle_ * kDegrees);
    maths::matrix_multiply(&orientation_, &tilt, &orientation_);
}

// 0x00524B40
void CarView::get_reset()
{
    free_look_->set_heading(0.0F);
    free_look_->set_lean_angle(0.0F);
    free_look_->assign_locked(false);
    const maths::Vec4& at = core::at(subject_->basis_frame().row, 2);
    target_direction_ = {.x = -at.x, .y = -at.y, .z = -at.z, .w = -at.w};
    target_direction_.y = 0.0F;
    target_direction_.make_unit();
    update_target_direction();
    direction_ = target_direction_;
    update_orientation();
    apply_to_camera();
    entity()->fetch_module(kCamera)->tick_frame();
}

// 0x00524940
void CarView::tick_frame()
{
    camera_of(entity())->remember_prev_pos();
    update_target_direction();
    const double dot = (static_cast<double>(direction_.z) * target_direction_.z) +
                       (static_cast<double>(direction_.y) * target_direction_.y) +
                       (static_cast<double>(direction_.x) * target_direction_.x);
    auto cosine = static_cast<float>(dot);
    if (dot > 1.0) {
        cosine = 1.0F;
    } else if (cosine < -1.0F) {
        cosine = -1.0F;
    }
    bool obtuse = false;
    float angle = maths::acos_f({.value = cosine, .limit = kHalfPi}, &obtuse);
    if (obtuse) {
        angle = maths::kPi - angle;
    }
    const bool left = (static_cast<double>(direction_.z) * target_direction_.x) -
                          (static_cast<double>(target_direction_.z) * direction_.x) >
                      0.0;
    double step = frame_clock::get_slomo_frame_seconds() * lerp_ * angle;
    step = std::min<double>(step, angle);
    const double turn = (left ? 1.0F : -1.0F) * step;
    double x = 0.0;
    double z = 0.0;
    maths::spin_by(turn, direction_.x, direction_.z, &x, &z);
    direction_.x = static_cast<float>(x);
    direction_.z = static_cast<float>(z);
    direction_.make_unit();
    update_orientation();
    const maths::Vec4 focus = get_behind(anchor_point_, reach_, orientation_);
    const maths::Vec4 target = get_behind(anchor_point_, reach_, orientation_);
    avoid_camera_collisions(&anchor_point_, &orientation_, &target, &focus, false, false);
    apply_to_camera();
}

} // namespace objects
