// The goal manager's script commands, 0x0055A700-0x0055B98F (see goal_manager_cfuncs.h): trick lookups, starting,
// expiring and timing goals, their counters and flags, the end of run types, network announcements, the career
// reset, the difficulty level, the stubbed stage commands, filming, the sponsor and the team, the trick beat goals,
// the reward points, the collect goals.

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
#include "hook/game.h"
#include "hook/hook.h"

namespace Game {

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

const char* Text(uint32_t address) { return reinterpret_cast<const char*>(address); }
CGameMode* GameMode() { return g_gameMode; }
GameNet::Manager* NetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
Obj::CPlayerProfileManager* ProfileManager() { return Obj::g_profileManager; }

uint32_t GoalName(Script::CStruct* params)
{
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    return name;
}
CGoal* NameThenGoal(Script::CStruct* params)
{
    uint32_t name = GoalName(params);
    return GetGoalManager()->m_goals.Get(name);
}
// The goal Name names - none without Name (0x00476EC0, the assert passed as a bool).
CGoal* OptionalGoal(CGoalManager* manager, Script::CStruct* params)
{
    uint32_t name = 0;
    params->GetChecksum(kName, &name, true);
    return name ? manager->m_goals.Get(name) : nullptr;
}


} // namespace

// 0x0055A700: the trick on `key_combo` in the skater's trick mapping (`profile`'s template, else the current
// profile): a create-a-trick's number (cat_num), else the trick's name and checksum (trick_string, trick_checksum)
// and its first ExtraTricks entry's (extra_trick_string, 0xD0BCFC62). With `special`, the special slot with those
// keys: its trick's name and checksum (an integer for a created trick) and its index. False if there's none.
bool __cdecl CFunc_GoalManager_GetTrickFromKeyCombo(Script::CStruct* params, Script::CScript* script)
{
    uint32_t keyCombo;
    params->GetChecksum(kKeyCombo, &keyCombo, 1);
    Obj::CSkaterProfile* profile;
    if (params->ContainsComponentNamed(kProfile)) {
        uint32_t name;
        params->GetChecksum(kProfile, &name, 1);
        profile = ProfileManager()->GetTemplate(name);
    } else {
        profile = SkateCalls::GetProfileFromParams(nullptr);
    }
    Script::CStruct* mapping = profile->GetInfoStructure(kTrickMapping);
    if (!params->ContainsFlag(kSpecialFlag)) {
        int created;
        if (mapping->GetInteger(keyCombo, &created, 0)) {
            script->m_params->AddInteger(kCatNum, created);
            return true;
        }
        uint32_t trick;
        if (!mapping->GetChecksum(keyCombo, &trick, 0) || trick == kUnassigned)
            return false;
        Script::CStruct* trickParams;
        Script::GetStructure(trick)->GetStructure(kParams, &trickParams, 1);
        const char* name;
        trickParams->GetString(kName, &name, 1);
        script->m_params->AddString(kTrickString, name);
        script->m_params->AddChecksum(kTrickChecksum, trick);
        uint32_t extra = 0;
        if (!trickParams->GetChecksum(kExtraTricks, &extra, 0)) {
            Script::CArray* extras;
            if (trickParams->GetArray(kExtraTricks, &extras, 0))
                extra = extras->GetElement(0);
        }
        if (extra) {
            auto* entry = reinterpret_cast<Script::CStruct*>(Script::GetArray(extra)->GetElement(0));
            Script::CStruct* extraParams;
            entry->GetStructure(kParams, &extraParams, 1);
            const char* extraName;
            extraParams->GetString(kName, &extraName, 1);
            script->m_params->AddString(kExtraTrickString, extraName);
            script->m_params->AddChecksum(kExtraTrickChecksum, extra);
        }
        return true;
    }
    if (!params->ContainsFlag(kSpecialFlag))
        return false;
    int slots = profile->GetMaxSpecials();
    for (int i = 0; i < slots; ++i) {
        Obj::CSkaterProfile::SpecialTrick special;
        profile->GetSpecialTrick(&special, i);
        if (special.keys != keyCombo)
            continue;
        if (special.trick == kUnassigned)
            return false;
        if (Script::CStruct* structure = Script::GetStructure(special.trick)) {
            Script::CStruct* trickParams;
            structure->GetStructure(kParams, &trickParams, 1);
            const char* name;
            trickParams->GetString(kName, &name, 1);
            script->m_params->AddString(kTrickString, name);
        }
        if (special.flag)
            script->m_params->AddInteger(kTrickChecksum, static_cast<int>(special.trick));
        else
            script->m_params->AddChecksum(kTrickChecksum, special.trick);
        script->m_params->AddInteger(kCurrentIndex, i);
        return true;
    }
    return false;
}

// 0x0055A9F0
bool __cdecl CFunc_GoalManager_UnlockGoal(Script::CStruct* params, Script::CScript*)
{
    NameThenGoal(params)->Unlock();
    return true;
}

// 0x0055AA40
bool __cdecl CFunc_GoalManager_QuickStartGoal(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = GoalName(params);
    bool dontAssert = params->ContainsFlag(kDontAssert);
    return GetGoalManager()->QuickStartGoal(name, dontAssert);
}

// 0x0055AAA0
bool __cdecl CFunc_GoalManager_InitializeGoal(Script::CStruct* params, Script::CScript*)
{
    NameThenGoal(params)->Init();
    return true;
}

// 0x0055AAF0
bool __cdecl CFunc_GoalManager_GoalIsExpired(Script::CStruct* params, Script::CScript*)
{
    return Virtual<bool>(NameThenGoal(params), CGoal::kVtShouldExpire);
}

// 0x0055AB40
bool __cdecl CFunc_GoalManager_ExpireGoal(Script::CStruct* params, Script::CScript*)
{
    Virtual<void>(NameThenGoal(params), CGoal::kVtExpire);
    return true;
}

// 0x0055AB90: the unnamed integer's seconds added to the goal's time.
bool __cdecl CFunc_GoalManager_AddTime(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = GoalName(params);
    int seconds;
    params->GetInteger(0u, &seconds, 1);
    GetGoalManager()->m_goals.Get(name)->AddTime(seconds);
    return true;
}

// 0x0055ABF0
bool __cdecl CFunc_GoalManager_GetNumberCollected(Script::CStruct* params, Script::CScript* script)
{
    int collected = NameThenGoal(params)->GetNumberCollected();
    script->m_params->AddInteger(kNumberCollected, collected);
    return true;
}

// 0x0055AC50: false if the goal has no flags to win (-1).
bool __cdecl CFunc_GoalManager_GetNumberOfFlags(Script::CStruct* params, Script::CScript* script)
{
    int flags = NameThenGoal(params)->GetNumFlagsToWin();
    if (flags == -1)
        return false;
    script->m_params->AddInteger(kNumberOfFlags, flags);
    return true;
}

// 0x0055ACB0: the cheat flag (TurnPro / UnlockAllGoals).
bool __cdecl CFunc_GoalManager_IsPro(Script::CStruct*, Script::CScript*) { return GetGoalManager()->m_allUnlocked; }

// 0x0055ACC0
bool __cdecl CFunc_GoalManager_ResetGoalFlags(Script::CStruct* params, Script::CScript*)
{
    NameThenGoal(params)->ResetGoalFlags();
    return true;
}

// 0x0055AD10: the goal's trick objects in `skater`'s colour (its player number + 1) - or cleared.
bool __cdecl CFunc_GoalManager_ColorTrickObjects(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = GoalName(params);
    bool clear = params->ContainsFlag(kClear);
    int skater = 0;
    params->GetInteger(kSkater, &skater, 0);
    GetGoalManager()->m_goals.Get(name)->ColorTrickObjects(skater + 1, clear);
    return true;
}

// 0x0055AD90
bool __cdecl CFunc_GoalManager_GetNumberOfTimesGoalStarted(Script::CStruct* params, Script::CScript* script)
{
    int started = NameThenGoal(params)->GetTimesStarted();
    script->m_params->AddInteger(kTimesStarted, started);
    return true;
}

// 0x0055ADF0
bool __cdecl CFunc_GoalManager_GoalExists(Script::CStruct* params, Script::CScript*)
{
    return NameThenGoal(params) != nullptr;
}

// 0x0055AE30: the unnamed integer (1 without one).
bool __cdecl CFunc_GoalManager_SetCanStartGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    int can = 1;
    params->GetInteger(0u, &can, 0);
    manager->m_canStartGoal = can != 0;
    return true;
}

// 0x0055AE70: the last goal as the script's goal_id. False if there's none.
bool __cdecl CFunc_GoalManager_GetLastGoalId(Script::CStruct*, Script::CScript* script)
{
    uint32_t last = GetGoalManager()->m_lastGoal;
    if (!last)
        return false;
    script->m_params->AddChecksum(kGoalIdChecksum, last);
    return true;
}

// 0x0055AEA0 (the goal taken to be a tetris goal)
bool __cdecl CFunc_GoalManager_ClearTetrisTricks(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    reinterpret_cast<CTetrisGoal*>(manager->m_goals.Get(GoalName(params)))->ClearTricksAndMenu();
    return true;
}

// 0x0055AEF0: a goal (not a network goal; the first of its chain) started told to the server (message 0x6F: the
// game's id and the goal's). (The local player is looked up and not used; the id word's three padding bytes are the
// original's stack garbage.)
bool __cdecl CFunc_GoalManager_AnnounceGoalStarted(Script::CStruct* params, Script::CScript*)
{
    GameNet::Manager* net = NetManager();
    CGoalManager* manager = GetGoalManager();
    CGoal* goal = manager->m_goals.Get(GoalName(params));
    if (goal->IsNetGoal() || goal->m_parents->m_goalId)
        return true;
    Net::App* client = net->GetClient(0);
    Net::MsgDesc desc;
    desc.Construct();
    net->GetLocalPlayer();
    struct {
        uint32_t gameId;
        uint32_t goal;
    } message = {};
    message.gameId = net->GetGameId();
    message.goal = goal->GetGoalId();
    desc.m_data = &message;
    desc.m_length = sizeof(message);
    desc.m_id = kMsgGoalStarted;
    static_cast<Net::Client*>(client)->EnqueueMessageToServer(&desc);
    return true;
}

// 0x0055AFB0: Goal_EndOfRun (the goal's own: 0), EndOfRun (1, also without any) or none (2).
bool __cdecl CFunc_GoalManager_SetEndRunType(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = GoalName(params);
    int type = 1;
    if (params->ContainsFlag(kGoalEndOfRun))
        type = 0;
    else if (params->ContainsFlag(kEndOfRun))
        type = 1;
    else if (params->ContainsFlag(kNone))
        type = 2;
    GetGoalManager()->m_goals.Get(name)->SetEndRunType(type);
    return true;
}

// 0x0055B040: the unnamed integer.
bool __cdecl CFunc_GoalManager_SetShouldDeactivateOnExpire(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = GoalName(params);
    int deactivate;
    params->GetInteger(0u, &deactivate, 1);
    CGoalManager* manager = GetGoalManager();
    bool on = deactivate != 0;
    manager->m_goals.Get(name)->SetDeactivateOnExpire(on);
    return true;
}

// 0x0055B0B0: the saved state reset (everything, or the goals with the unnamed checksum's flag).
bool __cdecl CFunc_GoalManager_ResetCareer(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t flag = 0;
    params->GetChecksum(0u, &flag, 0);
    manager->ResetGoalData(flag);
    return true;
}

// 0x0055B0F0
bool __cdecl CFunc_GoalManager_UpdateFamilyTrees(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->BuildGoalTree();
    return true;
}

// 0x0055B110: the goal's pro's logic suspended (suspend, 1 without one) - or resumed. (Without Name the pro is read
// through null, as the original.)
bool __cdecl CFunc_GoalManager_SuspendGoalPedLogic(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    int suspend = 1;
    params->GetInteger(kSuspend, &suspend, 0);
    CGoal* goal = OptionalGoal(manager, params);
    goal->m_ped->SuspendGoalLogic(suspend != 0);
    return true;
}

// 0x0055B1A0: the game mode's difficulty level set to the unnamed integer. Whether it isn't 0.
bool __cdecl CFunc_GoalManager_SetDifficultyLevel(Script::CStruct* params, Script::CScript*)
{
    int level;
    params->GetInteger(0u, &level, 1);
    CGoalManager* manager = GetGoalManager();
    manager->m_params->AddInteger(GameMode()->GetNameChecksum(), level);
    return level != 0;
}

// 0x0055B1F0: the game mode's difficulty level (0, 1 or 2; 1 if none, or another value) as the script's
// difficulty_level.
bool __cdecl CFunc_GoalManager_GetDifficultyLevel(Script::CStruct*, Script::CScript* script)
{
    CGoalManager* manager = GetGoalManager();
    int level;
    if (!manager->m_params->GetInteger(GameMode()->GetNameChecksum(), &level, 0))
        level = 1;
    int result;
    switch (level) {
    case 0: result = 0; break;
    case 2: result = 2; break;
    default: result = 1; break;
    }
    script->m_params->AddInteger(kDifficultyLevel, result);
    return true;
}

// 0x0055B260 / 0x0055B280 / 0x0055B2A0: stubbed in the game (a message only).
bool __cdecl CFunc_GoalManager_RestartStage(Script::CStruct*, Script::CScript*)
{
    GetGoalManager();
    Dbg::Printf(Text(0x0064DE78)); // "STUBBED\n"
    return true;
}
bool __cdecl CFunc_GoalManager_CanRestartStage(Script::CStruct*, Script::CScript*)
{
    GetGoalManager();
    Dbg::Printf(Text(0x0064DE78)); // "STUBBED\n"
    return false;
}
bool __cdecl CFunc_GoalManager_SetGoalChaptersAndStages(Script::CStruct*, Script::CScript*)
{
    GetGoalManager();
    Dbg::Printf(Text(0x0064DE84)); // "CGoalManager::SetGoalChaptersAndStages() Stubbed\n"
    return true;
}

// 0x0055B2C0: stubbed too (its parameters read, a message). False.
bool __cdecl CFunc_GoalManager_AdvanceStage(Script::CStruct* params, Script::CScript*)
{
    GetGoalManager();
    uint32_t stage = 0;
    params->GetChecksum(kStage, &stage, 0);
    params->ContainsFlag(kForce);
    Dbg::Printf(Text(0x0064DEC4)); // "STUBBED!\n"
    return false;
}

// 0x0055B310: the film goal's checkpoint passed (without Name, on null, as the original).
bool __cdecl CFunc_GoalManager_FilmGoalCheckpoint(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    reinterpret_cast<CFilmGoal*>(OptionalGoal(manager, params))->Checkpoint();
    return true;
}

// 0x0055B370: the film goal filming (+0x70 set: the code is the competition's PauseCompetition, 0x00551880).
bool __cdecl CFunc_GoalManager_StartFilming(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    reinterpret_cast<CCompetitionGoal*>(OptionalGoal(manager, params))->PauseCompetition();
    return true;
}

// 0x0055B3D0: the unnamed checksum.
bool __cdecl CFunc_GoalManager_SetSponsor(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t sponsor;
    params->GetChecksum(0u, &sponsor, 1);
    manager->m_sponsor = sponsor;
    return true;
}

// 0x0055B400
bool __cdecl CFunc_GoalManager_GetSponsor(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddChecksum(kSponsor, GetGoalManager()->m_sponsor);
    return true;
}

// 0x0055B430: the team (a structure: each member a component) and its size.
bool __cdecl CFunc_GoalManager_GetTeam(Script::CStruct*, Script::CScript* script)
{
    Script::CStruct* team = GetGoalManager()->m_team;
    script->m_params->AddStructure(kTeam, team);
    int members = 0;
    for (Script::CComponent* member = team->GetNextComponent(nullptr); member; member = team->GetNextComponent(member))
        ++members;
    script->m_params->AddInteger(kNumTeamMembers, members);
    return true;
}

// 0x0055B490: `pro` added to the team (1 with mark_used, else 0) - or removed.
bool __cdecl CFunc_GoalManager_SetTeamMember(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t pro;
    params->GetChecksum(kPro, &pro, 1);
    bool remove = params->ContainsFlag(kRemove);
    bool markUsed = params->ContainsFlag(kMarkUsed);
    if (remove)
        manager->m_team->RemoveComponent(pro);
    else
        manager->m_team->AddInteger(pro, markUsed ? 1 : 0);
    return true;
}

// 0x0055B520
bool __cdecl CFunc_GoalManager_KillTeamMembers(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->m_team->Clear();
    return true;
}

// 0x0055B540: the unnamed string as the team's team_name.
bool __cdecl CFunc_GoalManager_SetTeamName(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    const char* name;
    params->GetString(0u, &name, 1);
    manager->m_team->AddString(kTeamName, name);
    return true;
}

// 0x0055B580: nothing left in it.
bool __cdecl CFunc_GoalManager_RunLastStageScript(Script::CStruct*, Script::CScript*)
{
    GetGoalManager();
    return true;
}

// 0x0055B590
bool __cdecl CFunc_GoalManager_GoalInitialized(Script::CStruct* params, Script::CScript*)
{
    return Virtual<bool>(NameThenGoal(params), CGoal::kVtIsInitialized);
}

// 0x0055B5E0 / 0x0055B630: the goal's pro's last animations unloaded / last stream stopped.
bool __cdecl CFunc_GoalManager_UnloadLastFam(Script::CStruct* params, Script::CScript*)
{
    NameThenGoal(params)->m_ped->UnloadLastFam();
    return true;
}
bool __cdecl CFunc_GoalManager_StopLastStream(Script::CStruct* params, Script::CScript*)
{
    NameThenGoal(params)->m_ped->StopLastStream();
    return true;
}

// 0x0055B680: the unnamed integer.
bool __cdecl CFunc_GoalManager_HideAllGoalPeds(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    int hide;
    params->GetInteger(0u, &hide, 1);
    manager->HideAllGoalPeds(hide != 0);
    return true;
}

// 0x0055B6C0 (the goal taken to be a trick beat goal)
bool __cdecl CFunc_GoalManager_SetTrickBeatPattern(Script::CStruct* params, Script::CScript*)
{
    auto* goal = reinterpret_cast<CTrickBeatGoal*>(NameThenGoal(params));
    int tempo;
    params->GetInteger(kTempo, &tempo, 1);
    Script::CArray* pattern;
    params->GetArray(kBeatPattern, &pattern, 1);
    goal->SetBeatPattern(pattern, tempo);
    return true;
}

// 0x0055B740
bool __cdecl CFunc_GoalManager_ResetTrickBeatTimer(Script::CStruct* params, Script::CScript*)
{
    reinterpret_cast<CTrickBeatGoal*>(NameThenGoal(params))->ResetClock();
    return true;
}

// 0x0055B790: the saved goals' structure as the script's GoalManagerParams.
bool __cdecl CFunc_GoalManager_GetGoalManagerParams(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddStructure(kGoalManagerParams, GetGoalManager()->m_goalData);
    return true;
}

// 0x0055B7C0: the reward points of every goal: of those beaten (plus total_cheat_points) as active_points, of all as
// total_points.
bool __cdecl CFunc_GoalManager_GetActiveRewardPoints(Script::CStruct*, Script::CScript* script)
{
    CGoalManager* manager = GetGoalManager();
    int won = 0;
    int total = 0;
    for (int i = 0; i < manager->m_goals.m_count; ++i) {
        CGoal* goal = manager->m_goals.GetByIndex(i);
        int points = goal->GetRewardPoints();
        if (Virtual<bool>(goal, CGoal::kVtHasWon))
            won += points;
        total += points;
    }
    script->m_params->AddInteger(kActivePoints, Script::GetInteger(kTotalCheatPoints) + won);
    script->m_params->AddInteger(kTotalPoints, total);
    return true;
}

// 0x0055B8B0: the goal's reward points as the script's reward_points. False if it has none.
bool __cdecl CFunc_GoalManager_GetGoalRewardPoints(Script::CStruct* params, Script::CScript* script)
{
    CGoalManager* manager = GetGoalManager();
    CGoal* goal = manager->m_goals.Get(GoalName(params));
    if (!goal->GetRewardPoints())
        return false;
    script->m_params->AddInteger(kRewardPoints, goal->GetRewardPoints());
    return true;
}

// 0x0055B930 (Goal_Collect_Active, Goal_Gaps_Active, SkateLetters_Active): the goal goal_id won once all its flags
// are set.
bool __cdecl CFunc_Goal_CollectActive(Script::CStruct* params, Script::CScript*)
{
    uint32_t id;
    params->GetChecksum(kGoalIdChecksum, &id, 1);
    CGoalManager* manager = GetGoalManager();
    if (manager->m_goals.Get(id)->AllFlagsSet())
        manager->WinGoal(id);
    return true;
}

} // namespace Game

REPLACE(0x0055A700, Game::CFunc_GoalManager_GetTrickFromKeyCombo);
REPLACE(0x0055A9F0, Game::CFunc_GoalManager_UnlockGoal);
REPLACE(0x0055AA40, Game::CFunc_GoalManager_QuickStartGoal);
REPLACE(0x0055AAA0, Game::CFunc_GoalManager_InitializeGoal);
REPLACE(0x0055AAF0, Game::CFunc_GoalManager_GoalIsExpired);
REPLACE(0x0055AB40, Game::CFunc_GoalManager_ExpireGoal);
REPLACE(0x0055AB90, Game::CFunc_GoalManager_AddTime);
REPLACE(0x0055ABF0, Game::CFunc_GoalManager_GetNumberCollected);
REPLACE(0x0055AC50, Game::CFunc_GoalManager_GetNumberOfFlags);
REPLACE(0x0055ACB0, Game::CFunc_GoalManager_IsPro);
REPLACE(0x0055ACC0, Game::CFunc_GoalManager_ResetGoalFlags);
REPLACE(0x0055AD10, Game::CFunc_GoalManager_ColorTrickObjects);
REPLACE(0x0055AD90, Game::CFunc_GoalManager_GetNumberOfTimesGoalStarted);
REPLACE(0x0055ADF0, Game::CFunc_GoalManager_GoalExists);
REPLACE(0x0055AE30, Game::CFunc_GoalManager_SetCanStartGoal);
REPLACE(0x0055AE70, Game::CFunc_GoalManager_GetLastGoalId);
REPLACE(0x0055AEA0, Game::CFunc_GoalManager_ClearTetrisTricks);
REPLACE(0x0055AEF0, Game::CFunc_GoalManager_AnnounceGoalStarted);
REPLACE(0x0055AFB0, Game::CFunc_GoalManager_SetEndRunType);
REPLACE(0x0055B040, Game::CFunc_GoalManager_SetShouldDeactivateOnExpire);
REPLACE(0x0055B0B0, Game::CFunc_GoalManager_ResetCareer);
REPLACE(0x0055B0F0, Game::CFunc_GoalManager_UpdateFamilyTrees);
REPLACE(0x0055B110, Game::CFunc_GoalManager_SuspendGoalPedLogic);
REPLACE(0x0055B1A0, Game::CFunc_GoalManager_SetDifficultyLevel);
REPLACE(0x0055B1F0, Game::CFunc_GoalManager_GetDifficultyLevel);
REPLACE(0x0055B260, Game::CFunc_GoalManager_RestartStage);
REPLACE(0x0055B280, Game::CFunc_GoalManager_CanRestartStage);
REPLACE(0x0055B2A0, Game::CFunc_GoalManager_SetGoalChaptersAndStages);
REPLACE(0x0055B2C0, Game::CFunc_GoalManager_AdvanceStage);
REPLACE(0x0055B310, Game::CFunc_GoalManager_FilmGoalCheckpoint);
REPLACE(0x0055B370, Game::CFunc_GoalManager_StartFilming);
REPLACE(0x0055B3D0, Game::CFunc_GoalManager_SetSponsor);
REPLACE(0x0055B400, Game::CFunc_GoalManager_GetSponsor);
REPLACE(0x0055B430, Game::CFunc_GoalManager_GetTeam);
REPLACE(0x0055B490, Game::CFunc_GoalManager_SetTeamMember);
REPLACE(0x0055B520, Game::CFunc_GoalManager_KillTeamMembers);
REPLACE(0x0055B540, Game::CFunc_GoalManager_SetTeamName);
REPLACE(0x0055B580, Game::CFunc_GoalManager_RunLastStageScript);
REPLACE(0x0055B590, Game::CFunc_GoalManager_GoalInitialized);
REPLACE(0x0055B5E0, Game::CFunc_GoalManager_UnloadLastFam);
REPLACE(0x0055B630, Game::CFunc_GoalManager_StopLastStream);
REPLACE(0x0055B680, Game::CFunc_GoalManager_HideAllGoalPeds);
REPLACE(0x0055B6C0, Game::CFunc_GoalManager_SetTrickBeatPattern);
REPLACE(0x0055B740, Game::CFunc_GoalManager_ResetTrickBeatTimer);
REPLACE(0x0055B790, Game::CFunc_GoalManager_GetGoalManagerParams);
REPLACE(0x0055B7C0, Game::CFunc_GoalManager_GetActiveRewardPoints);
REPLACE(0x0055B8B0, Game::CFunc_GoalManager_GetGoalRewardPoints);
REPLACE(0x0055B930, Game::CFunc_Goal_CollectActive);
