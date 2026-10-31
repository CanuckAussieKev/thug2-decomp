#pragma once

// GoalSystem::CMatchRules (the singleton 0x007CE470, 0x20 bytes; 0x00552F70-0x00553DF9) - the game mode: free skate,
// career, the net games... its definition (mode_info's entry: the mode's structure, then options overridden) and its
// victory conditions. GoalSystem::CMatchFlow (0x00552E10-0x00552F6F): the skate module's game flow script, run until done.

#include <cstddef>
#include <cstdint>

#include "game/core/arena_alloc.h"
#include "game/script/param_struct.h"

namespace Scripts {
class CScript;
} // namespace Script

// The virtual table of the player lists' search objects (ListKit::Hunt<PlayerRecord>, 0x0064C1A8; defined in skate_module_state.cpp).
namespace GameModules {
extern void** const kPlayerSearchVtable;
} // namespace Mdl
// (ListKit::Hunt<JoinerRecord>, 0x0064C82C; defined in gamenet_handlers.cpp.)
namespace OnlinePlay {
extern void** const kNewPlayerSearchVtable;
} // namespace OnlinePlay

namespace GoalSystem {

class CWinRule;

// The game flow (0x10 bytes, vtable 0x0064DD50): a script started anew when one is requested.
class CMatchFlow {
public:
    void** m_vtable;          // +0x00
    Scripts::CScript* m_ownScript; // +0x04
    bool m_pending;           // +0x08: m_label to be started
    uint8_t m_pad09[3];
    uint32_t m_label;          // +0x0C

    // 0x00552E10: the constructor (no script). Returns this.
    CMatchFlow* MakeNew();
    // 0x00552F30 (vtable slot 0): the scalar deleting destructor (the script deleted).
    CMatchFlow* Teardown(uint8_t flags);
    // 0x00552E30: script `name` to be started (by the next Update).
    void Request(uint32_t name);
    // 0x00552E40: a requested script started (the old one deleted); the script updated - deleted once it's done.
    void TickFrame();
};
static_assert(offsetof(CMatchFlow, m_label) == 0x0C && sizeof(CMatchFlow) == 0x10, "layout must match the original");

class CMatchRules {
public:
    // The victory conditions' list (ListKit::Foremost<CWinRule>, vtable 0x0064DD90; its nodes' 0x0064DD54).
    struct ConditionNode {
        void** m_vtable;
        CWinRule* m_condition; // (-1 for the head)
        int m_rank;
        ConditionNode* m_succ;
        ConditionNode* m_lastValue;

        // 0x00553710: the node's destructor body (unlinked); 0x00553730 / 0x00553CB0: the node's / the head's
        // scalar deleting destructor.
        void DestroyBody();
        ConditionNode* Teardown(uint8_t flags);
        ConditionNode* DestroyHead(uint8_t flags);
    };

    void** m_vtable;          // +0x00 (0x0064DD58)
    Scripts::CStruct m_args; // +0x04: the mode's definition (Name, the options)
    ConditionNode m_conditions; // +0x0C

    // 0x00553DB0 (cdecl): the singleton (made with `create`, counted); 0x00552F70: one reference let go (the last
    // deletes it).
    static CMatchRules* Sole(bool create);
    static void Release();
    // 0x00553D30: the constructor: no conditions, the free skate mode. Returns this. 0x00553890: the destructor body.
    // 0x00553CE0 (vtable slot 0): the scalar deleting destructor.
    CMatchRules* MakeNew();
    void DestroyBody();
    CMatchRules* Teardown(uint8_t flags);
    // 0x00553D10: back to free skate.
    void Reset();
    // 0x00553AD0: the mode `type` of mode_info loaded (its definition's structure); in a net game max_players
    // kept. False if there's no such mode.
    bool ApplyMatchStyle(uint32_t type);
    // 0x00553BE0: `options` added over the definition (time_limit_type, victory_condition_type and screenmode
    // replaced); the conditions made again. True.
    bool ForceMatchSettings(Scripts::CStruct* options);
    // 0x00553770 / 0x005537D0: the victory conditions deleted / made from victory_conditions (in the skater info
    // heap).
    void ClearVictoryConditions();
    void BuildVictoryConditions();
    // 0x00553910: the run is over by the conditions: not elimiskate / scavenger hunt, with a clock, while the goal
    // manager's active net goal... is out of time - or every terminal condition complete.
    bool FinishRulesReached();
    // 0x00553A60: the first victory condition's +4 (its target - 0: none).
    int GetFirstConditionValue();

    // The definition's options.
    bool KeepsRunningTotal();      // 0x00552FB0 accumulate_score
    bool KeepsBestChain();       // 0x00552FD0 track_best_combo (none: false)
    void SetScoreAccumulation(bool on); // 0x00553000
    void HoldPoints(bool on);       // 0x00553020
    void SetScoreDegradation(bool on);  // 0x00553040
    void SetTrickScoreTracking(bool on); // 0x00553060
    void SetTeamGraffitiMode(uint32_t mode); // 0x00553080
    void SetGoalsMode(uint32_t mode);   // 0x005530A0
    int IsScoringPaused();                // 0x005530C0 score_frozen (0 / 1 in EAX)
    bool KeepsMovePoints();       // 0x005530F0 track_trick_score
    bool RepeatsLosePoints();          // 0x00553110 degrade_score
    int NumTeams();                     // 0x00553130 num_teams
    bool ClockBeepsNearEnd();             // 0x00553150 timer_beeps
    int GetTimeLimit();                 // 0x00553170 default_time_limit
    int GetStartingRiderCount();    // 0x00553190 initial_players
    void SetRiderCeiling(int players); // 0x005531B0
    int GetRiderCeiling();    // 0x005531D0 max_players
    int GetFireballLevel();             // 0x005531F0 fireball_level (3)
    bool IsTrue(uint32_t name);         // 0x00553220 the integer `name` is non-zero
    void SetRound(int round);           // 0x00553250
    int GetRound();                     // 0x00553270
    uint32_t GetNameKey();         // 0x005532A0 Name
    uint32_t GetGoalsMode();            // 0x005532D0 goals
    uint32_t GetTeamGraffitiMode();     // 0x00553300 team_graffiti_mode
    // 0x00553330: a net mode's name as its single player one (nettrickattack -> trickattack...).
    uint32_t RemapOnlineNameKey(uint32_t name);
    // 0x005533B0 / 0x00553400: the mode's goal's time left (ms; none: 0) / set (seconds).
    int GetGoalTimeLeft();
    void SetGoalTimeLeft(int seconds);
    // 0x00553460: the mode runs on a clock (not king / score challenge / create goals / firefight; a time limit).
    bool UsesCountdown();
    // 0x00553550: the run stops at zero (stop_at_zero; a multiplayer game with a clock only - else true).
    bool ClockHaltsAtZero();
    // 0x005535A0: num_teams (a net lobby makes the team flags). 0x005535F0: a team game (0 / 1 in EAX).
    void SetNumTeams(int teams);
    int IsSquadMatch();
    // 0x00553630: the screen mode: 0 single, 1 / 2 split screen vertical / horizontal, 4 horse.
    int GetViewLayout();
    // 0x005536D0: the round counted up.
    void IncrementRound();
};
static_assert(offsetof(CMatchRules, m_args) == 0x04 && offsetof(CMatchRules, m_conditions) == 0x0C &&
                  sizeof(CMatchRules) == 0x20,
              "layout must match the original");

// 0x00553C90: OverrideGameModeOptions - the game mode's options overridden by the command's parameters.
bool __cdecl CFunc_OverrideGameModeOptions(Scripts::CStruct* params, Scripts::CScript* script);

extern GoalSystem::CMatchRules* g_gameMode;

} // namespace Game
