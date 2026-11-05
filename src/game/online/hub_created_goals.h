#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace online {

constexpr size_t kHubCreatedGoalsSize = 0x1770;

// NOT ORIGINAL (the class; its fields are the network manager's +0x1D8-+0x194F): the message of the goals the goal
// editor made for a level - the level, then the goals - sent to the players of a created park, its size and level.
// Owned by the hub, in its place there.
class CreatedGoalsMessage {
public:
    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    // 0x005333A0: the goal editor's goals for `level` written into the goals message (+0x1D8: the level, then the
    // goals - at most 6000 bytes); +0x1948 its size, +0x194C the level.
    void prepare_created_goals(uint32_t level);
    // 0x005333E0 / 0x005333F0: the goals message / its size.
    void* get_created_goals_message();
    [[nodiscard]] uint32_t get_created_goals_size() const;

private:
    // The level, then the goals. The goal editor is given 6000 bytes after the level - 4 more than the buffer holds,
    // as in the original (they would reach the size, which is written after - so the size stays right after it).
    std::array<uint8_t, kHubCreatedGoalsSize> created_goals_{}; // +0x0000
    uint32_t created_goals_size_{0};                            // +0x1770
    uint32_t created_goals_level_{0};                           // +0x1774
};

} // namespace online
