#pragma once

#include <cstdint>

#include "base/core/enum_ops.h"

namespace online {

// The join's state (JoinState, 0x005331F0 / 0x00533200).
enum class EntryPhase : uint8_t {
    kNone = 0, // no join
    kConnecting = 1,
    kJoining = 2,
    kJoiningWithPassword = 3,
    kTryingPassword = 4,
    kCdKey = 5, // answered the host's CD-key challenge
    kJoined = 6, // the host let the player in
    kEntered = 7, // the network game's script has run
    kOver = 8, // the join is over
    kRefused = 9, // (the join-state task shows the refusal: CreateJoinRefusedDialog with refused_reason_)
    kCancelled = 10,
};

// NOT ORIGINAL (the class; its fields are the network manager's +0x198-+0x1A7): how far a join has got and the
// server it is joining (or joined) - its public and private address and its port. The join itself (the connection
// requests, the answers, the tasks that show it) stays with the hub. Owned by the hub, in its place there.
class JoinState {
public:
    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    // 0x005331F0 / 0x00533200: the join's phase.
    void set_entry_phase(EntryPhase state);
    [[nodiscard]] EntryPhase get_entry_phase() const;
    // (the phase is kept in the original's 32-bit word)
    [[nodiscard]] EntryPhase join_phase() const { return static_cast<EntryPhase>(join_phase_); }
    void set_join_phase(EntryPhase value) { join_phase_ = core::to_underlying(value); }
    // 0x00532E40 / 0x00532E50 / 0x00532E60 / 0x00532E70 / 0x00532E80 / 0x00532E90: the server joined: join_ip_,
    // join_private_ip_, join_port_.
    [[nodiscard]] uint32_t get_join_ip() const;
    [[nodiscard]] uint32_t get_join_private_ip() const;
    [[nodiscard]] int get_entry_port() const;
    void set_join_ip(uint32_t ip);
    void set_join_private_ip(uint32_t ip);
    void set_entry_port(int port);

private:
    int join_phase_{};           // +0x00 EntryPhase
    uint32_t join_ip_{};         // +0x04
    uint32_t join_private_ip_{}; // +0x08
    int join_port_{};            // +0x0C
};

} // namespace online
