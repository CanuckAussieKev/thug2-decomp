// GoalSystem::CGoalManager: the skater's tricks landed and bails passed to the active goals, the view goals menu's info
// (0x0055C7E0-0x0055D3E2), and the goal manager's script commands 0x0055D3F0-0x0055D954 (see goal_manager.h,
// goal_manager_cfuncs.h).

#include <cstdint>

#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_skate_line.h"
#include "game/goals/goal_net_types.h"
#include "game/goals/goal_manager_cfuncs.h"
#include "game/goals/goal_names.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/trick_goals.h"
#include "game/skater/rider.h"
#include "game/skate/point_tally.h"

namespace GoalSystem {

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
constexpr uint32_t kAttemptInfo = 0xD23AD129;           // (name unknown: GetObjectiveTryDetails's structure)
constexpr uint32_t kSkater = 0x5B8AB877;                // skater

const char* const kNoDescription = "Define <goal_description> for this goal and it will appear here....If you'd like, also define <view_goals_tip>, it's optional."; // "Define <goal_description> ..."

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* made = Scripts::NewStruct();
    return made ? made->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
Scripts::CArray* NewArray()
{
    Scripts::CArray* made = Scripts::NewArray();
    return made ? made->PrepareAll() : nullptr;
}
void FreeArray(Scripts::CArray* array)
{
    Scripts::ReleaseArrayStorage(array);
    if (array) {
        array->Clear();
        Scripts::DeleteArray(array);
    }
}

// The skater's score: its total (0x00561B40, no source yet) plus the pending trick's points (the base +0x14 times the
// multiplier +0x60, truncated - _ftol).
int TotalWithPending(void* score)
{
    auto* bytes = static_cast<uint8_t*>(score);
    int pending = static_cast<int>(static_cast<double>(*reinterpret_cast<int*>(bytes + 0x14)) *
                                   *reinterpret_cast<float*>(bytes + 0x60));
    return static_cast<GameModules::PointTally*>(score)->GetTotalScore() + pending;
}

// goal_lose_next_frame spawned for `goal`.
void LoseNextFrame(CObjective* goal)
{
    Scripts::CStruct* params = NewStructure();
    params->PutNameKey(kGoalIdChecksum, goal->GetObjectiveKey());
    Scripts::SpawnScript(kGoalLoseNextFrame, params, 0, nullptr, 0, 0, false, false, false, false);
    DeleteStructure(params);
}

} // namespace

// 0x0055C7E0
void CGoalManager::Land()
{
    TestTaggedProps();
    int count = m_goals.m_num; // (read once)
    for (int i = 0; i < count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CObjective::kVtIsActive))
            continue;
        if (goal->IsHorse()) {
            static_cast<CLetterGameObjective*>(goal)->Land(); // (the rest skipped)
            return;
        }
        Scripts::CStruct* params = goal->FetchArgs();
        if (params->HasBareFlag(kHighCombo)) {
            auto* skater = static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider());
            if (!skater)
                continue;
            auto* score = static_cast<uint8_t*>(skater->GetPointsTotal());
            if (*reinterpret_cast<int*>(score + 0x20) <= 0)
                continue;
            Scripts::CStruct* check = NewStructure();
            check->PutNameKey(kGoalIdChecksum, goal->GetObjectiveKey());
            check->PutInt(kLastScoreLanded, *reinterpret_cast<int*>(score + 0x20));
            Scripts::LaunchScript(kHighComboCheckScore, check, nullptr, false, nullptr);
            DeleteStructure(check);
        } else if (goal->FetchArgs()->HasBareFlag(kScoreGoal)) {
            auto* skater = static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider());
            if (!skater)
                continue;
            void* score = skater->GetPointsTotal();
            int needed;
            goal->FetchArgs()->FetchIntValue(kScore, &needed, 1);
            if (TotalWithPending(score) < needed)
                continue;
            int total = TotalWithPending(score);
            goal->FetchArgs()->PutInt(kWinningScore, total);
            CompleteObjective(goal->GetObjectiveKey());
            goal->FetchArgs()->EraseModule(kWinningScore);
        } else if (goal->FetchArgs()->HasBareFlag(kTrickObjects)) {
            reinterpret_cast<CTrickObjectsGoal*>(goal)->EvaluatePoints();
        } else if (goal->FetchArgs()->HasBareFlag(kTrickBeat)) {
            reinterpret_cast<CTrickBeatGoal*>(goal)->RemoveLandedTricks();
        } else if (goal->FetchArgs()->HasBareFlag(kSkateTheLine)) {
            static_cast<CSkateLineGoal*>(goal)->Land();
        } else if (goal->FetchArgs()->HasEntryNamed(kArcadeCurrentScoreTotal)) {
            auto* skater = static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider());
            if (!skater)
                continue;
            void* score = skater->GetPointsTotal();
            int current;
            goal->FetchArgs()->FetchIntValue(kArcadeCurrentScoreTotal, &current, 0);
            if (TotalWithPending(score) <= current)
                continue;
            int total = TotalWithPending(score);
            goal->FetchArgs()->PutInt(kArcadeCurrentScoreTotal, total);
        } else if (goal->RequiresCombo() && goal->FetchArgs()->HasBareFlag(kComboStarted)) {
            LoseNextFrame(goal);
        }
    }
}

// 0x0055CBB0
void CGoalManager::Bail()
{
    int count = m_goals.m_num; // (read once)
    for (int i = 0; i < count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CObjective::kVtIsActive))
            continue;
        if (goal->RequiresCombo() && goal->FetchArgs()->HasBareFlag(kComboStarted))
            LoseNextFrame(goal);
        if (goal->IsTetris()) {
            int flag;
            if (goal->FetchArgs()->FetchIntValue(kTrickFlag, &flag, 0) && flag)
                goal->FetchArgs()->PutInt(kTrickFlag, 0);
        } else if (goal->FetchArgs()->HasBareFlag(kTrickBeat)) {
            reinterpret_cast<CTrickBeatGoal*>(goal)->ClearLanded();
        }
    }
}

// 0x0055CD70
void CGoalManager::GetViewGoalsInfo(Scripts::CStruct* out, uint32_t skater)
{
    Scripts::CStruct* byPro = NewStructure(); // each pro: its goals' info by id, and their number
    Scripts::CStruct* info = NewStructure();
    int count = m_goals.m_num; // (read once)
    for (int i = 0; i < count; ++i) {
        uint32_t key;
        CObjective* goal = m_goals.GetByIndex(i, &key);
        uint32_t pro = goal->m_skater;
        if (skater && pro != skater)
            continue;
        uint32_t id = goal->GetObjectiveKey();
        int beaten = Virtual<bool>(goal, CObjective::kVtHasWon) ? 1 : 0;
        int locked = goal->IsLatched() ? 1 : 0;
        Scripts::CStruct* params = goal->FetchArgs();
        const char* text;
        params->FetchText(kViewGoalsText, &text, 1);
        Scripts::CStruct* entry = NewStructure();
        entry->PutNameKey(kGoalIdChecksum, id);
        entry->PutInt(kBeaten, beaten);
        entry->PutInt(kLocked, locked);
        entry->PutInt(kRewardPoints, goal->GetRewardPoints());
        entry->AppendWord(kViewGoalsText, text);
        const char* description = nullptr;
        Scripts::CArray* descriptions = nullptr;
        if (params->FetchText(kGoalDescription, &description, 0))
            entry->AppendWord(kGoalDescription, description);
        else if (params->FetchList(kGoalDescription, &descriptions, 0))
            entry->AppendArrayValue(kGoalDescription, descriptions);
        else
            entry->AppendWord(kGoalDescription, kNoDescription);
        const char* tip = nullptr;
        params->FetchText(kViewGoalsTip, &tip, 0);
        if (tip)
            entry->AppendWord(kViewGoalsTip, tip);
        Scripts::CArray* cameras = nullptr;
        params->FetchList(kViewGoalsCameras, &cameras, 0);
        if (cameras)
            entry->AppendArrayValue(kViewGoalsCameras, cameras);
        if (!byPro->HasEntryNamed(pro)) {
            Scripts::CStruct* empty = NewStructure();
            byPro->PutSubStruct(pro, empty);
            DeleteStructure(empty);
        }
        Scripts::CStruct* goals = nullptr;
        byPro->ReadSubStruct(pro, &goals, 1);
        goals->PutSubStruct(id, entry);
        DeleteStructure(entry);
        int number = 0;
        goals->FetchIntValue(kNumGoals, &number, 0);
        goals->PutInt(kNumGoals, ++number);
    }
    // Each pro's goals made an array of their structures: all as info's arrays by pro, or - for one skater - that
    // pro's array alone (the first pro's).
    for (Scripts::CComponent* component = byPro->StepToEntry(nullptr); component;) {
        Scripts::CComponent* nextPro = byPro->StepToEntry(component);
        Scripts::CStruct* goals = component->structure;
        int number;
        goals->FetchIntValue(kNumGoals, &number, 1);
        goals->EraseModule(kNumGoals);
        Scripts::CArray* list = NewArray();
        list->ConfigureSizeKind(static_cast<uint32_t>(number), Scripts::QBVAL_STRUCT);
        uint32_t index = 0;
        for (Scripts::CComponent* element = goals->StepToEntry(nullptr); element;) {
            Scripts::CComponent* next = goals->StepToEntry(element);
            Scripts::CStruct* copy = NewStructure();
            copy->AppendStructureIfAny(element->structure);
            list->SetElement(index++, reinterpret_cast<uint32_t>(copy));
            element = next;
        }
        if (skater) {
            out->AppendArrayRef(kViewGoalsInfo, list);
            DeleteStructure(byPro);
            DeleteStructure(info);
            return;
        }
        info->AppendArrayValue(component->name, list);
        FreeArray(list);
        component = nextPro;
    }
    DeleteStructure(byPro);
    out->AppendStructRef(kViewGoalsInfo, info);
}

// 0x0055D3F0
bool __cdecl CFunc_GoalManager_AddGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    Scripts::CStruct* goalParams;
    params->ReadSubStruct(kParams, &goalParams, 1);
    return manager->EnrolObjective(name, goalParams);
}

// 0x0055D450 (DropObjective, inlined)
bool __cdecl CFunc_GoalManager_RemoveGoal(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    return manager->DropObjective(name);
}

// 0x0055D4D0
bool __cdecl CFunc_GoalManager_RemoveAllGoals(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->WipeObjectives();
    return true;
}

// 0x0055D4F0
bool __cdecl CFunc_GoalManager_RestartLastGoal(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->RetryPreviousObjective();
    return true;
}

// 0x0055D510
bool __cdecl CFunc_GoalManager_LevelUnload(Scripts::CStruct*, Scripts::CScript*)
{
    GetObjectiveBoard()->LevelUnload();
    return true;
}

// 0x0055D530: the beaten goals of the level the unnamed integer names, as the script's num_beaten.
bool __cdecl CFunc_GoalManager_NumGoalsBeatenInLevel(Scripts::CStruct* params, Scripts::CScript* script)
{
    int level;
    params->FetchIntValue(0u, &level, 1);
    CGoalManager* manager = GetObjectiveBoard();
    int flag = params->HasBareFlag(kBeatenInLevelFlag);
    script->m_args->PutInt(kNumBeaten, manager->CountClearedInStage(level, flag));
    return true;
}

// 0x0055D590
bool __cdecl CFunc_GoalManager_AddGoalChoices(Scripts::CStruct* params, Scripts::CScript*)
{
    CGoalManager* manager = GetObjectiveBoard();
    bool selected = params->HasBareFlag(kSelectedOnly);
    bool classic = params->HasBareFlag(kClassicOnly);
    bool created = params->HasBareFlag(kCreatedOnly);
    manager->AppendObjectiveOptions(selected, classic, created);
    return true;
}

// 0x0055D600: every goal's goalId and hasBeaten as the script's GoalList.
bool __cdecl CFunc_GoalManager_GetGoalList(Scripts::CStruct*, Scripts::CScript* script)
{
    CGoalManager* manager = GetObjectiveBoard();
    Scripts::CArray* list = NewArray();
    list->ConfigureSizeKind(static_cast<uint32_t>(manager->m_goals.m_num), Scripts::QBVAL_STRUCT);
    for (int i = 0; i < manager->m_goals.m_num; ++i) {
        CObjective* goal = manager->m_goals.GetByIndex(i);
        Scripts::CStruct* entry = NewStructure();
        entry->PutNameKey(kGoalIdKey, goal->GetObjectiveKey());
        entry->PutInt(kHasBeaten, Virtual<bool>(goal, CObjective::kVtHasWon) ? 1 : 0);
        list->SetElement(static_cast<uint32_t>(i), reinterpret_cast<uint32_t>(entry));
    }
    script->m_args->AppendArrayValue(kGoalList, list);
    FreeArray(list);
    return true;
}

// 0x0055D7D0: with ALL every goal's texts; else goal Name's - `text` with its tricks as the script's tricktext,
// without `text` the goal's own texts (their answer). True without Name.
bool __cdecl CFunc_GoalManager_ReplaceTrickText(Scripts::CStruct* params, Scripts::CScript* script)
{
    CGoalManager* manager = GetObjectiveBoard();
    if (params->HasBareFlag(kAll)) {
        manager->ReplaceTrickTextAll();
        return true;
    }
    uint32_t name;
    if (!params->ReadHashValue(kName, &name, 0))
        return true;
    const char* text;
    if (!params->FetchText(kText, &text, 0))
        return manager->m_goals.Pull(name)->SwapMoveCaption();
    char replaced[0x200];
    if (manager->SwapMoveCaption(name, text, replaced, sizeof(replaced))) {
        script->m_args->EraseModule(kTrickText);
        script->m_args->AppendWord(kTrickText, replaced);
    }
    return true;
}

// 0x0055D8E0
bool __cdecl CFunc_GoalManager_GetGoalAttemptInfo(Scripts::CStruct*, Scripts::CScript* script)
{
    CGoalManager* manager = GetObjectiveBoard();
    Scripts::CStruct* results = script->m_args;
    results->AppendStructRef(kAttemptInfo, manager->GetObjectiveTryDetails());
    return true;
}

// 0x0055D910: the view goals menu's info (of `skater`'s goals only, if given).
bool __cdecl CFunc_GoalManager_GetViewGoalsInfo(Scripts::CStruct* params, Scripts::CScript* script)
{
    CGoalManager* manager = GetObjectiveBoard();
    uint32_t skater = 0;
    params->ReadHashValue(kSkater, &skater, 0);
    manager->GetViewGoalsInfo(script->m_args, skater);
    return true;
}

} // namespace Game
