// Game::CGoalManager: the skater's tricks landed and bails passed to the active goals, the view goals menu's info
// (0x0055C7E0-0x0055D3E2), and the goal manager's script commands 0x0055D3F0-0x0055D954 (see goal_manager.h,
// goal_manager_cfuncs.h).

#include <cstdint>

#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_skate_line.h"
#include "game/goals/goal_net_types.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/trick_goals.h"
#include "game/skater/skater.h"
#include "game/skate/score.h"

namespace Game {

namespace {

using namespace GoalNames;

constexpr uint32_t kHighCombo = 0xC63432A7;              // high_combo
constexpr uint32_t kLastScoreLanded = 0xB9072B65;       // last_score_landed
constexpr uint32_t kHighComboCheckScore = 0x6B755ED8;   // goal_highcombo_check_score
constexpr uint32_t kScoreGoal = 0x293B95A7;             // score_goal
constexpr uint32_t kWinningScore = 0x3DA891B6;          // winning_score
constexpr uint32_t kTrickObjects = 0xEFC35592;          // TrickObjects
constexpr uint32_t kTrickBeat = 0xD0DD047B;             // TrickBeat
constexpr uint32_t kSkateTheLine = 0xF268C278;          // SkateTheLine
constexpr uint32_t kArcadeCurrentScoreTotal = 0x655F4F3F; // arcade_current_score_total
constexpr uint32_t kGoalLoseNextFrame = 0x0FEFDA11;     // goal_lose_next_frame
constexpr uint32_t kTrickFlag = 0x60B827D5;             // trick_flag
constexpr uint32_t kViewGoalsText = 0x4BC5229D;         // view_goals_text
constexpr uint32_t kBeaten = 0x38318B67;                // beaten
constexpr uint32_t kLocked = 0x152D376E;                // locked
constexpr uint32_t kRewardPoints = 0xBAFFB052;          // reward_points
constexpr uint32_t kGoalDescription = 0x0C5D7E6B;       // goal_deScription
constexpr uint32_t kViewGoalsTip = 0x63981D6F;          // view_goals_tip
constexpr uint32_t kViewGoalsCameras = 0x22A555F7;      // view_goals_cameras
constexpr uint32_t kNumGoals = 0x5607E8CC;              // (name unknown: a pro's number of goals)
constexpr uint32_t kViewGoalsInfo = 0xBBC7B40D;         // view_goals_info
constexpr uint32_t kName = 0xA1DC81F9;                  // Name
constexpr uint32_t kParams = 0x7031F10C;                // params
constexpr uint32_t kBeatenInLevelFlag = 0x8690A9CF;     // (name unknown: passed on, never read)
constexpr uint32_t kNumBeaten = 0x8110DCDF;             // num_beaten
constexpr uint32_t kSelectedOnly = 0x5685C1B7;          // selected_only
constexpr uint32_t kClassicOnly = 0xBF6C452E;           // classic_only
constexpr uint32_t kCreatedOnly = 0x185133BB;           // created_only
constexpr uint32_t kGoalIdKey = 0x7C1956AF;             // goalId
constexpr uint32_t kGoalList = 0xC7EBD48F;              // GoalList
constexpr uint32_t kAll = 0xC4E78E22;                   // ALL
constexpr uint32_t kText = 0xC4745838;                  // text
constexpr uint32_t kTrickText = 0x3EAFA520;             // tricktext
constexpr uint32_t kAttemptInfo = 0xD23AD129;           // (name unknown: GetGoalAttemptInfo's structure)
constexpr uint32_t kSkater = 0x5B8AB877;                // skater

const char* const kNoDescription = "Define <goal_description> for this goal and it will appear here....If you'd like, also define <view_goals_tip>, it's optional."; // "Define <goal_description> ..."

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
Script::CArray* NewArray()
{
    Script::CArray* made = Script::NewArray();
    return made ? made->Init() : nullptr;
}
void FreeArray(Script::CArray* array)
{
    Script::CleanUpArray(array);
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
}

// The skater's score: its total (0x00561B40, no source yet) plus the pending trick's points (the base +0x14 times the
// multiplier +0x60, truncated - _ftol).
int TotalWithPending(void* score)
{
    auto* bytes = static_cast<uint8_t*>(score);
    int pending = static_cast<int>(static_cast<double>(*reinterpret_cast<int*>(bytes + 0x14)) *
                                   *reinterpret_cast<float*>(bytes + 0x60));
    return static_cast<Mdl::Score*>(score)->GetTotalScore() + pending;
}

// goal_lose_next_frame spawned for `goal`.
void LoseNextFrame(CGoal* goal)
{
    Script::CStruct* params = NewStructure();
    params->AddChecksum(kGoalIdChecksum, goal->GetGoalId());
    Script::SpawnScript(kGoalLoseNextFrame, params, 0, nullptr, 0, 0, false, false, false, false);
    DeleteStructure(params);
}

} // namespace

// 0x0055C7E0
void CGoalManager::Land()
{
    CheckTrickObjects();
    int count = m_goals.m_count; // (read once)
    for (int i = 0; i < count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CGoal::kVtIsActive))
            continue;
        if (goal->IsHorse()) {
            static_cast<CHorseGoal*>(goal)->Land(); // (the rest skipped)
            return;
        }
        Script::CStruct* params = goal->GetParams();
        if (params->ContainsFlag(kHighCombo)) {
            auto* skater = static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
            if (!skater)
                continue;
            auto* score = static_cast<uint8_t*>(skater->GetScore());
            if (*reinterpret_cast<int*>(score + 0x20) <= 0)
                continue;
            Script::CStruct* check = NewStructure();
            check->AddChecksum(kGoalIdChecksum, goal->GetGoalId());
            check->AddInteger(kLastScoreLanded, *reinterpret_cast<int*>(score + 0x20));
            Script::RunScript(kHighComboCheckScore, check, nullptr, false, nullptr);
            DeleteStructure(check);
        } else if (goal->GetParams()->ContainsFlag(kScoreGoal)) {
            auto* skater = static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
            if (!skater)
                continue;
            void* score = skater->GetScore();
            int needed;
            goal->GetParams()->GetInteger(kScore, &needed, 1);
            if (TotalWithPending(score) < needed)
                continue;
            int total = TotalWithPending(score);
            goal->GetParams()->AddInteger(kWinningScore, total);
            WinGoal(goal->GetGoalId());
            goal->GetParams()->RemoveComponent(kWinningScore);
        } else if (goal->GetParams()->ContainsFlag(kTrickObjects)) {
            reinterpret_cast<CTrickObjectsGoal*>(goal)->CheckScore();
        } else if (goal->GetParams()->ContainsFlag(kTrickBeat)) {
            reinterpret_cast<CTrickBeatGoal*>(goal)->RemoveLandedTricks();
        } else if (goal->GetParams()->ContainsFlag(kSkateTheLine)) {
            static_cast<CSkateLineGoal*>(goal)->Land();
        } else if (goal->GetParams()->ContainsComponentNamed(kArcadeCurrentScoreTotal)) {
            auto* skater = static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
            if (!skater)
                continue;
            void* score = skater->GetScore();
            int current;
            goal->GetParams()->GetInteger(kArcadeCurrentScoreTotal, &current, 0);
            if (TotalWithPending(score) <= current)
                continue;
            int total = TotalWithPending(score);
            goal->GetParams()->AddInteger(kArcadeCurrentScoreTotal, total);
        } else if (goal->RequiresCombo() && goal->GetParams()->ContainsFlag(kComboStarted)) {
            LoseNextFrame(goal);
        }
    }
}

// 0x0055CBB0
void CGoalManager::Bail()
{
    int count = m_goals.m_count; // (read once)
    for (int i = 0; i < count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CGoal::kVtIsActive))
            continue;
        if (goal->RequiresCombo() && goal->GetParams()->ContainsFlag(kComboStarted))
            LoseNextFrame(goal);
        if (goal->IsTetris()) {
            int flag;
            if (goal->GetParams()->GetInteger(kTrickFlag, &flag, 0) && flag)
                goal->GetParams()->AddInteger(kTrickFlag, 0);
        } else if (goal->GetParams()->ContainsFlag(kTrickBeat)) {
            reinterpret_cast<CTrickBeatGoal*>(goal)->ClearLanded();
        }
    }
}

// 0x0055CD70
void CGoalManager::GetViewGoalsInfo(Script::CStruct* out, uint32_t skater)
{
    Script::CStruct* byPro = NewStructure(); // each pro: its goals' info by id, and their number
    Script::CStruct* info = NewStructure();
    int count = m_goals.m_count; // (read once)
    for (int i = 0; i < count; ++i) {
        uint32_t key;
        CGoal* goal = m_goals.GetByIndex(i, &key);
        uint32_t pro = goal->m_skater;
        if (skater && pro != skater)
            continue;
        uint32_t id = goal->GetGoalId();
        int beaten = Virtual<bool>(goal, CGoal::kVtHasWon) ? 1 : 0;
        int locked = goal->IsLocked() ? 1 : 0;
        Script::CStruct* params = goal->GetParams();
        const char* text;
        params->GetString(kViewGoalsText, &text, 1);
        Script::CStruct* entry = NewStructure();
        entry->AddChecksum(kGoalIdChecksum, id);
        entry->AddInteger(kBeaten, beaten);
        entry->AddInteger(kLocked, locked);
        entry->AddInteger(kRewardPoints, goal->GetRewardPoints());
        entry->AddString(kViewGoalsText, text);
        const char* description = nullptr;
        Script::CArray* descriptions = nullptr;
        if (params->GetString(kGoalDescription, &description, 0))
            entry->AddString(kGoalDescription, description);
        else if (params->GetArray(kGoalDescription, &descriptions, 0))
            entry->AddArray(kGoalDescription, descriptions);
        else
            entry->AddString(kGoalDescription, kNoDescription);
        const char* tip = nullptr;
        params->GetString(kViewGoalsTip, &tip, 0);
        if (tip)
            entry->AddString(kViewGoalsTip, tip);
        Script::CArray* cameras = nullptr;
        params->GetArray(kViewGoalsCameras, &cameras, 0);
        if (cameras)
            entry->AddArray(kViewGoalsCameras, cameras);
        if (!byPro->ContainsComponentNamed(pro)) {
            Script::CStruct* empty = NewStructure();
            byPro->AddStructure(pro, empty);
            DeleteStructure(empty);
        }
        Script::CStruct* goals = nullptr;
        byPro->GetStructure(pro, &goals, 1);
        goals->AddStructure(id, entry);
        DeleteStructure(entry);
        int number = 0;
        goals->GetInteger(kNumGoals, &number, 0);
        goals->AddInteger(kNumGoals, ++number);
    }
    // Each pro's goals made an array of their structures: all as info's arrays by pro, or - for one skater - that
    // pro's array alone (the first pro's).
    for (Script::CComponent* component = byPro->GetNextComponent(nullptr); component;) {
        Script::CComponent* nextPro = byPro->GetNextComponent(component);
        Script::CStruct* goals = component->structure;
        int number;
        goals->GetInteger(kNumGoals, &number, 1);
        goals->RemoveComponent(kNumGoals);
        Script::CArray* list = NewArray();
        list->SetSizeAndType(static_cast<uint32_t>(number), Script::ESYMBOLTYPE_STRUCTURE);
        uint32_t index = 0;
        for (Script::CComponent* element = goals->GetNextComponent(nullptr); element;) {
            Script::CComponent* next = goals->GetNextComponent(element);
            Script::CStruct* copy = NewStructure();
            copy->AppendStructureIfAny(element->structure);
            list->SetElement(index++, reinterpret_cast<uint32_t>(copy));
            element = next;
        }
        if (skater) {
            out->AddArrayPointer(kViewGoalsInfo, list);
            DeleteStructure(byPro);
            DeleteStructure(info);
            return;
        }
        info->AddArray(component->name, list);
        FreeArray(list);
        component = nextPro;
    }
    DeleteStructure(byPro);
    out->AddStructurePointer(kViewGoalsInfo, info);
}

// 0x0055D3F0
bool __cdecl CFunc_GoalManager_AddGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    Script::CStruct* goalParams;
    params->GetStructure(kParams, &goalParams, 1);
    return manager->AddGoal(name, goalParams);
}

// 0x0055D450 (RemoveGoal, inlined)
bool __cdecl CFunc_GoalManager_RemoveGoal(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    return manager->RemoveGoal(name);
}

// 0x0055D4D0
bool __cdecl CFunc_GoalManager_RemoveAllGoals(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->RemoveAllGoals();
    return true;
}

// 0x0055D4F0
bool __cdecl CFunc_GoalManager_RestartLastGoal(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->RestartLastGoal();
    return true;
}

// 0x0055D510
bool __cdecl CFunc_GoalManager_LevelUnload(Script::CStruct*, Script::CScript*)
{
    GetGoalManager()->LevelUnload();
    return true;
}

// 0x0055D530: the beaten goals of the level the unnamed integer names, as the script's num_beaten.
bool __cdecl CFunc_GoalManager_NumGoalsBeatenInLevel(Script::CStruct* params, Script::CScript* script)
{
    int level;
    params->GetInteger(0u, &level, 1);
    CGoalManager* manager = GetGoalManager();
    int flag = params->ContainsFlag(kBeatenInLevelFlag);
    script->m_params->AddInteger(kNumBeaten, manager->NumGoalsBeatenInLevel(level, flag));
    return true;
}

// 0x0055D590
bool __cdecl CFunc_GoalManager_AddGoalChoices(Script::CStruct* params, Script::CScript*)
{
    CGoalManager* manager = GetGoalManager();
    bool selected = params->ContainsFlag(kSelectedOnly);
    bool classic = params->ContainsFlag(kClassicOnly);
    bool created = params->ContainsFlag(kCreatedOnly);
    manager->AddGoalChoices(selected, classic, created);
    return true;
}

// 0x0055D600: every goal's goalId and hasBeaten as the script's GoalList.
bool __cdecl CFunc_GoalManager_GetGoalList(Script::CStruct*, Script::CScript* script)
{
    CGoalManager* manager = GetGoalManager();
    Script::CArray* list = NewArray();
    list->SetSizeAndType(static_cast<uint32_t>(manager->m_goals.m_count), Script::ESYMBOLTYPE_STRUCTURE);
    for (int i = 0; i < manager->m_goals.m_count; ++i) {
        CGoal* goal = manager->m_goals.GetByIndex(i);
        Script::CStruct* entry = NewStructure();
        entry->AddChecksum(kGoalIdKey, goal->GetGoalId());
        entry->AddInteger(kHasBeaten, Virtual<bool>(goal, CGoal::kVtHasWon) ? 1 : 0);
        list->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entry));
    }
    script->m_params->AddArray(kGoalList, list);
    FreeArray(list);
    return true;
}

// 0x0055D7D0: with ALL every goal's texts; else goal Name's - `text` with its tricks as the script's tricktext,
// without `text` the goal's own texts (their answer). True without Name.
bool __cdecl CFunc_GoalManager_ReplaceTrickText(Script::CStruct* params, Script::CScript* script)
{
    CGoalManager* manager = GetGoalManager();
    if (params->ContainsFlag(kAll)) {
        manager->ReplaceTrickTextAll();
        return true;
    }
    uint32_t name;
    if (!params->GetChecksum(kName, &name, 0))
        return true;
    const char* text;
    if (!params->GetString(kText, &text, 0))
        return manager->m_goals.Get(name)->ReplaceTrickText();
    char replaced[0x200];
    if (manager->ReplaceTrickText(name, text, replaced, sizeof(replaced))) {
        script->m_params->RemoveComponent(kTrickText);
        script->m_params->AddString(kTrickText, replaced);
    }
    return true;
}

// 0x0055D8E0
bool __cdecl CFunc_GoalManager_GetGoalAttemptInfo(Script::CStruct*, Script::CScript* script)
{
    CGoalManager* manager = GetGoalManager();
    Script::CStruct* results = script->m_params;
    results->AddStructurePointer(kAttemptInfo, manager->GetGoalAttemptInfo());
    return true;
}

// 0x0055D910: the view goals menu's info (of `skater`'s goals only, if given).
bool __cdecl CFunc_GoalManager_GetViewGoalsInfo(Script::CStruct* params, Script::CScript* script)
{
    CGoalManager* manager = GetGoalManager();
    uint32_t skater = 0;
    params->GetChecksum(kSkater, &skater, 0);
    manager->GetViewGoalsInfo(script->m_params, skater);
    return true;
}

} // namespace Game
