#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's end-of-run component (SkaterEndRun, type 0xB1D5A3E1, vtable 0x0064BDB8, 0x1C bytes): where the skater
// is in the end of a run - the run's end started and done (the scripts' EndOfRunStarted / EndofRunDone), the same for
// a goal's run, and whether ForceEndOfRun has been run. In a network game, the local skater tells the server once
// its run is over. From the disassembly of 0x00510D20-0x00511001.
//
// Vtable: 0 Destroy, 1 Update, 12 CallMemberFunction; the rest the base's or shared empty functions.
namespace Obj {

// CSkaterEndRunComponent::m_flags (+0x18)
constexpr uint32_t END_RUN_STARTED = 0x01;      // EndOfRunStarted
constexpr uint32_t END_RUN_DONE = 0x02;         // EndofRunDone: the run is over
constexpr uint32_t END_RUN_FORCED = 0x04;       // ForceEndOfRun has been run (EndRun)
constexpr uint32_t END_RUN_GOAL_STARTED = 0x08; // Goal_EndOfRunStarted
constexpr uint32_t END_RUN_GOAL_DONE = 0x10;    // Goal_EndOfRunDone
constexpr uint32_t END_RUN_FLAG_20 = 0x20;      // cleared by Goal_EndOfRunDone and RunStarted; never set here

class CSkaterEndRunComponent : public CBaseComponent {
public:
    // 0x00510F90 (registered by RegisterComponentTypes): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x00510D20: the destructor body (the base's).
    void DestroyBody();
    // 0x00510D40 (vtable slot 0)
    CSkaterEndRunComponent* Destroy(unsigned int flags);

    // 0x00510D70 (vtable +0x04): outside a multiplayer game the component suspends itself. A local skater whose run
    // is over tells the network manager (0x00533250: message 0x59 to the server) unless it has already.
    void Update();
    // 0x00510DC0 (vtable +0x30): the script member functions IsInEndOfRun (the end started but not done),
    // EndOfRunStarted, EndofRunDone, ClearEndofRunStarted, Goal_EndOfRunStarted, Goal_EndOfRunDone and RunStarted
    // (every flag cleared): 1 / 0; 2 for any other name.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x00510D30: the flags, copied into `out` (returned).
    uint32_t* GetFlags(uint32_t* out);
    // 0x00510EA0: end the run. Unless `force`, a game mode that doesn't end runs here (0x00553550) only sends the
    // skater RunHasEnded and clears END_RUN_DONE. Otherwise, once: END_RUN_FORCED, and a local skater runs
    // ForceEndOfRun.
    void EndRun(bool force);
    // 0x00510F10: the goal's run is over: END_RUN_GOAL_DONE cleared and the skater sent GoalHasEnded. (`unused` isn't
    // read.)
    void EndGoalRun(void* unused);
    // 0x00510F30 / 0x00510F40 / 0x00510F50: END_RUN_DONE, END_RUN_GOAL_DONE, END_RUN_STARTED set.
    bool RunDone();
    bool GoalRunDone();
    bool EndOfRunStarted();
    // 0x00510F60 / 0x00510F70 / 0x00510F80: END_RUN_STARTED, END_RUN_GOAL_STARTED, END_RUN_FORCED cleared.
    void ClearEndOfRunStarted();
    void ClearGoalEndOfRunStarted();
    void ClearForced();

    uint32_t m_flags; // +0x18 END_RUN_*
};
static_assert(offsetof(CSkaterEndRunComponent, m_flags) == 0x18 && sizeof(CSkaterEndRunComponent) == 0x1C,
              "layout must match the original (0x00510F90 allocates 0x1C bytes)");

} // namespace Obj
