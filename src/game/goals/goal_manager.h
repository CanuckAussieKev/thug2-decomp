#pragma once

// goals::CGoalManager (0x39C bytes; the skate module's, 0x005591B0): every goal of the level in a lookup table by
// goal id, the active ones in a list, the saved state of each goal (also of goals not in this level) by id, the
// manager's own parameters (the difficulty level by game mode), which goal groups are unlocked in each level, the
// goal points. Its methods are 0x00557E30-0x005591BF and 0x0055B990-0x0055E980 (goal_manager*.cpp); its script
// commands (GoalManager_...) 0x005591C0-0x0055B98F (goal_manager_cfuncs*.cpp).

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/lists/lookup_table.h"
#include "engine/core/checksums.h"
#include <gsl/pointers>

namespace script {
class Struct;
} // namespace script

namespace goals {

class Objective;

// The active goals' list head (lists::Foremost<Objective>: data -1, linked to itself while empty; the original's size).
struct GoalListHead {
    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }

private:
    memory::ListNode list_link_;

public:
    // 0x0055B9F0: the destructor body: unlinked. (0x0055D960, its table's entry - the same, freed - isn't called.)
    void destroy_body();
    // 0x0055D990 (lists::Foremost<Objective>::fetch_entry): the node at `index`, or null past the end. (Each node's data is checked
    // for -1 before the node for null, as the original.)
    memory::ListNode* get_node_at(int index);
};

class GoalManager {
public:
    static constexpr int kGroupsPerLevel = 10;
    static constexpr int kLevels = 21;

    // The fields other code reads and sets.
    lists::TypedLookupTable<Objective>& goals() { return goals_; }
    [[nodiscard]] const lists::TypedLookupTable<Objective>& goals() const { return goals_; }
    [[nodiscard]] script::Struct* goal_data() const { return goal_data_; }
    [[nodiscard]] uint32_t previous_objective() const { return previous_objective_; }
    void set_previous_objective(uint32_t value) { previous_objective_ = value; }
    void set_tagging_style(int value) { tagging_style_ = value; }
    int& objective_score() { return objective_score_; }
    [[nodiscard]] int objective_score() const { return objective_score_; }
    int& total_goal_points() { return total_goal_points_; }
    [[nodiscard]] int total_goal_points() const { return total_goal_points_; }
    [[nodiscard]] bool all_unlocked() const { return all_unlocked_; }
    [[nodiscard]] bool start_allowed() const { return start_allowed_; }
    void set_start_allowed(bool value) { start_allowed_ = value; }
    [[nodiscard]] uint32_t backer() const { return backer_; }
    void set_backer(uint32_t value) { backer_ = value; }
    [[nodiscard]] script::Struct* team() const { return team_; }
    [[nodiscard]] script::Struct* args() const { return args_; }

private:
    lists::TypedLookupTable<Objective> goals_{};    // +0x00
    script::Struct* goal_data_{};                   // +0x14: each goal's saved state (a structure by goal id), and the
                                                    // GoalManager_Params component (the manager's difficulty_levels)
    GoalListHead active_goals_{};                   // +0x18: the active goals
    uint32_t previous_objective_{0};                // +0x2C: the last goal started that can be retried (0: none)
    uint32_t unknown30_{};                          // +0x30 (not set by the constructor)
    int tagging_style_{0};                          // +0x34 (GoalManager_SetGraffitiMode)
    int objective_score_{0};                        // +0x38: counted up with each goal points shown
    int total_goal_points_{0};                      // +0x3C
    int goals_beaten_{0};                           // +0x40: the goals beaten (the last of a chain, not edited goals)
    bool all_unlocked_{false};                      // +0x44: every goal seen / every pro goal unlocked (the cheats)
    bool start_allowed_{true};                      // +0x45 (true; GoalManager_SetCanStartGoal)
    uint32_t backer_{0};                            // +0x48 (GoalManager_SetSponsor)
    script::Struct* team_{};                        // +0x4C: the team (a component per member: 1 if used)
    std::array<int, kLevels * kGroupsPerLevel> group_unlocked_{}; // +0x50: level * 10 + group - 1 (groups are numbered from 1)
    script::Struct* args_{};                          // +0x398: the difficulty level by game mode name

public:

    // 0x0055D9C0: empty tables and list, three new structures, every group locked.
    GoalManager();
    // 0x0055DB50: every goal removed, the saved state deleted, the active list emptied, the table cleared. (team_
    // and args_ aren't deleted, as the original.)
    ~GoalManager();
    GoalManager(const GoalManager&) = delete;
    GoalManager& operator=(const GoalManager&) = delete;

    // 0x00557E30
    bool may_begin_objective();
    // 0x00557E40: the goal points counted up (both counts) and GoalManager_ShowGoalPoints run.
    void show_goal_points();
    // 0x00557E70: the saved state reset: with `flag` 0 everything (the manager's parameters cleared, every saved goal
    // dropped), else only the saved goals whose structure has `flag` (and that flag of the manager's parameters);
    // the GoalManager_Params component then holds the manager's (cleared) difficulty_levels. The counts and the
    // cheat flag cleared.
    void reset_goal_data(uint32_t flag);
    // 0x00557F90: the goal with id `id`, or null. (`assert` isn't read.) 0x00557FB0: the goal at `index`.
    Objective* get_goal(uint32_t id, int assert);
    Objective* get_objective_at(int index);
    // 0x00557FC0: goal `id` started (not if locked); a goal that can be retried and starts a chain becomes the
    // last goal. Whether it started. (`unused` isn't read.)
    bool engage_objective(uint32_t id, bool unused);
    // 0x00558030: goal `id` won: not the last goal any more if it ends a chain started by the last goal; a goal
    // not won before - the end of a chain - counted (not edited goals) or, in a network game, the server told
    // (message 0x6E with the chain's first goal). Whether there was such a goal.
    bool complete_objective(uint32_t id);
    // 0x00558180: goal `id` lost - in classic (also a goal attack of classic goals) started again. Whether there was
    // such a goal.
    bool fail_objective(uint32_t id);
    // 0x00558200: the goals selected (for a network game).
    int count_picked_objectives();
    // 0x00558250: every active goal stopped (`unused` passed on; each tree reset).
    void disengage_every_objective(bool unused);
    // 0x005582B0: every goal not active, not won and not a network goal started.
    void activate_all_goals();
    // 0x00558320: the chain goal `id` is in (from its first goal) uninitialized, children too.
    void release_objective_tree(uint32_t id);
    // 0x00558380 / 0x005583C0: every goal uninitialized / initialized (in a network game only the network goals;
    // not locked ones; won ones in classic, always_initialize_goal or edited_goal - only the first of each chain).
    void release_every_objective();
    void prepare_every_objective();
    // 0x005584C0: every selected goal initialized (goal_create_proset_geom run for it first). 0x00558530: every
    // selected goal started.
    void prepare_picked_objectives();
    void activate_all_selected_goals();
    // 0x00558580 / 0x005585C0 / 0x00558600: every goal deselected / paused / unpaused.
    void unpick_every_objective();
    void freeze_every_objective();
    void thaw_every_objective();
    // 0x00558640: goal `id` started with its quick_start flag set (cleared after); the last goal if it can be
    // retried. Whether it started; false if there's no such goal and `check_exists`.
    bool begin_objective_at_once(uint32_t id, bool check_exists);
    // 0x005586A0: goal `id`'s flag `flag` set to `value` (of skater structure `sub`). False if no such goal.
    bool raise_bit(uint32_t id, checksums::Name flag_name, int value, checksums::Name sub_goal);
    // 0x005586E0: gap `gap` passed to every goal (register_gap_hit). The last one's answer.
    bool register_gap_hit(uint32_t gap);
    // 0x00558730 / 0x00558750: goal `id`'s parameters / its next waypoint started.
    script::Struct* get_objective_args(uint32_t id);
    bool step_to_next_waypoint(uint32_t id);
    // 0x00558770: every active graffiti goal told cluster `cluster` was taken with `score` (not network goals).
    void kill_cluster(uint32_t cluster, int score);
    // 0x005587F0: every active goal's tricks checked: special (or create_a_trick) goals won once their trick
    // string is done (not special_gap ones), tetris goals' done tricks removed, find-gaps goals' gaps counted.
    void check_trick_goals();
    // 0x005588D0: goal `id` unlocked.
    void open_up_objective(uint32_t id);
    // 0x005588F0: goal `id` is beaten - or, not in this level, its saved hasBeaten.
    int has_beaten_goal(uint32_t id);
    // 0x00558960: goal `id` selected if it wasn't, else deselected.
    void flip_objective_pick(uint32_t id);
    // 0x005589A0: a goal of proset `prefix` is beaten.
    bool has_won_proset_goal(const char* prefix);
    // 0x00558A00: the saved state given to the goals (each its structure; the manager its difficulty_levels).
    void restore_goal_data();
    // 0x00558AB0 / 0x00558B10: the goals (the first of each chain) whose stage / network stage bit `bit` is set
    // (the player who beat them). 0x00558B70: the goals (the first of each chain) beaten.
    int count_cleared_by_player(int bit);
    int num_goals_net_beaten_by(int bit);
    int count_objectives_cleared();
    // 0x00558BC0: every goal unlocked (marked seen). 0x00558C00: every locked pro goal unlocked. Both set the cheat
    // flag.
    void open_up_every_objective();
    void unlock_all_pro_goals();
    // 0x00558C70: a goal's pro speaks: `params`' Name is the goal, VO a stream (else type, a type of stream).
    void play_objective_voice_line(script::Struct* params);
    // 0x00558D20: goal `id` is locked - or, not in this level, its saved isLocked.
    int is_goal_blocked(uint32_t id);
    // 0x00558D90: the active competition goal, or null.
    Objective* get_active_competition();
    // 0x00558DF0: every goal's texts redone with its tricks.
    void replace_trick_text_all();
    // 0x00558E30: there's a goal `id`.
    bool goal_exists(uint32_t id);
    // 0x00558E50: the local skater's trick objects (the clusters of its combo, up to 512) passed to kill_cluster
    // with its score's player.
    void test_tagged_props();
    // 0x00558ED0: each goal made a parent of its children, then the inherited flags passed down from every chain's
    // first goal.
    void build_goal_tree();
    // 0x00558FD0: every goal's create_geometry nodes created.
    void spawn_objective_props();
    // 0x00559050: the difficulty level (career: the career's; else the game mode's) - 0, 1 or 2 (1 if none, or
    // another value).
    int get_skill_tier(bool career);
    // 0x005590C0: every goal's pro hidden (or shown).
    void conceal_every_objective_npc(bool hide);
    // 0x00559110: group `group` of level `level` (-1: the career's) is locked; group -1: never.
    bool is_group_locked(int group, int level);
    // 0x00559150: every trick objects goal told cluster `cluster` was hit.
    void trick_object_hit(uint32_t cluster);

    // --- goal_manager_goals.cpp ---
    // 0x0055BA10: a goal `id` made from `params` (its type by the flags: competition, net, race, tetris, Horse,
    // findGaps, Film, TrickObjects, SkateTheLine, TimedScoreRun, TrickBeat, else a plain goal; its goal_id added),
    // into the table, its difficulty level's parameters, its children / child; in a multiplayer game its pro the
    // NetJudge. False (nothing made) in free skate out of multiplayer, for a goal not edited in create-a-goal mode,
    // for career_only in multiplayer; else whether one was made.
    bool enrol_objective(uint32_t id, script::Struct* params);
    // 0x0055BF40: the last goal started again (quick start): every goal stopped, the scripts of a killed skater
    // killed, kill_blur; its trigger reset (GoalManager_ResetGoalTrigger) unless it's won (dont_kill_trigger_on_success
    // anyway).
    void retry_previous_objective();
    // 0x0055C090: each goal's state saved into the saved goals (not null_goal or edited goals), the manager's
    // difficulty levels as GoalManager_Params - not in a network game.
    void level_unload();
    // 0x0055C250: the saved goals of level `level` (levelnum) beaten, after LevelUnload. (The second argument isn't
    // read.)
    int count_cleared_in_stage(int level, int unused);
    // 0x0055C2F0: a menu item (view_selected_goals_menu_set_events / choose_goals_menu_set_events) for each goal with
    // view_goals_text (not network goals, only the first of a chain): only selected ones, without edited ones, or only
    // edited ones.
    void append_objective_options(bool selected_only, bool skip_edited, bool edited_only);
    // 0x0055C4D0: `text` into `out` with goal `id`'s key combos' tricks for its \t / \k tokens. False (nothing
    // written) if a key combo has no trick.
    bool swap_move_caption(uint32_t id, const char* text, char* out, int size);
    // 0x0055C6E0: a new structure with the times the beaten goals were started (0xD023ADBE; not network goals).
    gsl::owner<script::Struct*> get_objective_try_details();

    // --- goal_manager_events.cpp ---
    // 0x0055C7E0: the skater landed: the trick objects checked, then each active goal - a horse goal's own land (and
    // no further goals), high_combo's goal_highcombo_check_score, score_goal won with the score (winning_score while
    // it wins), the trick objects / trick beat / skate line goals', arcade_current_score_total raised, a combo goal
    // whose combo started lost next frame.
    void land();
    // 0x0055CBB0: the skater bailed: each active combo goal whose combo started lost next frame, tetris goals'
    // trick_flag cleared, trick beat goals' landed tricks cleared.
    void get_bail();
    // 0x0055CD70: the view goals menu's info into `out`'s view_goals_info: each goal's (id, beaten, locked, reward
    // points, texts, description, tip, cameras) grouped by its pro - every pro's array (a structure), or with
    // `skater` that pro's goals' array alone.
    void get_view_goals_info(script::Struct* out, uint32_t skater);

    // --- goal_manager_update.cpp ---
    // 0x0055DD10: the_time's text: the first active goal with a timer shown (slot +0x28)'s time left as m:ss (else
    // empty).
    void update_time_display();
    // 0x0055DEA0: an active goal has required_gaps.
    bool has_active_gap_goal();
    // 0x0055DF90: every goal not beaten any more (out of a network game the counts too, and the saved goals
    // cleared); the goal points shown if no goal is active, out of a multiplayer game.
    void revoke_every_clear();
    // 0x0055E050: the view goals menu's items (view_goals_menu_add_item): the goals with a place in the list first,
    // in its order, then the rest; their records if won (a competition's place, a score, a time).
    void append_objective_list_entries();
    // 0x0055E790: `trick` landed, told to every active trick beat goal.
    void trick_landed(script::Struct* trick);
    // 0x0055E980: the first active network goal, or null.
    Objective* get_active_net_goal();
    // 0x0055EA40: every active goal updated; goals in state 0x02 removed and deleted; the timer shown.
    void tick_frame();

    // 0x0055BEA0: goal `id` removed (its pro destroyed, the goal deleted; not the last goal any more). True.
    bool drop_objective(uint32_t id);
    // 0x0055BEF0: every goal removed (no last goal).
    void wipe_objectives();
    // 0x0055DBE0: `goal` added to the active list (by its node's priority).
    void add_active_goal(Objective* goal);
    // 0x0055DBF0: the active goal at `index` (null if the list is empty).
    Objective* get_engaged_objective(int index);
    // 0x0055DC30: the active goals counted: network goals with `all`, the others with `all` or if active.
    int count_live_goals(bool all);
};

// 0x005591B0: the skate module's goal manager.
GoalManager* get_objective_board();

} // namespace goals
