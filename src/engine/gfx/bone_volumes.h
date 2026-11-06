#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "base/core/layout_32bit.h"
#include "base/maths/shape_tests.h"
#include "base/core/checksums.h"

// Bone volumes: a capsule around each bone of a skeleton (from the bone to its parent, its radius from the global
// array 0x00E7085E's description of the bone) that lines are tested against - what a model's geoms use to tell which
// bone a line hits. One set is shared by every skeleton with the same number of bones (the table 0x006FA308 is keyed
// by the bone count), counting its users. From the disassembly of 0x0048B330-0x0048BF0D.
namespace maths {
class Mat4;
}
namespace gfx {
class Rig;
class RigData;

// One bone's volume (0x2C bytes in the original: its vtable 0x00648988 holds only intersects_segment, never called
// through it, and it has no subclass - no vtable here -, then a word never set).
struct BoneVolume {
    maths::Capsule capsule_; // +0x08 (its radius +0x28)

};

// 0x0048B330 (not a Ghidra function; the vtable's slot 0): whether `segment` passes through the capsule (where along
// the capsule's line: `s`).
int intersects_segment(BoneVolume& volume, const maths::Seg* segment, float* along);

// A skeleton's bone volumes (0x0C bytes): one per bone (the root has none).
struct BoneVolumes {
    // The fields other code reads and sets.
    [[nodiscard]] uint32_t num() const { return num_; }
    int& users() { return users_; }
    [[nodiscard]] int users() const { return users_; }

private:
    BoneVolume** volumes_{nullptr}; // +0x00 (null where a bone has none)
    uint32_t num_;         // +0x04 the bones
    int users_{0};         // +0x08

public:

    // 0x0048B350: `count` bones, none with a volume yet, no users.
    explicit BoneVolumes(uint32_t count);
    // The per-bone slots as a run (the bones counted by `num_`).
    [[nodiscard]] std::span<BoneVolume*> volume_run() { return {volumes_, num_}; }
    // 0x0048BEA0: one more user; `count` bones, none with a volume yet (the old array dropped); registered under
    // `count`.
    void enrol(uint32_t count);
    // 0x0048B430: a volume for each bone after the root that hasn't one and has a description (by name, in `data`) -
    // radius 3 unless it gives one.
    void get_fill(RigData* data);
    // The bone volume's scale and the radius added to it.
    struct BoneReach {
        float scale{};
        float extra_radius_{};
    };

    // 0x0048B580: the bone whose volume `segment` passes through. With `bone`: that bone's (its raw radius); else the
    // nearest hit of every bone's but the root's (radii times the bone's largest scale, times `scale` when it's
    // positive, plus `extra_radius`; the bones' world positions kept in 0x006FA320). The distance to it (the square
    // root of the squared distance) into `distance` and where along the segment into `t`, when given. The bone's
    // name, 0 for none.
    // Where a segment hits a bone: the distance and how far along the segment.
    struct BoneHit {
        float distance{};
        float t{};
    };
    uint32_t segment_hits_bone(const maths::Seg* segment, Rig* skeleton, const maths::Mat4* world, checksums::Name bone_name,
                               BoneReach reach, BoneHit* hit);
    // 0x0048BC40: the volume of the capsule round `bone` (radius times the bone's largest scale; none: 0) into
    // `volume`. True.
    bool get_bone_volume(Rig* skeleton, const maths::Mat4* world, uint32_t bone, float* volume);
};
constexpr std::size_t kBoneVolumesBytes = 0x0C;

static_assert(core::layout_32bit(sizeof(BoneVolumes) == kBoneVolumesBytes), "allocated as 0x0C bytes by the model component");

// 0x0048B3C0: the bone volumes registered for skeletons of `count` bones, or null.
BoneVolumes* find_bone_volumes(uint32_t count);
// 0x0048B400: one user fewer of those (they must exist); the last takes them out of the table (not freed).
void release_bone_volumes(uint32_t count);

} // namespace gfx
