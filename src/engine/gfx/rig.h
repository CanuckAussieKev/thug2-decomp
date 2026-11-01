#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

namespace script {
class Struct;
}

// Skeletons (Gfx): a skeleton asset (RigData, loaded from a .ske file - its bones' names, parents and mirror
// bones, LOD masks and distances, and each bone's inverse bind matrix) and the skeleton of an object that uses it
// (Rig: a matrix and per-bone state for each bone, updated from an animation pose).
namespace gfx {

struct Stance;

// A skeleton asset (0x598 bytes in the original, vtable 0x00649F48 - only its deleting destructor; no subclass: no
// vtable here).
class RigData : public memory::HeapObject {
public:
    // 0x004A2C20: empty, no flags, the first LOD distance FLT_MAX-ish (0x7F7FC99E).
    RigData();
    // 0x004A3950 (the scalar deleting destructor): the inverse bind matrices and bind translations freed.
    ~RigData();
    RigData(const RigData&) = delete;
    RigData& operator=(const RigData&) = delete;
    // 0x004A39C0: read from a .ske file in memory (engine/gfx/skeleton_format.h: the flags, the count, the names,
    // parents' names and mirror bones' names, then each bone's bind rotation and translation): the inverse bind
    // matrices built down the hierarchy, the mirror pairs found (no mirror: -1). The flag isn't used; the size (not
    // original; 0: not known) bounds the reader - a damaged skeleton has no bones. Always true.
    bool read_in(const uint32_t* buffer, uint32_t size, bool unused);
    // 0x004A2C50: bone `index`'s parent's name (0: none).
    uint32_t fetch_parent_hash(int index) { return parent_names_[index]; }
    // 0x004A2C60 (not a Ghidra function): bone `index`'s mirror bone (-1: none).
    int fetch_mirror_slot(int index) { return flip_indices_[index]; }
    // 0x004A2C70: the index of the bone named `name` (0 when there's none).
    int fetch_slot_no(uint32_t name);
    // 0x004A30D0: bone `index`'s parent's index (0 when it has none).
    int fetch_parent_slot(int index);
    // 0x004A3100: whether `ancestor` (lower) is up `bone`'s hierarchy - walked while the parents are above it
    // (unsigned).
    bool is_descendant(uint32_t bone, uint32_t ancestor);
    // 0x004A2CA0
    maths::Mat4* get_inverse_bind_matrices() { return inverse_bind_; }

    int num_;                        // +0x004
    uint32_t names_[64];             // +0x008
    uint32_t parent_names_[64];      // +0x108
    uint32_t flip_names_[64];        // +0x208
    int flip_indices_[64];           // +0x308
    uint32_t lod_masks_[64];         // +0x408 per bone: the LODs it's collapsed into its parent at (bit per LOD)
    float lod_distances_[32];        // +0x508
    uint32_t unknown588_;            // +0x588
    maths::Mat4* inverse_bind_;      // +0x58C
    maths::Vec4* bind_translations_; // +0x590
    uint32_t flag_bits_;             // +0x594 1: the skeleton's flag 0x10000000
};

// A skeleton's bone (0x1C bytes).
struct Joint {
    enum : uint32_t {
        kScaled = 0x10000000,      // its matrix scaled by `scale` as it's built
        kScaledLocal = 0x20000000, // the final matrix's rows scaled
        kInactive = 0x40000000,    // left as it is
    };
    uint32_t name;
    maths::Mat4* parent; // its parent's matrix (the root: the first matrix)
    uint32_t flags;
    maths::Vec4 scale;
};
static_assert(sizeof(Joint) == 0x1C, "layout must match the original");

// An object's skeleton (0x1C bytes).
class Rig {
public:
    enum : uint32_t {
        kCollapseZeroScaled = 0x08000000, // bones scaled to nothing collapse to the root (Update's end)
        kMirrorData = 0x10000000,         // (the data's flag 1) the bone 0x580C0963's scale named 0xDFEE29AC
    };

    // 0x004A3D30: for `data`: its bones (names, parents' matrices, scale 1) and identity matrices, LOD limit 31.
    // Returns this.
    Rig* make_new(RigData* data);
    // 0x004A3E80: a copy of `other` (its matrices and bones' state).
    Rig* construct_copy(const Rig* other);
    // 0x004A2CB0: the bones and matrices freed.
    void destroy_body();
    // 0x004A2BA0 (4 bytes, a getter folded with others'): the bone count.
    int get_num_bones() { return bone_total_; }
    // 0x004A2B60: the index of the bone named `name` (-1: none).
    int fetch_bone_slot(uint32_t name);
    // 0x004A2B90
    Joint* get_bone(int index) { return bone_array_ + index; }
    // 0x004A2BB0: the bone named `name` made active or not. Whether it was found.
    bool enable_bone(uint32_t name, bool active);
    // 0x004A2CE0: every bone's scale back to 1 (unflagged).
    void reset_bone_scales();
    // 0x004A2D20: the scale of the bone named `name`. Whether it was found.
    bool fetch_bone_sizing(uint32_t name, maths::Vec4* scale);
    // 0x004A2D80: the bone's scale set; unless it's exactly 1 (x, y, z), flagged local (`local`) or not. Whether it
    // was found.
    bool change_bone_sizing(uint32_t name, const maths::Vec4* scale, bool local);
    // 0x004A2E20: `other`'s scales of the bones both have, set here - local unless `checkList` and the bone is in
    // the script array 0x20D9AC2F.
    void copy_bone_scales(const Rig* other, bool check_list);
    // 0x004A3570: the bones' scales multiplied by the structure's vectors named after them (local unless
    // 0x9E3A09FC=non-zero, or once a bone in the array 0x20D9AC2F is met). False without a structure.
    bool apply_bone_scales(script::Struct* params);
    // 0x004A3090: the LOD for `distance`: the first whose distance is beyond it (at most the limit).
    void set_lod(float distance);
    // 0x004A3150: each bone's parent matrix (its parent's index in `data` - none: the first) and flags cleared.
    void link_bones(RigData* data);
    // 0x004A36E0: bone `name`'s matrix in its own space (its inverse bind x its matrix; with the bone 0x9F173EEB, turned
    // by that bone's inverse bind about the origin). Whether it was found.
    bool read_joint_matrix(uint32_t name, maths::Mat4* out);
    // 0x004A3870: that matrix's translation (without the turn).
    bool get_bone_position(uint32_t name, maths::Vec4* out);
    // 0x004A31E0: each bone's position and its parent's, in the world (a debug line - compiled out).
    void blit(const maths::Mat4* world, uint32_t start_colour, uint32_t end_colour, int frames);
    // 0x004A4240: the matrices from the pose's rotations and translations: each bone's local matrix (scaled
    // when flagged) times its parent's (or its parent's when collapsed at this LOD), then times its inverse bind
    // matrix (rows scaled when flagged local), the rows' w cleared; then collapse_zero_scaled_bones.
    void tick_frame(const float (*rotations)[4], const float (*translations)[4]);
    // 0x004A44E0
    void tick_frame(const Stance* pose);
    // 0x004A4000: with kCollapseZeroScaled: the bones scaled to nothing (x, y, z all 0) put at the first scaled
    // bone's position (the first matrix's translation: that bone's GetBonePosition), their rotation rows (0, 0, 0, 1).
    void collapse_zero_scaled_bones();

    uint32_t flag_bits_;        // +0x00
    maths::Mat4* matrix_array_; // +0x04
    RigData* payload_;          // +0x08
    uint32_t lod_;              // +0x0C
    uint32_t max_lod_;          // +0x10
    int bone_total_;            // +0x14
    Joint* bone_array_;         // +0x18
};
static_assert(offsetof(Rig, bone_array_) == 0x18 && sizeof(Rig) == 0x1C, "layout must match the original");

// 0x004A2F20: a bone's matrix from a rotation and a translation (x87: the products and the -z w term unrounded, the
// -x w and -y w terms rounded); `mirror` / `swap_z` negate entries to mirror it.
void quat_to_bone_matrix(maths::Mat4* out, const maths::Vec4* translation, const float* quat, bool mirror, bool swap_z);

} // namespace gfx
