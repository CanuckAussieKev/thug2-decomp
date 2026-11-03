#pragma once

// modules::LetterMatch (0x438 bytes; 0x005600D0-0x005608BF): the HORSE game - the skate module's (+0x364). The players take
// runs in turn from the level's Horse restarts; a run that doesn't beat the score to beat earns the player a letter
// of the word; the first to spell it (and lose) ends the game. Its status (the table at 0x0067F340) and its panel
// text tell the scripts what happened.

#include <cstdint>
#include <array>

#include "engine/core/text_store.h"

namespace script {
class Struct;
} // namespace script

namespace modules {
constexpr std::size_t kWordBytes = 0x200;  // the word (HORSE) buffer
constexpr std::size_t kPanelBytes = 0x200; // the panel text buffer

class LetterMatch {
public:
    static constexpr int kPlayers = 8;

    // The fields other code reads and sets.
    [[nodiscard]] int skater() const { return skater_; }

private:
    int points_target_{};     // +0x000 (0: none set)
    int skater_{};            // +0x004: the skater taking the run (its id)
    int last_skater_{};       // +0x008
    std::array<char, kWordBytes> word_{};      // +0x00C ("HORSE")
    std::array<char, kPanelBytes> panel_{};     // +0x20C: what the last run did
    std::array<int, kPlayers> letters_{}; // +0x40C: each player's letters
    uint32_t unknown42c_{};
    int restart_{0};          // +0x430: the restart the next run starts from
    uint32_t cur_standing_{}; // +0x434: one of the table at 0x0067F340

public:

    // 0x005600D0: the word HORSE, restart 0.
    LetterMatch();
    // 0x00560100: a new game: no score, no letters, the panel "unknown", a random Horse restart.
    void prepare_all();
    // 0x00560180: a run started (the status idle).
    void begin_timed_session();
    // 0x00560190: the status is the one of the table that `params` has as a flag (false if it has none).
    bool state_matches(script::Struct* params) const;
    // 0x005601F0: the word set (copied from `word`, which is destroyed here - passed by value).
    void set_letters(const text::Chars& word);
    // 0x00560260: the word as `player` has it: the letters earned, dashes for the rest.
    text::Chars get_letter_string(int player);
    // 0x00560310: the restart the next run starts from (the restarts looked up and not used, as the original).
    [[nodiscard]] int get_restart() const;
    // 0x00560340: on to the next Horse restart (round to the first).
    void choose_next_spawn();
    // 0x00560380: a player (still in the game) has every letter.
    bool is_over();
    // 0x005603D0: the next player after the current one (0 with debug_horse).
    [[nodiscard]] int fetch_next_live_rider() const;
    // 0x00560440: one of the game's texts (panelString1 / 2, horsepostrun, youarea, horseprerun, finalword,
    // playerName; "Unknown" for another).
    text::Chars fetch_text(uint32_t which);
    // 0x00560720: the run ended: a better score is the score to beat (BeatScore); a worse one earns a letter
    // (GotLetter, or the end: "You are a ..."); the same - none set (NoScoreSet) or tied (TieScore).
    void end_run();
    // 0x005608B0: the next player's turn.
    void swap_riders();
};

} // namespace modules
