#pragma once

#include <cstdint>

#include "engine/memory/arena_alloc.h"

namespace net {
struct Conn;
} // namespace net

namespace online {

class PlayerRecord;
struct JoinerRecord;

// A walk over the players (PlayerRoster::begin_player_walk / step_player_walk) and over the joining players
// (begin_joiner_walk / step_joiner_walk): the current node. The original's searches (lists::Hunt<PlayerRecord> /
// <JoinerRecord>) have vtables holding only a destructor that resets them - 0x0064C1A8 (0x00519830 / 0x0051A120),
// 0x0064C828 (0x0052DB60 / 0x0052F730), 0x0064C82C (0x0052DB70 / 0x0052F760) - not kept.
struct PlayerSearch {
    memory::ListNode* list_link_ = nullptr;
};
struct NewPlayerSearch {
    memory::ListNode* list_link_ = nullptr;
};

// NOT ORIGINAL (the class; its fields are the network manager's +0x11C-+0x143): the session's players - those in the
// game (PlayerRecord, observers too) and those still joining (JoinerRecord) - and the walks, counts and searches over
// them. Making, adding and removing players stays with the hub (it involves the connections, skaters and messages).
// Owned by the hub, in its place there.
class PlayerRoster {
public:
    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    // 0x005372C0 / 0x00537010: the first / next player of the list (+0x11C) through `search`; without `observers`,
    // observers are skipped. Null at the end.
    PlayerRecord* begin_player_walk(PlayerSearch* search, bool observers);
    PlayerRecord* step_player_walk(PlayerSearch* search, bool observers);
    // 0x00537070: the first joining player (+0x130) through `search` (then the lists' generic next, 0x004573D0).
    JoinerRecord* begin_joiner_walk(NewPlayerSearch* search);
    // The next joining player through `search` (null at the end). No address of its own: the original calls the lists'
    // generic next, folded at 0x004573D0 (NetLink::AppHost::step_conn_walk), with the manager as its unused `this`.
    static JoinerRecord* step_joiner_walk(NewPlayerSearch* search);
    // 0x005370A0: the joining player whose object id (+0x10, a signed byte) is `id`.
    JoinerRecord* fetch_incoming_player_by_obj(uint16_t id);
    // 0x00537320 / 0x005373D0 / 0x00537480: the first player (observers too) that is local (flags bit 0) / the server's
    // (bit 9) / has the crown (0x00543490). Null if none.
    PlayerRecord* get_own_player();
    PlayerRecord* get_host_player();
    PlayerRecord* get_hill_ruler();
    // 0x00537530: the players (no observers) with neither flag 0x21 (bit 0 local; bit 5) set.
    int count_other_players();
    // 0x00537620: every player's flags 0x100 / 0x20000 and +0x1C, +0x20, +0x34 cleared, its skater's score reset
    // (the SkaterScore component's +0x18 - 0x00564A10).
    void clear_roster();
    // 0x00539880: the players (no observers) on `team` plus the joining players who will play (+0x20 flags 0x18, or
    // not 4) on it (+0x28).
    int count_team_players(int team);
    // 0x00539B00: the players (non-observers, and observers with flags bit 3) plus the joining players who will play
    // and aren't among them - the original's check compares each with the byte after the ids it noted (zero), so
    // only joining players with object id 0 are left out.
    int count_players();
    // 0x0053AC40: the lowest object id (0-15) no player's skater has and no joining player (observers aside) was
    // given; 16 if none.
    int reserve_obj_id_for_player();
    // 0x0053AE10: the player (observers too) of connection `conn`, or null.
    PlayerRecord* find_player_by_sender(net::Conn* conn);
    // 0x0053AEA0: the player (not an observer) whose skater has object id `id`, or null.
    PlayerRecord* find_player_slot_by_obj(uint16_t id);

    memory::ListNode& players() { return players_; }
    [[nodiscard]] const memory::ListNode& players() const { return players_; }
    memory::ListNode& joiners() { return joiners_; }
    [[nodiscard]] const memory::ListNode& joiners() const { return joiners_; }

private:
    memory::ListNode players_{}; // +0x00 lists::Foremost<PlayerRecord> (0x10 bytes here, 0x14 in the original)
    memory::ListNode joiners_{}; // +0x14 in the original: lists::Foremost<JoinerRecord>, the joining players
};

} // namespace online
