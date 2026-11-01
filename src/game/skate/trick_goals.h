#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "game/goals/objective.h"

// Four goal types: Skatetris (FallingTricksGoal - tricks fall into a stack as screen elements and are cleared by doing
// them; its TrickTris, combo and single_combo variants), TrickBeat (TrickBeatGoal - tricks to land on the beat of a
// sound pattern), TimedScoreRun and TrickObjects (Objective with almost nothing of their own).
// From the disassembly of 0x0056F090-0x00571110 and 0x00574960-0x00576520 (methods __thiscall). The goal manager
// makes them by the goal's type flag (0x0055BA10: tetris, TrickBeat, TimedScoreRun, TrickObjects).
namespace script {
class Array;
class Struct;
} // namespace script

namespace goals {

// One trick in the stack (0x20 bytes).
struct StackedTrickEntry {
    uint32_t trick;      // +0x00 the trick's name (a grind: "FS " + name)
    uint32_t trick2;     // +0x04 a grind's "BS " + name
    uint32_t key_combo_; // +0x08 a key combo's name (key combo tricks)
    uint32_t element;    // +0x0C its screen element's id
    bool free;           // +0x10 the slot is unused
    bool perfect;        // +0x11
    std::array<uint8_t, 2> pad12;
    int group; // +0x14 the combo group (combo modes)
    int spin;  // +0x18 in half turns
    int taps;  // +0x1C 1, or 2 for a double tap
};

// Skatetris (vtable 0x0064E8A0, 0x458 bytes).
class FallingTricksGoal final : public Objective {
public:
    FallingTricksGoal(const FallingTricksGoal&) = delete;
    FallingTricksGoal& operator=(const FallingTricksGoal&) = delete;
    static constexpr int kMaxTricks = 30;

    std::array<StackedTrickEntry, kMaxTricks> tricks_{}; // +0x70
    int trick_total_{};                      // +0x430
    int stopwatch_{};                        // +0x434 ms since the last trick was added
    std::array<bool, kMaxTricks> group_used_{};          // +0x438 the combo groups in use
    std::array<uint8_t, 2> pad456{};

    // 0x0056F090: the constructor (Objective's, then the vtable). 0x0056F0B0 / 0x0056F290: the destructor body and the
    // scalar deleting destructor.
    explicit FallingTricksGoal(script::Struct* params);
    ~FallingTricksGoal() override;
    // 0x0056F2E0: ready to start (trick_time copied to the current interval, the first trick soon).
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method): the original's non-virtual method of the same name
    void prepare_all();
    // 0x0056F130: every trick cleared (the slots left in use) and the single combo's added flag; 0x0056F5B0: the
    // slots freed and the stack's menu destroyed.
    void clear_tricks();
    void clear_tricks_and_menu();
    // 0x0056F180: no trick is in the stack.
    bool all_free();
    // 0x0056F1A0 / 0x0056F1F0 / 0x0056F240: the combo, single_combo and TrickTris variants.
    bool is_combo();
    bool is_single_combo();
    bool is_trick_tris();
    // 0x0056F3B0: each trick's alpha: faded once it's been done (the scores' trick counts), else unfaded.
    void update_alphas();
    // 0x0056F630: trick `index` taken off the stack (its scripts run, the completed count and the acceleration).
    void drop_move_entry(int index);
    // 0x0056FA30: `count` new tricks (random, or key combos) added; lost if the stack would overflow.
    void add_tricks(int count);
    // 0x00570A70: the combo variants: each group whose combo_size tricks are all done is removed.
    void check_combos();
    // 0x00570F60: the done tricks removed (combos: check_combos).
    void check_tricks();

    // The vtable's overrides: 0x0056F0C0 Win (+0x3C), 0x0056F2C0 Deactivate (+0x34), 0x0056F500 Expire (+0x14),
    // 0x0056F610 Activate (+0x2C), 0x00570C40 Update (+0x44).
    bool get_win() override;
    bool disengage(bool unused, bool reset_tree) override;
    void expire() override;
    bool activate() override;
    bool tick_frame() override;
};

// One trick of TrickBeat's (0x20 bytes).
struct BeatTrick {
    uint32_t trick;      // +0x00 the trick's name (key combos: its text's checksum)
    uint32_t trick2;     // +0x04
    uint32_t key_combo_; // +0x08
    uint32_t element;    // +0x0C
    bool landed;         // +0x10 done in the beat window
    bool free;           // +0x11
    bool perfect;        // +0x12
    int unknown14;       // +0x14
    int spin;            // +0x18
    int taps;            // +0x1C
};

// TrickBeat (vtable 0x0064E9B8, 0x47C bytes).
class TrickBeatGoal final : public Objective {
public:
    TrickBeatGoal(const TrickBeatGoal&) = delete;
    TrickBeatGoal& operator=(const TrickBeatGoal&) = delete;
    static constexpr int kMaxTricks = 30;
    static constexpr int kBeats = 16;

    std::array<BeatTrick, kMaxTricks> tricks_{}; // +0x70
    int trick_total_{};              // +0x430
    int last_time_{};                // +0x434 the game clock last frame (ms)
    int beat_time_{};                // +0x438 ms into the beat
    int beat_{};                     // +0x43C 0..15
    float beat_length_;            // +0x440 ms (15000 / tempo)
    bool pulse_{};                 // +0x444 the beat pattern's pulse flag for this beat
    script::Array* pattern_;       // +0x448 a copy of beat_pattern (16 structures: sounds, pulse)
    bool in_window_{};             // +0x44C tricks count as on the beat
    int window_time_{};            // +0x450 beat_window_time
    int completed_{};              // +0x454
    int total_to_win_{};           // +0x458 total_to_win
    std::array<bool, kMaxTricks> unknown45c_{}; // +0x45C
    std::array<uint8_t, 2> pad47a_{};

    // 0x00574C40 / 0x00575140 / 0x00576210: constructor, destructor body, scalar deleting destructor.
    explicit TrickBeatGoal(script::Struct* params);
    ~TrickBeatGoal() override;
    // 0x00574960: a new beat pattern and tempo.
    void set_beat_pattern(script::Array* pattern, int tempo);
    // 0x00574990: every trick cleared (and the added flag).
    void clear_tricks();
    // 0x00574A10: ready to start.
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method): the original's non-virtual method of the same name
    void prepare_all();
    // 0x00574AB0: a trick landed (its params): in the beat window, the matching tricks marked landed and faded.
    void trick_landed(script::Struct* trick);
    // 0x00574B90: the landed marks cleared (tricks unfaded).
    void clear_landed();
    // 0x00574C20: the clock restarted.
    void reset_clock();
    // 0x00574D80: the tricks not landed pulse (goal_trick_beat_pulse_trick).
    void pulse_tricks();
    // 0x00574F00: trick `index` taken off.
    void drop_move_entry(int index);
    // 0x005751C0: the beat moves on: on each beat its sounds play; the beat window opens / closes.
    void update_beat();
    // 0x00575450: `count` new tricks added.
    void add_tricks(int count);
    // 0x005761D0: the landed tricks removed (each counted).
    void remove_landed_tricks();

    // The vtable's overrides: 0x005749F0 Deactivate (+0x34), 0x00574D50 Activate (+0x2C), 0x00576240 Update (+0x44).
    bool disengage(bool unused, bool reset_tree) override;
    bool activate() override;
    bool tick_frame() override;
};

// TimedScoreRun (vtable 0x0064E938, 0x70 bytes).
class TimedScoreRunGoal final : public Objective {
public:
    TimedScoreRunGoal(const TimedScoreRunGoal&) = delete;
    TimedScoreRunGoal& operator=(const TimedScoreRunGoal&) = delete;
    // 0x005710B0 / 0x005710D0 / 0x00571110
    explicit TimedScoreRunGoal(script::Struct* params);
    ~TimedScoreRunGoal() override;
    // 0x005710E0 (vtable +0x28): active and not paused.
    bool shows_clock() override;
};
static_assert(sizeof(TimedScoreRunGoal) == 0x70, "the size the goal manager allocated");

// TrickObjects (vtable 0x0064EA30, 0x70 bytes).
class TrickObjectsGoal final : public Objective {
public:
    TrickObjectsGoal(const TrickObjectsGoal&) = delete;
    TrickObjectsGoal& operator=(const TrickObjectsGoal&) = delete;
    // 0x005763A0 / 0x005763C0 / 0x005763D0
    explicit TrickObjectsGoal(script::Struct* params);
    ~TrickObjectsGoal() override;
    // 0x00576400: the goal's end: won if it's ... (0x00554110) and the local skater has Required_Score (pending
    // points included), else lost.
    void evaluate_points();
    // 0x005764B0: the goal_objects entry with cluster_id `cluster` (null if none).
    script::Struct* get_object_by_cluster(uint32_t cluster);
    // 0x00576520: the objects of cluster `cluster` were hit: the goal started if need be and the object's flag set.
    void get_object_hit(uint32_t cluster);
};
static_assert(sizeof(TrickObjectsGoal) == 0x70, "the size the goal manager allocated");

} // namespace goals
