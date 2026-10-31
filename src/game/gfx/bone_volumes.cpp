// Bone volumes: see bone_volumes.h. From the disassembly of 0x0048B330-0x0048BF0D.

#include "game/gfx/bone_volumes.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/rig.h"
#include "game/lst/key_map.h"
#include "game/mth/shape_tests.h"
#include "game/mth/mat4.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/sys/startup_init.h"

namespace Gfx2D {

namespace {

extern void** const kVolumeVtable;

constexpr uint32_t kBoneDescriptions = 0x00E7085E; // (name unknown: the global array of bone descriptions)
constexpr uint32_t kBone = 0xCAB94088;              // bone
constexpr uint32_t kRadius = 0xC48391A5;            // radius
constexpr float kDefaultRadius = 3.0f;              // 0x40400000

// The shared sets, by bone count.
ListKit::KeyMap g_boneVolumeTable{ListKit::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(8) at start-up)
ListKit::KeyMap& Table() { return g_boneVolumeTable; }
Maths::Vec4 g_bonePositions[64]; // each bone's world position as TestLine found it

// Each bone's world position as TestLine found it (64 of them).
Maths::Vec4* BonePositions() { return g_bonePositions; }

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void Multiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// A bone's matrix in the world: its bind pose (the inverse bind matrix inverted) times `pose`, times `world`.
void WorldMatrix(Maths::Mat4* out, const Maths::Mat4& inverseBind, const Maths::Mat4& pose, const Maths::Mat4* world)
{
    *out = inverseBind;
    out->InvertOrthonormal();
    Multiply(out, out, &pose);
    Multiply(out, out, world);
}
// Bone `index`'s parent's world matrix (the root's: the identity's), with the parent's pose `parentPose`.
void ParentWorldMatrix(Maths::Mat4* out, CRigData* data, const Maths::Mat4* inverseBinds, int index,
                       const Maths::Mat4& parentPose, const Maths::Mat4* world)
{
    if (index == 0)
        out->ResetToIdentity();
    else
        *out = inverseBinds[data->FetchParentSlot(index)];
    out->InvertOrthonormal();
    Multiply(out, out, &parentPose);
    Multiply(out, out, world);
}

// The largest of a bone's three scales (a NaN loses each comparison).
float LargestScale(const CJoint* bone)
{
    float largest = bone->scale.x > bone->scale.y ? bone->scale.x : bone->scale.y;
    return largest > bone->scale.z ? largest : bone->scale.z;
}

// The capsule from `from` to `to` (all four components), of `radius`.
Maths::Capsule Between(const Maths::Vec4& from, const Maths::Vec4& to, float radius)
{
    Maths::Vec4 offset = {to.x - from.x, to.y - from.y, to.z - from.z, to.w - from.w};
    return Maths::Capsule(from, offset, radius);
}

// A capsule's volume as the original's 0x00403390 leaves it in ST0 (to 53 bits, rounded only when stored - our
// Capsule::Loudness rounds to a float on the way): the cylinder's (length r^2 pi), then the sphere's (r^3 4/3 pi).
double CapsuleVolume(const Maths::Capsule& capsule)
{
    double dx = static_cast<double>(capsule.line.end.x) - capsule.line.start.x;
    double dy = static_cast<double>(capsule.line.end.y) - capsule.line.start.y;
    double dz = static_cast<double>(capsule.line.end.z) - capsule.line.start.z;
    double length = std::sqrt(dz * dz + dx * dx + dy * dy);
    double r = capsule.radius;
    return length * r * r * static_cast<double>(3.14159274f) + r * r * r * static_cast<double>(4.18879032f);
}

// The array of `count` volumes, zeroed (no memory: a crash, as the original).
CBoneVolume** NewVolumeArray(uint32_t count)
{
    auto* volumes = static_cast<CBoneVolume**>(MemSys::Alloc(count * 4));
    std::memset(volumes, 0, count * 4);
    return volumes;
}

} // namespace

// 0x0048B330
int CBoneVolume::IntersectsSegment(const Maths::Seg* segment, float* s)
{
    return m_capsule.IntersectsSegment(*segment, s, nullptr, nullptr);
}

// 0x0048B350
CBoneVolumes* CBoneVolumes::MakeNew(uint32_t count)
{
    m_num = count;
    mp_volumes = nullptr;
    m_users = 0;
    if (count)
        mp_volumes = NewVolumeArray(m_num);
    return this;
}

// 0x0048BEA0
void CBoneVolumes::Enrol(uint32_t count)
{
    int users = m_users + 1;
    m_num = count;
    mp_volumes = nullptr;
    m_users = users;
    if (count)
        mp_volumes = NewVolumeArray(m_num);
    Table().StoreEntry(count, this);
}

// 0x0048B3C0 (the table's lookup, inlined)
CBoneVolumes* FindBoneVolumes(uint32_t count) { return static_cast<CBoneVolumes*>(Table().FetchEntry(count)); }

// 0x0048B400
void ReleaseBoneVolumes(uint32_t count)
{
    CBoneVolumes* volumes = FindBoneVolumes(count);
    if (--volumes->m_users == 0)
        Table().EraseEntry(count);
}

// 0x0048B430 (no descriptions, or no memory for a volume: a crash, as the original)
void CBoneVolumes::Fill(CRigData* data)
{
    Scripts::CArray* descriptions = Scripts::FetchList(kBoneDescriptions);
    for (uint32_t i = 1; i < m_num; ++i) {
        if (mp_volumes[i])
            continue;
        for (uint32_t j = 0; j < descriptions->m_extent; ++j) {
            uint32_t bone = 0;
            reinterpret_cast<Scripts::CStruct*>(descriptions->FetchPart(j))->ReadHashValue(kBone, &bone, 1);
            if (bone != data->m_names[i])
                continue;
            float radius = kDefaultRadius;
            auto* volume = static_cast<CBoneVolume*>(MemSys::Alloc(sizeof(CBoneVolume)));
            if (volume) {
                volume->m_vtable = kVolumeVtable;
                new (&volume->m_capsule) Maths::Capsule();
            }
            mp_volumes[i] = volume;
            reinterpret_cast<Scripts::CStruct*>(descriptions->FetchPart(j))->FetchFloat(kRadius, &radius, 1);
            mp_volumes[i]->m_capsule.radius = radius;
        }
    }
}

// 0x0048B580
uint32_t CBoneVolumes::TestLine(const Maths::Seg* segment, CRig* skeleton, const Maths::Mat4* world, uint32_t bone,
                                float scale, float extraRadius, float* distance, float* t)
{
    CRigData* data = skeleton->m_payload;
    Maths::Mat4* inverseBinds = data->GetInverseBindMatrices();
    if (bone) {
        int index = skeleton->FetchBoneSlot(bone);
        CJoint* boneState = skeleton->GetBone(index);
        Maths::Mat4 pose = skeleton->m_matrixArray[index];
        Maths::Mat4 at, parent;
        WorldMatrix(&at, inverseBinds[index], pose, world);
        pose = *boneState->parent;
        ParentWorldMatrix(&parent, data, inverseBinds, index, pose, world);
        CBoneVolume* volume = mp_volumes[index];
        float radius = volume ? volume->m_capsule.radius : 0.0f;
        Maths::Capsule capsule = Between(at.row[3], parent.row[3], radius);
        float distanceSquared;
        if (!capsule.IntersectsSegment(*segment, nullptr, t, &distanceSquared))
            return 0;
        if (distance)
            *distance = static_cast<float>(std::sqrt(static_cast<double>(distanceSquared)));
        return bone;
    }
    float nearest = 0.0f;
    float nearestT = 0.0f;
    uint32_t hit = 0;
    Maths::Vec4* positions = BonePositions();
    for (int i = 0; i < skeleton->GetNumBones(); ++i) {
        CJoint* boneState = skeleton->GetBone(i);
        Maths::Mat4 at;
        WorldMatrix(&at, inverseBinds[i], skeleton->m_matrixArray[i], world);
        positions[i] = at.row[3];
        if (i == 0)
            continue;
        Maths::Vec4 parent = positions[data->FetchParentSlot(i)];
        CBoneVolume* volume = mp_volumes[i];
        if (!volume)
            continue;
        double radius = static_cast<double>(LargestScale(boneState)) * volume->m_capsule.radius;
        if (radius < 0.0 || radius == 0.0) // (NaN goes on)
            continue;
        if (scale > 0.0f)
            radius *= scale;
        Maths::Capsule capsule = Between(at.row[3], parent, static_cast<float>(radius + extraRadius));
        float hitT, distanceSquared;
        if (capsule.IntersectsSegment(*segment, nullptr, &hitT, &distanceSquared) &&
            (!hit || distanceSquared < nearest)) {
            hit = boneState->name;
            nearest = distanceSquared;
            nearestT = hitT;
        }
    }
    if (distance)
        *distance = static_cast<float>(std::sqrt(static_cast<double>(nearest)));
    if (t)
        *t = nearestT;
    return hit;
}

// 0x0048BC40
bool CBoneVolumes::GetBoneVolume(CRig* skeleton, const Maths::Mat4* world, uint32_t bone, float* volume)
{
    CRigData* data = skeleton->m_payload;
    Maths::Mat4* inverseBinds = data->GetInverseBindMatrices();
    int index = skeleton->FetchBoneSlot(bone);
    CJoint* boneState = skeleton->GetBone(index);
    Maths::Mat4 pose = skeleton->m_matrixArray[index];
    Maths::Mat4 at, parent;
    WorldMatrix(&at, inverseBinds[index], pose, world);
    pose = *boneState->parent;
    ParentWorldMatrix(&parent, data, inverseBinds, index, pose, world);
    float radius = 0.0f;
    if (CBoneVolume* own = mp_volumes[index])
        radius = LargestScale(boneState) * own->m_capsule.radius;
    *volume = static_cast<float>(CapsuleVolume(Between(at.row[3], parent.row[3], radius)));
    return true;
}

namespace {
static VtableSlot kVolumeVtable_slots[] = {&Gfx2D::CBoneVolume::IntersectsSegment};
void** const kVolumeVtable = reinterpret_cast<void**>(kVolumeVtable_slots);
} // namespace

} // namespace Gfx

namespace Gfx2D {

// 0x00640AE0 (a static initialiser): the bone volumes' table, 256 buckets.
void ConstructBoneVolumeTable() { new (&g_boneVolumeTable) ListKit::KeyMap(8); }

} // namespace Gfx
