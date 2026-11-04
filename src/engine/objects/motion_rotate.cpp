// The motion component (GameObjects::KinematicsModule, motion.h): turning - about its own axes at a speed (Obj_RotX / Y / Z),
// to an orientation over time (Obj_Rotate's interpolation), to look at points, nodes and objects (Obj_LookAt*), and
// to its node's angles.

#include <cstddef>
#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/maths/mat4.h"
#include "engine/maths/rotation_blend.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_internal.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/maths/vec4.h"

namespace objects {

using namespace motion_detail;

namespace {
constexpr std::size_t kTurnBlenderBytes = 0x98;
constexpr int kDefaultTurnSpeed = 360;               // degrees a second
constexpr int kRotateForeverAxes = 7;                // the three axes' flags, as a multiplier of the first
constexpr uint32_t kOBJECTTYPESKATER = 0xBAF9B341; // OBJECT_TYPE_SKATER
constexpr uint32_t kOBJECTTYPEPED = 0xD2DB7F97; // OBJECT_TYPE_PED
constexpr uint32_t kOBJECTTYPEGAMEOBJ = 0xBC0D91D8; // OBJECT_TYPE_GAME_OBJ
constexpr uint32_t kOBJECTTYPECAR = 0x5C0312EB; // OBJECT_TYPE_CAR

constexpr uint32_t kAngle = 0xFF7EBAF6;          // Angle
constexpr uint32_t kDeceleration = 0xF59FF7D7;   // deceleration
constexpr uint32_t kAcceleration = 0x85894A84;   // Acceleration
constexpr uint32_t kTime = 0x906B67BA;           // time
constexpr uint32_t kAngleThreshold = 0xF73A4EFD; // AngleThreshold
constexpr uint32_t kInstant = 0xF16FB852;        // instant
constexpr uint32_t kAxes = 0xA4AECB20;           // (name unknown): which axes a look-at turns about (bits; default y)
constexpr uint32_t kLinkNum = 0xE85997D0;        // LinkNum
constexpr uint32_t kTypeName = 0x7321A8D6;       // Type
constexpr uint32_t kSkater = 0x5B8AB877;         // skater
constexpr uint32_t kPed = 0x061A741E;            // ped
constexpr uint32_t kGameObj = 0x3B5737A6;        // gameobj
constexpr uint32_t kCar = 0x88C21962;            // Car

// Degrees to radians (0x00645464).
constexpr float kDegrees = get_bits(0x3C8EFA35);

// Each axis' value of a per-axis vector.
float& get_axis(maths::Vec4& v, int axis) { return v[axis]; }


// The node after `node` in a manager's object list (null at the end: the head's data is -1).
memory::ListNode* next_object_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next();
    return next->data() == memory::ListNode::head_marker() ? nullptr : next;
}

} // namespace

// --- turning about the axes ---

// 0x00423F10
void KinematicsModule::set_rotation_direction(int axis)
{
    float& speed = get_axis(spin_rates_, axis);
    float& max_speed = get_axis(rot_max_speed_, axis);
    float& acceleration = get_axis(rot_acceleration_, axis);
    float& deceleration = get_axis(rot_deceleration_, axis);
    speed = std::fabs(speed);
    max_speed = std::fabs(max_speed);
    acceleration = std::fabs(acceleration);
    deceleration = std::fabs(deceleration);
    if (get_axis(rot_target_, axis) < 0.0F) {
        speed = -speed;
        max_speed = -max_speed;
        acceleration = -acceleration;
        deceleration = -deceleration;
    }
}

// The body of Obj_RotX / RotY / RotZ (0x00424B90 / 0x00424D50 / 0x00424F50: three copies, the axis chosen before).
void KinematicsModule::rotate_about_axis_by(script::Struct* params, AxisSign axis_sign)
{
    const auto axis = axis_sign.axis;
    const auto sign = axis_sign.sign;
    float& target = get_axis(rot_target_, axis);
    float& speed = get_axis(spin_rates_, axis);
    float& deceleration = get_axis(rot_deceleration_, axis);
    float& acceleration = get_axis(rot_acceleration_, axis);
    const uint32_t bit = 1U << axis;
    const uint32_t forever = kMotion2RotateForeverX << axis;
    deceleration = 0.0F;
    acceleration = 0.0F;
    target = 0.0F;
    if (params->fetch_float(kAngle, &target, 0)) {
        target = static_cast<float>(static_cast<double>(target) * kDegrees * sign);
        motion_flags2_ &= ~forever;
        get_axis(rot_done_, axis) = 0.0F;
        speed = 0.0F;
        if (target == 0.0F) {
            motion_flags_ &= ~bit;
            return;
        }
    }
    float* speed_param = nullptr;
    if (params->fetch_float(kDeceleration, &deceleration, 0)) {
        deceleration = static_cast<float>(static_cast<double>(deceleration) * kDegrees);
        speed_param = &get_axis(rot_max_speed_, axis);
    } else if (params->fetch_float(kAcceleration, &acceleration, 0)) {
        acceleration = static_cast<float>(static_cast<double>(acceleration) * kDegrees);
        float& max_speed = get_axis(rot_max_speed_, axis);
        if (params->fetch_float(kSpeed, &max_speed, 0)) {
            max_speed = static_cast<float>(static_cast<double>(max_speed) * kDegrees);
        }
        if ((motion_flags_ & bit) == 0U) {
            speed = 0.0F;
        }
        speed_param = nullptr;
    } else {
        speed_param = &speed;
    }
    if (speed_param && params->fetch_float(kSpeed, speed_param, 0)) {
        *speed_param = static_cast<float>(static_cast<double>(*speed_param) * kDegrees);
    }
    // (no angle and no deceleration: round and round)
    if (target == 0.0F && deceleration == 0.0F) {
        motion_flags2_ |= forever;
    }
    if ((motion_flags2_ & forever) == 0U) {
        set_rotation_direction(axis);
    }
    motion_flags_ |= bit;
}

// 0x00424B90
bool KinematicsModule::obj_rot_x(script::Struct* params, script::Script* /*script*/)
{
    float sign = 1.0F;
    if ((motion_flags_ & kMotionLookingAt) != 0U) {
        motion_flags_ &= ~(kMotionLookingAt | kMotionRotateAxes);
    }
    if (params->has_bare_flag(kMaxCoords)) {
        sign = -1.0F;
    }
    rotate_about_axis_by(params, {.axis = 0, .sign = sign});
    return true;
}

// 0x00424D50
bool KinematicsModule::obj_rot_y(script::Struct* params, script::Script* /*script*/)
{
    int axis = 1;
    if ((motion_flags_ & kMotionLookingAt) != 0U) {
        motion_flags_ &= ~(kMotionLookingAt | kMotionRotateAxes);
    }
    if (params->has_bare_flag(kMaxCoords)) {
        axis = 2;
    }
    rotate_about_axis_by(params, {.axis = axis, .sign = 1.0F});
    return true;
}

// 0x00424F50
bool KinematicsModule::obj_rot_z(script::Struct* params, script::Script* /*script*/)
{
    int axis = 2;
    if ((motion_flags_ & kMotionLookingAt) != 0U) {
        motion_flags_ &= ~(kMotionLookingAt | kMotionRotateAxes);
    }
    if (params->has_bare_flag(kMaxCoords)) {
        axis = 1;
    }
    rotate_about_axis_by(params, {.axis = axis, .sign = 1.0F});
    return true;
}

// 0x004258B0
void KinematicsModule::rotate_about_axis(AxisTurn axis_turn)
{
    const auto axis = axis_turn.axis;
    const auto angle = axis_turn.angle;
    maths::Mat4 rotation;
    maths::Mat4& matrix = entity()->basis_frame();
    switch (axis) {
    case 0:
        maths::matrix_multiply(&matrix, &maths::make_pitch_matrix(rotation, angle), &matrix);
        matrix.square_up_axes(0);
        break;
    case 1:
        maths::matrix_multiply(&matrix, &maths::make_yaw_matrix(rotation, angle), &matrix);
        matrix.square_up_axes(1);
        break;
    case 2:
        maths::matrix_multiply(&matrix, &maths::make_roll_matrix(rotation, angle), &matrix);
        matrix.square_up_axes(2);
        break;
    default:
        break;
    }
}

// 0x00425A80
bool KinematicsModule::update_axis_rotation(int axis)
{
    float& speed = get_axis(spin_rates_, axis);
    const float& max_speed = get_axis(rot_max_speed_, axis);
    const float& acceleration = get_axis(rot_acceleration_, axis);
    float& deceleration = get_axis(rot_deceleration_, axis);
    bool finished = false;
    if (!(acceleration == 0.0F)) {
        speed = static_cast<float>((static_cast<double>(step_seconds_) * acceleration) + speed);
        // (past the speed to reach, going its way: that speed)
        if (std::fabs(max_speed) < std::fabs(speed) && (speed > 0.0F) == (max_speed > 0.0F)) {
            speed = max_speed;
        }
    }
    if (!(deceleration == 0.0F)) {
        const double slowing = static_cast<double>(step_seconds_) * deceleration;
        if (std::fabs(static_cast<double>(speed) - max_speed) < std::fabs(slowing)) {
            // (down to the speed to reach: done slowing - and, at a standstill, done turning; no turn this frame)
            speed = max_speed;
            deceleration = 0.0F;
            return max_speed == 0.0F;
        }
        speed = static_cast<float>(static_cast<double>(speed) - slowing);
    }
    auto step = static_cast<float>(static_cast<double>(speed) * step_seconds_);
    if ((motion_flags2_ & (kMotion2RotateForeverX << axis)) == 0U) {
        const float target = get_axis(rot_target_, axis);
        float& done = get_axis(rot_done_, axis);
        const bool before = target > done;
        done = static_cast<float>(static_cast<double>(step) + done);
        if (before != (target > done)) {
            // (past the target: only as far as it)
            finished = true;
            step = static_cast<float>(static_cast<double>(step) - (static_cast<double>(done) - target));
        }
    }
    rotate_about_axis({.axis = axis, .angle = step});
    return finished;
}

// 0x00425C70
void KinematicsModule::update_rotations()
{
    for (int axis = 0; axis < 3; ++axis) {
        const uint32_t bit = 1U << axis;
        if (((motion_flags_ & bit) != 0U) && update_axis_rotation(axis)) {
            motion_flags_ &= ~bit;
        }
    }
}

// --- Obj_Rotate's turn ---

// 0x00425520
void KinematicsModule::start_rotation(script::Struct* params, const maths::Mat4* target)
{
    if (!turn_ptr_) {
        if (!memory::manager_ready_flag()) { // (the memory manager not set up yet)
            memory::ensure_manager();
        }
        // (its constructor, 0x005902E0, does nothing)
        turn_ptr_ = static_cast<maths::ArcRotationBlender*>(memory::Hub::get_pull()->carve(kTurnBlenderBytes, 1, nullptr));
    }
    maths::Mat4 end;
    end = *target;
    turn_ptr_->set_matrices(entity()->basis_frame(), end);
    turn_elapsed_ = 0.0F;
    turn_time_ = 0.0F;
    params->fetch_float(kTime, &turn_time_, 0);
    motion_flags_ |= kMotionTurning;
}

// 0x00424440
void KinematicsModule::update_turn()
{
    auto elapsed = static_cast<float>(static_cast<double>(step_seconds_) + turn_elapsed_);
    turn_elapsed_ = elapsed;
    float fraction = 0.0F;
    if (!(turn_time_ == 0.0F) && !(elapsed >= turn_time_)) {
        fraction = static_cast<float>(static_cast<double>(elapsed) / turn_time_);
    } else {
        fraction = 1.0F;
        motion_flags_ &= ~kMotionTurning;
    }
    turn_ptr_->fetch_mat(entity()->basis_frame(), fraction);
}

// --- looking at things ---

// 0x004240E0
bool KinematicsModule::setup_look_axis(const maths::Vec4* target, const maths::Vec4* from, LookAxes look_axes)
{
    const auto reference_axis = look_axes.reference_axis_;
    const auto axis = look_axes.axis;
    const auto threshold = look_axes.threshold;
    const maths::Vec4 offset = {.x = static_cast<float>(static_cast<double>(target->x) - from->x),
                                .y = static_cast<float>(static_cast<double>(target->y) - from->y),
                                .z = static_cast<float>(static_cast<double>(target->z) - from->z),
                                .w = static_cast<float>(static_cast<double>(target->w) - from->w)};
    get_axis(rot_done_, axis) = 0.0F;
    const float angle = maths::get_angle_in_plane(entity()->basis_frame(), offset, reference_axis, axis);
    get_axis(rot_target_, axis) = angle;
    if (std::fabs(angle) > threshold) {
        motion_flags_ |= 1U << axis;
        return true;
    }
    return false;
}

// 0x004252E0
bool KinematicsModule::get_look_at(script::Struct* params, const maths::Vec4* target)
{
    float threshold = 0.0F;
    params->fetch_float(kAngleThreshold, &threshold, 0);
    threshold = static_cast<float>(static_cast<double>(threshold) * kDegrees);
    if (params->has_bare_flag(kInstant)) {
        const maths::Vec4& pos = entity()->at_point();
        const maths::Vec4 offset = {.x = static_cast<float>(static_cast<double>(target->x) - pos.x),
                                    .y = static_cast<float>(static_cast<double>(target->y) - pos.y),
                                    .z = static_cast<float>(static_cast<double>(target->z) - pos.z),
                                    .w = static_cast<float>(static_cast<double>(target->w) - pos.w)};
        const float angle = maths::get_angle_in_plane(entity()->basis_frame(), offset, 2, 1);
        if (!(std::fabs(angle) > threshold)) {
            return false;
        }
        maths::Mat4 rotation;
        maths::Mat4& matrix = entity()->basis_frame();
        maths::matrix_multiply(&matrix, &maths::make_yaw_matrix(rotation, angle), &matrix);
        return true;
    }
    int speed = kDefaultTurnSpeed;
    int acceleration = 0;
    int axes = 2; // (y)
    float time = 0.0F;
    if ((motion_flags_ & kMotionLookingAt) == 0U) {
        motion_flags_ = (motion_flags_ & ~kMotionRotateAxes) | kMotionLookingAt;
        motion_flags2_ &= ~(kMotion2RotateForeverX * kRotateForeverAxes);
    }
    params->fetch_float(kTime, &time, 0);
    if (time == 0.0F) {
        params->fetch_int_value(kSpeed, &speed, 0);
        params->fetch_int_value(kAcceleration, &acceleration, 0);
        params->fetch_int_value(kAxes, &axes, 0);
    }
    bool turning = false;
    for (int axis = 0; axis < 3; ++axis) {
        if ((axes & (1 << axis)) == 0) {
            continue;
        }
        if (!setup_look_axis(
                target, &entity()->at_point(),
                {.reference_axis_ = static_cast<int>(axis != 2) + 1, .axis = axis, .threshold = threshold})) {
            continue;
        }
        if (!(time == 0.0F)) {
            get_axis(rot_acceleration_, axis) = 0.0F;
            get_axis(spin_rates_, axis) = static_cast<float>(static_cast<double>(get_axis(rot_target_, axis)) / time);
        } else {
            const double rate = static_cast<double>(acceleration) * kDegrees;
            get_axis(rot_acceleration_, axis) = static_cast<float>(rate);
            if (rate == 0.0) {
                get_axis(spin_rates_, axis) = static_cast<float>(static_cast<double>(speed) * kDegrees);
            } else {
                get_axis(spin_rates_, axis) = 0.0F;
                get_axis(rot_max_speed_, axis) = 0.0F;
            }
            set_rotation_direction(axis);
        }
        turning = true;
    }
    return turning;
}

// 0x00425710
void KinematicsModule::look_at_node_linked(script::Struct* params, script::Script* /*script*/)
{
    const int links = static_cast<int>(level_nodes::get_link_count(list_link_));
    if (links == 0) {
        return;
    }
    int link = 1;
    params->fetch_int_value(kLinkNum, &link, 0);
    link -= 1;
    if (link < 0 || link >= links) {
        link = 0;
    }
    maths::Vec4 pos;
    level_nodes::fetch_place(level_nodes::fetch_neighbour_node(list_link_, link), &pos);
    get_look_at(params, &pos);
}

// 0x004257A0
void KinematicsModule::look_at_node(script::Struct* params, script::Script* /*script*/)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 0);
    if (name == 0U) {
        return;
    }
    maths::Vec4 pos;
    level_nodes::fetch_place(level_nodes::fetch_vertex(name, true), &pos);
    get_look_at(params, &pos);
}

// 0x00425800
void KinematicsModule::look_at_position(script::Struct* params, bool absolute)
{
    maths::Vec4 pos = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    params->fetch_vec(0U, &pos, 0);
    if (params->has_bare_flag(kMaxCoords)) {
        pos.negate_x_rotate_x90();
    }
    if (!absolute) {
        const maths::Vec4& at = entity()->at_point();
        pos = {.x = pos.x + at.x, .y = pos.y + at.y, .z = pos.z + at.z, .w = pos.w + at.w};
    }
    get_look_at(params, &pos);
}

// 0x00427500
bool KinematicsModule::look_at_object(script::Struct* params, script::Script* /*script*/)
{
    uint32_t type = 0;
    if (params->read_hash_value(kTypeName, &type, 0)) {
        uint32_t object_type = 0;
        switch (type) {
        case kSkater:
            object_type = kOBJECTTYPESKATER; // OBJECT_TYPE_SKATER
            break;
        case kPed:
            object_type = kOBJECTTYPEPED; // OBJECT_TYPE_PED
            break;
        case kGameObj:
            object_type = kOBJECTTYPEGAMEOBJ; // OBJECT_TYPE_GAME_OBJ
            break;
        case kCar:
            object_type = kOBJECTTYPECAR; // OBJECT_TYPE_CAR
            break;
        default:
            return false;
        }
        const GameEntity* other = entity()->find_nearest_object_of_type(object_type);
        if (!other) {
            return false;
        }
        return get_look_at(params, &other->at_point());
    }
    uint32_t name = 0;
    if (!params->read_hash_value(kName, &name, 0)) {
        return false;
    }
    // (the object of that id in its manager's list, not itself)
    memory::ListNode* list = entity()->park_owner()->fetch_object_set();
    memory::ListNode* node = next_object_node(list);
    auto* object = node ? static_cast<Entity*>(node->data()) : nullptr;
    while (object) {
        Entity* current = object;
        node = next_object_node(node);
        object = node ? static_cast<Entity*>(node->data()) : nullptr;
        if (current != entity() && current->ident() == name) {
            return get_look_at(params, &core::down_cast<GameEntity>(current)->at_point());
        }
    }
    return false;
}

// 0x00425970
void KinematicsModule::face_node(script::Struct* node)
{
    maths::Mat4& matrix = entity()->basis_frame();
    core::at(matrix.row, 0) = {.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    core::at(matrix.row, 2) = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    core::at(matrix.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    maths::Vec4 angles;
    level_nodes::read_turn_angles(node, &angles);
    if (!(angles.x == 0.0F) || !(angles.y == 0.0F) || !(angles.z == 0.0F)) {
        maths::Mat4 rotation;
        maths::matrix_multiply(&matrix, &matrix, &maths::make_pitch_matrix(rotation, angles.x));
        maths::matrix_multiply(&matrix, &matrix, &maths::make_yaw_matrix(rotation, angles.y));
        maths::matrix_multiply(&matrix, &matrix, &maths::make_roll_matrix(rotation, angles.z));
    }
    entity()->display_matrix() = matrix;
}

// --- script member functions ---

// 0x00426770
bool KinematicsModule::obj_rotate(script::Struct* params, script::Script* /*script*/)
{
    rotate_to(params);
    return true;
}

// 0x00426790
bool KinematicsModule::obj_look_at_node(script::Struct* params, script::Script* script)
{
    look_at_node(params, script);
    return true;
}

// 0x004267B0
bool KinematicsModule::obj_look_at_node_linked(script::Struct* params, script::Script* script)
{
    look_at_node_linked(params, script);
    return true;
}

// 0x004267D0
bool KinematicsModule::obj_look_at_node_stored(script::Struct* params, script::Script* /*script*/)
{
    maths::Vec4 pos;
    level_nodes::fetch_place(saved_node_id_, &pos);
    get_look_at(params, &pos);
    return true;
}

// 0x00426810 (look_at_position's absolute case again, as its own code)
bool KinematicsModule::obj_look_at_pos(script::Struct* params, script::Script* /*script*/)
{
    maths::Vec4 pos = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    params->fetch_vec(0U, &pos, 0);
    if (params->has_bare_flag(kMaxCoords)) {
        pos.negate_x_rotate_x90();
    }
    get_look_at(params, &pos);
    return true;
}

// 0x00426880
bool KinematicsModule::obj_look_at_pos_stored(script::Struct* params, script::Script* /*script*/)
{
    get_look_at(params, &stored_pos_);
    return true;
}

// 0x004268A0
bool KinematicsModule::obj_look_at_rel_pos(script::Struct* params, script::Script* /*script*/)
{
    look_at_position(params, false);
    return true;
}

// 0x004276A0
bool KinematicsModule::obj_look_at_object(script::Struct* params, script::Script* script)
{
    return look_at_object(params, script);
}

} // namespace objects

