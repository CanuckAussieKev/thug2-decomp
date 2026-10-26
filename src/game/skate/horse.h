#pragma once

// Mdl::CHorse (0x438 bytes; 0x005600D0-0x005608BF): the HORSE game - the skate module's (+0x364). The players take
// runs in turn from the level's Horse restarts; a run that doesn't beat the score to beat earns the player a letter
// of the word; the first to spell it (and lose) ends the game. Its status (the table at 0x0067F340) and its panel
// text tell the scripts what happened.

#include <cstdint>

#include "game/core/string.h"

namespace Script {
class CStruct;
} // namespace Script

namespace Mdl {

class CHorse {
public:
    static constexpr int kPlayers = 8;

    int m_scoreToBeat;        // +0x000 (0: none set)
    int m_skater;             // +0x004: the skater taking the run (its id)
    int m_lastSkater;         // +0x008
    char m_word[0x200];       // +0x00C ("HORSE")
    char m_panel[0x200];      // +0x20C: what the last run did
    int m_letters[kPlayers];  // +0x40C: each player's letters
    uint32_t m_unknown42C;
    int m_restart;            // +0x430: the restart the next run starts from
    uint32_t m_status;        // +0x434: one of the table at 0x0067F340

    // 0x005600D0: the constructor: the word HORSE, restart 0. Returns this.
    CHorse* Construct();
    // 0x00560100: a new game: no score, no letters, the panel "unknown", a random Horse restart.
    void Init();
    // 0x00560180: a run started (the status idle).
    void StartRun();
    // 0x00560190: the status is the one of the table that `params` has as a flag (false if it has none).
    bool StatusEquals(Script::CStruct* params);
    // 0x005601F0: the word set (copied from `word`, which is destroyed here - passed by value).
    void SetWord(Str::String word);
    // 0x00560260: the word as `player` has it: the letters earned, dashes for the rest.
    Str::String GetWordForPlayer(int player);
    // 0x00560310: the restart the next run starts from (the restarts looked up and not used, as the original).
    int GetRestart();
    // 0x00560340: on to the next Horse restart (round to the first).
    void SetNextRestartPoint();
    // 0x00560380: a player (still in the game) has every letter.
    bool Ended();
    // 0x005603D0: the next player after the current one (0 with debug_horse).
    int GetNextValidSkaterId();
    // 0x00560440: one of the game's texts (panelString1 / 2, horsepostrun, youarea, horseprerun, finalword,
    // playerName; "Unknown" for another).
    Str::String GetString(uint32_t which);
    // 0x00560720: the run ended: a better score is the score to beat (BeatScore); a worse one earns a letter
    // (GotLetter, or the end: "You are a ..."); the same - none set (NoScoreSet) or tied (TieScore).
    void EndRun();
    // 0x005608B0: the next player's turn.
    void SwitchPlayers();
};
static_assert(sizeof(CHorse) == 0x438, "layout must match the original");

} // namespace Mdl
