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
#include "hook/game.h"
#include "hook/hook.h"

namespace Game {

namespace {

// The vtables.
void** const kBaseVtable = reinterpret_cast<void**>(0x0064EAA8);
void** const kHighestScoreVtable = reinterpret_cast<void**>(0x0064EB0C);
void** const kScoreReachedVtable = reinterpret_cast<void**>(0x0064EB20);
void** const kCompleteGoalsVtable = reinterpret_cast<void**>(0x0064EB34);
void** const kBestComboVtable = reinterpret_cast<void**>(0x0064EB48);
void** const kLastManStandingVtable = reinterpret_cast<void**>(0x0064EB5C);
void** const kEliminationVtable = reinterpret_cast<void**>(0x0064EB70);
void** const kCollectAllObjectsVtable = reinterpret_cast<void**>(0x0064EB84);

// The types (type=).
constexpr uint32_t kHighestScore = 0x8F30FD26;      // highest_score
constexpr uint32_t kTargetScore = 0x71CD53A2;       // target_score
constexpr uint32_t kCompleteGoals = 0x6FC19399;     // complete_goals
constexpr uint32_t kBestCombo = 0x6FAD9811;         // best_combo
constexpr uint32_t kLastManStanding = 0xCC06880F;   // last_man_standing
constexpr uint32_t kElimination = 0xA6B8D7B0;       // elimination
constexpr uint32_t kCollectAllObjects = 0x72068889; // collect_all_objects

// The skate module (Mdl::Skate, 0x007CE478) and its skaters (not rewritten yet).
uint8_t* Skate() { return GameVariable<uint8_t*>(0x007CE478); }
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
uint8_t* GetSkater(uint8_t* skate, uint32_t index)
{
    return reinterpret_cast<uint8_t*>(reinterpret_cast<Mdl::Skate*>(skate)->GetSkater(static_cast<int>(index)));
}
// A skater's score object (Mdl::Score) and its total.
uint8_t* ScoreOf(uint8_t* skater) { return static_cast<uint8_t*>(reinterpret_cast<Obj::CSkater*>(skater)->GetScore()); }
int TotalScore(uint8_t* score) { return reinterpret_cast<Mdl::Score*>(score)->GetTotalScore(); }

// The game mode (0x007CE470).
void* GameMode() { return GameVariable<void*>(0x007CE470); }
bool IsTeamGame() { return static_cast<CGameMode*>(GameMode())->IsTeamGame() != 0; }
int NumTeams() { return static_cast<CGameMode*>(GameMode())->NumTeams(); }

// The network manager's players (0x007CCE14): PlayerInfo +0x14 the skater, +0x30 the team, +0x34 the objects
// collected. Walked with a Lst::Search<PlayerInfo> (vtable 0x0064C1A8).
GameNet::Manager* NetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
struct PlayerSearch {
    void* m_vtable = reinterpret_cast<void*>(0x0064C1A8);
    void* m_node = nullptr;
};
uint8_t* FirstPlayer(PlayerSearch* search)
{
    return static_cast<uint8_t*>(NetManager()->FirstPlayerInfo(search, false));
}
uint8_t* NextPlayer(PlayerSearch* search)
{
    return static_cast<uint8_t*>(NetManager()->NextPlayerInfo(search, false));
}
uint8_t* LocalPlayer() { return static_cast<uint8_t*>(NetManager()->GetLocalPlayer()); }
bool IsEliminated(uint8_t* player) { return reinterpret_cast<GameNet::PlayerInfo*>(player)->IsEliminated(); }

} // namespace

// 0x005769A0
void CVictoryCondition::DestroyBody() { m_vtable = kBaseVtable; }

// 0x00576970
CVictoryCondition* CVictoryCondition::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00576650
CVictoryCondition* CVictoryCondition::DestroyBase(uint8_t flags)
{
    m_vtable = kBaseVtable;
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005769B0
bool CVictoryCondition::IsHighestScore(uint32_t player)
{
    uint8_t* skate = Skate();
    for (uint32_t i = 0; i < SkaterCount(skate); ++i) {
        if (i == player)
            continue;
        uint8_t* mine = ScoreOf(GetSkater(skate, player));
        uint8_t* theirs = ScoreOf(GetSkater(skate, i));
        int theirScore = TotalScore(theirs);
        if (TotalScore(mine) < theirScore)
            return false;
    }
    return true;
}

// 0x00576A40
bool CScoreReachedVictoryCondition::ConditionComplete()
{
    uint8_t* skate = Skate();
    if (IsTeamGame()) {
        int totals[4] = {};
        PlayerSearch search;
        for (uint8_t* player = FirstPlayer(&search); player; player = NextPlayer(&search)) {
            uint8_t* score = ScoreOf(*reinterpret_cast<uint8_t**>(player + 0x14));
            int team = *reinterpret_cast<int*>(player + 0x30);
            totals[team] += TotalScore(score);
        }
        for (int total : totals) {
            if (total >= m_score)
                return true;
        }
        return false;
    }
    for (uint32_t i = 0; i < SkaterCount(skate); ++i) {
        if (TotalScore(ScoreOf(GetSkater(skate, i))) >= m_score)
            return true;
    }
    return false;
}

// 0x00576610
bool CScoreReachedVictoryCondition::Print()
{
    Dbg::Printf("Found CScoreReachedVictoryCondition %d\n", m_score);
    return true;
}

// 0x00576630
bool CBestComboVictoryCondition::Print()
{
    Dbg::Printf("Found CBestComboVictoryCondition %d\n", m_numCombos);
    return true;
}

// 0x00576B80
bool CCompleteGoalsVictoryCondition::ConditionComplete()
{
    GameNet::Manager* net = NetManager();
    uint8_t* skate = Skate();
    CGoalManager* goals = GetGoalManager(); // the skate module's goal manager
    if (IsTeamGame()) {
        int needed = goals->GetNumSelectedGoals();
        for (int team = 0; team < NumTeams(); ++team) {
            if (net->GetTeamScore(team) >= needed)
                return true;
        }
        return false;
    }
    uint32_t count = *reinterpret_cast<uint32_t*>(goals); // (the goals there are: its first word - 0x00469490, a folded getter)
    for (uint32_t i = 0; i < SkaterCount(skate); ++i) {
        uint8_t* skater = GetSkater(skate, i);
        if (count == 0)
            return true;
        bool missing = false;
        for (uint32_t g = 0; g < count; ++g) {
            uint8_t* goal = reinterpret_cast<uint8_t*>(goals->GetGoalByIndex(static_cast<int>(g)));
            // A goal that doesn't count (+0x2C's value set, or 0x005542F0 false) or that the skater has won (vtable
            // +0x50, by the skater's id).
            if (**reinterpret_cast<int**>(goal + 0x2C) != 0 || !reinterpret_cast<CGoal*>(goal)->IsSelected())
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
bool CLastManStandingVictoryCondition::ConditionComplete()
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
bool CLastManStandingVictoryCondition::IsWinner(uint32_t player)
{
    uint8_t* score = ScoreOf(GetSkater(Skate(), player));
    return score && TotalScore(score) > 0;
}

// 0x005766E0
bool CEliminationVictoryCondition::ConditionComplete()
{
    uint8_t* player = LocalPlayer();
    return player && IsEliminated(player);
}

// 0x00576700
bool CEliminationVictoryCondition::IsWinner(uint32_t player)
{
    GameNet::Manager* net = NetManager();
    uint8_t* skater = GetSkater(Skate(), player);
    if (!skater)
        return false;
    uint8_t* info = net->PlayerByObjectId(
        static_cast<uint16_t>(*reinterpret_cast<uint32_t*>(skater + 0x14))); // the player with the skater's id
    return !IsEliminated(info);
}

// 0x00576740
bool CCollectAllObjectsVictoryCondition::ConditionComplete()
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
CVictoryCondition* CreateVictoryCondition(Script::CStruct* params)
{
    uint32_t type;
    params->GetChecksum("type", &type, 1);
    auto make = [](unsigned int size, void** vtable) {
        auto* condition = static_cast<CVictoryCondition*>(Mem::Calloc(size));
        if (condition)
            condition->m_vtable = vtable;
        return condition;
    };
    switch (type) {
    case kTargetScore: {
        int score;
        params->GetInteger("score", &score, 1);
        auto* condition = static_cast<CScoreReachedVictoryCondition*>(make(8, kScoreReachedVtable));
        if (condition)
            condition->m_score = score;
        return condition;
    }
    case kBestCombo: {
        int combos;
        params->GetInteger("num_combos", &combos, 1);
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

REPLACE(0x00576610, &Game::CScoreReachedVictoryCondition::Print);
REPLACE(0x00576630, &Game::CBestComboVictoryCondition::Print);
REPLACE(0x00576650, &Game::CVictoryCondition::DestroyBase);
REPLACE(0x00576680, &Game::CLastManStandingVictoryCondition::ConditionComplete);
REPLACE(0x005766B0, &Game::CLastManStandingVictoryCondition::IsWinner);
REPLACE(0x005766E0, &Game::CEliminationVictoryCondition::ConditionComplete);
REPLACE(0x00576700, &Game::CEliminationVictoryCondition::IsWinner);
REPLACE(0x00576740, &Game::CCollectAllObjectsVictoryCondition::ConditionComplete);
REPLACE(0x00576810, Game::CreateVictoryCondition);
REPLACE(0x00576970, &Game::CVictoryCondition::Destroy);
REPLACE(0x005769A0, &Game::CVictoryCondition::DestroyBody);
REPLACE(0x005769B0, &Game::CVictoryCondition::IsHighestScore);
REPLACE(0x00576A40, &Game::CScoreReachedVictoryCondition::ConditionComplete);
REPLACE(0x00576B80, &Game::CCompleteGoalsVictoryCondition::ConditionComplete);
// 0x00576CA0 is a jump to 0x00576B80 (the vtable's entry): the same source.
REPLACE_SOURCE_ONLY(0x00576CA0, &Game::CCompleteGoalsVictoryCondition::ConditionComplete);
