// The lockobj component (GameObjects::PinnedObjectModule): Obj_LockToObject's parameters and the update that keeps an object
// locked to another - with THAW's no_rotate added (lock_obj.h).

#include "engine/objects/lock_obj.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/maths/mat4.h"
#include "engine/maths/random.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "frontend/screen_element_cfuncs.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {
float g_lock_obj_wobble_step{0.006135923f};

constexpr uint32_t kObjLockToObject = 0x785E57C7; // Obj_LockToObject
constexpr uint32_t kOff = 0xD443A2BC;             // off
constexpr uint32_t kObjectName = 0x2BFC2486;      // ObjectName
constexpr uint32_t kBoneOffset = 0xBE9AB709;      // BoneOffset
constexpr uint32_t kId = 0x40C698AF;              // id
constexpr uint32_t kBone = 0xCAB94088;            // bone
constexpr uint32_t kLagRange = 0x700C4085;        // LagRange
constexpr uint32_t kLagWobble = 0xD25807F0;       // lagwobble
constexpr uint32_t kAngles = 0x9D2D0915;          // Angles
// NOT ORIGINAL: THAW's flag (THAW's names: 0xD74A6981)
constexpr uint32_t kKeepOrientation = 0xD74A6981; // no_rotate

// The wobble's step: 2 pi / 1024 as the float the original multiplies by (0x00646118; the update's own, 0x0064611C,
// is one bit larger).
float wobble_step() { return g_lock_obj_wobble_step; }

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The rotation of `angles` (radians) as a matrix (0x0041A730: the identity times RotationX, RotationY, RotationZ).
void angles_to_matrix(maths::Mat4* matrix, const maths::Vec4* angles) { matrix->init_from_angles(*angles); }

} // namespace

// 0x0041F520
void PinnedObjectModule::configure_from_data(script::Struct* params)
{
    entity_label_ = 0;
    bone_ = 0;
    target_id_ = frontend::g_unassigned_id;
    shift_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    // (NOT ORIGINAL: kLockFlagNoRotate cleared with the original's kLockFlagAngles)
    lock_flags_ &= ~(kLockFlagAngles | kLockFlagNoRotate);
    if (params->has_bare_flag(kOff)) {
        lock_flags_ &= ~kLockFlagLocked;
        return;
    }
    lock_flags_ |= kLockFlagLocked;
    params->read_hash_value(kObjectName, &entity_label_, 0);
    auto* offset = &shift_;
    if (!params->fetch_vec(0u, offset, 0))
        params->fetch_vec(kBoneOffset, offset, 0);
    params->read_hash_value(kId, &target_id_, 0);
    params->read_hash_value(kBone, &bone_, 0);
    // (updated after the target: its priority less one)
    EntityHub* manager = object_hub_slot().get();
    const Entity* target = manager->look_up_entity(target_id_);
    if (!target)
        target = manager->look_up_entity(entity_label_);
    if (target) {
        const int priority = get_truncated(static_cast<double>(target->list_link_.priority) - 1.0);
        manager->set_entity_rank(static_cast<Entity*>(entity_), priority);
    }
    lock_flags_ &= ~kLockFlagLag;
    lag_min_ = 1.0f;
    lag_max_ = 1.0f;
    if (params->fetch_pair(kLagRange, &lag_min_, &lag_max_, 0))
        lock_flags_ |= kLockFlagLag;
    core::at(wobble_, 0) = 0.0f;
    core::at(wobble_, 1) = 0.0f;
    if (params->fetch_pair(kLagWobble, &core::at(wobble_, 0), &core::at(wobble_, 1), 0))
        lock_flags_ |= kLockFlagLag;
    wobble_seed_ = maths::rnd(0x400);
    // (the masked time is never negative: the original's 2^32 correction for a negative one never applies)
    const int t = static_cast<int>((frame_clock::get_time() + static_cast<uint32_t>(wobble_seed_)) & 0x3FF);
    auto time = static_cast<double>(t);
    auto step = static_cast<double>(wobble_step());
    const double wobble = std::sin(time * static_cast<double>(core::at(wobble_, 1)) * step) *
                          std::sin(time * static_cast<double>(core::at(wobble_, 0)) * step);
    lag_ = static_cast<float>((wobble + 1.0) * (static_cast<double>(lag_max_) - static_cast<double>(lag_min_)) * 0.5 +
                              static_cast<double>(lag_min_));
    if (params->has_entry_named(kAngles))
        lock_flags_ |= kLockFlagAngles;
    else
        lock_flags_ &= ~kLockFlagAngles;
    if (lock_flags_ & kLockFlagAngles) {
        maths::Vec4 angles;
        level_nodes::read_turn_angles(params, &angles);
        angles_to_matrix(&orientation_turn_, &angles);
    }
    // NOT ORIGINAL: THAW's no_rotate (kLockFlagNoRotate)
    if (params->has_bare_flag(kKeepOrientation))
        lock_flags_ |= kLockFlagNoRotate;
}

// 0x0041FF90
void PinnedObjectModule::tick_frame()
{
    if (!(lock_flags_ & kLockFlagLocked))
        return;
    if (!(lock_flags_ & kLockFlagNoRotate)) {
        update_lock();
        return;
    }
    // NOT ORIGINAL: THAW's no_rotate - placed by the lock (the offset turned by the target, as THAW's ferris cars go
    // round with the wheel), its own orientation kept.
    const maths::Mat4 own = entity_->basis_frame_;
    update_lock();
    entity_->basis_frame_ = own;
    entity_->display_matrix_ = own;
}

// 0x0041F410
int PinnedObjectModule::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    if (name != kObjLockToObject)
        return 2;
    configure_from_data(params);
    return 1;
}

} // namespace objects
