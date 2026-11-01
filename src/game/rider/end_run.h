#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The skater's end-of-run component (SkaterEndRun, type 0xB1D5A3E1, vtable 0x0064BDB8, 0x1C bytes): where the skater
// is in the end of a run - the run's end started and done (the scripts' EndOfRunStarted / EndofRunDone), the same for
// a goal's run, and whether ForceEndOfRun has been run. In a network game, the local skater tells the server once
// its run is over. From the disassembly of 0x00510D20-0x00511001.
//
// Vtable: 0 Destroy, 1 Update, 12 invoke_method_by_name; the rest the base's or shared empty functions.
namespace objects {

// RiderSessionEnd::flag_bits_ (+0x18)
constexpr uint32_t kEndRunStarted = 0x01;     // EndOfRunStarted
constexpr uint32_t kEndRunDone = 0x02;        // EndofRunDone: the run is over
constexpr uint32_t kEndRunForced = 0x04;      // ForceEndOfRun has been run (EndRun)
constexpr uint32_t kEndRunGoalStarted = 0x08; // Goal_EndOfRunStarted
constexpr uint32_t kEndRunGoalDone = 0x10;    // Goal_EndOfRunDone
constexpr uint32_t kEndRunFlag20 = 0x20;      // cleared by Goal_EndOfRunDone and RunStarted; never set here

class RiderSessionEnd final : public ObjectModule {
public:
    RiderSessionEnd(const RiderSessionEnd&) = delete;
    RiderSessionEnd& operator=(const RiderSessionEnd&) = delete;
    // 0x00510F90 (registered by register_component_types): a new one (0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // The constructor (inlined in fabricate in the original).
    RiderSessionEnd();
    // 0x00510D20: the destructor body (the base's).
    ~RiderSessionEnd() override;

    // 0x00510D70 (vtable +0x04): outside a multiplayer game the component suspends itself. A local skater whose run
    // is over tells the network manager (0x00533250: message 0x59 to the server) unless it has already.
    void tick_frame() override;
    // 0x00510DC0 (vtable +0x30): the script member functions IsInEndOfRun (the end started but not done),
    // EndOfRunStarted, EndofRunDone, ClearEndofRunStarted, Goal_EndOfRunStarted, Goal_EndOfRunDone and RunStarted
    // (every flag cleared): 1 / 0; 2 for any other name.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x00510D30: the flags, copied into `out` (returned).
    uint32_t* fetch_bits(uint32_t* out);
    // 0x00510EA0: end the run. Unless `force`, a game mode that doesn't end runs here (0x00553550) only sends the
    // skater RunHasEnded and clears kEndRunDone. Otherwise, once: kEndRunForced, and a local skater runs
    // ForceEndOfRun.
    void end_run(bool force);
    // 0x00510F10: the goal's run is over: kEndRunGoalDone cleared and the skater sent GoalHasEnded. (`unused` isn't
    // read.)
    void finish_objective_run(void* unused);
    // 0x00510F30 / 0x00510F40 / 0x00510F50: kEndRunDone, kEndRunGoalDone, kEndRunStarted set.
    bool run_done();
    bool goal_run_done();
    bool end_of_run_started();
    // 0x00510F60 / 0x00510F70 / 0x00510F80: kEndRunStarted, kEndRunGoalStarted, kEndRunForced cleared.
    void clear_end_of_run_started();
    void clear_goal_end_of_run_started();
    void clear_forced();

    uint32_t flag_bits_; // +0x18 END_RUN_*
};

} // namespace objects
