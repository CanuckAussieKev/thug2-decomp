// The VehicleCamera component (GameObjects::CarView): see vehicle_camera.h. From the disassembly of
// 0x00524420-0x00524C1E (0x00524420 isn't a Ghidra function).

#include "game/rider/vehicle_camera.h"
#include "platform/d3dx.h"

#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/camera_util.h"
#include "engine/objects/compositemgr.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/maths/trig.h"
#include "platform/stopwatch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_component.h"
#include "engine/objects/vehicle_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

std::array<script::MemberFunctionEntry, 2> g_vehicle_camera_member_functions{{
    {.callsign_ = 0x000469FD, .owner_kind_ = 0x2C747D8A}, {.callsign_ = 0, .owner_kind_ = 0x2C747D8A}}};

namespace {

constexpr uint32_t kVehicleCamera = 0x2C747D8A;
constexpr uint32_t kVehicle = 0xE47F1B79;
constexpr uint32_t kCamera = 0xC4E311FA;
constexpr uint32_t kCameraLookAround = 0x81C7122D;
constexpr uint32_t kSubject = 0x0431C185, kLerp = 0x9213625F, kHeight = 0x14849B6D, kDistance = 0xBD3D3CA9,
                   kAngle = 0xFF7EBAF6;

constexpr float kSpeedScale = 0.0037878789007663727f; // 0x0064C5D4
constexpr float kMinFlatSpeed = 88000000.0f;          // 0x0064C5D8
constexpr float kPi = 3.1415927410125732f;            // 0x0064C59C
constexpr float kHalfPi = 1.5707963705062866f;        // 0x00645484
constexpr float kDegrees = 0.01745329238474369f;      // 0x00645464 (degrees to radians)

// Its Camera component's camera (+0x18).
gfx::ViewCam* camera_of(GameEntity* object)
{
    return core::down_cast<objects::ViewpointModule>(object->fetch_module(kCamera))->view_camera_.get();
}

// D3DXMatrixMultiply (linked later; 0x005BFA8C).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

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
CarView::CarView() { kind_tag_ = kVehicleCamera; }

// 0x00524420
void CarView::publish_method_table()
{
    script::register_member_function_table(g_vehicle_camera_member_functions);
}

// 0x00524440 (0x00524470 the deleting one)
CarView::~CarView() = default;

// 0x005244A0
void CarView::configure_from_data(script::Struct* params)
{
    if (params->has_entry_named(kSubject)) {
        uint32_t id = 0;
        params->read_hash_value(kSubject, &id, 1);
        subject_ = core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(id));
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
    free_look_ = core::down_cast<FreeLookModule>(entity_->fetch_module(kCameraLookAround));
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
    const auto& velocity = vehicle_->vel_;
    float x = -velocity[0];
    float y = 0.0f;
    float z = -velocity[2];
    const float w = -velocity[3];
    const double length = std::sqrt(static_cast<double>(z) * z + static_cast<double>(x) * x);
    auto speed = static_cast<float>(length);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        x = static_cast<float>(x * inverse);
        y = static_cast<float>(0.0f * inverse);
        z = static_cast<float>(z * inverse);
    }
    if (vehicle_->wheels_in_contact_ < 2) {
        if (!(speed < kMinFlatSpeed))
            target_direction_ = {.x = x, .y = y, .z = z, .w = w};
        return;
    }
    double scale = static_cast<double>(speed) * kSpeedScale;
    if (scale > 1.0)
        scale = 1.0;
    const maths::Vec4& at = core::at(subject_->basis_frame_.row, 2);
    const double a = -at.x;
    const double b = -at.y;
    const double c = -at.z;
    auto along = static_cast<float>((c * z + b * y + a * x) * scale);
    target_direction_ = {.x = -at.x, .y = -at.y, .z = -at.z, .w = -at.w};
    target_direction_.y = 0.0f;
    target_direction_.make_unit();
    if (along > 0.0f) {
        target_direction_.x = static_cast<float>(static_cast<double>(x) * along + target_direction_.x);
        target_direction_.y = static_cast<float>(static_cast<double>(y) * along + target_direction_.y);
        target_direction_.z = z * along + target_direction_.z;
        target_direction_.w = w * along + target_direction_.w;
        target_direction_.make_unit();
    }
}

// 0x00524720
void CarView::apply_to_camera()
{
    anchor_point_.w = 1.0f;
    core::at(orientation_.row, 0).w = 0.0f;
    core::at(orientation_.row, 1).w = 0.0f;
    core::at(orientation_.row, 2).w = 0.0f;
    entity_->at_point_ = anchor_point_;
    entity_->basis_frame_ = orientation_;
    entity_->display_matrix_ = orientation_;
}

// 0x00524780
void CarView::update_orientation()
{
    const double heading = free_look_->heading_;
    double rotated_x = 0.0, rotated_z = 0.0;
    maths::spin_by(heading, direction_.x, direction_.z, &rotated_x, &rotated_z);
    auto right = static_cast<float>(rotated_z);
    auto forward = static_cast<float>(rotated_x);
    core::at(orientation_.row, 0) = {.x = right, .y = 0.0f, .z = -forward, .w = 1.0f};
    core::at(orientation_.row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 1.0f};
    core::at(orientation_.row, 2) = {.x = forward, .y = direction_.y, .z = right, .w = direction_.w};
    core::at(orientation_.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    const maths::Vec4& subject = subject_->at_point_;
    const float back = right * reach_;
    anchor_point_.x = static_cast<float>(static_cast<double>(forward) * reach_ + subject.x);
    anchor_point_.y = level_y_ + subject.y;
    anchor_point_.z = back + subject.z;
    anchor_point_.w = subject.w + 1.0f;
    maths::Mat4 tilt{};
    maths::make_roll_matrix(tilt, angle_ * kDegrees);
    matrix_multiply(&orientation_, &tilt, &orientation_);
}

// 0x00524B40
void CarView::get_reset()
{
    free_look_->heading_ = 0.0f;
    free_look_->lean_angle_ = 0.0f;
    free_look_->locked_ = false;
    const maths::Vec4& at = core::at(subject_->basis_frame_.row, 2);
    target_direction_ = {.x = -at.x, .y = -at.y, .z = -at.z, .w = -at.w};
    target_direction_.y = 0.0f;
    target_direction_.make_unit();
    update_target_direction();
    direction_ = target_direction_;
    update_orientation();
    apply_to_camera();
    entity_->fetch_module(kCamera)->tick_frame();
}

// 0x00524940
void CarView::tick_frame()
{
    camera_of(entity_)->remember_prev_pos();
    update_target_direction();
    const double dot = static_cast<double>(direction_.z) * target_direction_.z +
                       static_cast<double>(direction_.y) * target_direction_.y +
                       static_cast<double>(direction_.x) * target_direction_.x;
    auto cosine = static_cast<float>(dot);
    if (dot > 1.0)
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    bool obtuse = false;
    float angle = maths::acos_f(cosine, kHalfPi, &obtuse);
    if (obtuse)
        angle = kPi - angle;
    const bool left = static_cast<double>(direction_.z) * target_direction_.x -
                          static_cast<double>(target_direction_.z) * direction_.x >
                      0.0;
    double step = frame_clock::get_slomo_frame_seconds() * lerp_ * angle;
    if (step > angle)
        step = angle;
    const double turn = (left ? 1.0f : -1.0f) * step;
    double x = 0.0, z = 0.0;
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
