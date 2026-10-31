#pragma once

// The horse, network and race goals (0x005608C0-0x0056123F): CObjective's subclasses for HORSE (its spots), the network
// game modes' goals (elimiskate, scavenger hunt, goal attack, the timed modes) and races (laps of waypoints). Each
// overrides some of CObjective's vtable slots (goal.h); the rest are CObjective's.

#include <cstdint>

#include "game/goals/goal.h"

namespace GoalSystem {

// A HORSE goal (vtable 0x0064E0D8, 0x70 bytes): skate to each horse spot and beat its score.
class CLetterGameObjective : public CObjective {
public:
    // 0x005608C0: the constructor (CObjective's). Returns this. 0x005608E0 / 0x00560980 (vtable +0x00): the destructor
    // body / the scalar deleting destructor.
    CLetterGameObjective* MakeNew(Scripts::CStruct* params);
    void DestroyBody();
    CLetterGameObjective* Teardown(uint8_t flags);
    // 0x00560930 / 0x005608F0 (vtable +0x04 / +0x08): never while 0xA32FAF13 is 1, else CObjective's.
    bool WantsToEnd();
    bool HasRunOut();
    // 0x00560970 (vtable +0x14): CObjective's (a jump to it).
    void Expire();
    // 0x005609B0: the skater landed: the first horse spot (0x92C6E839) whose flag isn't set yet - beaten (its
    // score, else the goal's) its flag set and on to the next spot, else the goal lost; 0xA32FAF13 cleared.
    void Land();
};

// A network game mode's goal (vtable 0x0064E150, 0x74 bytes).
class COnlineObjective : public CObjective {
public:
    bool m_isPrepared; // +0x70 (Update initializes it once)
    uint8_t m_pad71[3];

    // 0x00560AC0: the constructor (the normal end of run). Returns this. 0x00560AF0 / 0x00560B40: the destructor
    // body / the scalar deleting destructor.
    COnlineObjective* MakeNew(Scripts::CStruct* params);
    void DestroyBody();
    COnlineObjective* Teardown(uint8_t flags);
    // 0x00560B00 (vtable +0x28): the timer shown: a time limit (0 / 1 in all of EAX).
    int HasComponentA302D7CD();
    // 0x00560B10 (vtable +0x24): the timer beeps - not for elimiskate_interval / scavenger_interval.
    bool TimerBeeps();
    // 0x00560BC0 (vtable +0x2C): the game not over any more; CObjective's Activate.
    bool Activate();
    // 0x00560BE0 (vtable +0x34): not in a goal attack unless forced; CObjective's Deactivate.
    bool Disengage(bool force, bool resetTree);
    // 0x00560B70 (vtable +0x44): initialized once; in a goal attack expired once its victory condition is met;
    // else CObjective's Update.
    bool TickFrame();
    // 0x00560C20 (vtable +0x04): whether the game is over - by the mode (elimiskate / scavenger hunt: out of time;
    // goal attack: everyone's goals done and their runs over), no players, or out of time with every run done
    // (the runs still going ended).
    bool WantsToEnd();
    // 0x00560E20 (vtable +0x14): the game over (unless game_continues_after_expiration), no time left, CObjective's
    // Expire.
    void Expire();
};

// A race (vtable 0x0064E1C8, 0x7C bytes): laps of race_waypoints.
class CRaceObjective : public CObjective {
public:
    int m_laps;        // +0x70 (race_total_laps; 1)
    int m_lap;         // +0x74 (from 1)
    bool m_addTime;    // +0x78 (the 0xA4038EC9 component: each waypoint's time added)
    uint8_t m_pad79[3];

    // 0x00560E60: the constructor. Returns this. 0x00560EE0 / 0x00560F60: the destructor body / the scalar deleting
    // destructor.
    CRaceObjective* MakeNew(Scripts::CStruct* params);
    void DestroyBody();
    CRaceObjective* Teardown(uint8_t flags);
    // 0x00560EF0 (vtable +0x2C): the first lap; CObjective's Activate.
    bool Activate();
    // 0x00560F00 (vtable +0x08): out of time (with a time limit).
    bool HasRunOut();
    // 0x00560F20 (vtable +0x04): the same (CObjective's WantsToEnd run, its answer not used).
    bool WantsToEnd();
    // 0x00560F40 (vtable +0x14): the expire script; deactivated.
    void Expire();
    // 0x00560F90: the next waypoint not passed started (goal_race_next_waypoint: start_lap, last_waypoint,
    // quick_start, first_waypoint or the checkpoint message and its added time) - after the last, the next lap
    // (the flags reset) or the end. False if the goal should expire.
    bool AdvanceRaceGate(uint32_t id);
};

} // namespace Game
