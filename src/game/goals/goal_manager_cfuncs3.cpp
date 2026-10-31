// The goal manager's script commands, 0x0055A700-0x0055B98F (see goal_manager_cfuncs.h): trick lookups, starting,
// expiring and timing goals, their counters and flags, the end of run types, network announcements, the career
// reset, the difficulty level, the stubbed stage commands, filming, the sponsor and the team, the trick beat goals,
// the reward points, the collect goals.

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "game/goals/goal_types.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/trick_goals.h"

namespace GoalSystem {

namespace {

using namespace GoalNames;

constexpr uint32_t kName = 0xA1DC81F9;              // Name
constexpr uint32_t kKeyCombo = 0xACFDB27A;          // key_combo
constexpr uint32_t kProfile = 0x7EA855F0;           // profile
constexpr uint32_t kTrickMapping = 0xD544AA2D;      // trick_mapping
constexpr uint32_t kSpecialFlag = 0xB394C01C;       // special
constexpr uint32_t kCatNum = 0xA75B8581;            // cat_num
constexpr uint32_t kUnassigned = 0xF60C9090;        // Unassigned
constexpr uint32_t kParams = 0x7031F10C;            // params
constexpr uint32_t kTrickChecksum = 0x4443A4DA;     // trick_checksum
constexpr uint32_t kExtraTricks = 0x6E855102;       // ExtraTricks
constexpr uint32_t kExtraTrickString = 0x3DAAF58B;  // extra_trick_string
constexpr uint32_t kExtraTrickChecksum = 0xD0BCFC62; // (name unknown: the extra trick's checksum)
constexpr uint32_t kCurrentIndex = 0xDB92EFFA;      // current_index
constexpr uint32_t kDontAssert = 0x9AC8D54F;        // DontAssert
constexpr uint32_t kNumberOfFlags = 0xCB9D1224;     // number_of_flags
constexpr uint32_t kClear = 0x1A4E0EF9;             // clear
constexpr uint32_t kSkater = 0x5B8AB877;            // skater
constexpr uint32_t kTimesStarted = 0xFF61BA23;      // times_started
constexpr uint32_t kGoalEndOfRun = 0x54585419;      // Goal_EndOfRun
constexpr uint32_t kEndOfRun = 0xE022C12C;          // EndOfRun
constexpr uint32_t kNone = 0x806FFF30;              // none
constexpr uint32_t kSuspend = 0xCE0CA665;           // suspend
constexpr uint32_t kDifficultyLevel = 0x3671F748;   // difficulty_level
constexpr uint32_t kStage = 0x5D82C486;             // (name unknown: AdvanceStage's stage)
constexpr uint32_t kForce = 0x68977CD7;             // force
constexpr uint32_t kSponsor = 0x7E73362B;           // sponsor
constexpr uint32_t kTeam = 0x3B1F59E0;              // Team
constexpr uint32_t kNumTeamMembers = 0xEB8DD330;    // num_team_members
constexpr uint32_t kPro = 0x944B2900;               // pro
constexpr uint32_t kRemove = 0x977FE2CF;            // remove
constexpr uint32_t kMarkUsed = 0x531FF702;          // mark_used
constexpr uint32_t kTeamName = 0x703D7582;          // team_name
constexpr uint32_t kTempo = 0x8B4A1490;             // tempo
constexpr uint32_t kBeatPattern = 0x407CCE70;       // beat_pattern
constexpr uint32_t kGoalManagerParams = 0xAC7C2B62; // GoalManagerParams
constexpr uint32_t kTotalCheatPoints = 0x35F1301A;  // total_cheat_points
constexpr uint32_t kActivePoints = 0x477CA8C1;      // active_points
constexpr uint32_t kTotalPoints = 0x105152C0;       // total_points
constexpr uint32_t kRewardPoints = 0xBAFFB052;      // reward_points

constexpr uint8_t kMsgGoalStarted = 0x6F; // to the server: a goal (the first of its chain) started

CMatchRules* GameMode() { return g_gameMode; }
OnlinePlay::Hub* NetManager() { return OnlinePlay::g_gameNetManager; }
GameObjects::CProfileRoster* ProfileManager() { return GameObjects::g_profileManager; }

uint32_t GoalName(Scripts::CStruct* params)
{
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    return name;
}
CObjective* NameThenGoal(Scripts::CStruct* params)
{
    uint32_t name = GoalName(params);
    return GetObjectiveBoard()->m_goals.Pull(name);
}
// The goal Name names - none without Name (0x00476EC0, the assert passed as a bool).
CObjective* OptionalGoal(CGoalManager* manager, Scripts::CStruct* params)
{
    uint32_t name = 0;
    params->ReadHashValue(kName, &name, true);
    return name ? manager->m_goals.Pull(name) : nullptr;
}

} // namespace

// 0x0055A700: the trick on `key_combo` in the skater's trick mapping (`profile`'s template, else the current
// profile): a create-a-trick's number (cat_num), else the trick's name and checksum (trick_string, trick_checksum)
// and its first ExtraTricks entry's (extra_trick_string, 0xD0BCFC62). With `special`, the special slot with those
// keys: its trick's name and checksum (an integer for a created trick) and its index. False if there's none.
bool __cdecl CFunc_GoalManager_GetTrickFromKeyCombo(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t keyCombo;
    params->ReadHashValue(kKeyCombo, &keyCombo, 1);
    GameObjects::CRiderIdentity* profile;
    if (params->HasEntryNamed(kProfile)) {
        uint32_t name;
        params->ReadHashValue(kProfile, &name, 1);
        profile = ProfileManager()->GetTemplate(name);
    } else {
        profile = SkateCalls::GetProfileFromParams(nullptr);
    }
    Scripts::CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
    if (!params->HasBareFlag(kSpecialFlag)) {
        int created;
        if (mapping->FetchIntValue(keyCombo, &created, 0)) {
            script->m_args->PutInt(kCatNum, created);
            return true;
        }
        uint32_t trick;
        if (!mapping->ReadHashValue(keyCombo, &trick, 0) || trick == kUnassigned)
            return false;
        Scripts::CStruct* trickParams;
        Scripts::ReadSubStruct(trick)->ReadSubStruct(kParams, &trickParams, 1);
        const char* name;
        trickParams->FetchText(kName, &name, 1);
        script->m_args->AppendWord(kTrickString, name);
        script->m_args->PutNameKey(kTrickChecksum, trick);
        uint32_t extra = 0;
        if (!trickParams->ReadHashValue(kExtraTricks, &extra, 0)) {
            Scripts::CArray* extras;
            if (trickParams->FetchList(kExtraTricks, &extras, 0))
                extra = extras->FetchPart(0);
        }
        if (extra) {
            auto* entry = reinterpret_cast<Scripts::CStruct*>(Scripts::FetchList(extra)->FetchPart(0));
            Scripts::CStruct* extraParams;
            entry->ReadSubStruct(kParams, &extraParams, 1);
            const char* extraName;
            extraParams->FetchText(kName, &extraName, 1);
            script->m_args->AppendWord(kExtraTrickString, extraName);
            script->m_args->PutNameKey(kExtraTrickChecksum, extra);
        }
        return true;
    }
    if (!params->HasBareFlag(kSpecialFlag))
        return false;
    int slots = profile->GetMaxSpecials();
    for (int i = 0; i < slots; ++i) {
        GameObjects::CRiderIdentity::SpecialTrick special;
        profile->GetSpecialTrick(&special, i);
        if (special.keys != keyCombo)
            continue;
        if (special.trick == kUnassigned)
            return false;
        if (Scripts::CStruct* structure = Scripts::ReadSubStruct(special.trick)) {
            Scripts::CStruct* trickParams;
            structure->ReadSubStruct(kParams, &trickParams, 1);
            const char* name;
            trickParams->FetchText(kName, &name, 1);
            script->m_args->AppendWord(kTrickString, name);
        }
        if (special.flag)
            script->m_args->PutInt(kTrickChecksum, static_cast<int>(special.trick));
        else
            script->m_args->PutNameKey(kTrickChecksum, special.trick);
        script->m_args->PutInt(kCurrentIndex, i);
        return true;
    }
    return false;
}

// 0x0055A9F0
bool __cdecl CFunc_GoalManager_UnlockGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->Unlock();
    return true;
}

// 0x0055AA40
bool __cdecl CFunc_GoalManager_QuickStartGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = GoalName(params);
    bool dontAssert = params->HasBareFlag(kDontAssert);
    return GetObjectiveBoard()->BeginObjectiveAtOnce(name, dontAssert);
}

// 0x0055AAA0
bool __cdecl CFunc_GoalManager_InitializeGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->PrepareAll();
    return true;
}

// 0x0055AAF0
bool __cdecl CFunc_GoalManager_GoalIsExpired(Scripts::CStruct* params, Scripts::CScript*)
{
    return Virtual<bool>(NameThenGoal(params), CObjective::kVtShouldExpire);
}

// 0x0055AB40
bool __cdecl CFunc_GoalManager_ExpireGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    Virtual<void>(NameThenGoal(params), CObjective::kVtExpire);
    return true;
}

// 0x0055AB90: the unnamed integer's seconds added to the goal's time.
bool __cdecl CFunc_GoalManager_AddTime(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = GoalName(params);
    int seconds;
    params->FetchIntValue(0u, &seconds, 1);
    GetObjectiveBoard()->m_goals.Pull(name)->AdvanceClock(seconds);
    return true;
}

// 0x0055ABF0
bool __cdecl CFunc_GoalManager_GetNumberCollected(Scripts::CStruct* params, Scripts::CScript* script)
{
    int collected = NameThenGoal(params)->CountPickedUp();
    script->m_args->PutInt(kNumberCollected, collected);
    return true;
}

// 0x0055AC50: false if the goal has no flags to win (-1).
bool __cdecl CFunc_GoalManager_GetNumberOfFlags(Scripts::CStruct* params, Scripts::CScript* script)
{
    int flags = NameThenGoal(params)->GetNumFlagsToWin();
    if (flags == -1)
        return false;
    script->m_args->PutInt(kNumberOfFlags, flags);
    return true;
}

// 0x0055ACB0: the cheat flag (TurnPro / OpenUpEveryObjective).
bool __cdecl CFunc_GoalManager_IsPro(Scripts::CStruct*, Scripts::CScript*) { return GetObjectiveBoard()->m_allUnlocked; }

// 0x0055ACC0
bool __cdecl CFunc_GoalManager_ResetGoalFlags(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->ZeroObjectiveBits();
    return true;
}

// 0x0055AD10: the goal's trick objects in `skater`'s colour (its player number + 1) - or cleared.
bool __cdecl CFunc_GoalManager_ColorTrickObjects(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = GoalName(params);
    bool clear = params->HasBareFlag(kClear);
    int skater = 0;
    params->FetchIntValue(kSkater, &skater, 0);
    GetObjectiveBoard()->m_goals.Pull(name)->TintTaggedProps(skater + 1, clear);
    return true;
}

// 0x0055AD90
bool __cdecl CFunc_GoalManager_GetNumberOfTimesGoalStarted(Scripts::CStruct* params, Scripts::CScript* script)
{
    int started = NameThenGoal(params)->GetTimesStarted();
    script->m_args->PutInt(kTimesStarted, started);
    return true;
}

// 0x0055ADF0
bool __cdecl CFunc_GoalManager_GoalExists(Scripts::CStruct* params, Scripts::CScript*)
{
    return NameThenGoal(params) != nullptr;
}

// 0x0055AE30: the unnamed integer (1 without one).
bool __cdecl CFunc_GoalManager_SetCanStartGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    int can = 1;
    params->FetchIntValue(0u, &can, 0);
    manager->m_startAllowed = can != 0;
    return true;
}

// 0x0055AE70: the last goal as the script's goal_id. False if there's none.
bool __cdecl CFunc_GoalManager_GetLastGoalId(Scripts::CStruct*, Scripts::CScript* script)
{
    uint32_t last = GetObjectiveBoard()->m_previousObjective;
    if (!last)
        return false;
    script->m_args->PutNameKey(kGoalIdChecksum, last);
    return true;
}

// 0x0055AEA0 (the goal taken to be a tetris goal)
bool __cdecl CFunc_GoalManager_ClearTetrisTricks(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    reinterpret_cast<CTetrisGoal*>(manager->m_goals.Pull(GoalName(params)))->ClearTricksAndMenu();
    return true;
}

// 0x0055AEF0: a goal (not a network goal; the first of its chain) started told to the server (message 0x6F: the
// game's id and the goal's). (The local player is looked up and not used; the id word's three padding bytes are the
// original's stack garbage.)
bool __cdecl CFunc_GoalManager_AnnounceGoalStarted(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* net = NetManager();
    CGoalManager* manager = GetObjectiveBoard();
    CObjective* goal = manager->m_goals.Pull(GoalName(params));
    if (goal->IsOnlineObjective() || goal->m_parents->m_objectiveKey)
        return true;
    NetLink::AppHost* client = net->FetchPatron(0);
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    net->GetOwnPlayer();
    struct {
        uint32_t gameId;
        uint32_t goal;
    } message = {};
    message.gameId = net->GetGameId();
    message.goal = goal->GetObjectiveKey();
    desc.m_content = &message;
    desc.m_byteCount = sizeof(message);
    desc.m_ident = kMsgGoalStarted;
    static_cast<NetLink::Guest*>(client)->SendToHost(&desc);
    return true;
}

// 0x0055AFB0: Goal_EndOfRun (the goal's own: 0), EndOfRun (1, also without any) or none (2).
bool __cdecl CFunc_GoalManager_SetEndRunType(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = GoalName(params);
    int type = 1;
    if (params->HasBareFlag(kGoalEndOfRun))
        type = 0;
    else if (params->HasBareFlag(kEndOfRun))
        type = 1;
    else if (params->HasBareFlag(kNone))
        type = 2;
    GetObjectiveBoard()->m_goals.Pull(name)->ChooseRunEnding(type);
    return true;
}

// 0x0055B040: the unnamed integer.
bool __cdecl CFunc_GoalManager_SetShouldDeactivateOnExpire(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = GoalName(params);
    int deactivate;
    params->FetchIntValue(0u, &deactivate, 1);
    CGoalManager* manager = GetObjectiveBoard();
    bool on = deactivate != 0;
    manager->m_goals.Pull(name)->SetDeactivateOnExpire(on);
    return true;
}

// 0x0055B0B0: the saved state reset (everything, or the goals with the unnamed checksum's flag).
bool __cdecl CFunc_GoalManager_ResetCareer(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t flag = 0;
    params->ReadHashValue(0u, &flag, 0);
    manager->ResetGoalData(flag);
    return true;
}

// 0x0055B0F0
bool __cdecl CFunc_GoalManager_UpdateFamilyTrees(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->BuildGoalTree();
    return true;
}

// 0x0055B110: the goal's pro's logic suspended (suspend, 1 without one) - or resumed. (Without Name the pro is read
// through null, as the original.)
bool __cdecl CFunc_GoalManager_SuspendGoalPedLogic(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    int suspend = 1;
    params->FetchIntValue(kSuspend, &suspend, 0);
    CObjective* goal = OptionalGoal(manager, params);
    goal->m_ped->HaltObjectiveUpdates(suspend != 0);
    return true;
}

// 0x0055B1A0: the game mode's difficulty level set to the unnamed integer. Whether it isn't 0.
bool __cdecl CFunc_GoalManager_SetDifficultyLevel(Scripts::CStruct* params, Scripts::CScript*)
{
    int level;
    params->FetchIntValue(0u, &level, 1);
    CGoalManager* manager = GetObjectiveBoard();
    manager->m_args->PutInt(GameMode()->GetNameKey(), level);
    return level != 0;
}

// 0x0055B1F0: the game mode's difficulty level (0, 1 or 2; 1 if none, or another value) as the script's
// difficulty_level.
bool __cdecl CFunc_GoalManager_GetDifficultyLevel(Scripts::CStruct*, Scripts::CScript* script)
{
    CGoalManager* manager = GetObjectiveBoard();
    int level;
    if (!manager->m_args->FetchIntValue(GameMode()->GetNameKey(), &level, 0))
        level = 1;
    int result;
    switch (level) {
    case 0: result = 0; break;
    case 2: result = 2; break;
    default: result = 1; break;
    }
    script->m_args->PutInt(kDifficultyLevel, result);
    return true;
}

// 0x0055B260 / 0x0055B280 / 0x0055B2A0: stubbed in the game (a message only).
bool __cdecl CFunc_GoalManager_RestartStage(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard();
    DebugOut::Printf("STUBBED\n"); // "STUBBED\n"
    return true;
}
bool __cdecl CFunc_GoalManager_CanRestartStage(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard();
    DebugOut::Printf("STUBBED\n"); // "STUBBED\n"
    return false;
}
bool __cdecl CFunc_GoalManager_SetGoalChaptersAndStages(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard();
    DebugOut::Printf("CGoalManager::SetGoalChaptersAndStages() Stubbed\n"); // "CGoalManager::SetGoalChaptersAndStages() Stubbed\n"
    return true;
}

// 0x0055B2C0: stubbed too (its parameters read, a message). False.
bool __cdecl CFunc_GoalManager_AdvanceStage(Scripts::CStruct* params, Scripts::CScript*)
{
    GetObjectiveBoard();
    uint32_t stage = 0;
    params->ReadHashValue(kStage, &stage, 0);
    params->HasBareFlag(kForce);
    DebugOut::Printf("STUBBED!\n"); // "STUBBED!\n"
    return false;
}

// 0x0055B310: the film goal's checkpoint passed (without Name, on null, as the original).
bool __cdecl CFunc_GoalManager_FilmGoalCheckpoint(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    reinterpret_cast<CShootVideoObjective*>(OptionalGoal(manager, params))->Checkpoint();
    return true;
}

// 0x0055B370: the film goal filming (+0x70 set: the code is the competition's FreezeContest, 0x00551880).
bool __cdecl CFunc_GoalManager_StartFilming(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    reinterpret_cast<CContestObjective*>(OptionalGoal(manager, params))->FreezeContest();
    return true;
}

// 0x0055B3D0: the unnamed checksum.
bool __cdecl CFunc_GoalManager_SetSponsor(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t sponsor;
    params->ReadHashValue(0u, &sponsor, 1);
    manager->m_backer = sponsor;
    return true;
}

// 0x0055B400
bool __cdecl CFunc_GoalManager_GetSponsor(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutNameKey(kSponsor, GetObjectiveBoard()->m_backer);
    return true;
}

// 0x0055B430: the team (a structure: each member a component) and its size.
bool __cdecl CFunc_GoalManager_GetTeam(Scripts::CStruct*, Scripts::CScript* script)
{
    Scripts::CStruct* team = GetObjectiveBoard()->m_team;
    script->m_args->PutSubStruct(kTeam, team);
    int members = 0;
    for (Scripts::CComponent* member = team->StepToEntry(nullptr); member; member = team->StepToEntry(member))
        ++members;
    script->m_args->PutInt(kNumTeamMembers, members);
    return true;
}

// 0x0055B490: `pro` added to the team (1 with mark_used, else 0) - or removed.
bool __cdecl CFunc_GoalManager_SetTeamMember(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t pro;
    params->ReadHashValue(kPro, &pro, 1);
    bool remove = params->HasBareFlag(kRemove);
    bool markUsed = params->HasBareFlag(kMarkUsed);
    if (remove)
        manager->m_team->EraseModule(pro);
    else
        manager->m_team->PutInt(pro, markUsed ? 1 : 0);
    return true;
}

// 0x0055B520
bool __cdecl CFunc_GoalManager_KillTeamMembers(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->m_team->Clear();
    return true;
}

// 0x0055B540: the unnamed string as the team's team_name.
bool __cdecl CFunc_GoalManager_SetTeamName(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    const char* name;
    params->FetchText(0u, &name, 1);
    manager->m_team->AppendWord(kTeamName, name);
    return true;
}

// 0x0055B580: nothing left in it.
bool __cdecl CFunc_GoalManager_RunLastStageScript(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard();
    return true;
}

// 0x0055B590
bool __cdecl CFunc_GoalManager_GoalInitialized(Scripts::CStruct* params, Scripts::CScript*)
{
    return Virtual<bool>(NameThenGoal(params), CObjective::kVtIsInitialized);
}

// 0x0055B5E0 / 0x0055B630: the goal's pro's last animations unloaded / last stream stopped.
bool __cdecl CFunc_GoalManager_UnloadLastFam(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->m_ped->UnloadAnimBank();
    return true;
}
bool __cdecl CFunc_GoalManager_StopLastStream(Scripts::CStruct* params, Scripts::CScript*)
{
    NameThenGoal(params)->m_ped->CutPreviousVoiceLine();
    return true;
}

// 0x0055B680: the unnamed integer.
bool __cdecl CFunc_GoalManager_HideAllGoalPeds(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    int hide;
    params->FetchIntValue(0u, &hide, 1);
    manager->ConcealEveryObjectiveNpc(hide != 0);
    return true;
}

// 0x0055B6C0 (the goal taken to be a trick beat goal)
bool __cdecl CFunc_GoalManager_SetTrickBeatPattern(Scripts::CStruct* params, Scripts::CScript*)
{
    auto* goal = reinterpret_cast<CTrickBeatGoal*>(NameThenGoal(params));
    int tempo;
    params->FetchIntValue(kTempo, &tempo, 1);
    Scripts::CArray* pattern;
    params->FetchList(kBeatPattern, &pattern, 1);
    goal->SetBeatPattern(pattern, tempo);
    return true;
}

// 0x0055B740
bool __cdecl CFunc_GoalManager_ResetTrickBeatTimer(Scripts::CStruct* params, Scripts::CScript*)
{
    reinterpret_cast<CTrickBeatGoal*>(NameThenGoal(params))->ResetClock();
    return true;
}

// 0x0055B790: the saved goals' structure as the script's GoalManagerParams.
bool __cdecl CFunc_GoalManager_GetGoalManagerParams(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutSubStruct(kGoalManagerParams, GetObjectiveBoard()->m_goalData);
    return true;
}

// 0x0055B7C0: the reward points of every goal: of those beaten (plus total_cheat_points) as active_points, of all as
// total_points.
bool __cdecl CFunc_GoalManager_GetActiveRewardPoints(Scripts::CStruct*, Scripts::CScript* script)
{
    CGoalManager* manager = GetObjectiveBoard();
    int won = 0;
    int total = 0;
    for (int i = 0; i < manager->m_goals.m_num; ++i) {
        CObjective* goal = manager->m_goals.GetByIndex(i);
        int points = goal->GetRewardPoints();
        if (Virtual<bool>(goal, CObjective::kVtHasWon))
            won += points;
        total += points;
    }
    script->m_args->PutInt(kActivePoints, Scripts::FetchIntValue(kTotalCheatPoints) + won);
    script->m_args->PutInt(kTotalPoints, total);
    return true;
}

// 0x0055B8B0: the goal's reward points as the script's reward_points. False if it has none.
bool __cdecl CFunc_GoalManager_GetGoalRewardPoints(Scripts::CStruct* params, Scripts::CScript* script)
{
    CGoalManager* manager = GetObjectiveBoard();
    CObjective* goal = manager->m_goals.Pull(GoalName(params));
    if (!goal->GetRewardPoints())
        return false;
    script->m_args->PutInt(kRewardPoints, goal->GetRewardPoints());
    return true;
}

// 0x0055B930 (Goal_Collect_Active, Goal_Gaps_Active, SkateLetters_Active): the goal goal_id won once all its flags
// are set.
bool __cdecl CFunc_Goal_CollectActive(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t id;
    params->ReadHashValue(kGoalIdChecksum, &id, 1);
    CGoalManager* manager = GetObjectiveBoard();
    if (manager->m_goals.Pull(id)->HasEveryBit())
        manager->CompleteObjective(id);
    return true;
}

} // namespace Game
