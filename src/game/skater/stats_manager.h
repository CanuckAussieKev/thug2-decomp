#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The stats manager component (StatsManager, type 0x4FF8DAC6, vtable 0x0064C294, 0xAA0 bytes; made by 0x0051CB90):
// the story mode's stat challenges - up to 70 goals (the global array 0x4E46CC28, per difficulty) a skater meets by
// what it does: a combo's score or multiplier, air height and distance, a jump's rise or drop, tricks and gaps
// counted, a manual or grind held long enough. Each frame the skater's state and score are watched; a goal met is
// announced (script 0x55E516C8) and, when the stats are announced (0x0051CBF0), raises a stat in the profile.
//
// Vtable: 0 Destroy, 1 Update, 2 InitFromStructure, 3 the empty 0x005480A0, 5 0x004E3F90 (shared), 11 0x0042BB20
// (member function entries by table: the StatsManager_ functions, the table at 0x00684180), 12 0x0051ADC0 (shared).
namespace Obj {

class CStatsManagerComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x4FF8DAC6;
    static constexpr int kGoals = 70;

    // A stat challenge (0x24 bytes from +0x1C).
    struct Goal {
        uint32_t m_kind;          // +0x00 (0x0FFD7BBE of its structure) what's checked
        uint32_t m_stat;          // +0x04 (0xC2455018) the stat it raises
        int m_target;             // +0x08 (0xE288A7CB, by difficulty)
        bool m_met;               // +0x0C met (until announced or reset)
        uint8_t pad0D[3];
        int m_status;             // +0x10 0 open, 1 done, 2 / 3 locked until the am / pro goals unlock
        const char* m_name;       // +0x14 (0xC4745838)
        const char* m_text;       // +0x18 (0xCD021293, by difficulty)
        uint32_t m_trick;         // +0x1C (0xD8A9D3CA, by difficulty)
        uint32_t m_value20;       // +0x20 (0x3C2FB496, by difficulty)
    };
    static_assert(sizeof(Goal) == 0x24, "layout must match the original");

    // 0x0051CB90 (registered with the composite object manager): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0051C440 (not a Ghidra function; registered with Create): its script member functions (the table at
    // 0x00684180) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x0051C5A0: the base's constructor, its vtable and type; no skater, the positions and the last air combo's
    // score cleared. Returns this.
    CStatsManagerComponent* Construct();
    // 0x0051C460: the destructor body (the base's).
    void DestroyBody();
    // 0x0051C640 (vtable slot 0)
    CStatsManagerComponent* Destroy(unsigned int flags);

    // 0x0051C470 / 0x0051C480: goal `index`'s status; set (1 also marks that one was done, +0x9F6).
    int GetGoalStatus(int index);
    void SetGoalStatus(int index, int status);

    // 0x0051C670 (vtable slot 2): everything reset; the skater (the object's id in the object manager) and its
    // state and physics control components; the difficulty; the goals read from the global array 0x4E46CC28 (`params`
    // isn't read).
    void InitFromStructure(Script::CStruct* params);
    // 0x0051C950: whether the goals are checked: a skater, the game mode's flag 0x1DED1EA4, activated, the skate
    // module not in mode 0xE92ECAFE, and the first profile's name 0x0A7BE964.
    bool IsActive();
    // 0x0051C9D0: +0xA10 = `a` unless `b` is +0xA14 (checked). `c` isn't used.
    void Set10(int a, int b, int c);
    // 0x0051CA00: back to before the combo (script 0xD44EA68F): goals met but not done unmarked, the air and jump
    // phases and the two counters cleared, the starts back to the ends.
    void Reset();
    // 0x0051CAC0: +0xA58 = `value` (checked).
    void SetValue58(float value);
    // 0x0051CAE0: counts 0xBA15FC7F (+0xA80) and 0xB9AE26D2 (+0xA84) (checked).
    void Count(uint32_t name);
    // 0x0051CBF0: the goals met are done: announced (scripts 0x750BCF49, 0x55E516C8 with the stat's name), their
    // stats raised in the first profile (with +0x9F5) and given to the skater; the first time, 0xEF3A4EE5.
    void AnnounceMetGoals();
    // 0x0051CE80: goal `index` met (script 0x55E516C8 with its name, target, text and index).
    void MeetGoal(int index);
    // 0x0051CF90 (vtable slot 1): the score and multiplier noted, the air and jump watched, every open goal checked.
    void Update();
    // 0x0051D6A0: a balance trick of `trickType` held `time` seconds: the manual / grind time goals checked.
    void CheckBalanceTime(uint32_t trickType, float time, int unused);

    // --- script member functions (the table at 0x00684180) ---
    bool ReInit(Script::CStruct* params, Script::CScript* script);          // 0x0051C4A0 StatsManager_ReInit
    bool ResetGoals(Script::CStruct* params, Script::CScript* script);      // 0x0051CB20 StatsManager_Reset
    bool ActivateGoals(Script::CStruct* params, Script::CScript* script);   // 0x0051C4C0 (script 0x7EC5F3E0)
    bool DeactivateGoals(Script::CStruct* params, Script::CScript* script); // 0x0051C4F0 (script 0x3CB06807)
    bool GetGoalStatusMember(Script::CStruct* params, Script::CScript* script); // 0x0051CB30 StatsManager_GetGoalStatus
    bool UnlockAmGoals(Script::CStruct* params, Script::CScript* script);   // 0x0051C520 (status 2 -> 0)
    bool UnlockProGoals(Script::CStruct* params, Script::CScript* script);  // 0x0051C550 (status 3 -> 0)
    bool GetLastAirComboScore(Script::CStruct* params, Script::CScript* script);   // 0x0051CB70
    bool ClearLastAirComboScore(Script::CStruct* params, Script::CScript* script); // 0x0051C580

    bool m_firstUpdate;          // +0x18 the next update only clears the phases
    uint8_t pad19[3];
    Goal m_goals[kGoals];        // +0x1C
    bool m_activated;            // +0x9F4
    bool m_raiseStats;           // +0x9F5 (the global integer 0xB8688A8A is 1)
    bool m_goalDone;             // +0x9F6 a goal's status was set to 1
    uint8_t pad9F7;
    int m_score;                 // +0x9F8 the combo's points times its multiplier
    int m_lastScore;             // +0x9FC
    int m_points;                // +0xA00 the score over the multiplier
    int m_lastPoints;            // +0xA04
    float m_multiplier;          // +0xA08
    float m_lastMultiplier;      // +0xA0C
    int m_value10;               // +0xA10
    int m_value14;               // +0xA14
    int m_difficulty;            // +0xA18
    uint32_t m_state;            // +0xA1C this frame's (0x26A7CADF, 0xB39B4F1B, 0x439F4704)
    uint32_t m_lastState;        // +0xA20
    Mth::Vector m_airStart;      // +0xA24 where the skater left the ground (state +0x30)
    Mth::Vector m_airEnd;        // +0xA34 where it landed
    int m_airStartPoints;        // +0xA44
    float m_airStartMultiplier;  // +0xA48
    int m_airBest;               // +0xA4C the best combo score made in the air
    int m_airPhase;              // +0xA50 0, 1 in the air, 2 landed
    float m_airHeight;           // +0xA54 the most above the start
    float m_value58;             // +0xA58 (cleared after each update)
    Mth::Vector m_jumpStart;     // +0xA5C
    Mth::Vector m_jumpEnd;       // +0xA6C
    int m_jumpPhase;             // +0xA7C 0, 1 in the air, 2 landed
    int m_countA80;              // +0xA80 (0xBA15FC7F)
    int m_countA84;              // +0xA84 (0xB9AE26D2)
    int m_airScore;              // +0xA88
    int m_lastAirComboScore;     // +0xA8C
    CCompositeObject* m_skater;  // +0xA90
    CBaseComponent* m_skaterState;   // +0xA94
    CBaseComponent* m_physicsControl; // +0xA98
    uint32_t m_unknownA9C;
};
static_assert(offsetof(CStatsManagerComponent, m_goals) == 0x1C &&
                  offsetof(CStatsManagerComponent, m_activated) == 0x9F4 &&
                  offsetof(CStatsManagerComponent, m_score) == 0x9F8 &&
                  offsetof(CStatsManagerComponent, m_difficulty) == 0xA18 &&
                  offsetof(CStatsManagerComponent, m_airStart) == 0xA24 &&
                  offsetof(CStatsManagerComponent, m_jumpStart) == 0xA5C &&
                  offsetof(CStatsManagerComponent, m_lastAirComboScore) == 0xA8C &&
                  offsetof(CStatsManagerComponent, m_skater) == 0xA90 && sizeof(CStatsManagerComponent) == 0xAA0,
              "layout must match the original");

} // namespace Obj
