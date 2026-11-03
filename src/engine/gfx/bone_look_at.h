#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/gfx/anim_controller.h"

namespace script {
class Struct;
class Script;
} // namespace script

// An object's bones turned to look at something (gfx::BoneLookAt, 0x1A4 bytes, vtable 0x006488D8 - its base is the
// pose modifier at 0x0064877C, 0x14 bytes: +4 its owner, +8 the object, +0x10 its id; the base's code is at
// 0x00481710-0x00481770): up to four bones, each turned each frame towards a target - an object (followed, with an
// offset) or a fixed point - within angle limits, eased from the last frame's turn. From the disassembly of
// 0x00487D40-0x00488DA1 (with 0x00487D60 / 0x00487D90 / 0x00487DC0, code Ghidra never made functions of).
namespace gfx {

// Euler angles <-> quaternions, the angles (x, y, z) in radians.
// 0x00487F40 (cdecl): the rotation of these angles. Returns out.
maths::QuatRotation* angles_to_quat(maths::QuatRotation* out, const maths::Vec4* angles);
// 0x00488020 (cdecl): the angles of a rotation (w 1). Returns out.
maths::Vec4* quat_to_euler(maths::Vec4* out, const maths::QuatRotation* rotation);
// 0x00487EE0 (cdecl): `v` clamped (ClampVector::Confine) into `out`. Returns out.
maths::Vec4* clamped_vector(maths::Vec4* out, const maths::Vec4* v, const maths::Vec4* min, const maths::Vec4* max);
// A vector clamped.
struct ClampVector : maths::Vec4 {
    // 0x00487E00: each of the four components kept to min..max (above max: max; then below min: min - a NaN stays).
    // Returns this.
    maths::Vec4* confine(const maths::Vec4* min, const maths::Vec4* max);
};

constexpr std::size_t kBoneBytes = 0x64;
constexpr std::size_t kBoneLookAtBonesOffset = 0x14;
constexpr std::size_t kBoneLookAtBytes = 0x1A4;

class BoneLookAt final : public AnimDriver {
public:
    // One bone's look-at (0x64 bytes).
    struct Bone {
        uint32_t bone_;           // +0x00 its name (-1: none)
        uint32_t aim_point_;      // +0x04 the object looked at (its id)
        float weight_;            // +0x08 the clamped angles' scale (1)
        int pivot_dir_;           // +0x0C which way the bone looks: +-1 / +-2 x / z turned a quarter, +-3 as it is
        maths::Vec4 lower_limit_; // +0x10 the angle limits (radians; w 1)
        maths::Vec4 upper_limit_; // +0x20
        maths::Vec4 look_goal_;   // +0x30 the point looked at
        maths::Vec4 shift_;       // +0x40 from the target object's position (w 1)
        maths::QuatRotation orientation_turn_; // +0x50 the turn last applied
        bool active_;                          // +0x60
        bool fixed_pos_;                       // +0x61: look_goal_ given (the target object isn't followed)
        bool clamp_; // +0x62: the angles clamped and scaled (else each one past its limits zeroed)
        uint8_t pad63;
    };
    static_assert(sizeof(Bone) == kBoneBytes, "layout must match the original");

    // 0x00488300: the base's constructor (0x00481710 with `owner`), no bones, the object: the owner's (0x00481760)
    // or `object` without an owner.
    BoneLookAt(AnimTimeline* owner, void* object);
    // 0x00487D40 / 0x004880D0: nothing of its own. (Its table's slot 6, a jump to slot 4, is never called.)
    ~BoneLookAt() override = default;
    // 0x00488410 (vtable slot 1): updated (slot 5), then each active bone with a name the skeleton has turned towards
    // its target in `rotations` (the pose's quaternions, one a bone) - and the turn kept. True.
    bool get_apply(std::span<maths::QuatRotation> rotations);
    bool fetch_pose(Stance* pose) override { return get_apply(pose->rotations); }
    // 0x00488B40 (vtable slot 3): the script member functions 0x20D24E9A (on), 0x027B2B1C (whether on), 0x4599FB80
    // (off) and 0xB040E943 (settings) on the bone index=n, or id=<bone>, or all - else bone 0. 2: not one of these.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x00488B20 (vtable slot 4): the base's settings (its id: 0x00481740), then bone 0's (Configure).
    void configure_from_data(script::Struct* params) override;
    // 0x00488100 (vtable slot 5): each bone following an object (not fixed_pos_) gets its target position: the
    // object's position plus the offset (none: left as it was).
    void tick_frame() override;
    // 0x00488950: settings of bone `index` (with `all`: on / off and the angle limits for every bone with a name).
    void configure(bool all, int index, script::Struct* params);
    // 0x00487D60: the index of the bone named `bone` (-1: none).
    int find_bone(uint32_t bone);
    // 0x00487D90: whether every bone with a name is active.
    bool all_active();
    // 0x00487DC0 / 0x00487DE0: every bone with a name made active / inactive.
    void activate_all();
    void deactivate_all();
    // 0x00488170 / 0x00488220: every bone with a name given these limits.
    void set_all_min(const maths::Vec4* min);
    void set_all_max(const maths::Vec4* max);

private:
    // (the base's timeline +0x04, object +0x08 - a composite object -, priority +0x0C and id +0x10)
    std::array<Bone, 4> bones_{}; // +0x14
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(BoneLookAt, bones_) == kBoneLookAtBonesOffset && sizeof(BoneLookAt) == kBoneLookAtBytes, "the original's layout");
    }
};

} // namespace gfx
