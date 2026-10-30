// The skater's end-of-run component (Obj::CSkaterEndRunComponent): see end_run.h.

#include "game/skater/end_run.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/script/cscript.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064BDB8);
constexpr uint32_t kSkaterEndRun = 0xB1D5A3E1;

// Script member functions
constexpr uint32_t kIsInEndOfRun = 0x95BDCFCD, kEndOfRunStarted = 0x962DEA01, kEndofRunDone = 0x4C58771E,
                   kClearEndofRunStarted = 0xEA77E667, kGoalEndOfRunStarted = 0x00AFC6A7,
                   kGoalEndOfRunDone = 0x69A9E37F, kRunStarted = 0xB866BF9B;
// Events and scripts
constexpr uint32_t kRunHasEnded = 0x822E13A9, kGoalHasEnded = 0xAB676175, kForceEndOfRun = 0xF4CE3E97;

// Mdl::Skate (0x007CE478) and its 0x005651F0: a multiplayer game (a network game, or a game mode for more than one
// player). Not rewritten yet.
bool IsMultiplayerGame()
{
    return Mdl::Skate::IsMultiplayer() != 0;
}
// The game mode (0x007CE470)'s 0x00553550: whether a run is over at its end here (multiplayer, and the mode's
// stop_at_zero if it has the setting). Not rewritten yet.
bool GameModeEndsRuns()
{
    return static_cast<Game::CGameMode*>(static_cast<void*>(Game::g_gameMode))->ShouldStopAtZero();
}
// The network manager (0x007CCE14): whether it has told the server the local run is over (0x005332A0: its +0x39
// matches +0x38), and telling it (0x00533250).
bool ServerToldRunEnded()
{
    return GameVariable<GameNet::Manager*>(0x007CCE14)->GameEndedSent();
}
void TellServerRunEnded() { GameVariable<GameNet::Manager*>(0x007CCE14)->SendGameEnded(); }

// The skater object's +0xBC: a local skater (this machine's player).
bool IsLocalSkater(CCompositeObject* object) { return object->m_unknownBC != 0; }

} // namespace

// 0x00510F90
CBaseComponent* __cdecl CSkaterEndRunComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterEndRunComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterEndRunComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_flags = 0;
    component->m_type = kSkaterEndRun;
    return component;
}

// 0x00510D20
void CSkaterEndRunComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00510D40
CSkaterEndRunComponent* CSkaterEndRunComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00510D70
void CSkaterEndRunComponent::Update()
{
    if (!IsMultiplayerGame()) {
        VirtualSuspend(true);
        return;
    }
    if (IsLocalSkater(m_object) && (m_flags & END_RUN_DONE) && !ServerToldRunEnded())
        TellServerRunEnded();
}

// 0x00510DC0
int CSkaterEndRunComponent::CallMemberFunction(uint32_t name, Script::CStruct* /*params*/,
                                               Script::CScript* /*script*/)
{
    switch (name) {
    case kIsInEndOfRun:
        return (m_flags & END_RUN_STARTED) && !(m_flags & END_RUN_DONE) ? 1 : 0;
    case kGoalEndOfRunStarted:
        m_flags |= END_RUN_GOAL_STARTED;
        return 1;
    case kEndofRunDone:
        m_flags |= END_RUN_DONE;
        m_flags &= ~END_RUN_FORCED;
        return 1;
    case kGoalEndOfRunDone:
        m_flags |= END_RUN_GOAL_DONE;
        m_flags &= ~END_RUN_FLAG_20;
        return 1;
    case kEndOfRunStarted:
        m_flags |= END_RUN_STARTED;
        return 1;
    case kRunStarted:
        m_flags &= ~(END_RUN_STARTED | END_RUN_GOAL_STARTED | END_RUN_DONE | END_RUN_GOAL_DONE | END_RUN_FORCED |
                     END_RUN_FLAG_20);
        return 1;
    case kClearEndofRunStarted:
        m_flags &= ~(END_RUN_STARTED | END_RUN_FORCED);
        return 1;
    default:
        return 2;
    }
}

// 0x00510D30
uint32_t* CSkaterEndRunComponent::GetFlags(uint32_t* out)
{
    *out = m_flags;
    return out;
}

// 0x00510EA0
void CSkaterEndRunComponent::EndRun(bool force)
{
    if (!force && !GameModeEndsRuns()) {
        m_object->SelfEvent(kRunHasEnded, nullptr);
        m_flags &= ~END_RUN_DONE;
        return;
    }
    if (!(m_flags & END_RUN_FORCED) && !(m_flags & END_RUN_DONE)) {
        m_flags |= END_RUN_FORCED;
        if (IsLocalSkater(m_object))
            Script::RunScript(kForceEndOfRun, nullptr, m_object, false, nullptr);
    }
}

// 0x00510F10
void CSkaterEndRunComponent::EndGoalRun(void* /*unused*/)
{
    m_flags &= ~END_RUN_GOAL_DONE;
    m_object->SelfEvent(kGoalHasEnded, nullptr);
}

// 0x00510F30
bool CSkaterEndRunComponent::RunDone() { return (m_flags & END_RUN_DONE) != 0; }
// 0x00510F40
bool CSkaterEndRunComponent::GoalRunDone() { return (m_flags & END_RUN_GOAL_DONE) != 0; }
// 0x00510F50
bool CSkaterEndRunComponent::EndOfRunStarted() { return (m_flags & END_RUN_STARTED) != 0; }
// 0x00510F60
void CSkaterEndRunComponent::ClearEndOfRunStarted() { m_flags &= ~END_RUN_STARTED; }
// 0x00510F70
void CSkaterEndRunComponent::ClearGoalEndOfRunStarted() { m_flags &= ~END_RUN_GOAL_STARTED; }
// 0x00510F80
void CSkaterEndRunComponent::ClearForced() { m_flags &= ~END_RUN_FORCED; }

} // namespace Obj

REPLACE(0x00510F90, Obj::CSkaterEndRunComponent::Create);
REPLACE(0x00510D20, &Obj::CSkaterEndRunComponent::DestroyBody);
REPLACE(0x00510D40, &Obj::CSkaterEndRunComponent::Destroy);
REPLACE(0x00510D70, &Obj::CSkaterEndRunComponent::Update);
REPLACE(0x00510DC0, &Obj::CSkaterEndRunComponent::CallMemberFunction);
REPLACE(0x00510D30, &Obj::CSkaterEndRunComponent::GetFlags);
REPLACE(0x00510EA0, &Obj::CSkaterEndRunComponent::EndRun);
REPLACE(0x00510F10, &Obj::CSkaterEndRunComponent::EndGoalRun);
REPLACE(0x00510F30, &Obj::CSkaterEndRunComponent::RunDone);
REPLACE(0x00510F40, &Obj::CSkaterEndRunComponent::GoalRunDone);
REPLACE(0x00510F50, &Obj::CSkaterEndRunComponent::EndOfRunStarted);
REPLACE(0x00510F60, &Obj::CSkaterEndRunComponent::ClearEndOfRunStarted);
REPLACE(0x00510F70, &Obj::CSkaterEndRunComponent::ClearGoalEndOfRunStarted);
REPLACE(0x00510F80, &Obj::CSkaterEndRunComponent::ClearForced);
