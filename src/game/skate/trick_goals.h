#pragma once

#include <cstddef>
#include <cstdint>

// Four goal types: Skatetris (CFallingTricksGoal - tricks fall into a stack as screen elements and are cleared by doing
// them; its TrickTris, combo and single_combo variants), TrickBeat (CTrickBeatGoal - tricks to land on the beat of a
// sound pattern), TimedScoreRun and TrickObjects (CObjective with almost nothing of their own).
// From the disassembly of 0x0056F090-0x00571110 and 0x00574960-0x00576520 (methods __thiscall). The goal manager
// makes them by the goal's type flag (0x0055BA10: tetris, TrickBeat, TimedScoreRun, TrickObjects).
namespace Scripts {
class CArray;
class CStruct;
} // namespace Script

namespace GoalSystem {

// The first 0x70 bytes of CObjective (the goal base class, 0x00550040-0x0056C920 - not rewritten yet): only what these
// goals use. Its virtual functions by slot: +0x0C IsOperative, +0x14 Expire, +0x2C Activate, +0x34 Disengage(a, b),
// +0x3C Win, +0x44 Update, +0x48 IsFrozen, +0x4C SetFlag(flag, a, b), +0x54 IsLatched.
struct GoalBase {
    void** m_vtable;
    uint8_t m_unknown04[0x1C - 0x04];
    Scripts::CStruct* m_args; // +0x1C the goal's parameters (and its state as flags and integers)
    uint8_t m_unknown20[0x24 - 0x20];
    int m_curPhase;               // +0x24 (2: ...)
    uint8_t m_unknown28[0x50 - 0x28];
    int m_secondsRemaining;            // +0x50 ms
    uint8_t m_unknown54[0x6D - 0x54];
    uint8_t m_flagBits;           // +0x6D (0x10: no time limit)
    uint8_t m_unknown6E[2];

    bool IsOperative();
    bool IsFrozen();
};
static_assert(sizeof(GoalBase) == 0x70 && offsetof(GoalBase, m_args) == 0x1C && offsetof(GoalBase, m_secondsRemaining) == 0x50,
              "layout must match the original");

// One trick in the stack (0x20 bytes).
struct StackedTrickEntry {
    uint32_t trick;     // +0x00 the trick's name (a grind: "FS " + name)
    uint32_t trick2;    // +0x04 a grind's "BS " + name
    uint32_t keyCombo;  // +0x08 a key combo's name (key combo tricks)
    uint32_t element;   // +0x0C its screen element's id
    bool free;          // +0x10 the slot is unused
    bool perfect;       // +0x11
    uint8_t pad12[2];
    int group;          // +0x14 the combo group (combo modes)
    int spin;           // +0x18 in half turns
    int taps;           // +0x1C 1, or 2 for a double tap
};
static_assert(sizeof(StackedTrickEntry) == 0x20, "layout must match the original");

// Skatetris (vtable 0x0064E8A0, 0x458 bytes).
class CFallingTricksGoal : public GoalBase {
public:
    static constexpr int kMaxTricks = 30;

    StackedTrickEntry m_tricks[kMaxTricks]; // +0x70
    int m_trickTotal;                   // +0x430
    int m_stopwatch;                       // +0x434 ms since the last trick was added
    bool m_groupUsed[kMaxTricks];      // +0x438 the combo groups in use
    uint8_t pad456[2];

    // 0x0056F090: the constructor (CObjective's, then the vtable). 0x0056F0B0 / 0x0056F290: the destructor body and the
    // scalar deleting destructor.
    CFallingTricksGoal* MakeNew(Scripts::CStruct* params);
    void DestroyBody();
    CFallingTricksGoal* Teardown(uint8_t flags);
    // 0x0056F2E0: ready to start (trick_time copied to the current interval, the first trick soon).
    void PrepareAll();
    // 0x0056F130: every trick cleared (the slots left in use) and the single combo's added flag; 0x0056F5B0: the
    // slots freed and the stack's menu destroyed.
    void ClearTricks();
    void ClearTricksAndMenu();
    // 0x0056F180: no trick is in the stack.
    bool AllFree();
    // 0x0056F1A0 / 0x0056F1F0 / 0x0056F240: the combo, single_combo and TrickTris variants.
    bool IsCombo();
    bool IsSingleCombo();
    bool IsTrickTris();
    // 0x0056F3B0: each trick's alpha: faded once it's been done (the scores' trick counts), else unfaded.
    void UpdateAlphas();
    // 0x0056F630: trick `index` taken off the stack (its scripts run, the completed count and the acceleration).
    void DropMoveEntry(int index);
    // 0x0056FA30: `count` new tricks (random, or key combos) added; lost if the stack would overflow.
    void AddTricks(int count);
    // 0x00570A70: the combo variants: each group whose combo_size tricks are all done is removed.
    void CheckCombos();
    // 0x00570F60: the done tricks removed (combos: CheckCombos).
    void CheckTricks();

    // The vtable's overrides: 0x0056F0C0 Win (+0x3C), 0x0056F2C0 Deactivate (+0x34), 0x0056F500 Expire (+0x14),
    // 0x0056F610 Activate (+0x2C), 0x00570C40 Update (+0x44).
    bool Win();
    void Disengage(int a, int b);
    void Expire();
    bool Activate();
    bool TickFrame();
};
static_assert(offsetof(CFallingTricksGoal, m_trickTotal) == 0x430 && offsetof(CFallingTricksGoal, m_groupUsed) == 0x438 &&
                  sizeof(CFallingTricksGoal) == 0x458,
              "layout must match the original (0x0055BA10 allocates 0x458 bytes)");

// One trick of TrickBeat's (0x20 bytes).
struct BeatTrick {
    uint32_t trick;     // +0x00 the trick's name (key combos: its text's checksum)
    uint32_t trick2;    // +0x04
    uint32_t keyCombo;  // +0x08
    uint32_t element;   // +0x0C
    bool landed;        // +0x10 done in the beat window
    bool free;          // +0x11
    bool perfect;       // +0x12
    uint8_t pad13;
    int unknown14;      // +0x14
    int spin;           // +0x18
    int taps;           // +0x1C
};
static_assert(sizeof(BeatTrick) == 0x20, "layout must match the original");

// TrickBeat (vtable 0x0064E9B8, 0x47C bytes).
class CTrickBeatGoal : public GoalBase {
public:
    static constexpr int kMaxTricks = 30;
    static constexpr int kBeats = 16;

    BeatTrick m_tricks[kMaxTricks]; // +0x70
    int m_trickTotal;                 // +0x430
    int m_lastTime;                  // +0x434 the game clock last frame (ms)
    int m_beatTime;                  // +0x438 ms into the beat
    int m_beat;                      // +0x43C 0..15
    float m_beatLength;              // +0x440 ms (15000 / tempo)
    bool m_pulse;                    // +0x444 the beat pattern's pulse flag for this beat
    uint8_t pad445[3];
    Scripts::CArray* m_pattern;       // +0x448 a copy of beat_pattern (16 structures: sounds, pulse)
    bool m_inWindow;                 // +0x44C tricks count as on the beat
    uint8_t pad44D[3];
    int m_windowTime;                // +0x450 beat_window_time
    int m_completed;                 // +0x454
    int m_totalToWin;                // +0x458 total_to_win
    bool m_unknown45C[kMaxTricks];   // +0x45C
    uint8_t pad47A[2];

    // 0x00574C40 / 0x00575140 / 0x00576210: constructor, destructor body, scalar deleting destructor.
    CTrickBeatGoal* MakeNew(Scripts::CStruct* params);
    void DestroyBody();
    CTrickBeatGoal* Teardown(uint8_t flags);
    // 0x00574960: a new beat pattern and tempo.
    void SetBeatPattern(Scripts::CArray* pattern, int tempo);
    // 0x00574990: every trick cleared (and the added flag).
    void ClearTricks();
    // 0x00574A10: ready to start.
    void PrepareAll();
    // 0x00574AB0: a trick landed (its params): in the beat window, the matching tricks marked landed and faded.
    void TrickLanded(Scripts::CStruct* trick);
    // 0x00574B90: the landed marks cleared (tricks unfaded).
    void ClearLanded();
    // 0x00574C20: the clock restarted.
    void ResetClock();
    // 0x00574D80: the tricks not landed pulse (goal_trick_beat_pulse_trick).
    void PulseTricks();
    // 0x00574F00: trick `index` taken off.
    void DropMoveEntry(int index);
    // 0x005751C0: the beat moves on: on each beat its sounds play; the beat window opens / closes.
    void UpdateBeat();
    // 0x00575450: `count` new tricks added.
    void AddTricks(int count);
    // 0x005761D0: the landed tricks removed (each counted).
    void RemoveLandedTricks();

    // The vtable's overrides: 0x005749F0 Deactivate (+0x34), 0x00574D50 Activate (+0x2C), 0x00576240 Update (+0x44).
    void Disengage(int a, int b);
    bool Activate();
    bool TickFrame();
};
static_assert(offsetof(CTrickBeatGoal, m_trickTotal) == 0x430 && offsetof(CTrickBeatGoal, m_pattern) == 0x448 &&
                  offsetof(CTrickBeatGoal, m_unknown45C) == 0x45C && sizeof(CTrickBeatGoal) == 0x47C,
              "layout must match the original (0x0055BA10 allocates 0x47C bytes)");

// TimedScoreRun (vtable 0x0064E938, 0x70 bytes).
class CTimedScoreRunGoal : public GoalBase {
public:
    // 0x005710B0 / 0x005710D0 / 0x00571110
    CTimedScoreRunGoal* MakeNew(Scripts::CStruct* params);
    void DestroyBody();
    CTimedScoreRunGoal* Teardown(uint8_t flags);
    // 0x005710E0 (vtable +0x28): active and not paused.
    int IsUnderway();
};

// TrickObjects (vtable 0x0064EA30, 0x70 bytes).
class CTrickObjectsGoal : public GoalBase {
public:
    // 0x005763A0 / 0x005763C0 / 0x005763D0
    CTrickObjectsGoal* MakeNew(Scripts::CStruct* params);
    void DestroyBody();
    CTrickObjectsGoal* Teardown(uint8_t flags);
    // 0x00576400: the goal's end: won if it's ... (0x00554110) and the local skater has Required_Score (pending
    // points included), else lost.
    void EvaluatePoints();
    // 0x005764B0: the goal_objects entry with cluster_id `cluster` (null if none).
    Scripts::CStruct* GetObjectByCluster(uint32_t cluster);
    // 0x00576520: the objects of cluster `cluster` were hit: the goal started if need be and the object's flag set.
    void ObjectHit(uint32_t cluster);
};

} // namespace Game
