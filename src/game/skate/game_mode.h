#pragma once

// goals::MatchRules (the singleton 0x007CE470; 0x00552F70-0x00553DF9) - the game mode: free skate,
// career, the net games... its definition (mode_info's entry: the mode's structure, then options overridden) and its
// victory conditions. goals::MatchFlow (0x00552E10-0x00552F6F): the skate module's game flow script, run until done.

#include <cstddef>
#include <cstdint>

#include "engine/core/owner.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/script/param_struct.h"

namespace script {
class Script;
} // namespace script

namespace goals {

class WinRule;

// The game flow: a script started anew when one is requested.
class MatchFlow : public memory::HeapObject {
public:
    // 0x00552E10: no script.
    MatchFlow() = default;
    // 0x00552F30: the script deleted.
    ~MatchFlow();
    MatchFlow(const MatchFlow&) = delete;
    MatchFlow& operator=(const MatchFlow&) = delete;

    // 0x00552E30: script `name` to be started (by the next Update).
    void get_request(uint32_t name);
    // 0x00552E40: a requested script started (the old one deleted); the script updated - deleted once it's done.
    void tick_frame();

private:
    script::Script* own_script_ = nullptr;
    bool pending_ = false; // label_ to be started
    uint32_t label_ = 0;
};

class MatchRules : public memory::HeapObject {
public:
    // The victory conditions' list (an intrusive ring; the head's condition is -1). The original's list types have
    // a vtable each (the head's 0x0064DD90, the nodes' 0x0064DD54) holding only their destructors.
    struct ConditionNode {
        memory::owner<WinRule*> condition_; // (-1 for the head; the intrusive list's nodes own their conditions)
        int rank_;
        ConditionNode* succ_;
        ConditionNode* last_value_;

        // 0x00553710: out of its list (linked to itself).
        void unlink();
    };

    script::Struct args_;      // the mode's definition (Name, the options)
    ConditionNode conditions_; // the head

    // 0x00553DB0 (cdecl): the singleton (made with `create`, counted); 0x00552F70: one reference let go (the last
    // deletes it).
    static MatchRules* sole(bool create);
    static void get_release();
    // 0x00553D30: no conditions, the free skate mode. 0x00553890 / 0x00553CE0: the conditions deleted.
    MatchRules();
    ~MatchRules();
    MatchRules(const MatchRules&) = delete;
    MatchRules& operator=(const MatchRules&) = delete;
    // 0x00553D10: back to free skate.
    void get_reset();
    // 0x00553AD0: the mode `type` of mode_info loaded (its definition's structure); in a net game max_players
    // kept. False if there's no such mode.
    bool apply_match_style(uint32_t type);
    // 0x00553BE0: `options` added over the definition (time_limit_type, victory_condition_type and screenmode
    // replaced); the conditions made again. True.
    bool force_match_settings(script::Struct* options);
    // 0x00553770 / 0x005537D0: the victory conditions deleted / made from victory_conditions (in the skater info
    // heap).
    void clear_victory_conditions();
    void build_victory_conditions();
    // 0x00553910: the run is over by the conditions: not elimiskate / scavenger hunt, with a clock, while the goal
    // manager's active net goal... is out of time - or every terminal condition complete.
    bool finish_rules_reached();
    // 0x00553A60: the first victory condition's setting (its target_value; 0: no conditions).
    int get_first_condition_value();

    // The definition's options.
    bool keeps_running_total();                 // 0x00552FB0 accumulate_score
    bool keeps_best_chain();                    // 0x00552FD0 track_best_combo (none: false)
    void set_score_accumulation(bool on);       // 0x00553000
    void hold_points(bool on);                  // 0x00553020
    void set_score_degradation(bool on);        // 0x00553040
    void set_trick_score_tracking(bool on);     // 0x00553060
    void set_team_graffiti_mode(uint32_t mode); // 0x00553080
    void set_goals_mode(uint32_t mode);         // 0x005530A0
    int is_scoring_paused();                    // 0x005530C0 score_frozen (0 / 1 in EAX)
    bool keeps_move_points();                   // 0x005530F0 track_trick_score
    bool repeats_lose_points();                 // 0x00553110 degrade_score
    int current_num_teams();                    // 0x00553130 num_teams
    bool clock_beeps_near_end();                // 0x00553150 timer_beeps
    int get_time_limit();                       // 0x00553170 default_time_limit
    int get_starting_rider_count();             // 0x00553190 initial_players
    void set_rider_ceiling(int players);        // 0x005531B0
    int get_rider_ceiling();                    // 0x005531D0 max_players
    int get_fireball_level();                   // 0x005531F0 fireball_level (3)
    bool is_true(uint32_t name);                // 0x00553220 the integer `name` is non-zero
    void set_round(int round);                  // 0x00553250
    int get_round();                            // 0x00553270
    uint32_t get_name_key();                    // 0x005532A0 Name
    uint32_t get_goals_mode();                  // 0x005532D0 goals
    uint32_t get_team_graffiti_mode();          // 0x00553300 team_graffiti_mode
    // 0x00553330: a net mode's name as its single player one (nettrickattack -> trickattack...).
    uint32_t remap_online_name_key(uint32_t name);
    // 0x005533B0 / 0x00553400: the mode's goal's time left (ms; none: 0) / set (seconds).
    int get_goal_time_left();
    void set_goal_time_left(int seconds);
    // 0x00553460: the mode runs on a clock (not king / score challenge / create goals / firefight; a time limit).
    bool uses_countdown();
    // 0x00553550: the run stops at zero (stop_at_zero; a multiplayer game with a clock only - else true).
    bool clock_halts_at_zero();
    // 0x005535A0: num_teams (a net lobby makes the team flags). 0x005535F0: a team game (0 / 1 in EAX).
    void set_num_teams(int teams);
    int is_squad_match();
    // 0x00553630: the screen mode: 0 single, 1 / 2 split screen vertical / horizontal, 4 horse.
    int get_view_layout();
    // 0x005536D0: the round counted up.
    void increment_round();
};

// 0x00553C90: OverrideGameModeOptions - the game mode's options overridden by the command's parameters.
bool __cdecl cfunc_override_game_mode_options(script::Struct* params, script::Script* script);

extern goals::MatchRules* g_game_mode;

} // namespace goals
