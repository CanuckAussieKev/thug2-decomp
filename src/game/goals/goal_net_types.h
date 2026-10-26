#pragma once

// The horse, network and race goals (0x005608C0-0x0056123F): CGoal's subclasses for HORSE (its spots), the network
// game modes' goals (elimiskate, scavenger hunt, goal attack, the timed modes) and races (laps of waypoints). Each
// overrides some of CGoal's vtable slots (goal.h); the rest are CGoal's.

#include <cstdint>

#include "game/goals/goal.h"

namespace Game {

// A HORSE goal (vtable 0x0064E0D8, 0x70 bytes): skate to each horse spot and beat its score.
class CHorseGoal : public CGoal {
public:
    // 0x005608C0: the constructor (CGoal's). Returns this. 0x005608E0 / 0x00560980 (vtable +0x00): the destructor
    // body / the scalar deleting destructor.
    CHorseGoal* Construct(Script::CStruct* params);
    void DestroyBody();
    CHorseGoal* Destroy(uint8_t flags);
    // 0x00560930 / 0x005608F0 (vtable +0x04 / +0x08): never while 0xA32FAF13 is 1, else CGoal's.
    bool ShouldExpire();
    bool IsExpired();
    // 0x00560970 (vtable +0x14): CGoal's (a jump to it).
    void Expire();
    // 0x005609B0: the skater landed: the first horse spot (0x92C6E839) whose flag isn't set yet - beaten (its
    // score, else the goal's) its flag set and on to the next spot, else the goal lost; 0xA32FAF13 cleared.
    void Land();
};

// A network game mode's goal (vtable 0x0064E150, 0x74 bytes).
class CNetGoal : public CGoal {
public:
    bool m_initialized; // +0x70 (Update initializes it once)
    uint8_t m_pad71[3];

    // 0x00560AC0: the constructor (the normal end of run). Returns this. 0x00560AF0 / 0x00560B40: the destructor
    // body / the scalar deleting destructor.
    CNetGoal* Construct(Script::CStruct* params);
    void DestroyBody();
    CNetGoal* Destroy(uint8_t flags);
    // 0x00560B00 (vtable +0x28): the timer shown: a time limit (0 / 1 in all of EAX).
    int HasComponentA302D7CD();
    // 0x00560B10 (vtable +0x24): the timer beeps - not for elimiskate_interval / scavenger_interval.
    bool TimerBeeps();
    // 0x00560BC0 (vtable +0x2C): the game not over any more; CGoal's Activate.
    bool Activate();
    // 0x00560BE0 (vtable +0x34): not in a goal attack unless forced; CGoal's Deactivate.
    bool Deactivate(bool force, bool resetTree);
    // 0x00560B70 (vtable +0x44): initialized once; in a goal attack expired once its victory condition is met;
    // else CGoal's Update.
    bool Update();
    // 0x00560C20 (vtable +0x04): whether the game is over - by the mode (elimiskate / scavenger hunt: out of time;
    // goal attack: everyone's goals done and their runs over), no players, or out of time with every run done
    // (the runs still going ended).
    bool ShouldExpire();
    // 0x00560E20 (vtable +0x14): the game over (unless game_continues_after_expiration), no time left, CGoal's
    // Expire.
    void Expire();
};

// A race (vtable 0x0064E1C8, 0x7C bytes): laps of race_waypoints.
class CRaceGoal : public CGoal {
public:
    int m_laps;        // +0x70 (race_total_laps; 1)
    int m_lap;         // +0x74 (from 1)
    bool m_addTime;    // +0x78 (the 0xA4038EC9 component: each waypoint's time added)
    uint8_t m_pad79[3];

    // 0x00560E60: the constructor. Returns this. 0x00560EE0 / 0x00560F60: the destructor body / the scalar deleting
    // destructor.
    CRaceGoal* Construct(Script::CStruct* params);
    void DestroyBody();
    CRaceGoal* Destroy(uint8_t flags);
    // 0x00560EF0 (vtable +0x2C): the first lap; CGoal's Activate.
    bool Activate();
    // 0x00560F00 (vtable +0x08): out of time (with a time limit).
    bool IsExpired();
    // 0x00560F20 (vtable +0x04): the same (CGoal's ShouldExpire run, its answer not used).
    bool ShouldExpire();
    // 0x00560F40 (vtable +0x14): the expire script; deactivated.
    void Expire();
    // 0x00560F90: the next waypoint not passed started (goal_race_next_waypoint: start_lap, last_waypoint,
    // quick_start, first_waypoint or the checkpoint message and its added time) - after the last, the next lap
    // (the flags reset) or the end. False if the goal should expire.
    bool NextRaceWaypoint(uint32_t id);
};

} // namespace Game
