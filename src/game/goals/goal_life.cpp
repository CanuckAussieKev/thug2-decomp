// GoalSystem::CObjective's life: made, initialized, started, stopped, won, unlocked, rewarded, destroyed; its goal flags set
// (0x00555DF0-0x00556109, 0x00556110-0x0055632F, 0x005566A0-0x005568AF, 0x00556F90-0x00557ABF; see objective.h).

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/goals/objective.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/end_run.h"
#include "game/skate/game_mode.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/core/stubs.h"
#include "game/gfx/weather.h"
#include "game/core/code_address.h"

namespace GoalSystem {

float g_goalLifeSecondsPerMillisecond{0.001f};

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

static VtableSlot kGoalVtable_slots[] = {&GoalSystem::CObjective::Teardown, &GoalSystem::CObjective::WantsToEnd, &GoalSystem::CObjective::HasRunOut, &GoalSystem::CObjective::IsOperative,
                 Engine3D::VirtualSlot3Jump, &GoalSystem::CObjective::Expire, &GoalSystem::CObjective::RestoreSaveBlock, &GoalSystem::CObjective::CaptureSaveBlock,
                 &GoalSystem::CObjective::HasTimeLimit, Stubs::ReturnTrue, &GoalSystem::CObjective::HasComponentA302D7CD,
                 &GoalSystem::CObjective::Activate, &GoalSystem::CObjective::ChangeLive, &GoalSystem::CObjective::Disengage, &GoalSystem::CObjective::ClearEngagedBit,
                 &GoalSystem::CObjective::Win, &GoalSystem::CObjective::Lose, &GoalSystem::CObjective::TickFrame, &GoalSystem::CObjective::IsFrozen,
                 &GoalSystem::CObjective::SetFlag, &GoalSystem::CObjective::StageFlagSet, &GoalSystem::CObjective::HasWon,
                 &GoalSystem::CObjective::NetStageFlagSet, &GoalSystem::CObjective::HasWonGoalOrChild, &GoalSystem::CObjective::IsPrepared,
                 &GoalSystem::CObjective::MayTryAgain, &GoalSystem::CObjective::MarkWon, &GoalSystem::CObjective::SetStageFlag,
                 &GoalSystem::CObjective::SetNetStageFlag, &GoalSystem::CObjective::UnBeat};
void** const kGoalVtable = reinterpret_cast<void**>(kGoalVtable_slots);

// The x87 code's float to int conversion (_ftol): truncation.
int Truncate(double value) { return static_cast<int>(value); }

OnlinePlay::Hub* NetManager() { return OnlinePlay::g_gameNetManager; }
uint32_t GameModeName() { return static_cast<GoalSystem::CMatchRules*>(SkateCalls::GameMode())->GetNameKey(); }

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
GoalLink* AllocChainLink()
{
    auto* link = static_cast<GoalLink*>(MemSys::Alloc(sizeof(GoalLink)));
    if (link) {
        link->m_objectiveKey = 0;
        link->m_succ = nullptr;
        link->m_won = false;
    }
    return link;
}
// A goal's list node out of its list (linked to itself).
void Uncouple(MemSys::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

// The goal's pro (GoalSystem::CObjectiveNpc, 0x14 bytes): 0x0055EB40 the constructor (its goal's parameters); its destructor
// is empty (0x005480A0).
CObjectiveNpc* NewGoalPed(Scripts::CStruct* params)
{
    void* memory = MemSys::Alloc(0x14);
    return memory ? static_cast<CObjectiveNpc*>(memory)->MakeNew(params) : nullptr;
}
inline void DestroyGoalPedBody(CObjectiveNpc*) {}

// ListKit::Hunt<PlayerRecord> (vtable 0x0064C1A8) for the network manager's BeginPlayerWalk / StepPlayerWalk.
struct PlayerSearch {
    void** m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};
GameObjects::CGameEntity* PlayerSkater(void* player)
{
    return *reinterpret_cast<GameObjects::CGameEntity**>(static_cast<uint8_t*>(player) + 0x14);
}
// A skater's score (its SkaterScore component's +0x18: GameModules::PointTally) reset (0x00564A10) / its total (0x00561B40).
void* ScoreOf(GameObjects::CGameEntity* skater)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(skater->FetchModule(kSkaterScore)) + 0x18);
}
void ResetScore(void* score) { static_cast<GameModules::PointTally*>(score)->Reset(); }
int TotalScore(void* score) { return static_cast<GameModules::PointTally*>(score)->GetTotalScore(); }
// The root of a goal's parents (the last one whose first parent link has a goal), or null if it has none.
CObjective* RootOf(void* manager, GoalLink* parents)
{
    CObjective* root = nullptr;
    for (GoalLink* link = parents; link && link->m_objectiveKey; link = root->m_parents)
        root = GetGoal(manager, link->m_objectiveKey, 1);
    return root;
}

} // namespace

static VtableSlot kGoalNodeVtable_slots[] = {&GoalSystem::CObjective::DestroyNode};
void** const kGoalNodeVtable = reinterpret_cast<void**>(kGoalNodeVtable_slots); // (also used by goal_manager.cpp)
// 0x00556FE0
CObjective* CObjective::MakeNew(Scripts::CStruct* params)
{
    m_listLink.data = this;
    m_listLink.priority = 0;
    m_listLink.next = &m_listLink;
    m_listLink.prev = &m_listLink;
    m_listLink.vtable = kGoalVtable;
    m_stageFlags = 0;
    m_netStageFlags = 0;
    m_subNodes = AllocChainLink();
    m_parents = AllocChainLink();
    m_curPhase = static_cast<uint8_t>((m_curPhase & ~(kStateActive | kStatePaused)) | kStateNoTimeLimit);
    m_flagBits &= ~(kFlagSeen | kFlagWon | kFlagWonInNetGame);
    m_stageFlags = 0;
    m_netStageFlags = 0;
    m_flagBits = static_cast<uint8_t>((m_flagBits & ~(kFlagInitialized | kFlagSelected)) | kFlagDeactivateOnExpire);
    m_timesStarted = 0;
    m_secondsRemaining = 0;
    m_unknown68 = -1;
    m_unknown64 = -1;
    m_args = NewStructure();
    m_args->AppendStructureIfAny(params);
    m_ped = NewGoalPed(m_args);
    m_goalFlags = NewStructure();
    m_numFlags = 0;
    m_numFlagsSet = 0;
    m_elapsed = 0;
    m_bestTime = 0x7FFFFFFF;
    m_bestScore = 0;
    m_runEnding = 0;
    m_level = SkateCalls::Career()->FetchStage();
    m_bundle = -1;
    params->FetchIntValue(kGroupNumber, &m_bundle, 0);
    AllocObjectiveBits(params);
    if (params->HasBareFlag(kSingleTimer))
        m_inherited |= 2;
    if (params->HasBareFlag(kRetryStage))
        m_inherited |= 4;
    m_skater = kNone;
    params->ReadHashValue(kSkater, &m_skater, 0);
    if (params->HasBareFlag(kUnlockedByAnother) || params->HasBareFlag(kProGoal))
        m_flagBits |= kFlagLocked;
    else
        m_flagBits &= ~kFlagLocked;
    return this;
}

// 0x00556110
void CObjective::DestroyBody()
{
    m_listLink.vtable = kGoalVtable;
    uint32_t script;
    if (m_args->ReadHashValue(kDestroy, &script, 0))
        Scripts::LaunchScript(script, m_args, nullptr, false, nullptr);
    if (m_ped) {
        DestroyGoalPedBody(m_ped);
        MemSys::Dealloc(m_ped);
    }
    DeleteStructure(m_args);
    DeleteStructure(m_goalFlags);
    for (GoalLink* link = m_subNodes; link;) {
        GoalLink* next = link->m_succ;
        MemSys::Dealloc(link);
        link = next;
    }
    for (GoalLink* link = m_parents; link;) {
        GoalLink* next = link->m_succ;
        MemSys::Dealloc(link);
        link = next;
    }
    m_listLink.vtable = kGoalNodeVtable;
    Uncouple(&m_listLink);
}

// 0x005566A0
CObjective* CObjective::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x005560D0
CObjective* CObjective::DestroyNode(uint8_t flags)
{
    m_listLink.vtable = kGoalNodeVtable;
    Uncouple(&m_listLink);
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00556F90
void CObjective::MergeSkillTierArgs()
{
    Scripts::CArray* levels;
    if (!m_args->FetchList(kDifficultyLevelParams, &levels, 0))
        return;
    int level = GetSkillTier(ObjectiveBoard(), false);
    m_args->AppendStructureIfAny(reinterpret_cast<Scripts::CStruct*>(levels->FetchPart(static_cast<uint32_t>(level))));
    AllocObjectiveBits(m_args);
}

// 0x00557270
void CObjective::PrepareAll()
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_bottomUpHeap);
    // (MergeSkillTierArgs, inlined)
    Scripts::CArray* levels;
    if (m_args->FetchList(kDifficultyLevelParams, &levels, 0)) {
        int level = GetSkillTier(ObjectiveBoard(), false);
        m_args->AppendStructureIfAny(
            reinterpret_cast<Scripts::CStruct*>(levels->FetchPart(static_cast<uint32_t>(level))));
        AllocObjectiveBits(m_args);
    }
    uint32_t script;
    if (m_args->ReadHashValue(kInit, &script, 0))
        Scripts::LaunchScript(script, m_args, nullptr, false, nullptr);
    SwapMoveCaption();
    m_flagBits |= kFlagInitialized;
    int unlimited = 0;
    m_args->FetchIntValue(kUnlimitedTime, &unlimited, 0);
    if (unlimited)
        m_curPhase |= kStateNoTimeLimit;
    if (m_args->HasEntryNamed(kDontDeactivateOnExpiry))
        m_flagBits &= ~kFlagDeactivateOnExpire;
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00557360
bool CObjective::Activate()
{
    if (Virtual<bool>(this, kVtIsActive))
        return false;
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_bottomUpHeap);
    void* manager = ObjectiveBoard();
    ++m_timesStarted;
    m_elapsed = 0;
    OnlinePlay::Hub* net = NetManager();
    PlayerSearch search;
    if (m_args->HasBareFlag(kResetScore)) {
        for (void* player = net->BeginPlayerWalk(&search, false); player; player = net->StepPlayerWalk(&search, false))
            ResetScore(ScoreOf(PlayerSkater(player)));
    }
    // A child goal: the timer passed down (single_timer: its root's, in whole seconds), the root's state for it.
    CObjective* root = m_parents->m_objectiveKey ? RootOf(manager, m_parents) : nullptr;
    if (root) {
        if (m_inherited & 2)
            m_secondsRemaining = Truncate(root->m_secondsRemaining * static_cast<double>(g_goalLifeSecondsPerMillisecond)) * 1000;
        else
            ResetTimer();
        if (Virtual<bool>(root, kVtIsInitialized)) {
            root->Uninitialize(false, 0);
            if (Virtual<bool>(this, kVtIsInitialized)) {
                Uninitialize(false, 0);
                PrepareAll();
            }
        }
    } else {
        ResetTimer();
    }
    if (!Virtual<bool>(this, kVtIsInitialized))
        PrepareAll();
    m_flagBits &= ~kFlagEndOfRunStarted;
    m_args->RemoveFlag(kComboStarted);
    uint32_t script;
    if (m_args->ReadHashValue(kActivate, &script, 0))
        Scripts::LaunchScript(script, m_args, nullptr, false, nullptr);
    if (!net->InNetGame())
        m_flagBits |= kFlagSeen;
    Virtual<void>(this, kVtSetActive);
    AddActiveGoal(manager, this);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return true;
}

// 0x00557560
bool CObjective::Disengage(bool, bool resetTree)
{
    if (!Virtual<bool>(this, kVtIsActive))
        return false;
    if (m_args->HasBareFlag(kSpecial))
        m_args->EraseModule(kKeyCombos);
    Virtual<void>(this, kVtSetInactive);
    if (!m_args->HasBareFlag(kSaveGoalFlags))
        ZeroObjectiveBits();
    ClearChildWins();
    uint32_t script;
    if (m_args->ReadHashValue(kDeactivate, &script, 0))
        Scripts::LaunchScript(script, m_args, nullptr, false, nullptr);
    if (!m_args->HasBareFlag(kQuickStart))
        m_args->RemoveFlag(kTestingGoal);
    if (!m_args->HasBareFlag(kJustWonGoal) && !m_args->HasBareFlag(kQuickStart)) {
        Scripts::LaunchScript(kReadySkaterForEarlyEnd, m_args, nullptr, false, nullptr);
        if (resetTree && m_parents && m_parents->m_objectiveKey) {
            Uninitialize(false, 0);
            void* manager = ObjectiveBoard();
            CObjective* root = RootOf(manager, m_parents);
            if (root && !Virtual<bool>(root, kVtHasWon))
                root->PrepareAll();
        }
    }
    Uncouple(&m_listLink);
    return true;
}

// 0x005576D0
bool CObjective::Win()
{
    m_args->PutInt(kWinRecord, 0);
    GoalLink* children = m_subNodes;
    if (children->m_objectiveKey) {
        // A goal of stages: the next one started.
        void* manager = ObjectiveBoard();
        Virtual<bool>(this, kVtDeactivate, false, true);
        Uninitialize(false, 0);
        GetGoal(manager, children->m_objectiveKey, 1)->PrepareAll();
        EngageObjective(manager, children->m_objectiveKey, false);
        children->m_won = true;
        return true;
    }
    m_args->PutNameKey(kNoName, kJustWonGoal);
    OnlinePlay::Hub* net = NetManager();
    bool newRecord = false;
    uint32_t type;
    if (!net->InNetGame() && m_args->ReadHashValue(kRecordType, &type, 0)) {
        int record = 0;
        bool better = false;
        if (type == kScore) {
            int custom;
            if (m_args->FetchIntValue(kCustomScoreRecord, &custom, 0)) {
                record = custom;
                better = record > m_bestScore;
            } else {
                void* score = ScoreOf(SkateCalls::GetHomeRider());
                record = m_args->HasBareFlag(kHighCombo) ? *reinterpret_cast<int*>(static_cast<uint8_t*>(score) + 0x20)
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
            m_args->PutInt(kWinRecord, record);
            newRecord = true;
        }
    }
    bool rewarded = false;
    if (!Virtual<bool>(this, kVtHasWon) && !net->InNetGame()) {
        Scripts::CStruct* rewards = NewStructure();
        GetPrizeStruct(rewards);
        if (newRecord)
            rewards->PutNameKey(kNoName, kNewRecord);
        m_args->AppendStructRef(kRewardParams, rewards);
        OpenUpPrizeObjectives();
        rewarded = true;
    } else {
        m_args->EraseModule(kRewardParams);
    }
    if (newRecord)
        m_args->PutNameKey(kNoName, kNewRecord);
    else
        m_args->RemoveFlag(kNewRecord);
    uint32_t script;
    if (m_args->ReadHashValue(kSuccess, &script, 0))
        Scripts::LaunchScript(script, m_args, nullptr, false, nullptr);
    bool testing = m_args->HasBareFlag(kTestingGoal);
    if (Virtual<bool>(this, kVtIsActive))
        Virtual<bool>(this, kVtDeactivate, false, true);
    if (!testing) {
        Virtual<void>(this, kVtMarkWon);
        if (rewarded) {
            GameObjects::CRiderStoryProgress* career = SkateCalls::Career();
            if (SkateCalls::GameModeIsTrue(kIsCareer)) {
                career->DepositMoney(GetRewardPoints());
                Scripts::CStruct* params = NewStructure();
                params->PutInt(kRewardPointsName, GetRewardPoints());
                Scripts::LaunchScript(kCheckChapterAdvancement, params, nullptr, false, nullptr);
                DeleteStructure(params);
            } else if (SkateCalls::GameModeIsTrue(kIsClassic)) {
                career->IncrementCounter();
            }
        }
    }
    if (net->InNetGame())
        Uninitialize(false, 0);
    m_args->RemoveFlag(kJustWonGoal);
    return true;
}

// 0x00557A40
void CObjective::Unlock()
{
    if (!(m_flagBits & kFlagLocked))
        return;
    m_flagBits &= ~kFlagLocked;
    if (NetManager()->InNetGame())
        return;
    if (!Virtual<bool>(this, kVtHasWon) || GameModeName() == kClassic || m_args->HasBareFlag(kAlwaysInitializeGoal) ||
        m_args->HasBareFlag(kEditedGoal))
        PrepareAll();
    InvokeHookScript(kUnlockScript);
}

// 0x00555DF0
void CObjective::GetPrizeStruct(Scripts::CStruct* rewards)
{
    void* manager = ObjectiveBoard();
    OnlinePlay::Hub* net = NetManager();
    if (!m_args->HasBareFlag(kEditedGoal))
        ShowGoalPoints(manager);
    rewards->PutNameKey(kNoName, kRewardsGiven);
    if (net->InNetGame())
        return;
    // A special trick slot as the reward, while there's room for one.
    if (m_args->HasBareFlag(kRewardTrickslot) && !m_args->HasBareFlag(kCreateATrick)) {
        GameObjects::CRiderIdentity* profile = SkateCalls::GetProfileFromParams(nullptr);
        int slots = profile->GetMaxSpecials();
        if (slots <= Scripts::FetchIntValue(kMaxTrickslots)) {
            int remove = 0;
            m_args->FetchIntValue(kShouldRemoveTrick, &remove, 0);
            if (remove == 1) {
                m_args->PutInt(kShouldRemoveTrick, 0);
                m_args->PutNameKey(kNoName, kAlreadyAddedTrickslot);
                m_args->PutInt(kJustAddedTrickslot, 1);
            }
        }
    }
    Scripts::CStruct* all = Scripts::ReadSubStruct(kGoalRewards);
    uint32_t id = 0;
    m_args->ReadHashValue(kGoalIdName, &id, 1);
    Scripts::CStruct* own = nullptr;
    all->ReadSubStruct(id, &own, 0);
    if (own) {
        if (own->HasBareFlag(kRewardStat)) {
            own->EraseModule(kRewardStat);
            own->PutInt(kRewardStat, 1);
        }
        rewards->AppendStructureIfAny(own);
        return;
    }
    if (m_args->HasBareFlag(kRewardStat)) {
        rewards->PutInt(kRewardStat, 1);
        return;
    }
    int stat;
    if (m_args->FetchIntValue(kRewardStat, &stat, 0))
        rewards->PutInt(kRewardStat, stat);
}

// 0x00555FB0
void CObjective::OpenUpPrizeObjectives()
{
    void* manager = ObjectiveBoard();
    uint32_t goal = 0;
    if (m_args->ReadHashValue(kRewardGoal, &goal, 0)) {
        OpenUpObjective(manager, goal);
        return;
    }
    Scripts::CArray* goals;
    if (!m_args->FetchList(kRewardGoal, &goals, 0))
        return;
    int count = static_cast<int>(goals->m_extent);
    for (int i = 0; i < count; ++i) {
        uint32_t id = goals->FetchPart(static_cast<uint32_t>(i));
        if (id)
            OpenUpObjective(manager, id);
    }
}

// 0x00556050
int CObjective::GetRewardPoints()
{
    int points = 0;
    if (m_args->FetchIntValue(kRewardPoints, &points, 0))
        return points;
    Scripts::CArray* byLevel;
    if (m_args->FetchList(kRewardPoints, &byLevel, 0)) {
        int level = GetSkillTier(ObjectiveBoard(), false);
        if (level >= 0 && level < static_cast<int>(byLevel->m_extent))
            return static_cast<int>(byLevel->FetchPart(static_cast<uint32_t>(level)));
    }
    return 0;
}

// 0x00556330
bool CObjective::SetFlag(uint32_t flag, int value, uint32_t sub)
{
    // A silently activated goal (not won yet) takes flags while inactive, and is won by them.
    bool silent = m_args->HasBareFlag(kSilentActivation) && !Virtual<bool>(this, kVtHasWon);
    if (!Virtual<bool>(this, kVtIsActive) && !silent)
        return false;
    void* manager = ObjectiveBoard();
    if ((m_flagBits & kFlagLocked) || IsGroupLocked(manager, m_bundle, m_level))
        return false;
    Scripts::CStruct* flags = m_goalFlags;
    if (sub)
        m_goalFlags->ReadSubStruct(sub, &flags, 1);
    bool changed = false;
    if (flags->HasEntryNamed(flag)) {
        int old;
        flags->FetchIntValue(flag, &old, 1);
        flags->PutInt(flag, value);
        if (old != value) {
            bool counted;
            if (sub) {
                int set;
                flags->FetchIntValue(kNumFlagsSet, &set, 1);
                counted = value != 0;
                set += counted ? 1 : -1;
                flags->PutInt(kNumFlagsSet, set);
            } else {
                counted = value != 0;
                m_numFlagsSet += counted ? 1 : -1;
            }
            if (counted && !m_args->HasBareFlag(kNoFlagSound))
                Scripts::LaunchScript(kGoalGotFlagSound, nullptr, nullptr, false, nullptr);
            if (m_args->HasBareFlag(kUseHudCounter))
                Scripts::LaunchScript(kGoalUpdateCounter, m_args, nullptr, false, nullptr);
            m_args->PutInt(kNumFlagsSet, m_numFlagsSet);
        }
        changed = true;
    }
    if (!silent)
        return changed;
    if (m_args->HasBareFlag(kTrickObjects))
        return changed;
    if (HasEveryBit()) {
        void* goals = ObjectiveBoard();
        uint32_t id = GetObjectiveKey();
        CompleteObjective(goals, id);
    } else if (!sub && m_args->HasBareFlag(kGaps) && !m_args->HasBareFlag(kNoGapMessages)) {
        Scripts::CStruct* params = NewStructure();
        params->AppendStructureIfAny(m_args);
        Scripts::LaunchScript(kGoalGapsGotGap, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    return changed;
}

// 0x005566D0
void CObjective::AllocObjectiveBits(Scripts::CStruct* params)
{
    m_goalFlags->Clear();
    m_numFlags = 0;
    Scripts::CArray* flags;
    if (params->FetchList(kGoalFlags, &flags, 0)) {
        if (flags->m_kindTag == Scripts::QBVAL_STRUCT) {
            // One structure per skater: its own flags and counts.
            m_goalFlags->PutNameKey(kNoName, kSkaterFlags);
            int skaters = static_cast<int>(flags->m_extent);
            for (int i = 0; i < skaters; ++i) {
                auto* entry = reinterpret_cast<Scripts::CStruct*>(flags->FetchPart(static_cast<uint32_t>(i)));
                uint32_t skater;
                entry->ReadHashValue(kSkater, &skater, 1);
                Scripts::CStruct* own = NewStructure();
                Scripts::CArray* names;
                entry->FetchList(kGoalFlags, &names, 1);
                int count = static_cast<int>(names->m_extent);
                for (int k = 0; k < count; ++k)
                    own->PutInt(names->FetchPart(static_cast<uint32_t>(k)), 0);
                own->PutInt(kNumFlags, count);
                own->PutInt(kNumFlagsSet, 0);
                m_goalFlags->PutSubStruct(skater, own);
                DeleteStructure(own);
            }
        } else {
            for (uint32_t i = 0; i < flags->m_extent; ++i) {
                m_goalFlags->PutInt(flags->FetchPart(i), 0);
                ++m_numFlags;
            }
        }
    }
    m_args->PutInt(kNumFlags, m_numFlags);
}

// 0x00556240
bool CObjective::AllSkatersRunDone()
{
    OnlinePlay::Hub* net = NetManager();
    PlayerSearch search;
    for (void* player = net->BeginPlayerWalk(&search, false); player; player = net->StepPlayerWalk(&search, false)) {
        GameObjects::CGameEntity* skater = PlayerSkater(player);
        auto* endRun = static_cast<GameObjects::CRiderSessionEnd*>(skater->FetchModule(kSkaterEndRun));
        // (in horse, a skater not taking the turn doesn't count)
        if (GameModeName() == kHorse && (reinterpret_cast<uint8_t*>(skater)[0x320] ||
                                         (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(skater) + 0x104) >> 2) & 1))
            continue;
        bool done;
        if (m_runEnding == 0)
            done = endRun->GoalRunDone();
        else if (m_runEnding == 1)
            done = endRun->RunDone();
        else
            continue;
        if (!done)
            return false;
    }
    return true;
}

} // namespace Game
