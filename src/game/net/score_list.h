#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

// The network game's score list (the net_score_1 ... net_score_8 lines): GameNet::Manager::GetTeamScore /
// GetPlayerScore (gamenet_manager.h) and the task that fills the lines.
namespace GameNet {

extern void** const kScoreEntryVtable; // 0x0064CCF8: Lst::Node<ScoreEntry> (score_list.cpp)

// A line of the score list (0x48 bytes, vtable 0x0064CCF8 - its scalar deleting destructor 0x00544720, the node's
// body 0x00544700: ScoreNode in player_info.h), a node of a local list sorted by its priority, the score.
struct ScoreEntry : Mem::ListNode {
    char m_text[32];   // +0x14 the player's (or team's) name; "> " before the local one
    bool m_king;       // +0x34 has the crown
    bool m_local;      // +0x35
    bool m_hasFlag;    // +0x36 carries a flag
    uint8_t pad37;
    uint32_t m_flags;  // +0x38 the teams whose flags it carries (bit n: team n)
    int m_colour;      // +0x3C the text colour (\c): the team + 2, or the skater's id + 2
    int m_teamScore;   // +0x40 (teams)
    uint32_t m_unknown44;
};
static_assert(offsetof(ScoreEntry, m_text) == 0x14 && offsetof(ScoreEntry, m_flags) == 0x38 &&
                  offsetof(ScoreEntry, m_teamScore) == 0x40 && sizeof(ScoreEntry) == 0x48,
              "layout must match the original");

// 0x00544780 (the task's callback; the manager at the task's +0x1C - made by 0x0053EE90): at most once a second,
// clear_scores, then one line per player (team games other than the lobby: per team with players, the team's
// players' marks on it) - sorted by score (the time for king of the hill, 5 minus the player's +0x34 in a running
// scavenger hunt; in the lobby in player order) - each written to its net_score_<n> element (SetProperties with
// text and id) and to update_score. The text: a prefix (the crown's abbreviation, or \s0-\s3 for the flags carried),
// the colour, the name and the score (king of the hill: m:ss or h:mm:ss; a running scavenger hunt: no score but for
// the local player).
void __cdecl ScoreUpdateTask(void* task);

} // namespace GameNet
