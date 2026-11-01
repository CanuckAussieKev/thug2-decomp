#pragma once

// goals::Objective (0x70 bytes; 0x00553E00-0x00557ABF): the goal base class. Every goal of a level is one (or one of its
// types: competition, film, find gaps, horse, net, race, skate line, tetris, trick beat... - their own files): its
// parameters (the goal's script structure, also its state as flags and integers), its flags structure (the
// goal_flags that must be set to win), the goals linked to it (children it starts, the parent that started it), its
// timer, its records. The goal manager (goal_manager.h) owns them, in a lookup table by goal id, and drives them
// from its script commands and its update.
//
// A real C++ class (the original's vtables: Objective's 0x0064DDD0, each type's its own). The virtual functions are
// the slots whose bodies differ between the types' tables; the slots with one body in every table are plain methods.

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"

namespace script {
class Array;
class Script;
class Struct;
} // namespace script

namespace goals {

class ObjectiveNpc;

// A link to another goal by id (0xC bytes): the children (+0x28) and the parents (+0x2C) are singly linked lists of
// these; a child's link is marked when that child is won (Win, 0x005576D0).
struct GoalEdge {
    uint32_t objective_key_; // +0x00 (0: the list's first link, not yet used)
    GoalEdge* succ_;         // +0x04
    bool won_;               // +0x08
};

// The original's vtable slots (0x0064DDD0): 0 the destructor, 1 wants_to_end, 2 has_run_out, 3 is_operative, 4 a jump
// to slot 3, 5 expire, 6 / 7 restore / capture_save_block, 8 has_time_limit, 9 timer_beeps, 10 shows_clock, 11 activate,
// 12 change_live, 13 disengage, 14 clear_engaged_bit, 15 get_win, 16 lose, 17 tick_frame, 18 is_frozen, 19 set_flag,
// 20 stage_flag_set, 21 has_won, 22 net_stage_flag_set, 23 has_won_goal_or_child, 24 is_prepared, 25 may_try_again,
// 26 mark_won, 27 / 28 set_stage_flag / set_net_stage_flag, 29 un_beat. Made zeroed from the current heap (the goal
// manager's alloc_zeroed).
class Objective : public memory::HeapObject {
public:
    // +0x6C (flag_bits_)
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): bit flags, or-ed together
    enum Flags : uint8_t {
        kFlagInitialized = 0x01,  // Init / Uninitialize
        kFlagSeen = 0x02,         // the player has started it (hasSeen)
        kFlagWon = 0x04,          // beaten (out of the goal attack / lobby net games)
        kFlagWonInNetGame = 0x08, // beaten in a goal attack / net lobby game
        kFlagSelected = 0x10,     // chosen for a net game
        kFlagLocked = 0x20,       // locked until another goal unlocks it (unlocked_by_another, pro_goal)
        kFlagDeactivateOnExpire = 0x40,
        kFlagEndOfRunStarted = 0x80, // the skater's end of run started by the goal (has_run_out)
    };
    // +0x6D (cur_phase_)
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
    enum State : uint8_t {
        kStateActive = 0x01,
        kState02 = 0x02, // (read by 0x005543E0, never set in this range)
        kStatePaused = 0x04,
        kStateNoTimeLimit = 0x10,
    };

    // +0x04: the goal's node in the goal manager's list of active goals (data this, priority 0, linked to itself while
    // not in the list; Deactivate unlinks it). (The original's goal was its own node: the node's table word was the
    // goal's vtable; here the vptr is at +0 and the node follows it, in the original's place.)
    memory::ListNode list_link_;
    ObjectiveNpc* ped_;          // +0x14: the goal's pro (0x14 bytes)
    uint32_t inherited_;         // +0x18: passed to the children (2: single_timer, 4: retry_stage)
    script::Struct* args_;       // +0x1C
    script::Struct* goal_flags_; // +0x20: goal_flags, each an integer (1: set); skater_flags: a structure each
    int run_ending_;             // +0x24: 0 the goal's own end of run (goal_run_done...), 1 the normal one
    GoalEdge* sub_nodes_;        // +0x28
    GoalEdge* parents_;          // +0x2C
    uint32_t skater_;            // +0x30: skater (none 0x806FFF30)
    int level_;                  // +0x34: the career's level when made
    int bundle_;                 // +0x38: group_number (-1: none) - locked while the manager's group is
    uint32_t stage_flags_;       // +0x3C: bits by number (set_stage_flag)
    uint32_t net_stage_flags_;   // +0x40
    int times_started_;          // +0x44
    int best_time_;              // +0x48: ms (record_type time; 0x7FFFFFFF none)
    int record_points_;          // +0x4C (record_type score)
    int seconds_remaining_;      // +0x50: ms
    uint32_t unknown54_;
    int num_flags_;     // +0x58
    int num_flags_set_; // +0x5C (also the number collected without number_collected)
    int elapsed_;       // +0x60: ms since activated
    int unknown64_;     // +0x64 (-1)
    int unknown68_;     // +0x68 (-1)
    uint8_t flag_bits_; // +0x6C (Flags)
    uint8_t cur_phase_; // +0x6D (State)
    uint8_t pad6e_[2];

    // 0x00556FE0: a copy of `params` (the goal's own), its pro, an empty flags structure, the links' first entries,
    // the career's level, group_number, the goal flags, single_timer / retry_stage (passed to the children), skater,
    // locked by unlocked_by_another / pro_goal. (inherited_ isn't cleared first, as the original: the memory is.)
    explicit Objective(script::Struct* params);
    Objective(const Objective&) = delete;
    Objective& operator=(const Objective&) = delete;
    // 0x00556110 (0x005566A0 the deleting destructor): the destroy script, the pro, the structures, the links freed,
    // the node unlinked.
    virtual ~Objective();

    // The goal's node in the active list (see list_link_).
    memory::ListNode* list_node() { return &list_link_; }

    // 0x00553E00: this goal's inherited bits passed to every child, down the tree. 0x00553E50: the children's links'
    // won marks cleared, down the tree. (The depth argument is only passed on.)
    void pass_inherited_flags(int depth);
    void clear_child_wins();
    // 0x00553E90
    script::Struct* fetch_args();
    // 0x00553EA0: the timer from time (120 if none) seconds, plus 20 ms - unless unlimited_Time is 1.
    void reset_timer();
    // 0x00553F10 / 0x00553F20 / 0x00553F30: the time left set (seconds, no extra 20 ms) / read (ms) / added to.
    void set_seconds_remaining(int seconds);
    int get_seconds_remaining();
    void advance_clock(int seconds);
    // 0x00553F50 / 0x00553F60 (vtable +0x30 / +0x38): the active bit.
    virtual void change_live();
    virtual void clear_engaged_bit();
    // 0x00553F70: `params` added to the goal's parameters. False.
    bool rewrite_args(script::Struct* params);
    // 0x00553F90 (vtable +0x0C)
    bool is_operative();
    // 0x00553FA0
    bool has_seen();
    // 0x00553FB0 / 0x00553FC0
    void get_freeze();
    void un_pause();
    // 0x00553FD0: goal flag `flag` (of the skater_flags structure `sub`, if any) is there and equals `value`.
    bool bit_matches(uint32_t flag, int value, uint32_t sub);
    // 0x00554030 (cdecl): every flag of `flags` but num_flags cleared.
    static void clear_flag_values(script::Struct* flags);
    // 0x00554070: every goal flag cleared (each skater's structure's), none set.
    void zero_objective_bits();
    // 0x00554110: enough flags set to win (num_flags_to_win, else all; every skater's structure's all).
    bool has_every_bit();
    // 0x005541E0: goal flag `flag` (of `sub`'s structure, if any) is non-zero (asserted).
    bool bit_is_on(uint32_t flag, uint32_t sub);
    // 0x00554220: locked (its own lock, or its group's - the goal manager's).
    bool is_latched();
    // 0x00554250 / 0x00554260 / 0x00554270: the Graffiti, special and tetris flags.
    bool is_graffiti();
    bool is_special();
    bool is_tetris();
    // 0x00554280: goal_id (asserted).
    uint32_t get_objective_key();
    // 0x005542B0 / 0x00554300: the edited_goal flag (Create-a-Goal's).
    bool is_user_made_objective();
    bool is_edited_goal2();
    // 0x005542C0 (vtable +0x5C): won, or one of its children's links marked.
    bool has_won_goal_or_child();
    // 0x005542F0
    bool is_picked();
    // 0x005519F0 (vtable +0x60): initialised (Init, until Uninitialize).
    bool is_prepared();
    // 0x00554320: number_collected counted up (goal_update_counter run with use_hud_counter).
    void picked_up_countable();
    // 0x00554390: number_collected reached number.
    bool tally_objective_finished();
    // 0x005543E0
    bool state02();
    // 0x005543F0: proset_prefix is `prefix` (any case).
    bool has_proset_prefix(const char* prefix);
    // 0x00554430 / 0x00554440 / 0x00554450
    void mark_viewed();
    void choose_one();
    void unpick();
    // 0x00554460 (vtable +0x64): can_retry_goal.
    bool may_try_again();
    // 0x00554470: current_time 0. 0x00554480: unless paused, current_time counted up by the frame (ms), and its
    // seconds and hundredths (current_time_seconds, current_time_fraction).
    void reset_current_time();
    void update_current_time();
    // 0x00554510 / 0x00554520: the quick_start flag.
    void set_quick_start();
    void unset_quick_start();
    // 0x00554530 / 0x00554540 / 0x00554550: pro_goal, net (components), competition (flag).
    bool is_pro_challenge();
    bool is_online_objective();
    bool is_contest_objective();
    // 0x00554560 (vtable +0x48)
    virtual bool is_frozen();
    // 0x00554570: view_goals_text.
    bool get_objective_list_caption(const char** text);
    // 0x00554590
    bool end_of_run_started();
    // 0x005545A0 (vtable +0x20): not unlimited_Time.
    bool has_time_limit();
    // 0x005545F0 (vtable +0x28): whether the HUD shows its clock (the time display's first such active goal): the
    // 0xA302D7CD component.
    virtual bool shows_clock();
    // (slot 9: the shared "true") Whether the timer beeps in its last seconds.
    virtual bool timer_beeps() { return true; }
    // 0x00554610 (vtable +0x18) / 0x00554770 (vtable +0x1C): the saved state (hasBeaten, isLocked, hasSeen, the
    // record, the goal flags) read / written.
    virtual void restore_save_block(script::Struct* data);
    virtual void capture_save_block(script::Struct* data);
    // 0x00554890 (cdecl): `text` copied to `out` with \t<n> / \k<n> (one or two digits, from 1) replaced by trick
    // `n`'s / key combo `n`'s text. (`size` isn't used: no bounds checks, as the original.)
    static void replace_trick_tokens(const char* text, char* out, script::Array* key_combos, script::Array* tricks,
                                     int size);
    // 0x005549A0: number_collected, else the flags set.
    int count_picked_up();
    // 0x005549D0: num_flags_to_win, else the number of flags (0: -1).
    int get_num_flags_to_win();
    // 0x00554A00 / 0x00554A10 / 0x00554A20
    int get_times_started();
    void choose_run_ending(int type);
    void change_ends_on_timeout(bool deactivate);
    // 0x00554A40 / 0x00554A50 / 0x00554A70: the Horse, findGaps and require_combo flags.
    bool is_horse();
    bool is_find_gaps();
    bool requires_combo();
    // 0x00554A60: the no-time-limit state bit (unlimited_Time).
    bool has_no_time_limit();

    // 0x00554A80: the parameters' script named `name` (a checksum component) run with them. Whether there was one.
    bool invoke_hook_script(uint32_t name);
    // 0x00554AC0 / 0x00554B40: a parent / child linked (the list's first link reused while it has no goal; a child
    // only once). A failed allocation writes through null, as the original.
    void attach_upper_link(uint32_t id);
    void attach_child(uint32_t id);
    // 0x00554BD0: uninitialized (deactivated first if active; the uninit script); with `children`, every child too.
    void uninitialize(bool children, int depth);
    // 0x00554C70 (vtable +0x40): lost - the fail script, deactivated. False if it wasn't active.
    bool lose();
    // 0x00554CD0 (vtable +0x08): no local skater or no players, or out of time (never without a time limit).
    virtual bool has_run_out();
    // 0x00554D10: the same, but out of time only once the skater's run is done (the goal's or the normal one, by the
    // end of run type); out of time before that, the run is ended (`force` passed on) and false.
    bool is_expired_at_end_of_run(bool force);
    // 0x00556230 (vtable +0x04): is_expired_at_end_of_run(false).
    virtual bool wants_to_end();
    // 0x00554DD0 (vtable +0x44): while active and not paused: the active script, the timer (and its beeps under
    // ten seconds), require_combo's combo_started, expired. True.
    virtual bool tick_frame();
    // 0x00554F40 (vtable +0x14): the expire script; deactivated if it's to be on expiring.
    virtual void expire();
    // 0x00554F90: gap `gap` is one of required_gaps (and required_trick_name, if any, has been done: by its name or
    // as a grind's BS / FS): its flag got_<n> set (n from 1). Whether it was.
    bool register_gap_hit(uint32_t gap);
    // 0x00555100: the first of the waypoints (0x160B3220, else the asserted 0x92C6E839) whose flag isn't set yet
    // started: paused, its time (added, with 0xFB11CB01, else the goal's), the goal's id and the first_spot flag
    // passed, spawned (the horse's or the race's script). True.
    bool step_to_next_waypoint();
    // 0x005552D0: cluster `cluster` taken with a score above its kill_clusters entry's (else the goal's) score: the
    // cluster turned to the player's colour (0) and the entry's flag set.
    void kill_cluster(uint32_t cluster, int score);
    // 0x005553C0: trick_string has been done (a grind's FS / BS name first).
    bool did_trick_string();
    // 0x00555540: the topmost parent's goal_id (this goal's if it has none).
    uint32_t get_top_objective_key();
    // 0x00555590 (vtable +0x54): beaten (in a goal attack or net lobby game: kFlagWonInNetGame).
    virtual bool has_won();
    // 0x005555E0 (vtable +0x68): beaten - and every parent.
    virtual void mark_won();
    // 0x00555650 / 0x00555670 (vtable +0x6C / +0x70): stage bit `bit` set. 0x00555690 / 0x005556B0 (vtable +0x50 /
    // +0x58): whether it is (0 / 1 in all of EAX).
    void set_stage_flag(int bit);
    void set_net_stage_flag(int bit);
    int stage_flag_set(int bit);
    int net_stage_flag_set(int bit);
    // 0x005556D0: start_height: the local skater's height now (feet).
    void set_launch_altitude();
    // 0x00555710: unless paused: the skater's height above start_height (feet and inches) - a new record (above
    // 0x78FFB85C, at least 10000) kept with its feet and inches. Whether it was one.
    bool test_highest_air();
    // 0x00555800: the same for the distance from the 0x8E18F3B3 object (current_distance_feet / _inches).
    bool test_longest_jump();
    // 0x00555930 (vtable +0x74): not beaten any more (in a net game the stage bits cleared too). Whether it was.
    virtual bool un_beat();
    // 0x00555970: the end of run done (the goal's or the normal one): the skater told, the started flag cleared.
    void end_of_run_done(script::Script* script);
    // 0x005559E0 / 0x00555A20: the end of run (the goal's or the normal one) done / started.
    bool run_done();
    bool run_started();

    // --- goal_life.cpp ---
    // 0x00557270: initialized: the difficulty level's parameters, the init script, the trick texts, unlimited_Time,
    // dont_deactivate_upon_expiration (in the bottom-up heap).
    void prepare_all();
    // 0x00556F90: the difficulty level's parameters added (and the goal flags made again).
    void merge_skill_tier_args();
    // 0x00557360 (vtable +0x2C): started (if not active): counted, reset_score, the timer (a child with
    // single_timer: its root's), the root uninitialized for it, initialized, the activate script, seen, active, the
    // goal manager's active list. False if it was active.
    virtual bool activate();
    // 0x00557560 (vtable +0x34): stopped (if active): special's key combos dropped, inactive, the goal flags reset
    // (unless kept), the deactivate script, testing_goal; not won: the skater readied, and with `reset_tree` the root
    // made ready again; unlinked. (`unused` isn't read.) False if it wasn't active.
    virtual bool disengage(bool unused, bool reset_tree);
    // 0x005576D0 (vtable +0x3C): won. With a child: the child started instead. Otherwise the record (score - the
    // custom one, high combo or total - or time), the rewards (first win, not in a net game), the success script,
    // deactivated, marked won, the cash / chapter (career) or the counter (classic). True.
    virtual bool get_win();
    // 0x00557A40: unlocked (if locked): initialized unless won (classic / always_initialize_goal / edited_goal
    // anyway), the unlock script - not in a net game.
    void get_unlock();
    // 0x00555DF0: the reward parameters into `rewards` (the goal points shown, a trick slot, goal_rewards' entry for
    // the goal, reward_stat).
    void get_prize_struct(script::Struct* rewards);
    // 0x00555FB0: Reward_Goal's goal(s) unlocked.
    void open_up_prize_objectives();
    // 0x00556050: reward_points (an array: by the difficulty level), else 0.
    int get_reward_points();
    // 0x00556330 (vtable +0x4C): goal flag `flag` (of skater structure `sub`, if any) set to `value` - while active,
    // or silent_activation and not won; not locked. A change counts the flags set (the flag sound, the counter); a
    // silently activated goal is won once all are set (TrickObjects aside), else Gaps shows the gap. Whether it
    // changed (or was already set).
    virtual bool set_flag(uint32_t flag, int value, uint32_t sub);
    // 0x005566D0: the goal flags from `params`' goal_flags (names: each 0; structures: skater_flags, one structure
    // per skater with its flags and counts); num_flags.
    void alloc_objective_bits(script::Struct* params);
    // 0x00556240: every player's run (by the end of run type) is done (in horse, those not playing aside).
    bool all_skaters_run_done();

    // --- goal_tricks.cpp ---
    // 0x005565C0: goal_no_valid_key_combos run (with the goal's id).
    void no_combo_sequence_left();
    // 0x005568B0: a random key combo of `combos` that the skater has a trick on (100 tries, then each in turn - which
    // returns the last random one, as the original); -1 (no_combo_sequence_left) if none.
    int pick_key_combo(script::Array* combos);
    // 0x00555B50 (cdecl): each key combo's text (goal_tetris_trick_text) and trick's name (the skater's trick
    // mapping: a special slot's trick, a premade create-a-trick, or the trick's own) into the two arrays. False if a
    // key combo has no trick.
    static bool build_trick_texts(script::Array* key_combos, script::Array* key_texts, script::Array* trick_texts,
                                  int premade_cat);
    // 0x00556A00: the texts' \t / \k tokens replaced by the key combos' tricks (the originals kept as original_*).
    // False without key_combos or a trick for one.
    bool swap_move_caption();
    // 0x00555A60: a special trick slot given as a reward taken back (should_remove_trick), the key combos dropped.
    void remove_reward_trick();
    // 0x00555D40: the kill_clusters turned to `player`'s colour (or cleared with `clear`).
    void tint_tagged_props(int player, bool clear);
    // 0x00557AC0: the goal's trick made a special (a free key combo of the trick type's - or the slot it already
    // has), key_combos set to it, the texts redone. Whether it was new.
    bool award_special_trick();
};

// (0x005560D0, the goal list's node table's slot 0 - lists::Elem<Objective>: unlinked, freed with flags bit 0 - is
// the goal's own node's uncouple here; nothing deletes a node through it.)

extern float g_goal_life_seconds_per_millisecond;

} // namespace goals
