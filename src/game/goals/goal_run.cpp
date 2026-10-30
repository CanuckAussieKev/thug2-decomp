// Game::CGoal while it runs: its scripts, links, update, expiry, the skater's end of run, gaps, waypoints, clusters,
// tricks, records (0x00554A80-0x00555A5F, 0x00556230; see goal.h).

#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/goals/goal.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/end_run.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/obj/compositemgr.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/tod_manager.h"
#include "hook/game_code.h"

namespace Game {

GAME_GLOBAL(float, g_goalRunMinusMilliseconds, 0x0064604C, -1000.0f);
GAME_GLOBAL(float, g_goalRunInchesToFeet, 0x006461F4, 0.083333336f);

namespace {

using namespace GoalCalls;
using namespace GoalNames;

// The x87 code's float to int conversion (_ftol): truncation.
int Truncate(double value) { return static_cast<int>(value); }

// A new link (the manager's Allocate, as the original's inlined new).
GoalLink* NewLink()
{
    auto* link = static_cast<GoalLink*>(Mem::New(sizeof(GoalLink)));
    if (link) {
        link->m_goalId = 0;
        link->m_next = nullptr;
        link->m_won = false;
    }
    return link;
}

GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }
Obj::CCompositeObject* LocalSkater() { return SkateCalls::GetLocalSkater(); }
// The game mode's name (0x005532A0) and whether it beeps the timer (0x00553150).
uint32_t GameModeName() { return static_cast<Game::CGameMode*>(SkateCalls::GameMode())->GetNameChecksum(); }
bool GameModeTimerBeeps() { return static_cast<Game::CGameMode*>(SkateCalls::GameMode())->ShouldTimerBeep(); }
// A field at `offset`.
float FloatAt(const void* object, uint32_t offset)
{
    return *reinterpret_cast<const float*>(static_cast<const uint8_t*>(object) + offset);
}
// How often the score (Mdl::Score) has seen trick `trick` with no spin, not perfect, one tap (0x005624C0).
int TrickCount(void* score, uint32_t trick)
{
    return static_cast<Mdl::Score*>(score)->CountTrickAfterTaps(trick, 0, false, 1);
}
// An object's member function by name (vtable +0x0C).
bool CallObjectMember(Obj::CCompositeObject* object, uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    return reinterpret_cast<bool(__thiscall*)(void*, uint32_t, Script::CStruct*, Script::CScript*)>(
        (*reinterpret_cast<void***>(object))[3])(object, name, params, script);
}
Obj::CSkaterEndRunComponent* EndRunOf(Obj::CCompositeObject* skater)
{
    return static_cast<Obj::CSkaterEndRunComponent*>(skater->GetComponent(kSkaterEndRun));
}

} // namespace

// 0x00554A80
bool CGoal::RunCallbackScript(uint32_t name)
{
    uint32_t script = name;
    if (!m_params->GetChecksum(name, &script, 0))
        return false;
    Script::RunScript(script, m_params, nullptr, false, nullptr);
    return true;
}

// 0x00554AC0
void CGoal::AddParent(uint32_t id)
{
    if (!m_parents->m_goalId) {
        m_parents->m_goalId = id;
        return;
    }
    GoalLink* link = NewLink();
    link->m_goalId = id;
    link->m_next = m_parents;
    m_parents = link;
}

// 0x00554B40
void CGoal::AddChild(uint32_t id)
{
    if (!m_children->m_goalId) {
        m_children->m_goalId = id;
        return;
    }
    for (GoalLink* link = m_children; link && link->m_goalId; link = link->m_next) {
        if (link->m_goalId == id)
            return;
    }
    GoalLink* link = NewLink();
    link->m_goalId = id;
    link->m_next = m_children;
    m_children = link;
}

// 0x00554BD0
void CGoal::Uninitialize(bool children, int depth)
{
    if (Virtual<bool>(this, kVtIsActive))
        Virtual<bool>(this, kVtDeactivate, true, true);
    uint32_t script;
    if (m_params->GetChecksum(kUninit, &script, 0))
        Script::RunScript(script, m_params, nullptr, false, nullptr);
    m_flags &= ~kFlagInitialized;
    if (!children)
        return;
    void* manager = GoalManager();
    for (GoalLink* link = m_children; link && link->m_goalId; link = link->m_next)
        GetGoal(manager, link->m_goalId, 1)->Uninitialize(children, depth + 1);
}

// 0x00554C70
bool CGoal::Lose()
{
    if (!Virtual<bool>(this, kVtIsActive))
        return false;
    uint32_t script;
    if (m_params->GetChecksum(kFail, &script, 0))
        Script::RunScript(script, m_params, nullptr, false, nullptr);
    Virtual<bool>(this, kVtDeactivate, true, true);
    return true;
}

// 0x00554CD0
bool CGoal::IsExpired()
{
    GameNet::Manager* net = NetManager();
    if (!LocalSkater() || !net->CountPlayers())
        return true;
    if (m_state & kStateNoTimeLimit)
        return false;
    return m_timeLeft <= 0;
}

// 0x00554D10
bool CGoal::IsExpiredAtEndOfRun(bool force)
{
    GameNet::Manager* net = NetManager();
    Obj::CCompositeObject* skater = LocalSkater();
    if (!skater)
        return true;
    Obj::CSkaterEndRunComponent* endRun = EndRunOf(skater);
    if (!net->CountPlayers())
        return true;
    if (m_state & kStateNoTimeLimit)
        return false;
    if (m_endRunType == 0 || m_endRunType == 1) {
        bool done = m_endRunType == 1 ? endRun->RunDone() : endRun->GoalRunDone();
        if (done)
            return m_timeLeft <= 0;
    }
    if (m_timeLeft > 0)
        return false;
    // Out of time: the run is ended, the goal expires once it's done.
    if (m_endRunType == 0) {
        m_flags |= kFlagEndOfRunStarted;
        endRun->EndGoalRun(reinterpret_cast<void*>(static_cast<uintptr_t>(force)));
    } else if (m_endRunType == 1) {
        m_flags |= kFlagEndOfRunStarted;
        endRun->EndRun(force);
    }
    return false;
}

// 0x00556230
bool CGoal::ShouldExpire() { return IsExpiredAtEndOfRun(false); }

// 0x00554DD0
bool CGoal::Update()
{
    if (Virtual<bool>(this, kVtIsActive) && !Virtual<bool>(this, kVtIsPaused))
        RunCallbackScript(kActiveScript);
    if (!Virtual<bool>(this, kVtIsActive) || Virtual<bool>(this, kVtIsPaused))
        return true;
    if (Virtual<bool>(this, kVtHasTimeLimit) && m_timeLeft >= 1) {
        int before = m_timeLeft;
        m_timeLeft += Truncate(Tmr::GetSlomoFrameSeconds() * g_goalRunMinusMilliseconds); // (-1000)
        m_elapsed += Truncate(Tmr::GetSlomoFrameSeconds() * Gfx::g_thousand);  // (1000)
        // The last ten seconds beep each second.
        if (m_timeLeft < 10000 && before / 1000 > m_timeLeft / 1000 && Virtual<bool>(this, kVtTimerBeeps) &&
            GameModeTimerBeeps())
            Script::RunScript(kTimerRunoutBeep, nullptr, nullptr, false, nullptr);
    }
    if (m_params->ContainsFlag(kRequireCombo) && !m_params->ContainsFlag(kComboStarted)) {
        Obj::CCompositeObject* skater = LocalSkater();
        if (skater) {
            const auto* score = static_cast<const uint8_t*>(static_cast<Obj::CSkater*>(skater)->GetScore());
            // (the combo's points: its base score times its multiplier)
            if (score &&
                Truncate(*reinterpret_cast<const int*>(score + 0x14) * static_cast<double>(FloatAt(score, 0x60))) > 0)
                m_params->AddChecksum(kNoName, kComboStarted);
        }
    }
    if (Virtual<bool>(this, kVtHasTimeLimit) && Virtual<bool>(this, kVtShouldExpire))
        Virtual<void>(this, kVtExpire);
    return true;
}

// 0x00554F40
void CGoal::Expire()
{
    uint32_t script;
    if (m_params->GetChecksum(kExpireScript, &script, 0))
        Script::RunScript(script, m_params, nullptr, false, nullptr);
    if (m_flags & kFlagDeactivateOnExpire)
        Virtual<bool>(this, kVtDeactivate, false, true);
}

// 0x00554F90
bool CGoal::GotGap(uint32_t gap)
{
    Script::CArray* gaps = nullptr;
    if (!m_params->GetArray(kRequiredGaps, &gaps, 0))
        return false;
    const char* trick = kEmptyString;
    m_params->GetString(kRequiredTrickName, &trick, 0);
    if (*trick) {
        Obj::CCompositeObject* skater = LocalSkater();
        if (!skater)
            return false;
        void* score = static_cast<Obj::CSkater*>(skater)->GetScore();
        uint32_t name = Crc::GenerateCRCFromString(trick);
        uint32_t backside = Crc::ExtendCRCWithString(kBsPrefix, trick);
        uint32_t frontside = Crc::ExtendCRCWithString(kFsPrefix, trick);
        if (!TrickCount(score, name) && !TrickCount(score, backside) && !TrickCount(score, frontside))
            return false;
    }
    for (uint32_t i = 0; i < gaps->m_size; ++i) {
        if (gaps->GetElement(i) == gap) {
            char flag[32];
            std::snprintf(flag, sizeof(flag), kGotFormat, static_cast<int>(i + 1));
            Virtual<bool>(this, kVtSetFlag, Crc::GenerateCRCFromString(flag), 1, 0u);
            return true;
        }
    }
    return false;
}

// 0x00555100
bool CGoal::NextWaypoint()
{
    Script::CArray* waypoints;
    if (!m_params->GetArray(kWaypoints, &waypoints, 0))
        m_params->GetArray(kWaypoints2, &waypoints, 1);
    for (uint32_t i = 0; i < waypoints->m_size; ++i) {
        auto* waypoint = reinterpret_cast<Script::CStruct*>(waypoints->GetElement(i));
        uint32_t flag;
        waypoint->GetChecksum(kFlag, &flag, 1);
        if (m_goalFlags->ContainsComponentNamed(flag)) {
            int set;
            m_goalFlags->GetInteger(flag, &set, 1);
            if (set == 1)
                continue;
        }
        m_state |= kStatePaused;
        int time;
        waypoint->GetInteger(kTime, &time, 1);
        if (m_params->ContainsFlag(kAddTime)) {
            m_timeLeft += time * 1000 + 20;
        } else {
            m_params->AddInteger(kTime, time);
            ResetTimer();
        }
        uint32_t id = 0;
        m_params->GetChecksum(kGoalIdName, &id, 1);
        waypoint->AddChecksum(kGoalIdChecksum, id);
        if (!m_numFlagsSet)
            waypoint->AddChecksum(kNoName, kFirstSpot);
        if (m_params->ContainsFlag(kQuickStart))
            waypoint->AddChecksum(kNoName, kQuickStart);
        else
            waypoint->RemoveFlag(kQuickStart);
        Script::SpawnScript(m_params->ContainsFlag(kHorse) ? kHorseSpotScript : kRaceSpotScript, waypoint, 0, nullptr,
                            0, 0, false, false, false, false);
        waypoint->RemoveFlag(kQuickStart);
        return true;
    }
    return true;
}

// 0x005552D0
void CGoal::KillCluster(uint32_t cluster, int score)
{
    Script::CArray* clusters;
    m_params->GetArray(kKillClusters, &clusters, 1);
    for (uint32_t i = 0; i < clusters->m_size; ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(clusters->GetElement(i));
        uint32_t id;
        entry->GetChecksum(kIdName, &id, 1);
        int needed;
        if (!entry->GetInteger(kScore, &needed, 0))
            m_params->GetInteger(kScore, &needed, 1);
        if (id == cluster && score > needed) {
            if (!Obj::g_trickObjectManager->SetObjectGraffitiColor(cluster, 0))
                Dbg::Printf(GAME_TEXT(0x0064DDA4, "ModulateTrickObjectColor didn't work\n")); // "ModulateTrickObjectColor didn't work\n"
            uint32_t flag;
            entry->GetChecksum(kFlag, &flag, 1);
            Virtual<bool>(this, kVtSetFlag, flag, 1, 0u);
            return;
        }
    }
}

// 0x005553C0
bool CGoal::DidTrickString()
{
    void* score = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(LocalSkater()->GetComponent(kSkaterScore)) +
                                            0x18);
    const char* trick;
    m_params->GetString(kTrickString, &trick, 1);
    uint32_t type;
    m_params->GetChecksum(kTrickType, &type, 1);
    if (type == kGrind) {
        // (the original copies into its 128 bytes unbounded)
        char name[128];
        std::snprintf(name, sizeof(name), "FS %s", trick);
        if (TrickCount(score, Crc::GenerateCRCFromString(name)))
            return true;
        std::snprintf(name, sizeof(name), "BS %s", trick);
        if (TrickCount(score, Crc::GenerateCRCFromString(name)))
            return true;
    }
    // (create_a_trick is looked at, but either way the trick string itself is counted)
    m_params->ContainsFlag(kCreateATrick);
    return TrickCount(score, Crc::GenerateCRCFromString(trick)) > 0;
}

// 0x00555540
uint32_t CGoal::GetRootGoalId()
{
    void* manager = GoalManager();
    CGoal* goal = this;
    while (goal->m_parents && goal->m_parents->m_goalId)
        goal = GetGoal(manager, goal->m_parents->m_goalId, 1);
    uint32_t id = 0;
    goal->m_params->GetChecksum(kGoalIdName, &id, 1);
    return id;
}

// 0x00555590
bool CGoal::HasWon()
{
    // (NetLobby counts whether or not a network game is on)
    if ((NetManager()->InNetGame() && GameModeName() == kNetGoalAttack) || GameModeName() == kNetLobby)
        return (m_flags & kFlagWonInNetGame) != 0;
    return (m_flags & kFlagWon) != 0;
}

// 0x005555E0
void CGoal::MarkWon()
{
    if (NetManager()->InNetGame() && GameModeName() == kNetGoalAttack)
        m_flags |= kFlagWonInNetGame;
    else
        m_flags |= kFlagWon;
    void* manager = GoalManager();
    for (GoalLink* link = m_parents; link && link->m_goalId; link = link->m_next)
        Virtual<void>(GetGoal(manager, link->m_goalId, 1), kVtMarkWon);
}

// 0x00555650
void CGoal::SetStageFlag(int bit) { m_stageFlags |= 1u << (bit & 31); }

// 0x00555670
void CGoal::SetNetStageFlag(int bit) { m_netStageFlags |= 1u << (bit & 31); }

// 0x00555690
int CGoal::StageFlagSet(int bit) { return (m_stageFlags & (1u << (bit & 31))) ? 1 : 0; }

// 0x005556B0
int CGoal::NetStageFlagSet(int bit) { return (m_netStageFlags & (1u << (bit & 31))) ? 1 : 0; }

// 0x005556D0
void CGoal::SetStartHeight()
{
    void* state = LocalSkater()->GetComponent(kSkaterState);
    m_params->AddInteger(kStartHeight, Truncate(FloatAt(state, 0x11C)));
}

// 0x00555710
bool CGoal::CheckHeightRecord()
{
    if (Virtual<bool>(this, kVtIsPaused))
        return false;
    int record = 0;
    m_params->GetInteger(kRecord, &record, 0);
    Obj::CCompositeObject* skater = LocalSkater();
    int start = 10000;
    m_params->GetInteger(kStartHeight, &start, 0);
    void* state = skater->GetComponent(kSkaterState);
    int height = Truncate(FloatAt(state, 0x11C)) - start;
    if (height < 0)
        height = 0;
    int feet = height / 12;
    int inches = height % 12;
    m_params->AddInteger(kCurrentHeightFeet, feet);
    m_params->AddInteger(kCurrentHeightInches, inches);
    if (height <= record)
        return false;
    m_params->AddInteger(kRecord, height);
    m_params->AddInteger(kRecordFeet, feet);
    m_params->AddInteger(kRecordInches, inches);
    return true;
}

// 0x00555800
bool CGoal::CheckDistanceRecord()
{
    uint32_t id;
    m_params->GetChecksum(kDistanceObject, &id, 1);
    Obj::CObject* object = reinterpret_cast<Obj::CBaseManager*>(Obj::g_compositeObjectManager)->GetObjectByID(id);
    Obj::CCompositeObject* skater = LocalSkater();
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
    m_params->AddInteger(kCurrentDistanceFeet, feet);
    m_params->AddInteger(kCurrentDistanceInches, inches);
    int record = 0;
    m_params->GetInteger(kRecord, &record, 0);
    if (!(static_cast<double>(record) < rounded))
        return false;
    m_params->AddInteger(kRecord, whole);
    m_params->AddInteger(kRecordFeet, feet);
    m_params->AddInteger(kRecordInches, inches);
    return true;
}

// 0x00555930
bool CGoal::UnBeat()
{
    if (NetManager()->InNetGame()) {
        m_stageFlags = 0;
        m_netStageFlags = 0;
        if (m_flags & kFlagWonInNetGame) {
            m_flags &= ~kFlagWonInNetGame;
            return true;
        }
    } else if (m_flags & kFlagWon) {
        m_flags &= ~kFlagWon;
        return true;
    }
    return false;
}

// 0x00555970
void CGoal::EndOfRunDone(Script::CScript* script)
{
    m_flags &= ~kFlagEndOfRunStarted;
    Obj::CCompositeObject* skater = LocalSkater();
    Obj::CSkaterEndRunComponent* endRun = EndRunOf(skater);
    if (m_endRunType == 0) {
        CallObjectMember(skater, kGoalEndOfRunDone, nullptr, script);
        endRun->ClearGoalEndOfRunStarted();
    } else if (m_endRunType == 1) {
        CallObjectMember(skater, kEndofRunDone, nullptr, script);
        endRun->ClearEndOfRunStarted();
    }
}

// 0x005559E0
bool CGoal::RunDone()
{
    Obj::CSkaterEndRunComponent* endRun = EndRunOf(LocalSkater());
    if (m_endRunType == 0)
        return endRun->GoalRunDone();
    if (m_endRunType == 1)
        return endRun->RunDone();
    return false;
}

// 0x00555A20
bool CGoal::RunStarted()
{
    Obj::CSkaterEndRunComponent* endRun = EndRunOf(LocalSkater());
    if (m_endRunType == 0)
        return (m_flags & kFlagEndOfRunStarted) != 0;
    if (m_endRunType == 1)
        return endRun->EndOfRunStarted();
    return false;
}

} // namespace Game

REPLACE(0x00554A80, &Game::CGoal::RunCallbackScript);
REPLACE(0x00554AC0, &Game::CGoal::AddParent);
REPLACE(0x00554B40, &Game::CGoal::AddChild);
REPLACE(0x00554BD0, &Game::CGoal::Uninitialize);
REPLACE(0x00554C70, &Game::CGoal::Lose);
REPLACE(0x00554CD0, &Game::CGoal::IsExpired);
REPLACE(0x00554D10, &Game::CGoal::IsExpiredAtEndOfRun);
REPLACE(0x00554DD0, &Game::CGoal::Update);
REPLACE(0x00554F40, &Game::CGoal::Expire);
REPLACE(0x00554F90, &Game::CGoal::GotGap);
REPLACE_SOURCE_ONLY(0x00554FD3, &Game::CGoal::GotGap); // (not a function: the rest of 0x00554F90's body, Ghidra's split)
REPLACE(0x00555100, &Game::CGoal::NextWaypoint);
REPLACE(0x005552D0, &Game::CGoal::KillCluster);
REPLACE(0x005553C0, &Game::CGoal::DidTrickString);
REPLACE(0x00555540, &Game::CGoal::GetRootGoalId);
REPLACE(0x00555590, &Game::CGoal::HasWon);
REPLACE(0x005555E0, &Game::CGoal::MarkWon);
REPLACE(0x00555650, &Game::CGoal::SetStageFlag);
REPLACE(0x00555670, &Game::CGoal::SetNetStageFlag);
REPLACE(0x00555690, &Game::CGoal::StageFlagSet);
REPLACE(0x005556B0, &Game::CGoal::NetStageFlagSet);
REPLACE(0x005556D0, &Game::CGoal::SetStartHeight);
REPLACE(0x00555710, &Game::CGoal::CheckHeightRecord);
REPLACE(0x00555800, &Game::CGoal::CheckDistanceRecord);
REPLACE(0x00555930, &Game::CGoal::UnBeat);
REPLACE(0x00555970, &Game::CGoal::EndOfRunDone);
REPLACE(0x005559E0, &Game::CGoal::RunDone);
REPLACE(0x00555A20, &Game::CGoal::RunStarted);
REPLACE(0x00556230, &Game::CGoal::ShouldExpire);
