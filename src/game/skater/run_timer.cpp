// The skater's run timer component (Obj::CSkaterRunTimerComponent): see run_timer.h.

#include "game/skater/run_timer.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/camera_lookaround.h"
#include "game/skater/vehicle_sound.h"
#include "game/skater/skater_camera_component.h"

namespace Obj {

uint32_t g_runTimerStateNames[4]{0x742CF11F, 0x5392CA8E, 0x26776873, 0xDEF417FA};
uint32_t g_runTimerMemberFunctions[42]{
            0x82445966, 0x8BCE7E66, 0, 0, 0, 0,
            0x956236B1, 0x8BCE7E66, 0, 0, 0, 0,
            0xAB04CBF9, 0x8BCE7E66, 0, 0, 0, 0,
            0x96D81D8E, 0x8BCE7E66, 0, 0, 0, 0,
            0x81AAA6DD, 0x8BCE7E66, 0, 0, 0, 0,
            0x37CC7691, 0x8BCE7E66, 0, 0, 0, 0,
            0, 0x8BCE7E66, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterRunTimer = 0x8BCE7E66, kWalk = 0x726E85AA;
constexpr uint32_t kMState = 0x109B9260, kMTimer = 0xD9D89E81, kHangSpeedAdjustment = 0x92FB4D09,
                   kRunTimerUp = 0x9E22A8E9, kHideRunTimer = 0x813CC576, kShowRunTimer = 0x47EEC244,
                   kDuration = 0x0B84F532 /* Physics_RunTimer_Duration */, kFactorComplete = 0x41F56511,
                   kRunTimerControllerId = 0x39778122, kRunTimerId = 0x628F21C9;
// The run timer script's spawn id and the ids the scripts get: from the skater's index (the object's +0x2BC).
constexpr uint32_t kRunTimerScriptId = 0x83321DEA, kRunTimerIdBase = 0x7D42821C;

constexpr int kMaxResumes = 8;
constexpr float kResumeShare = 0.125f; // 0x0064B960 (1 / kMaxResumes)

// The state names m_state shows as (0x0064C080).
const uint32_t* StateNames() { return g_runTimerStateNames; }

uint32_t SkaterIndex(CCompositeObject* skater) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(skater) + 0x2BC); }
uint32_t TimerScriptId(CCompositeObject* skater) { return SkaterIndex(skater) + kRunTimerScriptId; }

// The skater's physics value `name`.
double PhysicsValue(CCompositeObject* skater, uint32_t name)
{
    return static_cast<CSkater*>(skater)->GetScaledPhysics(name);
}

// An event of `type` to the run timer's script (no data, not broadcast).
void TellTimerScript(CCompositeObject* skater, uint32_t type)
{
    Tracker()->LaunchEvent(type, TimerScriptId(skater), skater->m_id, nullptr, false, -1.0f);
}

} // namespace

// 0x005185D0
CBaseComponent* __cdecl CSkaterRunTimerComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterRunTimerComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterRunTimerComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterRunTimer;
    return component;
}

// 0x00518240
void __cdecl CSkaterRunTimerComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kSkaterRunTimer), g_runTimerMemberFunctions,
                                        0x18);
}

// 0x00518260
void CSkaterRunTimerComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x005182B0
CSkaterRunTimerComponent* CSkaterRunTimerComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005182E0
void CSkaterRunTimerComponent::Finalize() { m_walk = reinterpret_cast<uint8_t*>(m_object->GetComponent(kWalk)); }

// 0x00518270
void CSkaterRunTimerComponent::GetDebugInfo(Script::CStruct* params)
{
    params->AddChecksum(kMState, StateNames()[m_state]);
    params->AddFloat(kMTimer, m_timeLeft);
    // (then the base's GetDebugInfo, which is empty: 0x005BAB90)
}

// 0x00518300
void CSkaterRunTimerComponent::Update()
{
    if (m_state == ACTIVE_RUNNING) {
        int walkState = *reinterpret_cast<int*>(m_walk + 0x18);
        double elapsed;
        if (walkState >= 2 && walkState <= 4) {
            float adjustment = Script::GetFloat(kHangSpeedAdjustment);
            elapsed = Tmr::GetSlomoFrameSeconds() * adjustment;
        } else {
            elapsed = Tmr::GetSlomoFrameSeconds();
        }
        m_timeLeft = static_cast<float>(m_timeLeft - elapsed);
        if (m_timeLeft < 0.0f) {
            m_state = ACTIVE_TIME_UP;
            m_object->SelfEvent(kRunTimerUp, nullptr);
        }
    } else if (m_state == ACTIVE_TIME_UP) {
        m_object->SelfEvent(kRunTimerUp, nullptr);
    }
}

// 0x00518390
void CSkaterRunTimerComponent::Stop()
{
    if (Script::FindSpawnedScriptWithId(TimerScriptId(m_object)))
        TellTimerScript(m_object, kHideRunTimer);
    m_state = INACTIVE;
}

// 0x005183F0
void CSkaterRunTimerComponent::Resume()
{
    if (m_state == INACTIVE)
        return;
    m_state = m_timeLeft > 0.0f ? ACTIVE_RUNNING : ACTIVE_TIME_UP;
    int16_t resumes = m_resumes;
    if (resumes < kMaxResumes) {
        double cap = PhysicsValue(m_object, kDuration) * (kMaxResumes - resumes) * kResumeShare;
        if (!(m_timeLeft < cap))
            m_timeLeft = static_cast<float>(cap);
    }
    m_resumes = static_cast<int16_t>(resumes + 1);
    TellTimerScript(m_object, kShowRunTimer);
}

// 0x005184A0
bool CSkaterRunTimerComponent::Pause(Script::CStruct*, Script::CScript*)
{
    if (m_state != INACTIVE) {
        m_state = ACTIVE_PAUSED;
        TellTimerScript(m_object, kHideRunTimer);
    }
    return true;
}
// 0x005184F0
bool CSkaterRunTimerComponent::Unpause(Script::CStruct*, Script::CScript*)
{
    Resume();
    return true;
}
// 0x00518500
bool CSkaterRunTimerComponent::GetFactorComplete(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddFloat(kFactorComplete,
                               static_cast<float>(m_timeLeft / PhysicsValue(m_object, kDuration)));
    return true;
}
// 0x00518540
bool CSkaterRunTimerComponent::GetRunTimerControllerId(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(kRunTimerControllerId, TimerScriptId(m_object));
    return true;
}
// 0x00518570
bool CSkaterRunTimerComponent::GetRunTimerId(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(kRunTimerId, SkaterIndex(m_object) + kRunTimerIdBase);
    return true;
}
// 0x005185A0
bool CSkaterRunTimerComponent::ComboStarted(Script::CStruct*, Script::CScript*)
{
    m_timeLeft = static_cast<float>(PhysicsValue(m_object, kDuration));
    m_resumes = 0;
    m_state = ACTIVE_PAUSED;
    return true;
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CSkaterRunTimerComponent::Destroy,
         &Obj::CSkaterRunTimerComponent::Update,
         &CSkaterCleanupStateComponent::InitFromStructure,
         &Obj::CSkaterRunTimerComponent::Finalize,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         &Obj::CSkaterRunTimerComponent::GetDebugInfo,
         &Obj::CBaseComponent::RunMemberFunction,
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
