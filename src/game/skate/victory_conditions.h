#pragma once

#include <cstdint>

// goals::WinRule: how a game mode is won - a goal's victory_conditions entries (type= highest_score,
// target_score, complete_goals, best_combo, last_man_standing, elimination, collect_all_objects) are made into
// these by create_victory_condition, and the game asks them who won and whether the game is over.
// From the disassembly of 0x00576610-0x00576CA0 (methods __thiscall; the vtables 0x0064EAA8 - the base - and
// 0x0064EB0C-0x0064EB84, five slots each: 0 the scalar deleting destructor, 1 has_won_match(player), 2 whether meeting
// the condition ends the game (shared stubs returning true / false), 3 rule_satisfied(), 4 a debug print that
// returns true).
namespace script {
class Struct;
}

namespace goals {

class WinRule {
public:
    void** vtable_;

    // 0x005769A0: the destructor body (the base vtable back). 0x00576970: the subclasses' scalar deleting
    // destructor; 0x00576650: the base's (the same, inline).
    void destroy_body();
    WinRule* teardown(uint8_t flags);
    WinRule* destroy_base(uint8_t flags);

    // 0x005769B0: no other skater has a higher score than skater `player` (the has_won_match of highest_score,
    // target_score, best_combo and collect_all_objects).
    bool is_highest_score(uint32_t player);
};

// highest_score (vtable 0x0064EB0C): the highest score wins; never completes by itself.
class TopPointsWin : public WinRule {};

// target_score (vtable 0x0064EB20): a skater (or, in a team game, a team) has `score` points.
class ScoreReachedVictoryCondition : public WinRule {
public:
    int points_held_; // +0x04 score=
    // 0x00576A40
    bool rule_satisfied();
    // 0x00576610: "Found CScoreReachedVictoryCondition %d".
    bool echo_out();
};

// complete_goals (vtable 0x0064EB34): a skater (or team) has completed every goal; nobody "wins" by score.
class CompleteGoalsVictoryCondition : public WinRule {
public:
    // 0x00576B80 (and the jump to it, 0x00576CA0)
    bool rule_satisfied();
};

// best_combo (vtable 0x0064EB48): the highest score wins; never completes by itself.
class BestComboVictoryCondition : public WinRule {
public:
    int num_combos_; // +0x04 num_combos=
    // 0x00576630: "Found CBestComboVictoryCondition %d".
    bool echo_out();
};

// last_man_standing (vtable 0x0064EB5C).
class SoleSurvivorWin : public WinRule {
public:
    // 0x00576680: the local player has no score left.
    bool rule_satisfied();
    // 0x005766B0: skater `player` still has a score.
    bool has_won_match(uint32_t player);
};

// elimination (vtable 0x0064EB70).
class EliminationVictoryCondition : public WinRule {
public:
    // 0x005766E0: the local player is eliminated.
    bool rule_satisfied();
    // 0x00576700: skater `player`'s player isn't eliminated.
    bool has_won_match(uint32_t player);
};

// collect_all_objects (vtable 0x0064EB84).
class CollectAllObjectsVictoryCondition : public WinRule {
public:
    // 0x00576740: a skater's score is every player's objects (+0x34) together (and there are some).
    bool rule_satisfied();
};

// 0x00576810: a new condition from `params`' type= (null for an unknown type or out of memory).
WinRule* create_victory_condition(script::Struct* params);

} // namespace Game
