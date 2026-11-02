#pragma once

// modules::Contest (the skate module's competition, modules::Skate +0x360, 0x168 bytes; 0x0054FFD0-0x005517BF): a
// competition goal's standings. Eight leaders - the player (id 0) and seven pros in a random order, each with a
// score per run (a run's score: the best two of the runs so far summed) - and the five judges' scores of the last
// run. Start makes up the pros' scores around the medal scores; EndRun turns the player's points into judges' scores
// and places everyone; the leader board and the judges' scores are shown by scripts.

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/checked_index.h"
#include "engine/core/text_store.h"
#include "engine/core/raw_view.h"

namespace script {
class Struct;
} // namespace script

namespace modules {

struct Contest {
    static constexpr int kLeaders = 8;
    static constexpr int kJudges = 5;
    static constexpr int kRuns = 3;

    // One of the eight standings (0x20 bytes).
    struct Leader {
        text::Chars label_; // +0x00
        int ident_{};       // +0x0C: 0 the player, 1-7 the pros (a random order)
        std::array<int, kRuns> scores_{}; // +0x10: each run's score
        bool unknown1c_{};    // +0x1C (copied with the leader, never set: the leader board's debug output reads it
                              // and its padding as a fourth score)
        std::array<uint8_t, 3> pad1d_{};

        // (0x0054FFD0, the constructor - the name's - and 0x0054FFE0, the destructor - a jump to TextUtil::Chars's
        // (text_store.cpp): the implicit ones)

        // The best two of the first `count` scores summed (none above 0: 0).
        [[nodiscard]] int best_two(int count) const;
        // The same of the four words from the scores on (the debug output's: unknown1c_ and its padding the fourth).
        [[nodiscard]] int best_two_of_four_words() const;
        // The leader's words from +0x10: the three scores, then (the debug output reads four) the flag and its padding
        // as one little-endian word - what the original reached past the scores; any other index ends the game.
        [[nodiscard]] int get_word(int index) const
        {
            if (index == kRuns)
                return static_cast<int>(static_cast<uint32_t>(unknown1c_) | (static_cast<uint32_t>(core::at(pad1d_, 0)) << 8) |
                                        (static_cast<uint32_t>(core::at(pad1d_, 1)) << 16) | (static_cast<uint32_t>(core::at(pad1d_, 2)) << 24));
            return core::at(scores_, index);
        }
    };

    script::Struct* args_;             // +0x00: StartCompetition's extra parameters (leader_board_names,
                                       // first_place_name)
    int runs_{};                       // +0x04: the current run (begin_timed_session counts them; -1 after Start)
    std::array<Leader, kLeaders> leaders_;         // +0x08: sorted by score after each run (sort_leaders)
    float third_mark_{};               // +0x108: the medal points (Start; made a little random)
    float second_mark_{};              // +0x10C
    float top_mark_{};                 // +0x110
    float bronze_score_{};             // +0x114: the trick scores worth the medal points
    float silver_score_{};             // +0x118
    float gold_score_{};               // +0x11C
    float bail_penalty_{};             // +0x120 (stored by Start: not read here)
    float crash_penalty_{};            // +0x124: taken off the run's points (cleared when a run starts)
    std::array<float, kJudges> judge_scores_{};    // +0x128: the last run's judges' scores
    std::array<const char*, kJudges> judge_names_{}; // +0x13C: comp_str_Judge1..5
    float path_vertices_{};              // +0x150: the last run's points (cleared when a run starts)
    std::array<int, kRuns> run_totals_{};            // +0x154: the player's score of each run
    bool ended_{};                       // +0x160
    int standing_{};                     // +0x164: the player's place (1-8) after the last run

    // 0x005504B0: the constructor (the leaders' names, an empty parameter structure). The argument isn't used.
    // Returns this.
    // (The original's argument isn't used.)
    Contest();
    // 0x0054FFF0: the leaders' names destroyed, last first (the implicit destructor).
    Contest(const Contest&) = delete;
    Contest& operator=(const Contest&) = delete;
    ~Contest() = default;
    // 0x00550010: copies of `params`' components added to the competition's parameters (if any).
    void rewrite_args(script::Struct* params);
    // 0x00550020: a run starts.
    void begin_timed_session();
    // 0x00550560: the competition starts: the medal points and scores stored, the player first (the current
    // profile's name), seven pros (a random order of the pros that aren't hidden or the player) with scores made up
    // around the medal points, sorted; the judges' names.
    void kickoff(float bronze, float silver, float gold, float bronze_score, float silver_score, float gold_score,
                 float bail_penalty);
    // 0x00550B50: the top five shown (goal_comp_add_leader_board) - only while a competition goal is active.
    void show_leader_board();
    // 0x00550EF0: the run ends with the player's trick score `score`: its points (by the medal scores, less the bail
    // points), the judges' scores around them (rounded), the run's score, the standings sorted, the player's place;
    // shown (goal_comp_show_run_scores) while a competition goal is active. Nothing once ended.
    void end_run(int score);
    // 0x00550200: the leaders sorted by the best two of their scores so far (the first runs + 1, all three once
    // ended), highest first; a tie goes to the player.
    void sort_leaders();
    // 0x00550040: judge `judge` gave one of the three highest scores of the last run.
    bool is_leading_scorer(int judge);
    // 0x005501A0: the player's place is `place`. 0x005501C0: the run is round `round` (runs + 1).
    bool place_is(int place);
    bool round_is(int round);
    // 0x005501E0: the competition ends. 0x005501F0: it has.
    void get_end();
    bool is_ended();
};

inline constexpr float kCompetitionTiny{0.0001f}; // 0x006453D4

} // namespace modules
