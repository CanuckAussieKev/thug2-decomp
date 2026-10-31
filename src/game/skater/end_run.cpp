// The skater's end-of-run component (GameObjects::CRiderSessionEnd): see end_run.h.

#include "game/skater/end_run.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/script/cscript.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterEndRun = 0xB1D5A3E1;

// Script member functions
constexpr uint32_t kIsInEndOfRun = 0x95BDCFCD, kEndOfRunStarted = 0x962DEA01, kEndofRunDone = 0x4C58771E,
                   kClearEndofRunStarted = 0xEA77E667, kGoalEndOfRunStarted = 0x00AFC6A7,
                   kGoalEndOfRunDone = 0x69A9E37F, kRunStarted = 0xB866BF9B;
// Events and scripts
constexpr uint32_t kRunHasEnded = 0x822E13A9, kGoalHasEnded = 0xAB676175, kForceEndOfRun = 0xF4CE3E97;

// GameModules::Skate (0x007CE478) and its 0x005651F0: a multiplayer game (a network game, or a game mode for more than one
// player). Not rewritten yet.
bool IsVersusSession()
{
    return GameModules::Skate::IsMultiplayer() != 0;
}
// The game mode (0x007CE470)'s 0x00553550: whether a run is over at its end here (multiplayer, and the mode's
// stop_at_zero if it has the setting). Not rewritten yet.
bool GameModeEndsRuns()
{
    return static_cast<GoalSystem::CMatchRules*>(static_cast<void*>(GoalSystem::g_gameMode))->ClockHaltsAtZero();
}
// The network manager (0x007CCE14): whether it has told the server the local run is over (0x005332A0: its +0x39
// matches +0x38), and telling it (0x00533250).
bool ServerToldRunEnded()
{
    return OnlinePlay::g_gameNetManager->GameEndedSent();
}
void TellServerRunEnded() { OnlinePlay::g_gameNetManager->SendGameEnded(); }

// The skater object's +0xBC: a local skater (this machine's player).
bool IsLocalSkater(CGameEntity* object) { return object->m_unknownBC != 0; }

} // namespace

// 0x00510F90
CObjectModule* __cdecl CRiderSessionEnd::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderSessionEnd));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderSessionEnd*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_flagBits = 0;
    component->m_kindTag = kSkaterEndRun;
    return component;
}

// 0x00510D20
void CRiderSessionEnd::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00510D40
CRiderSessionEnd* CRiderSessionEnd::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00510D70
void CRiderSessionEnd::TickFrame()
{
    if (!IsVersusSession()) {
        VirtualSuspend(true);
        return;
    }
    if (IsLocalSkater(m_entity) && (m_flagBits & END_RUN_DONE) && !ServerToldRunEnded())
        TellServerRunEnded();
}

// 0x00510DC0
int CRiderSessionEnd::InvokeMethodByName(uint32_t name, Scripts::CStruct* /*params*/,
                                               Scripts::CScript* /*script*/)
{
    switch (name) {
    case kIsInEndOfRun:
        return (m_flagBits & END_RUN_STARTED) && !(m_flagBits & END_RUN_DONE) ? 1 : 0;
    case kGoalEndOfRunStarted:
        m_flagBits |= END_RUN_GOAL_STARTED;
        return 1;
    case kEndofRunDone:
        m_flagBits |= END_RUN_DONE;
        m_flagBits &= ~END_RUN_FORCED;
        return 1;
    case kGoalEndOfRunDone:
        m_flagBits |= END_RUN_GOAL_DONE;
        m_flagBits &= ~END_RUN_FLAG_20;
        return 1;
    case kEndOfRunStarted:
        m_flagBits |= END_RUN_STARTED;
        return 1;
    case kRunStarted:
        m_flagBits &= ~(END_RUN_STARTED | END_RUN_GOAL_STARTED | END_RUN_DONE | END_RUN_GOAL_DONE | END_RUN_FORCED |
                     END_RUN_FLAG_20);
        return 1;
    case kClearEndofRunStarted:
        m_flagBits &= ~(END_RUN_STARTED | END_RUN_FORCED);
        return 1;
    default:
        return 2;
    }
}

// 0x00510D30
uint32_t* CRiderSessionEnd::FetchBits(uint32_t* out)
{
    *out = m_flagBits;
    return out;
}

// 0x00510EA0
void CRiderSessionEnd::EndRun(bool force)
{
    if (!force && !GameModeEndsRuns()) {
        m_entity->PostOwnEvent(kRunHasEnded, nullptr);
        m_flagBits &= ~END_RUN_DONE;
        return;
    }
    if (!(m_flagBits & END_RUN_FORCED) && !(m_flagBits & END_RUN_DONE)) {
        m_flagBits |= END_RUN_FORCED;
        if (IsLocalSkater(m_entity))
            Scripts::LaunchScript(kForceEndOfRun, nullptr, m_entity, false, nullptr);
    }
}

// 0x00510F10
void CRiderSessionEnd::FinishObjectiveRun(void* /*unused*/)
{
    m_flagBits &= ~END_RUN_GOAL_DONE;
    m_entity->PostOwnEvent(kGoalHasEnded, nullptr);
}

// 0x00510F30
bool CRiderSessionEnd::RunDone() { return (m_flagBits & END_RUN_DONE) != 0; }
// 0x00510F40
bool CRiderSessionEnd::GoalRunDone() { return (m_flagBits & END_RUN_GOAL_DONE) != 0; }
// 0x00510F50
bool CRiderSessionEnd::EndOfRunStarted() { return (m_flagBits & END_RUN_STARTED) != 0; }
// 0x00510F60
void CRiderSessionEnd::ClearEndOfRunStarted() { m_flagBits &= ~END_RUN_STARTED; }
// 0x00510F70
void CRiderSessionEnd::ClearGoalEndOfRunStarted() { m_flagBits &= ~END_RUN_GOAL_STARTED; }
// 0x00510F80
void CRiderSessionEnd::ClearForced() { m_flagBits &= ~END_RUN_FORCED; }

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderSessionEnd::Teardown,
         &GameObjects::CRiderSessionEnd::TickFrame,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CRiderSessionEnd::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
