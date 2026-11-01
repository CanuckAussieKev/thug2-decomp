// Bone volumes: see bone_volumes.h. From the disassembly of 0x0048B330-0x0048BF0D.

#include "engine/gfx/bone_volumes.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/rig.h"
#include "engine/lists/key_map.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/script/param_struct.h"
#include "engine/sys/startup_init.h"

namespace gfx {

namespace {

constexpr uint32_t kBoneDescriptions = 0x00E7085E; // (name unknown: the global array of bone descriptions)
constexpr uint32_t kBone = 0xCAB94088;             // bone
constexpr uint32_t kRadius = 0xC48391A5;           // radius
constexpr float kDefaultRadius = 3.0f;             // 0x40400000

// The shared sets, by bone count.
lists::KeyMap g_bone_volume_table{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(8) at start-up)
lists::KeyMap& get_table() { return g_bone_volume_table; }
maths::Vec4 g_bone_world_pos[64]; // each bone's world position as segment_hits_bone found it

// Each bone's world position as segment_hits_bone found it (64 of them).
maths::Vec4* joint_point_array() { return g_bone_world_pos; }

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void get_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

// A bone's matrix in the world: its bind pose (the inverse bind matrix inverted) times `pose`, times `world`.
void world_matrix(maths::Mat4* out, const maths::Mat4& inverse_bind, const maths::Mat4& pose, const maths::Mat4* world)
{
    *out = inverse_bind;
    out->invert_orthonormal();
    get_multiply(out, out, &pose);
    get_multiply(out, out, world);
}
// Bone `index`'s parent's world matrix (the root's: the identity's), with the parent's pose `parent_pose`.
void parent_world_matrix(maths::Mat4* out, RigData* data, const maths::Mat4* inverse_binds, int index,
                         const maths::Mat4& parent_pose, const maths::Mat4* world)
{
    if (index == 0)
        out->reset_to_identity();
    else
        *out = inverse_binds[data->fetch_parent_slot(index)];
    out->invert_orthonormal();
    get_multiply(out, out, &parent_pose);
    get_multiply(out, out, world);
}

// The largest of a bone's three scales (a NaN loses each comparison).
float largest_scale(const Joint* bone)
{
    float largest = bone->scale.x > bone->scale.y ? bone->scale.x : bone->scale.y;
    return largest > bone->scale.z ? largest : bone->scale.z;
}

// The capsule from `from` to `to` (all four components), of `radius`.
maths::Capsule get_between(const maths::Vec4& from, const maths::Vec4& to, float radius)
{
    maths::Vec4 offset = {to.x - from.x, to.y - from.y, to.z - from.z, to.w - from.w};
    return {from, offset, radius};
}

// A capsule's volume as the original's 0x00403390 leaves it in ST0 (to 53 bits, rounded only when stored - our
// Capsule::Loudness rounds to a float on the way): the cylinder's (length r^2 pi), then the sphere's (r^3 4/3 pi).
double capsule_volume(const maths::Capsule& capsule)
{
    double dx = static_cast<double>(capsule.line.end.x) - capsule.line.start.x;
    double dy = static_cast<double>(capsule.line.end.y) - capsule.line.start.y;
    double dz = static_cast<double>(capsule.line.end.z) - capsule.line.start.z;
    double length = std::sqrt(dz * dz + dx * dx + dy * dy);
    double r = capsule.radius;
    return length * r * r * static_cast<double>(3.14159274f) + r * r * r * static_cast<double>(4.18879032f);
}

// The array of `count` volumes, zeroed (no memory: a crash, as the original).
BoneVolume** new_volume_array(uint32_t count)
{
    auto* volumes = static_cast<BoneVolume**>(memory::alloc(count * 4));
    std::memset(volumes, 0, count * 4);
    return volumes;
}

} // namespace

// 0x0048B330
int BoneVolume::intersects_segment(const maths::Seg* segment, float* s)
{
    return capsule_.intersects_segment(*segment, s, nullptr, nullptr);
}

// 0x0048B350
BoneVolumes::BoneVolumes(uint32_t count)
{
    num_ = count;
    volumes_ = nullptr;
    users_ = 0;
    if (count)
        volumes_ = new_volume_array(num_);
}

// 0x0048BEA0
void BoneVolumes::enrol(uint32_t count)
{
    int users = users_ + 1;
    num_ = count;
    volumes_ = nullptr;
    users_ = users;
    if (count)
        volumes_ = new_volume_array(num_);
    get_table().store_entry(count, this);
}

// 0x0048B3C0 (the table's lookup, inlined)
BoneVolumes* find_bone_volumes(uint32_t count) { return static_cast<BoneVolumes*>(get_table().fetch_entry(count)); }

// 0x0048B400
void release_bone_volumes(uint32_t count)
{
    BoneVolumes* volumes = find_bone_volumes(count);
    if (--volumes->users_ == 0)
        get_table().erase_entry(count);
}

// 0x0048B430 (no descriptions, or no memory for a volume: a crash, as the original)
void BoneVolumes::get_fill(RigData* data)
{
    script::Array* descriptions = script::fetch_list(kBoneDescriptions);
    for (uint32_t i = 1; i < num_; ++i) {
        if (volumes_[i])
            continue;
        for (uint32_t j = 0; j < descriptions->extent_; ++j) {
            uint32_t bone = 0;
            reinterpret_cast<script::Struct*>(descriptions->fetch_part(j))->read_hash_value(kBone, &bone, 1);
            if (bone != data->names_[i])
                continue;
            float radius = kDefaultRadius;
            auto* volume = static_cast<BoneVolume*>(memory::alloc(sizeof(BoneVolume)));
            if (volume)
                new (&volume->capsule_) maths::Capsule();
            volumes_[i] = volume;
            reinterpret_cast<script::Struct*>(descriptions->fetch_part(j))->fetch_float(kRadius, &radius, 1);
            volumes_[i]->capsule_.radius = radius;
        }
    }
}

// 0x0048B580
uint32_t BoneVolumes::segment_hits_bone(const maths::Seg* segment, Rig* skeleton, const maths::Mat4* world,
                                        uint32_t bone, float scale, float extra_radius, float* distance, float* t)
{
    RigData* data = skeleton->payload_;
    maths::Mat4* inverse_binds = data->get_inverse_bind_matrices();
    if (bone) {
        int index = skeleton->fetch_bone_slot(bone);
        Joint* bone_state = skeleton->get_bone(index);
        maths::Mat4 pose = skeleton->matrix_array_[index];
        maths::Mat4 at{}, parent{};
        world_matrix(&at, inverse_binds[index], pose, world);
        pose = *bone_state->parent;
        parent_world_matrix(&parent, data, inverse_binds, index, pose, world);
        BoneVolume* volume = volumes_[index];
        float radius = volume ? volume->capsule_.radius : 0.0f;
        maths::Capsule capsule = get_between(at.row[3], parent.row[3], radius);
        float distance_squared = 0.0f;
        if (!capsule.intersects_segment(*segment, nullptr, t, &distance_squared))
            return 0;
        if (distance)
            *distance = static_cast<float>(std::sqrt(static_cast<double>(distance_squared)));
        return bone;
    }
    float nearest = 0.0f;
    float nearest_t = 0.0f;
    uint32_t hit = 0;
    maths::Vec4* positions = joint_point_array();
    for (int i = 0; i < skeleton->get_num_bones(); ++i) {
        Joint* bone_state = skeleton->get_bone(i);
        maths::Mat4 at{};
        world_matrix(&at, inverse_binds[i], skeleton->matrix_array_[i], world);
        positions[i] = at.row[3];
        if (i == 0)
            continue;
        maths::Vec4 parent = positions[data->fetch_parent_slot(i)];
        BoneVolume* volume = volumes_[i];
        if (!volume)
            continue;
        double radius = static_cast<double>(largest_scale(bone_state)) * volume->capsule_.radius;
        if (radius < 0.0 || radius == 0.0) // (NaN goes on)
            continue;
        if (scale > 0.0f)
            radius *= scale;
        maths::Capsule capsule = get_between(at.row[3], parent, static_cast<float>(radius + extra_radius));
        float hit_t = 0.0f, distance_squared = 0.0f;
        if (capsule.intersects_segment(*segment, nullptr, &hit_t, &distance_squared) &&
            (!hit || distance_squared < nearest)) {
            hit = bone_state->name;
            nearest = distance_squared;
            nearest_t = hit_t;
        }
    }
    if (distance)
        *distance = static_cast<float>(std::sqrt(static_cast<double>(nearest)));
    if (t)
        *t = nearest_t;
    return hit;
}

// 0x0048BC40
bool BoneVolumes::get_bone_volume(Rig* skeleton, const maths::Mat4* world, uint32_t bone, float* volume)
{
    RigData* data = skeleton->payload_;
    maths::Mat4* inverse_binds = data->get_inverse_bind_matrices();
    int index = skeleton->fetch_bone_slot(bone);
    Joint* bone_state = skeleton->get_bone(index);
    maths::Mat4 pose = skeleton->matrix_array_[index];
    maths::Mat4 at{}, parent{};
    world_matrix(&at, inverse_binds[index], pose, world);
    pose = *bone_state->parent;
    parent_world_matrix(&parent, data, inverse_binds, index, pose, world);
    float radius = 0.0f;
    if (BoneVolume* own = volumes_[index])
        radius = largest_scale(bone_state) * own->capsule_.radius;
    *volume = static_cast<float>(capsule_volume(get_between(at.row[3], parent.row[3], radius)));
    return true;
}

} // namespace gfx

namespace gfx {

// 0x00640AE0 (a static initialiser): the bone volumes' table, 256 buckets.
void construct_bone_volume_table() { new (&g_bone_volume_table) lists::KeyMap(8); }

} // namespace gfx
