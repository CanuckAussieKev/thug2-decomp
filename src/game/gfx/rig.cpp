// Skeletons: see rig.h. The matrix products are the statically linked D3DXMatrixMultiply's (a library
// function, not decompiled); the x87 code is followed where it keeps values unrounded.

#include "game/gfx/rig.h"
#include "game/gfx/d3dx.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/anim_blend.h"
#include "game/gfx/debug_lines.h"
#include "game/obj/cutscene.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"

namespace Gfx2D {

namespace {

extern void** const kSkeletonDataVtable;
constexpr uint32_t kLocalScaleExceptions = 0x20D9AC2F; // a script array of bone names
constexpr uint32_t kNotLocal = 0x9E3A09FC;
constexpr uint32_t kMirroredBone = 0x580C0963;
constexpr uint32_t kMirroredBoneScale = 0xDFEE29AC;
constexpr uint32_t kTurnBone = 0x9F173EEB;

// out = a * b (D3DXMatrixMultiply, 0x005BFA8C).
void Multiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

void SetIdentity(Maths::Mat4& m)
{
    m.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
}
// A bone collapsed to a point: the identity's rotation rows replaced by (0, 0, 0, 1).
void SetCollapsed(Maths::Mat4& m)
{
    SetIdentity(m);
    m.row[0] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[1] = {0.0f, 0.0f, 0.0f, 1.0f};
    m.row[2] = {0.0f, 0.0f, 0.0f, 1.0f};
}
bool ZeroScale(const Maths::Vec4& scale) { return scale.x == 0.0f && scale.y == 0.0f && scale.z == 0.0f; }

// Whether `name` is in the script array 0x20D9AC2F.
bool InExceptions(uint32_t name)
{
    bool found = false;
    Scripts::CArray* array = Scripts::FetchList(kLocalScaleExceptions);
    if (array) {
        for (uint32_t i = 0; i < array->m_extent; ++i) {
            if (array->FetchPart(i) == name)
                found = true;
        }
    }
    return found;
}

CJoint* FindBone(CRig* skeleton, uint32_t name)
{
    for (int i = 0; i < skeleton->m_boneTotal; ++i) {
        if (skeleton->m_boneArray[i].name == name)
            return &skeleton->m_boneArray[i];
    }
    return nullptr;
}

// A new bone array (null without memory) with each bone cleared, scale 1.
CJoint* NewBones(int count)
{
    auto* bones = static_cast<CJoint*>(MemSys::Alloc(static_cast<uint32_t>(count) * sizeof(CJoint)));
    if (!bones)
        return nullptr;
    for (int i = 0; i < count; ++i)
        bones[i] = {0, nullptr, 0, {1.0f, 1.0f, 1.0f, 1.0f}};
    return bones;
}

// The bone's matrix in its own space (ReadJointMatrix / GetBonePosition): its inverse bind times its matrix.
Maths::Mat4 BoneSpace(CRig* skeleton, int index)
{
    Maths::Mat4 matrix;
    matrix = skeleton->m_matrixArray[index];
    Maths::Mat4 result;
    result = skeleton->m_payload->m_inverseBind[index];
    result.InvertOrthonormal();
    Multiply(&result, &result, &matrix);
    return result;
}

} // namespace

// 0x004A2C20
CRigData* CRigData::MakeNew()
{
    m_vtable = reinterpret_cast<void**>(kSkeletonDataVtable);
    m_num = 0;
    m_flagBits = 0;
    m_unknown588 = 0;
    m_inverseBind = nullptr;
    reinterpret_cast<uint32_t&>(m_lodDistances[0]) = 0x7F7FC99E;
    return this;
}

// 0x004A3950
CRigData* CRigData::Teardown(unsigned int freeFlags)
{
    m_vtable = reinterpret_cast<void**>(kSkeletonDataVtable);
    if (m_inverseBind) {
        MemSys::Hub::Pull()->Dealloc(m_inverseBind);
        m_inverseBind = nullptr;
    }
    if (m_bindTranslations) {
        MemSys::Hub::Pull()->Dealloc(m_bindTranslations);
        m_bindTranslations = nullptr;
    }
    if (freeFlags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004A2C70
int CRigData::FetchSlotNo(uint32_t name)
{
    for (int i = 0; i < m_num; ++i) {
        if (m_names[i] == name)
            return i;
    }
    return 0;
}

// 0x004A30D0
int CRigData::FetchParentSlot(int index)
{
    return FetchSlotNo(m_parentNames[index]);
}

// 0x004A3100
bool CRigData::IsDescendant(uint32_t bone, uint32_t ancestor)
{
    while (bone > ancestor) {
        uint32_t parent = static_cast<uint32_t>(FetchParentSlot(static_cast<int>(bone)));
        if (parent == ancestor)
            return true;
        bone = parent;
    }
    return false;
}

// 0x004A39C0
bool CRigData::ReadIn(const uint32_t* buffer, uint32_t, bool)
{
    m_flagBits = buffer[1];
    m_num = static_cast<int>(buffer[2]);
    const uint32_t* p = buffer + 3;
    std::memcpy(m_names, p, 4u * m_num);
    p += m_num;
    std::memcpy(m_parentNames, p, 4u * m_num);
    p += m_num;
    std::memcpy(m_flipNames, p, 4u * m_num);
    p += m_num;
    const auto* bind = reinterpret_cast<const float*>(p);
    m_inverseBind = static_cast<Maths::Mat4*>(MemSys::Alloc(static_cast<uint32_t>(m_num) << 6));
    m_bindTranslations = static_cast<Maths::Vec4*>(MemSys::Alloc(static_cast<uint32_t>(m_num) << 4));
    for (int i = 0; i < m_num; ++i, bind += 8) {
        float rotation[4] = {bind[0], bind[1], bind[2], bind[3]};
        Maths::Vec4 translation = {bind[4], bind[5], bind[6], bind[7]};
        m_bindTranslations[i] = translation;
        Maths::Mat4 matrix;
        GameObjects::QuatToMatrix(rotation, &translation.x, &matrix.row[0].x);
        if (i != 0) {
            // (relative to the parent: its bind pose, the inverse of its inverse bind matrix)
            Maths::Mat4 parent;
            parent = m_inverseBind[FetchSlotNo(m_parentNames[i])];
            parent.InvertOrthonormal();
            Multiply(&matrix, &matrix, &parent);
            matrix.row[0].w = 0.0f;
            matrix.row[1].w = 0.0f;
            matrix.row[2].w = 0.0f;
            matrix.row[3].w = 1.0f;
        }
        matrix.InvertOrthonormal();
        m_inverseBind[i] = matrix;
    }
    for (int i = 0; i < m_num; ++i) {
        if (!m_flipNames[i]) {
            m_flipIndices[i] = -1;
            continue;
        }
        int mirror = FetchSlotNo(m_flipNames[i]);
        m_flipIndices[i] = mirror;
        m_flipIndices[mirror] = i;
    }
    return true;
}

// 0x004A3D30
CRig* CRig::MakeNew(CRigData* data)
{
    m_flagBits = 0;
    if (data->m_flagBits & 1)
        m_flagBits = kMirrorData;
    m_boneArray = NewBones(data->m_num);
    m_matrixArray = static_cast<Maths::Mat4*>(MemSys::Alloc(static_cast<uint32_t>(data->m_num) << 6));
    for (int i = 0; i < data->m_num; ++i)
        SetIdentity(m_matrixArray[i]);
    m_boneTotal = data->m_num;
    for (int i = 0; i < data->m_num; ++i)
        m_boneArray[i].name = data->m_names[i]; // (no check for bones without memory, as the original)
    LinkBones(data);
    m_payload = data;
    m_maxLod = 0x1F;
    return this;
}

// 0x004A3E80
CRig* CRig::ConstructCopy(const CRig* other)
{
    m_boneTotal = other->m_payload->m_num;
    m_boneArray = NewBones(m_boneTotal);
    m_matrixArray = static_cast<Maths::Mat4*>(MemSys::Alloc(static_cast<uint32_t>(m_boneTotal) << 6));
    for (int i = 0; i < m_boneTotal; ++i)
        std::memcpy(&m_matrixArray[i], &other->m_matrixArray[i], sizeof(Maths::Mat4));
    for (int i = 0; i < other->m_payload->m_num; ++i)
        m_boneArray[i].name = other->m_payload->m_names[i];
    LinkBones(other->m_payload);
    for (int i = 0; i < m_boneTotal; ++i) {
        m_boneArray[i].flags = other->m_boneArray[i].flags;
        m_boneArray[i].name = other->m_boneArray[i].name;
        m_boneArray[i].scale = other->m_boneArray[i].scale;
    }
    m_payload = other->m_payload;
    m_lod = other->m_lod;
    m_flagBits = other->m_flagBits;
    m_maxLod = other->m_maxLod;
    return this;
}

// 0x004A2CB0
void CRig::DestroyBody()
{
    MemSys::Hub::Pull()->Dealloc(m_boneArray);
    MemSys::Hub::Pull()->Dealloc(m_matrixArray);
}

// 0x004A2B60
int CRig::FetchBoneSlot(uint32_t name)
{
    for (int i = 0; i < m_boneTotal; ++i) {
        if (m_boneArray[i].name == name)
            return i;
    }
    return -1;
}

// 0x004A2BB0
bool CRig::EnableBone(uint32_t name, bool active)
{
    CJoint* bone = FindBone(this, name);
    if (!bone)
        return false;
    bone->flags = (bone->flags & ~CJoint::kInactive) | (active ? 0u : CJoint::kInactive);
    return true;
}

// 0x004A2CE0
void CRig::ResetBoneScales()
{
    for (int i = 0; i < m_boneTotal; ++i) {
        CJoint& bone = m_boneArray[i];
        bone.flags &= ~(CJoint::kScaled | CJoint::kScaledLocal);
        bone.scale = {1.0f, 1.0f, 1.0f, 1.0f};
    }
}

// 0x004A2D20
bool CRig::FetchBoneSizing(uint32_t name, Maths::Vec4* scale)
{
    CJoint* bone = FindBone(this, name);
    if (!bone)
        return false;
    *scale = bone->scale;
    return true;
}

// 0x004A2D80
bool CRig::ChangeBoneSizing(uint32_t name, const Maths::Vec4* scale, bool local)
{
    CJoint* bone = FindBone(this, name);
    if (!bone)
        return false;
    bone->scale = *scale;
    // (exactly 1 compared as bit patterns)
    const auto* bits = reinterpret_cast<const uint32_t*>(scale);
    bool one = bits[0] == 0x3F800000 && bits[1] == 0x3F800000 && bits[2] == 0x3F800000;
    bone->flags &= ~(CJoint::kScaled | CJoint::kScaledLocal);
    if (!one)
        bone->flags |= local ? CJoint::kScaledLocal : CJoint::kScaled;
    return true;
}

// 0x004A2E20
void CRig::CopyBoneScales(const CRig* other, bool checkList)
{
    const CRigData* data = other->m_payload;
    for (int i = 0; i < data->m_num; ++i) {
        uint32_t name = data->m_names[i];
        if (!FindBone(this, name))
            continue;
        Maths::Vec4 scale;
        if (!const_cast<CRig*>(other)->FetchBoneSizing(name, &scale))
            continue;
        bool local = true;
        if (checkList && InExceptions(name))
            local = false;
        ChangeBoneSizing(name, &scale, local);
    }
}

// 0x004A3570
bool CRig::ApplyBoneScales(Scripts::CStruct* params)
{
    if (!params)
        return false;
    int notLocal = 0;
    params->FetchIntValue(kNotLocal, &notLocal, 0);
    bool local = notLocal == 0; // (once a bone of the exceptions is met, false for the rest - as the original)
    for (int i = 0; i < m_boneTotal; ++i) {
        Scripts::VectorValue given;
        if (!params->FetchVec(m_boneArray[i].name, &given, 0))
            continue;
        uint32_t name = m_boneArray[i].name;
        if ((m_flagBits & kMirrorData) && name == kMirroredBone)
            name = kMirroredBoneScale;
        if (InExceptions(name))
            local = false;
        Maths::Vec4 scale = {1.0f, 1.0f, 1.0f, 1.0f};
        if (!FetchBoneSizing(name, &scale))
            continue;
        scale = {given.x * scale.x, given.y * scale.y, given.z * scale.z, 1.0f};
        ChangeBoneSizing(name, &scale, local);
    }
    return true;
}

// 0x004A3090
void CRig::SetLOD(float distance)
{
    if (m_payload) {
        for (uint32_t i = 0; i <= m_maxLod; ++i) {
            if (distance < m_payload->m_lodDistances[i]) {
                m_lod = i;
                return;
            }
        }
    }
    m_lod = m_maxLod;
}

// 0x004A3150
void CRig::LinkBones(CRigData* data)
{
    for (int i = 0; i < data->m_num; ++i) {
        uint32_t parent = data->m_parentNames[i];
        int index = parent ? data->FetchSlotNo(parent) : 0;
        m_boneArray[i].parent = &m_matrixArray[index];
        m_boneArray[i].flags = 0;
    }
}

// 0x004A36E0
bool CRig::ReadJointMatrix(uint32_t name, Maths::Mat4* out)
{
    if (!FindBone(this, name))
        return false;
    Maths::Mat4 result = BoneSpace(this, FetchBoneSlot(name));
    for (int i = 0; i < m_boneTotal; ++i) {
        if (m_boneArray[i].name != kTurnBone)
            continue;
        Maths::Vec4 translation = result.row[3];
        result.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        Maths::Mat4 turn;
        turn = m_payload->m_inverseBind[i];
        Multiply(&result, &turn, &result);
        result.row[3] = translation;
        break;
    }
    *out = result;
    return true;
}

// 0x004A3870
bool CRig::GetBonePosition(uint32_t name, Maths::Vec4* out)
{
    if (!FindBone(this, name))
        return false;
    Maths::Mat4 result = BoneSpace(this, FetchBoneSlot(name));
    *out = result.row[3];
    return true;
}

// 0x004A31E0
void CRig::Blit(const Maths::Mat4* world, uint32_t startColour, uint32_t endColour, int frames)
{
    Maths::Mat4* inverseBind = m_payload->m_inverseBind;
    for (int i = 0; i < m_boneTotal; ++i) {
        Maths::Mat4 matrix = m_matrixArray[i];
        Maths::Mat4 bone = inverseBind[i];
        bone.InvertOrthonormal();
        Multiply(&bone, &bone, &matrix);
        Multiply(&bone, &bone, world);
        matrix = *m_boneArray[i].parent;
        Maths::Mat4 parent;
        if (i == 0)
            SetIdentity(parent);
        else
            parent = inverseBind[m_payload->FetchParentSlot(i)];
        parent.InvertOrthonormal();
        Multiply(&parent, &parent, &matrix);
        Multiply(&parent, &parent, world);
        PushDiagLine(&bone.row[3], &parent.row[3], startColour, endColour, frames);
    }
}

// 0x004A2F20
void QuatToBoneMatrix(Maths::Mat4* out, const Maths::Vec4* translation, const float* q, bool mirror, bool swapZ)
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
void CRig::TickFrame(const float (*rotations)[4], const float (*translations)[4])
{
    uint32_t lodBit = 1u << (m_lod & 31);
    for (int i = 0; i < m_boneTotal; ++i) {
        CJoint& bone = m_boneArray[i];
        Maths::Mat4& matrix = m_matrixArray[i];
        if (bone.flags & CJoint::kInactive)
            continue;
        if (m_payload->m_lodMasks[i] & lodBit) { // (collapsed into its parent at this LOD)
            matrix = *bone.parent;
            continue;
        }
        QuatToBoneMatrix(&matrix, reinterpret_cast<const Maths::Vec4*>(translations[i]), rotations[i], false, false);
        if (bone.flags & CJoint::kScaled) {
            matrix.StretchBy(bone.scale);
            if (ZeroScale(bone.scale)) {
                matrix.row[3] = bone.parent->row[3];
                continue;
            }
        }
        if (i != 0)
            Multiply(&matrix, &matrix, bone.parent);
    }
    Maths::Mat4* inverseBind = m_payload->m_inverseBind;
    for (int i = 0; i < m_boneTotal; ++i) {
        CJoint& bone = m_boneArray[i];
        Maths::Mat4& matrix = m_matrixArray[i];
        if (bone.flags & CJoint::kInactive)
            continue;
        bool collapsed = false;
        if (bone.flags & CJoint::kScaledLocal) {
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
void CRig::TickFrame(const CStance* pose)
{
    TickFrame(pose->rotations, pose->translations);
}

// 0x004A4000
void CRig::CollapseZeroScaledBones()
{
    if (!(m_flagBits & kCollapseZeroScaled))
        return;
    int first = 1;
    while (true) {
        if (first >= m_boneTotal)
            return;
        if (!ZeroScale(m_boneArray[first].scale))
            break;
        ++first;
    }
    Maths::Mat4* root = m_matrixArray;
    Maths::Mat4* scaled = m_matrixArray + first;
    if (!scaled) // (the original checks the address)
        return;
    SetCollapsed(*root);
    root->row[3] = scaled->row[3];
    GetBonePosition(m_boneArray[first].name, &m_matrixArray[0].row[3]);
    for (int i = 0; i < m_boneTotal; ++i) {
        if (!ZeroScale(m_boneArray[i].scale))
            continue;
        SetCollapsed(m_matrixArray[i]);
        m_matrixArray[i].row[3] = m_matrixArray[0].row[3];
    }
}

namespace {
static VtableSlot kSkeletonDataVtable_slots[] = {&Gfx2D::CRigData::Teardown};
void** const kSkeletonDataVtable = reinterpret_cast<void**>(kSkeletonDataVtable_slots);
} // namespace

} // namespace Gfx
