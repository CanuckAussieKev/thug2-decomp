// GoalSystem::CGoalManager's own methods: 0x00557E30-0x005591BF, its construction and its goal table and active list
// (0x0055B990-0x0055BF3F, 0x0055D960-0x0055DD02); see goal_manager.h.

#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/goals/goal.h"
#include "game/goals/goal_names.h"
#include "game/goals/goal_types.h"
#include "game/lst/head.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module_calls.h"
#include "game/skate/trick_goals.h"
#include "game/obj/trick_objects.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/core/code_address.h"

namespace GoalSystem {

namespace {

using namespace GoalNames;

constexpr uint32_t kGoalManagerShowGoalPoints = 0x888EF05E; // GoalManager_ShowGoalPoints
constexpr uint32_t kGoalManagerParams = 0x23D4170A;         // GoalManager_Params
constexpr uint32_t kDifficultyLevels = 0xB13D98D3;          // difficulty_levels
constexpr uint32_t kGoalsClassic = 0xE8E7DBAD;              // goals_classic
constexpr uint32_t kAlwaysInitializeGoal = 0x4E863E93;      // always_initialize_goal
constexpr uint32_t kGoalCreateProsetGeom = 0xB3108A05;      // goal_create_proset_geom
constexpr uint32_t kName = 0xA1DC81F9;                      // Name
constexpr uint32_t kVO = 0x1A4E4FB9;                        // VO
constexpr uint32_t kType = 0x7321A8D6;                      // type
constexpr uint32_t kTrick = 0x270F56E1;                     // trick (the component)
constexpr uint32_t kCreateGeometry = 0xF4EB5C60;            // create_geometry
constexpr uint32_t kTrickObjects = 0xEFC35592;              // TrickObjects

constexpr uint8_t kMsgGoalWon = 0x6E;      // to the server: a goal (the chain's first) won
constexpr uint8_t kGroupGoals = 8;         // (its message group)

static VtableSlot kGoalListHeadVtable_slots[] = {&GoalSystem::GoalListHead::Destroy};
void** const kGoalListHeadVtable = reinterpret_cast<void**>(kGoalListHeadVtable_slots);

const char* const kSpecialGap = "special_gap"; // "special_gap"

OnlinePlay::Manager* NetManager() { return OnlinePlay::g_gameNetManager; }
CMatchRules* GameMode() { return g_gameMode; }

void RemoveObjectiveNpc(CObjectiveNpc* ped) { ped->RemoveObjectiveNpc(); }
void HideGoalPed(CObjectiveNpc* ped, bool hide) { ped->Hide(hide); }

// The first goal of the chain `goal` is in (its parents' first links, up to one without a parent): its id - `id`'s
// if it has no parent. (A parent that isn't in the table is read through null, as the original.)
template <typename F>
CObjective* WalkToRoot(CGoalManager* manager, CObjective* goal, F&& onParent)
{
    for (GoalLink* link = goal->m_parents; link && link->m_objectiveKey;) {
        onParent(link->m_objectiveKey);
        goal = manager->m_goals.Get(link->m_objectiveKey);
        link = goal->m_parents;
    }
    return goal;
}

bool HasNoParent(CObjective* goal) { return goal->m_parents->m_objectiveKey == 0; }

} // namespace

// 0x0055B9F0
void GoalListHead::DestroyBody() { m_listLink.UnlinkAs(kGoalNodeVtable); }

// 0x0055D960
GoalListHead* GoalListHead::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

// 0x0055D9C0
CGoalManager* CGoalManager::Construct()
{
    m_goals.m_count = 0;
    m_goals.m_first = nullptr;
    m_goals.m_cursor = nullptr;
    MemSys::ListNode& head = m_activeGoals.m_listLink;
    head.data = reinterpret_cast<void*>(-1);
    head.priority = 0;
    head.next = &head;
    head.prev = &head;
    head.vtable = kGoalListHeadVtable;
    m_previousObjective = 0;
    m_taggingStyle = 0;
    m_objectiveScore = 0;
    m_totalGoalPoints = 0;
    m_goalsBeaten = 0;
    m_allUnlocked = false;
    m_startAllowed = true;
    m_backer = 0;
    for (Scripts::CStruct** structure : {&m_team, &m_goalData, &m_params}) {
        Scripts::CStruct* made = Scripts::NewStruct();
        *structure = made ? made->Init() : nullptr;
    }
    for (int& unlocked : m_groupUnlocked)
        unlocked = 0;
    return this;
}

// 0x0055DB50
void CGoalManager::DestroyBody()
{
    WipeObjectives();
    if (Scripts::CStruct* data = m_goalData) {
        data->Clear();
        Scripts::DeleteStruct(data);
    }
    reinterpret_cast<GameObjects::TrickObjectHead*>(&m_activeGoals)->UnlinkAll();
    m_activeGoals.m_listLink.UnlinkAs(kGoalNodeVtable);
    reinterpret_cast<GameObjects::ProfileTable*>(&m_goals)->Clear();
}

// 0x00557E30
bool CGoalManager::MayBeginObjective() { return m_startAllowed; }

// 0x00557E40
void CGoalManager::ShowGoalPoints()
{
    ++m_objectiveScore;
    ++m_totalGoalPoints;
    Scripts::LaunchScript(kGoalManagerShowGoalPoints, nullptr, nullptr, false, nullptr);
}

// 0x00557E70
void CGoalManager::ResetGoalData(uint32_t flag)
{
    if (flag)
        m_params->RemoveComponent(flag);
    else
        m_params->Clear();
    Scripts::CComponent* component = m_goalData->StepToEntry(nullptr);
    while (component) {
        Scripts::CComponent* next = m_goalData->StepToEntry(component);
        if (!flag) {
            m_goalData->RemoveComponent(component->name);
        } else if (component->name == kGoalManagerParams) {
            component->structure->Clear();
            component->structure->PutSubStruct(kDifficultyLevels, m_params);
        } else if (component->structure && component->structure->HasBareFlag(flag)) {
            m_goalData->RemoveComponent(component->name);
        }
        component = next;
    }
    m_objectiveScore = 0;
    m_totalGoalPoints = 0;
    m_goalsBeaten = 0;
    m_allUnlocked = false;
}

// 0x00557F90
CObjective* CGoalManager::GetGoal(uint32_t id, int) { return m_goals.Get(id); }

// 0x00557FB0
CObjective* CGoalManager::GetObjectiveAt(int index) { return m_goals.GetByIndex(index); }

// 0x00557FC0
bool CGoalManager::EngageObjective(uint32_t id, bool)
{
    CObjective* goal = m_goals.Get(id);
    if (!goal || goal->IsLocked())
        return false;
    bool started = Virtual<bool>(goal, CObjective::kVtActivate);
    if (started && Virtual<bool>(goal, CObjective::kVtCanRetry) && HasNoParent(goal))
        m_previousObjective = id;
    return started;
}

// 0x00558030
bool CGoalManager::CompleteObjective(uint32_t id)
{
    OnlinePlay::Manager* net = NetManager();
    CObjective* goal = m_goals.Get(id);
    if (!goal)
        return false;
    uint32_t root = id;
    WalkToRoot(this, goal, [&](uint32_t parent) { root = parent; });
    if (root == m_previousObjective && goal->m_subNodes->m_objectiveKey == 0)
        m_previousObjective = 0;
    if (!Virtual<bool>(goal, CObjective::kVtHasWon)) {
        if (goal->m_subNodes->m_objectiveKey == 0 && net->InNetGame()) {
            NetLink::NetMessageInfo desc;
            desc.Construct();
            NetLink::App* client = net->GetClient(0);
            // The message: the game's id and the chain's first goal (the id's three padding bytes are left as the
            // original's stack had them: the network manager's address).
            struct {
                uint32_t gameId;
                uint32_t goal;
            } message = {reinterpret_cast<uint32_t>(net), 0};
            message.gameId = (message.gameId & ~0xFFu) | net->GetGameId();
            message.goal = goal->GetTopObjectiveKey();
            desc.m_byteCount = sizeof(message);
            desc.m_groupId = kGroupGoals;
            desc.m_data = &message;
            desc.m_id = kMsgGoalWon;
            desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
            static_cast<NetLink::Client*>(client)->SendToHost(&desc);
        } else if (goal->m_subNodes->m_objectiveKey == 0 && !goal->GetParams()->HasBareFlag(kEditedGoal)) {
            ++m_goalsBeaten;
        }
    }
    Virtual<bool>(goal, CObjective::kVtWin);
    return true;
}

// 0x00558180
bool CGoalManager::FailObjective(uint32_t id)
{
    CObjective* goal = m_goals.Get(id);
    if (!goal)
        return false;
    Virtual<bool>(goal, CObjective::kVtLose);
    if (GameMode()->GetNameKey() == kClassic ||
        (GameMode()->GetNameKey() == kNetGoalAttack && GameMode()->GetGoalsMode() == kGoalsClassic))
        EngageObjective(id, false);
    return true;
}

// 0x00558200
int CGoalManager::CountPickedObjectives()
{
    int selected = 0;
    for (int i = 0; i < m_goals.m_count; ++i)
        if (m_goals.GetByIndex(i)->IsSelected())
            ++selected;
    return selected;
}

// 0x00558250
void CGoalManager::DisengageEveryObjective(bool unused)
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (Virtual<bool>(goal, CObjective::kVtIsActive))
            Virtual<bool>(goal, CObjective::kVtDeactivate, unused, true);
    }
}

// 0x005582B0
void CGoalManager::ActivateAllGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CObjective::kVtIsActive) && !Virtual<bool>(goal, CObjective::kVtHasWon) && !goal->IsOnlineObjective())
            Virtual<bool>(goal, CObjective::kVtActivate);
    }
}

// 0x00558320
void CGoalManager::ReleaseObjectiveTree(uint32_t id)
{
    CObjective* root = WalkToRoot(this, m_goals.Get(id), [](uint32_t) {});
    root->Uninitialize(true, 0);
}

// 0x00558380
void CGoalManager::ReleaseEveryObjective()
{
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->Uninitialize(false, 0);
}

// 0x005583C0
void CGoalManager::PrepareEveryObjective()
{
    OnlinePlay::Manager* net = NetManager();
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (net->InNetGame() && !goal->IsOnlineObjective())
            continue;
        goal->MergeSkillTierArgs();
        if (goal->IsLocked())
            continue;
        if (Virtual<bool>(goal, CObjective::kVtHasWon) && !goal->GetParams()->HasBareFlag(kAlwaysInitializeGoal))
            continue;
        GoalLink* parents = goal->m_parents;
        Scripts::CStruct* params = goal->GetParams();
        if (parents->m_objectiveKey)
            continue;
        if (GameMode()->GetNameKey() == kClassic || !Virtual<bool>(goal, CObjective::kVtHasWon) ||
            params->HasBareFlag(kAlwaysInitializeGoal) || params->HasBareFlag(kEditedGoal))
            goal->Init();
    }
}

// 0x005584C0
void CGoalManager::PreparePickedObjectives()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (!goal->IsSelected())
            continue;
        Scripts::LaunchScript(kGoalCreateProsetGeom, goal->GetParams(), nullptr, false, nullptr);
        goal->Init();
    }
}

// 0x00558530
void CGoalManager::ActivateAllSelectedGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (goal->IsSelected())
            Virtual<bool>(goal, CObjective::kVtActivate);
    }
}

// 0x00558580
void CGoalManager::UnpickEveryObjective()
{
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->Deselect();
}

// 0x005585C0
void CGoalManager::FreezeEveryObjective()
{
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->Pause();
}

// 0x00558600
void CGoalManager::ThawEveryObjective()
{
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->UnPause();
}

// 0x00558640
bool CGoalManager::BeginObjectiveAtOnce(uint32_t id, bool checkExists)
{
    CObjective* goal = m_goals.Get(id);
    if (!goal && checkExists)
        return false;
    goal->SetQuickStart(); // (no such goal and not checkExists: written through null, as the original)
    bool started = Virtual<bool>(goal, CObjective::kVtActivate);
    if (started && Virtual<bool>(goal, CObjective::kVtCanRetry))
        m_previousObjective = id;
    goal->UnsetQuickStart();
    return started;
}

// 0x005586A0
bool CGoalManager::RaiseBit(uint32_t id, uint32_t flag, int value, uint32_t sub)
{
    CObjective* goal = m_goals.Get(id);
    if (!goal)
        return false;
    return Virtual<bool>(goal, CObjective::kVtSetFlag, flag, value, sub);
}

// 0x005586E0
bool CGoalManager::RegisterGapHit(uint32_t gap)
{
    bool got = false;
    for (int i = 0; i < m_goals.m_count; ++i)
        got = m_goals.GetByIndex(i)->RegisterGapHit(gap);
    return got;
}

// 0x00558730
Scripts::CStruct* CGoalManager::GetObjectiveArgs(uint32_t id) { return m_goals.Get(id)->GetParams(); }

// 0x00558750
bool CGoalManager::NextWaypoint(uint32_t id) { return m_goals.Get(id)->NextWaypoint(); }

// 0x00558770
void CGoalManager::KillCluster(uint32_t cluster, int score)
{
    int count = m_goals.m_count; // (read once)
    for (int i = 0; i < count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CObjective::kVtIsActive) || goal->IsOnlineObjective())
            continue;
        if (goal->IsGraffiti())
            goal->KillCluster(cluster, score);
        else
            Stubs::IgnoreArgument(static_cast<int>(cluster));
    }
}

// 0x005587F0
void CGoalManager::CheckTrickGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CObjective::kVtIsActive) || goal->IsOnlineObjective())
            continue;
        Scripts::CStruct* params = goal->GetParams();
        if (goal->IsSpecial() || params->HasBareFlag(kCreateATrick)) {
            if (!params->HasBareFlag(kSpecialGap) && goal->DidTrickString())
                CompleteObjective(goal->GetObjectiveKey());
        } else if (goal->IsTetris()) {
            reinterpret_cast<CTetrisGoal*>(goal)->CheckTricks();
        } else if (goal->IsFindGaps()) {
            reinterpret_cast<CHuntGapsObjective*>(goal)->ScanForGaps();
        }
    }
}

// 0x005588D0
void CGoalManager::OpenUpObjective(uint32_t id) { m_goals.Get(id)->Unlock(); }

// 0x005588F0
int CGoalManager::HasBeatenGoal(uint32_t id)
{
    if (CObjective* goal = m_goals.Get(id))
        return Virtual<bool>(goal, CObjective::kVtHasWon);
    int beaten = 0;
    Scripts::CStruct* data;
    if (m_goalData->ReadSubStruct(id, &data, 0))
        data->FetchIntValue(kHasBeaten, &beaten, 1);
    return beaten != 0;
}

// 0x00558960
void CGoalManager::FlipObjectivePick(uint32_t id)
{
    CObjective* goal = m_goals.Get(id);
    if (goal->IsSelected())
        goal->Deselect();
    else
        goal->Select();
}

// 0x005589A0
bool CGoalManager::HasWonProsetGoal(const char* prefix)
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (Virtual<bool>(goal, CObjective::kVtHasWon) && goal->HasProsetPrefix(prefix))
            return true;
    }
    return false;
}

// 0x00558A00
void CGoalManager::RestoreGoalData()
{
    Scripts::CComponent* component = m_goalData->StepToEntry(nullptr);
    while (component) {
        Scripts::CComponent* next = m_goalData->StepToEntry(component);
        if (component->name == kGoalManagerParams) {
            Scripts::CStruct* levels;
            component->structure->ReadSubStruct(kDifficultyLevels, &levels, 1);
            m_params->Clear();
            m_params->AppendStructureIfAny(levels);
        } else if (CObjective* goal = m_goals.Get(component->name)) {
            Virtual<void>(goal, CObjective::kVtLoadSaveData, component->structure);
        }
        component = next;
    }
}

// 0x00558AB0
int CGoalManager::CountClearedByPlayer(int bit)
{
    int count = 0;
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (HasNoParent(goal) && Virtual<bool>(goal, CObjective::kVtStageFlagSet, bit))
            ++count;
    }
    return count;
}

// 0x00558B10
int CGoalManager::NumGoalsNetBeatenBy(int bit)
{
    int count = 0;
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (HasNoParent(goal) && Virtual<bool>(goal, CObjective::kVtNetStageFlagSet, bit))
            ++count;
    }
    return count;
}

// 0x00558B70
int CGoalManager::CountObjectivesCleared()
{
    int count = 0;
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (HasNoParent(goal) && Virtual<bool>(goal, CObjective::kVtHasWon))
            ++count;
    }
    return count;
}

// 0x00558BC0
void CGoalManager::OpenUpEveryObjective()
{
    m_allUnlocked = true;
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->MarkViewed();
}

// 0x00558C00
void CGoalManager::UnlockAllProGoals()
{
    m_allUnlocked = true;
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (goal->IsProChallenge() && goal->IsLocked())
            goal->Unlock();
    }
}

// 0x00558C70
void CGoalManager::PlayObjectiveVoiceLine(Scripts::CStruct* params)
{
    uint32_t name;
    params->ReadHashValue(kName, &name, 1);
    CObjective* goal = m_goals.Get(name);
    uint32_t stream = 0;
    params->ReadHashValue(kVO, &stream, 0);
    if (stream) {
        goal->m_ped->PlayStream(stream, true, 0, false);
        return;
    }
    const char* type = nullptr;
    if (params->GetString(kType, &type, 0))
        goal->m_ped->PlayObjectiveVoiceLine(type, params);
}

// 0x00558D20
int CGoalManager::IsGoalLocked(uint32_t id)
{
    if (CObjective* goal = m_goals.Get(id))
        return goal->IsLocked();
    int locked = 0;
    Scripts::CStruct* data;
    if (m_goalData->ReadSubStruct(id, &data, 0))
        data->FetchIntValue(kIsLocked, &locked, 1);
    return locked != 0;
}

// 0x00558D90
CObjective* CGoalManager::GetActiveCompetition()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (goal->IsContestObjective() && Virtual<bool>(goal, CObjective::kVtIsActive))
            return goal;
    }
    return nullptr;
}

// 0x00558DF0
void CGoalManager::ReplaceTrickTextAll()
{
    int count = m_goals.m_count; // (read once)
    for (int i = 0; i < count; ++i)
        m_goals.GetByIndex(i)->SwapMoveCaption();
}

// 0x00558E30
bool CGoalManager::GoalExists(uint32_t id) { return m_goals.Get(id) != nullptr; }

// 0x00558E50
void CGoalManager::TestTaggedProps()
{
    auto* skater = static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider());
    int player = *reinterpret_cast<int*>(static_cast<uint8_t*>(skater->GetPointsTotal()) + 0x20);
    auto* trick = reinterpret_cast<uint8_t*>(skater->GetComponent(kTrick));
    // The combo's trick objects: up to 512 clusters, then their count (copied first, as the original).
    struct TrickObjects {
        uint32_t clusters[512];
        int count;
    };
    TrickObjects objects;
    std::memcpy(&objects, trick + 0x13B4, sizeof(objects));
    int count = objects.count > 512 ? 512 : objects.count;
    for (int i = 0; i < count; ++i)
        KillCluster(objects.clusters[i], player);
}

// 0x00558ED0
void CGoalManager::BuildGoalTree()
{
    int count = m_goals.m_count; // (read once)
    for (int i = 0; i < count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        uint32_t id = goal->GetObjectiveKey();
        for (GoalLink* link = goal->m_subNodes; link && link->m_objectiveKey; link = link->m_next) {
            CObjective* child = m_goals.Get(link->m_objectiveKey); // (one not in the table is read through null)
            bool linked = false;
            for (GoalLink* parent = child->m_parents; parent && parent->m_objectiveKey; parent = parent->m_next) {
                if (parent->m_objectiveKey == id) {
                    linked = true;
                    break;
                }
            }
            if (!linked)
                child->AttachUpperLink(id);
        }
    }
    for (int i = 0; i < count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (HasNoParent(goal))
            goal->PassInheritedFlags(0);
    }
}

// 0x00558FD0
void CGoalManager::SpawnObjectiveProps()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        Scripts::CArray* nodes;
        if (!m_goals.GetByIndex(i)->GetParams()->GetArray(kCreateGeometry, &nodes, 0))
            continue;
        for (uint32_t n = 0; n < nodes->m_size; ++n)
            ScriptCommands::CreateFromNodeName(nodes->GetElement(n));
    }
}

// 0x00559050
int CGoalManager::GetSkillTier(bool career)
{
    uint32_t mode = career ? kCareer : GameMode()->GetNameKey();
    int level;
    if (!m_params->FetchIntValue(mode, &level, 0))
        level = 1;
    switch (level) {
    case 0: return 0;
    case 2: return 2;
    default: return 1;
    }
}

// 0x005590C0
void CGoalManager::ConcealEveryObjectiveNpc(bool hide)
{
    for (int i = 0; i < m_goals.m_count; ++i)
        HideGoalPed(m_goals.GetByIndex(i)->m_ped, hide);
}

// 0x00559110
bool CGoalManager::IsGroupLocked(int group, int level)
{
    if (group == -1)
        return false;
    if (level == -1)
        level = SkateCalls::Career()->GetLevel();
    return m_groupUnlocked[level * kGroupsPerLevel + group - 1] == 0;
}

// 0x00559150
void CGoalManager::TrickObjectHit(uint32_t cluster)
{
    int count = m_goals.m_count; // (read once)
    for (int i = 0; i < count; ++i) {
        CObjective* goal = m_goals.GetByIndex(i);
        if (goal->GetParams()->HasBareFlag(kTrickObjects))
            reinterpret_cast<CTrickObjectsGoal*>(goal)->ObjectHit(cluster);
    }
}

// 0x0055BEA0
bool CGoalManager::DropObjective(uint32_t id)
{
    CObjective* goal = m_goals.Get(id);
    RemoveObjectiveNpc(goal->m_ped);
    m_goals.Remove(id);
    if (m_previousObjective == id)
        m_previousObjective = 0;
    Virtual<CObjective*>(goal, CObjective::kVtDestroy, static_cast<uint8_t>(1));
    return true;
}

// 0x0055BEF0
void CGoalManager::WipeObjectives()
{
    m_previousObjective = 0;
    for (int i = 0; i < m_goals.m_count; ++i)
        RemoveObjectiveNpc(m_goals.GetByIndex(i)->m_ped);
    ListKit::TableItemEraser destroyer = {&m_goals};
    destroyer.EmptyTableItems();
}

// 0x0055DBE0
void CGoalManager::AddActiveGoal(CObjective* goal)
{
    reinterpret_cast<ListKit::Head*>(&m_activeGoals)->AddNode(&goal->m_listLink);
}

namespace {

// The active list's nodes (the walk checks each node's data for -1 before the node for null, as the original).
int CountActive(MemSys::ListNode* head)
{
    MemSys::ListNode* node = head->next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return 0;
    int count = 0;
    do {
        node = node->next;
        ++count;
    } while (node->data != reinterpret_cast<void*>(-1) && node);
    return count;
}

} // namespace

// 0x0055D990
MemSys::ListNode* GoalListHead::NodeAt(int index)
{
    MemSys::ListNode* node = m_listLink.next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return nullptr;
    while (index--) {
        node = node->next;
        if (node->data == reinterpret_cast<void*>(-1) || !node)
            return nullptr;
    }
    return node;
}

// 0x0055DBF0
CObjective* CGoalManager::GetEngagedObjective(int index)
{
    if (!CountActive(&m_activeGoals.m_listLink))
        return nullptr;
    return static_cast<CObjective*>(m_activeGoals.NodeAt(index)->data);
}

// 0x0055DC30
int CGoalManager::NumActiveGoals(bool all)
{
    int counted = 0;
    int count = CountActive(&m_activeGoals.m_listLink);
    for (int i = 0; i < count; ++i) {
        CObjective* goal = CountActive(&m_activeGoals.m_listLink)
                          ? static_cast<CObjective*>(m_activeGoals.NodeAt(i)->data)
                          : nullptr;
        bool counts = goal->IsOnlineObjective() ? all : all || Virtual<bool>(goal, CObjective::kVtIsActive2);
        if (counts)
            ++counted;
    }
    return counted;
}

// 0x005591B0
CGoalManager* GetObjectiveBoard()
{
    return reinterpret_cast<GameModules::Skate*>(SkateCalls::Skate())->GetObjectiveBoard();
}

} // namespace Game
