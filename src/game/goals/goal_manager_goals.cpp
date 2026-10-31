// GoalSystem::CGoalManager: its goals made from the level's scripts, restarted, saved at the level's end, listed for the
// menus (0x0055BA10-0x0055BE8F, 0x0055BF40-0x0055C7D6; see goal_manager.h).

#include <cstdint>

#include "game/core/mem.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_skate_line.h"
#include "game/goals/goal_net_types.h"
#include "game/goals/goal_names.h"
#include "game/goals/goal_types.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_manager.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/trick_goals.h"

namespace GoalSystem {

namespace {

using namespace GoalNames;

constexpr uint32_t kIsFreeskate = 0x337E7779;     // is_freeskate
constexpr uint32_t kIsCreateGoals = 0x353961D7;   // is_creategoals
constexpr uint32_t kCareerOnly = 0xC18FEF36;      // career_only
constexpr uint32_t kCompetitionFlag = 0x4AF5D34E; // competition
constexpr uint32_t kNetFlag = 0x0D15EA00;         // net
constexpr uint32_t kRaceFlag = 0x25904450;        // race
constexpr uint32_t kTetrisFlag = 0x4147922B;      // tetris
constexpr uint32_t kHorseFlag = 0x9D65D0E7;       // Horse
constexpr uint32_t kFindGapsFlag = 0xC5EC08E6;    // findGaps
constexpr uint32_t kFilmFlag = 0x7DBB41DD;        // Film
constexpr uint32_t kTrickObjectsFlag = 0xEFC35592; // TrickObjects
constexpr uint32_t kSkateTheLine = 0xF268C278;    // SkateTheLine
constexpr uint32_t kTimedScoreRun = 0xD3C2899D;   // TimedScoreRun
constexpr uint32_t kTrickBeat = 0xD0DD047B;       // TrickBeat
constexpr uint32_t kChildren = 0x5E684E45;        // children
constexpr uint32_t kChild = 0xDD4CABD6;           // child
constexpr uint32_t kPro = 0x944B2900;             // pro
constexpr uint32_t kFullName = 0x243B9C3B;        // full_name
constexpr uint32_t kJudgeFullName = 0xA500BC5F;   // judge_full_name
constexpr uint32_t kSk3KillSkater = 0xDEA3057B;   // sk3_killskater
constexpr uint32_t kSk3KillSkaterFinish = 0x0B38ED6B; // SK3_Killskater_Finish
constexpr uint32_t kKillBlur = 0xBDA66E7D;        // kill_blur
constexpr uint32_t kDontKillTriggerOnSuccess = 0x6BEBDBCC; // dont_kill_trigger_on_success
constexpr uint32_t kResetGoalTrigger = 0x17C2E09F; // GoalManager_ResetGoalTrigger
constexpr uint32_t kGoalManagerParams = 0x23D4170A; // GoalManager_Params
constexpr uint32_t kDifficultyLevels = 0xB13D98D3; // difficulty_levels
constexpr uint32_t kNullGoal = 0x3D1CAB0B;        // null_goal
constexpr uint32_t kParent = 0xC2719FB0;          // parent
constexpr uint32_t kCurrentMenu = 0x4D49AC0A;     // current_menu
constexpr uint32_t kText = 0xC4745838;            // text
constexpr uint32_t kFont = 0x2F6BF72D;            // font
constexpr uint32_t kSmall = 0x8ABA15EC;           // small
constexpr uint32_t kScale = 0x13B9DA7B;           // scale
constexpr uint32_t kViewSelectedGoalsMenuSetEvents = 0xE704C86C; // view_selected_goals_menu_set_events
constexpr uint32_t kChooseGoalsMenuSetEvents = 0x6790AC11;       // choose_goals_menu_set_events
constexpr uint32_t kTrickName = 0x5B077CE1;       // trickname
constexpr uint32_t kKeyCombos = 0x79704516;       // key_combos
constexpr uint32_t kAttempts = 0xD023ADBE;        // (name unknown: the times the beaten goals were started)

const char* const kNetJudge = "NetJudge"; // "NetJudge"

CMatchRules* GameMode() { return g_gameMode; }
OnlinePlay::Hub* NetManager() { return OnlinePlay::g_gameNetManager; }
bool InMultiplayerGame() { return static_cast<uint8_t>(ScriptCommands::CFunc_InMultiplayerGame(nullptr, nullptr)) != 0; }

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
    if (array) {
        array->Clear();
        Scripts::DeleteArray(array);
    }
}

// The goal's type by its flags: its size and constructor.
CObjective* MakeGoal(Scripts::CStruct* params)
{
    if (params->HasBareFlag(kCompetitionFlag)) {
        void* memory = MemSys::AllocZeroed(sizeof(CContestObjective));
        return memory ? static_cast<CContestObjective*>(memory)->MakeNew(params) : nullptr;
    }
    if (params->HasBareFlag(kNetFlag)) {
        void* memory = MemSys::AllocZeroed(0x74);
        return memory ? static_cast<COnlineObjective*>(memory)->MakeNew(params) : nullptr;
    }
    if (params->HasBareFlag(kRaceFlag)) {
        void* memory = MemSys::AllocZeroed(0x7C);
        return memory ? static_cast<CRaceObjective*>(memory)->MakeNew(params) : nullptr;
    }
    if (params->HasBareFlag(kTetrisFlag)) {
        void* memory = MemSys::AllocZeroed(0x458);
        return memory ? reinterpret_cast<CObjective*>(static_cast<CTetrisGoal*>(memory)->MakeNew(params)) : nullptr;
    }
    if (params->HasBareFlag(kHorseFlag)) {
        void* memory = MemSys::AllocZeroed(0x70);
        return memory ? static_cast<CLetterGameObjective*>(memory)->MakeNew(params) : nullptr;
    }
    if (params->HasBareFlag(kFindGapsFlag)) {
        void* memory = MemSys::AllocZeroed(sizeof(CHuntGapsObjective));
        return memory ? static_cast<CHuntGapsObjective*>(memory)->MakeNew(params) : nullptr;
    }
    if (params->HasBareFlag(kFilmFlag)) {
        void* memory = MemSys::AllocZeroed(sizeof(CShootVideoObjective));
        return memory ? static_cast<CShootVideoObjective*>(memory)->MakeNew(params) : nullptr;
    }
    if (params->HasBareFlag(kTrickObjectsFlag)) {
        void* memory = MemSys::AllocZeroed(0x70);
        return memory ? reinterpret_cast<CObjective*>(static_cast<CTrickObjectsGoal*>(memory)->MakeNew(params)) : nullptr;
    }
    if (params->HasBareFlag(kSkateTheLine)) {
        void* memory = MemSys::AllocZeroed(0x70);
        return memory ? static_cast<CSkateLineGoal*>(memory)->MakeNew(params) : nullptr;
    }
    if (params->HasBareFlag(kTimedScoreRun)) {
        void* memory = MemSys::AllocZeroed(0x70);
        return memory ? reinterpret_cast<CObjective*>(static_cast<CTimedScoreRunGoal*>(memory)->MakeNew(params)) : nullptr;
    }
    if (params->HasBareFlag(kTrickBeat)) {
        void* memory = MemSys::AllocZeroed(0x47C);
        return memory ? reinterpret_cast<CObjective*>(static_cast<CTrickBeatGoal*>(memory)->MakeNew(params)) : nullptr;
    }
    void* memory = MemSys::AllocZeroed(sizeof(CObjective));
    return memory ? static_cast<CObjective*>(memory)->MakeNew(params) : nullptr;
}

} // namespace

// 0x0055BA10
bool CGoalManager::EnrolObjective(uint32_t id, Scripts::CStruct* params)
{
    bool freeSkate = GameMode()->IsTrue(kIsFreeskate);
    if (!InMultiplayerGame() && freeSkate)
        return false;
    if (GameMode()->IsTrue(kIsCreateGoals) && !params->HasBareFlag(kEditedGoal))
        return false;
    params->PutNameKey(kGoalIdChecksum, id);
    if (params->HasBareFlag(kCareerOnly) && InMultiplayerGame())
        return false;
    CObjective* goal = MakeGoal(params);
    m_goals.PlaceIn(id, goal);
    goal->MergeSkillTierArgs(); // (a failed allocation is used through null, as the original)
    Scripts::CStruct* own = goal->FetchArgs();
    Scripts::CArray* children;
    if (own->FetchList(kChildren, &children, 0)) {
        int count = static_cast<int>(children->m_extent);
        for (int i = 0; i < count; ++i) {
            children->FetchPart(i); // (read twice, as the original)
            goal->AttachChild(children->FetchPart(i));
        }
    } else {
        uint32_t child;
        if (own->ReadHashValue(kChild, &child, 0))
            goal->AttachChild(child);
    }
    if (InMultiplayerGame()) {
        goal->FetchArgs()->AppendWord(kPro, kNetJudge);
        const char* judge = Scripts::FetchText(kJudgeFullName);
        goal->FetchArgs()->AppendWord(kFullName, judge);
    }
    return goal != nullptr;
}

// 0x0055BF40
void CGoalManager::RetryPreviousObjective()
{
    if (!m_previousObjective)
        return;
    CObjective* goal = m_goals.Pull(m_previousObjective);
    goal->SetQuickStart();
    DisengageEveryObjective(false);
    Scripts::StopScriptsTargeting(kSk3KillSkater);
    Scripts::StopScriptsTargeting(kSk3KillSkaterFinish);
    Scripts::LaunchScript(kKillBlur, nullptr, nullptr, false, nullptr);
    if (!Virtual<bool>(goal, CObjective::kVtHasWon) || goal->FetchArgs()->HasBareFlag(kDontKillTriggerOnSuccess)) {
        Scripts::CStruct* params = NewStructure();
        params->PutNameKey("goal_id", goal->GetObjectiveKey()); // "goal_id"
        Scripts::LaunchScript(kResetGoalTrigger, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    Virtual<bool>(goal, CObjective::kVtActivate);
    goal->UnsetQuickStart();
}

// 0x0055C090
void CGoalManager::LevelUnload()
{
    if (NetManager()->InNetGame())
        return;
    Scripts::CStruct* own = NewStructure();
    own->PutSubStruct(kDifficultyLevels, m_args);
    m_goalData->PutSubStruct(kGoalManagerParams, own);
    DeleteStructure(own);
    for (int i = 0; i < m_goals.m_num; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (goal->FetchArgs()->HasBareFlag(kNullGoal) || goal->IsUserMadeObjective())
            continue;
        Scripts::CStruct* data = NewStructure();
        Virtual<void>(goal, CObjective::kVtGetSaveData, data);
        m_goalData->PutSubStruct(goal->GetObjectiveKey(), data);
        DeleteStructure(data);
    }
}

// 0x0055C250
int CGoalManager::CountClearedInStage(int level, int)
{
    LevelUnload();
    int count = 0;
    Scripts::CComponent* component = m_goalData->StepToEntry(nullptr);
    while (component) {
        Scripts::CComponent* next = m_goalData->StepToEntry(component);
        int goalLevel = 0;
        if (component->structure->FetchIntValue(kLevelNum, &goalLevel, 0)) {
            int beaten = 0;
            component->structure->FetchIntValue(kHasBeaten, &beaten, 0);
            if (goalLevel == level && beaten)
                ++count;
        }
        component = next;
    }
    return count;
}

// 0x0055C2F0
void CGoalManager::AppendObjectiveOptions(bool selectedOnly, bool skipEdited, bool editedOnly)
{
    for (int i = 0; i < m_goals.m_num; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        goal->SwapMoveCaption();
        const char* text = nullptr;
        goal->GetObjectiveListCaption(&text);
        if (!text || goal->IsOnlineObjective() || goal->m_parents->m_objectiveKey)
            continue;
        OnlinePlay::CFunc_IsHost(nullptr, nullptr); // (its answer isn't used)
        if (selectedOnly && !goal->IsPicked())
            continue;
        if (skipEdited && goal->IsEditedGoal2())
            continue;
        if (editedOnly && !goal->IsEditedGoal2())
            continue;
        Scripts::CStruct* choice = NewStructure();
        choice->PutNameKey(kGoalIdChecksum, goal->GetObjectiveKey());
        choice->PutNameKey(kParent, kCurrentMenu);
        choice->AppendWord(kText, text);
        choice->PutNameKey(kFont, kSmall);
        choice->PutInt(kScale, 0);
        Scripts::LaunchScript(selectedOnly ? kViewSelectedGoalsMenuSetEvents : kChooseGoalsMenuSetEvents, choice, nullptr,
                          false, nullptr);
        DeleteStructure(choice);
    }
}

// 0x0055C4D0
bool CGoalManager::SwapMoveCaption(uint32_t id, const char* text, char* out, int size)
{
    Scripts::CStruct* params = m_goals.Pull(id)->FetchArgs();
    Scripts::CArray* keyCombos;
    params->FetchList(kKeyCombos, &keyCombos, 1);
    Scripts::CArray* keyTexts = NewArray();
    Scripts::CArray* trickTexts = NewArray();
    uint32_t count = keyCombos->m_extent;
    keyTexts->ConfigureSizeKind(count, Scripts::QBVAL_STRING);
    trickTexts->ConfigureSizeKind(count, Scripts::QBVAL_STRING);
    int premade = -1; // (the created trick's number, with create_a_trick)
    if (params->HasBareFlag(kCreateATrick))
        params->FetchIntValue(kTrickName, &premade, 1);
    bool done = CObjective::BuildTrickTexts(keyCombos, keyTexts, trickTexts, premade);
    if (done)
        CObjective::ReplaceTrickTokens(text, out, keyTexts, trickTexts, size);
    Scripts::ReleaseArrayStorage(keyTexts);
    Scripts::ReleaseArrayStorage(trickTexts);
    FreeArray(keyTexts);
    FreeArray(trickTexts);
    return done;
}

// 0x0055C6E0
Scripts::CStruct* CGoalManager::GetObjectiveTryDetails()
{
    int count = m_goals.m_num; // (read once)
    Scripts::CStruct* info = NewStructure();
    int attempts = 0;
    for (int i = 0; i < count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (!goal->IsOnlineObjective() && Virtual<bool>(goal, CObjective::kVtHasWon))
            attempts += goal->GetTimesStarted();
    }
    info->PutInt(kAttempts, attempts);
    return info;
}

} // namespace Game
