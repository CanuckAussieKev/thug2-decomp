// An object's bones turned to look at something (see bone_look_at.h). From the disassembly of 0x00487D40-0x00488DA1.

#include "engine/gfx/bone_look_at.h"

#include <cmath>
#include <cstdint>
#include <array>

#include "engine/core/mem.h"
#include "engine/gfx/anim_blend.h"
#include "engine/gfx/anim_controller.h"
#include "engine/gfx/rig.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/param_struct.h"
#include "platform/code_address.h"
#include "engine/gfx/model_utils.h"
#include "engine/core/stubs.h"
#include "engine/objects/anim_movies.h"
#include "engine/objects/skeleton_component.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace gfx {

namespace {

using script::Struct;

constexpr uint32_t kNoBone = 0xFFFFFFFF;
constexpr uint32_t kSkeletonModuleName = 0x222756D5;
// Member functions (names unknown)
constexpr uint32_t kOn = 0x20D24E9A;
constexpr uint32_t kIsOn = 0x027B2B1C;
constexpr uint32_t kOff = 0x4599FB80;
constexpr uint32_t kSettings = 0xB040E943;
// Parameters
constexpr uint32_t kIndex = 0x7F8C98FE;        // index
constexpr uint32_t kId = 0x40C698AF;           // id
constexpr uint32_t kAll = 0xC4E78E22;          // all
constexpr uint32_t kOnFlag = 0x0C5F37CD;       // (name unknown) made active
constexpr uint32_t kOffFlag = 0xB0BAB2AE;      // (name unknown) made inactive
constexpr uint32_t kBone = 0xCAB94088;         // bone
constexpr uint32_t kTargetObject = 0x4ACEE3F7; // (name unknown) the object looked at
constexpr uint32_t kAimPointHash = 0xDC741BAD; // (name unknown) a point looked at
constexpr uint32_t kOffset = 0x1F07CF31;       // (name unknown) the offset from the object
constexpr uint32_t kAxis = 0x7AF07905;         // Axis
constexpr uint32_t kWeight = 0xF832AABE;       // weight
constexpr uint32_t kMinAngles = 0x0B405328;    // min_angles
constexpr uint32_t kMaxAngles = 0x0A4CC99C;    // max_angles
constexpr uint32_t kNoClamp = 0xD4A8CD5F;      // (name unknown) each angle past its limits zeroed instead

constexpr float kQuarterTurn = 1.57079637f; // 0x00645484
constexpr float kEaseBelow = 0.923879027f;  // 0x006488F8: a turn this close to the last one isn't eased
constexpr float kEaseFactor = 0.216478556f; // 0x006488F4

float epsilon() { return objects::g_float_zero; }

// x, y and z of a vector scaled to length 1 when longer than the epsilon (from the sum (x x + z z) + y y, the
// reciprocal unrounded).
void normalize_xzy(float& x, float& y, float& z)
{
    const double length =
        std::sqrt((static_cast<double>(x) * x + static_cast<double>(z) * z) + static_cast<double>(y) * y);
    if (length > epsilon()) {
        const double inverse = 1.0 / length;
        x = static_cast<float>(x * inverse);
        y = static_cast<float>(y * inverse);
        z = static_cast<float>(z * inverse);
    }
}

// The bone's index from the parameters: index=n, else id=<bone> (its index), else bone 0 - or all.
int bone_from_params(BoneLookAt* look_at, Struct* params, bool* all, bool* by_id)
{
    int index = 0;
    *all = false;
    *by_id = false;
    if (params->fetch_int_value(kIndex, &index, 0))
        return index;
    index = 0;
    uint32_t id = 0;
    if (params->read_hash_value(kId, &id, 0)) {
        *by_id = true;
        return look_at->find_bone(id);
    }
    if (params->has_entry_named(kAll))
        *all = true;
    return 0;
}

} // namespace

// 0x00487E00
maths::Vec4* ClampVector::confine(const maths::Vec4* min, const maths::Vec4* max)
{
    float* values = &x;
    const float* low = &min->x;
    const float* high = &max->x;
    for (int i = 0; i < 4; ++i) {
        const float value = values[i] > high[i] ? high[i] : values[i];
        values[i] = value < low[i] ? low[i] : value;
    }
    return this;
}

// 0x00487EE0
maths::Vec4* clamped_vector(maths::Vec4* out, const maths::Vec4* v, const maths::Vec4* min, const maths::Vec4* max)
{
    ClampVector clamped;
    static_cast<maths::Vec4&>(clamped) = *v;
    clamped.confine(min, max);
    *out = clamped;
    return out;
}

// 0x00487F40 (each sine and cosine rounded to a float where the original stores it; sin(y) kept at the x87's
// precision into its two products)
maths::QuatRotation* angles_to_quat(maths::QuatRotation* out, const maths::Vec4* angles)
{
    const double half_z = angles->z * 0.5;
    const double half_y = angles->y * 0.5;
    const double half_x = angles->x * 0.5;
    auto cos_z = static_cast<float>(std::cos(half_z));
    auto cos_y = static_cast<float>(std::cos(half_y));
    auto cos_x = static_cast<float>(std::cos(half_x));
    const float sin_z = std::sin(static_cast<float>(half_z));
    const float sin_x = std::sin(static_cast<float>(half_x));
    auto cycz = static_cast<float>(static_cast<double>(cos_y) * cos_z);
    auto szsy = static_cast<float>(std::sin(half_y) * sin_z);
    auto sycz = static_cast<float>(std::sin(half_y) * cos_z);
    auto szcy = static_cast<float>(static_cast<double>(sin_z) * cos_y);
    const double w = static_cast<double>(szsy) * sin_x + static_cast<double>(cycz) * cos_x;
    const double x = static_cast<double>(cycz) * sin_x - static_cast<double>(szsy) * cos_x;
    const double y = static_cast<double>(szcy) * sin_x + static_cast<double>(sycz) * cos_x;
    const double z = static_cast<double>(szcy) * cos_x - static_cast<double>(sycz) * sin_x;
    out->x = static_cast<float>(x);
    out->y = static_cast<float>(y);
    out->z = static_cast<float>(z);
    out->w = static_cast<float>(w);
    return out;
}

// 0x00488020
maths::Vec4* quat_to_euler(maths::Vec4* out, const maths::QuatRotation* q)
{
    const double asin_argument = -((static_cast<double>(q->x) * q->z - static_cast<double>(q->w) * q->y) * 2.0);
    const double y = std::asin(asin_argument);
    const float ww = q->w * q->w;
    const float xx = q->x * q->x;
    const float yy = q->y * q->y;
    const float zz = q->z * q->z;
    const double z_num = (static_cast<double>(q->w) * q->z + static_cast<double>(q->y) * q->x) * 2.0;
    const double z_den = ((static_cast<double>(xx) + ww) - yy) - zz;
    auto z = static_cast<float>(std::atan2(z_num, z_den));
    out->w = 1.0f;
    const double x_num = (static_cast<double>(q->w) * q->x + static_cast<double>(q->y) * q->z) * 2.0;
    const double x_den = ((static_cast<double>(ww) - xx) - yy) + zz;
    out->x = static_cast<float>(std::atan2(x_num, x_den));
    out->y = static_cast<float>(y);
    out->z = z;
    return out;
}

// --- the bones' look-at ------------------------------------------------------------------------------------------

// 0x00488300
BoneLookAt::BoneLookAt(AnimTimeline* owner, void* object) : AnimDriver(owner)
{
    owner_object_ = owner ? fetch_subject() : static_cast<objects::GameEntity*>(object);
    for (Bone& bone : bones_) {
        bone.active_ = false;
        bone.clamp_ = true;
        bone.bone_ = kNoBone;
        bone.lower_limit_ = {0.0f, 0.0f, 0.0f, 1.0f};
        bone.upper_limit_ = {0.0f, 0.0f, 0.0f, 1.0f};
        bone.look_goal_ = {0.0f, 0.0f, 0.0f, 1.0f};
        bone.weight_ = 1.0f;
        bone.fixed_pos_ = true;
        bone.aim_point_ = 0;
        bone.pivot_dir_ = 0;
        bone.orientation_turn_ = {0.0f, 0.0f, 0.0f, 1.0f};
    }
}

// 0x00488100
void BoneLookAt::tick_frame()
{
    for (Bone& bone : bones_) {
        if (bone.fixed_pos_)
            continue;
        auto* object =
            core::down_cast<objects::GameEntity>(objects::object_hub_slot()->look_up_entity(bone.aim_point_));
        if (!object)
            continue;
        bone.look_goal_.z = bone.shift_.z + object->at_point_.z;
        bone.look_goal_.w = bone.shift_.w + object->at_point_.w;
        bone.look_goal_.x = bone.shift_.x + object->at_point_.x;
        bone.look_goal_.y = bone.shift_.y + object->at_point_.y;
    }
}

// 0x00488410
bool BoneLookAt::get_apply(maths::QuatRotation* rotations)
{
    tick_frame();
    objects::GameEntity* object = owner_object_;
    objects::ObjectModule* component = object->fetch_module(kSkeletonModuleName);
    auto* skeleton =
        component ? (core::down_cast<objects::BoneRigModule>(component)->bone_rig_) : nullptr;
    for (Bone& bone : bones_) {
        if (bone.bone_ == kNoBone)
            continue;
        const int index = skeleton->fetch_bone_slot(bone.bone_);
        if (index == -1)
            continue;
        const maths::Vec4& pos = object->at_point_;
        const maths::Vec4& at = core::at(object->basis_frame_.row, 2);
        // The direction to the target, and the turn from the object's own
        float dx = bone.look_goal_.x - pos.x;
        float dy = bone.look_goal_.y - pos.y;
        float dz = bone.look_goal_.z - pos.z;
        normalize_xzy(dx, dy, dz);
        maths::QuatRotation& pose = rotations[index];
        const double dot =
            (static_cast<double>(dx) * at.x + static_cast<double>(dz) * at.z) + static_cast<double>(dy) * at.y;
        auto angle = static_cast<float>(std::acos(dot));
        maths::Vec4 axis;
        axis.x = static_cast<float>(static_cast<double>(dy) * at.z - static_cast<double>(dz) * at.y);
        axis.y = static_cast<float>(static_cast<double>(dz) * at.x - static_cast<double>(at.z) * dx);
        axis.z = static_cast<float>(static_cast<double>(at.y) * dx - static_cast<double>(dy) * at.x);
        axis.w = 0.0f;
        normalize_xzy(axis.x, axis.y, axis.z);
        // The axis turned to the bone's own way of looking
        maths::QuatRotation turn = {0.0f, 0.0f, 0.0f, 1.0f};
        const int axis_code = bone.pivot_dir_;
        auto which = static_cast<unsigned int>(axis_code < 0 ? -axis_code : axis_code);
        if (which <= 3) {
            if (which <= 2) {
                const volatile int divisor = axis_code; // (0 divides by zero: the original's)
                int sign = axis_code / divisor;
                if (axis_code < 0)
                    sign = -sign;
                const double quarter = sign * static_cast<double>(kQuarterTurn);
                const double s = std::sin(quarter);
                const double c = std::cos(quarter);
                if (which <= 1) {
                    const double old_y = axis.y;
                    axis.y = static_cast<float>(old_y * c - axis.z * s);
                    axis.z = static_cast<float>(old_y * s + axis.z * c);
                } else {
                    auto sin_f = static_cast<float>(s);
                    const double old_x = axis.x;
                    axis.x = static_cast<float>(c * old_x - static_cast<double>(axis.y) * sin_f);
                    axis.y = static_cast<float>(sin_f * old_x + axis.y * c);
                }
            }
            maths::QuatRotation made{};
            turn = *made.init_from_axis_angle(axis, angle);
        }
        // Its angles: clamped and scaled, or each one past its limits dropped
        maths::Vec4 angles;
        quat_to_euler(&angles, &turn);
        if (bone.clamp_) {
            maths::Vec4 clamped;
            clamped_vector(&clamped, &angles, &bone.lower_limit_, &bone.upper_limit_);
            angles.x = bone.weight_ * clamped.x;
            angles.y = bone.weight_ * clamped.y;
            angles.z = bone.weight_ * clamped.z;
            angles.w = bone.weight_ * clamped.w;
        } else {
            if (angles.x < bone.lower_limit_.x)
                angles.x = 0.0f;
            if (angles.y < bone.lower_limit_.y)
                angles.y = 0.0f;
            if (angles.z < bone.lower_limit_.z)
                angles.z = 0.0f;
            if (angles.x > bone.upper_limit_.x)
                angles.x = 0.0f;
            if (angles.y > bone.upper_limit_.y)
                angles.y = 0.0f;
            if (angles.z > bone.upper_limit_.z)
                angles.z = 0.0f;
        }
        // Applied to the pose's rotation, eased from the last turn when it's far from it
        maths::QuatRotation rotation{};
        angles_to_quat(&rotation, &angles);
        rotation *= pose;
        pose = rotation;
        pose.make_unit(1.0f);
        maths::QuatRotation& last = bone.orientation_turn_;
        const double closeness = ((static_cast<double>(pose.z) * last.z + static_cast<double>(last.x) * pose.x) +
                                  static_cast<double>(pose.w) * last.w) +
                                 static_cast<double>(pose.y) * last.y;
        if (closeness < kEaseBelow) {
            auto t = static_cast<float>(std::fabs(closeness) * kEaseFactor);
            std::array<float, 4> eased{};
            const float* result = interpolate_rotation(eased.data(), &last.x, &pose.x, t);
            pose = {result[0], result[1], result[2], result[3]};
        }
        last = pose;
    }
    return true;
}

// 0x00488950
void BoneLookAt::configure(bool all, int index, Struct* params)
{
    if (params->has_entry_named(kOnFlag)) {
        if (all)
            activate_all();
        else
            core::at(bones_, index).active_ = true;
    } else if (params->has_entry_named(kOffFlag)) {
        if (all)
            deactivate_all();
        else
            core::at(bones_, index).active_ = false;
    }
    Bone& bone = core::at(bones_, index);
    params->read_hash_value(kBone, &bone.bone_, 0);
    if (params->read_hash_value(kTargetObject, &bone.aim_point_, 0)) {
        bone.fixed_pos_ = false;
        bone.shift_ = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (params->fetch_vec(kAimPointHash, &bone.look_goal_, 0))
        bone.fixed_pos_ = true;
    if (params->fetch_vec(kOffset, &bone.shift_, 0))
        bone.fixed_pos_ = false;
    params->fetch_int_value(kAxis, &bone.pivot_dir_, 0);
    params->fetch_float(kWeight, &bone.weight_, 0);
    maths::Vec4 limit;
    if (params->fetch_vec(kMinAngles, &limit, 0)) {
        limit.degree_to_radian_conv();
        if (all)
            set_all_min(&limit);
        else
            bone.lower_limit_ = limit;
    }
    if (params->fetch_vec(kMaxAngles, &limit, 0)) {
        limit.degree_to_radian_conv();
        if (all)
            set_all_max(&limit);
        else
            bone.upper_limit_ = limit;
    }
    bone.clamp_ = !params->has_bare_flag(kNoClamp);
}

// 0x00488B20
void BoneLookAt::configure_from_data(Struct* params)
{
    AnimDriver::configure_from_data(params); // (the base's: its id)
    configure(false, 0, params);
}

// 0x00488B40
int BoneLookAt::invoke_method_by_name(uint32_t name, Struct* params, script::Script*)
{
    if (name != kOn && name != kIsOn && name != kOff && name != kSettings)
        return 2;
    bool all = false;
    bool by_id = false;
    const int index = bone_from_params(this, params, &all, &by_id);
    if (name == kOn) {
        if (all)
            activate_all();
        else
            core::at(bones_, index).active_ = true;
        return 1;
    }
    if (name == kOff) {
        if (all)
            deactivate_all();
        else
            core::at(bones_, index).active_ = false;
        return 1;
    }
    if (name == kIsOn)
        return all ? (all_active() ? 1 : 0) : (core::at(bones_, index).active_ ? 1 : 0);
    configure(all, index, params);
    return 1;
}

// 0x00487D60
int BoneLookAt::find_bone(uint32_t bone)
{
    for (int i = 0; i < 4; ++i) {
        if (core::at(bones_, i).bone_ == bone)
            return i;
    }
    return -1;
}

// 0x00487D90
bool BoneLookAt::all_active()
{
    for (const Bone& bone : bones_) {
        if (bone.bone_ != kNoBone && !bone.active_)
            return false;
    }
    return true;
}

// 0x00487DC0
void BoneLookAt::activate_all()
{
    for (Bone& bone : bones_) {
        if (bone.bone_ != kNoBone)
            bone.active_ = true;
    }
}

// 0x00487DE0
void BoneLookAt::deactivate_all()
{
    for (Bone& bone : bones_) {
        if (bone.bone_ != kNoBone)
            bone.active_ = false;
    }
}

// 0x00488170
void BoneLookAt::set_all_min(const maths::Vec4* min)
{
    for (Bone& bone : bones_) {
        if (bone.bone_ != kNoBone)
            bone.lower_limit_ = *min;
    }
}

// 0x00488220
void BoneLookAt::set_all_max(const maths::Vec4* max)
{
    for (Bone& bone : bones_) {
        if (bone.bone_ != kNoBone)
            bone.upper_limit_ = *max;
    }
}

} // namespace gfx
