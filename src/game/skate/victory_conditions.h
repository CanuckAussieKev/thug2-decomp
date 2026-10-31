#pragma once

#include <cstdint>

#include "engine/core/mem.h"

// goals::WinRule: how a game mode is won - a goal's victory_conditions entries (type= highest_score,
// target_score, complete_goals, best_combo, last_man_standing, elimination, collect_all_objects) are made into
// these by create_victory_condition, and the game asks them who won and whether the game is over.
// From the disassembly of 0x00576610-0x00576CA0 (the original's vtables: 0x0064EAA8 the base's, 0x0064EB0C-0x0064EB84
// the seven types'; the shared slots are stubs returning true / false).
namespace script {
class Struct;
}

namespace goals {

class WinRule : public memory::HeapObject {
public:
    // 0x00576970 / 0x00576650: the scalar deleting destructors (the subclasses' / the base's; nothing to free).
    virtual ~WinRule() = default;

    // Whether skater `player` has won. The base's answer: no.
    virtual bool has_won_match(uint32_t player);
    // Whether meeting this condition ends the game.
    virtual bool is_terminal() const = 0;
    // Whether the condition is met.
    virtual bool rule_satisfied() = 0;
    // A debug print of the condition. True.
    virtual bool echo_out();
    // The condition's setting: target_score's score, best_combo's num_combos (0 for the others).
    virtual int target_value() const;

protected:
    // 0x005769B0: no other skater has a higher score than skater `player` (the has_won_match of highest_score,
    // target_score, best_combo and collect_all_objects).
    static bool is_highest_score(uint32_t player);
};

// highest_score (vtable 0x0064EB0C): the highest score wins; never completes by itself.
class TopPointsWin : public WinRule {
public:
    bool has_won_match(uint32_t player) override { return is_highest_score(player); }
    bool is_terminal() const override { return false; }
    bool rule_satisfied() override { return false; }
};

// target_score (vtable 0x0064EB20): a skater (or, in a team game, a team) has `score` points.
class ScoreReachedVictoryCondition : public WinRule {
public:
    explicit ScoreReachedVictoryCondition(int points) : points_held_(points) {}
    bool has_won_match(uint32_t player) override { return is_highest_score(player); }
    bool is_terminal() const override { return true; }
    // 0x00576A40
    bool rule_satisfied() override;
    // 0x00576610: "Found CScoreReachedVictoryCondition %d".
    bool echo_out() override;
    int target_value() const override { return points_held_; }

private:
    int points_held_; // score=
};

// complete_goals (vtable 0x0064EB34): a skater (or team) has completed every goal; nobody "wins" by score.
class CompleteGoalsVictoryCondition : public WinRule {
public:
    bool is_terminal() const override { return true; }
    bool rule_satisfied() override { return all_goals_complete(); }
    // 0x00576B80 (and the jump to it, 0x00576CA0): a skater (or team) has completed every goal. It doesn't use its
    // object - the goal attack's net goal asks it directly (goal_net_types.cpp).
    static bool all_goals_complete();
};

// best_combo (vtable 0x0064EB48): the highest score wins; never completes by itself.
class BestComboVictoryCondition : public WinRule {
public:
    explicit BestComboVictoryCondition(int combos) : num_combos_(combos) {}
    bool has_won_match(uint32_t player) override { return is_highest_score(player); }
    bool is_terminal() const override { return false; }
    bool rule_satisfied() override { return false; }
    // 0x00576630: "Found CBestComboVictoryCondition %d".
    bool echo_out() override;
    int target_value() const override { return num_combos_; }

private:
    int num_combos_; // num_combos=
};

// last_man_standing (vtable 0x0064EB5C).
class SoleSurvivorWin : public WinRule {
public:
    // 0x005766B0: skater `player` still has a score.
    bool has_won_match(uint32_t player) override;
    bool is_terminal() const override { return true; }
    // 0x00576680: the local player has no score left.
    bool rule_satisfied() override;
};

// elimination (vtable 0x0064EB70).
class EliminationVictoryCondition : public WinRule {
public:
    // 0x00576700: skater `player`'s player isn't eliminated.
    bool has_won_match(uint32_t player) override;
    bool is_terminal() const override { return true; }
    // 0x005766E0: the local player is eliminated.
    bool rule_satisfied() override;
};

// collect_all_objects (vtable 0x0064EB84).
class CollectAllObjectsVictoryCondition : public WinRule {
public:
    bool has_won_match(uint32_t player) override { return is_highest_score(player); }
    bool is_terminal() const override { return true; }
    // 0x00576740: a skater's score is every player's objects (+0x34) together (and there are some).
    bool rule_satisfied() override;
};

// 0x00576810: a new condition from `params`' type= (null for an unknown type or out of memory).
WinRule* create_victory_condition(script::Struct* params);

} // namespace goals
