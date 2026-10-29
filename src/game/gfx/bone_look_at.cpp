// An object's bones turned to look at something (see bone_look_at.h). From the disassembly of 0x00487D40-0x00488DA1.

#include "game/gfx/bone_look_at.h"

#include <cmath>

#include "game/core/mem.h"
#include "game/gfx/anim_blend.h"
#include "game/gfx/skeleton.h"
#include "game/mth/x87_math.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

using Script::CStruct;

constexpr uint32_t kVtable = 0x006488D8;
constexpr uint32_t kNoBone = 0xFFFFFFFF;
constexpr uint32_t kSkeletonComponent = 0x222756D5;
// Member functions (names unknown)
constexpr uint32_t kOn = 0x20D24E9A;
constexpr uint32_t kIsOn = 0x027B2B1C;
constexpr uint32_t kOff = 0x4599FB80;
constexpr uint32_t kSettings = 0xB040E943;
// Parameters
constexpr uint32_t kIndex = 0x7F8C98FE;      // index
constexpr uint32_t kId = 0x40C698AF;         // id
constexpr uint32_t kAll = 0xC4E78E22;        // all
constexpr uint32_t kOnFlag = 0x0C5F37CD;     // (name unknown) made active
constexpr uint32_t kOffFlag = 0xB0BAB2AE;    // (name unknown) made inactive
constexpr uint32_t kBone = 0xCAB94088;       // bone
constexpr uint32_t kTargetObject = 0x4ACEE3F7; // (name unknown) the object looked at
constexpr uint32_t kTargetPos = 0xDC741BAD;  // (name unknown) a point looked at
constexpr uint32_t kOffset = 0x1F07CF31;     // (name unknown) the offset from the object
constexpr uint32_t kAxis = 0x7AF07905;       // Axis
constexpr uint32_t kWeight = 0xF832AABE;     // weight
constexpr uint32_t kMinAngles = 0x0B405328;  // min_angles
constexpr uint32_t kMaxAngles = 0x0A4CC99C;  // max_angles
constexpr uint32_t kNoClamp = 0xD4A8CD5F;    // (name unknown) each angle past its limits zeroed instead

constexpr float kQuarterTurn = 1.57079637f;  // 0x00645484
constexpr float kEaseBelow = 0.923879027f;   // 0x006488F8: a turn this close to the last one isn't eased
constexpr float kEaseFactor = 0.216478556f;  // 0x006488F4

float Epsilon() { return GameVariable<float>(0x0066D520); }

// x, y and z of a vector scaled to length 1 when longer than the epsilon (from the sum (x x + z z) + y y, the
// reciprocal unrounded).
void NormalizeXZY(float& x, float& y, float& z)
{
    double length = std::sqrt((static_cast<double>(x) * x + static_cast<double>(z) * z) + static_cast<double>(y) * y);
    if (length > Epsilon()) {
        double inverse = 1.0 / length;
        x = static_cast<float>(x * inverse);
        y = static_cast<float>(y * inverse);
        z = static_cast<float>(z * inverse);
    }
}

// The bone's index from the parameters: index=n, else id=<bone> (its index), else bone 0 - or all.
int BoneFromParams(CBoneLookAt* lookAt, CStruct* params, bool* all, bool* byId)
{
    int index = 0;
    *all = false;
    *byId = false;
    if (params->GetInteger(kIndex, &index, 0))
        return index;
    index = 0;
    uint32_t id = 0;
    if (params->GetChecksum(kId, &id, 0)) {
        *byId = true;
        return lookAt->FindBone(id);
    }
    if (params->ContainsComponentNamed(kAll))
        *all = true;
    return 0;
}

} // namespace

// 0x00487E00
Mth::Vector* SClampVector::Clamp(const Mth::Vector* min, const Mth::Vector* max)
{
    float* values = &x;
    const float* low = &min->x;
    const float* high = &max->x;
    for (int i = 0; i < 4; ++i) {
        float value = values[i] > high[i] ? high[i] : values[i];
        values[i] = value < low[i] ? low[i] : value;
    }
    return this;
}

// 0x00487EE0
Mth::Vector* __cdecl ClampedVector(Mth::Vector* out, const Mth::Vector* v, const Mth::Vector* min,
                                   const Mth::Vector* max)
{
    SClampVector clamped;
    static_cast<Mth::Vector&>(clamped) = *v;
    clamped.Clamp(min, max);
    *out = clamped;
    return out;
}

// 0x00487F40 (each sine and cosine rounded to a float where the original stores it; sin(y) kept at the x87's
// precision into its two products)
Mth::Quat* __cdecl EulerToQuat(Mth::Quat* out, const Mth::Vector* angles)
{
    double halfZ = angles->z * 0.5;
    double halfY = angles->y * 0.5;
    double halfX = angles->x * 0.5;
    float cosZ = X87::CosF(halfZ);
    float cosY = X87::CosF(halfY);
    float cosX = X87::CosF(halfX);
    float sinZ = X87::SinF(static_cast<float>(halfZ));
    float sinX = X87::SinF(static_cast<float>(halfX));
    float cycz = static_cast<float>(static_cast<double>(cosY) * cosZ);
    float szsy = static_cast<float>(X87::SinTimes(halfY, sinZ));
    float sycz = static_cast<float>(X87::SinTimes(halfY, cosZ));
    float szcy = static_cast<float>(static_cast<double>(sinZ) * cosY);
    double w = static_cast<double>(szsy) * sinX + static_cast<double>(cycz) * cosX;
    double x = static_cast<double>(cycz) * sinX - static_cast<double>(szsy) * cosX;
    double y = static_cast<double>(szcy) * sinX + static_cast<double>(sycz) * cosX;
    double z = static_cast<double>(szcy) * cosX - static_cast<double>(sycz) * sinX;
    out->x = static_cast<float>(x);
    out->y = static_cast<float>(y);
    out->z = static_cast<float>(z);
    out->w = static_cast<float>(w);
    return out;
}

// 0x00488020
Mth::Vector* __cdecl QuatToEuler(Mth::Vector* out, const Mth::Quat* q)
{
    double asinArgument = -((static_cast<double>(q->x) * q->z - static_cast<double>(q->w) * q->y) * 2.0);
    double y = X87::Asin(asinArgument);
    float ww = q->w * q->w;
    float xx = q->x * q->x;
    float yy = q->y * q->y;
    float zz = q->z * q->z;
    double zNum = (static_cast<double>(q->w) * q->z + static_cast<double>(q->y) * q->x) * 2.0;
    double zDen = ((static_cast<double>(xx) + ww) - yy) - zz;
    float z = X87::Atan2F(zNum, zDen);
    out->w = 1.0f;
    double xNum = (static_cast<double>(q->w) * q->x + static_cast<double>(q->y) * q->z) * 2.0;
    double xDen = ((static_cast<double>(ww) - xx) - yy) + zz;
    out->x = X87::Atan2F(xNum, xDen);
    out->y = static_cast<float>(y);
    out->z = z;
    return out;
}

// --- the bones' look-at ------------------------------------------------------------------------------------------

// 0x00488300
CBoneLookAt* CBoneLookAt::Construct(void* owner, void* object)
{
    GameFunction<void*(__thiscall*)(CBoneLookAt*, void*)>(0x00481710)(this, owner);
    vtable = reinterpret_cast<void**>(kVtable);
    mp_object = static_cast<uint8_t*>(owner ? GameFunction<void*(__thiscall*)(CBoneLookAt*)>(0x00481760)(this)
                                            : object);
    for (SBone& bone : m_bones) {
        bone.m_active = false;
        bone.m_clamp = true;
        bone.m_bone = kNoBone;
        bone.m_min = {0.0f, 0.0f, 0.0f, 1.0f};
        bone.m_max = {0.0f, 0.0f, 0.0f, 1.0f};
        bone.m_targetPos = {0.0f, 0.0f, 0.0f, 1.0f};
        bone.m_weight = 1.0f;
        bone.m_fixedPos = true;
        bone.m_target = 0;
        bone.m_axis = 0;
        bone.m_rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return this;
}

// 0x00487D40
void CBoneLookAt::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kVtable);
    GameFunction<void(__thiscall*)(CBoneLookAt*)>(0x00481730)(this); // (the base's)
}

// 0x004880D0
CBoneLookAt* CBoneLookAt::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00488100
void CBoneLookAt::Update()
{
    for (SBone& bone : m_bones) {
        if (bone.m_fixedPos)
            continue;
        auto* object = reinterpret_cast<Obj::CCompositeObject*>(
            Obj::CompositeObjectManager()->GetObjectByID(bone.m_target));
        if (!object)
            continue;
        bone.m_targetPos.z = bone.m_offset.z + object->m_pos.z;
        bone.m_targetPos.w = bone.m_offset.w + object->m_pos.w;
        bone.m_targetPos.x = bone.m_offset.x + object->m_pos.x;
        bone.m_targetPos.y = bone.m_offset.y + object->m_pos.y;
    }
}

// 0x00488410
bool CBoneLookAt::Apply(Mth::Quat* rotations)
{
    reinterpret_cast<void(__thiscall*)(CBoneLookAt*)>(vtable[5])(this);
    auto* object = reinterpret_cast<Obj::CCompositeObject*>(mp_object);
    Obj::CBaseComponent* component = object->GetComponent(kSkeletonComponent);
    auto* skeleton = component ? *reinterpret_cast<CSkeleton**>(reinterpret_cast<uint8_t*>(component) + 0x18) : nullptr;
    for (SBone& bone : m_bones) {
        if (bone.m_bone == kNoBone)
            continue;
        int index = skeleton->GetBoneIndex(bone.m_bone);
        if (index == -1)
            continue;
        const Mth::Vector& pos = object->m_pos;
        const Mth::Vector& at = object->m_matrix.row[2];
        // The direction to the target, and the turn from the object's own
        float dx = bone.m_targetPos.x - pos.x;
        float dy = bone.m_targetPos.y - pos.y;
        float dz = bone.m_targetPos.z - pos.z;
        NormalizeXZY(dx, dy, dz);
        Mth::Quat& pose = rotations[index];
        double dot = (static_cast<double>(dx) * at.x + static_cast<double>(dz) * at.z) + static_cast<double>(dy) * at.y;
        float angle = static_cast<float>(X87::Acos(dot));
        Mth::Vector axis;
        axis.x = static_cast<float>(static_cast<double>(dy) * at.z - static_cast<double>(dz) * at.y);
        axis.y = static_cast<float>(static_cast<double>(dz) * at.x - static_cast<double>(at.z) * dx);
        axis.z = static_cast<float>(static_cast<double>(at.y) * dx - static_cast<double>(dy) * at.x);
        axis.w = 0.0f;
        NormalizeXZY(axis.x, axis.y, axis.z);
        // The axis turned to the bone's own way of looking
        Mth::Quat turn = {0.0f, 0.0f, 0.0f, 1.0f};
        int axisCode = bone.m_axis;
        unsigned int which = static_cast<unsigned int>(axisCode < 0 ? -axisCode : axisCode);
        if (which <= 3) {
            if (which <= 2) {
                volatile int divisor = axisCode; // (0 divides by zero: the original's)
                int sign = axisCode / divisor;
                if (axisCode < 0)
                    sign = -sign;
                double quarter = sign * static_cast<double>(kQuarterTurn);
                double s = X87::Sin(quarter);
                double c = X87::Cos(quarter);
                if (which <= 1) {
                    double oldY = axis.y;
                    axis.y = static_cast<float>(oldY * c - axis.z * s);
                    axis.z = static_cast<float>(oldY * s + axis.z * c);
                } else {
                    float sinF = static_cast<float>(s);
                    double oldX = axis.x;
                    axis.x = static_cast<float>(c * oldX - static_cast<double>(axis.y) * sinF);
                    axis.y = static_cast<float>(sinF * oldX + axis.y * c);
                }
            }
            Mth::Quat made;
            turn = *made.InitFromAxisAngle(axis, angle);
        }
        // Its angles: clamped and scaled, or each one past its limits dropped
        Mth::Vector angles;
        QuatToEuler(&angles, &turn);
        if (bone.m_clamp) {
            Mth::Vector clamped;
            ClampedVector(&clamped, &angles, &bone.m_min, &bone.m_max);
            angles.x = bone.m_weight * clamped.x;
            angles.y = bone.m_weight * clamped.y;
            angles.z = bone.m_weight * clamped.z;
            angles.w = bone.m_weight * clamped.w;
        } else {
            if (angles.x < bone.m_min.x)
                angles.x = 0.0f;
            if (angles.y < bone.m_min.y)
                angles.y = 0.0f;
            if (angles.z < bone.m_min.z)
                angles.z = 0.0f;
            if (angles.x > bone.m_max.x)
                angles.x = 0.0f;
            if (angles.y > bone.m_max.y)
                angles.y = 0.0f;
            if (angles.z > bone.m_max.z)
                angles.z = 0.0f;
        }
        // Applied to the pose's rotation, eased from the last turn when it's far from it
        Mth::Quat rotation;
        EulerToQuat(&rotation, &angles);
        rotation *= pose;
        pose = rotation;
        pose.Normalize(1.0f);
        Mth::Quat& last = bone.m_rotation;
        double closeness = ((static_cast<double>(pose.z) * last.z + static_cast<double>(last.x) * pose.x) +
                            static_cast<double>(pose.w) * last.w) +
                           static_cast<double>(pose.y) * last.y;
        if (closeness < kEaseBelow) {
            float t = static_cast<float>(std::fabs(closeness) * kEaseFactor);
            float eased[4];
            float* result = InterpolateRotation(eased, &last.x, &pose.x, t);
            pose = {result[0], result[1], result[2], result[3]};
        }
        last = pose;
    }
    return true;
}

// 0x00488950
void CBoneLookAt::Configure(bool all, int index, CStruct* params)
{
    if (params->ContainsComponentNamed(kOnFlag)) {
        if (all)
            ActivateAll();
        else
            m_bones[index].m_active = true;
    } else if (params->ContainsComponentNamed(kOffFlag)) {
        if (all)
            DeactivateAll();
        else
            m_bones[index].m_active = false;
    }
    SBone& bone = m_bones[index];
    params->GetChecksum(kBone, &bone.m_bone, 0);
    if (params->GetChecksum(kTargetObject, &bone.m_target, 0)) {
        bone.m_fixedPos = false;
        bone.m_offset = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (params->GetVector(kTargetPos, reinterpret_cast<Script::VectorValue*>(&bone.m_targetPos), 0))
        bone.m_fixedPos = true;
    if (params->GetVector(kOffset, reinterpret_cast<Script::VectorValue*>(&bone.m_offset), 0))
        bone.m_fixedPos = false;
    params->GetInteger(kAxis, &bone.m_axis, 0);
    params->GetFloat(kWeight, &bone.m_weight, 0);
    Mth::Vector limit;
    if (params->GetVector(kMinAngles, reinterpret_cast<Script::VectorValue*>(&limit), 0)) {
        limit.DegreesToRadians();
        if (all)
            SetAllMin(&limit);
        else
            bone.m_min = limit;
    }
    if (params->GetVector(kMaxAngles, reinterpret_cast<Script::VectorValue*>(&limit), 0)) {
        limit.DegreesToRadians();
        if (all)
            SetAllMax(&limit);
        else
            bone.m_max = limit;
    }
    bone.m_clamp = !params->ContainsFlag(kNoClamp);
}

// 0x00488B20
void CBoneLookAt::InitFromStructure(CStruct* params)
{
    GameFunction<void(__thiscall*)(CBoneLookAt*, CStruct*)>(0x00481740)(this, params); // (the base's: its id)
    Configure(false, 0, params);
}

// 0x00488B40
int CBoneLookAt::CallMemberFunction(uint32_t name, CStruct* params, Script::CScript*)
{
    if (name != kOn && name != kIsOn && name != kOff && name != kSettings)
        return 2;
    bool all;
    bool byId;
    int index = BoneFromParams(this, params, &all, &byId);
    if (name == kOn) {
        if (all)
            ActivateAll();
        else
            m_bones[index].m_active = true;
        return 1;
    }
    if (name == kOff) {
        if (all)
            DeactivateAll();
        else
            m_bones[index].m_active = false;
        return 1;
    }
    if (name == kIsOn)
        return all ? (AllActive() ? 1 : 0) : (m_bones[index].m_active ? 1 : 0);
    Configure(all, index, params);
    return 1;
}

// 0x00487D60
int CBoneLookAt::FindBone(uint32_t bone)
{
    for (int i = 0; i < 4; ++i) {
        if (m_bones[i].m_bone == bone)
            return i;
    }
    return -1;
}

// 0x00487D90
bool CBoneLookAt::AllActive()
{
    for (SBone& bone : m_bones) {
        if (bone.m_bone != kNoBone && !bone.m_active)
            return false;
    }
    return true;
}

// 0x00487DC0
void CBoneLookAt::ActivateAll()
{
    for (SBone& bone : m_bones) {
        if (bone.m_bone != kNoBone)
            bone.m_active = true;
    }
}

// 0x00487DE0
void CBoneLookAt::DeactivateAll()
{
    for (SBone& bone : m_bones) {
        if (bone.m_bone != kNoBone)
            bone.m_active = false;
    }
}

// 0x00488170
void CBoneLookAt::SetAllMin(const Mth::Vector* min)
{
    for (SBone& bone : m_bones) {
        if (bone.m_bone != kNoBone)
            bone.m_min = *min;
    }
}

// 0x00488220
void CBoneLookAt::SetAllMax(const Mth::Vector* max)
{
    for (SBone& bone : m_bones) {
        if (bone.m_bone != kNoBone)
            bone.m_max = *max;
    }
}

} // namespace Gfx

REPLACE(0x00487E00, &Gfx::SClampVector::Clamp);
REPLACE(0x00487EE0, Gfx::ClampedVector);
REPLACE(0x00487F40, Gfx::EulerToQuat);
REPLACE(0x00488020, Gfx::QuatToEuler);
REPLACE(0x00488300, &Gfx::CBoneLookAt::Construct);
REPLACE(0x00487D40, &Gfx::CBoneLookAt::DestroyBody);
REPLACE(0x004880D0, &Gfx::CBoneLookAt::Destroy);
REPLACE(0x00488100, &Gfx::CBoneLookAt::Update);
REPLACE(0x00488410, &Gfx::CBoneLookAt::Apply);
REPLACE(0x00488950, &Gfx::CBoneLookAt::Configure);
REPLACE(0x00488B20, &Gfx::CBoneLookAt::InitFromStructure);
REPLACE(0x00488B40, &Gfx::CBoneLookAt::CallMemberFunction);
REPLACE(0x00487D60, &Gfx::CBoneLookAt::FindBone);
REPLACE(0x00487D90, &Gfx::CBoneLookAt::AllActive);
REPLACE(0x00487DC0, &Gfx::CBoneLookAt::ActivateAll);
REPLACE(0x00487DE0, &Gfx::CBoneLookAt::DeactivateAll);
REPLACE(0x00488170, &Gfx::CBoneLookAt::SetAllMin);
REPLACE(0x00488220, &Gfx::CBoneLookAt::SetAllMax);
