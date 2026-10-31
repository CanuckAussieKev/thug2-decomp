#pragma once

// The goal types of 0x005517C0-0x00552E0F: competition, film, find gaps (each a GoalSystem::CObjective with a vtable of its
// own: the slots it changes are its own functions, the others CObjective's; see objective.h).

#include <cstddef>
#include <cstdint>

#include "game/goals/objective.h"

namespace Scripts {
class CStruct;
} // namespace Script

namespace GoalSystem {

// A competition (vtable 0x0064D9F0, 0x78 bytes): the medals by the competition's place (the skate module's
// GameModules::CContest), the best place its record.
class CContestObjective : public CObjective {
public:
    bool m_isHalted;      // +0x70: paused by the competition (FreezeContest)
    bool m_hasTopPrize;     // +0x71
    bool m_hasSecondPlace;   // +0x72
    bool m_hasThirdPlace;   // +0x73
    int m_prizePaid;  // +0x74 (reward_given)

    // 0x005517C0: the constructor (CObjective's; no medals, the record 0x7FFFFFFF, the normal end of run). Returns this.
    CContestObjective* MakeNew(Scripts::CStruct* params);
    // 0x00551800 / 0x00551A00 (vtable +0x00): the destructor body / the scalar deleting destructor.
    void DestroyBody();
    CContestObjective* Teardown(uint8_t flags);
    // 0x00551810 (vtable +0x14): the expire script only. Whether there was one.
    bool Expire();
    // 0x00551820 / 0x00551830 (vtable +0x30 / +0x38): active (and not paused) / inactive; the competition's pause
    // cleared.
    void ChangeLive();
    void ClearEngagedBit();
    // 0x00551840 (vtable +0x54): won (no net game variant).
    bool HasWon();
    // 0x00551850 (vtable +0x74): not won any more (the medals but gold - and gold too if still won, which it can't
    // be: the bit is cleared first). Whether it was.
    bool UnBeat();
    // 0x00551880 / 0x00551890: the competition's pause on / off (off also clears the end of run started).
    void FreezeContest();
    void ThawContest();
    // 0x005518A0 (vtable +0x48): paused (0 / 1 in all of EAX).
    int IsFrozen();
    // 0x005518C0 (vtable +0x18) / 0x00551970 (vtable +0x1C): the medals, reward_given and win_record read / written
    // with CObjective's. (Any medal read sets gotBronze, as the original.)
    void RestoreSaveBlock(Scripts::CStruct* data);
    void CaptureSaveBlock(Scripts::CStruct* data);
    // 0x00551A30 (vtable +0x68): won - the medals of the place (1: all three, 2: silver and bronze, 3: bronze).
    void MarkWon();
    // 0x00551A90 (vtable +0x3C): won: the deactivate script, inactive, the place a record (lower), the rewards the
    // first time, the success script, marked won. True.
    bool Win();
};
static_assert(offsetof(CContestObjective, m_isHalted) == 0x70 && offsetof(CContestObjective, m_prizePaid) == 0x74 &&
                  sizeof(CContestObjective) == 0x78,
              "layout must match the original");

// A film goal (vtable 0x0064DC60, 0x88 bytes): keep the target in the camera's view for long enough (or pass its
// checkpoints).
class CShootVideoObjective : public CObjective {
public:
    bool m_recordingShot;          // +0x70 (StartFilming)
    uint8_t m_pad71[3];
    uint32_t m_shotHoldTime; // +0x74 ms
    uint32_t m_timeNeeded;   // +0x78 ms (total_time_required)
    uint32_t m_unknown7C;    // +0x7C (always 0: 0 times time)
    int m_checkpoints;       // +0x80 (0xC808F9FB: the checkpoints to pass)
    int m_checkpointsPassed; // +0x84

    // 0x005527D0: the constructor. Returns this. 0x005527F0 / 0x005528A0 (vtable +0x00): the destructor body / the
    // scalar deleting destructor.
    CShootVideoObjective* MakeNew(Scripts::CStruct* params);
    void DestroyBody();
    CShootVideoObjective* Teardown(uint8_t flags);
    // 0x00552800 (vtable +0x2C): started: no time on camera; the time needed or the checkpoints; CObjective's Activate.
    bool Activate();
    // 0x005528D0: the target (film_target) is in the camera's view (its bounding sphere not hidden) and closer
    // than max_distance_to_target.
    bool TargetInView();
    // 0x00552A10 (vtable +0x44): last_time_on_camera; while filming: the time on camera counted, won when enough,
    // lost when it can't be any more; else won by the checkpoints. Otherwise CObjective's Update.
    bool TickFrame();
    // 0x00552AF0: a checkpoint passed with the target in view (won after the last).
    void Checkpoint();
};
static_assert(offsetof(CShootVideoObjective, m_shotHoldTime) == 0x74 && offsetof(CShootVideoObjective, m_checkpointsPassed) == 0x84 &&
                  sizeof(CShootVideoObjective) == 0x88,
              "layout must match the original");

// A find gaps goal (vtable 0x0064DCD8; the goal manager allocates 0x74 bytes, the last word never used): find
// num_gaps_to_find of the level's gaps.
class CHuntGapsObjective : public CObjective {
public:
    uint32_t m_unused70; // +0x70
    // 0x00552B30: the constructor. Returns this. 0x00552B50 / 0x00552B60 (vtable +0x00): the destructor body /
    // the scalar deleting destructor.
    CHuntGapsObjective* MakeNew(Scripts::CStruct* params);
    void DestroyBody();
    CHuntGapsObjective* Teardown(uint8_t flags);
    // 0x00552B90: the skater's gaps not found before counted (goal_find_gaps_found_gap for new ones).
    void ScanForGaps();
    // 0x00552D20 (vtable +0x2C): started: no gaps found yet; CObjective's Activate.
    bool Activate();
};
static_assert(sizeof(CHuntGapsObjective) == 0x74, "the size the goal manager allocates");

} // namespace Game
