#pragma once

#include <cstddef>
#include <cstdint>

// The skater's career (0x00584110-0x00584CF3; the rest of its code is later in the range): for each of 21 levels its
// goals (256 bits) and flags (256 bits), the goals and flags as they were when the level started (to tell which were
// just got), cash and a second per-level amount with their totals, a per-level counter, global flags (256 bits: the
// cheats are among them), the current level, each level's gap checklist, whether each level was visited, and an
// info structure.
namespace Game {
class CGapChecklist;
}
namespace Script {
class CStruct;
}

namespace Obj {

class CSkaterCareer {
public:
    static constexpr int kLevels = 21;

    // 0x00584110: level `level`'s gap checklist (-1: the current level's). 0x00584130: the current level.
    Game::CGapChecklist* GetGapChecklist(int level);
    int GetLevel();
    // 0x00584140 / 0x00584180 / 0x005841D0: goal `goal` of `level` (-1: the current one) set / cleared (also from
    // the level-start copy) / asked. 0x00584210: whether the current level's goal changed since the level started.
    void SetGoal(int goal, int level);
    void UnSetGoal(int goal, int level);
    bool GetGoal(int goal, int level);
    bool JustGotGoal(int goal);
    // 0x00584250: how many of the first 64 goals of `level` are set. 0x005842A0: those of every level not left out
    // (0x007CE4E8); 0x00584320: how many of those levels have any.
    int CountGoals(int level);
    int CountTotalGoals();
    int CountLevelsWithGoals();
    // 0x005843A0 / 0x005843D0 / 0x00584400: a global flag set / cleared / asked.
    void SetGlobalFlag(int flag);
    void UnSetGlobalFlag(int flag);
    bool GetGlobalFlag(int flag);
    // 0x00584430 / 0x00584470 / 0x005844C0 / 0x00584500: a level flag, as the goals.
    void SetFlag(int flag, int level);
    void UnSetFlag(int flag, int level);
    bool GetFlag(int flag, int level);
    bool JustGotFlag(int flag);
    // 0x00584540 / 0x00584560: an integer of its info structure (by name) read / set (true).
    int GetInfoInteger(uint32_t name);
    bool SetInfoInteger(uint32_t name, int value);
    // 0x00584580: the info's 0x471FC4DD integer increased by `amount`.
    void AddToInfo471FC4DD(int amount);
    // 0x005845C0: cash added to the current level's and the total; the level's amount returned (as the original: a
    // pointer to it). 0x005845F0: the current level's second amount set (the total changed by the difference).
    uint16_t* AddCash(int amount);
    void SetLevelAmount(int amount);
    // 0x00584620: the current level's counter and the total counter increased; the level's returned. 0x00584640:
    // a level's counter. 0x00584650: the flag at +0x5EF set.
    uint8_t* IncrementCounter();
    uint8_t GetCounter(int level);
    void SetFlag5EF(uint8_t value);
    // 0x00584660: the value at +0x634 set (+0x638 keeps the highest).
    void SetValue634(int value);
    // 0x00584680: reset: every gap checklist (if asked); the current level 0; then the part asked (0: both):
    // 0x4DA4937B - flags and goals 0-199 of every level, the left-out levels, cash, the second amounts (1000 each),
    // the level-start copies, the totals (the second 6000) -; 0x4C10DE52 - flags and goals 200-255 of the first 18
    // levels, their counters, the counter total, +0x5EF 0x82, the info's defaults (0x0486FD79). True.
    bool Reset(bool resetGaps, uint32_t part);
    // 0x00584BA0: Reset without the gaps. True.
    bool ResetPart(uint32_t part);
    // 0x00584BC0: whether every level listed in 0xEB1A4BC9 was visited and has all its gaps.
    bool AllGapsGot();
    // 0x00584C60: level `level` started (-1: the current one): visited; its goals and flags copied for JustGot*.
    void StartLevel(int level);
    // 0x00584CC0: the global flag a script integer names (a cheat); also left at 0x007CE4FD.
    bool GetCheat(uint32_t name);

    // The goal / flag words of every level, in one run (level * 8 + word).
    uint32_t* GoalWords() { return &m_goals[0][0]; }
    uint32_t* FlagWords() { return &m_flags[0][0]; }

    uint32_t m_unknown0;
    uint32_t m_goals[kLevels][8];        // +0x004
    uint32_t m_flags[kLevels][8];        // +0x2A4
    uint32_t m_startGoals[8];            // +0x544
    uint32_t m_startFlags[8];            // +0x564
    uint16_t m_cash[kLevels];            // +0x584
    uint16_t m_amount[kLevels];          // +0x5AE
    uint16_t m_totalCash;                // +0x5D8
    uint16_t m_totalAmount;              // +0x5DA
    uint8_t m_counter[18];               // +0x5DC
    uint8_t m_totalCounter;              // +0x5EE
    uint8_t m_flag5EF;                   // +0x5EF
    uint32_t m_globalFlags[16];          // +0x5F0
    int m_level;                         // +0x630
    int m_value634;                      // +0x634
    int m_maxValue634;                   // +0x638
    Game::CGapChecklist* m_gaps[kLevels]; // +0x63C
    bool m_visited[kLevels];             // +0x690
    uint8_t pad6A5[3];
    Script::CStruct* m_info;             // +0x6A8
};
static_assert(offsetof(CSkaterCareer, m_flags) == 0x2A4 && offsetof(CSkaterCareer, m_startGoals) == 0x544 &&
                  offsetof(CSkaterCareer, m_cash) == 0x584 && offsetof(CSkaterCareer, m_totalCash) == 0x5D8 &&
                  offsetof(CSkaterCareer, m_counter) == 0x5DC && offsetof(CSkaterCareer, m_globalFlags) == 0x5F0 &&
                  offsetof(CSkaterCareer, m_level) == 0x630 && offsetof(CSkaterCareer, m_gaps) == 0x63C &&
                  offsetof(CSkaterCareer, m_visited) == 0x690 && offsetof(CSkaterCareer, m_info) == 0x6A8,
              "layout must match the original");

} // namespace Obj
