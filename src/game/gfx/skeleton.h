#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Script {
class CStruct;
}

// Skeletons (Gfx): a skeleton asset (CSkeletonData, loaded from a .ske file - its bones' names, parents and mirror
// bones, LOD masks and distances, and each bone's inverse bind matrix) and the skeleton of an object that uses it
// (CSkeleton: a matrix and per-bone state for each bone, updated from an animation pose).
namespace Gfx {

struct CPose;

// A skeleton asset (0x598 bytes, vtable 0x00649F48 - only its deleting destructor).
class CSkeletonData {
public:
    // 0x004A2C20: empty, no flags, the first LOD distance FLT_MAX-ish (0x7F7FC99E). Returns this.
    CSkeletonData* Construct();
    // 0x004A3950 (virtual [0]): the inverse bind matrices and bind translations freed; freed itself with flags & 1.
    // Returns this.
    CSkeletonData* Destroy(unsigned int freeFlags);
    // 0x004A39C0: read from a .ske file in memory (+4 the flags, +8 the count, then the names, parents' names and
    // mirror bones' names, then each bone's bind rotation (a quaternion) and translation): the inverse bind
    // matrices built down the hierarchy, the mirror pairs found (no mirror: -1). The size and the flag aren't used.
    // Always true.
    bool Load(const uint32_t* buffer, uint32_t size, bool unused);
    // 0x004A2C50: bone `index`'s parent's name (0: none).
    uint32_t GetParentName(int index) { return m_parentNames[index]; }
    // 0x004A2C60 (not a Ghidra function): bone `index`'s mirror bone (-1: none).
    int GetFlipIndex(int index) { return m_flipIndices[index]; }
    // 0x004A2C70: the index of the bone named `name` (0 when there's none).
    int GetIndex(uint32_t name);
    // 0x004A30D0: bone `index`'s parent's index (0 when it has none).
    int GetParentIndex(int index);
    // 0x004A3100: whether `ancestor` (lower) is up `bone`'s hierarchy - walked while the parents are above it
    // (unsigned).
    bool IsDescendant(uint32_t bone, uint32_t ancestor);
    // 0x004A2CA0
    Mth::Matrix* GetInverseBindMatrices() { return mp_inverseBind; }

    void** m_vtable;
    int m_count;                      // +0x004
    uint32_t m_names[64];             // +0x008
    uint32_t m_parentNames[64];       // +0x108
    uint32_t m_flipNames[64];         // +0x208
    int m_flipIndices[64];            // +0x308
    uint32_t m_lodMasks[64];          // +0x408 per bone: the LODs it's collapsed into its parent at (bit per LOD)
    float m_lodDistances[32];         // +0x508
    uint32_t m_unknown588;            // +0x588
    Mth::Matrix* mp_inverseBind;      // +0x58C
    Mth::Vector* mp_bindTranslations; // +0x590
    uint32_t m_flags;                 // +0x594 1: the skeleton's flag 0x10000000
};
static_assert(offsetof(CSkeletonData, m_lodMasks) == 0x408 && offsetof(CSkeletonData, mp_inverseBind) == 0x58C &&
                  sizeof(CSkeletonData) == 0x598,
              "layout must match the original");

// A skeleton's bone (0x1C bytes).
struct CBone {
    enum : uint32_t {
        kScaled = 0x10000000,      // its matrix scaled by `scale` as it's built
        kScaledLocal = 0x20000000, // the final matrix's rows scaled
        kInactive = 0x40000000,    // left as it is
    };
    uint32_t name;
    Mth::Matrix* parent; // its parent's matrix (the root: the first matrix)
    uint32_t flags;
    Mth::Vector scale;
};
static_assert(sizeof(CBone) == 0x1C, "layout must match the original");

// An object's skeleton (0x1C bytes).
class CSkeleton {
public:
    enum : uint32_t {
        kCollapseZeroScaled = 0x08000000, // bones scaled to nothing collapse to the root (Update's end)
        kMirrorData = 0x10000000,         // (the data's flag 1) the bone 0x580C0963's scale named 0xDFEE29AC
    };

    // 0x004A3D30: for `data`: its bones (names, parents' matrices, scale 1) and identity matrices, LOD limit 31.
    // Returns this.
    CSkeleton* Construct(CSkeletonData* data);
    // 0x004A3E80: a copy of `other` (its matrices and bones' state).
    CSkeleton* ConstructCopy(const CSkeleton* other);
    // 0x004A2CB0: the bones and matrices freed.
    void DestroyBody();
    // 0x004A2BA0 (4 bytes, a getter folded with others'): the bone count.
    int GetNumBones() { return m_numBones; }
    // 0x004A2B60: the index of the bone named `name` (-1: none).
    int GetBoneIndex(uint32_t name);
    // 0x004A2B90
    CBone* GetBone(int index) { return mp_bones + index; }
    // 0x004A2BB0: the bone named `name` made active or not. Whether it was found.
    bool SetBoneActive(uint32_t name, bool active);
    // 0x004A2CE0: every bone's scale back to 1 (unflagged).
    void ResetBoneScales();
    // 0x004A2D20: the scale of the bone named `name`. Whether it was found.
    bool GetBoneScale(uint32_t name, Mth::Vector* scale);
    // 0x004A2D80: the bone's scale set; unless it's exactly 1 (x, y, z), flagged local (`local`) or not. Whether it
    // was found.
    bool SetBoneScale(uint32_t name, const Mth::Vector* scale, bool local);
    // 0x004A2E20: `other`'s scales of the bones both have, set here - local unless `checkList` and the bone is in
    // the script array 0x20D9AC2F.
    void CopyBoneScales(const CSkeleton* other, bool checkList);
    // 0x004A3570: the bones' scales multiplied by the structure's vectors named after them (local unless
    // 0x9E3A09FC=non-zero, or once a bone in the array 0x20D9AC2F is met). False without a structure.
    bool ApplyBoneScales(Script::CStruct* params);
    // 0x004A3090: the LOD for `distance`: the first whose distance is beyond it (at most the limit).
    void SetLOD(float distance);
    // 0x004A3150: each bone's parent matrix (its parent's index in `data` - none: the first) and flags cleared.
    void LinkBones(CSkeletonData* data);
    // 0x004A36E0: bone `name`'s matrix in its own space (its inverse bind x its matrix; with the bone 0x9F173EEB, turned
    // by that bone's inverse bind about the origin). Whether it was found.
    bool GetBoneMatrix(uint32_t name, Mth::Matrix* out);
    // 0x004A3870: that matrix's translation (without the turn).
    bool GetBonePosition(uint32_t name, Mth::Vector* out);
    // 0x004A31E0: each bone's position and its parent's, in the world (a debug line - compiled out).
    void Draw(const Mth::Matrix* world, uint32_t startColour, uint32_t endColour, int frames);
    // 0x004A4240: the matrices from the pose's rotations and translations: each bone's local matrix (scaled
    // when flagged) times its parent's (or its parent's when collapsed at this LOD), then times its inverse bind
    // matrix (rows scaled when flagged local), the rows' w cleared; then CollapseZeroScaledBones.
    void Update(const float (*rotations)[4], const float (*translations)[4]);
    // 0x004A44E0
    void Update(const CPose* pose);
    // 0x004A4000: with kCollapseZeroScaled: the bones scaled to nothing (x, y, z all 0) put at the first scaled
    // bone's position (the first matrix's translation: that bone's GetBonePosition), their rotation rows (0, 0, 0, 1).
    void CollapseZeroScaledBones();

    uint32_t m_flags;          // +0x00
    Mth::Matrix* mp_matrices;  // +0x04
    CSkeletonData* mp_data;    // +0x08
    uint32_t m_lod;            // +0x0C
    uint32_t m_maxLod;         // +0x10
    int m_numBones;            // +0x14
    CBone* mp_bones;           // +0x18
};
static_assert(offsetof(CSkeleton, mp_bones) == 0x18 && sizeof(CSkeleton) == 0x1C, "layout must match the original");

// 0x004A2F20 (EAX: out, EDX: the translation, BL: `swapZ`; the quaternion and `mirror` on the stack, the caller
// pops them): a bone's matrix from a rotation and a translation (x87: the products and the -z w term unrounded, the
// -x w and -y w terms rounded); `mirror` / `swapZ` negate entries to mirror it.
void QuatToBoneMatrix(Mth::Matrix* out, const Mth::Vector* translation, const float* quat, bool mirror, bool swapZ);
void QuatToBoneMatrixStub();

} // namespace Gfx
