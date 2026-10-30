// Game::CGoalManager's own methods: 0x00557E30-0x005591BF, its construction and its goal table and active list
// (0x0055B990-0x0055BF3F, 0x0055D960-0x0055DD02); see goal_manager.h.

#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"

#include <cstring>

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
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"

namespace Game {

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

void** const kGoalListHeadVtable = reinterpret_cast<void**>(0x0064DF78);
void** const kGoalNodeVtable = reinterpret_cast<void**>(0x0064DDCC);

const char* const kSpecialGap = GAME_TEXT(0x0064DEB8, "special_gap"); // "special_gap"

GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }
CGameMode* GameMode() { return g_gameMode; }

void DestroyGoalPed(CGoalPed* ped) { ped->DestroyGoalPed(); }
void HideGoalPed(CGoalPed* ped, bool hide) { ped->Hide(hide); }

// The first goal of the chain `goal` is in (its parents' first links, up to one without a parent): its id - `id`'s
// if it has no parent. (A parent that isn't in the table is read through null, as the original.)
template <typename F>
CGoal* WalkToRoot(CGoalManager* manager, CGoal* goal, F&& onParent)
{
    for (GoalLink* link = goal->m_parents; link && link->m_goalId;) {
        onParent(link->m_goalId);
        goal = manager->m_goals.Get(link->m_goalId);
        link = goal->m_parents;
    }
    return goal;
}

bool HasNoParent(CGoal* goal) { return goal->m_parents->m_goalId == 0; }

} // namespace

// 0x0055B9F0
void GoalListHead::DestroyBody() { m_node.UnlinkAs(kGoalNodeVtable); }

// 0x0055D960
GoalListHead* GoalListHead::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x0055D9C0
CGoalManager* CGoalManager::Construct()
{
    m_goals.m_count = 0;
    m_goals.m_first = nullptr;
    m_goals.m_cursor = nullptr;
    Mem::ListNode& head = m_activeGoals.m_node;
    head.data = reinterpret_cast<void*>(-1);
    head.priority = 0;
    head.next = &head;
    head.prev = &head;
    head.vtable = kGoalListHeadVtable;
    m_lastGoal = 0;
    m_graffitiMode = 0;
    m_goalPoints = 0;
    m_totalGoalPoints = 0;
    m_goalsBeaten = 0;
    m_allUnlocked = false;
    m_canStartGoal = true;
    m_sponsor = 0;
    for (Script::CStruct** structure : {&m_team, &m_goalData, &m_params}) {
        Script::CStruct* made = Script::NewStruct();
        *structure = made ? made->Init() : nullptr;
    }
    for (int& unlocked : m_groupUnlocked)
        unlocked = 0;
    return this;
}

// 0x0055DB50
void CGoalManager::DestroyBody()
{
    RemoveAllGoals();
    if (Script::CStruct* data = m_goalData) {
        data->Clear();
        Script::DeleteStruct(data);
    }
    reinterpret_cast<Obj::TrickObjectHead*>(&m_activeGoals)->UnlinkAll();
    m_activeGoals.m_node.UnlinkAs(kGoalNodeVtable);
    reinterpret_cast<Obj::ProfileTable*>(&m_goals)->Clear();
}

// 0x00557E30
bool CGoalManager::CanStartGoal() { return m_canStartGoal; }

// 0x00557E40
void CGoalManager::ShowGoalPoints()
{
    ++m_goalPoints;
    ++m_totalGoalPoints;
    Script::RunScript(kGoalManagerShowGoalPoints, nullptr, nullptr, false, nullptr);
}

// 0x00557E70
void CGoalManager::ResetGoalData(uint32_t flag)
{
    if (flag)
        m_params->RemoveComponent(flag);
    else
        m_params->Clear();
    Script::CComponent* component = m_goalData->GetNextComponent(nullptr);
    while (component) {
        Script::CComponent* next = m_goalData->GetNextComponent(component);
        if (!flag) {
            m_goalData->RemoveComponent(component->name);
        } else if (component->name == kGoalManagerParams) {
            component->structure->Clear();
            component->structure->AddStructure(kDifficultyLevels, m_params);
        } else if (component->structure && component->structure->ContainsFlag(flag)) {
            m_goalData->RemoveComponent(component->name);
        }
        component = next;
    }
    m_goalPoints = 0;
    m_totalGoalPoints = 0;
    m_goalsBeaten = 0;
    m_allUnlocked = false;
}

// 0x00557F90
CGoal* CGoalManager::GetGoal(uint32_t id, int) { return m_goals.Get(id); }

// 0x00557FB0
CGoal* CGoalManager::GetGoalByIndex(int index) { return m_goals.GetByIndex(index); }

// 0x00557FC0
bool CGoalManager::ActivateGoal(uint32_t id, bool)
{
    CGoal* goal = m_goals.Get(id);
    if (!goal || goal->IsLocked())
        return false;
    bool started = Virtual<bool>(goal, CGoal::kVtActivate);
    if (started && Virtual<bool>(goal, CGoal::kVtCanRetry) && HasNoParent(goal))
        m_lastGoal = id;
    return started;
}

// 0x00558030
bool CGoalManager::WinGoal(uint32_t id)
{
    GameNet::Manager* net = NetManager();
    CGoal* goal = m_goals.Get(id);
    if (!goal)
        return false;
    uint32_t root = id;
    WalkToRoot(this, goal, [&](uint32_t parent) { root = parent; });
    if (root == m_lastGoal && goal->m_children->m_goalId == 0)
        m_lastGoal = 0;
    if (!Virtual<bool>(goal, CGoal::kVtHasWon)) {
        if (goal->m_children->m_goalId == 0 && net->InNetGame()) {
            Net::MsgDesc desc;
            desc.Construct();
            Net::App* client = net->GetClient(0);
            // The message: the game's id and the chain's first goal (the id's three padding bytes are left as the
            // original's stack had them: the network manager's address).
            struct {
                uint32_t gameId;
                uint32_t goal;
            } message = {reinterpret_cast<uint32_t>(net), 0};
            message.gameId = (message.gameId & ~0xFFu) | net->GetGameId();
            message.goal = goal->GetRootGoalId();
            desc.m_length = sizeof(message);
            desc.m_groupId = kGroupGoals;
            desc.m_data = &message;
            desc.m_id = kMsgGoalWon;
            desc.m_queue = Net::MsgDesc::kQueueSequenced;
            static_cast<Net::Client*>(client)->EnqueueMessageToServer(&desc);
        } else if (goal->m_children->m_goalId == 0 && !goal->GetParams()->ContainsFlag(kEditedGoal)) {
            ++m_goalsBeaten;
        }
    }
    Virtual<bool>(goal, CGoal::kVtWin);
    return true;
}

// 0x00558180
bool CGoalManager::LoseGoal(uint32_t id)
{
    CGoal* goal = m_goals.Get(id);
    if (!goal)
        return false;
    Virtual<bool>(goal, CGoal::kVtLose);
    if (GameMode()->GetNameChecksum() == kClassic ||
        (GameMode()->GetNameChecksum() == kNetGoalAttack && GameMode()->GetGoalsMode() == kGoalsClassic))
        ActivateGoal(id, false);
    return true;
}

// 0x00558200
int CGoalManager::GetNumSelectedGoals()
{
    int selected = 0;
    for (int i = 0; i < m_goals.m_count; ++i)
        if (m_goals.GetByIndex(i)->IsSelected())
            ++selected;
    return selected;
}

// 0x00558250
void CGoalManager::DeactivateAllGoals(bool unused)
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (Virtual<bool>(goal, CGoal::kVtIsActive))
            Virtual<bool>(goal, CGoal::kVtDeactivate, unused, true);
    }
}

// 0x005582B0
void CGoalManager::ActivateAllGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CGoal::kVtIsActive) && !Virtual<bool>(goal, CGoal::kVtHasWon) && !goal->IsNetGoal())
            Virtual<bool>(goal, CGoal::kVtActivate);
    }
}

// 0x00558320
void CGoalManager::UninitializeGoalTree(uint32_t id)
{
    CGoal* root = WalkToRoot(this, m_goals.Get(id), [](uint32_t) {});
    root->Uninitialize(true, 0);
}

// 0x00558380
void CGoalManager::UninitializeAllGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->Uninitialize(false, 0);
}

// 0x005583C0
void CGoalManager::InitializeAllGoals()
{
    GameNet::Manager* net = NetManager();
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (net->InNetGame() && !goal->IsNetGoal())
            continue;
        goal->AppendDifficultyLevelParams();
        if (goal->IsLocked())
            continue;
        if (Virtual<bool>(goal, CGoal::kVtHasWon) && !goal->GetParams()->ContainsFlag(kAlwaysInitializeGoal))
            continue;
        GoalLink* parents = goal->m_parents;
        Script::CStruct* params = goal->GetParams();
        if (parents->m_goalId)
            continue;
        if (GameMode()->GetNameChecksum() == kClassic || !Virtual<bool>(goal, CGoal::kVtHasWon) ||
            params->ContainsFlag(kAlwaysInitializeGoal) || params->ContainsFlag(kEditedGoal))
            goal->Init();
    }
}

// 0x005584C0
void CGoalManager::InitializeAllSelectedGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (!goal->IsSelected())
            continue;
        Script::RunScript(kGoalCreateProsetGeom, goal->GetParams(), nullptr, false, nullptr);
        goal->Init();
    }
}

// 0x00558530
void CGoalManager::ActivateAllSelectedGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (goal->IsSelected())
            Virtual<bool>(goal, CGoal::kVtActivate);
    }
}

// 0x00558580
void CGoalManager::DeselectAllGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->Deselect();
}

// 0x005585C0
void CGoalManager::PauseAllGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->Pause();
}

// 0x00558600
void CGoalManager::UnPauseAllGoals()
{
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->UnPause();
}

// 0x00558640
bool CGoalManager::QuickStartGoal(uint32_t id, bool checkExists)
{
    CGoal* goal = m_goals.Get(id);
    if (!goal && checkExists)
        return false;
    goal->SetQuickStart(); // (no such goal and not checkExists: written through null, as the original)
    bool started = Virtual<bool>(goal, CGoal::kVtActivate);
    if (started && Virtual<bool>(goal, CGoal::kVtCanRetry))
        m_lastGoal = id;
    goal->UnsetQuickStart();
    return started;
}

// 0x005586A0
bool CGoalManager::SetGoalFlag(uint32_t id, uint32_t flag, int value, uint32_t sub)
{
    CGoal* goal = m_goals.Get(id);
    if (!goal)
        return false;
    return Virtual<bool>(goal, CGoal::kVtSetFlag, flag, value, sub);
}

// 0x005586E0
bool CGoalManager::GotGap(uint32_t gap)
{
    bool got = false;
    for (int i = 0; i < m_goals.m_count; ++i)
        got = m_goals.GetByIndex(i)->GotGap(gap);
    return got;
}

// 0x00558730
Script::CStruct* CGoalManager::GetGoalParams(uint32_t id) { return m_goals.Get(id)->GetParams(); }

// 0x00558750
bool CGoalManager::NextWaypoint(uint32_t id) { return m_goals.Get(id)->NextWaypoint(); }

// 0x00558770
void CGoalManager::KillCluster(uint32_t cluster, int score)
{
    int count = m_goals.m_count; // (read once)
    for (int i = 0; i < count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CGoal::kVtIsActive) || goal->IsNetGoal())
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
        CGoal* goal = m_goals.GetByIndex(i);
        if (!Virtual<bool>(goal, CGoal::kVtIsActive) || goal->IsNetGoal())
            continue;
        Script::CStruct* params = goal->GetParams();
        if (goal->IsSpecial() || params->ContainsFlag(kCreateATrick)) {
            if (!params->ContainsFlag(kSpecialGap) && goal->DidTrickString())
                WinGoal(goal->GetGoalId());
        } else if (goal->IsTetris()) {
            reinterpret_cast<CTetrisGoal*>(goal)->CheckTricks();
        } else if (goal->IsFindGaps()) {
            reinterpret_cast<CFindGapsGoal*>(goal)->CheckGaps();
        }
    }
}

// 0x005588D0
void CGoalManager::UnlockGoal(uint32_t id) { m_goals.Get(id)->Unlock(); }

// 0x005588F0
int CGoalManager::HasBeatenGoal(uint32_t id)
{
    if (CGoal* goal = m_goals.Get(id))
        return Virtual<bool>(goal, CGoal::kVtHasWon);
    int beaten = 0;
    Script::CStruct* data;
    if (m_goalData->GetStructure(id, &data, 0))
        data->GetInteger(kHasBeaten, &beaten, 1);
    return beaten != 0;
}

// 0x00558960
void CGoalManager::ToggleGoalSelection(uint32_t id)
{
    CGoal* goal = m_goals.Get(id);
    if (goal->IsSelected())
        goal->Deselect();
    else
        goal->Select();
}

// 0x005589A0
bool CGoalManager::HasWonProsetGoal(const char* prefix)
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (Virtual<bool>(goal, CGoal::kVtHasWon) && goal->HasProsetPrefix(prefix))
            return true;
    }
    return false;
}

// 0x00558A00
void CGoalManager::RestoreGoalData()
{
    Script::CComponent* component = m_goalData->GetNextComponent(nullptr);
    while (component) {
        Script::CComponent* next = m_goalData->GetNextComponent(component);
        if (component->name == kGoalManagerParams) {
            Script::CStruct* levels;
            component->structure->GetStructure(kDifficultyLevels, &levels, 1);
            m_params->Clear();
            m_params->AppendStructureIfAny(levels);
        } else if (CGoal* goal = m_goals.Get(component->name)) {
            Virtual<void>(goal, CGoal::kVtLoadSaveData, component->structure);
        }
        component = next;
    }
}

// 0x00558AB0
int CGoalManager::NumGoalsBeatenBy(int bit)
{
    int count = 0;
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (HasNoParent(goal) && Virtual<bool>(goal, CGoal::kVtStageFlagSet, bit))
            ++count;
    }
    return count;
}

// 0x00558B10
int CGoalManager::NumGoalsNetBeatenBy(int bit)
{
    int count = 0;
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (HasNoParent(goal) && Virtual<bool>(goal, CGoal::kVtNetStageFlagSet, bit))
            ++count;
    }
    return count;
}

// 0x00558B70
int CGoalManager::NumGoalsBeaten()
{
    int count = 0;
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (HasNoParent(goal) && Virtual<bool>(goal, CGoal::kVtHasWon))
            ++count;
    }
    return count;
}

// 0x00558BC0
void CGoalManager::UnlockAllGoals()
{
    m_allUnlocked = true;
    for (int i = 0; i < m_goals.m_count; ++i)
        m_goals.GetByIndex(i)->SetHasSeen();
}

// 0x00558C00
void CGoalManager::UnlockAllProGoals()
{
    m_allUnlocked = true;
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (goal->IsProGoal() && goal->IsLocked())
            goal->Unlock();
    }
}

// 0x00558C70
void CGoalManager::PlayGoalStream(Script::CStruct* params)
{
    uint32_t name;
    params->GetChecksum(kName, &name, 1);
    CGoal* goal = m_goals.Get(name);
    uint32_t stream = 0;
    params->GetChecksum(kVO, &stream, 0);
    if (stream) {
        goal->m_ped->PlayStream(stream, true, 0, false);
        return;
    }
    const char* type = nullptr;
    if (params->GetString(kType, &type, 0))
        goal->m_ped->PlayGoalStream(type, params);
}

// 0x00558D20
int CGoalManager::IsGoalLocked(uint32_t id)
{
    if (CGoal* goal = m_goals.Get(id))
        return goal->IsLocked();
    int locked = 0;
    Script::CStruct* data;
    if (m_goalData->GetStructure(id, &data, 0))
        data->GetInteger(kIsLocked, &locked, 1);
    return locked != 0;
}

// 0x00558D90
CGoal* CGoalManager::GetActiveCompetition()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (goal->IsCompetition() && Virtual<bool>(goal, CGoal::kVtIsActive))
            return goal;
    }
    return nullptr;
}

// 0x00558DF0
void CGoalManager::ReplaceTrickTextAll()
{
    int count = m_goals.m_count; // (read once)
    for (int i = 0; i < count; ++i)
        m_goals.GetByIndex(i)->ReplaceTrickText();
}

// 0x00558E30
bool CGoalManager::GoalExists(uint32_t id) { return m_goals.Get(id) != nullptr; }

// 0x00558E50
void CGoalManager::CheckTrickObjects()
{
    auto* skater = static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
    int player = *reinterpret_cast<int*>(static_cast<uint8_t*>(skater->GetScore()) + 0x20);
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
        CGoal* goal = m_goals.GetByIndex(i);
        uint32_t id = goal->GetGoalId();
        for (GoalLink* link = goal->m_children; link && link->m_goalId; link = link->m_next) {
            CGoal* child = m_goals.Get(link->m_goalId); // (one not in the table is read through null)
            bool linked = false;
            for (GoalLink* parent = child->m_parents; parent && parent->m_goalId; parent = parent->m_next) {
                if (parent->m_goalId == id) {
                    linked = true;
                    break;
                }
            }
            if (!linked)
                child->AddParent(id);
        }
    }
    for (int i = 0; i < count; ++i) {
        CGoal* goal = m_goals.GetByIndex(i);
        if (HasNoParent(goal))
            goal->PassInheritedFlags(0);
    }
}

// 0x00558FD0
void CGoalManager::CreateGoalLevelObjects()
{
    for (int i = 0; i < m_goals.m_count; ++i) {
        Script::CArray* nodes;
        if (!m_goals.GetByIndex(i)->GetParams()->GetArray(kCreateGeometry, &nodes, 0))
            continue;
        for (uint32_t n = 0; n < nodes->m_size; ++n)
            CFuncs::CreateFromNodeName(nodes->GetElement(n));
    }
}

// 0x00559050
int CGoalManager::GetDifficultyLevel(bool career)
{
    uint32_t mode = career ? kCareer : GameMode()->GetNameChecksum();
    int level;
    if (!m_params->GetInteger(mode, &level, 0))
        level = 1;
    switch (level) {
    case 0: return 0;
    case 2: return 2;
    default: return 1;
    }
}

// 0x005590C0
void CGoalManager::HideAllGoalPeds(bool hide)
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
        CGoal* goal = m_goals.GetByIndex(i);
        if (goal->GetParams()->ContainsFlag(kTrickObjects))
            reinterpret_cast<CTrickObjectsGoal*>(goal)->ObjectHit(cluster);
    }
}

// 0x0055BEA0
bool CGoalManager::RemoveGoal(uint32_t id)
{
    CGoal* goal = m_goals.Get(id);
    DestroyGoalPed(goal->m_ped);
    m_goals.Remove(id);
    if (m_lastGoal == id)
        m_lastGoal = 0;
    Virtual<CGoal*>(goal, CGoal::kVtDestroy, static_cast<uint8_t>(1));
    return true;
}

// 0x0055BEF0
void CGoalManager::RemoveAllGoals()
{
    m_lastGoal = 0;
    for (int i = 0; i < m_goals.m_count; ++i)
        DestroyGoalPed(m_goals.GetByIndex(i)->m_ped);
    Lst::LookupTableDestroyer destroyer = {&m_goals};
    destroyer.DeleteTableContents();
}

// 0x0055DBE0
void CGoalManager::AddActiveGoal(CGoal* goal)
{
    reinterpret_cast<Lst::Head*>(&m_activeGoals)->AddNode(&goal->m_node);
}

namespace {

// The active list's nodes (the walk checks each node's data for -1 before the node for null, as the original).
int CountActive(Mem::ListNode* head)
{
    Mem::ListNode* node = head->next;
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
Mem::ListNode* GoalListHead::NodeAt(int index)
{
    Mem::ListNode* node = m_node.next;
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
CGoal* CGoalManager::GetActiveGoal(int index)
{
    if (!CountActive(&m_activeGoals.m_node))
        return nullptr;
    return static_cast<CGoal*>(m_activeGoals.NodeAt(index)->data);
}

// 0x0055DC30
int CGoalManager::NumActiveGoals(bool all)
{
    int counted = 0;
    int count = CountActive(&m_activeGoals.m_node);
    for (int i = 0; i < count; ++i) {
        CGoal* goal = CountActive(&m_activeGoals.m_node)
                          ? static_cast<CGoal*>(m_activeGoals.NodeAt(i)->data)
                          : nullptr;
        bool counts = goal->IsNetGoal() ? all : all || Virtual<bool>(goal, CGoal::kVtIsActive2);
        if (counts)
            ++counted;
    }
    return counted;
}

// 0x005591B0
CGoalManager* GetGoalManager()
{
    return reinterpret_cast<Mdl::Skate*>(SkateCalls::Skate())->GetGoalManager();
}

} // namespace Game

REPLACE(0x0055B9F0, &Game::GoalListHead::DestroyBody);
REPLACE(0x0055D960, &Game::GoalListHead::Destroy);
REPLACE(0x0055D9C0, &Game::CGoalManager::Construct);
REPLACE(0x0055DB50, &Game::CGoalManager::DestroyBody);
REPLACE_SOURCE_ONLY(0x00557E30, &Game::CGoalManager::CanStartGoal); // (4 bytes)
REPLACE(0x00557E40, &Game::CGoalManager::ShowGoalPoints);
REPLACE(0x00557E70, &Game::CGoalManager::ResetGoalData);
REPLACE(0x00557F90, &Game::CGoalManager::GetGoal);
REPLACE(0x00557FB0, &Game::CGoalManager::GetGoalByIndex);
REPLACE(0x00557FC0, &Game::CGoalManager::ActivateGoal);
REPLACE(0x00558030, &Game::CGoalManager::WinGoal);
REPLACE(0x00558180, &Game::CGoalManager::LoseGoal);
REPLACE(0x00558200, &Game::CGoalManager::GetNumSelectedGoals);
REPLACE(0x00558250, &Game::CGoalManager::DeactivateAllGoals);
REPLACE(0x005582B0, &Game::CGoalManager::ActivateAllGoals);
REPLACE(0x00558320, &Game::CGoalManager::UninitializeGoalTree);
REPLACE(0x00558380, &Game::CGoalManager::UninitializeAllGoals);
REPLACE(0x005583C0, &Game::CGoalManager::InitializeAllGoals);
REPLACE(0x005584C0, &Game::CGoalManager::InitializeAllSelectedGoals);
REPLACE(0x00558530, &Game::CGoalManager::ActivateAllSelectedGoals);
REPLACE(0x00558580, &Game::CGoalManager::DeselectAllGoals);
REPLACE(0x005585C0, &Game::CGoalManager::PauseAllGoals);
REPLACE(0x00558600, &Game::CGoalManager::UnPauseAllGoals);
REPLACE(0x00558640, &Game::CGoalManager::QuickStartGoal);
REPLACE(0x005586A0, &Game::CGoalManager::SetGoalFlag);
REPLACE(0x005586E0, &Game::CGoalManager::GotGap);
REPLACE(0x00558730, &Game::CGoalManager::GetGoalParams);
REPLACE(0x00558750, &Game::CGoalManager::NextWaypoint);
REPLACE(0x00558770, &Game::CGoalManager::KillCluster);
REPLACE(0x005587F0, &Game::CGoalManager::CheckTrickGoals);
REPLACE(0x005588D0, &Game::CGoalManager::UnlockGoal);
REPLACE(0x005588F0, &Game::CGoalManager::HasBeatenGoal);
REPLACE(0x00558960, &Game::CGoalManager::ToggleGoalSelection);
REPLACE(0x005589A0, &Game::CGoalManager::HasWonProsetGoal);
REPLACE(0x00558A00, &Game::CGoalManager::RestoreGoalData);
REPLACE(0x00558AB0, &Game::CGoalManager::NumGoalsBeatenBy);
REPLACE(0x00558B10, &Game::CGoalManager::NumGoalsNetBeatenBy);
REPLACE(0x00558B70, &Game::CGoalManager::NumGoalsBeaten);
REPLACE(0x00558BC0, &Game::CGoalManager::UnlockAllGoals);
REPLACE(0x00558C00, &Game::CGoalManager::UnlockAllProGoals);
REPLACE(0x00558C70, &Game::CGoalManager::PlayGoalStream);
REPLACE(0x00558D20, &Game::CGoalManager::IsGoalLocked);
REPLACE(0x00558D90, &Game::CGoalManager::GetActiveCompetition);
REPLACE(0x00558DF0, &Game::CGoalManager::ReplaceTrickTextAll);
REPLACE(0x00558E30, &Game::CGoalManager::GoalExists);
REPLACE(0x00558E50, &Game::CGoalManager::CheckTrickObjects);
REPLACE(0x00558ED0, &Game::CGoalManager::BuildGoalTree);
REPLACE(0x00558FD0, &Game::CGoalManager::CreateGoalLevelObjects);
REPLACE(0x00559050, &Game::CGoalManager::GetDifficultyLevel);
REPLACE(0x005590C0, &Game::CGoalManager::HideAllGoalPeds);
REPLACE(0x00559110, &Game::CGoalManager::IsGroupLocked);
REPLACE(0x00559150, &Game::CGoalManager::TrickObjectHit);
REPLACE(0x005591B0, Game::GetGoalManager);
REPLACE(0x0055BEA0, &Game::CGoalManager::RemoveGoal);
REPLACE(0x0055BEF0, &Game::CGoalManager::RemoveAllGoals);
REPLACE(0x0055D990, &Game::GoalListHead::NodeAt);
REPLACE(0x0055DBE0, &Game::CGoalManager::AddActiveGoal);
REPLACE(0x0055DBF0, &Game::CGoalManager::GetActiveGoal);
REPLACE(0x0055DC30, &Game::CGoalManager::NumActiveGoals);
