// The game modes' victory conditions. See victory_conditions.h.

#include "game/skate/victory_conditions.h"

#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "game/core/stubs.h"
#include "game/core/code_address.h"

namespace GoalSystem {

namespace {

// The vtables.
static VtableSlot kBaseVtable_slots[] = {&GoalSystem::CWinRule::DestroyBase, Stubs::ReturnFalse1, &_purecall,
                 &_purecall, &_purecall};
void** const kBaseVtable = reinterpret_cast<void**>(kBaseVtable_slots);
static VtableSlot kHighestScoreVtable_slots[] = {&GoalSystem::CWinRule::Destroy, &GoalSystem::CWinRule::IsHighestScore, Stubs::ReturnFalse,
                 Stubs::ReturnFalse, Stubs::ReturnTrue};
void** const kHighestScoreVtable = reinterpret_cast<void**>(kHighestScoreVtable_slots);
static VtableSlot kScoreReachedVtable_slots[] = {&GoalSystem::CWinRule::Destroy, &GoalSystem::CWinRule::IsHighestScore, Stubs::ReturnTrue,
                 &GoalSystem::CScoreReachedVictoryCondition::RuleSatisfied, &GoalSystem::CScoreReachedVictoryCondition::Print};
void** const kScoreReachedVtable = reinterpret_cast<void**>(kScoreReachedVtable_slots);
static VtableSlot kCompleteGoalsVtable_slots[] = {&GoalSystem::CWinRule::Destroy, Stubs::ReturnFalse1, Stubs::ReturnTrue,
                 &GoalSystem::CCompleteGoalsVictoryCondition::RuleSatisfied, Stubs::ReturnTrue};
void** const kCompleteGoalsVtable = reinterpret_cast<void**>(kCompleteGoalsVtable_slots);
static VtableSlot kBestComboVtable_slots[] = {&GoalSystem::CWinRule::Destroy, &GoalSystem::CWinRule::IsHighestScore, Stubs::ReturnFalse,
                 Stubs::ReturnFalse, &GoalSystem::CBestComboVictoryCondition::Print};
void** const kBestComboVtable = reinterpret_cast<void**>(kBestComboVtable_slots);
static VtableSlot kLastManStandingVtable_slots[] = {&GoalSystem::CWinRule::Destroy, &GoalSystem::CSoleSurvivorWin::HasWonMatch,
                 Stubs::ReturnTrue, &GoalSystem::CSoleSurvivorWin::RuleSatisfied, Stubs::ReturnTrue};
void** const kLastManStandingVtable = reinterpret_cast<void**>(kLastManStandingVtable_slots);
static VtableSlot kEliminationVtable_slots[] = {&GoalSystem::CWinRule::Destroy, &GoalSystem::CEliminationVictoryCondition::HasWonMatch, Stubs::ReturnTrue,
                 &GoalSystem::CEliminationVictoryCondition::RuleSatisfied, Stubs::ReturnTrue};
void** const kEliminationVtable = reinterpret_cast<void**>(kEliminationVtable_slots);
static VtableSlot kCollectAllObjectsVtable_slots[] = {&GoalSystem::CWinRule::Destroy, &GoalSystem::CWinRule::IsHighestScore, Stubs::ReturnTrue,
                 &GoalSystem::CCollectAllObjectsVictoryCondition::RuleSatisfied, Stubs::ReturnTrue};
void** const kCollectAllObjectsVtable = reinterpret_cast<void**>(kCollectAllObjectsVtable_slots);

// The types (type=).
constexpr uint32_t kHighestScore = 0x8F30FD26;      // highest_score
constexpr uint32_t kTargetScore = 0x71CD53A2;       // target_score
constexpr uint32_t kCompleteGoals = 0x6FC19399;     // complete_goals
constexpr uint32_t kBestCombo = 0x6FAD9811;         // best_combo
constexpr uint32_t kLastManStanding = 0xCC06880F;   // last_man_standing
constexpr uint32_t kElimination = 0xA6B8D7B0;       // elimination
constexpr uint32_t kCollectAllObjects = 0x72068889; // collect_all_objects

// The skate module (GameModules::Skate, 0x007CE478) and its skaters (not rewritten yet).
uint8_t* Skate() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
// How many skaters there are: the skate module's skater list (+0x34; nodes: +0x04 the skater, -1 at the end,
// +0x0C the next).
uint32_t SkaterCount(uint8_t* skate)
{
    uint8_t* node = *reinterpret_cast<uint8_t**>(skate + 0x34);
    uint32_t count = 0;
    if (*reinterpret_cast<int*>(node + 4) == -1 || !node) // (read before the null check, as in the original)
        return 0;
    do {
        node = *reinterpret_cast<uint8_t**>(node + 0xC);
        ++count;
    } while (*reinterpret_cast<int*>(node + 4) != -1 && node);
    return count;
}
uint8_t* GetRiderAt(uint8_t* skate, uint32_t index)
{
    return reinterpret_cast<uint8_t*>(reinterpret_cast<GameModules::Skate*>(skate)->GetRiderAt(static_cast<int>(index)));
}
// A skater's score object (GameModules::Score) and its total.
uint8_t* ScoreOf(uint8_t* skater) { return static_cast<uint8_t*>(reinterpret_cast<GameObjects::CRider*>(skater)->GetPointsTotal()); }
int TotalScore(uint8_t* score) { return reinterpret_cast<GameModules::Score*>(score)->GetTotalScore(); }

// The game mode (0x007CE470).
void* GameMode() { return static_cast<void*>(g_gameMode); }
bool IsSquadMatch() { return static_cast<CMatchRules*>(GameMode())->IsSquadMatch() != 0; }
int NumTeams() { return static_cast<CMatchRules*>(GameMode())->NumTeams(); }

// The network manager's players (0x007CCE14): PlayerRecord +0x14 the skater, +0x30 the team, +0x34 the objects
// collected. Walked with a ListKit::Search<PlayerRecord> (vtable 0x0064C1A8).
OnlinePlay::Manager* NetManager() { return OnlinePlay::g_gameNetManager; }
struct PlayerSearch {
    void* m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};
uint8_t* FirstPlayer(PlayerSearch* search)
{
    return static_cast<uint8_t*>(NetManager()->BeginPlayerWalk(search, false));
}
uint8_t* NextPlayer(PlayerSearch* search)
{
    return static_cast<uint8_t*>(NetManager()->StepPlayerWalk(search, false));
}
uint8_t* LocalPlayer() { return static_cast<uint8_t*>(NetManager()->GetOwnPlayer()); }
bool IsEliminated(uint8_t* player) { return reinterpret_cast<OnlinePlay::PlayerRecord*>(player)->IsEliminated(); }

} // namespace

// 0x005769A0
void CWinRule::DestroyBody() { m_vtable = kBaseVtable; }

// 0x00576970
CWinRule* CWinRule::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00576650
CWinRule* CWinRule::DestroyBase(uint8_t flags)
{
    m_vtable = kBaseVtable;
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x005769B0
bool CWinRule::IsHighestScore(uint32_t player)
{
    uint8_t* skate = Skate();
    for (uint32_t i = 0; i < SkaterCount(skate); ++i) {
        if (i == player)
            continue;
        uint8_t* mine = ScoreOf(GetRiderAt(skate, player));
        uint8_t* theirs = ScoreOf(GetRiderAt(skate, i));
        int theirScore = TotalScore(theirs);
        if (TotalScore(mine) < theirScore)
            return false;
    }
    return true;
}

// 0x00576A40
bool CScoreReachedVictoryCondition::RuleSatisfied()
{
    uint8_t* skate = Skate();
    if (IsSquadMatch()) {
        int totals[4] = {};
        PlayerSearch search;
        for (uint8_t* player = FirstPlayer(&search); player; player = NextPlayer(&search)) {
            uint8_t* score = ScoreOf(*reinterpret_cast<uint8_t**>(player + 0x14));
            int team = *reinterpret_cast<int*>(player + 0x30);
            totals[team] += TotalScore(score);
        }
        for (int total : totals) {
            if (total >= m_pointsHeld)
                return true;
        }
        return false;
    }
    for (uint32_t i = 0; i < SkaterCount(skate); ++i) {
        if (TotalScore(ScoreOf(GetRiderAt(skate, i))) >= m_pointsHeld)
            return true;
    }
    return false;
}

// 0x00576610
bool CScoreReachedVictoryCondition::Print()
{
    DebugOut::Printf("Found CScoreReachedVictoryCondition %d\n", m_pointsHeld);
    return true;
}

// 0x00576630
bool CBestComboVictoryCondition::Print()
{
    DebugOut::Printf("Found CBestComboVictoryCondition %d\n", m_numCombos);
    return true;
}

// 0x00576B80
bool CCompleteGoalsVictoryCondition::RuleSatisfied()
{
    OnlinePlay::Manager* net = NetManager();
    uint8_t* skate = Skate();
    CGoalManager* goals = GetObjectiveBoard(); // the skate module's goal manager
    if (IsSquadMatch()) {
        int needed = goals->CountPickedObjectives();
        for (int team = 0; team < NumTeams(); ++team) {
            if (net->GetSquadPoints(team) >= needed)
                return true;
        }
        return false;
    }
    uint32_t count = *reinterpret_cast<uint32_t*>(goals); // (the goals there are: its first word - 0x00469490, a folded getter)
    for (uint32_t i = 0; i < SkaterCount(skate); ++i) {
        uint8_t* skater = GetRiderAt(skate, i);
        if (count == 0)
            return true;
        bool missing = false;
        for (uint32_t g = 0; g < count; ++g) {
            uint8_t* goal = reinterpret_cast<uint8_t*>(goals->GetObjectiveAt(static_cast<int>(g)));
            // A goal that doesn't count (+0x2C's value set, or 0x005542F0 false) or that the skater has won (vtable
            // +0x50, by the skater's id).
            if (**reinterpret_cast<int**>(goal + 0x2C) != 0 || !reinterpret_cast<CObjective*>(goal)->IsSelected())
                continue;
            auto hasWon = reinterpret_cast<bool(__thiscall*)(uint8_t*, uint32_t)>((*reinterpret_cast<void***>(goal))[0x50 / 4]);
            if (hasWon(goal, *reinterpret_cast<uint32_t*>(skater + 0x14)))
                continue;
            missing = true;
            break;
        }
        if (!missing)
            return true;
    }
    return false;
}

// 0x00576680
bool CSoleSurvivorWin::RuleSatisfied()
{
    uint8_t* player = LocalPlayer();
    if (player) {
        uint8_t* skater = *reinterpret_cast<uint8_t**>(player + 0x14);
        if (skater && TotalScore(ScoreOf(skater)) <= 0)
            return true;
    }
    return false;
}

// 0x005766B0
bool CSoleSurvivorWin::HasWonMatch(uint32_t player)
{
    uint8_t* score = ScoreOf(GetRiderAt(Skate(), player));
    return score && TotalScore(score) > 0;
}

// 0x005766E0
bool CEliminationVictoryCondition::RuleSatisfied()
{
    uint8_t* player = LocalPlayer();
    return player && IsEliminated(player);
}

// 0x00576700
bool CEliminationVictoryCondition::HasWonMatch(uint32_t player)
{
    OnlinePlay::Manager* net = NetManager();
    uint8_t* skater = GetRiderAt(Skate(), player);
    if (!skater)
        return false;
    uint8_t* info = net->PlayerByObjectId(
        static_cast<uint16_t>(*reinterpret_cast<uint32_t*>(skater + 0x14))); // the player with the skater's id
    return !IsEliminated(info);
}

// 0x00576740
bool CCollectAllObjectsVictoryCondition::RuleSatisfied()
{
    int total = 0;
    PlayerSearch search;
    for (uint8_t* player = FirstPlayer(&search); player; player = NextPlayer(&search))
        total += *reinterpret_cast<int*>(player + 0x34);
    if (!total)
        return false;
    for (uint8_t* player = FirstPlayer(&search); player; player = NextPlayer(&search)) {
        if (TotalScore(ScoreOf(*reinterpret_cast<uint8_t**>(player + 0x14))) == total)
            return true;
    }
    return false;
}

// 0x00576810
CWinRule* CreateVictoryCondition(Scripts::CStruct* params)
{
    uint32_t type;
    params->ReadHashValue("type", &type, 1);
    auto make = [](unsigned int size, void** vtable) {
        auto* condition = static_cast<CWinRule*>(MemSys::AllocZeroed(size));
        if (condition)
            condition->m_vtable = vtable;
        return condition;
    };
    switch (type) {
    case kTargetScore: {
        int score;
        params->FetchIntValue("score", &score, 1);
        auto* condition = static_cast<CScoreReachedVictoryCondition*>(make(8, kScoreReachedVtable));
        if (condition)
            condition->m_pointsHeld = score;
        return condition;
    }
    case kBestCombo: {
        int combos;
        params->FetchIntValue("num_combos", &combos, 1);
        auto* condition = static_cast<CBestComboVictoryCondition*>(make(8, kBestComboVtable));
        if (condition)
            condition->m_numCombos = combos;
        return condition;
    }
    case kCompleteGoals:
        return make(4, kCompleteGoalsVtable);
    case kCollectAllObjects:
        return make(4, kCollectAllObjectsVtable);
    case kHighestScore:
        return make(4, kHighestScoreVtable);
    case kElimination:
        return make(4, kEliminationVtable);
    case kLastManStanding:
        return make(4, kLastManStandingVtable);
    default:
        return nullptr;
    }
}

} // namespace Game
