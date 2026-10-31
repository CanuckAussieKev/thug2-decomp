// Game::CGoalManager: its goals made from the level's scripts, restarted, saved at the level's end, listed for the
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

namespace Game {

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

CGameMode* GameMode() { return g_gameMode; }
GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }
bool InMultiplayerGame() { return static_cast<uint8_t>(CFuncs::CFunc_InMultiplayerGame(nullptr, nullptr)) != 0; }

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
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
}

// The goal's type by its flags: its size and constructor.
CGoal* MakeGoal(Script::CStruct* params)
{
    if (params->ContainsFlag(kCompetitionFlag)) {
        void* memory = Mem::Calloc(sizeof(CCompetitionGoal));
        return memory ? static_cast<CCompetitionGoal*>(memory)->Construct(params) : nullptr;
    }
    if (params->ContainsFlag(kNetFlag)) {
        void* memory = Mem::Calloc(0x74);
        return memory ? static_cast<CNetGoal*>(memory)->Construct(params) : nullptr;
    }
    if (params->ContainsFlag(kRaceFlag)) {
        void* memory = Mem::Calloc(0x7C);
        return memory ? static_cast<CRaceGoal*>(memory)->Construct(params) : nullptr;
    }
    if (params->ContainsFlag(kTetrisFlag)) {
        void* memory = Mem::Calloc(0x458);
        return memory ? reinterpret_cast<CGoal*>(static_cast<CTetrisGoal*>(memory)->Construct(params)) : nullptr;
    }
    if (params->ContainsFlag(kHorseFlag)) {
        void* memory = Mem::Calloc(0x70);
        return memory ? static_cast<CHorseGoal*>(memory)->Construct(params) : nullptr;
    }
    if (params->ContainsFlag(kFindGapsFlag)) {
        void* memory = Mem::Calloc(sizeof(CFindGapsGoal));
        return memory ? static_cast<CFindGapsGoal*>(memory)->Construct(params) : nullptr;
    }
    if (params->ContainsFlag(kFilmFlag)) {
        void* memory = Mem::Calloc(sizeof(CFilmGoal));
        return memory ? static_cast<CFilmGoal*>(memory)->Construct(params) : nullptr;
    }
    if (params->ContainsFlag(kTrickObjectsFlag)) {
        void* memory = Mem::Calloc(0x70);
        return memory ? reinterpret_cast<CGoal*>(static_cast<CTrickObjectsGoal*>(memory)->Construct(params)) : nullptr;
    }
    if (params->ContainsFlag(kSkateTheLine)) {
        void* memory = Mem::Calloc(0x70);
        return memory ? static_cast<CSkateLineGoal*>(memory)->Construct(params) : nullptr;
    }
    if (params->ContainsFlag(kTimedScoreRun)) {
        void* memory = Mem::Calloc(0x70);
        return memory ? reinterpret_cast<CGoal*>(static_cast<CTimedScoreRunGoal*>(memory)->Construct(params)) : nullptr;
    }
    if (params->ContainsFlag(kTrickBeat)) {
        void* memory = Mem::Calloc(0x47C);
        return memory ? reinterpret_cast<CGoal*>(static_cast<CTrickBeatGoal*>(memory)->Construct(params)) : nullptr;
    }
    void* memory = Mem::Calloc(sizeof(CGoal));
    return memory ? static_cast<CGoal*>(memory)->Construct(params) : nullptr;
}

} // namespace

// 0x0055BA10
bool CGoalManager::AddGoal(uint32_t id, Script::CStruct* params)
{
    bool freeSkate = GameMode()->IsTrue(kIsFreeskate);
    if (!InMultiplayerGame() && freeSkate)
        return false;
    if (GameMode()->IsTrue(kIsCreateGoals) && !params->ContainsFlag(kEditedGoal))
        return false;
    params->AddChecksum(kGoalIdChecksum, id);
    if (params->ContainsFlag(kCareerOnly) && InMultiplayerGame())
        return false;
    CGoal* goal = MakeGoal(params);
    m_goals.Put(id, goal);
    goal->AppendDifficultyLevelParams(); // (a failed allocation is used through null, as the original)
    Script::CStruct* own = goal->GetParams();
    Script::CArray* children;
    if (own->GetArray(kChildren, &children, 0)) {
        int count = static_cast<int>(children->m_size);
        for (int i = 0; i < count; ++i) {
            children->GetElement(i); // (read twice, as the original)
            goal->AddChild(children->GetElement(i));
        }
    } else {
        uint32_t child;
        if (own->GetChecksum(kChild, &child, 0))
            goal->AddChild(child);
    }
    if (InMultiplayerGame()) {
        goal->GetParams()->AddString(kPro, kNetJudge);
        const char* judge = Script::GetString(kJudgeFullName);
        goal->GetParams()->AddString(kFullName, judge);
    }
    return goal != nullptr;
}

// 0x0055BF40
void CGoalManager::RestartLastGoal()
{
    if (!m_lastGoal)
        return;
    CGoal* goal = m_goals.Get(m_lastGoal);
    goal->SetQuickStart();
    DeactivateAllGoals(false);
    Script::KillSpawnedScriptsThatReferTo(kSk3KillSkater);
    Script::KillSpawnedScriptsThatReferTo(kSk3KillSkaterFinish);
    Script::RunScript(kKillBlur, nullptr, nullptr, false, nullptr);
    if (!Virtual<bool>(goal, CGoal::kVtHasWon) || goal->GetParams()->ContainsFlag(kDontKillTriggerOnSuccess)) {
        Script::CStruct* params = NewStructure();
        params->AddChecksum("goal_id", goal->GetGoalId()); // "goal_id"
        Script::RunScript(kResetGoalTrigger, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    Virtual<bool>(goal, CGoal::kVtActivate);
    goal->UnsetQuickStart();
}

// 0x0055C090
void CGoalManager::LevelUnload()
{
    if (NetManager()->InNetGame())
        return;
    Script::CStruct* own = NewStructure();
    own->AddStructure(kDifficultyLevels, m_params);
    m_goalData->AddStructure(kGoalManagerParams, own);
    DeleteStructure(own);
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (goal->GetParams()->ContainsFlag(kNullGoal) || goal->IsEditedGoal())
            continue;
        Script::CStruct* data = NewStructure();
        Virtual<void>(goal, CGoal::kVtGetSaveData, data);
        m_goalData->AddStructure(goal->GetGoalId(), data);
        DeleteStructure(data);
    }
}

// 0x0055C250
int CGoalManager::NumGoalsBeatenInLevel(int level, int)
{
    LevelUnload();
    int count = 0;
    Script::CComponent* component = m_goalData->GetNextComponent(nullptr);
    while (component) {
        Script::CComponent* next = m_goalData->GetNextComponent(component);
        int goalLevel = 0;
        if (component->structure->GetInteger(kLevelNum, &goalLevel, 0)) {
            int beaten = 0;
            component->structure->GetInteger(kHasBeaten, &beaten, 0);
            if (goalLevel == level && beaten)
                ++count;
        }
        component = next;
    }
    return count;
}

// 0x0055C2F0
void CGoalManager::AddGoalChoices(bool selectedOnly, bool skipEdited, bool editedOnly)
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        goal->ReplaceTrickText();
        const char* text = nullptr;
        goal->GetViewGoalsText(&text);
        if (!text || goal->IsNetGoal() || goal->m_parents->m_goalId)
            continue;
        GameNet::CFunc_IsHost(nullptr, nullptr); // (its answer isn't used)
        if (selectedOnly && !goal->IsSelected())
            continue;
        if (skipEdited && goal->IsEditedGoal2())
            continue;
        if (editedOnly && !goal->IsEditedGoal2())
            continue;
        Script::CStruct* choice = NewStructure();
        choice->AddChecksum(kGoalIdChecksum, goal->GetGoalId());
        choice->AddChecksum(kParent, kCurrentMenu);
        choice->AddString(kText, text);
        choice->AddChecksum(kFont, kSmall);
        choice->AddInteger(kScale, 0);
        Script::RunScript(selectedOnly ? kViewSelectedGoalsMenuSetEvents : kChooseGoalsMenuSetEvents, choice, nullptr,
                          false, nullptr);
        DeleteStructure(choice);
    }
}

// 0x0055C4D0
bool CGoalManager::ReplaceTrickText(uint32_t id, const char* text, char* out, int size)
{
    Script::CStruct* params = m_goals.Get(id)->GetParams();
    Script::CArray* keyCombos;
    params->GetArray(kKeyCombos, &keyCombos, 1);
    Script::CArray* keyTexts = NewArray();
    Script::CArray* trickTexts = NewArray();
    uint32_t count = keyCombos->m_size;
    keyTexts->SetSizeAndType(count, Script::ESYMBOLTYPE_STRING);
    trickTexts->SetSizeAndType(count, Script::ESYMBOLTYPE_STRING);
    int premade = -1; // (the created trick's number, with create_a_trick)
    if (params->ContainsFlag(kCreateATrick))
        params->GetInteger(kTrickName, &premade, 1);
    bool done = CGoal::BuildTrickTexts(keyCombos, keyTexts, trickTexts, premade);
    if (done)
        CGoal::ReplaceTrickTokens(text, out, keyTexts, trickTexts, size);
    Script::CleanUpArray(keyTexts);
    Script::CleanUpArray(trickTexts);
    FreeArray(keyTexts);
    FreeArray(trickTexts);
    return done;
}

// 0x0055C6E0
Script::CStruct* CGoalManager::GetGoalAttemptInfo()
{
    int count = m_goals.m_count; // (read once)
    Script::CStruct* info = NewStructure();
    int attempts = 0;
    for (int i = 0; i < count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (!goal->IsNetGoal() && Virtual<bool>(goal, CGoal::kVtHasWon))
            attempts += goal->GetTimesStarted();
    }
    info->AddInteger(kAttempts, attempts);
    return info;
}

} // namespace Game
