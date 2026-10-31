// The skater's run timer component (GameObjects::CRiderRunClock): see run_timer.h.

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

namespace GameObjects {

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

// The state names m_curPhase shows as (0x0064C080).
const uint32_t* StateNames() { return g_runTimerStateNames; }

uint32_t SkaterIndex(CGameEntity* skater) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(skater) + 0x2BC); }
uint32_t TimerScriptId(CGameEntity* skater) { return SkaterIndex(skater) + kRunTimerScriptId; }

// The skater's physics value `name`.
double PhysicsValue(CGameEntity* skater, uint32_t name)
{
    return static_cast<CRider*>(skater)->GetScaledPhysics(name);
}

// An event of `type` to the run timer's script (no data, not broadcast).
void TellTimerScript(CGameEntity* skater, uint32_t type)
{
    Tracker()->LaunchEvent(type, TimerScriptId(skater), skater->m_ident, nullptr, false, -1.0f);
}

} // namespace

// 0x005185D0
CObjectModule* __cdecl CRiderRunClock::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderRunClock));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderRunClock*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kSkaterRunTimer;
    return component;
}

// 0x00518240
void __cdecl CRiderRunClock::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kSkaterRunTimer), g_runTimerMemberFunctions,
                                        0x18);
}

// 0x00518260
void CRiderRunClock::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x005182B0
CRiderRunClock* CRiderRunClock::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005182E0
void CRiderRunClock::Seal() { m_walk = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kWalk)); }

// 0x00518270
void CRiderRunClock::FetchDiagnostics(Scripts::CStruct* params)
{
    params->PutNameKey(kMState, StateNames()[m_curPhase]);
    params->PushFloat(kMTimer, m_secondsRemaining);
    // (then the base's FetchDiagnostics, which is empty: 0x005BAB90)
}

// 0x00518300
void CRiderRunClock::TickFrame()
{
    if (m_curPhase == CLOCK_TICKING) {
        int walkState = *reinterpret_cast<int*>(m_walk + 0x18);
        double elapsed;
        if (walkState >= 2 && walkState <= 4) {
            float adjustment = Scripts::FetchFloat(kHangSpeedAdjustment);
            elapsed = FrameClock::GetSlomoFrameSeconds() * adjustment;
        } else {
            elapsed = FrameClock::GetSlomoFrameSeconds();
        }
        m_secondsRemaining = static_cast<float>(m_secondsRemaining - elapsed);
        if (m_secondsRemaining < 0.0f) {
            m_curPhase = ACTIVE_TIME_UP;
            m_entity->PostOwnEvent(kRunTimerUp, nullptr);
        }
    } else if (m_curPhase == ACTIVE_TIME_UP) {
        m_entity->PostOwnEvent(kRunTimerUp, nullptr);
    }
}

// 0x00518390
void CRiderRunClock::Halt()
{
    if (Scripts::FindSpawnedScriptWithId(TimerScriptId(m_entity)))
        TellTimerScript(m_entity, kHideRunTimer);
    m_curPhase = CLOCK_IDLE;
}

// 0x005183F0
void CRiderRunClock::Resume()
{
    if (m_curPhase == CLOCK_IDLE)
        return;
    m_curPhase = m_secondsRemaining > 0.0f ? CLOCK_TICKING : ACTIVE_TIME_UP;
    int16_t resumes = m_resumes;
    if (resumes < kMaxResumes) {
        double cap = PhysicsValue(m_entity, kDuration) * (kMaxResumes - resumes) * kResumeShare;
        if (!(m_secondsRemaining < cap))
            m_secondsRemaining = static_cast<float>(cap);
    }
    m_resumes = static_cast<int16_t>(resumes + 1);
    TellTimerScript(m_entity, kShowRunTimer);
}

// 0x005184A0
bool CRiderRunClock::Freeze(Scripts::CStruct*, Scripts::CScript*)
{
    if (m_curPhase != CLOCK_IDLE) {
        m_curPhase = ACTIVE_PAUSED;
        TellTimerScript(m_entity, kHideRunTimer);
    }
    return true;
}
// 0x005184F0
bool CRiderRunClock::Unpause(Scripts::CStruct*, Scripts::CScript*)
{
    Resume();
    return true;
}
// 0x00518500
bool CRiderRunClock::GetFactorComplete(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PushFloat(kFactorComplete,
                               static_cast<float>(m_secondsRemaining / PhysicsValue(m_entity, kDuration)));
    return true;
}
// 0x00518540
bool CRiderRunClock::GetRunTimerControllerId(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kRunTimerControllerId, TimerScriptId(m_entity));
    return true;
}
// 0x00518570
bool CRiderRunClock::GetRunTimerId(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kRunTimerId, SkaterIndex(m_entity) + kRunTimerIdBase);
    return true;
}
// 0x005185A0
bool CRiderRunClock::ChainBegan(Scripts::CStruct*, Scripts::CScript*)
{
    m_secondsRemaining = static_cast<float>(PhysicsValue(m_entity, kDuration));
    m_resumes = 0;
    m_curPhase = ACTIVE_PAUSED;
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderRunClock::Teardown,
         &GameObjects::CRiderRunClock::TickFrame,
         &CRiderStateReset::ConfigureFromData,
         &GameObjects::CRiderRunClock::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         &GameObjects::CRiderRunClock::FetchDiagnostics,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
