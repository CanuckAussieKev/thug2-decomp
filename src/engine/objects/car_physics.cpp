// The car physics component (GameObjects::AutomobileDynamicsModule): see car_physics.h. From the disassembly of
// 0x0041B910-0x0041C1ED. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "engine/objects/path_follower.h"
#include "engine/objects/car_physics.h"
#include "platform/d3dx.h"
#include "engine/gfx/model.h"

#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <array>
#include <span>

#include "engine/maths/mat4.h"
#include "engine/maths/maths.h"
#include "engine/maths/vec4.h"
#include "engine/objects/motion_path.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/model_component.h"
#include "engine/objects/skeleton_component.h"
#include "engine/gfx/rig.h"
#include "engine/objects/motion.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/scene_load.h"

namespace objects {

std::array<script::MemberFunctionEntry, 3>& car_physics_member_functions()
{
    static std::array<script::MemberFunctionEntry, 3> car_physics_member_functions{{
        {.callsign_ = 0xA2331896, .owner_kind_ = 0xC695136D}, {.callsign_ = 0xC9F0DB4B, .owner_kind_ = 0xC695136D}, {.callsign_ = 0, .owner_kind_ = 0xC695136D}}};
    return car_physics_member_functions;
}

namespace {

constexpr uint32_t kMinTilt = 0x877DB8DA;
constexpr uint32_t kMaxTilt = 0xFB3464DF;
constexpr uint32_t kTiltRate = 0xFD8BC8D6;
constexpr uint32_t kTiltDivisor = 0xB2C7447B;
constexpr uint32_t kReadGlobalsEveryUpdate = 0x4CF13D8F; // (a script global: while set, the four re-read each update)

constexpr uint32_t kMotion = 0x0A015E17;
constexpr uint32_t kSuspend = 0xCE0CA665;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kModel = 0x286A8D26;

// The bones it turns.
constexpr uint32_t kWheelA = 0x80212262, kWheelB = 0x1E45B7C1;
constexpr uint32_t kWheelC = 0x69428757, kWheelD = 0xF04BD6ED;
constexpr uint32_t kSteeringWheelA = 0x6E2F434E, kSteeringWheelB = 0xF72612F4;
constexpr uint32_t kBody = 0x88C21962; // car

constexpr float kFramesPerSecond = 60.0f;              // 0x00645EA0
constexpr float kMinusRadiansToDegrees = -57.2957764f; // 0x00645EA4
constexpr float kFullTurn = 360.0f;                    // 0x00645EA8
constexpr float kSpinScale = 0.000668449211f;          // 0x00645EAC
constexpr float kSpinSpeedScale = 90.0f;               // 0x00645EB0
constexpr float kRadiansPerDegree = 0.0174532924f;     // 0x00645464

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// The model's skeleton description (0x00493040, no source yet).
std::span<const render::HierarchyNode> model_bones(void* model)
{
    return static_cast<render::RenderModel*>(model)->asset_hierarchy_run();
}
// A skeleton's bone matrices (+0x04) and bone count (+0x14) - the folded getters 0x005480C0 / 0x004A2BA0.
std::span<maths::Mat4> rig_matrix_block(void* skeleton) { return static_cast<gfx::Rig*>(skeleton)->matrices(); }
int skeleton_bone_count(void* skeleton) { return static_cast<gfx::Rig*>(skeleton)->bone_total(); }

// The motion component's speed (+0x30) and path follower (0x00423510).
float motion_speed(KinematicsModule* motion) { return motion->path_speed(); }
PathFollower* motion_path_follower(KinematicsModule* motion)
{
    return motion->get_path_follower();
}

// The suspend component (no source yet): suspended (+0x30, the getter 0x00433E90), and whether its object should be
// updated this frame (0x00433EA0).
bool suspend_suspended(const DormancyModule* suspend) { return suspend->far(); }
bool suspend_should_update(DormancyModule* suspend, float* distance)
{
    return suspend->wants_animating(distance);
}

} // namespace

// 0x0041BD90
gsl::owner<ObjectModule*> AutomobileDynamicsModule::fabricate() { return new AutomobileDynamicsModule; }

// 0x0041B910
void AutomobileDynamicsModule::publish_method_table()
{
    script::register_member_function_table(car_physics_member_functions());
}

// 0x0041BAD0
AutomobileDynamicsModule::AutomobileDynamicsModule() { kind_tag_ = kType; }

// 0x0041B930 (0x0041BB00 the deleting one)
AutomobileDynamicsModule::~AutomobileDynamicsModule() = default;

// 0x0041B940
void AutomobileDynamicsModule::configure_from_data(script::Struct* params)
{
    min_tilt_ = script::fetch_float(kMinTilt);
    params->fetch_float(kMinTilt, &min_tilt_, 0);
    max_tilt_ = script::fetch_float(kMaxTilt);
    params->fetch_float(kMaxTilt, &max_tilt_, 0);
    tilt_rate_ = script::fetch_float(kTiltRate);
    params->fetch_float(kTiltRate, &tilt_rate_, 0);
    tilt_divisor_ = script::fetch_float(kTiltDivisor);
    params->fetch_float(kTiltDivisor, &tilt_divisor_, 0);
}

// 0x0041BE10
void AutomobileDynamicsModule::seal()
{
    motion_ = core::down_cast<KinematicsModule>(entity_->fetch_module(kMotion));
    suspend_ = core::down_cast<DormancyModule>(entity_->fetch_module(kSuspend));
    ObjectModule* skeleton = entity_->fetch_module(kSkeleton);
    if (!skeleton)
        return;
    mesh_instance_ = core::down_cast<objects::MeshInstanceModule>(entity_->fetch_module(kModel))->mesh_instance().get();
    bone_rig_ = core::down_cast<objects::BoneRigModule>(skeleton)->bone_rig_;
    const std::span<const render::HierarchyNode> bones = model_bones(mesh_instance_);
    maths::Mat4 a = core::at(bones, 2).matrix;
    matrix_multiply(&a, &a, &core::at(bones, 0).matrix);
    maths::Mat4 b = core::at(bones, 4).matrix;
    matrix_multiply(&b, &b, &core::at(bones, 0).matrix);
    const double dx = static_cast<double>(core::at(a.row, 3).x) - core::at(b.row, 3).x;
    const double dy = static_cast<double>(core::at(a.row, 3).y) - core::at(b.row, 3).y;
    const double dz = static_cast<double>(core::at(a.row, 3).z) - core::at(b.row, 3).z;
    wheel_base_ = static_cast<float>(std::fabs(std::sqrt(dz * dz + dy * dy + dx * dx)));
}

// 0x0041BB30
void AutomobileDynamicsModule::update_angles()
{
    KinematicsModule* motion = motion_;
    // The spin, kept within 0-360 (the steps taken on the FPU stack, from the stored value).
    const double frames = frame_clock::get_slomo_frame_seconds() * kFramesPerSecond;
    const double spin =
        frames * (static_cast<double>(motion_speed(motion)) * kSpinSpeedScale) * kSpinScale + wheel_spin_;
    wheel_spin_ = static_cast<float>(spin);
    if (spin > kFullTurn) {
        double turned = wheel_spin_;
        turned -= kFullTurn;
        while (turned > kFullTurn)
            turned -= kFullTurn;
        wheel_spin_ = static_cast<float>(turned);
    }
    if (wheel_spin_ < 0.0f) {
        double turned = wheel_spin_;
        turned += kFullTurn;
        while (turned < 0.0)
            turned += kFullTurn;
        wheel_spin_ = static_cast<float>(turned);
    }
    // The steering.
    const PathFollower* follower = motion_path_follower(motion);
    if (!follower) {
        steer_ = 0.0f;
    } else {
        // (the original's +0xE0 / +0x04 / +0xB0: the follower's last arc step, its position and the arc's centre)
        const float turn = follower->step();
        if (turn == 0.0f) {
            target_steer_ = 0.0f;
        } else {
            const auto& point = follower->at_point();
            const auto& from = follower->center();
            const double dx = static_cast<double>(point[0]) - from[0];
            const double dy = static_cast<double>(point[1]) - from[1];
            const double dz = static_cast<double>(point[2]) - from[2];
            const double ratio = wheel_base_ / std::sqrt(dz * dz + dy * dy + dx * dx);
            auto clamped = static_cast<float>(ratio);
            if (ratio > 1.0f)
                clamped = 1.0f;
            else if (clamped < -1.0f)
                clamped = -1.0f;
            const double target = std::asin(static_cast<double>(clamped)) * kMinusRadiansToDegrees;
            target_steer_ = static_cast<float>(target);
            if (turn < 0.0f)
                target_steer_ = static_cast<float>(-target);
        }
        steer_ = maths::keep_within(target_steer_, steer_,
                                    static_cast<float>(frame_clock::get_slomo_frame_seconds() * kFramesPerSecond));
    }
    // The tilt.
    const float speed = motion_speed(motion_);
    auto acceleration = static_cast<float>(static_cast<double>(speed) - last_speed_);
    last_speed_ = speed;
    if (script::fetch_int_value(kReadGlobalsEveryUpdate)) {
        min_tilt_ = script::fetch_float(kMinTilt);
        max_tilt_ = script::fetch_float(kMaxTilt);
        tilt_rate_ = script::fetch_float(kTiltRate);
        tilt_divisor_ = script::fetch_float(kTiltDivisor);
    }
    const double target = tilt_divisor_ == 0.0f ? 0.0 : -(static_cast<double>(acceleration) / tilt_divisor_);
    target_tilt_ = static_cast<float>(target);
    if (target > max_tilt_)
        target_tilt_ = max_tilt_;
    if (target_tilt_ < min_tilt_)
        target_tilt_ = min_tilt_;
    lean_angle_ =
        maths::keep_within(target_tilt_, lean_angle_,
                           static_cast<float>(frame_clock::get_slomo_frame_seconds() * kFramesPerSecond * tilt_rate_));
}

// 0x0041BF00
void AutomobileDynamicsModule::refresh_bone_rig(void* skeleton, void* model, float tilt, float spin, float steer)
{
    const std::span<const render::HierarchyNode> bones = model_bones(model);
    const std::span<maths::Mat4> matrices = rig_matrix_block(skeleton);
    const float tilt_radians = tilt * kRadiansPerDegree;
    const float steer_radians = steer * kRadiansPerDegree;
    const float spin_radians = spin * kRadiansPerDegree;
    for (int i = 0; i < skeleton_bone_count(skeleton); ++i) {
        const render::HierarchyNode& bone = core::at(bones, i);
        maths::Mat4* matrix = &core::at(matrices, static_cast<int8_t>(bone.bone_slot_));
        *matrix = bone.matrix;
        maths::Mat4 rotation{};
        switch (bone.checksum) {
        case kWheelA:
        case kWheelB:
        case kWheelC:
        case kWheelD:
            matrix_multiply(matrix, &maths::make_pitch_matrix(rotation, spin_radians), matrix);
            break;
        case kSteeringWheelA:
        case kSteeringWheelB:
            matrix_multiply(matrix, &maths::make_roll_matrix(rotation, steer_radians), matrix);
            matrix_multiply(matrix, &maths::make_pitch_matrix(rotation, spin_radians), matrix);
            break;
        case kBody:
            matrix_multiply(matrix, &maths::make_pitch_matrix(rotation, tilt_radians), matrix);
            break;
        default:
            break;
        }
        if (i != 0)
            matrix_multiply(matrix, matrix, &core::at(matrices, static_cast<int16_t>(bone.parent_index_)));
    }
}

// 0x0041C0B0
void AutomobileDynamicsModule::update_bones()
{
    refresh_bone_rig(bone_rig_, mesh_instance_, lean_angle_, wheel_spin_, steer_);
    if (body_shown_)
        return;
    maths::Mat4 collapsed{};
    collapsed.reset_to_identity();
    const maths::Vec4 nothing = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    collapsed.scale_tool(nothing);
    core::at(rig_matrix_block(bone_rig_), 0) = collapsed;
}

// 0x0041C1B0
void AutomobileDynamicsModule::tick_frame()
{
    if (suspend_suspended(suspend_))
        return;
    if (entity_->object_flags() & 1)
        return;
    if (!suspend_should_update(suspend_, nullptr))
        return;
    if (!bone_rig_)
        return;
    update_angles();
    update_bones();
}

// 0x0041B9E0
int AutomobileDynamicsModule::show_body(script::Struct* params, script::Script*)
{
    int show = 0;
    params->fetch_int_value(0u, &show, 0);
    body_shown_ = show != 0;
    return 1;
}

// 0x0041BA20
int AutomobileDynamicsModule::do_nothing(script::Struct*, script::Script*) { return 1; }

} // namespace objects
