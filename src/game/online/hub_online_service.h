#pragma once

#include <cstdint>

#include "base/core/enum_ops.h"
#include "game/online/service.h"

namespace online {

// NOT ORIGINAL (the class; its fields are the network manager's +0x1B8-+0x1C3): the link to the online services -
// the state machine the online service task (net::service_task, service.cpp) steps through: GameSpy's availability
// check, the news and the message of the day, the lobby list check - its state, the state it moves on to and when.
// Queueing and dropping the task stays with the hub (Hub::set_service_state). Owned by the hub, in its place there.
class OnlineService {
public:
    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    // 0x005451B0 / 0x005451C0: the online service's next state (+0x1BC) / its state (+0x1B8).
    void set_next_service_state(ServiceState state);
    [[nodiscard]] ServiceState get_service_state() const;
    // (the states are kept in the original's 32-bit words)
    [[nodiscard]] ServiceState service_state() const { return static_cast<ServiceState>(service_state_); }
    void assign_service_state(ServiceState value) { service_state_ = core::to_underlying(value); }
    [[nodiscard]] ServiceState next_service_state() const { return static_cast<ServiceState>(next_service_state_); }
    void assign_next_service_state(ServiceState value) { next_service_state_ = core::to_underlying(value); }
    [[nodiscard]] uint32_t service_wake_at() const { return service_wake_at_; }
    void set_service_wake_at(uint32_t value) { service_wake_at_ = value; }

private:
    int service_state_{core::to_underlying(ServiceState::kStopped)};      // +0x00 the state (service_task)
    int next_service_state_{core::to_underlying(ServiceState::kStopped)}; // +0x04 state 0 moves on to it at the wake time
    uint32_t service_wake_at_{}; // +0x08 frame_clock::get_time() milliseconds; 0: none
};

} // namespace online
