#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"

// The network game's score list (the net_score_1 ... net_score_8 lines): online::Hub::get_squad_points /
// get_entrant_points (gamenet_manager.h) and the task that fills the lines.
namespace tasks {
template <typename Owner>
class Task;
} // namespace tasks
namespace online {

class Hub;

// A line of the score list (0x48 bytes), a node of a local list sorted by its priority, the score (its node keeps the
// original's size).
struct ScoreEntry : memory::ListNode {
    // 0x00544720: the scalar deleting destructor (out of its list; freed with flag 1).
    ScoreEntry* teardown(unsigned int flags);

    std::array<char, 32> text_;      // +0x14 the player's (or team's) name; "> " before the local one
    bool king_;          // +0x34 has the crown
    bool local_;         // +0x35
    bool has_flag_;      // +0x36 carries a flag
    uint32_t flag_bits_; // +0x38 the teams whose flags it carries (bit n: team n)
    int shade_;          // +0x3C the text colour (\c): the team + 2, or the skater's id + 2
    int team_score_;     // +0x40 (teams)
    uint32_t unknown44_;
};

// 0x00544780 (the task's callback; the manager at the task's +0x1C - made by 0x0053EE90): at most once a second,
// clear_scores, then one line per player (team games other than the lobby: per team with players, the team's
// players' marks on it) - sorted by score (the time for king of the hill, 5 minus the player's +0x34 in a running
// scavenger hunt; in the lobby in player order) - each written to its net_score_<n> element (change_traits with
// text and id) and to update_score. The text: a prefix (the crown's abbreviation, or \s0-\s3 for the flags carried),
// the colour, the name and the score (king of the hill: m:ss or h:mm:ss; a running scavenger hunt: no score but for
// the local player).
void score_broadcast_tick(tasks::Task<Hub>* task);

} // namespace online
