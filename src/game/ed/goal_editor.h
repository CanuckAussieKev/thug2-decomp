#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
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

    // 0x004E93D0: point `index` dropped onto what's below it (a line from 10000 above to 10000 below; missed, from
    // 20000 above), if anything.
    void DropPointToGround(int index);
    // 0x004E9590: every set point dropped to the ground and its object made there (the script 0x79582240 with its
    // node's Name - none for the start - and Pos).
    void PlacePointsOnGround();
    // 0x004EB550: the current point (not the pro's) placed again at `pos` (false if it can't go there), and the
    // point before it current; back at the first, the script 0xD066A889 run with 0x7F1538E0.
    bool PlacePointAndStepBack(const Mth::Vector& pos, float height, float angle);

    // 0x004E96D0: the goal from a structure (GetEditedGoalsInfo's entries, the CreatedGoals save): its defaults, then
    // level, number, type, score, time, control, texts, points and the type's settings. Marked used, not won.
    void ReadFromStructure(Script::CStruct* params);
    // 0x004EB260: the goal into a structure (ReadFromStructure's names).
    void WriteIntoStructure(Script::CStruct* out);
    // 0x004E9BC0: the key combo sets as the array 0x0490F171 (none: nothing).
    void WriteKeyComboSets(Script::CStruct* out);
    // 0x004E98F0: the key combo sets as the goal manager wants them: every key combo of each set (from the global
    // array 0xDF1451EB) with the set's taps, perfect flag and the goal's spin - no sets: emergency_key_combos.
    void WriteGoalKeyCombos(Script::CStruct* out);
    // 0x004E9D60: a trick goal's settings; `forGoalManager` (1): the key combos for the goal manager.
    void WriteTrickSettings(Script::CStruct* out, int forGoalManager);
    // 0x004E9E00: the type's own settings; `forGoalManager` (1): as the goal manager wants them.
    void WriteSpecificParams(Script::CStruct* out, int forGoalManager);
    // 0x004EA850: the goal manager's parameters for the goal: the type's structures, its nodes (pro, restart,
    // letters / checkpoints), its settings, texts, win message, id CreatedGoal<n>, time and control.
    void WriteGoalManagerParams(Script::CStruct* out);
    // 0x004EB6B0: the goal added to the goal manager as CreatedGoal<n> (seen; won again unless `unwin`).
    void AddToGoalManager(bool unwin);

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

// The goal editor component ("goaleditor", type 0x81F01058, vtable 0x0064B534, 0xA114 bytes; made by 0x004EB7F0) of
// the permanent object GoalEditor: every created goal of every level (50 slots), the one being edited, and the
// object's input and editor camera components (the cursor). Its script member functions are the table at 0x00684270
// (filled by 0x00643960).
//
// Vtable: 0 Destroy 0x004E7B20; 1 Update 0x004EBD30; 2 the empty 0x005BAB90; 3 Finalize 0x004E8210; 8 Hide
// 0x004EBC40; the rest shared.
class CGoalEditorComponent : public Obj::CBaseComponent {
public:
    // 0x004EB7F0: a new one (0 if out of memory).
    static Obj::CBaseComponent* __cdecl Create();
    // 0x004E6FB0 (not a Ghidra function): its script member functions (the table at 0x00684270) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x004EA230: the base's constructor, its vtable and type, every goal's defaults (the array constructor with
    // 0x004E93C0), the goals cleared; no input or camera yet. Returns this.
    CGoalEditorComponent* Construct();
    // 0x004E6FD0: the destructor body: the script 0xFCC60D8B run, then the base's.
    void DestroyBody();
    // 0x004E7B20 (vtable slot 0)
    CGoalEditorComponent* Destroy(unsigned int flags);
    // 0x004E8210 (vtable slot 3): the object's input (0x27D7CD28) and editor camera components found.
    void Finalize();

    // 0x004E8470: the goal editor (the object GoalEditor's component).
    static CGoalEditorComponent* __cdecl Get();

    // 0x004E7BE0: the current level's goal named `name` (view_goals_text, any case), or null.
    CEditedGoal* FindGoalByName(const char* name);
    // 0x004E7C60: the current level's goal whose CreatedGoal<n> is `id`, or null.
    CEditedGoal* FindGoalById(uint32_t id);
    // 0x004E7D00: the goal the parameters name (goal_id, a goal name 0xBFECC45B, a number 0x474A6A7F), or the one
    // being edited.
    CEditedGoal* GetGoalFromParams(Script::CStruct* params);
    // 0x004E7E00: how many slots are used.
    int CountUsedGoals();
    // 0x004E7E50: how many goals of `level` (any with 0) have all their points.
    int CountCompleteGoals(uint32_t level);
    // 0x004E8130: whether point `index` of any goal of `level` is within the radius 0x71B20276 of `pos` raised by
    // `height`.
    bool PointNearAnyGoal(const Mth::Vector& pos, float height, uint32_t level, int index);
    // 0x004E8240: every goal outside the park editor cleared; none current.
    void ClearLevelGoals();
    // 0x004E8280: every goal of the park editor cleared; none current.
    void ClearParkGoals();
    // 0x004E82C0: every goal without all its points cleared.
    void ClearIncompleteGoals();
    // 0x004E8300: whether any park goal's start lies outside x0..x1, z0..z1.
    bool AnyParkGoalOutside(float x0, float z0, float x1, float z1);
    // 0x004E8390: the park goals whose start lies outside x0..x1, z0..z1 removed.
    void RemoveParkGoalsOutside(float x0, float z0, float x1, float z1);
    // 0x004E8420: gap `gap` removed from the park's gap goals (later gaps renumbered).
    void RemoveGapFromParkGoals(int gap);
    // 0x004E8090: `goal` taken out of the goal manager (if it's there) and cleared; no longer current.
    void RemoveGoal(CEditedGoal* goal);
    // 0x004E7B50: the editor camera's simple collision on for a letter / checkpoint point, else in the park editor.
    void UpdateCameraCollision();
    // 0x004E7F30: the editor camera's cursor put on the current point.
    void MoveCursorToCurrentPoint();
    // 0x004E7FC0: the script 0xDEA7FE56 run with the cursor's place (raised by its height) and heading.
    void RunCursorScript();
    // 0x004EA580: the cursor's model for the current point (the script 0x36791E3C on the object).
    void RefreshCursor();
    // 0x004EA6C0: a new goal in the first free slot (the level's lowest free number), current; incomplete goals
    // cleared first. Nothing without a free slot.
    void NewGoal();
    // 0x004EA780: the park's goals' points dropped to the ground.
    void PlaceParkGoalsOnGround();
    // 0x004EA470: the goals from the structure's array 0x38DBE1D0 (`parkOnly`: the park editor's, else the others'),
    // the matching goals cleared first.
    void ReadGoals(Script::CStruct* params, int parkOnly);
    // 0x004EA2C0: `level`'s goals from a written structure (the park's, from a buffer). Returns the buffer's end.
    const uint8_t* ReadLevelGoalsFromBuffer(uint32_t level, const uint8_t* buffer);
    // 0x004EB850: every goal outside the park editor into the array 0x38DBE1D0 (none: nothing).
    void WriteGoals(Script::CStruct* out);
    // 0x004EBA10: `level`'s goals into the array 0x38DBE1D0 (sized by the complete ones).
    void WriteLevelGoals(uint32_t level, Script::CStruct* out);
    // 0x004EBB70: `level`'s goals written into `buffer` (WriteToBuffer's size).
    uint32_t WriteLevelGoalsToBuffer(uint32_t level, uint8_t* buffer, uint32_t size);

    // 0x004EA7C0 [NewEditorGoal]: a new goal; its goal_id returned in the script's parameters. True.
    bool NewEditorGoal(Script::CStruct* params, Script::CScript* script);

    CEditedGoal m_goals[50];               // +0x0018
    CEditedGoal* mp_current;               // +0xA108 the one being edited
    Obj::CBaseComponent* mp_input;         // +0xA10C the object's input component
    Obj::CBaseComponent* mp_camera;        // +0xA110 its editor camera
};
static_assert(offsetof(CGoalEditorComponent, m_goals) == 0x18 && offsetof(CGoalEditorComponent, mp_current) == 0xA108 &&
                  sizeof(CGoalEditorComponent) == 0xA114,
              "layout must match the original");

} // namespace Ed
