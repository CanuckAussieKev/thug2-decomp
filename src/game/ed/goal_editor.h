#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"

namespace Script {
class CStruct;
} // namespace Script

// Create-a-goal: a goal the player made in the goal editor (CEditedGoal, a 0x338-byte record) - its level, texts,
// type, points (the pro, the start, the letters / cone checkpoints) and the type's own settings (time, score, laps,
// trick settings, key combo sets, gaps) - and how it's read from / written into script structures and placed.
namespace Ed {

// A point of an edited goal (0x1C bytes).
struct EditedGoalPoint {
    // 1 the pro, 2 the start (restart), 3 an object (a letter), 4 a checkpoint (the cone goal's).
    int kind;            // +0x00
    Mth::Vector pos;     // +0x04
    float height;        // +0x14 above pos
    float angle;         // +0x18 heading (radians)
};
static_assert(sizeof(EditedGoalPoint) == 0x1C, "layout must match the original");

// A key combo set of a trick goal (num_taps etc.).
struct KeyComboSet {
    uint8_t set;            // +0x0 (key_combo_set)
    uint8_t requirePerfect; // +0x1 (the flag 0x687E2C25)
    uint8_t spin;           // +0x2 in half turns (spin / 180)
    uint8_t numTaps;        // +0x3
};

enum EditedGoalPointKind { kPointPro = 1, kPointStart = 2, kPointObject = 3, kPointCheckpoint = 4 };

// The goal types (their names' checksums).
constexpr uint32_t kGoalTypeSkate = 0x54166ACD, kGoalTypeCombo = 0x4EC3CFB5, kGoalTypeRace = 0x25904450,
                   kGoalTypeComboTricks = 0x0FC6F698, kGoalTypeGap = 0x61C5D092, kGoalTypeScore = 0x6FE44C6D,
                   kGoalTypeTrickAttack = 0x7FE2238A, kGoalTypeKeyCombo = 0xD0DD047B, kGoalTypeTricktris = 0xEA7BA666,
                   kGoalTypeHighCombo = 0xEC414B76;
// The park editor's level (Load_Sk5Ed): its goals go with the park.
constexpr uint32_t kParkEditorLevel = 0xE8B4B836;

// An edited goal (0x338 bytes; constructed by 0x004E93C0, its defaults by InitEditedGoal 0x004E69D0).
class CEditedGoal {
public:
    // 0x004E93C0 (not a Ghidra function: the goal editor's array constructor's element function): its defaults
    // (InitEditedGoal). Returns this.
    CEditedGoal* Construct();

    // 0x004E6B80: `gap` taken out of the gap list (the rest moved down), if it's there.
    void RemoveGap(uint8_t gap);
    // 0x004E6BF0: `gap` taken out, and every later gap number one less (a gap removed from the park).
    void RemoveGapAndRenumber(uint8_t gap);
    // 0x004E7390: `gap` added to the gap list unless it's there ("Warning! MAX_GAPS limit reached!" with ten).
    void AddGap(uint8_t gap);
    // 0x004E6C40: the key combo set `set` given (replaced if there, else added - no limit checked); spin in degrees.
    void AddKeyComboSet(int set, int requirePerfect, int spin, int numTaps);
    // 0x004E6CD0: the key combo set `set` taken out (the rest moved down), if it's there.
    void RemoveKeyComboSet(int set);
    // 0x004E6D50: the key combo set `set`, or null.
    KeyComboSet* FindKeyComboSet(int set);

    // 0x004E6DA0: the goal's type: its points' kinds and how many it takes, then the type's defaults structure's
    // time limit, score and text (GoalTypeStructure).
    void SetType(uint32_t type);
    // 0x004E6F70: the time limit (a trick goal's "stop adding tricks" time kept 5 below it).
    void SetTimeLimit(int seconds);

    // 0x004E7050: the marker object of point `index` made (the script 0xEFC08BAE, with its kind's model, place,
    // height and heading).
    void CreateMarker(int index);
    // 0x004E7780: the markers of every set point but the pro made again; the first point current, not won.
    void CreateMarkers();
    // 0x004E7210: whether `pos` is too near another set point (one radius between a letter and the start, another
    // otherwise; the current point isn't counted).
    bool NearOtherPoint(const Mth::Vector& pos);
    // 0x004E7300: whether a point can go at `pos` raised by `height` (inside the park in the park editor; not too
    // near another point).
    bool CanPlacePoint(const Mth::Vector& pos, float height);
    // 0x004E77C0: the current point placed at `pos` (false if it can't go there): its marker made, the next point
    // current (starting where this one is).
    bool PlacePoint(const Mth::Vector& pos, float height, float angle);
    // 0x004E7960: the current point's place, height and heading. True.
    bool GetCurrentPoint(Mth::Vector* pos, float* height, float* angle);
    // 0x004E79C0: point `index`'s node (`nodeName`) given the point's Pos and Angles (and a letter's type / model).
    void WriteNodePosition(int index, uint32_t nodeName);
    // 0x004E7A80: every set point's node (TRG_EditedGoal<n>_Pro / _Restart / _Object_<k>) given its position.
    void WriteNodePositions();

    // 0x004E73F0: the key combo sets from the parameters' array 0x0490F171.
    void ReadKeyComboSets(Script::CStruct* params);
    // 0x004E74F0: a trick goal's settings (acceleration, trick time, max tricks, spin, key combo sets).
    void ReadTrickSettings(Script::CStruct* params);
    // 0x004E7590: the type's own settings from the parameters (SetGoalSpecificParams).
    void ReadSpecificParams(Script::CStruct* params);

    uint32_t m_level;                // +0x000 the level's load script
    char m_name[0x32];               // +0x004 view_goals_text
    char m_pedName[0x15];            // +0x036
    char m_description[0x64];        // +0x04B goal_description
    char m_winMessage[0x65];         // +0x0AF
    uint32_t m_type;                 // +0x114
    EditedGoalPoint m_points[13];    // +0x118
    uint8_t m_maxPoints;             // +0x284
    uint8_t m_minPoints;             // +0x285 (complete with this many)
    uint8_t m_numPoints;             // +0x286 set
    uint8_t m_currentPoint;          // +0x287
    bool m_lastPointPlaced;          // +0x288
    bool m_won;                      // +0x289
    bool m_used;                     // +0x28A
    uint8_t pad28B;
    int m_goalIndex;                 // +0x28C its number in its level (CreatedGoal<n>)
    int m_timeLimit;                 // +0x290
    uint32_t m_controlType;          // +0x294
    int m_score;                     // +0x298
    int m_raceTotalLaps;             // +0x29C
    int m_racePerCheckpointTime;     // +0x2A0
    int m_accelerationInterval;      // +0x2A4
    float m_accelerationPercent;     // +0x2A8
    int m_trickTime;                 // +0x2AC
    int m_timeToStopAddingTricks;    // +0x2B0
    int m_maxTricks;                 // +0x2B4
    int m_spin;                      // +0x2B8
    bool m_singleCombo;              // +0x2BC
    uint8_t pad2BD[3];
    int m_comboSize;                 // +0x2C0
    int m_tricktrisBlockSize;        // +0x2C4
    int m_tricktrisTotalToWin;       // +0x2C8
    KeyComboSet m_keyComboSets[10];  // +0x2CC
    int m_numKeyComboSets;           // +0x2F4
    uint8_t m_gaps[10];              // +0x2F8
    uint8_t m_numGaps;               // +0x302
    char m_requiredTrick[0x35];      // +0x303
};
static_assert(offsetof(CEditedGoal, m_type) == 0x114 && offsetof(CEditedGoal, m_points) == 0x118 &&
                  offsetof(CEditedGoal, m_maxPoints) == 0x284 && offsetof(CEditedGoal, m_goalIndex) == 0x28C &&
                  offsetof(CEditedGoal, m_singleCombo) == 0x2BC && offsetof(CEditedGoal, m_keyComboSets) == 0x2CC &&
                  offsetof(CEditedGoal, m_numKeyComboSets) == 0x2F4 && offsetof(CEditedGoal, m_gaps) == 0x2F8 &&
                  offsetof(CEditedGoal, m_requiredTrick) == 0x303 && sizeof(CEditedGoal) == 0x338,
              "layout must match the original");

} // namespace Ed
