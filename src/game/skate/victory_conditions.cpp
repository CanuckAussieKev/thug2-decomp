// The game modes' victory conditions. See victory_conditions.h.

#include "game/skate/victory_conditions.h"

#include <algorithm>
#include <cstdint>

#include "engine/memory/arena_alloc.h"
#include "base/core/dbg.h"
#include "engine/memory/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "game/goals/goal_manager.h"
#include "game/goals/objective.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include <gsl/pointers>
#include <array>
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/net/net_game_state.h"

namespace goals {

namespace {

// The types (type=).
constexpr uint32_t kHighestScore = 0x8F30FD26;      // highest_score
constexpr uint32_t kTargetScore = 0x71CD53A2;       // target_score
constexpr uint32_t kCompleteGoals = 0x6FC19399;     // complete_goals
constexpr uint32_t kLabelBestChain = 0x6FAD9811;    // best_combo
constexpr uint32_t kLastManStanding = 0xCC06880F;   // last_man_standing
constexpr uint32_t kElimination = 0xA6B8D7B0;       // elimination
constexpr uint32_t kCollectAllObjects = 0x72068889; // collect_all_objects

// The skate module (modules::Skate, 0x007CE478) and its skaters (not rewritten yet).

// How many skaters there are: the skate module's skater list (memory::ListNode: data the skater, -1 at the head).
uint32_t skater_count(modules::Skate* skate)
{
    const memory::ListNode* node = skate->riders().list_link().next();
    uint32_t count = 0;
    if (node->is_head() || !node) { // (read before the null check, as in the original)
        return 0;
    }
    for (bool again = true; again; again = !node->is_head() && (node != nullptr)) {
        node = node->next();
        ++count;
    }
    return count;
}
objects::Rider* get_rider_at(modules::Skate* skate, uint32_t index)
{
    return core::down_cast<objects::Rider>(skate->get_rider_at(static_cast<int>(index)));
}
int total_score(modules::PointTally* score) { return score->get_total_score(); }

// The game mode (0x007CE470).
void* get_game_mode() { return static_cast<void*>(game_mode()); }
bool is_squad_match() { return static_cast<MatchRules*>(get_game_mode())->is_squad_match() != 0; }
int current_num_teams() { return static_cast<MatchRules*>(get_game_mode())->current_num_teams(); }

// The network manager's players (0x007CCE14), walked with a lists::Hunt<PlayerRecord> (vtable 0x0064C1A8).
using PlayerSearch = online::PlayerSearch;
using online::PlayerRecord;
PlayerRecord* start_roster_sweep(PlayerSearch* search) { return online::online_hub()->begin_player_walk(search, false); }
PlayerRecord* step_roster_sweep(PlayerSearch* search) { return online::online_hub()->step_player_walk(search, false); }
PlayerRecord* find_own_player_record() { return online::online_hub()->get_own_player(); }
bool is_eliminated(PlayerRecord* player) { return player->is_eliminated(); }

} // namespace

bool WinRule::has_won_match(uint32_t /*player*/) { return false; }
bool WinRule::echo_out() { return true; }
int WinRule::target_value() const { return 0; }

// 0x005769B0
bool WinRule::is_highest_score(uint32_t player)
{
    modules::Skate* skate = modules::Skate::sole();
    for (uint32_t i = 0; i < skater_count(skate); ++i) {
        if (i == player) {
            continue;
        }
        auto* mine = get_rider_at(skate, player)->get_points_total();
        auto* theirs = get_rider_at(skate, i)->get_points_total();
        const int their_score = total_score(theirs);
        if (total_score(mine) < their_score) {
            return false;
        }
    }
    return true;
}

// 0x00576A40
bool ScoreReachedVictoryCondition::rule_satisfied()
{
    modules::Skate* skate = modules::Skate::sole();
    if (is_squad_match()) {
        std::array<int, 4> totals = {};
        PlayerSearch search;
        for (PlayerRecord* player = start_roster_sweep(&search); player; player = step_roster_sweep(&search)) {
            modules::PointTally* score = player->rider()->get_points_total();
            const int team = player->team();
            core::at(totals, team) += total_score(score);
        }
        return std::ranges::any_of(totals, [&](const int total) { return total >= points_held_; });
    }
    for (uint32_t i = 0; i < skater_count(skate); ++i) {
        if (total_score(get_rider_at(skate, i)->get_points_total()) >= points_held_) {
            return true;
        }
    }
    return false;
}

// 0x00576610
bool ScoreReachedVictoryCondition::echo_out()
{
    debug::printf("Found CScoreReachedVictoryCondition %d\n", points_held_);
    return true;
}

// 0x00576630
bool BestComboVictoryCondition::echo_out()
{
    debug::printf("Found CBestComboVictoryCondition %d\n", num_combos_);
    return true;
}

// 0x00576B80
bool CompleteGoalsVictoryCondition::all_goals_complete()
{
    online::Hub* net = online::online_hub();
    modules::Skate* skate = modules::Skate::sole();
    GoalManager* goals = get_objective_board(); // the skate module's goal manager
    if (is_squad_match()) {
        const int needed = goals->count_picked_objectives();
        for (int team = 0; team < current_num_teams(); ++team) {
            if (net->get_squad_points(team) >= needed) {
                return true;
            }
        }
        return false;
    }
    const auto count = static_cast<uint32_t>(goals->goals().num()); // (the goals there are - 0x00469490, a folded getter)
    for (uint32_t i = 0; i < skater_count(skate); ++i) {
        auto* skater = get_rider_at(skate, i);
        if (count == 0) {
            return true;
        }
        bool missing = false;
        for (uint32_t index = 0; index < count; ++index) {
            Objective* goal = goals->get_objective_at(static_cast<int>(index));
            // A goal that doesn't count (a parent: its first parent link's id set, or 0x005542F0 false) or that the
            // skater has won (its stage flag by the skater's id: the original's vtable +0x50).
            if (goal->parents()->objective_key_ != 0 || !goal->is_picked()) {
                continue;
            }
            if (goal->stage_flag_set(static_cast<int>(skater->ident())) != 0) {
                continue;
            }
            missing = true;
            break;
        }
        if (!missing) {
            return true;
        }
    }
    return false;
}

// 0x00576680
bool SoleSurvivorWin::rule_satisfied()
{
    PlayerRecord* player = find_own_player_record();
    if (player) {
        objects::Rider* skater = player->rider();
        if (skater && total_score(skater->get_points_total()) <= 0) {
            return true;
        }
    }
    return false;
}

// 0x005766B0
bool SoleSurvivorWin::has_won_match(uint32_t player)
{
    auto* score = get_rider_at(modules::Skate::sole(), player)->get_points_total();
    return (score != nullptr) && total_score(score) > 0;
}

// 0x005766E0
bool EliminationVictoryCondition::rule_satisfied()
{
    PlayerRecord* player = find_own_player_record();
    return (player != nullptr) && is_eliminated(player);
}

// 0x00576700
bool EliminationVictoryCondition::has_won_match(uint32_t player)
{
    online::Hub* net = online::online_hub();
    const objects::Rider* skater = get_rider_at(modules::Skate::sole(), player);
    if (!skater) {
        return false;
    }
    PlayerRecord* info = net->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident())); // the skater's player
    return !is_eliminated(info);
}

// 0x00576740
bool CollectAllObjectsVictoryCondition::rule_satisfied()
{
    int total = 0;
    PlayerSearch search;
    for (PlayerRecord* player = start_roster_sweep(&search); player; player = step_roster_sweep(&search)) {
        total += player->num_placed();
    }
    if (total == 0) {
        return false;
    }
    for (PlayerRecord* player = start_roster_sweep(&search); player; player = step_roster_sweep(&search)) {
        if (total_score(player->rider()->get_points_total()) == total) {
            return true;
        }
    }
    return false;
}

// 0x00576810
gsl::owner<WinRule*> create_victory_condition(script::Struct* params)
{
    uint32_t type = 0;
    params->read_hash_value("type", &type, 1);
    switch (type) {
    case kTargetScore: {
        int score = 0;
        params->fetch_int_value("score", &score, 1);
        return new ScoreReachedVictoryCondition(score);
    }
    case kLabelBestChain: {
        int combos = 0;
        params->fetch_int_value("num_combos", &combos, 1);
        return new BestComboVictoryCondition(combos);
    }
    case kCompleteGoals:
        return new CompleteGoalsVictoryCondition;
    case kCollectAllObjects:
        return new CollectAllObjectsVictoryCondition;
    case kHighestScore:
        return new TopPointsWin;
    case kElimination:
        return new EliminationVictoryCondition;
    case kLastManStanding:
        return new SoleSurvivorWin;
    default:
        return nullptr;
    }
}

} // namespace goals
