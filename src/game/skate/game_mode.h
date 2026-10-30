#pragma once

// Game::CGameMode (the singleton 0x007CE470, 0x20 bytes; 0x00552F70-0x00553DF9) - the game mode: free skate,
// career, the net games... its definition (mode_info's entry: the mode's structure, then options overridden) and its
// victory conditions. Game::CGameFlow (0x00552E10-0x00552F6F): the skate module's game flow script, run until done.

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/script/struct.h"
#include "hook/game_global.h"

namespace Script {
class CScript;
} // namespace Script

namespace Game {

class CVictoryCondition;

// The game flow (0x10 bytes, vtable 0x0064DD50): a script started anew when one is requested.
class CGameFlow {
public:
    void** m_vtable;          // +0x00
    Script::CScript* m_script; // +0x04
    bool m_pending;           // +0x08: m_name to be started
    uint8_t m_pad09[3];
    uint32_t m_name;          // +0x0C

    // 0x00552E10: the constructor (no script). Returns this.
    CGameFlow* Construct();
    // 0x00552F30 (vtable slot 0): the scalar deleting destructor (the script deleted).
    CGameFlow* Destroy(uint8_t flags);
    // 0x00552E30: script `name` to be started (by the next Update).
    void Request(uint32_t name);
    // 0x00552E40: a requested script started (the old one deleted); the script updated - deleted once it's done.
    void Update();
};
static_assert(offsetof(CGameFlow, m_name) == 0x0C && sizeof(CGameFlow) == 0x10, "layout must match the original");

class CGameMode {
public:
    // The victory conditions' list (Lst::Head<CVictoryCondition>, vtable 0x0064DD90; its nodes' 0x0064DD54).
    struct ConditionNode {
        void** m_vtable;
        CVictoryCondition* m_condition; // (-1 for the head)
        int m_priority;
        ConditionNode* m_next;
        ConditionNode* m_prev;

        // 0x00553710: the node's destructor body (unlinked); 0x00553730 / 0x00553CB0: the node's / the head's
        // scalar deleting destructor.
        void DestroyBody();
        ConditionNode* Destroy(uint8_t flags);
        ConditionNode* DestroyHead(uint8_t flags);
    };

    void** m_vtable;          // +0x00 (0x0064DD58)
    Script::CStruct m_params; // +0x04: the mode's definition (Name, the options)
    ConditionNode m_conditions; // +0x0C

    // 0x00553DB0 (cdecl): the singleton (made with `create`, counted); 0x00552F70: one reference let go (the last
    // deletes it).
    static CGameMode* Instance(bool create);
    static void Release();
    // 0x00553D30: the constructor: no conditions, the free skate mode. Returns this. 0x00553890: the destructor body.
    // 0x00553CE0 (vtable slot 0): the scalar deleting destructor.
    CGameMode* Construct();
    void DestroyBody();
    CGameMode* Destroy(uint8_t flags);
    // 0x00553D10: back to free skate.
    void Reset();
    // 0x00553AD0: the mode `type` of mode_info loaded (its definition's structure); in a net game max_players
    // kept. False if there's no such mode.
    bool LoadGameType(uint32_t type);
    // 0x00553BE0: `options` added over the definition (time_limit_type, victory_condition_type and screenmode
    // replaced); the conditions made again. True.
    bool OverrideOptions(Script::CStruct* options);
    // 0x00553770 / 0x005537D0: the victory conditions deleted / made from victory_conditions (in the skater info
    // heap).
    void ClearVictoryConditions();
    void BuildVictoryConditions();
    // 0x00553910: the run is over by the conditions: not elimiskate / scavenger hunt, with a clock, while the goal
    // manager's active net goal... is out of time - or every terminal condition complete.
    bool EndConditionsMet();
    // 0x00553A60: the first victory condition's +4 (its target - 0: none).
    int GetFirstConditionValue();

    // The definition's options.
    bool ShouldAccumulateScore();      // 0x00552FB0 accumulate_score
    bool ShouldTrackBestCombo();       // 0x00552FD0 track_best_combo (none: false)
    void SetScoreAccumulation(bool on); // 0x00553000
    void SetScoreFrozen(bool on);       // 0x00553020
    void SetScoreDegradation(bool on);  // 0x00553040
    void SetTrickScoreTracking(bool on); // 0x00553060
    void SetTeamGraffitiMode(uint32_t mode); // 0x00553080
    void SetGoalsMode(uint32_t mode);   // 0x005530A0
    int IsScoreFrozen();                // 0x005530C0 score_frozen (0 / 1 in EAX)
    bool ShouldTrackTrickScore();       // 0x005530F0 track_trick_score
    bool ShouldDegradeScore();          // 0x00553110 degrade_score
    int NumTeams();                     // 0x00553130 num_teams
    bool ShouldTimerBeep();             // 0x00553150 timer_beeps
    int GetTimeLimit();                 // 0x00553170 default_time_limit
    int GetInitialNumberOfPlayers();    // 0x00553190 initial_players
    void SetMaximumNumberOfPlayers(int players); // 0x005531B0
    int GetMaximumNumberOfPlayers();    // 0x005531D0 max_players
    int GetFireballLevel();             // 0x005531F0 fireball_level (3)
    bool IsTrue(uint32_t name);         // 0x00553220 the integer `name` is non-zero
    void SetRound(int round);           // 0x00553250
    int GetRound();                     // 0x00553270
    uint32_t GetNameChecksum();         // 0x005532A0 Name
    uint32_t GetGoalsMode();            // 0x005532D0 goals
    uint32_t GetTeamGraffitiMode();     // 0x00553300 team_graffiti_mode
    // 0x00553330: a net mode's name as its single player one (nettrickattack -> trickattack...).
    uint32_t ConvertNetNameChecksum(uint32_t name);
    // 0x005533B0 / 0x00553400: the mode's goal's time left (ms; none: 0) / set (seconds).
    int GetGoalTimeLeft();
    void SetGoalTimeLeft(int seconds);
    // 0x00553460: the mode runs on a clock (not king / score challenge / create goals / firefight; a time limit).
    bool ShouldUseClock();
    // 0x00553550: the run stops at zero (stop_at_zero; a multiplayer game with a clock only - else true).
    bool ShouldStopAtZero();
    // 0x005535A0: num_teams (a net lobby makes the team flags). 0x005535F0: a team game (0 / 1 in EAX).
    void SetNumTeams(int teams);
    int IsTeamGame();
    // 0x00553630: the screen mode: 0 single, 1 / 2 split screen vertical / horizontal, 4 horse.
    int GetScreenMode();
    // 0x005536D0: the round counted up.
    void IncrementRound();
};
static_assert(offsetof(CGameMode, m_params) == 0x04 && offsetof(CGameMode, m_conditions) == 0x0C &&
                  sizeof(CGameMode) == 0x20,
              "layout must match the original");

// 0x00553C90: OverrideGameModeOptions - the game mode's options overridden by the command's parameters.
bool __cdecl CFunc_OverrideGameModeOptions(Script::CStruct* params, Script::CScript* script);

GAME_GLOBAL_EXTERN(Game::CGameMode*, g_gameMode);

} // namespace Game
