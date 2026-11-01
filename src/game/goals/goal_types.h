#pragma once

// The goal types of 0x005517C0-0x00552E0F: competition, film, find gaps (each a goals::Objective with a vtable of its
// own: the slots it changes are its own functions, the others Objective's; see objective.h).

#include <cstddef>
#include <cstdint>

#include "game/goals/objective.h"

namespace script {
class Struct;
} // namespace script

namespace goals {

// A competition (vtable 0x0064D9F0, 0x78 bytes): the medals by the competition's place (the skate module's
// modules::Contest), the best place its record.
class ContestObjective final : public Objective {
public:
    ContestObjective(const ContestObjective&) = delete;
    ContestObjective& operator=(const ContestObjective&) = delete;
    bool is_halted_;        // +0x70: paused by the competition (freeze_contest)
    bool has_top_prize_;    // +0x71
    bool has_second_place_; // +0x72
    bool has_third_place_;  // +0x73
    int prize_paid_;        // +0x74 (reward_given)

    // 0x005517C0: the constructor (Objective's; no medals, the record 0x7FFFFFFF, the normal end of run).
    explicit ContestObjective(script::Struct* params);
    // 0x00551800 / 0x00551A00 (vtable +0x00): the destructor body / the scalar deleting destructor.
    ~ContestObjective() override;
    // 0x00551810 (vtable +0x14): the expire script only (whether there was one in EAX: unread).
    void expire() override;
    // 0x00551820 / 0x00551830 (vtable +0x30 / +0x38): active (and not paused) / inactive; the competition's pause
    // cleared.
    void change_live() override;
    void clear_engaged_bit() override;
    // 0x00551840 (vtable +0x54): won (no net game variant).
    bool has_won() override;
    // 0x00551850 (vtable +0x74): not won any more (the medals but gold - and gold too if still won, which it can't
    // be: the bit is cleared first). Whether it was.
    bool un_beat() override;
    // 0x00551880 / 0x00551890: the competition's pause on / off (off also clears the end of run started).
    void freeze_contest();
    void thaw_contest();
    // 0x005518A0 (vtable +0x48): paused.
    bool is_frozen() override;
    // 0x005518C0 (vtable +0x18) / 0x00551970 (vtable +0x1C): the medals, reward_given and win_record read / written
    // with Objective's. (Any medal read sets gotBronze, as the original.)
    void restore_save_block(script::Struct* data) override;
    void capture_save_block(script::Struct* data) override;
    // 0x00551A30 (vtable +0x68): won - the medals of the place (1: all three, 2: silver and bronze, 3: bronze).
    void mark_won() override;
    // 0x00551A90 (vtable +0x3C): won: the deactivate script, inactive, the place a record (lower), the rewards the
    // first time, the success script, marked won. True.
    bool get_win() override;
};

// A film goal (vtable 0x0064DC60, 0x88 bytes): keep the target in the camera's view for long enough (or pass its
// checkpoints).
class ShootVideoObjective final : public Objective {
public:
    ShootVideoObjective(const ShootVideoObjective&) = delete;
    ShootVideoObjective& operator=(const ShootVideoObjective&) = delete;
    bool recording_shot_{}; // +0x70 (StartFilming)
    uint8_t pad71_[3]{};
    uint32_t shot_hold_time_{}; // +0x74 ms
    uint32_t time_needed_{};    // +0x78 ms (total_time_required)
    uint32_t unknown7c_{};      // +0x7C (always 0: 0 times time)
    int checkpoints_{};         // +0x80 (0xC808F9FB: the checkpoints to pass)
    int checkpoints_passed_{};  // +0x84

    // 0x005527D0: the constructor. 0x005527F0 / 0x005528A0 (vtable +0x00): the destructor body / the
    // scalar deleting destructor.
    explicit ShootVideoObjective(script::Struct* params);
    ~ShootVideoObjective() override;
    // 0x00552800 (vtable +0x2C): started: no time on camera; the time needed or the checkpoints; Objective's Activate.
    bool activate() override;
    // 0x005528D0: the target (film_target) is in the camera's view (its bounding sphere not hidden) and closer
    // than max_distance_to_target.
    bool target_in_view();
    // 0x00552A10 (vtable +0x44): last_time_on_camera; while filming: the time on camera counted, won when enough,
    // lost when it can't be any more; else won by the checkpoints. Otherwise Objective's Update.
    bool tick_frame() override;
    // 0x00552AF0: a checkpoint passed with the target in view (won after the last).
    void checkpoint();
};

// A find gaps goal (vtable 0x0064DCD8; the goal manager allocates 0x74 bytes, the last word never used): find
// num_gaps_to_find of the level's gaps.
class HuntGapsObjective final : public Objective {
public:
    HuntGapsObjective(const HuntGapsObjective&) = delete;
    HuntGapsObjective& operator=(const HuntGapsObjective&) = delete;
    uint32_t unused70_{}; // +0x70
    // 0x00552B30: the constructor. 0x00552B50 / 0x00552B60 (vtable +0x00): the destructor body /
    // the scalar deleting destructor.
    explicit HuntGapsObjective(script::Struct* params);
    ~HuntGapsObjective() override;
    // 0x00552B90: the skater's gaps not found before counted (goal_find_gaps_found_gap for new ones).
    void scan_for_gaps();
    // 0x00552D20 (vtable +0x2C): started: no gaps found yet; Objective's Activate.
    bool activate() override;
};
static_assert(sizeof(HuntGapsObjective) == 0x74, "the size the goal manager allocates");

} // namespace goals
