// GoalSystem::CObjective while it runs: its scripts, links, update, expiry, the skater's end of run, gaps, waypoints, clusters,
// tricks, records (0x00554A80-0x00555A5F, 0x00556230; see objective.h).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "game/goals/objective.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/end_run.h"
#include "game/rider/rider.h"
#include "platform/stopwatch.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "engine/objects/compositemgr.h"
#include "engine/gfx/tod_manager.h"

namespace GoalSystem {

float g_goalRunMinusMilliseconds{-1000.0f};
float g_goalRunInchesToFeet{0.083333336f};

namespace {

using namespace GoalCalls;
using namespace GoalNames;

// The x87 code's float to int conversion (_ftol): truncation.
int Truncate(double value) { return static_cast<int>(value); }

// A new link (the manager's Allocate, as the original's inlined new).
GoalEdge* AllocChainLink()
{
    auto* link = static_cast<GoalEdge*>(MemSys::Alloc(sizeof(GoalEdge)));
    if (link) {
        link->m_objectiveKey = 0;
        link->m_succ = nullptr;
        link->m_won = false;
    }
    return link;
}

OnlinePlay::Hub* NetSessionSlot() { return OnlinePlay::g_onlineHub; }
GameObjects::CGameEntity* LocalSkater() { return SkateCalls::GetHomeRider(); }
// The game mode's name (0x005532A0) and whether it beeps the timer (0x00553150).
uint32_t ActiveRulesetLabel() { return static_cast<GoalSystem::CMatchRules*>(SkateCalls::GameMode())->GetNameKey(); }
bool GameModeTimerBeeps() { return static_cast<GoalSystem::CMatchRules*>(SkateCalls::GameMode())->ClockBeepsNearEnd(); }
// A field at `offset`.
float FloatAt(const void* object, uint32_t offset)
{
    return *reinterpret_cast<const float*>(static_cast<const uint8_t*>(object) + offset);
}
// How often the score (GameModules::PointTally) has seen trick `trick` with no spin, not perfect, one tap (0x005624C0).
int TrickCount(void* score, uint32_t trick)
{
    return static_cast<GameModules::PointTally*>(score)->CountTrickAfterTaps(trick, 0, false, 1);
}
// An object's member function by name (vtable +0x0C).
bool CallObjectMember(GameObjects::CGameEntity* object, uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    return reinterpret_cast<bool(__thiscall*)(void*, uint32_t, Scripts::CStruct*, Scripts::CScript*)>(
        (*reinterpret_cast<void***>(object))[3])(object, name, params, script);
}
GameObjects::CRiderSessionEnd* EndRunOf(GameObjects::CGameEntity* skater)
{
    return static_cast<GameObjects::CRiderSessionEnd*>(skater->FetchModule(kSkaterEndRun));
}

} // namespace

// 0x00554A80
bool CObjective::InvokeHookScript(uint32_t name)
{
    uint32_t script = name;
    if (!m_args->ReadHashValue(name, &script, 0))
        return false;
    Scripts::LaunchScript(script, m_args, nullptr, false, nullptr);
    return true;
}

// 0x00554AC0
void CObjective::AttachUpperLink(uint32_t id)
{
    if (!m_parents->m_objectiveKey) {
        m_parents->m_objectiveKey = id;
        return;
    }
    GoalEdge* link = AllocChainLink();
    link->m_objectiveKey = id;
    link->m_succ = m_parents;
    m_parents = link;
}

// 0x00554B40
void CObjective::AttachChild(uint32_t id)
{
    if (!m_subNodes->m_objectiveKey) {
        m_subNodes->m_objectiveKey = id;
        return;
    }
    for (GoalEdge* link = m_subNodes; link && link->m_objectiveKey; link = link->m_succ) {
        if (link->m_objectiveKey == id)
            return;
    }
    GoalEdge* link = AllocChainLink();
    link->m_objectiveKey = id;
    link->m_succ = m_subNodes;
    m_subNodes = link;
}

// 0x00554BD0
void CObjective::Uninitialize(bool children, int depth)
{
    if (Virtual<bool>(this, kVtIsActive))
        Virtual<bool>(this, kVtDeactivate, true, true);
    uint32_t script;
    if (m_args->ReadHashValue(kUninit, &script, 0))
        Scripts::LaunchScript(script, m_args, nullptr, false, nullptr);
    m_flagBits &= ~kFlagInitialized;
    if (!children)
        return;
    void* manager = ObjectiveBoard();
    for (GoalEdge* link = m_subNodes; link && link->m_objectiveKey; link = link->m_succ)
        GetGoal(manager, link->m_objectiveKey, 1)->Uninitialize(children, depth + 1);
}

// 0x00554C70
bool CObjective::Lose()
{
    if (!Virtual<bool>(this, kVtIsActive))
        return false;
    uint32_t script;
    if (m_args->ReadHashValue(kFail, &script, 0))
        Scripts::LaunchScript(script, m_args, nullptr, false, nullptr);
    Virtual<bool>(this, kVtDeactivate, true, true);
    return true;
}

// 0x00554CD0
bool CObjective::HasRunOut()
{
    OnlinePlay::Hub* net = NetSessionSlot();
    if (!LocalSkater() || !net->CountPlayers())
        return true;
    if (m_curPhase & kStateNoTimeLimit)
        return false;
    return m_secondsRemaining <= 0;
}

// 0x00554D10
bool CObjective::IsExpiredAtEndOfRun(bool force)
{
    OnlinePlay::Hub* net = NetSessionSlot();
    GameObjects::CGameEntity* skater = LocalSkater();
    if (!skater)
        return true;
    GameObjects::CRiderSessionEnd* endRun = EndRunOf(skater);
    if (!net->CountPlayers())
        return true;
    if (m_curPhase & kStateNoTimeLimit)
        return false;
    if (m_runEnding == 0 || m_runEnding == 1) {
        bool done = m_runEnding == 1 ? endRun->RunDone() : endRun->GoalRunDone();
        if (done)
            return m_secondsRemaining <= 0;
    }
    if (m_secondsRemaining > 0)
        return false;
    // Out of time: the run is ended, the goal expires once it's done.
    if (m_runEnding == 0) {
        m_flagBits |= kFlagEndOfRunStarted;
        endRun->FinishObjectiveRun(reinterpret_cast<void*>(static_cast<uintptr_t>(force)));
    } else if (m_runEnding == 1) {
        m_flagBits |= kFlagEndOfRunStarted;
        endRun->EndRun(force);
    }
    return false;
}

// 0x00556230
bool CObjective::WantsToEnd() { return IsExpiredAtEndOfRun(false); }

// 0x00554DD0
bool CObjective::TickFrame()
{
    if (Virtual<bool>(this, kVtIsActive) && !Virtual<bool>(this, kVtIsPaused))
        InvokeHookScript(kActiveScript);
    if (!Virtual<bool>(this, kVtIsActive) || Virtual<bool>(this, kVtIsPaused))
        return true;
    if (Virtual<bool>(this, kVtHasTimeLimit) && m_secondsRemaining >= 1) {
        int before = m_secondsRemaining;
        m_secondsRemaining += Truncate(FrameClock::GetSlomoFrameSeconds() * g_goalRunMinusMilliseconds); // (-1000)
        m_elapsed += Truncate(FrameClock::GetSlomoFrameSeconds() * Gfx2D::g_thousand);  // (1000)
        // The last ten seconds beep each second.
        if (m_secondsRemaining < 10000 && before / 1000 > m_secondsRemaining / 1000 && Virtual<bool>(this, kVtTimerBeeps) &&
            GameModeTimerBeeps())
            Scripts::LaunchScript(kTimerRunoutBeep, nullptr, nullptr, false, nullptr);
    }
    if (m_args->HasBareFlag(kRequireCombo) && !m_args->HasBareFlag(kChainActiveFlag)) {
        GameObjects::CGameEntity* skater = LocalSkater();
        if (skater) {
            const auto* score = static_cast<const uint8_t*>(static_cast<GameObjects::CRider*>(skater)->GetPointsTotal());
            // (the combo's points: its base score times its multiplier)
            if (score &&
                Truncate(*reinterpret_cast<const int*>(score + 0x14) * static_cast<double>(FloatAt(score, 0x60))) > 0)
                m_args->PutNameKey(kNoName, kChainActiveFlag);
        }
    }
    if (Virtual<bool>(this, kVtHasTimeLimit) && Virtual<bool>(this, kVtShouldExpire))
        Virtual<void>(this, kVtExpire);
    return true;
}

// 0x00554F40
void CObjective::Expire()
{
    uint32_t script;
    if (m_args->ReadHashValue(kExpireScript, &script, 0))
        Scripts::LaunchScript(script, m_args, nullptr, false, nullptr);
    if (m_flagBits & kFlagDeactivateOnExpire)
        Virtual<bool>(this, kVtDeactivate, false, true);
}

// 0x00554F90
bool CObjective::RegisterGapHit(uint32_t gap)
{
    Scripts::CArray* gaps = nullptr;
    if (!m_args->FetchList(kNeededGapList, &gaps, 0))
        return false;
    const char* trick = kEmptyString;
    m_args->FetchText(kDemandedTrick, &trick, 0);
    if (*trick) {
        GameObjects::CGameEntity* skater = LocalSkater();
        if (!skater)
            return false;
        void* score = static_cast<GameObjects::CRider*>(skater)->GetPointsTotal();
        uint32_t name = Checksums::HashFromText(trick);
        uint32_t backside = Checksums::FoldTextIntoChecksum(kBsPrefix, trick);
        uint32_t frontside = Checksums::FoldTextIntoChecksum(kFsPrefix, trick);
        if (!TrickCount(score, name) && !TrickCount(score, backside) && !TrickCount(score, frontside))
            return false;
    }
    for (uint32_t i = 0; i < gaps->m_extent; ++i) {
        if (gaps->FetchPart(i) == gap) {
            char flag[32];
            std::snprintf(flag, sizeof(flag), kGotFormat, static_cast<int>(i + 1));
            Virtual<bool>(this, kVtSetFlag, Checksums::HashFromText(flag), 1, 0u);
            return true;
        }
    }
    return false;
}

// 0x00555100
bool CObjective::StepToNextWaypoint()
{
    Scripts::CArray* waypoints;
    if (!m_args->FetchList(kWaypoints, &waypoints, 0))
        m_args->FetchList(kWaypoints2, &waypoints, 1);
    for (uint32_t i = 0; i < waypoints->m_extent; ++i) {
        auto* waypoint = reinterpret_cast<Scripts::CStruct*>(waypoints->FetchPart(i));
        uint32_t flag;
        waypoint->ReadHashValue(kFlag, &flag, 1);
        if (m_goalFlags->HasEntryNamed(flag)) {
            int set;
            m_goalFlags->FetchIntValue(flag, &set, 1);
            if (set == 1)
                continue;
        }
        m_curPhase |= kStatePaused;
        int time;
        waypoint->FetchIntValue(kTime, &time, 1);
        if (m_args->HasBareFlag(kAddTime)) {
            m_secondsRemaining += time * 1000 + 20;
        } else {
            m_args->PutInt(kTime, time);
            ResetTimer();
        }
        uint32_t id = 0;
        m_args->ReadHashValue(kGoalIdName, &id, 1);
        waypoint->PutNameKey(kGoalIdChecksum, id);
        if (!m_numFlagsSet)
            waypoint->PutNameKey(kNoName, kFirstSpot);
        if (m_args->HasBareFlag(kQuickStart))
            waypoint->PutNameKey(kNoName, kQuickStart);
        else
            waypoint->RemoveFlag(kQuickStart);
        Scripts::SpawnScript(m_args->HasBareFlag(kHorse) ? kHorseSpotScript : kRaceSpotScript, waypoint, 0, nullptr,
                            0, 0, false, false, false, false);
        waypoint->RemoveFlag(kQuickStart);
        return true;
    }
    return true;
}

// 0x005552D0
void CObjective::KillCluster(uint32_t cluster, int score)
{
    Scripts::CArray* clusters;
    m_args->FetchList(kKillClusters, &clusters, 1);
    for (uint32_t i = 0; i < clusters->m_extent; ++i) {
        auto* entry = reinterpret_cast<Scripts::CStruct*>(clusters->FetchPart(i));
        uint32_t id;
        entry->ReadHashValue(kIdName, &id, 1);
        int needed;
        if (!entry->FetchIntValue(kScore, &needed, 0))
            m_args->FetchIntValue(kScore, &needed, 1);
        if (id == cluster && score > needed) {
            if (!GameObjects::g_trickSpotHub->SetObjectGraffitiColor(cluster, 0))
                DebugOut::Printf("ModulateTrickObjectColor didn't work\n"); // "ModulateTrickObjectColor didn't work\n"
            uint32_t flag;
            entry->ReadHashValue(kFlag, &flag, 1);
            Virtual<bool>(this, kVtSetFlag, flag, 1, 0u);
            return;
        }
    }
}

// 0x005553C0
bool CObjective::DidTrickString()
{
    void* score = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(LocalSkater()->FetchModule(kSkaterScore)) +
                                            0x18);
    const char* trick;
    m_args->FetchText(kTrickString, &trick, 1);
    uint32_t type;
    m_args->ReadHashValue(kTrickType, &type, 1);
    if (type == kGrind) {
        // (the original copies into its 128 bytes unbounded)
        char name[128];
        std::snprintf(name, sizeof(name), "FS %s", trick);
        if (TrickCount(score, Checksums::HashFromText(name)))
            return true;
        std::snprintf(name, sizeof(name), "BS %s", trick);
        if (TrickCount(score, Checksums::HashFromText(name)))
            return true;
    }
    // (create_a_trick is looked at, but either way the trick string itself is counted)
    m_args->HasBareFlag(kCreateATrick);
    return TrickCount(score, Checksums::HashFromText(trick)) > 0;
}

// 0x00555540
uint32_t CObjective::GetTopObjectiveKey()
{
    void* manager = ObjectiveBoard();
    CObjective* goal = this;
    while (goal->m_parents && goal->m_parents->m_objectiveKey)
        goal = GetGoal(manager, goal->m_parents->m_objectiveKey, 1);
    uint32_t id = 0;
    goal->m_args->ReadHashValue(kGoalIdName, &id, 1);
    return id;
}

// 0x00555590
bool CObjective::HasWon()
{
    // (NetLobby counts whether or not a network game is on)
    if ((NetSessionSlot()->InNetGame() && ActiveRulesetLabel() == kNetGoalAttack) || ActiveRulesetLabel() == kNetLobby)
        return (m_flagBits & kFlagWonInNetGame) != 0;
    return (m_flagBits & kFlagWon) != 0;
}

// 0x005555E0
void CObjective::MarkWon()
{
    if (NetSessionSlot()->InNetGame() && ActiveRulesetLabel() == kNetGoalAttack)
        m_flagBits |= kFlagWonInNetGame;
    else
        m_flagBits |= kFlagWon;
    void* manager = ObjectiveBoard();
    for (GoalEdge* link = m_parents; link && link->m_objectiveKey; link = link->m_succ)
        Virtual<void>(GetGoal(manager, link->m_objectiveKey, 1), kVtMarkWon);
}

// 0x00555650
void CObjective::SetStageFlag(int bit) { m_stageFlags |= 1u << (bit & 31); }

// 0x00555670
void CObjective::SetNetStageFlag(int bit) { m_netStageFlags |= 1u << (bit & 31); }

// 0x00555690
int CObjective::StageFlagSet(int bit) { return (m_stageFlags & (1u << (bit & 31))) ? 1 : 0; }

// 0x005556B0
int CObjective::NetStageFlagSet(int bit) { return (m_netStageFlags & (1u << (bit & 31))) ? 1 : 0; }

// 0x005556D0
void CObjective::SetLaunchAltitude()
{
    void* state = LocalSkater()->FetchModule(kSkaterState);
    m_args->PutInt(kLaunchAltitude, Truncate(FloatAt(state, 0x11C)));
}

// 0x00555710
bool CObjective::TestHighestAir()
{
    if (Virtual<bool>(this, kVtIsPaused))
        return false;
    int record = 0;
    m_args->FetchIntValue(kRecord, &record, 0);
    GameObjects::CGameEntity* skater = LocalSkater();
    int start = 10000;
    m_args->FetchIntValue(kLaunchAltitude, &start, 0);
    void* state = skater->FetchModule(kSkaterState);
    int height = Truncate(FloatAt(state, 0x11C)) - start;
    if (height < 0)
        height = 0;
    int feet = height / 12;
    int inches = height % 12;
    m_args->PutInt(kCurrentHeightFeet, feet);
    m_args->PutInt(kCurrentHeightInches, inches);
    if (height <= record)
        return false;
    m_args->PutInt(kRecord, height);
    m_args->PutInt(kRecordFeet, feet);
    m_args->PutInt(kRecordInches, inches);
    return true;
}

// 0x00555800
bool CObjective::TestLongestJump()
{
    uint32_t id;
    m_args->ReadHashValue(kDistanceObject, &id, 1);
    GameObjects::CEntity* object = reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::g_compositeRegistry)->LookUpEntity(id);
    GameObjects::CGameEntity* skater = LocalSkater();
    if (!skater || !object)
        return false;
    // The distance across the ground: the height's square added and taken away again, as the original.
    const auto* from = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(object) + 0x4C);
    const auto* to = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(skater) + 0x4C);
    double dx = static_cast<double>(from[0]) - to[0];
    double dy = static_cast<double>(from[1]) - to[1];
    double dz = static_cast<double>(from[2]) - to[2];
    double down = static_cast<double>(to[1]) - from[1];
    double distance = std::sqrt(dz * dz + dx * dx + dy * dy - down * down);
    float rounded = static_cast<float>(distance);
    int feet = Truncate(distance * g_goalRunInchesToFeet); // (1/12)
    int whole = Truncate(rounded);
    int inches = whole % 12;
    m_args->PutInt(kCurrentDistanceFeet, feet);
    m_args->PutInt(kCurrentDistanceInches, inches);
    int record = 0;
    m_args->FetchIntValue(kRecord, &record, 0);
    if (!(static_cast<double>(record) < rounded))
        return false;
    m_args->PutInt(kRecord, whole);
    m_args->PutInt(kRecordFeet, feet);
    m_args->PutInt(kRecordInches, inches);
    return true;
}

// 0x00555930
bool CObjective::UnBeat()
{
    if (NetSessionSlot()->InNetGame()) {
        m_stageFlags = 0;
        m_netStageFlags = 0;
        if (m_flagBits & kFlagWonInNetGame) {
            m_flagBits &= ~kFlagWonInNetGame;
            return true;
        }
    } else if (m_flagBits & kFlagWon) {
        m_flagBits &= ~kFlagWon;
        return true;
    }
    return false;
}

// 0x00555970
void CObjective::EndOfRunDone(Scripts::CScript* script)
{
    m_flagBits &= ~kFlagEndOfRunStarted;
    GameObjects::CGameEntity* skater = LocalSkater();
    GameObjects::CRiderSessionEnd* endRun = EndRunOf(skater);
    if (m_runEnding == 0) {
        CallObjectMember(skater, kGoalEndOfRunDone, nullptr, script);
        endRun->ClearGoalEndOfRunStarted();
    } else if (m_runEnding == 1) {
        CallObjectMember(skater, kEndofRunDone, nullptr, script);
        endRun->ClearEndOfRunStarted();
    }
}

// 0x005559E0
bool CObjective::RunDone()
{
    GameObjects::CRiderSessionEnd* endRun = EndRunOf(LocalSkater());
    if (m_runEnding == 0)
        return endRun->GoalRunDone();
    if (m_runEnding == 1)
        return endRun->RunDone();
    return false;
}

// 0x00555A20
bool CObjective::RunStarted()
{
    GameObjects::CRiderSessionEnd* endRun = EndRunOf(LocalSkater());
    if (m_runEnding == 0)
        return (m_flagBits & kFlagEndOfRunStarted) != 0;
    if (m_runEnding == 1)
        return endRun->EndOfRunStarted();
    return false;
}

} // namespace Game
