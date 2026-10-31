// The horse, network and race goals, 0x005608C0-0x0056123F (see goal_net_types.h).

#include "game/goals/goal_net_types.h"

#include <cstdint>
#include <cstdio>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_names.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/victory_conditions.h"
#include "game/skater/end_run.h"
#include "game/skater/skater.h"
#include "game/core/stubs.h"
#include "game/gfx/weather.h"
#include "game/core/code_address.h"

namespace Game {

namespace {

using namespace GoalNames;

constexpr uint32_t kHorseRunDone = 0xA32FAF13;     // (name unknown: the horse goal's run over)
constexpr uint32_t kHorseSpots = 0x92C6E839;       // (name unknown: the horse spots)
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

static VtableSlot kHorseGoalVtable_slots[] = {&Game::CHorseGoal::Destroy, &Game::CHorseGoal::ShouldExpire, &Game::CHorseGoal::IsExpired,
                 &Game::CGoal::IsActive, Nx::VirtualSlot3Jump, &Game::CHorseGoal::Expire, &Game::CGoal::LoadSaveData,
                 &Game::CGoal::GetSaveData, &Game::CGoal::HasTimeLimit, Stubs::ReturnTrue,
                 &Game::CGoal::HasComponentA302D7CD, &Game::CGoal::Activate, &Game::CGoal::SetActive,
                 &Game::CGoal::Deactivate, &Game::CGoal::SetInactive, &Game::CGoal::Win, &Game::CGoal::Lose,
                 &Game::CGoal::Update, &Game::CGoal::IsPaused, &Game::CGoal::SetFlag, &Game::CGoal::StageFlagSet,
                 &Game::CGoal::HasWon, &Game::CGoal::NetStageFlagSet, &Game::CGoal::HasWonGoalOrChild,
                 &Game::CGoal::IsInitialized, &Game::CGoal::CanRetry, &Game::CGoal::MarkWon,
                 &Game::CGoal::SetStageFlag, &Game::CGoal::SetNetStageFlag, &Game::CGoal::UnBeat};
void** const kHorseGoalVtable = reinterpret_cast<void**>(kHorseGoalVtable_slots);
static VtableSlot kNetGoalVtable_slots[] = {&Game::CNetGoal::Destroy, &Game::CNetGoal::ShouldExpire, &Game::CGoal::IsExpired,
                 &Game::CGoal::IsActive, Nx::VirtualSlot3Jump, &Game::CNetGoal::Expire, &Game::CGoal::LoadSaveData,
                 &Game::CGoal::GetSaveData, &Game::CGoal::HasTimeLimit, &Game::CNetGoal::TimerBeeps,
                 &Game::CNetGoal::HasComponentA302D7CD, &Game::CNetGoal::Activate, &Game::CGoal::SetActive,
                 &Game::CNetGoal::Deactivate, &Game::CGoal::SetInactive, &Game::CGoal::Win, &Game::CGoal::Lose,
                 &Game::CNetGoal::Update, &Game::CGoal::IsPaused, &Game::CGoal::SetFlag, &Game::CGoal::StageFlagSet,
                 &Game::CGoal::HasWon, &Game::CGoal::NetStageFlagSet, &Game::CGoal::HasWonGoalOrChild,
                 &Game::CGoal::IsInitialized, &Game::CGoal::CanRetry, &Game::CGoal::MarkWon,
                 &Game::CGoal::SetStageFlag, &Game::CGoal::SetNetStageFlag, &Game::CGoal::UnBeat};
void** const kNetGoalVtable = reinterpret_cast<void**>(kNetGoalVtable_slots);
static VtableSlot kRaceGoalVtable_slots[] = {&Game::CRaceGoal::Destroy, &Game::CRaceGoal::ShouldExpire, &Game::CRaceGoal::IsExpired,
                 &Game::CGoal::IsActive, Nx::VirtualSlot3Jump, &Game::CRaceGoal::Expire, &Game::CGoal::LoadSaveData,
                 &Game::CGoal::GetSaveData, &Game::CGoal::HasTimeLimit, Stubs::ReturnTrue,
                 &Game::CGoal::HasComponentA302D7CD, &Game::CRaceGoal::Activate, &Game::CGoal::SetActive,
                 &Game::CGoal::Deactivate, &Game::CGoal::SetInactive, &Game::CGoal::Win, &Game::CGoal::Lose,
                 &Game::CGoal::Update, &Game::CGoal::IsPaused, &Game::CGoal::SetFlag, &Game::CGoal::StageFlagSet,
                 &Game::CGoal::HasWon, &Game::CGoal::NetStageFlagSet, &Game::CGoal::HasWonGoalOrChild,
                 &Game::CGoal::IsInitialized, &Game::CGoal::CanRetry, &Game::CGoal::MarkWon,
                 &Game::CGoal::SetStageFlag, &Game::CGoal::SetNetStageFlag, &Game::CGoal::UnBeat};
void** const kRaceGoalVtable = reinterpret_cast<void**>(kRaceGoalVtable_slots);

GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }
uint32_t GameModeName() { return g_gameMode->GetNameChecksum(); }
// Whether every goal is completed (a goal attack's victory condition). CCompleteGoalsVictoryCondition::ConditionComplete
// doesn't use its object: the original calls it with whatever ECX holds, the game mode just asked for its name.
bool GoalsCompleted()
{
    return reinterpret_cast<CCompleteGoalsVictoryCondition*>(g_gameMode)->ConditionComplete();
}

struct PlayerSearch {
    void** m_vtable = Mdl::kPlayerSearchVtable;
    void* m_node = nullptr;
};

Script::CStruct* NewStructure()
{
    Script::CStruct* made = Script::NewStruct();
    return made ? made->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

} // namespace

// --- CHorseGoal -----------------------------------------------------------------------------------------------------

// 0x005608C0
CHorseGoal* CHorseGoal::Construct(Script::CStruct* params)
{
    CGoal::Construct(params);
    m_node.vtable = kHorseGoalVtable;
    return this;
}

// 0x005608E0
void CHorseGoal::DestroyBody()
{
    m_node.vtable = kHorseGoalVtable;
    CGoal::DestroyBody();
}

// 0x00560980
CHorseGoal* CHorseGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00560930
bool CHorseGoal::ShouldExpire()
{
    int done = 0;
    m_params->GetInteger(kHorseRunDone, &done, 1);
    return done == 1 ? false : CGoal::ShouldExpire();
}

// 0x005608F0
bool CHorseGoal::IsExpired()
{
    int done = 0;
    m_params->GetInteger(kHorseRunDone, &done, 1);
    return done == 1 ? false : CGoal::IsExpired();
}

// 0x00560970
void CHorseGoal::Expire() { CGoal::Expire(); }

// 0x005609B0
void CHorseGoal::Land()
{
    Script::CArray* spots;
    m_params->GetArray(kHorseSpots, &spots, 1);
    int count = static_cast<int>(spots->m_size);
    int i = 0;
    int score = 0;    // (both set before they're read)
    uint32_t flag = 0;
    for (; i < count; ++i) {
        auto* spot = reinterpret_cast<Script::CStruct*>(spots->GetElement(static_cast<uint32_t>(i)));
        spot->GetChecksum(kFlag, &flag, 1);
        int set;
        m_goalFlags->GetInteger(flag, &set, 1);
        if (set)
            continue;
        if (!spot->GetInteger(kScore, &score, 0))
            m_params->GetInteger(kScore, &score, 1);
        break;
    }
    if (i == count)
        return;
    int landed = *reinterpret_cast<int*>(
        static_cast<uint8_t*>(static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater())->GetScore()) + 0x20);
    if (landed > 0) {
        uint32_t id = GetGoalId();
        CGoalManager* manager = GetGoalManager();
        if (landed > score) {
            manager->SetGoalFlag(id, flag, 1, 0);
            manager->NextWaypoint(id);
        } else {
            manager->LoseGoal(id);
        }
    }
    m_params->AddInteger(kHorseRunDone, 0);
}

// --- CNetGoal -------------------------------------------------------------------------------------------------------

// 0x00560AC0
CNetGoal* CNetGoal::Construct(Script::CStruct* params)
{
    CGoal::Construct(params);
    m_node.vtable = kNetGoalVtable;
    m_endRunType = 1;
    m_initialized = false;
    return this;
}

// 0x00560AF0
void CNetGoal::DestroyBody()
{
    m_node.vtable = kNetGoalVtable;
    CGoal::DestroyBody();
}

// 0x00560B40
CNetGoal* CNetGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00560B00
int CNetGoal::HasComponentA302D7CD() { return (m_state & kStateNoTimeLimit) ? 0 : 1; }

// 0x00560B10
bool CNetGoal::TimerBeeps() { return GetGoalId() != kElimiskateInterval && GetGoalId() != kScavengerInterval; }

// 0x00560BC0
bool CNetGoal::Activate()
{
    NetManager()->ClearGameOver();
    return CGoal::Activate();
}

// 0x00560BE0
bool CNetGoal::Deactivate(bool force, bool resetTree)
{
    if (!force && GameModeName() == kNetGoalAttack)
        return false;
    return CGoal::Deactivate(force, resetTree);
}

// 0x00560B70
bool CNetGoal::Update()
{
    if (!m_initialized) {
        Init();
        m_initialized = true;
    }
    if (GameModeName() == kNetGoalAttack && GoalsCompleted() && Virtual<bool>(this, kVtIsActive)) {
        Virtual<void>(this, kVtExpire);
        return true;
    }
    return CGoal::Update();
}

// 0x00560C20
bool CNetGoal::ShouldExpire()
{
    bool timed = false;
    bool scavenger = false;
    if (GameModeName() == kNetElimiskate)
        timed = true;
    if (GameModeName() == kNetScavengerHunt || GameModeName() == kScavengerHunt) {
        scavenger = true;
        if (GetGoalId() != kScavengerCollectPeriod)
            timed = true;
    }
    if (GameModeName() == kNetGoalAttack)
        return GoalsCompleted() && AllSkatersRunDone();
    if (timed)
        return m_timeLeft <= 0;
    GameNet::Manager* net = NetManager();
    PlayerSearch search;
    if (!net->CountPlayers())
        return true;
    if (m_state & kStateNoTimeLimit)
        return false;
    if (!net->FinalScoresReceived() && net->InNetGame() && !scavenger)
        return false;
    if (m_endRunType == 2)
        return m_timeLeft <= 0;
    if (AllSkatersRunDone() && m_timeLeft <= 0)
        return true;
    for (void* player = net->FirstPlayerInfo(&search, false); player; player = net->NextPlayerInfo(&search, false)) {
        auto* skater = *reinterpret_cast<Obj::CCompositeObject**>(static_cast<uint8_t*>(player) + 0x14);
        auto* endRun = static_cast<Obj::CSkaterEndRunComponent*>(skater->GetComponent(kSkaterEndRun));
        if (!endRun->RunDone() && !reinterpret_cast<uint8_t*>(skater)[0x320] && m_endRunType != 2 && m_timeLeft <= 0) {
            m_flags |= kFlagEndOfRunStarted;
            endRun->EndRun(false);
        }
    }
    return net->IsGameOver();
}

// 0x00560E20
void CNetGoal::Expire()
{
    GameNet::Manager* net = NetManager();
    if (!m_params->ContainsFlag(kGameContinues))
        net->SetGameOver();
    SetTimeLeft(0);
    CGoal::Expire();
}

// --- CRaceGoal ------------------------------------------------------------------------------------------------------

// 0x00560E60
CRaceGoal* CRaceGoal::Construct(Script::CStruct* params)
{
    CGoal::Construct(params);
    m_node.vtable = kRaceGoalVtable;
    m_laps = 1;
    params->GetInteger(kRaceTotalLaps, &m_laps, 0);
    m_addTime = params->ContainsComponentNamed(kRaceAddTime);
    return this;
}

// 0x00560EE0
void CRaceGoal::DestroyBody()
{
    m_node.vtable = kRaceGoalVtable;
    CGoal::DestroyBody();
}

// 0x00560F60
CRaceGoal* CRaceGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00560EF0
bool CRaceGoal::Activate()
{
    m_lap = 1;
    return CGoal::Activate();
}

// 0x00560F00
bool CRaceGoal::IsExpired() { return !(m_state & kStateNoTimeLimit) && m_timeLeft <= 0; }

// 0x00560F20
bool CRaceGoal::ShouldExpire()
{
    if ((m_state & kStateNoTimeLimit) || m_timeLeft > 0)
        return false;
    CGoal::ShouldExpire();
    return true;
}

// 0x00560F40
void CRaceGoal::Expire()
{
    RunCallbackScript(kExpireScript);
    Virtual<bool>(this, kVtDeactivate, false, true);
}

// 0x00560F90
bool CRaceGoal::NextRaceWaypoint(uint32_t id)
{
    if (Virtual<bool>(this, kVtShouldExpire)) {
        Dbg::Printf("expired!\n"); // "expired!\n"
        return false;
    }
    Script::CArray* waypoints;
    m_params->GetArray(kRaceWaypoints, &waypoints, 1);
    for (uint32_t i = 0; i < waypoints->m_size; ++i) {
        auto* waypoint = reinterpret_cast<Script::CStruct*>(waypoints->GetElement(i));
        uint32_t flag;
        waypoint->GetChecksum(kFlag, &flag, 1);
        if (GoalFlagEquals(flag, 1, 0)) {
            if (i != waypoints->m_size - 1)
                continue;
            if (m_lap++ >= m_laps)
                return true;
            ResetGoalFlags();
            return NextRaceWaypoint(id);
        }
        Script::CStruct* params = NewStructure();
        params->AddChecksum(kGoalIdChecksum, id);
        if (i == 0 && m_laps > 1)
            params->AddInteger(kStartLap, m_lap);
        if (i == waypoints->m_size - 1 && m_lap == m_laps)
            params->AddChecksum(kLastWaypoint, 1);
        if (m_params->ContainsFlag(kQuickStart))
            params->AddChecksum(0u, kQuickStart);
        else
            params->RemoveFlag(kQuickStart);
        if (m_lap == 1 && i == 0) {
            params->AddInteger(kFirstWaypoint, 1);
        } else {
            params->AddChecksum(kScr, kCheckpointMessage);
            if (!(m_state & kStateNoTimeLimit) && m_addTime) {
                int time;
                waypoint->GetInteger(kTime, &time, 0);
                AddTime(time);
                params->AddInteger(kAddedTime, time);
            }
        }
        params->AppendStructureIfAny(waypoint);
        const char* goalName;
        m_params->GetString(kGoalName, &goalName, 1);
        char scriptName[0x100];
        std::snprintf(scriptName, sizeof(scriptName), "%s_next_waypoint_script_id", goalName); // "%s_next_waypoint_script_id"
        Script::SpawnScript(kRaceNextWaypoint, params, 0, nullptr, 0, Crc::GenerateCRCFromString(scriptName), false,
                            false, false, false);
        DeleteStructure(params);
        return true;
    }
    return true;
}

} // namespace Game
