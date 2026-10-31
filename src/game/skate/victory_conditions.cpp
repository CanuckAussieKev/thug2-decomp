// The game modes' victory conditions. See victory_conditions.h.

#include "game/skate/victory_conditions.h"

#include <cstdint>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "game/goals/goal_manager.h"
#include "game/goals/objective.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/core/stubs.h"
#include "engine/core/code_address.h"

namespace goals {

namespace {

// The vtables.
static VtableSlot g_base_vtable_slots[] = {&goals::WinRule::destroy_base, stubs::return_false1, &_purecall,
                 &_purecall, &_purecall};
void** const kBaseVtable = reinterpret_cast<void**>(g_base_vtable_slots);
static VtableSlot g_highest_score_vtable_slots[] = {&goals::WinRule::teardown, &goals::WinRule::is_highest_score, stubs::return_false,
                 stubs::return_false, stubs::return_true};
void** const kHighestScoreVtable = reinterpret_cast<void**>(g_highest_score_vtable_slots);
static VtableSlot g_score_reached_vtable_slots[] = {&goals::WinRule::teardown, &goals::WinRule::is_highest_score, stubs::return_true,
                 &goals::ScoreReachedVictoryCondition::rule_satisfied, &goals::ScoreReachedVictoryCondition::echo_out};
void** const kScoreReachedVtable = reinterpret_cast<void**>(g_score_reached_vtable_slots);
static VtableSlot g_complete_goals_vtable_slots[] = {&goals::WinRule::teardown, stubs::return_false1, stubs::return_true,
                 &goals::CompleteGoalsVictoryCondition::rule_satisfied, stubs::return_true};
void** const kCompleteGoalsVtable = reinterpret_cast<void**>(g_complete_goals_vtable_slots);
static VtableSlot g_best_combo_vtable_slots[] = {&goals::WinRule::teardown, &goals::WinRule::is_highest_score, stubs::return_false,
                 stubs::return_false, &goals::BestComboVictoryCondition::echo_out};
void** const kBestComboVtable = reinterpret_cast<void**>(g_best_combo_vtable_slots);
static VtableSlot g_last_man_standing_vtable_slots[] = {&goals::WinRule::teardown, &goals::SoleSurvivorWin::has_won_match,
                 stubs::return_true, &goals::SoleSurvivorWin::rule_satisfied, stubs::return_true};
void** const kLastManStandingVtable = reinterpret_cast<void**>(g_last_man_standing_vtable_slots);
static VtableSlot g_elimination_vtable_slots[] = {&goals::WinRule::teardown, &goals::EliminationVictoryCondition::has_won_match, stubs::return_true,
                 &goals::EliminationVictoryCondition::rule_satisfied, stubs::return_true};
void** const kEliminationVtable = reinterpret_cast<void**>(g_elimination_vtable_slots);
static VtableSlot g_collect_all_objects_vtable_slots[] = {&goals::WinRule::teardown, &goals::WinRule::is_highest_score, stubs::return_true,
                 &goals::CollectAllObjectsVictoryCondition::rule_satisfied, stubs::return_true};
void** const kCollectAllObjectsVtable = reinterpret_cast<void**>(g_collect_all_objects_vtable_slots);

// The types (type=).
constexpr uint32_t kHighestScore = 0x8F30FD26;      // highest_score
constexpr uint32_t kTargetScore = 0x71CD53A2;       // target_score
constexpr uint32_t kCompleteGoals = 0x6FC19399;     // complete_goals
constexpr uint32_t kLabelBestChain = 0x6FAD9811;         // best_combo
constexpr uint32_t kLastManStanding = 0xCC06880F;   // last_man_standing
constexpr uint32_t kElimination = 0xA6B8D7B0;       // elimination
constexpr uint32_t kCollectAllObjects = 0x72068889; // collect_all_objects

// The skate module (modules::Skate, 0x007CE478) and its skaters (not rewritten yet).
uint8_t* get_skate() { return reinterpret_cast<uint8_t*>(modules::g_skate_module); }
// How many skaters there are: the skate module's skater list (+0x34; nodes: +0x04 the skater, -1 at the end,
// +0x0C the next).
uint32_t skater_count(uint8_t* skate)
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
uint8_t* get_rider_at(uint8_t* skate, uint32_t index)
{
    return reinterpret_cast<uint8_t*>(reinterpret_cast<modules::Skate*>(skate)->get_rider_at(static_cast<int>(index)));
}
// A skater's score object (modules::PointTally) and its total.
uint8_t* get_score_of(uint8_t* skater) { return static_cast<uint8_t*>(reinterpret_cast<objects::Rider*>(skater)->get_points_total()); }
int total_score(uint8_t* score) { return reinterpret_cast<modules::PointTally*>(score)->get_total_score(); }

// The game mode (0x007CE470).
void* get_game_mode() { return static_cast<void*>(g_game_mode); }
bool is_squad_match() { return static_cast<MatchRules*>(get_game_mode())->is_squad_match() != 0; }
int current_num_teams() { return static_cast<MatchRules*>(get_game_mode())->current_num_teams(); }

// The network manager's players (0x007CCE14): PlayerRecord +0x14 the skater, +0x30 the team, +0x34 the objects
// collected. Walked with a lists::Hunt<PlayerRecord> (vtable 0x0064C1A8).
online::Hub* net_session_slot() { return online::g_online_hub; }
struct PlayerSearch {
    void* vtable_ = modules::kPlayerSearchVtable;
    void* list_link_ = nullptr;
};
uint8_t* start_roster_sweep(PlayerSearch* search)
{
    return static_cast<uint8_t*>(net_session_slot()->begin_player_walk(search, false));
}
uint8_t* step_roster_sweep(PlayerSearch* search)
{
    return static_cast<uint8_t*>(net_session_slot()->step_player_walk(search, false));
}
uint8_t* find_own_player_record() { return static_cast<uint8_t*>(net_session_slot()->get_own_player()); }
bool is_eliminated(uint8_t* player) { return reinterpret_cast<online::PlayerRecord*>(player)->is_eliminated(); }

} // namespace

// 0x005769A0
void WinRule::destroy_body() { vtable_ = kBaseVtable; }

// 0x00576970
WinRule* WinRule::teardown(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00576650
WinRule* WinRule::destroy_base(uint8_t flags)
{
    vtable_ = kBaseVtable;
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x005769B0
bool WinRule::is_highest_score(uint32_t player)
{
    uint8_t* skate = get_skate();
    for (uint32_t i = 0; i < skater_count(skate); ++i) {
        if (i == player)
            continue;
        uint8_t* mine = get_score_of(get_rider_at(skate, player));
        uint8_t* theirs = get_score_of(get_rider_at(skate, i));
        int their_score = total_score(theirs);
        if (total_score(mine) < their_score)
            return false;
    }
    return true;
}

// 0x00576A40
bool ScoreReachedVictoryCondition::rule_satisfied()
{
    uint8_t* skate = get_skate();
    if (is_squad_match()) {
        int totals[4] = {};
        PlayerSearch search;
        for (uint8_t* player = start_roster_sweep(&search); player; player = step_roster_sweep(&search)) {
            uint8_t* score = get_score_of(*reinterpret_cast<uint8_t**>(player + 0x14));
            int team = *reinterpret_cast<int*>(player + 0x30);
            totals[team] += total_score(score);
        }
        for (int total : totals) {
            if (total >= points_held_)
                return true;
        }
        return false;
    }
    for (uint32_t i = 0; i < skater_count(skate); ++i) {
        if (total_score(get_score_of(get_rider_at(skate, i))) >= points_held_)
            return true;
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
bool CompleteGoalsVictoryCondition::rule_satisfied()
{
    online::Hub* net = net_session_slot();
    uint8_t* skate = get_skate();
    GoalManager* goals = get_objective_board(); // the skate module's goal manager
    if (is_squad_match()) {
        int needed = goals->count_picked_objectives();
        for (int team = 0; team < current_num_teams(); ++team) {
            if (net->get_squad_points(team) >= needed)
                return true;
        }
        return false;
    }
    uint32_t count = *reinterpret_cast<uint32_t*>(goals); // (the goals there are: its first word - 0x00469490, a folded getter)
    for (uint32_t i = 0; i < skater_count(skate); ++i) {
        uint8_t* skater = get_rider_at(skate, i);
        if (count == 0)
            return true;
        bool missing = false;
        for (uint32_t g = 0; g < count; ++g) {
            uint8_t* goal = reinterpret_cast<uint8_t*>(goals->get_objective_at(static_cast<int>(g)));
            // A goal that doesn't count (+0x2C's value set, or 0x005542F0 false) or that the skater has won (vtable
            // +0x50, by the skater's id).
            if (**reinterpret_cast<int**>(goal + 0x2C) != 0 || !reinterpret_cast<Objective*>(goal)->is_picked())
                continue;
            auto has_won = reinterpret_cast<bool(__thiscall*)(uint8_t*, uint32_t)>((*reinterpret_cast<void***>(goal))[0x50 / 4]);
            if (has_won(goal, *reinterpret_cast<uint32_t*>(skater + 0x14)))
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
bool SoleSurvivorWin::rule_satisfied()
{
    uint8_t* player = find_own_player_record();
    if (player) {
        uint8_t* skater = *reinterpret_cast<uint8_t**>(player + 0x14);
        if (skater && total_score(get_score_of(skater)) <= 0)
            return true;
    }
    return false;
}

// 0x005766B0
bool SoleSurvivorWin::has_won_match(uint32_t player)
{
    uint8_t* score = get_score_of(get_rider_at(get_skate(), player));
    return score && total_score(score) > 0;
}

// 0x005766E0
bool EliminationVictoryCondition::rule_satisfied()
{
    uint8_t* player = find_own_player_record();
    return player && is_eliminated(player);
}

// 0x00576700
bool EliminationVictoryCondition::has_won_match(uint32_t player)
{
    online::Hub* net = net_session_slot();
    uint8_t* skater = get_rider_at(get_skate(), player);
    if (!skater)
        return false;
    uint8_t* info = net->find_player_slot_by_obj(
        static_cast<uint16_t>(*reinterpret_cast<uint32_t*>(skater + 0x14))); // the player with the skater's id
    return !is_eliminated(info);
}

// 0x00576740
bool CollectAllObjectsVictoryCondition::rule_satisfied()
{
    int total = 0;
    PlayerSearch search;
    for (uint8_t* player = start_roster_sweep(&search); player; player = step_roster_sweep(&search))
        total += *reinterpret_cast<int*>(player + 0x34);
    if (!total)
        return false;
    for (uint8_t* player = start_roster_sweep(&search); player; player = step_roster_sweep(&search)) {
        if (total_score(get_score_of(*reinterpret_cast<uint8_t**>(player + 0x14))) == total)
            return true;
    }
    return false;
}

// 0x00576810
WinRule* create_victory_condition(script::Struct* params)
{
    uint32_t type;
    params->read_hash_value("type", &type, 1);
    auto make = [](unsigned int size, void** vtable) {
        auto* condition = static_cast<WinRule*>(memory::alloc_zeroed(size));
        if (condition)
            condition->vtable_ = vtable;
        return condition;
    };
    switch (type) {
    case kTargetScore: {
        int score;
        params->fetch_int_value("score", &score, 1);
        auto* condition = static_cast<ScoreReachedVictoryCondition*>(make(8, kScoreReachedVtable));
        if (condition)
            condition->points_held_ = score;
        return condition;
    }
    case kLabelBestChain: {
        int combos;
        params->fetch_int_value("num_combos", &combos, 1);
        auto* condition = static_cast<BestComboVictoryCondition*>(make(8, kBestComboVtable));
        if (condition)
            condition->num_combos_ = combos;
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
