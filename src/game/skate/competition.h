#pragma once

// Mdl::CCompetition (the skate module's competition, Mdl::Skate +0x360, 0x168 bytes; 0x0054FFD0-0x005517BF): a
// competition goal's standings. Eight leaders - the player (id 0) and seven pros in a random order, each with a
// score per run (a run's score: the best two of the runs so far summed) - and the five judges' scores of the last
// run. Start makes up the pros' scores around the medal scores; EndRun turns the player's points into judges' scores
// and places everyone; the leader board and the judges' scores are shown by scripts.

#include <cstddef>
#include <cstdint>

#include "game/core/string.h"

namespace Script {
class CStruct;
} // namespace Script

namespace Mdl {

struct CCompetition {
    static constexpr int kLeaders = 8;
    static constexpr int kJudges = 5;
    static constexpr int kRuns = 3;

    // One of the eight standings (0x20 bytes).
    struct Leader {
        Str::String m_name;   // +0x00
        int m_id;             // +0x0C: 0 the player, 1-7 the pros (a random order)
        int m_scores[kRuns];  // +0x10: each run's score
        bool m_unknown1C;     // +0x1C (copied with the leader, never set: the leader board's debug output reads it
                              // and its padding as a fourth score)
        uint8_t m_pad1D[3];

        // 0x0054FFD0: the constructor (the name's). Returns this.
        Leader* Construct();
        // 0x0054FFE0 is its destructor: a jump to Str::String's (string.cpp).

        // The best two of the first `count` scores summed (none above 0: 0).
        int BestTwo(int count) const;
        // The same of the four words from the scores on (the debug output's: m_unknown1C and its padding the fourth).
        int BestTwoOfFourWords() const;
        int Word(int index) const { return reinterpret_cast<const int*>(m_scores)[index]; }
    };

    Script::CStruct* m_params;     // +0x00: StartCompetition's extra parameters (leader_board_names,
                                   // first_place_name)
    int m_runs;                    // +0x04: the current run (StartRun counts them; -1 after Start)
    Leader m_leaders[kLeaders];    // +0x08: sorted by score after each run (SortLeaders)
    float m_bronze;                // +0x108: the medal points (Start; made a little random)
    float m_silver;                // +0x10C
    float m_gold;                  // +0x110
    float m_bronzeScore;           // +0x114: the trick scores worth the medal points
    float m_silverScore;           // +0x118
    float m_goldScore;             // +0x11C
    float m_bailPenalty;           // +0x120 (stored by Start: not read here)
    float m_bailPoints;            // +0x124: taken off the run's points (cleared when a run starts)
    float m_judgeScores[kJudges];  // +0x128: the last run's judges' scores
    const char* m_judgeNames[kJudges]; // +0x13C: comp_str_Judge1..5
    float m_points;                // +0x150: the last run's points (cleared when a run starts)
    int m_runScores[kRuns];        // +0x154: the player's score of each run
    bool m_ended;                  // +0x160
    uint8_t m_pad161[3];
    int m_place;                   // +0x164: the player's place (1-8) after the last run

    // 0x005504B0: the constructor (the leaders' names, an empty parameter structure). The argument isn't used.
    // Returns this.
    CCompetition* Construct(int unused);
    // 0x0054FFF0: the destructor body - the leaders' names, last first.
    void DestroyBody();
    // 0x00550010: copies of `params`' components added to the competition's parameters (if any).
    void EditParams(Script::CStruct* params);
    // 0x00550020: a run starts.
    void StartRun();
    // 0x00550560: the competition starts: the medal points and scores stored, the player first (the current
    // profile's name), seven pros (a random order of the pros that aren't hidden or the player) with scores made up
    // around the medal points, sorted; the judges' names.
    void Start(float bronze, float silver, float gold, float bronzeScore, float silverScore, float goldScore,
               float bailPenalty);
    // 0x00550B50: the top five shown (goal_comp_add_leader_board) - only while a competition goal is active.
    void ShowLeaderBoard();
    // 0x00550EF0: the run ends with the player's trick score `score`: its points (by the medal scores, less the bail
    // points), the judges' scores around them (rounded), the run's score, the standings sorted, the player's place;
    // shown (goal_comp_show_run_scores) while a competition goal is active. Nothing once ended.
    void EndRun(int score);
    // 0x00550200: the leaders sorted by the best two of their scores so far (the first runs + 1, all three once
    // ended), highest first; a tie goes to the player.
    void SortLeaders();
    // 0x00550040: judge `judge` gave one of the three highest scores of the last run.
    bool IsTopJudge(int judge);
    // 0x005501A0: the player's place is `place`. 0x005501C0: the run is round `round` (runs + 1).
    bool PlaceIs(int place);
    bool RoundIs(int round);
    // 0x005501E0: the competition ends. 0x005501F0: it has.
    void End();
    bool IsEnded();
};
static_assert(sizeof(CCompetition::Leader) == 0x20 && offsetof(CCompetition::Leader, m_id) == 0x0C &&
                  offsetof(CCompetition::Leader, m_scores) == 0x10 && offsetof(CCompetition::Leader, m_unknown1C) == 0x1C,
              "layout must match the original");
static_assert(offsetof(CCompetition, m_leaders) == 0x08 && offsetof(CCompetition, m_bronze) == 0x108 &&
                  offsetof(CCompetition, m_bailPoints) == 0x124 && offsetof(CCompetition, m_judgeScores) == 0x128 &&
                  offsetof(CCompetition, m_judgeNames) == 0x13C && offsetof(CCompetition, m_points) == 0x150 &&
                  offsetof(CCompetition, m_runScores) == 0x154 && offsetof(CCompetition, m_ended) == 0x160 &&
                  offsetof(CCompetition, m_place) == 0x164 && sizeof(CCompetition) == 0x168,
              "layout must match the original");

} // namespace Mdl
