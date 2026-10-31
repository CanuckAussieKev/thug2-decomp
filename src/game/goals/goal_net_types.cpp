// The horse, network and race goals, 0x005608C0-0x0056123F (see goal_net_types.h).

#include "game/goals/goal_net_types.h"

#include <cstdint>
#include <cstdio>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_names.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/victory_conditions.h"
#include "game/skater/end_run.h"
#include "game/skater/rider.h"
#include "game/core/stubs.h"
#include "game/gfx/weather.h"
#include "game/core/code_address.h"

namespace GoalSystem {

namespace {

using namespace GoalNames;

constexpr uint32_t kHorseRunDone = 0xA32FAF13;     // (name unknown: the horse goal's run over)
constexpr uint32_t kLetterSpots = 0x92C6E839;       // (name unknown: the horse spots)
constexpr uint32_t kElimiskateInterval = 0xF92E5D89; // elimiskate_interval
constexpr uint32_t kScavengerInterval = 0x8A6B04AD;  // scavenger_interval
constexpr uint32_t kNetElimiskate = 0x10F84276;    // netelimiskate
constexpr uint32_t kNetScavengerHunt = 0xFC13B531; // netscavengerhunt
constexpr uint32_t kScavengerHunt = 0x846504FC;    // scavengerhunt
constexpr uint32_t kScavengerCollectPeriod = 0xD4F04E7F; // scavenger_collect_period
constexpr uint32_t kGameContinues = 0xA4BA22BA;    // game_continues_after_expiration
constexpr uint32_t kRaceTotalLaps = 0x1DAAE95B;    // race_total_laps
constexpr uint32_t kRaceAddTime = 0xA4038EC9;      // (name unknown: a race whose waypoints add time)
constexpr uint32_t kRaceWaypoints = 0xA77B1F07;    // race_waypoints
constexpr uint32_t kStartLap = 0x61FB20D5;         // start_lap
constexpr uint32_t kLastWaypoint = 0x3442E2E0;     // last_waypoint
constexpr uint32_t kFirstWaypoint = 0xF83B9525;    // first_waypoint
constexpr uint32_t kScr = 0xA6D2D890;              // scr
constexpr uint32_t kCheckpointMessage = 0x1E1B568A; // goal_race_checkpoint_message
constexpr uint32_t kAddedTime = 0xC906B763;        // added_time
constexpr uint32_t kGoalName = 0xBFECC45B;         // goal_name
constexpr uint32_t kRaceNextWaypoint = 0xD3741449; // goal_race_next_waypoint

static VtableSlot kHorseGoalVtable_slots[] = {&GoalSystem::CLetterGameObjective::Teardown, &GoalSystem::CLetterGameObjective::WantsToEnd, &GoalSystem::CLetterGameObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CLetterGameObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue,
                 &GoalSystem::CObjective::HasComponentA302D7CD, &GoalSystem::CObjective::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::CObjective::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CObjective::Win, &GoalSystem::CObjective::Lose,
                 &GoalSystem::CObjective::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CObjective::SetFlag, &GoalSystem::CObjective::StageFlagSet,
                 &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet, &GoalSystem::CObjective::HasWonGoalOrChild,
                 &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain, &GoalSystem::CObjective::MarkWon,
                 &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag, &GoalSystem::CObjective::UnBeat};
void** const kHorseGoalVtable = reinterpret_cast<void**>(kHorseGoalVtable_slots);
static VtableSlot kNetGoalVtable_slots[] = {&GoalSystem::COnlineObjective::Teardown, &GoalSystem::COnlineObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::COnlineObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, &GoalSystem::COnlineObjective::TimerBeeps,
                 &GoalSystem::COnlineObjective::HasComponentA302D7CD, &GoalSystem::COnlineObjective::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::COnlineObjective::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CObjective::Win, &GoalSystem::CObjective::Lose,
                 &GoalSystem::COnlineObjective::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CObjective::SetFlag, &GoalSystem::CObjective::StageFlagSet,
                 &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet, &GoalSystem::CObjective::HasWonGoalOrChild,
                 &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain, &GoalSystem::CObjective::MarkWon,
                 &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag, &GoalSystem::CObjective::UnBeat};
void** const kNetGoalVtable = reinterpret_cast<void**>(kNetGoalVtable_slots);
static VtableSlot kRaceGoalVtable_slots[] = {&GoalSystem::CRaceObjective::Teardown, &GoalSystem::CRaceObjective::WantsToEnd, &GoalSystem::CRaceObjective::HasRunOut,
                 &GoalSystem::CObjective::IsOperative, Engine3D::VirtualSlot3Jump, &GoalSystem::CRaceObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock,
                 &GoalSystem::CObjective::CaptureSaveBlock, &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue,
                 &GoalSystem::CObjective::HasComponentA302D7CD, &GoalSystem::CRaceObjective::Activate, &GoalSystem::CObjective::ChangeLive,
                 &GoalSystem::CObjective::Disengage, &GoalSystem::CObjective::ClearEngagedBit, &GoalSystem::CObjective::Win, &GoalSystem::CObjective::Lose,
                 &GoalSystem::CObjective::TickFrame, &GoalSystem::CObjective::IsFrozen, &GoalSystem::CObjective::SetFlag, &GoalSystem::CObjective::StageFlagSet,
                 &GoalSystem::CObjective::HasWon, &GoalSystem::CObjective::NetStageFlagSet, &GoalSystem::CObjective::HasWonGoalOrChild,
                 &GoalSystem::CObjective::IsPrepared, &GoalSystem::CObjective::MayTryAgain, &GoalSystem::CObjective::MarkWon,
                 &GoalSystem::CObjective::SetStageFlag, &GoalSystem::CObjective::SetNetStageFlag, &GoalSystem::CObjective::UnBeat};
void** const kRaceGoalVtable = reinterpret_cast<void**>(kRaceGoalVtable_slots);

OnlinePlay::Hub* NetSessionSlot() { return OnlinePlay::g_onlineHub; }
uint32_t ActiveRulesetLabel() { return g_gameMode->GetNameKey(); }
// Whether every goal is completed (a goal attack's victory condition). CCompleteGoalsVictoryCondition::RuleSatisfied
// doesn't use its object: the original calls it with whatever ECX holds, the game mode just asked for its name.
bool GoalsCompleted()
{
    return reinterpret_cast<CCompleteGoalsVictoryCondition*>(g_gameMode)->RuleSatisfied();
}

struct PlayerSearch {
    void** m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};

Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* made = Scripts::SpawnParamBag();
    return made ? made->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

} // namespace

// --- CLetterGameObjective -----------------------------------------------------------------------------------------------------

// 0x005608C0
CLetterGameObjective* CLetterGameObjective::MakeNew(Scripts::CStruct* params)
{
    CObjective::MakeNew(params);
    m_listLink.vtable = kHorseGoalVtable;
    return this;
}

// 0x005608E0
void CLetterGameObjective::DestroyBody()
{
    m_listLink.vtable = kHorseGoalVtable;
    CObjective::DestroyBody();
}

// 0x00560980
CLetterGameObjective* CLetterGameObjective::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00560930
bool CLetterGameObjective::WantsToEnd()
{
    int done = 0;
    m_args->FetchIntValue(kHorseRunDone, &done, 1);
    return done == 1 ? false : CObjective::WantsToEnd();
}

// 0x005608F0
bool CLetterGameObjective::HasRunOut()
{
    int done = 0;
    m_args->FetchIntValue(kHorseRunDone, &done, 1);
    return done == 1 ? false : CObjective::HasRunOut();
}

// 0x00560970
void CLetterGameObjective::Expire() { CObjective::Expire(); }

// 0x005609B0
void CLetterGameObjective::Land()
{
    Scripts::CArray* spots;
    m_args->FetchList(kLetterSpots, &spots, 1);
    int count = static_cast<int>(spots->m_extent);
    int i = 0;
    int score = 0;    // (both set before they're read)
    uint32_t flag = 0;
    for (; i < count; ++i) {
        auto* spot = reinterpret_cast<Scripts::CStruct*>(spots->FetchPart(static_cast<uint32_t>(i)));
        spot->ReadHashValue(kFlag, &flag, 1);
        int set;
        m_goalFlags->FetchIntValue(flag, &set, 1);
        if (set)
            continue;
        if (!spot->FetchIntValue(kScore, &score, 0))
            m_args->FetchIntValue(kScore, &score, 1);
        break;
    }
    if (i == count)
        return;
    int landed = *reinterpret_cast<int*>(
        static_cast<uint8_t*>(static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider())->GetPointsTotal()) + 0x20);
    if (landed > 0) {
        uint32_t id = GetObjectiveKey();
        CGoalManager* manager = GetObjectiveBoard();
        if (landed > score) {
            manager->RaiseBit(id, flag, 1, 0);
            manager->StepToNextWaypoint(id);
        } else {
            manager->FailObjective(id);
        }
    }
    m_args->PutInt(kHorseRunDone, 0);
}

// --- COnlineObjective -------------------------------------------------------------------------------------------------------

// 0x00560AC0
COnlineObjective* COnlineObjective::MakeNew(Scripts::CStruct* params)
{
    CObjective::MakeNew(params);
    m_listLink.vtable = kNetGoalVtable;
    m_runEnding = 1;
    m_isPrepared = false;
    return this;
}

// 0x00560AF0
void COnlineObjective::DestroyBody()
{
    m_listLink.vtable = kNetGoalVtable;
    CObjective::DestroyBody();
}

// 0x00560B40
COnlineObjective* COnlineObjective::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00560B00
int COnlineObjective::HasComponentA302D7CD() { return (m_curPhase & kStateNoTimeLimit) ? 0 : 1; }

// 0x00560B10
bool COnlineObjective::TimerBeeps() { return GetObjectiveKey() != kElimiskateInterval && GetObjectiveKey() != kScavengerInterval; }

// 0x00560BC0
bool COnlineObjective::Activate()
{
    NetSessionSlot()->ResetMatchEnded();
    return CObjective::Activate();
}

// 0x00560BE0
bool COnlineObjective::Disengage(bool force, bool resetTree)
{
    if (!force && ActiveRulesetLabel() == kNetGoalAttack)
        return false;
    return CObjective::Disengage(force, resetTree);
}

// 0x00560B70
bool COnlineObjective::TickFrame()
{
    if (!m_isPrepared) {
        PrepareAll();
        m_isPrepared = true;
    }
    if (ActiveRulesetLabel() == kNetGoalAttack && GoalsCompleted() && Virtual<bool>(this, kVtIsActive)) {
        Virtual<void>(this, kVtExpire);
        return true;
    }
    return CObjective::TickFrame();
}

// 0x00560C20
bool COnlineObjective::WantsToEnd()
{
    bool timed = false;
    bool scavenger = false;
    if (ActiveRulesetLabel() == kNetElimiskate)
        timed = true;
    if (ActiveRulesetLabel() == kNetScavengerHunt || ActiveRulesetLabel() == kScavengerHunt) {
        scavenger = true;
        if (GetObjectiveKey() != kScavengerCollectPeriod)
            timed = true;
    }
    if (ActiveRulesetLabel() == kNetGoalAttack)
        return GoalsCompleted() && AllSkatersRunDone();
    if (timed)
        return m_secondsRemaining <= 0;
    OnlinePlay::Hub* net = NetSessionSlot();
    PlayerSearch search;
    if (!net->CountPlayers())
        return true;
    if (m_curPhase & kStateNoTimeLimit)
        return false;
    if (!net->FinalScoresReceived() && net->InNetGame() && !scavenger)
        return false;
    if (m_runEnding == 2)
        return m_secondsRemaining <= 0;
    if (AllSkatersRunDone() && m_secondsRemaining <= 0)
        return true;
    for (void* player = net->BeginPlayerWalk(&search, false); player; player = net->StepPlayerWalk(&search, false)) {
        auto* skater = *reinterpret_cast<GameObjects::CGameEntity**>(static_cast<uint8_t*>(player) + 0x14);
        auto* endRun = static_cast<GameObjects::CRiderSessionEnd*>(skater->FetchModule(kSkaterEndRun));
        if (!endRun->RunDone() && !reinterpret_cast<uint8_t*>(skater)[0x320] && m_runEnding != 2 && m_secondsRemaining <= 0) {
            m_flagBits |= kFlagEndOfRunStarted;
            endRun->EndRun(false);
        }
    }
    return net->HasMatchEnded();
}

// 0x00560E20
void COnlineObjective::Expire()
{
    OnlinePlay::Hub* net = NetSessionSlot();
    if (!m_args->HasBareFlag(kGameContinues))
        net->MarkMatchEnded();
    SetSecondsRemaining(0);
    CObjective::Expire();
}

// --- CRaceObjective ------------------------------------------------------------------------------------------------------

// 0x00560E60
CRaceObjective* CRaceObjective::MakeNew(Scripts::CStruct* params)
{
    CObjective::MakeNew(params);
    m_listLink.vtable = kRaceGoalVtable;
    m_laps = 1;
    params->FetchIntValue(kRaceTotalLaps, &m_laps, 0);
    m_addTime = params->HasEntryNamed(kRaceAddTime);
    return this;
}

// 0x00560EE0
void CRaceObjective::DestroyBody()
{
    m_listLink.vtable = kRaceGoalVtable;
    CObjective::DestroyBody();
}

// 0x00560F60
CRaceObjective* CRaceObjective::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00560EF0
bool CRaceObjective::Activate()
{
    m_lap = 1;
    return CObjective::Activate();
}

// 0x00560F00
bool CRaceObjective::HasRunOut() { return !(m_curPhase & kStateNoTimeLimit) && m_secondsRemaining <= 0; }

// 0x00560F20
bool CRaceObjective::WantsToEnd()
{
    if ((m_curPhase & kStateNoTimeLimit) || m_secondsRemaining > 0)
        return false;
    CObjective::WantsToEnd();
    return true;
}

// 0x00560F40
void CRaceObjective::Expire()
{
    InvokeHookScript(kExpireScript);
    Virtual<bool>(this, kVtDeactivate, false, true);
}

// 0x00560F90
bool CRaceObjective::AdvanceRaceGate(uint32_t id)
{
    if (Virtual<bool>(this, kVtShouldExpire)) {
        DebugOut::Printf("expired!\n"); // "expired!\n"
        return false;
    }
    Scripts::CArray* waypoints;
    m_args->FetchList(kRaceWaypoints, &waypoints, 1);
    for (uint32_t i = 0; i < waypoints->m_extent; ++i) {
        auto* waypoint = reinterpret_cast<Scripts::CStruct*>(waypoints->FetchPart(i));
        uint32_t flag;
        waypoint->ReadHashValue(kFlag, &flag, 1);
        if (BitMatches(flag, 1, 0)) {
            if (i != waypoints->m_extent - 1)
                continue;
            if (m_lap++ >= m_laps)
                return true;
            ZeroObjectiveBits();
            return AdvanceRaceGate(id);
        }
        Scripts::CStruct* params = SpawnParamTable();
        params->PutNameKey(kGoalIdChecksum, id);
        if (i == 0 && m_laps > 1)
            params->PutInt(kStartLap, m_lap);
        if (i == waypoints->m_extent - 1 && m_lap == m_laps)
            params->PutNameKey(kLastWaypoint, 1);
        if (m_args->HasBareFlag(kQuickStart))
            params->PutNameKey(0u, kQuickStart);
        else
            params->RemoveFlag(kQuickStart);
        if (m_lap == 1 && i == 0) {
            params->PutInt(kFirstWaypoint, 1);
        } else {
            params->PutNameKey(kScr, kCheckpointMessage);
            if (!(m_curPhase & kStateNoTimeLimit) && m_addTime) {
                int time;
                waypoint->FetchIntValue(kTime, &time, 0);
                AdvanceClock(time);
                params->PutInt(kAddedTime, time);
            }
        }
        params->AppendStructureIfAny(waypoint);
        const char* goalName;
        m_args->FetchText(kGoalName, &goalName, 1);
        char scriptName[0x100];
        std::snprintf(scriptName, sizeof(scriptName), "%s_next_waypoint_script_id", goalName); // "%s_next_waypoint_script_id"
        Scripts::SpawnScript(kRaceNextWaypoint, params, 0, nullptr, 0, Checksums::HashFromText(scriptName), false,
                            false, false, false);
        DeleteStructure(params);
        return true;
    }
    return true;
}

} // namespace Game
