#pragma once

// The horse, network and race goals (0x005608C0-0x0056123F): Objective's subclasses for HORSE (its spots), the network
// game modes' goals (elimiskate, scavenger hunt, goal attack, the timed modes) and races (laps of waypoints). Each
// overrides some of Objective's vtable slots (objective.h); the rest are Objective's.

#include <cstdint>

#include "game/goals/objective.h"

namespace goals {

// A HORSE goal (vtable 0x0064E0D8, 0x70 bytes): skate to each horse spot and beat its score.
class LetterGameObjective final : public Objective {
public:
    LetterGameObjective(const LetterGameObjective&) = delete;
    LetterGameObjective& operator=(const LetterGameObjective&) = delete;
    // 0x005608C0: the constructor (Objective's). 0x005608E0 / 0x00560980 (vtable +0x00): the destructor
    // body / the scalar deleting destructor.
    explicit LetterGameObjective(script::Struct* params);
    ~LetterGameObjective() override;
    // 0x00560930 / 0x005608F0 (vtable +0x04 / +0x08): never while 0xA32FAF13 is 1, else Objective's.
    bool wants_to_end() override;
    bool has_run_out() override;
    // 0x00560970 (vtable +0x14): Objective's (a jump to it).
    void expire() override;
    // 0x005609B0: the skater landed: the first horse spot (0x92C6E839) whose flag isn't set yet - beaten (its
    // score, else the goal's) its flag set and on to the next spot, else the goal lost; 0xA32FAF13 cleared.
    void land();
};
static_assert(sizeof(LetterGameObjective) == 0x70, "the size the goal manager allocated");

// A network game mode's goal (vtable 0x0064E150, 0x74 bytes).
class OnlineObjective final : public Objective {
public:
    OnlineObjective(const OnlineObjective&) = delete;
    OnlineObjective& operator=(const OnlineObjective&) = delete;
    bool is_prepared_; // +0x70 (Update initializes it once)
    uint8_t pad71_[3]{};

    // 0x00560AC0: the constructor (the normal end of run). 0x00560AF0 / 0x00560B40: the destructor
    // body / the scalar deleting destructor.
    explicit OnlineObjective(script::Struct* params);
    ~OnlineObjective() override;
    // 0x00560B00 (vtable +0x28): the timer shown: a time limit (0 / 1 in all of EAX).
    bool shows_clock() override;
    // 0x00560B10 (vtable +0x24): the timer beeps - not for elimiskate_interval / scavenger_interval.
    bool timer_beeps() override;
    // 0x00560BC0 (vtable +0x2C): the game not over any more; Objective's Activate.
    bool activate() override;
    // 0x00560BE0 (vtable +0x34): not in a goal attack unless forced; Objective's Deactivate.
    bool disengage(bool force, bool reset_tree) override;
    // 0x00560B70 (vtable +0x44): initialized once; in a goal attack expired once its victory condition is met;
    // else Objective's Update.
    bool tick_frame() override;
    // 0x00560C20 (vtable +0x04): whether the game is over - by the mode (elimiskate / scavenger hunt: out of time;
    // goal attack: everyone's goals done and their runs over), no players, or out of time with every run done
    // (the runs still going ended).
    bool wants_to_end() override;
    // 0x00560E20 (vtable +0x14): the game over (unless game_continues_after_expiration), no time left, Objective's
    // Expire.
    void expire() override;
};
static_assert(sizeof(OnlineObjective) == 0x74, "the size the goal manager allocated");

// A race (vtable 0x0064E1C8, 0x7C bytes): laps of race_waypoints.
class RaceObjective final : public Objective {
public:
    RaceObjective(const RaceObjective&) = delete;
    RaceObjective& operator=(const RaceObjective&) = delete;
    int laps_;      // +0x70 (race_total_laps; 1)
    int lap_{};     // +0x74 (from 1)
    bool add_time_; // +0x78 (the 0xA4038EC9 component: each waypoint's time added)
    uint8_t pad79_[3]{};

    // 0x00560E60: the constructor. 0x00560EE0 / 0x00560F60: the destructor body / the scalar deleting
    // destructor.
    explicit RaceObjective(script::Struct* params);
    ~RaceObjective() override;
    // 0x00560EF0 (vtable +0x2C): the first lap; Objective's Activate.
    bool activate() override;
    // 0x00560F00 (vtable +0x08): out of time (with a time limit).
    bool has_run_out() override;
    // 0x00560F20 (vtable +0x04): the same (Objective's wants_to_end run, its answer not used).
    bool wants_to_end() override;
    // 0x00560F40 (vtable +0x14): the expire script; deactivated.
    void expire() override;
    // 0x00560F90: the next waypoint not passed started (goal_race_next_waypoint: start_lap, last_waypoint,
    // quick_start, first_waypoint or the checkpoint message and its added time) - after the last, the next lap
    // (the flags reset) or the end. False if the goal should expire.
    bool advance_race_gate(uint32_t id);
};
static_assert(sizeof(RaceObjective) == 0x7C, "the size the goal manager allocated");

} // namespace goals
