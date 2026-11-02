// The skater's end-of-run component (GameObjects::RiderSessionEnd): see end_run.h.

#include "game/rider/end_run.h"

#include <cstdint>
#include <gsl/pointers>

#include "engine/net/gamenet_manager.h"
#include "engine/script/cscript.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

constexpr uint32_t kSkaterEndRun = 0xB1D5A3E1;

// Script member functions
constexpr uint32_t kIsInEndOfRun = 0x95BDCFCD, kEndOfRunStarted = 0x962DEA01, kEndofRunDone = 0x4C58771E,
                   kClearEndofRunStarted = 0xEA77E667, kGoalEndOfRunStarted = 0x00AFC6A7,
                   kGoalEndOfRunDone = 0x69A9E37F, kRunStarted = 0xB866BF9B;
// Events and scripts
constexpr uint32_t kRunHasEnded = 0x822E13A9, kGoalHasEnded = 0xAB676175, kForceEndOfRun = 0xF4CE3E97;

// modules::Skate (0x007CE478) and its 0x005651F0: a multiplayer game (a network game, or a game mode for more than one
// player). Not rewritten yet.
bool is_versus_session() { return modules::Skate::is_multiplayer() != 0; }
// The game mode (0x007CE470)'s 0x00553550: whether a run is over at its end here (multiplayer, and the mode's
// stop_at_zero if it has the setting). Not rewritten yet.
bool game_mode_ends_runs() { return goals::g_game_mode->clock_halts_at_zero(); }
// The network manager (0x007CCE14): whether it has told the server the local run is over (0x005332A0: its +0x39
// matches +0x38), and telling it (0x00533250).
bool server_told_run_ended() { return online::g_online_hub->game_ended_sent(); }
void tell_server_run_ended() { online::g_online_hub->send_game_ended(); }

// The skater object's +0xBC: a local skater (this machine's player).
bool is_local_skater(GameEntity* object) { return object->is_local_skater_ != 0; }

} // namespace

// 0x00510F90
gsl::owner<ObjectModule*> RiderSessionEnd::fabricate() { return new RiderSessionEnd; }

// (the constructor: inlined in fabricate in the original)
RiderSessionEnd::RiderSessionEnd() { kind_tag_ = kSkaterEndRun; }

// 0x00510D20 (0x00510D40 the deleting one)
RiderSessionEnd::~RiderSessionEnd() = default;

// 0x00510D70
void RiderSessionEnd::tick_frame()
{
    if (!is_versus_session()) {
        hold_off(true);
        return;
    }
    if (is_local_skater(entity_) && (flag_bits_ & kEndRunDone) && !server_told_run_ended())
        tell_server_run_ended();
}

// 0x00510DC0
int RiderSessionEnd::invoke_method_by_name(uint32_t name, script::Struct* /*params*/, script::Script* /*script*/)
{
    switch (name) {
    case kIsInEndOfRun:
        return (flag_bits_ & kEndRunStarted) && !(flag_bits_ & kEndRunDone) ? 1 : 0;
    case kGoalEndOfRunStarted:
        flag_bits_ |= kEndRunGoalStarted;
        return 1;
    case kEndofRunDone:
        flag_bits_ |= kEndRunDone;
        flag_bits_ &= ~kEndRunForced;
        return 1;
    case kGoalEndOfRunDone:
        flag_bits_ |= kEndRunGoalDone;
        flag_bits_ &= ~kEndRunFlag20;
        return 1;
    case kEndOfRunStarted:
        flag_bits_ |= kEndRunStarted;
        return 1;
    case kRunStarted:
        flag_bits_ &=
            ~(kEndRunStarted | kEndRunGoalStarted | kEndRunDone | kEndRunGoalDone | kEndRunForced | kEndRunFlag20);
        return 1;
    case kClearEndofRunStarted:
        flag_bits_ &= ~(kEndRunStarted | kEndRunForced);
        return 1;
    default:
        return 2;
    }
}

// 0x00510D30
uint32_t* RiderSessionEnd::fetch_bits(uint32_t* out)
{
    *out = flag_bits_;
    return out;
}

// 0x00510EA0
void RiderSessionEnd::end_run(bool force)
{
    if (!force && !game_mode_ends_runs()) {
        entity_->post_own_event(kRunHasEnded, nullptr);
        flag_bits_ &= ~kEndRunDone;
        return;
    }
    if (!(flag_bits_ & kEndRunForced) && !(flag_bits_ & kEndRunDone)) {
        flag_bits_ |= kEndRunForced;
        if (is_local_skater(entity_))
            script::launch_script(kForceEndOfRun, nullptr, entity_, false, nullptr);
    }
}

// 0x00510F10
void RiderSessionEnd::finish_objective_run()
{
    flag_bits_ &= ~kEndRunGoalDone;
    entity_->post_own_event(kGoalHasEnded, nullptr);
}

// 0x00510F30
bool RiderSessionEnd::run_done() { return (flag_bits_ & kEndRunDone) != 0; }
// 0x00510F40
bool RiderSessionEnd::goal_run_done() { return (flag_bits_ & kEndRunGoalDone) != 0; }
// 0x00510F50
bool RiderSessionEnd::end_of_run_started() { return (flag_bits_ & kEndRunStarted) != 0; }
// 0x00510F60
void RiderSessionEnd::clear_end_of_run_started() { flag_bits_ &= ~kEndRunStarted; }
// 0x00510F70
void RiderSessionEnd::clear_goal_end_of_run_started() { flag_bits_ &= ~kEndRunGoalStarted; }
// 0x00510F80
void RiderSessionEnd::clear_forced() { flag_bits_ &= ~kEndRunForced; }

} // namespace objects
