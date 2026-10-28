// Skeletons: see skeleton.h. The matrix products are the statically linked D3DXMatrixMultiply's (a library
// function, not decompiled); the x87 code is followed where it keeps values unrounded.

#include "game/gfx/skeleton.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/anim_blend.h"
#include "game/gfx/debug_lines.h"
#include "game/obj/cutscene.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

constexpr uint32_t kSkeletonDataVtable = 0x00649F48;
constexpr uint32_t kLocalScaleExceptions = 0x20D9AC2F; // a script array of bone names
constexpr uint32_t kNotLocal = 0x9E3A09FC;
constexpr uint32_t kMirroredBone = 0x580C0963;
constexpr uint32_t kMirroredBoneScale = 0xDFEE29AC;
constexpr uint32_t kTurnBone = 0x9F173EEB;

// out = a * b (D3DXMatrixMultiply, 0x005BFA8C).
void Multiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

void SetIdentity(Mth::Matrix& m)
{
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}
// A bone collapsed to a point: the identity's rotation rows replaced by (0, 0, 0, 1).
void SetCollapsed(Mth::Matrix& m)
{
    SetIdentity(m);
    m.row[0] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[1] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[2] = {0.0f, 0.0f, 0.0f, 1.0f};
}
bool ZeroScale(const Mth::Vector& scale) { return scale.x == 0.0f && scale.y == 0.0f && scale.z == 0.0f; }

// Whether `name` is in the script array 0x20D9AC2F.
bool InExceptions(uint32_t name)
{
    bool found = false;
    Script::CArray* array = Script::GetArray(kLocalScaleExceptions);
    if (array) {
        for (uint32_t i = 0; i < array->m_size; ++i) {
            if (array->GetElement(i) == name)
                found = true;
        }
    }
    return found;
}

CBone* FindBone(CSkeleton* skeleton, uint32_t name)
{
    for (int i = 0; i < skeleton->m_numBones; ++i) {
        if (skeleton->mp_bones[i].name == name)
            return &skeleton->mp_bones[i];
    }
    return nullptr;
}

// A new bone array (null without memory) with each bone cleared, scale 1.
CBone* NewBones(int count)
{
    auto* bones = static_cast<CBone*>(Mem::New(static_cast<uint32_t>(count) * sizeof(CBone)));
    if (!bones)
        return nullptr;
    for (int i = 0; i < count; ++i)
        bones[i] = {0, nullptr, 0, {1.0f, 1.0f, 1.0f, 1.0f}};
    return bones;
}

// The bone's matrix in its own space (GetBoneMatrix / GetBonePosition): its inverse bind times its matrix.
Mth::Matrix BoneSpace(CSkeleton* skeleton, int index)
{
    Mth::Matrix matrix;
    matrix = skeleton->mp_matrices[index];
    Mth::Matrix result;
    result = skeleton->mp_data->mp_inverseBind[index];
    result.InvertOrthonormal();
    Multiply(&result, &result, &matrix);
    return result;
}

} // namespace

// 0x004A2C20
CSkeletonData* CSkeletonData::Construct()
{
    m_vtable = reinterpret_cast<void**>(kSkeletonDataVtable);
    m_count = 0;
    m_flags = 0;
    m_unknown588 = 0;
    mp_inverseBind = nullptr;
    reinterpret_cast<uint32_t&>(m_lodDistances[0]) = 0x7F7FC99E;
    return this;
}

// 0x004A3950
CSkeletonData* CSkeletonData::Destroy(unsigned int freeFlags)
{
    m_vtable = reinterpret_cast<void**>(kSkeletonDataVtable);
    if (mp_inverseBind) {
        Mem::Manager::Get()->Free(mp_inverseBind);
        mp_inverseBind = nullptr;
    }
    if (mp_bindTranslations) {
        Mem::Manager::Get()->Free(mp_bindTranslations);
        mp_bindTranslations = nullptr;
    }
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004A2C70
int CSkeletonData::GetIndex(uint32_t name)
{
    for (int i = 0; i < m_count; ++i) {
        if (m_names[i] == name)
            return i;
    }
    return 0;
}

// 0x004A30D0
int CSkeletonData::GetParentIndex(int index)
{
    return GetIndex(m_parentNames[index]);
}

// 0x004A3100
bool CSkeletonData::IsDescendant(uint32_t bone, uint32_t ancestor)
{
    while (bone > ancestor) {
        uint32_t parent = static_cast<uint32_t>(GetParentIndex(static_cast<int>(bone)));
        if (parent == ancestor)
            return true;
        bone = parent;
    }
    return false;
}

// 0x004A39C0
bool CSkeletonData::Load(const uint32_t* buffer, uint32_t, bool)
{
    m_flags = buffer[1];
    m_count = static_cast<int>(buffer[2]);
    const uint32_t* p = buffer + 3;
    std::memcpy(m_names, p, 4u * m_count);
    p += m_count;
    std::memcpy(m_parentNames, p, 4u * m_count);
    p += m_count;
    std::memcpy(m_flipNames, p, 4u * m_count);
    p += m_count;
    const auto* bind = reinterpret_cast<const float*>(p);
    mp_inverseBind = static_cast<Mth::Matrix*>(Mem::New(static_cast<uint32_t>(m_count) << 6));
    mp_bindTranslations = static_cast<Mth::Vector*>(Mem::New(static_cast<uint32_t>(m_count) << 4));
    for (int i = 0; i < m_count; ++i, bind += 8) {
        float rotation[4] = {bind[0], bind[1], bind[2], bind[3]};
        Mth::Vector translation = {bind[4], bind[5], bind[6], bind[7]};
        mp_bindTranslations[i] = translation;
        Mth::Matrix matrix;
        Obj::QuatToMatrix(rotation, &translation.x, &matrix.row[0].x);
        if (i != 0) {
            // (relative to the parent: its bind pose, the inverse of its inverse bind matrix)
            Mth::Matrix parent;
            parent = mp_inverseBind[GetIndex(m_parentNames[i])];
            parent.InvertOrthonormal();
            Multiply(&matrix, &matrix, &parent);
            matrix.row[0].w = 0.0f;
            matrix.row[1].w = 0.0f;
            matrix.row[2].w = 0.0f;
            matrix.row[3].w = 1.0f;
        }
        matrix.InvertOrthonormal();
        mp_inverseBind[i] = matrix;
    }
    for (int i = 0; i < m_count; ++i) {
        if (!m_flipNames[i]) {
            m_flipIndices[i] = -1;
            continue;
        }
        int mirror = GetIndex(m_flipNames[i]);
        m_flipIndices[i] = mirror;
        m_flipIndices[mirror] = i;
    }
    return true;
}

// 0x004A3D30
CSkeleton* CSkeleton::Construct(CSkeletonData* data)
{
    m_flags = 0;
    if (data->m_flags & 1)
        m_flags = kMirrorData;
    mp_bones = NewBones(data->m_count);
    mp_matrices = static_cast<Mth::Matrix*>(Mem::New(static_cast<uint32_t>(data->m_count) << 6));
    for (int i = 0; i < data->m_count; ++i)
        SetIdentity(mp_matrices[i]);
    m_numBones = data->m_count;
    for (int i = 0; i < data->m_count; ++i)
        mp_bones[i].name = data->m_names[i]; // (no check for bones without memory, as the original)
    LinkBones(data);
    mp_data = data;
    m_maxLod = 0x1F;
    return this;
}

// 0x004A3E80
CSkeleton* CSkeleton::ConstructCopy(const CSkeleton* other)
{
    m_numBones = other->mp_data->m_count;
    mp_bones = NewBones(m_numBones);
    mp_matrices = static_cast<Mth::Matrix*>(Mem::New(static_cast<uint32_t>(m_numBones) << 6));
    for (int i = 0; i < m_numBones; ++i)
        std::memcpy(&mp_matrices[i], &other->mp_matrices[i], sizeof(Mth::Matrix));
    for (int i = 0; i < other->mp_data->m_count; ++i)
        mp_bones[i].name = other->mp_data->m_names[i];
    LinkBones(other->mp_data);
    for (int i = 0; i < m_numBones; ++i) {
        mp_bones[i].flags = other->mp_bones[i].flags;
        mp_bones[i].name = other->mp_bones[i].name;
        mp_bones[i].scale = other->mp_bones[i].scale;
    }
    mp_data = other->mp_data;
    m_lod = other->m_lod;
    m_flags = other->m_flags;
    m_maxLod = other->m_maxLod;
    return this;
}

// 0x004A2CB0
void CSkeleton::DestroyBody()
{
    Mem::Manager::Get()->Free(mp_bones);
    Mem::Manager::Get()->Free(mp_matrices);
}

// 0x004A2B60
int CSkeleton::GetBoneIndex(uint32_t name)
{
    for (int i = 0; i < m_numBones; ++i) {
        if (mp_bones[i].name == name)
            return i;
    }
    return -1;
}

// 0x004A2BB0
bool CSkeleton::SetBoneActive(uint32_t name, bool active)
{
    CBone* bone = FindBone(this, name);
    if (!bone)
        return false;
    bone->flags = (bone->flags & ~CBone::kInactive) | (active ? 0u : CBone::kInactive);
    return true;
}

// 0x004A2CE0
void CSkeleton::ResetBoneScales()
{
    for (int i = 0; i < m_numBones; ++i) {
        CBone& bone = mp_bones[i];
        bone.flags &= ~(CBone::kScaled | CBone::kScaledLocal);
        bone.scale = {1.0f, 1.0f, 1.0f, 1.0f};
    }
}

// 0x004A2D20
bool CSkeleton::GetBoneScale(uint32_t name, Mth::Vector* scale)
{
    CBone* bone = FindBone(this, name);
    if (!bone)
        return false;
    *scale = bone->scale;
    return true;
}

// 0x004A2D80
bool CSkeleton::SetBoneScale(uint32_t name, const Mth::Vector* scale, bool local)
{
    CBone* bone = FindBone(this, name);
    if (!bone)
        return false;
    bone->scale = *scale;
    // (exactly 1 compared as bit patterns)
    const auto* bits = reinterpret_cast<const uint32_t*>(scale);
    bool one = bits[0] == 0x3F800000 && bits[1] == 0x3F800000 && bits[2] == 0x3F800000;
    bone->flags &= ~(CBone::kScaled | CBone::kScaledLocal);
    if (!one)
        bone->flags |= local ? CBone::kScaledLocal : CBone::kScaled;
    return true;
}

// 0x004A2E20
void CSkeleton::CopyBoneScales(const CSkeleton* other, bool checkList)
{
    const CSkeletonData* data = other->mp_data;
    for (int i = 0; i < data->m_count; ++i) {
        uint32_t name = data->m_names[i];
        if (!FindBone(this, name))
            continue;
        Mth::Vector scale;
        if (!const_cast<CSkeleton*>(other)->GetBoneScale(name, &scale))
            continue;
        bool local = true;
        if (checkList && InExceptions(name))
            local = false;
        SetBoneScale(name, &scale, local);
    }
}

// 0x004A3570
bool CSkeleton::ApplyBoneScales(Script::CStruct* params)
{
    if (!params)
        return false;
    int notLocal = 0;
    params->GetInteger(kNotLocal, &notLocal, 0);
    bool local = notLocal == 0; // (once a bone of the exceptions is met, false for the rest - as the original)
    for (int i = 0; i < m_numBones; ++i) {
        Script::VectorValue given;
        if (!params->GetVector(mp_bones[i].name, &given, 0))
            continue;
        uint32_t name = mp_bones[i].name;
        if ((m_flags & kMirrorData) && name == kMirroredBone)
            name = kMirroredBoneScale;
        if (InExceptions(name))
            local = false;
        Mth::Vector scale = {1.0f, 1.0f, 1.0f, 1.0f};
        if (!GetBoneScale(name, &scale))
            continue;
        scale = {given.x * scale.x, given.y * scale.y, given.z * scale.z, 1.0f};
        SetBoneScale(name, &scale, local);
    }
    return true;
}

// 0x004A3090
void CSkeleton::SetLOD(float distance)
{
    if (mp_data) {
        for (uint32_t i = 0; i <= m_maxLod; ++i) {
            if (distance < mp_data->m_lodDistances[i]) {
                m_lod = i;
                return;
            }
        }
    }
    m_lod = m_maxLod;
}

// 0x004A3150
void CSkeleton::LinkBones(CSkeletonData* data)
{
    for (int i = 0; i < data->m_count; ++i) {
        uint32_t parent = data->m_parentNames[i];
        int index = parent ? data->GetIndex(parent) : 0;
        mp_bones[i].parent = &mp_matrices[index];
        mp_bones[i].flags = 0;
    }
}

// 0x004A36E0
bool CSkeleton::GetBoneMatrix(uint32_t name, Mth::Matrix* out)
{
    if (!FindBone(this, name))
        return false;
    Mth::Matrix result = BoneSpace(this, GetBoneIndex(name));
    for (int i = 0; i < m_numBones; ++i) {
        if (mp_bones[i].name != kTurnBone)
            continue;
        Mth::Vector translation = result.row[3];
        result.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        Mth::Matrix turn;
        turn = mp_data->mp_inverseBind[i];
        Multiply(&result, &turn, &result);
        result.row[3] = translation;
        break;
    }
    *out = result;
    return true;
}

// 0x004A3870
bool CSkeleton::GetBonePosition(uint32_t name, Mth::Vector* out)
{
    if (!FindBone(this, name))
        return false;
    Mth::Matrix result = BoneSpace(this, GetBoneIndex(name));
    *out = result.row[3];
    return true;
}

// 0x004A31E0
void CSkeleton::Draw(const Mth::Matrix* world, uint32_t startColour, uint32_t endColour, int frames)
{
    Mth::Matrix* inverseBind = mp_data->mp_inverseBind;
    for (int i = 0; i < m_numBones; ++i) {
        Mth::Matrix matrix = mp_matrices[i];
        Mth::Matrix bone = inverseBind[i];
        bone.InvertOrthonormal();
        Multiply(&bone, &bone, &matrix);
        Multiply(&bone, &bone, world);
        matrix = *mp_bones[i].parent;
        Mth::Matrix parent;
        if (i == 0)
            SetIdentity(parent);
        else
            parent = inverseBind[mp_data->GetParentIndex(i)];
        parent.InvertOrthonormal();
        Multiply(&parent, &parent, &matrix);
        Multiply(&parent, &parent, world);
        AddDebugLine(&bone.row[3], &parent.row[3], startColour, endColour, frames);
    }
}

// 0x004A2F20
void QuatToBoneMatrix(Mth::Matrix* out, const Mth::Vector* translation, const float* q, bool mirror, bool swapZ)
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double xx = x * x, yy = y * y, zz = z * z;
    double zy = z * y, zx = z * x, yx = y * x;
    double xw = static_cast<float>(-(x * w)); // (stored as floats)
    double yw = static_cast<float>(-(y * w));
    double zw = -(z * w); // (kept)
    auto* m = &out->row[0].x;
    m[0] = static_cast<float>(1.0 - ((zz + yy) + (zz + yy)));
    m[1] = static_cast<float>((zw + yx) + (zw + yx));
    m[2] = static_cast<float>((zx - yw) + (zx - yw));
    m[3] = 0.0f;
    m[4] = static_cast<float>((yx - zw) + (yx - zw));
    m[5] = static_cast<float>(1.0 - ((zz + xx) + (zz + xx)));
    m[6] = static_cast<float>((xw + zy) + (xw + zy));
    m[7] = 0.0f;
    m[8] = static_cast<float>((yw + zx) + (yw + zx));
    m[9] = static_cast<float>((zy - xw) + (zy - xw));
    m[10] = static_cast<float>(1.0 - ((yy + xx) + (yy + xx)));
    m[11] = 0.0f;
    m[12] = translation->x;
    m[13] = translation->y;
    m[14] = translation->z;
    m[15] = 1.0f;
    if (mirror) {
        m[0] = -m[0];
        if (swapZ) {
            for (int i : {5, 6, 8, 12})
                m[i] = -m[i];
        } else {
            for (int i : {1, 2, 4, 5, 6})
                m[i] = -m[i];
        }
    } else if (swapZ) {
        for (int i : {1, 2, 4, 8, 12})
            m[i] = -m[i];
    }
}

// The original's convention: EAX = out, EDX = the translation, BL = swapZ; [ESP+4] the quaternion, [ESP+8] mirror
// (a byte) - popped by the caller.
__declspec(naked) void QuatToBoneMatrixStub()
{
    __asm {
        movzx ecx, bl
        push ecx                     // swapZ
        movzx ecx, byte ptr [esp + 0xC]
        push ecx                     // mirror
        push dword ptr [esp + 0xC]   // the quaternion
        push edx
        push eax
        call QuatToBoneMatrix
        add esp, 0x14
        ret
    }
}

// 0x004A4240
void CSkeleton::Update(const float (*rotations)[4], const float (*translations)[4])
{
    uint32_t lodBit = 1u << (m_lod & 31);
    for (int i = 0; i < m_numBones; ++i) {
        CBone& bone = mp_bones[i];
        Mth::Matrix& matrix = mp_matrices[i];
        if (bone.flags & CBone::kInactive)
            continue;
        if (mp_data->m_lodMasks[i] & lodBit) { // (collapsed into its parent at this LOD)
            matrix = *bone.parent;
            continue;
        }
        QuatToBoneMatrix(&matrix, reinterpret_cast<const Mth::Vector*>(translations[i]), rotations[i], false, false);
        if (bone.flags & CBone::kScaled) {
            matrix.Scale(bone.scale);
            if (ZeroScale(bone.scale)) {
                matrix.row[3] = bone.parent->row[3];
                continue;
            }
        }
        if (i != 0)
            Multiply(&matrix, &matrix, bone.parent);
    }
    Mth::Matrix* inverseBind = mp_data->mp_inverseBind;
    for (int i = 0; i < m_numBones; ++i) {
        CBone& bone = mp_bones[i];
        Mth::Matrix& matrix = mp_matrices[i];
        if (bone.flags & CBone::kInactive)
            continue;
        bool collapsed = false;
        if (bone.flags & CBone::kScaledLocal) {
            for (int k = 0; k < 4; ++k) {
                (&matrix.row[0].x)[k] *= bone.scale.x;
                (&matrix.row[1].x)[k] *= bone.scale.y;
                (&matrix.row[2].x)[k] *= bone.scale.z;
            }
            if (ZeroScale(bone.scale)) {
                matrix.row[3] = inverseBind[i].row[3];
                collapsed = true;
            }
        }
        if (!collapsed)
            Multiply(&matrix, &inverseBind[i], &matrix);
        matrix.row[0].w = 0.0f;
        matrix.row[1].w = 0.0f;
        matrix.row[2].w = 0.0f;
    }
    CollapseZeroScaledBones();
}

// 0x004A44E0
void CSkeleton::Update(const CPose* pose)
{
    Update(pose->rotations, pose->translations);
}

// 0x004A4000
void CSkeleton::CollapseZeroScaledBones()
{
    if (!(m_flags & kCollapseZeroScaled))
        return;
    int first = 1;
    while (true) {
        if (first >= m_numBones)
            return;
        if (!ZeroScale(mp_bones[first].scale))
            break;
        ++first;
    }
    Mth::Matrix* root = mp_matrices;
    Mth::Matrix* scaled = mp_matrices + first;
    if (!scaled) // (the original checks the address)
        return;
    SetCollapsed(*root);
    root->row[3] = scaled->row[3];
    GetBonePosition(mp_bones[first].name, &mp_matrices[0].row[3]);
    for (int i = 0; i < m_numBones; ++i) {
        if (!ZeroScale(mp_bones[i].scale))
            continue;
        SetCollapsed(mp_matrices[i]);
        mp_matrices[i].row[3] = mp_matrices[0].row[3];
    }
}

} // namespace Gfx

REPLACE(0x004A2C20, &Gfx::CSkeletonData::Construct);
REPLACE(0x004A3950, &Gfx::CSkeletonData::Destroy);
REPLACE(0x004A39C0, &Gfx::CSkeletonData::Load);
REPLACE(0x004A2C50, &Gfx::CSkeletonData::GetParentName);
REPLACE(0x004A2C60, &Gfx::CSkeletonData::GetFlipIndex);
REPLACE(0x004A2C70, &Gfx::CSkeletonData::GetIndex);
REPLACE(0x004A30D0, &Gfx::CSkeletonData::GetParentIndex);
REPLACE(0x004A3100, &Gfx::CSkeletonData::IsDescendant);
REPLACE(0x004A2CA0, &Gfx::CSkeletonData::GetInverseBindMatrices);
REPLACE(0x004A3D30, &Gfx::CSkeleton::Construct);
REPLACE(0x004A3E80, &Gfx::CSkeleton::ConstructCopy);
REPLACE(0x004A2CB0, &Gfx::CSkeleton::DestroyBody);
REPLACE_SOURCE_ONLY(0x004A2BA0, &Gfx::CSkeleton::GetNumBones); // 4 bytes (folded: other classes' +0x14 getters too)
REPLACE(0x004A2B60, &Gfx::CSkeleton::GetBoneIndex);
REPLACE(0x004A2B90, &Gfx::CSkeleton::GetBone);
REPLACE(0x004A2BB0, &Gfx::CSkeleton::SetBoneActive);
REPLACE(0x004A2CE0, &Gfx::CSkeleton::ResetBoneScales);
REPLACE(0x004A2D20, &Gfx::CSkeleton::GetBoneScale);
REPLACE(0x004A2D80, &Gfx::CSkeleton::SetBoneScale);
REPLACE(0x004A2E20, &Gfx::CSkeleton::CopyBoneScales);
REPLACE(0x004A3570, &Gfx::CSkeleton::ApplyBoneScales);
REPLACE(0x004A3090, &Gfx::CSkeleton::SetLOD);
REPLACE(0x004A3150, &Gfx::CSkeleton::LinkBones);
REPLACE(0x004A36E0, &Gfx::CSkeleton::GetBoneMatrix);
REPLACE(0x004A3870, &Gfx::CSkeleton::GetBonePosition);
REPLACE(0x004A31E0, &Gfx::CSkeleton::Draw);
REPLACE_UNCHECKED(0x004A2F20, Gfx::QuatToBoneMatrixStub); // (registers and caller-popped stack arguments)
REPLACE(0x004A4240, static_cast<void (Gfx::CSkeleton::*)(const float (*)[4], const float (*)[4])>(&Gfx::CSkeleton::Update));
REPLACE(0x004A44E0, static_cast<void (Gfx::CSkeleton::*)(const Gfx::CPose*)>(&Gfx::CSkeleton::Update));
REPLACE(0x004A4000, &Gfx::CSkeleton::CollapseZeroScaledBones);
