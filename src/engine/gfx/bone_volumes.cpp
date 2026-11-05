// Bone volumes: see bone_volumes.h. From the disassembly of 0x0048B330-0x0048BF0D.

#include "engine/gfx/bone_volumes.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <array>
#include <span>

#include "engine/memory/mem.h"
#include "engine/gfx/rig.h"
#include "engine/lists/key_map.h"
#include "base/maths/shape_tests.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/script/param_struct.h"
#include "engine/sys/startup_init.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/core/checksums.h"
#include "base/maths/trig.h"

namespace gfx {

constexpr std::size_t kBoneVolumeCount = 64;
constexpr float kSphereVolumeFactor = 4.18879032F; // 4/3 pi, rounded as the original has it
constexpr int kBoneVolumeBuckets = 8;

namespace {

constexpr uint32_t kBoneDescriptions = 0x00E7085E; // (name unknown: the global array of bone descriptions)
constexpr uint32_t kBone = 0xCAB94088;             // bone
constexpr uint32_t kRadius = 0xC48391A5;           // radius
constexpr float kDefaultRadius = 3.0F;             // 0x40400000

// NOT ORIGINAL grouping - bone_volumes.cpp's state (the original's globals). Made on first use, never destroyed
// (bone_volumes_state()).
struct BoneVolumesState {
    // The shared sets, by bone count.
    lists::KeyMap bone_volume_table_{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(8) at start-up)
    std::array<maths::Vec4, kBoneVolumeCount> bone_world_pos_{}; // each bone's world position as segment_hits_bone found it
};

BoneVolumesState& bone_volumes_state()
{
    static core::NoDestroy<BoneVolumesState> state;
    return state.get();
}

lists::KeyMap& get_table() { return bone_volumes_state().bone_volume_table_; }

// Each bone's world position as segment_hits_bone found it (64 of them).
std::array<maths::Vec4, kBoneVolumeCount>& joint_point_array() { return bone_volumes_state().bone_world_pos_; }

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void get_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { maths::matrix_multiply(out, a, b); }

// A bone's matrices: its inverse bind matrix, its pose, and the model's world matrix.
struct BoneFrames {
    const maths::Mat4* inverse_bind_;
    const maths::Mat4* pose;
    const maths::Mat4* world;
};

// A bone's matrix in the world: its bind pose (the inverse bind matrix inverted) times its pose, times the world.
void world_matrix(maths::Mat4* out, const BoneFrames& bone)
{
    *out = *bone.inverse_bind_;
    out->invert_orthonormal();
    get_multiply(out, out, bone.pose);
    get_multiply(out, out, bone.world);
}

// Bone `index`'s parent's world matrix (the root's: the identity's), with the parent's pose `parent_pose`.
void parent_world_matrix(maths::Mat4* out, RigData* data, std::span<const maths::Mat4> inverse_binds, int index,
                         const maths::Mat4& parent_pose, const maths::Mat4* world)
{
    if (index == 0) {
        out->reset_to_identity();
    } else {
        *out = core::at(inverse_binds, data->fetch_parent_slot(index));
    }
    out->invert_orthonormal();
    get_multiply(out, out, &parent_pose);
    get_multiply(out, out, world);
}

// The largest of a bone's three scales (a NaN loses each comparison).
float largest_scale(const Joint* bone)
{
    const float largest = bone->scale.x > bone->scale.y ? bone->scale.x : bone->scale.y;
    return largest > bone->scale.z ? largest : bone->scale.z;
}

// The capsule from `from` to `to` (all four components), of `radius`.
maths::Capsule get_between(const maths::Vec4& from, const maths::Vec4& to, float radius)
{
    const maths::Vec4 offset = {.x = to.x - from.x, .y = to.y - from.y, .z = to.z - from.z, .w = to.w - from.w};
    return {from, offset, radius};
}

// A capsule's volume as the original's 0x00403390 leaves it in ST0 (to 53 bits, rounded only when stored - our
// Capsule::Loudness rounds to a float on the way): the cylinder's (length r^2 pi), then the sphere's (r^3 4/3 pi).
double capsule_volume(const maths::Capsule& capsule)
{
    const double dx = static_cast<double>(capsule.line.end.x) - capsule.line.start.x;
    const double dy = static_cast<double>(capsule.line.end.y) - capsule.line.start.y;
    const double dz = static_cast<double>(capsule.line.end.z) - capsule.line.start.z;
    const double length = std::sqrt((dz * dz) + (dx * dx) + (dy * dy));
    const double radius = capsule.radius;
    return (length * radius * radius * static_cast<double>(maths::kPi)) +
           (radius * radius * radius * static_cast<double>(kSphereVolumeFactor));
}

// The array of `count` volumes, zeroed (no memory: a crash, as the original).
BoneVolume** new_volume_array(uint32_t count)
{
    auto* volumes = static_cast<BoneVolume**>(memory::alloc(count * 4));
    std::memset(static_cast<void*>(volumes), 0, count * 4);
    return volumes;
}

} // namespace

// 0x0048B330
int intersects_segment(BoneVolume& volume, const maths::Seg* segment, float* along)
{
    return volume.capsule_.intersects_segment(*segment, {.s = along, .t = nullptr, .squared_gap_ = nullptr});
}

// 0x0048B350
BoneVolumes::BoneVolumes(uint32_t count) : num_(count)
{
    if (count != 0U) {
        volumes_ = new_volume_array(num_);
    }
}

// 0x0048BEA0
void BoneVolumes::enrol(uint32_t count)
{
    const int users = users_ + 1;
    num_ = count;
    volumes_ = nullptr;
    users_ = users;
    if (count != 0U) {
        volumes_ = new_volume_array(num_);
    }
    get_table().store_entry(count, this);
}

// 0x0048B3C0 (the table's lookup, inlined)
BoneVolumes* find_bone_volumes(uint32_t count) { return static_cast<BoneVolumes*>(get_table().fetch_entry(count)); }

// 0x0048B400
void release_bone_volumes(uint32_t count)
{
    BoneVolumes* volumes = find_bone_volumes(count);
    if (--volumes->users() == 0) {
        get_table().erase_entry(count);
    }
}

// 0x0048B430 (no descriptions, or no memory for a volume: a crash, as the original)
void BoneVolumes::get_fill(RigData* data)
{
    script::Array* descriptions = script::fetch_list(kBoneDescriptions);
    for (uint32_t i = 1; i < num_; ++i) {
        if (core::at(volume_run(), i) != nullptr) {
            continue;
        }
        for (uint32_t j = 0; j < descriptions->extent(); ++j) {
            uint32_t bone = 0;
            descriptions->struct_at(j)->read_hash_value(kBone, &bone, 1);
            if (bone != core::at(data->names(), i)) {
                continue;
            }
            float radius = kDefaultRadius;
            auto* volume = static_cast<BoneVolume*>(memory::alloc(sizeof(BoneVolume)));
            if (volume) {
                new (&volume->capsule_) maths::Capsule();
            }
            core::at(volume_run(), i) = volume;
            descriptions->struct_at(j)->fetch_float(kRadius, &radius, 1);
            core::at(volume_run(), i)->capsule_.radius = radius;
        }
    }
}

// 0x0048B580
uint32_t BoneVolumes::segment_hits_bone(const maths::Seg* segment, Rig* skeleton, const maths::Mat4* world,
                                        checksums::Name bone_name, BoneReach reach, BoneHit* bone_hit)
{
    const auto scale = reach.scale;
    const auto extra_radius = reach.extra_radius_;
    const uint32_t bone = bone_name.value();
    RigData* data = skeleton->payload();
    const std::span<const maths::Mat4> inverse_binds = data->inverse_binds();
    if (bone != 0U) {
        const int index = skeleton->fetch_bone_slot(bone);
        const Joint* bone_state = skeleton->get_bone(index);
        maths::Mat4 pose = core::at(skeleton->matrices(), index);
        maths::Mat4 at{};
        maths::Mat4 parent{};
        world_matrix(&at, {.inverse_bind_ = &core::at(inverse_binds, index), .pose = &pose, .world = world});
        pose = *bone_state->parent;
        parent_world_matrix(&parent, data, inverse_binds, index, pose, world);
        const BoneVolume* volume = core::at(volume_run(), index);
        const float radius = volume ? volume->capsule_.radius : 0.0F;
        const maths::Capsule capsule = get_between(core::at(at.row, 3), core::at(parent.row, 3), radius);
        float distance_squared = 0.0F;
        if (capsule.intersects_segment(
                *segment, {.s = nullptr, .t = bone_hit ? &bone_hit->t : nullptr, .squared_gap_ = &distance_squared}) ==
            0) {
            return 0;
        }
        if (bone_hit) {
            bone_hit->distance = static_cast<float>(std::sqrt(static_cast<double>(distance_squared)));
        }
        return bone;
    }
    float nearest = 0.0F;
    float nearest_t = 0.0F;
    uint32_t hit = 0;
    auto& positions = joint_point_array();
    for (int i = 0; i < skeleton->get_num_bones(); ++i) {
        const Joint* bone_state = skeleton->get_bone(i);
        maths::Mat4 at{};
        world_matrix(&at, {.inverse_bind_ = &core::at(inverse_binds, i), .pose = &core::at(skeleton->matrices(), i), .world = world});
        core::at(positions, i) = core::at(at.row, 3);
        if (i == 0) {
            continue;
        }
        const maths::Vec4 parent = core::at(positions, data->fetch_parent_slot(i));
        const BoneVolume* volume = core::at(volume_run(), i);
        if (!volume) {
            continue;
        }
        double radius = static_cast<double>(largest_scale(bone_state)) * volume->capsule_.radius;
        if (radius < 0.0 || radius == 0.0) { // (NaN goes on)
            continue;
        }
        if (scale > 0.0F) {
            radius *= scale;
        }
        const maths::Capsule capsule = get_between(core::at(at.row, 3), parent, static_cast<float>(radius + extra_radius));
        float hit_t = 0.0F;
        float distance_squared = 0.0F;
        if ((capsule.intersects_segment(*segment, {.s = nullptr, .t = &hit_t, .squared_gap_ = &distance_squared}) !=
             0) &&
            ((hit == 0U) || distance_squared < nearest)) {
            hit = bone_state->name;
            nearest = distance_squared;
            nearest_t = hit_t;
        }
    }
    if (bone_hit) {
        bone_hit->distance = static_cast<float>(std::sqrt(static_cast<double>(nearest)));
    }
    if (bone_hit) {
        bone_hit->t = nearest_t;
    }
    return hit;
}

// 0x0048BC40
bool BoneVolumes::get_bone_volume(Rig* skeleton, const maths::Mat4* world, uint32_t bone, float* volume)
{
    RigData* data = skeleton->payload();
    const std::span<const maths::Mat4> inverse_binds = data->inverse_binds();
    const int index = skeleton->fetch_bone_slot(bone);
    const Joint* bone_state = skeleton->get_bone(index);
    maths::Mat4 pose = core::at(skeleton->matrices(), index);
    maths::Mat4 at{};
    maths::Mat4 parent{};
    world_matrix(&at, {.inverse_bind_ = &core::at(inverse_binds, index), .pose = &pose, .world = world});
    pose = *bone_state->parent;
    parent_world_matrix(&parent, data, inverse_binds, index, pose, world);
    float radius = 0.0F;
    if (const BoneVolume* own = core::at(volume_run(), index)) {
        radius = largest_scale(bone_state) * own->capsule_.radius;
    }
    *volume = static_cast<float>(capsule_volume(get_between(core::at(at.row, 3), core::at(parent.row, 3), radius)));
    return true;
}

} // namespace gfx

namespace gfx {

// 0x00640AE0 (a static initialiser): the bone volumes' table, 256 buckets.
void construct_bone_volume_table() { new (&bone_volumes_state().bone_volume_table_) lists::KeyMap(kBoneVolumeBuckets); }

} // namespace gfx
