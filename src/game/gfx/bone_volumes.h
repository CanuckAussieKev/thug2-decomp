#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"

// Bone volumes: a capsule around each bone of a skeleton (from the bone to its parent, its radius from the global
// array 0x00E7085E's description of the bone) that lines are tested against - what a model's geoms use to tell which
// bone a line hits. One set is shared by every skeleton with the same number of bones (the table 0x006FA308 is keyed
// by the bone count), counting its users. From the disassembly of 0x0048B330-0x0048BF0D.
namespace Mth {
class Matrix;
}
namespace Gfx {
class CSkeleton;
class CSkeletonData;

// One bone's volume (0x2C bytes, vtable 0x00648988).
struct CBoneVolume {
    void** m_vtable;
    uint32_t m_unknown4; // (never set)
    Mth::Capsule m_capsule; // +0x08 (its radius +0x28)

    // 0x0048B330 (not a Ghidra function; vtable slot 0): whether `segment` passes through the capsule (where along
    // the capsule's line: `s`).
    int IntersectsSegment(const Mth::Line* segment, float* s);
};
static_assert(offsetof(CBoneVolume, m_capsule) == 0x08 && sizeof(CBoneVolume) == 0x2C,
              "layout must match the original (the radius at +0x28)");

// A skeleton's bone volumes (0x0C bytes): one per bone (the root has none).
struct CBoneVolumes {
    CBoneVolume** mp_volumes; // +0x00 (null where a bone has none)
    uint32_t m_count;         // +0x04 the bones
    int m_users;              // +0x08

    // 0x0048B350: `count` bones, none with a volume yet, no users. Returns this.
    CBoneVolumes* Construct(uint32_t count);
    // 0x0048BEA0: one more user; `count` bones, none with a volume yet (the old array dropped); registered under
    // `count`.
    void Register(uint32_t count);
    // 0x0048B430: a volume for each bone after the root that hasn't one and has a description (by name, in `data`) -
    // radius 3 unless it gives one.
    void Fill(CSkeletonData* data);
    // 0x0048B580: the bone whose volume `segment` passes through. With `bone`: that bone's (its raw radius); else the
    // nearest hit of every bone's but the root's (radii times the bone's largest scale, times `scale` when it's
    // positive, plus `extraRadius`; the bones' world positions kept in 0x006FA320). The distance to it (the square
    // root of the squared distance) into `distance` and where along the segment into `t`, when given. The bone's
    // name, 0 for none.
    uint32_t TestLine(const Mth::Line* segment, CSkeleton* skeleton, const Mth::Matrix* world, uint32_t bone,
                      float scale, float extraRadius, float* distance, float* t);
    // 0x0048BC40: the volume of the capsule round `bone` (radius times the bone's largest scale; none: 0) into
    // `volume`. True.
    bool GetBoneVolume(CSkeleton* skeleton, const Mth::Matrix* world, uint32_t bone, float* volume);
};
static_assert(sizeof(CBoneVolumes) == 0x0C, "allocated as 0x0C bytes by the model component");

// 0x0048B3C0: the bone volumes registered for skeletons of `count` bones, or null.
CBoneVolumes* FindBoneVolumes(uint32_t count);
// 0x0048B400: one user fewer of those (they must exist); the last takes them out of the table (not freed).
void ReleaseBoneVolumes(uint32_t count);

} // namespace Gfx
