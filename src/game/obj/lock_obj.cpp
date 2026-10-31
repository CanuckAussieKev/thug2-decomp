// The lockobj component (GameObjects::CPinnedObjectModule): Obj_LockToObject's parameters and the update that keeps an object
// locked to another - with THAW's no_rotate added (lock_obj.h).

#include "game/obj/lock_obj.h"

#include <cstdint>
#include <cstring>

#include "game/maths/mat4.h"
#include "game/maths/random.h"
#include "game/obj/compositemgr.h"
#include "game/obj/entity_hub.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/front/screen_element_cfuncs.h"

namespace GameObjects {

float g_lockObjWobbleStep{0.006135923f};

namespace {

constexpr uint32_t kObjLockToObject = 0x785E57C7; // Obj_LockToObject
constexpr uint32_t kOff = 0xD443A2BC;             // off
constexpr uint32_t kObjectName = 0x2BFC2486;      // ObjectName
constexpr uint32_t kBoneOffset = 0xBE9AB709;      // BoneOffset
constexpr uint32_t kId = 0x40C698AF;              // id
constexpr uint32_t kBone = 0xCAB94088;            // bone
constexpr uint32_t kLagRange = 0x700C4085;        // LagRange
constexpr uint32_t kLagWobble = 0xD25807F0;       // lagwobble
constexpr uint32_t kAngles = 0x9D2D0915;          // Angles
// NOT ORIGINAL: THAW's flag (THAW's names: 0xD74A6981)
constexpr uint32_t kNoRotate = 0xD74A6981; // no_rotate

// The wobble's step: 2 pi / 1024 as the float the original multiplies by (0x00646118; the update's own, 0x0064611C,
// is one bit larger).
float WobbleStep() { return g_lockObjWobbleStep; }

// x87 FSIN, as the original takes the wobble's sines.
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The rotation of `angles` (radians) as a matrix (0x0041A730: the identity times RotationX, RotationY, RotationZ).
void AnglesToMatrix(Maths::Mat4* matrix, const Maths::Vec4* angles)
{
    matrix->InitFromAngles(*angles);
}

} // namespace

// 0x0041F520
void CPinnedObjectModule::ConfigureFromData(Scripts::CStruct* params)
{
    m_entityLabel = 0;
    m_bone = 0;
    m_targetId = MenuSystem::g_unassignedId;
    m_shift = {0.0f, 0.0f, 0.0f, 1.0f};
    // (NOT ORIGINAL: LOCK_FLAG_NO_ROTATE cleared with the original's LOCK_FLAG_ANGLES)
    m_lockFlags &= ~(LOCK_FLAG_ANGLES | LOCK_FLAG_NO_ROTATE);
    if (params->HasBareFlag(kOff)) {
        m_lockFlags &= ~LOCK_FLAG_LOCKED;
        return;
    }
    m_lockFlags |= LOCK_FLAG_LOCKED;
    params->ReadHashValue(kObjectName, &m_entityLabel, 0);
    auto* offset = reinterpret_cast<Scripts::VectorValue*>(&m_shift);
    if (!params->FetchVec(0u, offset, 0))
        params->FetchVec(kBoneOffset, offset, 0);
    params->ReadHashValue(kId, &m_targetId, 0);
    params->ReadHashValue(kBone, &m_bone, 0);
    // (updated after the target: its priority less one)
    CEntityHub* manager = CompositeObjectManager();
    CEntity* target = manager->LookUpEntity(m_targetId);
    if (!target)
        target = manager->LookUpEntity(m_entityLabel);
    if (target) {
        int priority = Truncated(static_cast<double>(target->m_listLink.priority) - 1.0);
        reinterpret_cast<void(__thiscall*)(CObjectHubBase*, CEntity*, int)>(manager->m_vtable[4])(
            manager, reinterpret_cast<CEntity*>(m_entity), priority);
    }
    m_lockFlags &= ~LOCK_FLAG_LAG;
    m_lagMin = 1.0f;
    m_lagMax = 1.0f;
    if (params->FetchPair(kLagRange, &m_lagMin, &m_lagMax, 0))
        m_lockFlags |= LOCK_FLAG_LAG;
    m_wobble[0] = 0.0f;
    m_wobble[1] = 0.0f;
    if (params->FetchPair(kLagWobble, &m_wobble[0], &m_wobble[1], 0))
        m_lockFlags |= LOCK_FLAG_LAG;
    m_wobbleSeed = Maths::Rnd(0x400);
    // (the masked time is never negative: the original's 2^32 correction for a negative one never applies)
    int t = static_cast<int>((FrameClock::GetTime() + static_cast<uint32_t>(m_wobbleSeed)) & 0x3FF);
    double time = static_cast<double>(t);
    double step = static_cast<double>(WobbleStep());
    double wobble = X87Sin(time * static_cast<double>(m_wobble[1]) * step) *
                    X87Sin(time * static_cast<double>(m_wobble[0]) * step);
    m_lag = static_cast<float>((wobble + 1.0) * (static_cast<double>(m_lagMax) - static_cast<double>(m_lagMin)) * 0.5 +
                               static_cast<double>(m_lagMin));
    if (params->HasEntryNamed(kAngles))
        m_lockFlags |= LOCK_FLAG_ANGLES;
    else
        m_lockFlags &= ~LOCK_FLAG_ANGLES;
    if (m_lockFlags & LOCK_FLAG_ANGLES) {
        Maths::Vec4 angles;
        LevelNodes::ReadTurnAngles(params, &angles);
        AnglesToMatrix(&m_orientationTurn, &angles);
    }
    // NOT ORIGINAL: THAW's no_rotate (LOCK_FLAG_NO_ROTATE)
    if (params->HasBareFlag(kNoRotate))
        m_lockFlags |= LOCK_FLAG_NO_ROTATE;
}

// 0x0041FF90
void CPinnedObjectModule::TickFrame()
{
    if (!(m_lockFlags & LOCK_FLAG_LOCKED))
        return;
    if (!(m_lockFlags & LOCK_FLAG_NO_ROTATE)) {
        UpdateLock();
        return;
    }
    // NOT ORIGINAL: THAW's no_rotate - placed by the lock (the offset turned by the target, as THAW's ferris cars go
    // round with the wheel), its own orientation kept.
    Maths::Mat4 own = m_entity->m_basisFrame;
    UpdateLock();
    m_entity->m_basisFrame = own;
    m_entity->m_displayMatrix = own;
}

// 0x0041F410
int CPinnedObjectModule::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    if (name != kObjLockToObject)
        return 2;
    CObjectModule::ConfigureFromData(params);
    return 1;
}

} // namespace Obj
