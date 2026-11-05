#pragma once

#include <cstdint>

#include "engine/script/preferences.h"

namespace online {

// NOT ORIGINAL (the class; its fields are the network manager's +0x104-+0x11B): the session's settings - the network
// preferences (the host's game options, the connection, the player's name) and the taunts, and what the session reads
// from them. Owned by the hub, in its place there.
class SessionSettings {
public:
    // Both sets read from their global structures, the network one first (the hub's constructor).
    void read_in(uint32_t network_key, uint32_t taunt_key);
    // Both sets' destructors, the taunt one first (the hub's destructor's last steps; the members' own run after, as
    // they always did).
    void destroy();
    // Not original - the fields' offsets checked against the original's at compile time (gamenet_manager_create.cpp).
    static void check_layout();

    // 0x00532F70 / 0x00532F80: the network preferences (+0x104) / the taunt preferences (+0x110).
    prefs::Preferences* get_online_settings();
    prefs::Preferences* get_taunt_settings();
    prefs::Preferences& net_options() { return net_options_; }
    [[nodiscard]] const prefs::Preferences& net_options() const { return net_options_; }

    // 0x00532AE0: the network preference rank_test: 1 rank_test_less, 2 rank_test_greater, else 0.
    int get_rank_test();
    // 0x00532B30: rank_limit: num_1 ... num_9 -> 0 ... 8, else 9.
    int get_rank_limit();
    // 0x00532BE0: skill_level: num_1 ... num_4 -> 0 ... 3, num_5 -> 4, else 2.
    int get_difficulty_tier();
    // 0x00532C50: num_players: num_2 ... num_8 -> 2 ... 8, else 2.
    int get_player_limit();
    // 0x00532CF0: team_mode: teams_none 0, teams_two 2, teams_three 3, teams_four (or anything else) 4.
    int get_num_teams();
    // 0x00532D60: num_observers: num_0 ... num_8 -> 0 ... 8, else 2.
    int get_spectator_limit();
    // 0x00533080: the level preference's checksum (level).
    uint32_t get_level_preference();
    // 0x005330B0: fireball_difficulty: num_1 ... num_4 -> 1 ... 4, num_5 -> 5, else 3.
    int get_fireball_level();
    // 0x00533140: show_names is boolean_true (a missing preference: false, not asserted).
    bool wants_name_tags();
    // 0x00534090 / 0x005341B0: the network preference num_players / num_observers set to `count` - its ui_string
    // ("<n> <netmenu_str_players>"; for observers "no observers", "1 observer", "<n> observers") and checksum
    // (num_<n>). The parameters are never freed (a leak the original has).
    void set_max_players_preference(int count);
    void set_max_observers_preference(int count);
    // 0x00534310: the network preference password's ui_string.
    const char* fetch_secret();

private:
    // (prefs::Preferences, made in place by make_new; 8 bytes each here, 0xC in the original, whose sets had a table -
    // so the taunts are at +8, not +0xC, and the hub's fields after these 8 bytes before the original's: check_layout)
    prefs::Preferences net_options_;   // +0x00
    prefs::Preferences taunt_options_; // +0x0C in the original
};

} // namespace online
