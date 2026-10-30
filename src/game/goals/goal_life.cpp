// Game::CGoal's life: made, initialized, started, stopped, won, unlocked, rewarded, destroyed; its goal flags set
// (0x00555DF0-0x00556109, 0x00556110-0x0055632F, 0x005566A0-0x005568AF, 0x00556F90-0x00557ABF; see goal.h).

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/goals/goal.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/end_run.h"
#include "game/skate/game_mode.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/core/stubs.h"
#include "game/gfx/weather.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "hook/game_code.h"

namespace Game {

GAME_GLOBAL(float, g_goalLifeSecondsPerMillisecond, 0x006461D8, 0.001f);

namespace {

using namespace GoalCalls;
using namespace GoalNames;

constexpr uint32_t kGroupNumber = 0xBAE9387B;          // group_number
constexpr uint32_t kSingleTimer = 0x04AB5F14;          // single_timer
constexpr uint32_t kRetryStage = 0xC6B4258F;           // retry_stage
constexpr uint32_t kSkater = 0x5B8AB877;               // skater
constexpr uint32_t kNone = 0x806FFF30;                 // none
constexpr uint32_t kUnlockedByAnother = 0x8A3A324E;    // unlocked_by_another
constexpr uint32_t kDestroy = 0x83B57984;              // destroy
constexpr uint32_t kDifficultyLevelParams = 0x85104AB3; // difficulty_level_params
constexpr uint32_t kInit = 0x398B1B8B;                 // init
constexpr uint32_t kDontDeactivateOnExpiry = 0x79BBFFCB; // dont_deactivate_upon_expiration
constexpr uint32_t kResetScore = 0x4599B58C;           // reset_score
constexpr uint32_t kActivate = 0x4999FB52;             // activate
constexpr uint32_t kKeyCombos = 0x79704516;            // key_combos
constexpr uint32_t kDeactivate = 0x01C92CAC;           // deactivate
constexpr uint32_t kTestingGoal = 0x54D3CAC1;          // testing_goal
constexpr uint32_t kJustWonGoal = 0xC309CAD1;          // just_won_goal
constexpr uint32_t kReadySkaterForEarlyEnd = 0x719AE783; // ready_skater_for_early_end_current_goal
constexpr uint32_t kCustomScoreRecord = 0xFC9630AB;    // custom_score_record
constexpr uint32_t kHighCombo = 0xC63432A7;            // high_combo
constexpr uint32_t kNewRecord = 0xE7B8254F;            // (name unknown: a new record set)
constexpr uint32_t kRewardParams = 0x128B3088;         // reward_params
constexpr uint32_t kSuccess = 0x90FF204D;              // success
constexpr uint32_t kIsCareer = 0x1DED1EA4;             // is_career
constexpr uint32_t kIsClassic = 0x5A2C589A;            // is_classic
constexpr uint32_t kRewardPointsName = 0x35A1C616;     // RewardPoints
constexpr uint32_t kCheckChapterAdvancement = 0x7CF09270; // CheckChapterAdvancementStatus
constexpr uint32_t kAlwaysInitializeGoal = 0x4E863E93; // always_initialize_goal
constexpr uint32_t kUnlockScript = 0x951B4F10;         // unlock
constexpr uint32_t kRewardsGiven = 0xC7FEF529;         // (name unknown: the rewards structure's flag)
constexpr uint32_t kRewardTrickslot = 0xB6432D23;      // reward_trickslot
constexpr uint32_t kMaxTrickslots = 0x20297D6F;        // max_number_of_special_trickslots
constexpr uint32_t kShouldRemoveTrick = 0xBC678826;    // should_remove_trick
constexpr uint32_t kAlreadyAddedTrickslot = 0x65CA5FFC; // already_added_trickslot
constexpr uint32_t kJustAddedTrickslot = 0xB45FD352;   // just_added_trickslot
constexpr uint32_t kGoalRewards = 0x67F6E334;          // goal_rewards
constexpr uint32_t kRewardStat = 0xDE569045;           // reward_stat
constexpr uint32_t kRewardGoal = 0x0232844A;           // Reward_Goal
constexpr uint32_t kRewardPoints = 0xBAFFB052;         // reward_points
constexpr uint32_t kSilentActivation = 0x2D4C5829;     // silent_activation
constexpr uint32_t kNoFlagSound = 0x524566E3;          // (name unknown: no flag sound)
constexpr uint32_t kGoalGotFlagSound = 0x5ED34DEB;     // goal_got_flag_sound
constexpr uint32_t kTrickObjects = 0xEFC35592;         // TrickObjects
constexpr uint32_t kGaps = 0xD76C173E;                 // Gaps
constexpr uint32_t kNoGapMessages = 0x5BC07EAA;        // No_Gap_Messages
constexpr uint32_t kGoalGapsGotGap = 0x96DE9A0C;       // goal_gaps_got_gap

GAME_VTABLE(kGoalVtable, 0x0064DDD0,
            &Game::CGoal::Destroy, &Game::CGoal::ShouldExpire, &Game::CGoal::IsExpired, &Game::CGoal::IsActive,
            Nx::VirtualSlot3Jump, &Game::CGoal::Expire, &Game::CGoal::LoadSaveData, &Game::CGoal::GetSaveData,
            &Game::CGoal::HasTimeLimit, Stubs::ReturnTrue, &Game::CGoal::HasComponentA302D7CD,
            &Game::CGoal::Activate, &Game::CGoal::SetActive, &Game::CGoal::Deactivate, &Game::CGoal::SetInactive,
            &Game::CGoal::Win, &Game::CGoal::Lose, &Game::CGoal::Update, &Game::CGoal::IsPaused,
            &Game::CGoal::SetFlag, &Game::CGoal::StageFlagSet, &Game::CGoal::HasWon,
            &Game::CGoal::NetStageFlagSet, &Game::CGoal::HasWonGoalOrChild, &Game::CGoal::IsInitialized,
            &Game::CGoal::CanRetry, &Game::CGoal::MarkWon, &Game::CGoal::SetStageFlag,
            &Game::CGoal::SetNetStageFlag, &Game::CGoal::UnBeat);

// The x87 code's float to int conversion (_ftol): truncation.
int Truncate(double value) { return static_cast<int>(value); }

GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }
uint32_t GameModeName() { return static_cast<Game::CGameMode*>(SkateCalls::GameMode())->GetNameChecksum(); }

Script::CStruct* NewStructure()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}
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
// A goal's list node out of its list (linked to itself).
void Unlink(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// The goal's pro (Game::CGoalPed, 0x14 bytes): 0x0055EB40 the constructor (its goal's parameters); its destructor
// is empty (0x005480A0).
CGoalPed* NewGoalPed(Script::CStruct* params)
{
    void* memory = Mem::New(0x14);
    return memory ? static_cast<CGoalPed*>(memory)->Construct(params) : nullptr;
}
inline void DestroyGoalPedBody(CGoalPed*) {}

// Lst::Search<PlayerInfo> (vtable 0x0064C1A8) for the network manager's FirstPlayerInfo / NextPlayerInfo.
struct PlayerSearch {
    void** m_vtable = Mdl::kPlayerSearchVtable;
    void* m_node = nullptr;
};
Obj::CCompositeObject* PlayerSkater(void* player)
{
    return *reinterpret_cast<Obj::CCompositeObject**>(static_cast<uint8_t*>(player) + 0x14);
}
// A skater's score (its SkaterScore component's +0x18: Mdl::Score) reset (0x00564A10) / its total (0x00561B40).
void* ScoreOf(Obj::CCompositeObject* skater)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterScore)) + 0x18);
}
void ResetScore(void* score) { static_cast<Mdl::Score*>(score)->Reset(); }
int TotalScore(void* score) { return static_cast<Mdl::Score*>(score)->GetTotalScore(); }
// The root of a goal's parents (the last one whose first parent link has a goal), or null if it has none.
CGoal* RootOf(void* manager, GoalLink* parents)
{
    CGoal* root = nullptr;
    for (GoalLink* link = parents; link && link->m_goalId; link = root->m_parents)
        root = GetGoal(manager, link->m_goalId, 1);
    return root;
}

} // namespace

GAME_VTABLE(kGoalNodeVtable, 0x0064DDCC, &Game::CGoal::DestroyNode); // (also used by goal_manager.cpp)
// 0x00556FE0
CGoal* CGoal::Construct(Script::CStruct* params)
{
    m_node.data = this;
    m_node.priority = 0;
    m_node.next = &m_node;
    m_node.prev = &m_node;
    m_node.vtable = kGoalVtable;
    m_stageFlags = 0;
    m_netStageFlags = 0;
    m_children = NewLink();
    m_parents = NewLink();
    m_state = static_cast<uint8_t>((m_state & ~(kStateActive | kStatePaused)) | kStateNoTimeLimit);
    m_flags &= ~(kFlagSeen | kFlagWon | kFlagWonInNetGame);
    m_stageFlags = 0;
    m_netStageFlags = 0;
    m_flags = static_cast<uint8_t>((m_flags & ~(kFlagInitialized | kFlagSelected)) | kFlagDeactivateOnExpire);
    m_timesStarted = 0;
    m_timeLeft = 0;
    m_unknown68 = -1;
    m_unknown64 = -1;
    m_params = NewStructure();
    m_params->AppendStructureIfAny(params);
    m_ped = NewGoalPed(m_params);
    m_goalFlags = NewStructure();
    m_numFlags = 0;
    m_numFlagsSet = 0;
    m_elapsed = 0;
    m_bestTime = 0x7FFFFFFF;
    m_bestScore = 0;
    m_endRunType = 0;
    m_level = SkateCalls::Career()->GetLevel();
    m_group = -1;
    params->GetInteger(kGroupNumber, &m_group, 0);
    CreateGoalFlags(params);
    if (params->ContainsFlag(kSingleTimer))
        m_inherited |= 2;
    if (params->ContainsFlag(kRetryStage))
        m_inherited |= 4;
    m_skater = kNone;
    params->GetChecksum(kSkater, &m_skater, 0);
    if (params->ContainsFlag(kUnlockedByAnother) || params->ContainsFlag(kProGoal))
        m_flags |= kFlagLocked;
    else
        m_flags &= ~kFlagLocked;
    return this;
}

// 0x00556110
void CGoal::DestroyBody()
{
    m_node.vtable = kGoalVtable;
    uint32_t script;
    if (m_params->GetChecksum(kDestroy, &script, 0))
        Script::RunScript(script, m_params, nullptr, false, nullptr);
    if (m_ped) {
        DestroyGoalPedBody(m_ped);
        Mem::Free(m_ped);
    }
    DeleteStructure(m_params);
    DeleteStructure(m_goalFlags);
    for (GoalLink* link = m_children; link;) {
        GoalLink* next = link->m_next;
        Mem::Free(link);
        link = next;
    }
    for (GoalLink* link = m_parents; link;) {
        GoalLink* next = link->m_next;
        Mem::Free(link);
        link = next;
    }
    m_node.vtable = kGoalNodeVtable;
    Unlink(&m_node);
}

// 0x005566A0
CGoal* CGoal::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x005560D0
CGoal* CGoal::DestroyNode(uint8_t flags)
{
    m_node.vtable = kGoalNodeVtable;
    Unlink(&m_node);
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00556F90
void CGoal::AppendDifficultyLevelParams()
{
    Script::CArray* levels;
    if (!m_params->GetArray(kDifficultyLevelParams, &levels, 0))
        return;
    int level = GetDifficultyLevel(GoalManager(), false);
    m_params->AppendStructureIfAny(reinterpret_cast<Script::CStruct*>(levels->GetElement(static_cast<uint32_t>(level))));
    CreateGoalFlags(m_params);
}

// 0x00557270
void CGoal::Init()
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_bottomUpHeap);
    // (AppendDifficultyLevelParams, inlined)
    Script::CArray* levels;
    if (m_params->GetArray(kDifficultyLevelParams, &levels, 0)) {
        int level = GetDifficultyLevel(GoalManager(), false);
        m_params->AppendStructureIfAny(
            reinterpret_cast<Script::CStruct*>(levels->GetElement(static_cast<uint32_t>(level))));
        CreateGoalFlags(m_params);
    }
    uint32_t script;
    if (m_params->GetChecksum(kInit, &script, 0))
        Script::RunScript(script, m_params, nullptr, false, nullptr);
    ReplaceTrickText();
    m_flags |= kFlagInitialized;
    int unlimited = 0;
    m_params->GetInteger(kUnlimitedTime, &unlimited, 0);
    if (unlimited)
        m_state |= kStateNoTimeLimit;
    if (m_params->ContainsComponentNamed(kDontDeactivateOnExpiry))
        m_flags &= ~kFlagDeactivateOnExpire;
    Mem::Manager::Get()->PopContext();
}

// 0x00557360
bool CGoal::Activate()
{
    if (Virtual<bool>(this, kVtIsActive))
        return false;
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_bottomUpHeap);
    void* manager = GoalManager();
    ++m_timesStarted;
    m_elapsed = 0;
    GameNet::Manager* net = NetManager();
    PlayerSearch search;
    if (m_params->ContainsFlag(kResetScore)) {
        for (void* player = net->FirstPlayerInfo(&search, false); player; player = net->NextPlayerInfo(&search, false))
            ResetScore(ScoreOf(PlayerSkater(player)));
    }
    // A child goal: the timer passed down (single_timer: its root's, in whole seconds), the root's state for it.
    CGoal* root = m_parents->m_goalId ? RootOf(manager, m_parents) : nullptr;
    if (root) {
        if (m_inherited & 2)
            m_timeLeft = Truncate(root->m_timeLeft * static_cast<double>(g_goalLifeSecondsPerMillisecond)) * 1000;
        else
            ResetTimer();
        if (Virtual<bool>(root, kVtIsInitialized)) {
            root->Uninitialize(false, 0);
            if (Virtual<bool>(this, kVtIsInitialized)) {
                Uninitialize(false, 0);
                Init();
            }
        }
    } else {
        ResetTimer();
    }
    if (!Virtual<bool>(this, kVtIsInitialized))
        Init();
    m_flags &= ~kFlagEndOfRunStarted;
    m_params->RemoveFlag(kComboStarted);
    uint32_t script;
    if (m_params->GetChecksum(kActivate, &script, 0))
        Script::RunScript(script, m_params, nullptr, false, nullptr);
    if (!net->InNetGame())
        m_flags |= kFlagSeen;
    Virtual<void>(this, kVtSetActive);
    AddActiveGoal(manager, this);
    Mem::Manager::Get()->PopContext();
    return true;
}

// 0x00557560
bool CGoal::Deactivate(bool, bool resetTree)
{
    if (!Virtual<bool>(this, kVtIsActive))
        return false;
    if (m_params->ContainsFlag(kSpecial))
        m_params->RemoveComponent(kKeyCombos);
    Virtual<void>(this, kVtSetInactive);
    if (!m_params->ContainsFlag(kSaveGoalFlags))
        ResetGoalFlags();
    ClearChildWins();
    uint32_t script;
    if (m_params->GetChecksum(kDeactivate, &script, 0))
        Script::RunScript(script, m_params, nullptr, false, nullptr);
    if (!m_params->ContainsFlag(kQuickStart))
        m_params->RemoveFlag(kTestingGoal);
    if (!m_params->ContainsFlag(kJustWonGoal) && !m_params->ContainsFlag(kQuickStart)) {
        Script::RunScript(kReadySkaterForEarlyEnd, m_params, nullptr, false, nullptr);
        if (resetTree && m_parents && m_parents->m_goalId) {
            Uninitialize(false, 0);
            void* manager = GoalManager();
            CGoal* root = RootOf(manager, m_parents);
            if (root && !Virtual<bool>(root, kVtHasWon))
                root->Init();
        }
    }
    Unlink(&m_node);
    return true;
}

// 0x005576D0
bool CGoal::Win()
{
    m_params->AddInteger(kWinRecord, 0);
    GoalLink* children = m_children;
    if (children->m_goalId) {
        // A goal of stages: the next one started.
        void* manager = GoalManager();
        Virtual<bool>(this, kVtDeactivate, false, true);
        Uninitialize(false, 0);
        GetGoal(manager, children->m_goalId, 1)->Init();
        ActivateGoal(manager, children->m_goalId, false);
        children->m_won = true;
        return true;
    }
    m_params->AddChecksum(kNoName, kJustWonGoal);
    GameNet::Manager* net = NetManager();
    bool newRecord = false;
    uint32_t type;
    if (!net->InNetGame() && m_params->GetChecksum(kRecordType, &type, 0)) {
        int record = 0;
        bool better = false;
        if (type == kScore) {
            int custom;
            if (m_params->GetInteger(kCustomScoreRecord, &custom, 0)) {
                record = custom;
                better = record > m_bestScore;
            } else {
                void* score = ScoreOf(SkateCalls::GetLocalSkater());
                record = m_params->ContainsFlag(kHighCombo) ? *reinterpret_cast<int*>(static_cast<uint8_t*>(score) + 0x20)
                                                            : TotalScore(score);
                better = record > m_bestScore;
            }
            if (better)
                m_bestScore = record;
        } else if (type == kTime) {
            record = m_elapsed;
            better = static_cast<uint32_t>(record) < static_cast<uint32_t>(m_bestTime);
            if (better)
                m_bestTime = record;
        }
        if (better) {
            m_params->AddInteger(kWinRecord, record);
            newRecord = true;
        }
    }
    bool rewarded = false;
    if (!Virtual<bool>(this, kVtHasWon) && !net->InNetGame()) {
        Script::CStruct* rewards = NewStructure();
        GetRewardsStructure(rewards);
        if (newRecord)
            rewards->AddChecksum(kNoName, kNewRecord);
        m_params->AddStructurePointer(kRewardParams, rewards);
        UnlockRewardGoals();
        rewarded = true;
    } else {
        m_params->RemoveComponent(kRewardParams);
    }
    if (newRecord)
        m_params->AddChecksum(kNoName, kNewRecord);
    else
        m_params->RemoveFlag(kNewRecord);
    uint32_t script;
    if (m_params->GetChecksum(kSuccess, &script, 0))
        Script::RunScript(script, m_params, nullptr, false, nullptr);
    bool testing = m_params->ContainsFlag(kTestingGoal);
    if (Virtual<bool>(this, kVtIsActive))
        Virtual<bool>(this, kVtDeactivate, false, true);
    if (!testing) {
        Virtual<void>(this, kVtMarkWon);
        if (rewarded) {
            Obj::CSkaterCareer* career = SkateCalls::Career();
            if (SkateCalls::GameModeIsTrue(kIsCareer)) {
                career->AddCash(GetRewardPoints());
                Script::CStruct* params = NewStructure();
                params->AddInteger(kRewardPointsName, GetRewardPoints());
                Script::RunScript(kCheckChapterAdvancement, params, nullptr, false, nullptr);
                DeleteStructure(params);
            } else if (SkateCalls::GameModeIsTrue(kIsClassic)) {
                career->IncrementCounter();
            }
        }
    }
    if (net->InNetGame())
        Uninitialize(false, 0);
    m_params->RemoveFlag(kJustWonGoal);
    return true;
}

// 0x00557A40
void CGoal::Unlock()
{
    if (!(m_flags & kFlagLocked))
        return;
    m_flags &= ~kFlagLocked;
    if (NetManager()->InNetGame())
        return;
    if (!Virtual<bool>(this, kVtHasWon) || GameModeName() == kClassic || m_params->ContainsFlag(kAlwaysInitializeGoal) ||
        m_params->ContainsFlag(kEditedGoal))
        Init();
    RunCallbackScript(kUnlockScript);
}

// 0x00555DF0
void CGoal::GetRewardsStructure(Script::CStruct* rewards)
{
    void* manager = GoalManager();
    GameNet::Manager* net = NetManager();
    if (!m_params->ContainsFlag(kEditedGoal))
        ShowGoalPoints(manager);
    rewards->AddChecksum(kNoName, kRewardsGiven);
    if (net->InNetGame())
        return;
    // A special trick slot as the reward, while there's room for one.
    if (m_params->ContainsFlag(kRewardTrickslot) && !m_params->ContainsFlag(kCreateATrick)) {
        Obj::CSkaterProfile* profile = SkateCalls::GetProfileFromParams(nullptr);
        int slots = profile->GetMaxSpecials();
        if (slots <= Script::GetInteger(kMaxTrickslots)) {
            int remove = 0;
            m_params->GetInteger(kShouldRemoveTrick, &remove, 0);
            if (remove == 1) {
                m_params->AddInteger(kShouldRemoveTrick, 0);
                m_params->AddChecksum(kNoName, kAlreadyAddedTrickslot);
                m_params->AddInteger(kJustAddedTrickslot, 1);
            }
        }
    }
    Script::CStruct* all = Script::GetStructure(kGoalRewards);
    uint32_t id = 0;
    m_params->GetChecksum(kGoalIdName, &id, 1);
    Script::CStruct* own = nullptr;
    all->GetStructure(id, &own, 0);
    if (own) {
        if (own->ContainsFlag(kRewardStat)) {
            own->RemoveComponent(kRewardStat);
            own->AddInteger(kRewardStat, 1);
        }
        rewards->AppendStructureIfAny(own);
        return;
    }
    if (m_params->ContainsFlag(kRewardStat)) {
        rewards->AddInteger(kRewardStat, 1);
        return;
    }
    int stat;
    if (m_params->GetInteger(kRewardStat, &stat, 0))
        rewards->AddInteger(kRewardStat, stat);
}

// 0x00555FB0
void CGoal::UnlockRewardGoals()
{
    void* manager = GoalManager();
    uint32_t goal = 0;
    if (m_params->GetChecksum(kRewardGoal, &goal, 0)) {
        UnlockGoal(manager, goal);
        return;
    }
    Script::CArray* goals;
    if (!m_params->GetArray(kRewardGoal, &goals, 0))
        return;
    int count = static_cast<int>(goals->m_size);
    for (int i = 0; i < count; ++i) {
        uint32_t id = goals->GetElement(static_cast<uint32_t>(i));
        if (id)
            UnlockGoal(manager, id);
    }
}

// 0x00556050
int CGoal::GetRewardPoints()
{
    int points = 0;
    if (m_params->GetInteger(kRewardPoints, &points, 0))
        return points;
    Script::CArray* byLevel;
    if (m_params->GetArray(kRewardPoints, &byLevel, 0)) {
        int level = GetDifficultyLevel(GoalManager(), false);
        if (level >= 0 && level < static_cast<int>(byLevel->m_size))
            return static_cast<int>(byLevel->GetElement(static_cast<uint32_t>(level)));
    }
    return 0;
}

// 0x00556330
bool CGoal::SetFlag(uint32_t flag, int value, uint32_t sub)
{
    // A silently activated goal (not won yet) takes flags while inactive, and is won by them.
    bool silent = m_params->ContainsFlag(kSilentActivation) && !Virtual<bool>(this, kVtHasWon);
    if (!Virtual<bool>(this, kVtIsActive) && !silent)
        return false;
    void* manager = GoalManager();
    if ((m_flags & kFlagLocked) || IsGroupLocked(manager, m_group, m_level))
        return false;
    Script::CStruct* flags = m_goalFlags;
    if (sub)
        m_goalFlags->GetStructure(sub, &flags, 1);
    bool changed = false;
    if (flags->ContainsComponentNamed(flag)) {
        int old;
        flags->GetInteger(flag, &old, 1);
        flags->AddInteger(flag, value);
        if (old != value) {
            bool counted;
            if (sub) {
                int set;
                flags->GetInteger(kNumFlagsSet, &set, 1);
                counted = value != 0;
                set += counted ? 1 : -1;
                flags->AddInteger(kNumFlagsSet, set);
            } else {
                counted = value != 0;
                m_numFlagsSet += counted ? 1 : -1;
            }
            if (counted && !m_params->ContainsFlag(kNoFlagSound))
                Script::RunScript(kGoalGotFlagSound, nullptr, nullptr, false, nullptr);
            if (m_params->ContainsFlag(kUseHudCounter))
                Script::RunScript(kGoalUpdateCounter, m_params, nullptr, false, nullptr);
            m_params->AddInteger(kNumFlagsSet, m_numFlagsSet);
        }
        changed = true;
    }
    if (!silent)
        return changed;
    if (m_params->ContainsFlag(kTrickObjects))
        return changed;
    if (AllFlagsSet()) {
        void* goals = GoalManager();
        uint32_t id = GetGoalId();
        WinGoal(goals, id);
    } else if (!sub && m_params->ContainsFlag(kGaps) && !m_params->ContainsFlag(kNoGapMessages)) {
        Script::CStruct* params = NewStructure();
        params->AppendStructureIfAny(m_params);
        Script::RunScript(kGoalGapsGotGap, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    return changed;
}

// 0x005566D0
void CGoal::CreateGoalFlags(Script::CStruct* params)
{
    m_goalFlags->Clear();
    m_numFlags = 0;
    Script::CArray* flags;
    if (params->GetArray(kGoalFlags, &flags, 0)) {
        if (flags->m_type == Script::ESYMBOLTYPE_STRUCTURE) {
            // One structure per skater: its own flags and counts.
            m_goalFlags->AddChecksum(kNoName, kSkaterFlags);
            int skaters = static_cast<int>(flags->m_size);
            for (int i = 0; i < skaters; ++i) {
                auto* entry = reinterpret_cast<Script::CStruct*>(flags->GetElement(static_cast<uint32_t>(i)));
                uint32_t skater;
                entry->GetChecksum(kSkater, &skater, 1);
                Script::CStruct* own = NewStructure();
                Script::CArray* names;
                entry->GetArray(kGoalFlags, &names, 1);
                int count = static_cast<int>(names->m_size);
                for (int k = 0; k < count; ++k)
                    own->AddInteger(names->GetElement(static_cast<uint32_t>(k)), 0);
                own->AddInteger(kNumFlags, count);
                own->AddInteger(kNumFlagsSet, 0);
                m_goalFlags->AddStructure(skater, own);
                DeleteStructure(own);
            }
        } else {
            for (uint32_t i = 0; i < flags->m_size; ++i) {
                m_goalFlags->AddInteger(flags->GetElement(i), 0);
                ++m_numFlags;
            }
        }
    }
    m_params->AddInteger(kNumFlags, m_numFlags);
}

// 0x00556240
bool CGoal::AllSkatersRunDone()
{
    GameNet::Manager* net = NetManager();
    PlayerSearch search;
    for (void* player = net->FirstPlayerInfo(&search, false); player; player = net->NextPlayerInfo(&search, false)) {
        Obj::CCompositeObject* skater = PlayerSkater(player);
        auto* endRun = static_cast<Obj::CSkaterEndRunComponent*>(skater->GetComponent(kSkaterEndRun));
        // (in horse, a skater not taking the turn doesn't count)
        if (GameModeName() == kHorse && (reinterpret_cast<uint8_t*>(skater)[0x320] ||
                                         (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(skater) + 0x104) >> 2) & 1))
            continue;
        bool done;
        if (m_endRunType == 0)
            done = endRun->GoalRunDone();
        else if (m_endRunType == 1)
            done = endRun->RunDone();
        else
            continue;
        if (!done)
            return false;
    }
    return true;
}

} // namespace Game

REPLACE(0x00555DF0, &Game::CGoal::GetRewardsStructure);
REPLACE(0x00555FB0, &Game::CGoal::UnlockRewardGoals);
REPLACE(0x00556050, &Game::CGoal::GetRewardPoints);
REPLACE(0x005560D0, &Game::CGoal::DestroyNode);
REPLACE(0x00556110, &Game::CGoal::DestroyBody);
REPLACE(0x00556240, &Game::CGoal::AllSkatersRunDone);
REPLACE(0x00556330, &Game::CGoal::SetFlag);
REPLACE(0x005566A0, &Game::CGoal::Destroy);
REPLACE(0x005566D0, &Game::CGoal::CreateGoalFlags);
REPLACE(0x00556F90, &Game::CGoal::AppendDifficultyLevelParams);
REPLACE(0x00556FE0, &Game::CGoal::Construct);
REPLACE(0x00557270, &Game::CGoal::Init);
REPLACE(0x00557360, &Game::CGoal::Activate);
REPLACE(0x00557560, &Game::CGoal::Deactivate);
REPLACE(0x005576D0, &Game::CGoal::Win);
REPLACE(0x00557A40, &Game::CGoal::Unlock);
