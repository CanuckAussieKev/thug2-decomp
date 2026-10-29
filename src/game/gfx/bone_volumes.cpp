// Bone volumes: see bone_volumes.h. From the disassembly of 0x0048B330-0x0048BF0D.

#include "game/gfx/bone_volumes.h"

#include <cmath>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/skeleton.h"
#include "game/lst/hashtable.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

void** const kVolumeVtable = reinterpret_cast<void**>(0x00648988);

constexpr uint32_t kBoneDescriptions = 0x00E7085E; // (name unknown: the global array of bone descriptions)
constexpr uint32_t kBone = 0xCAB94088;              // bone
constexpr uint32_t kRadius = 0xC48391A5;            // radius
constexpr float kDefaultRadius = 3.0f;              // 0x40400000

// The shared sets, by bone count.
Lst::HashTable& Table() { return GameVariable<Lst::HashTable>(0x006FA308); }
// Each bone's world position as TestLine found it (64 of them).
Mth::Vector* BonePositions() { return &GameVariable<Mth::Vector>(0x006FA320); }

// D3DXMatrixMultiply (0x005BFA8C, the D3DX library's): out = a * b.
void Multiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

// A bone's matrix in the world: its bind pose (the inverse bind matrix inverted) times `pose`, times `world`.
void WorldMatrix(Mth::Matrix* out, const Mth::Matrix& inverseBind, const Mth::Matrix& pose, const Mth::Matrix* world)
{
    *out = inverseBind;
    out->InvertOrthonormal();
    Multiply(out, out, &pose);
    Multiply(out, out, world);
}
// Bone `index`'s parent's world matrix (the root's: the identity's), with the parent's pose `parentPose`.
void ParentWorldMatrix(Mth::Matrix* out, CSkeletonData* data, const Mth::Matrix* inverseBinds, int index,
                       const Mth::Matrix& parentPose, const Mth::Matrix* world)
{
    if (index == 0)
        out->Ident();
    else
        *out = inverseBinds[data->GetParentIndex(index)];
    out->InvertOrthonormal();
    Multiply(out, out, &parentPose);
    Multiply(out, out, world);
}

// The largest of a bone's three scales (a NaN loses each comparison).
float LargestScale(const CBone* bone)
{
    float largest = bone->scale.x > bone->scale.y ? bone->scale.x : bone->scale.y;
    return largest > bone->scale.z ? largest : bone->scale.z;
}

// The capsule from `from` to `to` (all four components), of `radius`.
Mth::Capsule Between(const Mth::Vector& from, const Mth::Vector& to, float radius)
{
    Mth::Vector offset = {to.x - from.x, to.y - from.y, to.z - from.z, to.w - from.w};
    return Mth::Capsule(from, offset, radius);
}

// A capsule's volume as the original's 0x00403390 leaves it in ST0 (to 53 bits, rounded only when stored - our
// Capsule::Volume rounds to a float on the way): the cylinder's (length r^2 pi), then the sphere's (r^3 4/3 pi).
double CapsuleVolume(const Mth::Capsule& capsule)
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
    auto* volumes = static_cast<CBoneVolume**>(Mem::New(count * 4));
    std::memset(volumes, 0, count * 4);
    return volumes;
}

} // namespace

// 0x0048B330
int CBoneVolume::IntersectsSegment(const Mth::Line* segment, float* s)
{
    return m_capsule.IntersectsSegment(*segment, s, nullptr, nullptr);
}

// 0x0048B350
CBoneVolumes* CBoneVolumes::Construct(uint32_t count)
{
    m_count = count;
    mp_volumes = nullptr;
    m_users = 0;
    if (count)
        mp_volumes = NewVolumeArray(m_count);
    return this;
}

// 0x0048BEA0
void CBoneVolumes::Register(uint32_t count)
{
    int users = m_users + 1;
    m_count = count;
    mp_volumes = nullptr;
    m_users = users;
    if (count)
        mp_volumes = NewVolumeArray(m_count);
    Table().PutItem(count, this);
}

// 0x0048B3C0 (the table's lookup, inlined)
CBoneVolumes* FindBoneVolumes(uint32_t count) { return static_cast<CBoneVolumes*>(Table().GetItem(count)); }

// 0x0048B400
void ReleaseBoneVolumes(uint32_t count)
{
    CBoneVolumes* volumes = FindBoneVolumes(count);
    if (--volumes->m_users == 0)
        Table().FlushItem(count);
}

// 0x0048B430 (no descriptions, or no memory for a volume: a crash, as the original)
void CBoneVolumes::Fill(CSkeletonData* data)
{
    Script::CArray* descriptions = Script::GetArray(kBoneDescriptions);
    for (uint32_t i = 1; i < m_count; ++i) {
        if (mp_volumes[i])
            continue;
        for (uint32_t j = 0; j < descriptions->m_size; ++j) {
            uint32_t bone = 0;
            reinterpret_cast<Script::CStruct*>(descriptions->GetElement(j))->GetChecksum(kBone, &bone, 1);
            if (bone != data->m_names[i])
                continue;
            float radius = kDefaultRadius;
            auto* volume = static_cast<CBoneVolume*>(Mem::New(sizeof(CBoneVolume)));
            if (volume) {
                volume->m_vtable = kVolumeVtable;
                new (&volume->m_capsule) Mth::Capsule();
            }
            mp_volumes[i] = volume;
            reinterpret_cast<Script::CStruct*>(descriptions->GetElement(j))->GetFloat(kRadius, &radius, 1);
            mp_volumes[i]->m_capsule.radius = radius;
        }
    }
}

// 0x0048B580
uint32_t CBoneVolumes::TestLine(const Mth::Line* segment, CSkeleton* skeleton, const Mth::Matrix* world, uint32_t bone,
                                float scale, float extraRadius, float* distance, float* t)
{
    CSkeletonData* data = skeleton->mp_data;
    Mth::Matrix* inverseBinds = data->GetInverseBindMatrices();
    if (bone) {
        int index = skeleton->GetBoneIndex(bone);
        CBone* boneState = skeleton->GetBone(index);
        Mth::Matrix pose = skeleton->mp_matrices[index];
        Mth::Matrix at, parent;
        WorldMatrix(&at, inverseBinds[index], pose, world);
        pose = *boneState->parent;
        ParentWorldMatrix(&parent, data, inverseBinds, index, pose, world);
        CBoneVolume* volume = mp_volumes[index];
        float radius = volume ? volume->m_capsule.radius : 0.0f;
        Mth::Capsule capsule = Between(at.row[3], parent.row[3], radius);
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
    Mth::Vector* positions = BonePositions();
    for (int i = 0; i < skeleton->GetNumBones(); ++i) {
        CBone* boneState = skeleton->GetBone(i);
        Mth::Matrix at;
        WorldMatrix(&at, inverseBinds[i], skeleton->mp_matrices[i], world);
        positions[i] = at.row[3];
        if (i == 0)
            continue;
        Mth::Vector parent = positions[data->GetParentIndex(i)];
        CBoneVolume* volume = mp_volumes[i];
        if (!volume)
            continue;
        double radius = static_cast<double>(LargestScale(boneState)) * volume->m_capsule.radius;
        if (radius < 0.0 || radius == 0.0) // (NaN goes on)
            continue;
        if (scale > 0.0f)
            radius *= scale;
        Mth::Capsule capsule = Between(at.row[3], parent, static_cast<float>(radius + extraRadius));
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
bool CBoneVolumes::GetBoneVolume(CSkeleton* skeleton, const Mth::Matrix* world, uint32_t bone, float* volume)
{
    CSkeletonData* data = skeleton->mp_data;
    Mth::Matrix* inverseBinds = data->GetInverseBindMatrices();
    int index = skeleton->GetBoneIndex(bone);
    CBone* boneState = skeleton->GetBone(index);
    Mth::Matrix pose = skeleton->mp_matrices[index];
    Mth::Matrix at, parent;
    WorldMatrix(&at, inverseBinds[index], pose, world);
    pose = *boneState->parent;
    ParentWorldMatrix(&parent, data, inverseBinds, index, pose, world);
    float radius = 0.0f;
    if (CBoneVolume* own = mp_volumes[index])
        radius = LargestScale(boneState) * own->m_capsule.radius;
    *volume = static_cast<float>(CapsuleVolume(Between(at.row[3], parent.row[3], radius)));
    return true;
}

} // namespace Gfx

REPLACE(0x0048B330, &Gfx::CBoneVolume::IntersectsSegment);
REPLACE(0x0048B350, &Gfx::CBoneVolumes::Construct);
REPLACE(0x0048B3C0, Gfx::FindBoneVolumes);
REPLACE(0x0048B400, Gfx::ReleaseBoneVolumes);
REPLACE(0x0048B430, &Gfx::CBoneVolumes::Fill);
REPLACE(0x0048B580, &Gfx::CBoneVolumes::TestLine);
REPLACE(0x0048BC40, &Gfx::CBoneVolumes::GetBoneVolume);
REPLACE(0x0048BEA0, &Gfx::CBoneVolumes::Register);
