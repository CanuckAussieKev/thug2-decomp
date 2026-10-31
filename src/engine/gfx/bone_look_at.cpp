// An object's bones turned to look at something (see bone_look_at.h). From the disassembly of 0x00487D40-0x00488DA1.

#include "engine/gfx/bone_look_at.h"

#include <cmath>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/gfx/anim_blend.h"
#include "engine/gfx/anim_controller.h"
#include "engine/gfx/rig.h"
#include "engine/maths/x87_math.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/gfx/model_utils.h"
#include "engine/core/stubs.h"
#include "engine/objects/anim_movies.h"

namespace Gfx2D {

namespace {

using Scripts::CStruct;

extern void** const kVtable;
constexpr uint32_t kNoBone = 0xFFFFFFFF;
constexpr uint32_t kSkeletonModuleName = 0x222756D5;
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
constexpr uint32_t kAimPointHash = 0xDC741BAD;  // (name unknown) a point looked at
constexpr uint32_t kOffset = 0x1F07CF31;     // (name unknown) the offset from the object
constexpr uint32_t kAxis = 0x7AF07905;       // Axis
constexpr uint32_t kWeight = 0xF832AABE;     // weight
constexpr uint32_t kMinAngles = 0x0B405328;  // min_angles
constexpr uint32_t kMaxAngles = 0x0A4CC99C;  // max_angles
constexpr uint32_t kNoClamp = 0xD4A8CD5F;    // (name unknown) each angle past its limits zeroed instead

constexpr float kQuarterTurn = 1.57079637f;  // 0x00645484
constexpr float kEaseBelow = 0.923879027f;   // 0x006488F8: a turn this close to the last one isn't eased
constexpr float kEaseFactor = 0.216478556f;  // 0x006488F4

float Epsilon() { return GameObjects::g_floatZero; }

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
    if (params->FetchIntValue(kIndex, &index, 0))
        return index;
    index = 0;
    uint32_t id = 0;
    if (params->ReadHashValue(kId, &id, 0)) {
        *byId = true;
        return lookAt->FindBone(id);
    }
    if (params->HasEntryNamed(kAll))
        *all = true;
    return 0;
}

} // namespace

// 0x00487E00
Maths::Vec4* SClampVector::Confine(const Maths::Vec4* min, const Maths::Vec4* max)
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
Maths::Vec4* __cdecl ClampedVector(Maths::Vec4* out, const Maths::Vec4* v, const Maths::Vec4* min,
                                   const Maths::Vec4* max)
{
    SClampVector clamped;
    static_cast<Maths::Vec4&>(clamped) = *v;
    clamped.Confine(min, max);
    *out = clamped;
    return out;
}

// 0x00487F40 (each sine and cosine rounded to a float where the original stores it; sin(y) kept at the x87's
// precision into its two products)
Maths::QuatRotation* __cdecl AnglesToQuat(Maths::QuatRotation* out, const Maths::Vec4* angles)
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
Maths::Vec4* __cdecl QuatToEuler(Maths::Vec4* out, const Maths::QuatRotation* q)
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
CBoneLookAt* CBoneLookAt::MakeNew(void* owner, void* object)
{
    CAnimDriver* base = reinterpret_cast<CAnimDriver*>(this);
    base->CAnimDriver::MakeNew(static_cast<CAnimTimeline*>(owner));
    vtable = reinterpret_cast<void**>(kVtable);
    m_ownerObject = static_cast<uint8_t*>(owner ? static_cast<void*>(base->CAnimDriver::FetchSubject()) : object);
    for (SBone& bone : m_bones) {
        bone.m_active = false;
        bone.m_clamp = true;
        bone.m_bone = kNoBone;
        bone.m_lowerLimit = {0.0f, 0.0f, 0.0f, 1.0f};
        bone.m_upperLimit = {0.0f, 0.0f, 0.0f, 1.0f};
        bone.m_lookGoal = {0.0f, 0.0f, 0.0f, 1.0f};
        bone.m_weight = 1.0f;
        bone.m_fixedPos = true;
        bone.m_aimPoint = 0;
        bone.m_pivotDir = 0;
        bone.m_orientationTurn = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return this;
}

// 0x00487D40
void CBoneLookAt::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kVtable);
    reinterpret_cast<CAnimDriver*>(this)->CAnimDriver::DestroyBody(); // (the base's)
}

// 0x004880D0
CBoneLookAt* CBoneLookAt::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00488100
void CBoneLookAt::TickFrame()
{
    for (SBone& bone : m_bones) {
        if (bone.m_fixedPos)
            continue;
        auto* object = reinterpret_cast<GameObjects::CGameEntity*>(
            GameObjects::ObjectHubSlot()->LookUpEntity(bone.m_aimPoint));
        if (!object)
            continue;
        bone.m_lookGoal.z = bone.m_shift.z + object->m_atPoint.z;
        bone.m_lookGoal.w = bone.m_shift.w + object->m_atPoint.w;
        bone.m_lookGoal.x = bone.m_shift.x + object->m_atPoint.x;
        bone.m_lookGoal.y = bone.m_shift.y + object->m_atPoint.y;
    }
}

// 0x00488410
bool CBoneLookAt::Apply(Maths::QuatRotation* rotations)
{
    reinterpret_cast<void(__thiscall*)(CBoneLookAt*)>(vtable[5])(this);
    auto* object = reinterpret_cast<GameObjects::CGameEntity*>(m_ownerObject);
    GameObjects::CObjectModule* component = object->FetchModule(kSkeletonModuleName);
    auto* skeleton = component ? *reinterpret_cast<CRig**>(reinterpret_cast<uint8_t*>(component) + 0x18) : nullptr;
    for (SBone& bone : m_bones) {
        if (bone.m_bone == kNoBone)
            continue;
        int index = skeleton->FetchBoneSlot(bone.m_bone);
        if (index == -1)
            continue;
        const Maths::Vec4& pos = object->m_atPoint;
        const Maths::Vec4& at = object->m_basisFrame.row[2];
        // The direction to the target, and the turn from the object's own
        float dx = bone.m_lookGoal.x - pos.x;
        float dy = bone.m_lookGoal.y - pos.y;
        float dz = bone.m_lookGoal.z - pos.z;
        NormalizeXZY(dx, dy, dz);
        Maths::QuatRotation& pose = rotations[index];
        double dot = (static_cast<double>(dx) * at.x + static_cast<double>(dz) * at.z) + static_cast<double>(dy) * at.y;
        float angle = static_cast<float>(X87::Acos(dot));
        Maths::Vec4 axis;
        axis.x = static_cast<float>(static_cast<double>(dy) * at.z - static_cast<double>(dz) * at.y);
        axis.y = static_cast<float>(static_cast<double>(dz) * at.x - static_cast<double>(at.z) * dx);
        axis.z = static_cast<float>(static_cast<double>(at.y) * dx - static_cast<double>(dy) * at.x);
        axis.w = 0.0f;
        NormalizeXZY(axis.x, axis.y, axis.z);
        // The axis turned to the bone's own way of looking
        Maths::QuatRotation turn = {0.0f, 0.0f, 0.0f, 1.0f};
        int axisCode = bone.m_pivotDir;
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
            Maths::QuatRotation made;
            turn = *made.InitFromAxisAngle(axis, angle);
        }
        // Its angles: clamped and scaled, or each one past its limits dropped
        Maths::Vec4 angles;
        QuatToEuler(&angles, &turn);
        if (bone.m_clamp) {
            Maths::Vec4 clamped;
            ClampedVector(&clamped, &angles, &bone.m_lowerLimit, &bone.m_upperLimit);
            angles.x = bone.m_weight * clamped.x;
            angles.y = bone.m_weight * clamped.y;
            angles.z = bone.m_weight * clamped.z;
            angles.w = bone.m_weight * clamped.w;
        } else {
            if (angles.x < bone.m_lowerLimit.x)
                angles.x = 0.0f;
            if (angles.y < bone.m_lowerLimit.y)
                angles.y = 0.0f;
            if (angles.z < bone.m_lowerLimit.z)
                angles.z = 0.0f;
            if (angles.x > bone.m_upperLimit.x)
                angles.x = 0.0f;
            if (angles.y > bone.m_upperLimit.y)
                angles.y = 0.0f;
            if (angles.z > bone.m_upperLimit.z)
                angles.z = 0.0f;
        }
        // Applied to the pose's rotation, eased from the last turn when it's far from it
        Maths::QuatRotation rotation;
        AnglesToQuat(&rotation, &angles);
        rotation *= pose;
        pose = rotation;
        pose.MakeUnit(1.0f);
        Maths::QuatRotation& last = bone.m_orientationTurn;
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
    if (params->HasEntryNamed(kOnFlag)) {
        if (all)
            ActivateAll();
        else
            m_bones[index].m_active = true;
    } else if (params->HasEntryNamed(kOffFlag)) {
        if (all)
            DeactivateAll();
        else
            m_bones[index].m_active = false;
    }
    SBone& bone = m_bones[index];
    params->ReadHashValue(kBone, &bone.m_bone, 0);
    if (params->ReadHashValue(kTargetObject, &bone.m_aimPoint, 0)) {
        bone.m_fixedPos = false;
        bone.m_shift = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (params->FetchVec(kAimPointHash, reinterpret_cast<Scripts::VectorValue*>(&bone.m_lookGoal), 0))
        bone.m_fixedPos = true;
    if (params->FetchVec(kOffset, reinterpret_cast<Scripts::VectorValue*>(&bone.m_shift), 0))
        bone.m_fixedPos = false;
    params->FetchIntValue(kAxis, &bone.m_pivotDir, 0);
    params->FetchFloat(kWeight, &bone.m_weight, 0);
    Maths::Vec4 limit;
    if (params->FetchVec(kMinAngles, reinterpret_cast<Scripts::VectorValue*>(&limit), 0)) {
        limit.DegreeToRadianConv();
        if (all)
            SetAllMin(&limit);
        else
            bone.m_lowerLimit = limit;
    }
    if (params->FetchVec(kMaxAngles, reinterpret_cast<Scripts::VectorValue*>(&limit), 0)) {
        limit.DegreeToRadianConv();
        if (all)
            SetAllMax(&limit);
        else
            bone.m_upperLimit = limit;
    }
    bone.m_clamp = !params->HasBareFlag(kNoClamp);
}

// 0x00488B20
void CBoneLookAt::ConfigureFromData(CStruct* params)
{
    reinterpret_cast<CAnimDriver*>(this)->CAnimDriver::ConfigureFromData(params); // (the base's: its id)
    Configure(false, 0, params);
}

// 0x00488B40
int CBoneLookAt::InvokeMethodByName(uint32_t name, CStruct* params, Scripts::CScript*)
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
void CBoneLookAt::SetAllMin(const Maths::Vec4* min)
{
    for (SBone& bone : m_bones) {
        if (bone.m_bone != kNoBone)
            bone.m_lowerLimit = *min;
    }
}

// 0x00488220
void CBoneLookAt::SetAllMax(const Maths::Vec4* max)
{
    for (SBone& bone : m_bones) {
        if (bone.m_bone != kNoBone)
            bone.m_upperLimit = *max;
    }
}

namespace {
static VtableSlot kVtable_slots[] = {&Gfx2D::CBoneLookAt::Teardown, &Gfx2D::CBoneLookAt::Apply, Stubs::IgnoreArgument,
                &Gfx2D::CBoneLookAt::InvokeMethodByName, &Gfx2D::CBoneLookAt::ConfigureFromData, &Gfx2D::CBoneLookAt::TickFrame,
                Gfx2D::VirtualSlot4Jump};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);
} // namespace

} // namespace Gfx
