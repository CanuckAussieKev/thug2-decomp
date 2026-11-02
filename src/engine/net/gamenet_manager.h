#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <gsl/pointers>
#include <array>
#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "wire/gamenet_messages.h"
#include "engine/script/preferences.h"

namespace net {
struct AppHost;
struct Conn;
struct InboundMsgCtx;
} // namespace net
namespace objects {
class Entity;
} // namespace objects
namespace prefs {
class Preferences;
} // namespace prefs

namespace tasks {
class SchedulerTask;
}
namespace pad_input {
class InputHandler;
template <typename Owner>
class Responder;
} // namespace pad_input

// The game's network manager online::Hub (0x00533F20-0x0054EF60; the singleton at 0x007CCE14): the network
// game itself - the players, the levels and modes the server picks, the lobby. Partly rewritten: the join path
// (gamenet_join.cpp) and the level name; only the fields used so far are named.
namespace tasks {
template <typename Owner>
class Task;
} // namespace tasks
namespace objects {
class CrownObject;
} // namespace objects
namespace online {

class RoomManager;
class VaultMan;
class FriendsManager;
class RankingsManager;
class PlayerRecord;
struct JoinerRecord;

// --- The join messages (THUG2's) ---

// (the join messages: wire/gamenet_messages.h - ConnectInfo, JoinRequestBlock, ProceedInfo)

// Join refused (message 71, 10 bytes): the reason; with kRefusedCdKey, the host's CD-key challenge after it.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): values fixed by a file, a script, the wire or the SDK, used as integers
enum JoinRefusedReason : int8_t {
    kRefusedFull = 0,
    kRefusedPassword = 1,
    kRefusedVersion = 2,
    kRefusedInProgress = 3,
    kRefusedBanned = 4,
    kRefusedFullObservers = 5,
    kRefusedRankLow = 6,  // the host's "rank greater than" test
    kRefusedRankHigh = 7, // its "rank less than" test
    kRefusedCdKey = 8,    // (and the challenge: the client answers with a new connection request)
};
constexpr uint16_t kJoinRefusedSize = 10;

// The join's state (join_phase_, 0x005331F0 / 0x00533200).
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): held in the original's integer fields (to convert with them)
enum EntryPhase : uint8_t {
    kJoinStateConnecting = 1,
    kJoinStateJoining = 2,
    kJoinStateJoiningWithPassword = 3,
    kJoinStateTryingPassword = 4,
    kJoinStateCdKey = 5,   // answered the host's CD-key challenge
    kJoinStateRefused = 9, // (the join-state task shows the refusal: CreateJoinRefusedDialog with refused_reason_)
};

// A walk over the players (Hub::begin_player_walk / step_player_walk) and over the joining players
// (begin_joiner_walk / step_joiner_walk): the current node. The original's searches (lists::Hunt<PlayerRecord> /
// <JoinerRecord>) have vtables holding only a destructor that resets them - 0x0064C1A8 (0x00519830 / 0x0051A120),
// 0x0064C828 (0x0052DB60 / 0x0052F730), 0x0064C82C (0x0052DB70 / 0x0052F760) - not kept.
struct PlayerSearch {
    memory::ListNode* list_link_ = nullptr;
};
struct NewPlayerSearch {
    memory::ListNode* list_link_ = nullptr;
};

class Hub {
public:
    // 0x005369A0: the current network level's name - the `text` of its entry in the level select's list
    // (`level`, a load script's checksum, matching m_level); for the created park (load_sk5ed_gameplay) with
    // `parkName` set, the park's own name. Null when the level is in no list.
    // The original looks in level_select_menu_level_info only, THUG2's own levels: ours also looks in the lists
    // of the ported levels (tools/pipeline/menu_level/level_select.ts sections()), which the original's null crashed on
    // (0x005311F0, the server's level change, adds it to a structure as a string).
    const char* get_map_name(bool park_name);
    // 0x0044E4E0: whether the network mode bits (+0, bits 2-3) are set: 1 or 0.
    int is_network_game();
    // 0x0044E4F0: whether a network game is on: in network mode, with a server or client app that isn't local.
    bool in_net_game();

    // NOT ORIGINAL: the level select's name for `level` (a load script), in THUG2's list or the ported levels' -
    // get_map_name's lookup for any level (null if none).
    static const char* level_list_name(uint32_t level);
    // 0x00533F90 (inline here): the server's side of the game (OnServer): flags bit 0.
    [[nodiscard]] bool get_on_server() const { return (flag_bits_ & 1) != 0; }
    // 0x00533F80 (inline here): internet play (flags bit 2), not LAN.
    [[nodiscard]] bool in_internet_mode() const { return (flag_bits_ & 4) != 0; }

    // --- gamenet_state.cpp: the manager's small members (0x00532AA0-0x005333F0) ---
    // 0x00532AA0 / 0x00532AC0: how the player joins (join_mode_: 0 to play, 1 to watch - IsObserving). Setting it
    // clears the join-by-address flag (0x007CCA88, TryJoinServerIPSuccess).
    void set_join_mode(int mode);
    int get_entry_mode();
    // 0x00532AD0: host_mode_ (1: the task at +0xF4 is removed when the player list is sent - 0x0056B890).
    int get_hosting_role();
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
    // 0x00533130: the player-names flag (+0x47) toggled (TogglePlayerNames).
    void toggle_player_names();
    // 0x00532FE0: the game mode (+0x30) has no time limit: goal attack, king of the hill, score challenge (and
    // their net versions) always; else, in a network game, a capture the flag that isn't timed_ctf, or the
    // stop_at_zero preference set.
    bool is_untimed_game();
    // 0x00532E30: the task at +0xF4 taken off its list.
    void remove_host_task();
    // 0x00532E40 / 0x00532E50 / 0x00532E60 / 0x00532E70 / 0x00532E80 / 0x00532E90: the server joined: join_ip_,
    // join_private_ip_, join_port_.
    uint32_t get_join_ip();
    uint32_t get_join_private_ip();
    int get_entry_port();
    void set_join_ip(uint32_t ip);
    void set_join_private_ip(uint32_t ip);
    void set_entry_port(int port);
    // 0x00532EA0: client `index` (client_slot).
    net::AppHost* fetch_patron(int index);
    // 0x00532EB0: the server list's client (browser_client_).
    net::AppHost* get_matchmaking_client();
    // 0x00532EC0: viewed_player_.
    PlayerRecord* fetch_viewed_player();
    // 0x00532ED0: ask for servers on the LAN: message 0x11 (the time now, 12 bytes) through the lobby client to the
    // address asked for (0x007CCA84, once), else broadcast on port 5153 and to the address from the command line.
    void scan_local_network();
    // 0x00532F70 / 0x00532F80: the network preferences (+0x104) / the taunt preferences (+0x110).
    prefs::Preferences* get_online_settings();
    prefs::Preferences* get_taunt_settings();
    // 0x00532F90 / 0x00532FD0: the eight words at +0x48 (the start info's, message 0x34) / one of them.
    void set_start_info_words(const uint32_t* words);
    uint32_t get_start_info_word(int index);
    // 0x00533060 / 0x00533070: the current game's number (+0x38).
    void set_game_id(uint8_t id);
    uint8_t get_game_id();
    // 0x00533250: tell the server the game has ended here (message 0x59, the game's number - once every player's
    // has, the server sends 0x5A, game over) and note the number (+0x39).
    void send_game_ended();
    // 0x005332A0: the end of the current game has been reported.
    bool game_ended_sent();
    // 0x005332B0 / 0x005332C0 / 0x005332D0: match_ended_ (the server's message 0x5A).
    bool has_match_ended();
    void mark_match_ended();
    void reset_match_ended();
    // 0x005332E0 / 0x005332F0: the cheating notice has been shown (+0x3B).
    bool cheating_noted();
    void note_cheating();
    // 0x00533180 / 0x00533190 / 0x005331A0: the last player (+0x1D0) / team (+0x1D4) announced as the leader.
    void set_last_leader(PlayerRecord* player);
    PlayerRecord* get_last_leader();
    int get_last_leading_team();
    // 0x005331B0 / 0x005331C0: score_synced_at_.
    void change_score_sync_stamp(uint32_t time);
    uint32_t fetch_score_sync_stamp();
    // 0x005331D0 / 0x005331E0: the final scores have come (+0x44).
    void set_final_scores_received(bool received);
    bool final_scores_received();
    // 0x005331F0 / 0x00533200: join_phase_.
    void set_entry_phase(int state);
    int get_entry_phase();
    // 0x00533210 / 0x00533220: the crown of king of the hill (+0x6C) / forgotten.
    objects::CrownObject* get_crown_holder();
    void clear_crown();
    // 0x00533230: the crown's restart point (+0x68), a signed byte.
    void set_crown_start_spot(char point);
    // 0x00533240: +0x9950.
    void set_room_id(uint32_t id);
    // 0x00533300: message 0x26 - the ready query's id the server sent (last_ready_probe_id_, message 0x25) back to the
    // server through each client (the player's connection is ready again).
    void send_ready_response();
    // 0x00533360: the buffer at +0xFDF8 (its size in *size, +0xFDFC).
    void* get_pending_data(uint32_t* size);
    // 0x00533380: the goal editor has created goals (0x004E7E50 with 0).
    bool has_created_goals();
    // 0x005333A0: the goal editor's goals for `level` written into the goals message (+0x1D8: the level, then the
    // goals - at most 6000 bytes); +0x1948 its size, +0x194C the level.
    void prepare_created_goals(uint32_t level);
    // 0x005333E0 / 0x005333F0: the goals message / its size.
    void* get_created_goals_message();
    uint32_t get_created_goals_size();

    // --- gamenet_members.cpp: the network mode, the team flags, preferences, the server's end (0x00533F20-0x00534FC0)
    // 0x00533F20: the network mode (flags bits 2-3): 1 LAN (bit 3), 2 internet (bit 2), else neither.
    void set_network_mode(int mode);
    // 0x00533F50: the server's side (flags bit 0) on or off.
    void set_server_mode(bool on);
    // 0x00533F70: LAN play (flags bit 3).
    int is_local_network_play();
    // 0x00533FA0: the client's side (flags bit 1).
    int is_client_side();
    // 0x00533FB0: Kill_Team_Flags, then (with teams) create_team_flags {num_teams = n}.
    void build_team_flag_set(int num_teams);
    // 0x00534090 / 0x005341B0: the network preference num_players / num_observers set to `count` - its ui_string
    // ("<n> <netmenu_str_players>"; for observers "no observers", "1 observer", "<n> observers") and checksum
    // (num_<n>). The parameters are never freed (a leak the original has).
    void set_max_players_preference(int count);
    void set_max_observers_preference(int count);
    // 0x00534310: the network preference password's ui_string.
    const char* fetch_secret();
    // 0x00534340: whether skaters collide with each other: never in a finished game, free skate or horse (unless in
    // the lobby); always in split screen, with player_collision on, and in (net)slap.
    bool players_can_collide();
    // 0x005343E0: a team game's scores shown by team (score_display = score_teams; never in the lobby).
    bool show_team_scores();
    // 0x00534CF0: the server destroyed (an internet one's GameSpy reporting stopped first), its two tasks removed;
    // flags bit 0 cleared.
    void destroy_server();
    // 0x00534D50: the server list's client destroyed.
    void destroy_match_client();
    // 0x00534D70: a client (not a modem) with a custom tag texture streams its palette (0x98) and texture (0x97) to
    // the server over a broadband connection, then tells which CAS logos it has (0x9A, 4 bytes). True if the skater
    // has a custom tag texture at all.
    bool send_custom_tag_texture();
    // 0x00534F40 (4 bytes: source only): the level's load script.
    uint32_t fetch_stage();
    // 0x00534F50 / 0x00534FC0: in the global array `list` (structures), the `name` string / the integer `field` of
    // the entry whose checksum is `choice` (the last such; null / 0 if none).
    const char* get_choice_name(uint32_t list, uint32_t choice);
    int get_choice_integer(uint32_t list, uint32_t choice, uint32_t field);
    // 0x005359F0: the starting points (the eight words at +0x48): a random order of 0 ... n-1 plus a random offset
    // (0-255) - n 8 in a network game, else the game mode's max_players, or 2 (offset 0) when the park editor's +0x40
    // isn't `off` and the mode isn't horse; the crown's (+0x68) random 0-9.
    void choose_start_points();
    // 0x00535BC0: the net metrics (+0x45) toggled - their task (+0xC4) added to the logic tasks or removed.
    void toggle_net_metrics();
    // 0x00535BF0: the scores' display (+0x46) toggled.
    void toggle_scores();
    // 0x00535D00: whether the game keeps a score: modes accumulating score or tracking the best combo, firefight, goal
    // attack, CTF, scavenger hunt - but not elimiskate (unless the net firefight / elimiskate modes).
    bool keeps_score();
    // 0x00535D90: script_flag_bits_ (a signed byte, widened).
    void set_preset_option_bits(char flags);

    // --- gamenet_settings.cpp: the network settings and the connection (0x00535040-0x00535CB0) ---
    // 0x00535040: the network preferences handed to the net library (NetLink::Hub): the master servers (+0x9954, ten
    // names of 16 bytes: the two set, unless "0.0.0.0", then default_master_servers - or only those), DHCP or the
    // addresses, DNS, dial-up, the device and connection type (modems: at most 3 players, no observers),
    // dialup_authentication. Four global strings (names unknown) override the address, gateway, subnet mask and DNS
    // server (an address of our own means no DHCP and the USB adaptor set-up script).
    void use_network_preferences();
    // 0x00535AD0: team_graffiti_mode in a network team game, else graffiti_individual_scoring.
    uint32_t get_team_graffiti_mode();
    // NOT ORIGINAL (inline in both below): the network preferences in a network game, else the skate module's.
    prefs::Preferences* get_game_preferences();
    // 0x00535B20 / 0x00535B70: the goals / game_type preference's checksum (the manager's address on a miss).
    uint32_t get_goals_preference();
    uint32_t get_game_type_preference();
    // 0x00535C10: go online (NetLink::Hub::ConnectToInternet unless already); then `online_script` runs. If it fails, a
    // modem or PPPoE connection (types 1 / 3) carries on in the online state task (+0xD8), `failScript` kept (+0x1C8).
    bool connect_to_internet(uint32_t online_script, uint32_t fail_script);
    // 0x00535CB0: go offline: the online state task added (state -1; `script`, if any, for when it's done), the lobby
    // left (0x005453E0 with 2), NetLink::Hub::DisconnectFromInternet.
    void disconnect_from_internet(uint32_t script);
    // 0x00536C10 (the CFunc CancelConnectToInternet's; no `this`): by the net library's modem state - 0 / 1 (dialling):
    // the online task restarted, the lobby left (state 2), offline; 2: modem state 6; 5: the online task removed and
    // the fail script (+0x1C8) run; 9: the online task removed; else nothing.
    static void cancel_connect_to_internet();
    // 0x00536490: look for a server at `ip`: the server lists cleared (0x00545370, 0x005462F0, 0x00545300; the
    // join-by-address flag 0x007CCA88 too), the server-list client made if there's none, then (an address given) the
    // find-server message 0x11 (the time, 12 bytes) sent to ip:5153 and the address kept (0x007CCA84; 0 with none).
    void find_server_at(uint32_t ip);
    // 0x00536640 (from the main loop; no `this`): GameSpy's think (0x005F9C40); in a network game with its query /
    // reporting up (0x007CCA82, 0x007CCE1C), its reporting's think (0x005FBBA0) too.
    static void service_game_spy();
    // 0x00536A40: the `name` of the set-up game's game type in mode_info (null if it isn't there).
    const char* get_game_type_name();
    // 0x00536B90: the level (m_level); the lobby's GameSpy peer (connected, the lobby hosting - 0x0053F810) told its
    // state changed (0x005FCCF0).
    void change_arena(uint32_t level);

    // --- gamenet_players.cpp: the players' records (0x00536820-0x00537F40) ---
    // 0x00536820: every joining player (the list at +0x130) removed: its connection terminated on the server, its node
    // and record (0x00543560) deleted.
    void remove_all_new_players();
    // 0x00536B10: the joining player `player` removed (its connection terminated on the server, or on client 0 if it
    // has one).
    void remove_new_player(JoinerRecord* player);
    // 0x005372C0 / 0x00537010: the first / next player of the list (+0x11C) through `search`; without `observers`,
    // observers are skipped. Null at the end.
    PlayerRecord* begin_player_walk(PlayerSearch* search, bool observers);
    PlayerRecord* step_player_walk(PlayerSearch* search, bool observers);
    // 0x00537070: the first joining player (+0x130) through `search` (then the lists' generic next, 0x004573D0).
    JoinerRecord* begin_joiner_walk(NewPlayerSearch* search);
    // The next joining player through `search` (null at the end). No address of its own: the original calls the lists'
    // generic next, folded at 0x004573D0 (NetLink::AppHost::step_conn_walk), with the manager as its unused `this`.
    JoinerRecord* step_joiner_walk(NewPlayerSearch* search);
    // 0x005370A0: the joining player whose object id (+0x10, a signed byte) is `id`.
    JoinerRecord* fetch_incoming_player_by_obj(uint16_t id);
    // 0x00537140: ready to play (cleared_to_start_) or not; when it changes, every skater of the skate module (+0x28's
    // list) told - its virtual slot 12 when ready, 11 when not.
    void set_ready_state(bool ready);
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
    // 0x005376F0: a panel message: with `broadcast`, message 0x3F to every player through the server (the CRC of
    // `properties`, `text`, `time`, string 1 and 2 - 16 bytes each; sequenced in group 8 when `important`); else locally:
    // create_net_panel_message {text (the global text `text`, its "%s0" replaced by string 1), id = net_panel_msg,
    // msg_time = time}. `object` isn't used.
    void build_net_notice(bool broadcast, uint32_t text, const char* string1, const char* string2, void* object,
                          const char* properties, bool important, int time);
    // 0x00537A10: a script queued to spawn once the level is up (+0x144; in the network heap) - unless one of the same
    // name is queued already.
    void queue_script(int node, uint32_t object_id, uint32_t script);
    // 0x00537B30: every queued script dropped.
    void clear_queued_scripts();
    // 0x00537D90: every queued script spawned at its node on its object and run to its end (in the bottom-up heap,
    // the main volume off, between PreRunQueuedScripts and PostRunQueuedScripts and the skate module's 0x00569580 /
    // 0x00569590), then dropped.
    void launch_queued_scripts();
    // 0x00537F40: pro set `bit` toggled (script_flag_bits_) and told to every remote player (message 0x70: `param`, the
    // bit's byte; sequenced in group 8).
    void toggle_pro_set(uint8_t bit, uint32_t param);
    // 0x00539880: the players (no observers) on `team` plus the joining players who will play (+0x20 flags 0x18, or
    // not 4) on it (+0x28).
    int count_team_players(int team);
    // 0x00539B00: the players (non-observers, and observers with flags bit 3) plus the joining players who will play
    // and aren't among them - the original's check compares each with the byte after the ids it noted (zero), so
    // only joining players with object id 0 are left out.
    int count_players();
    // 0x00539C70: the observers (not flags bit 3; the local one only in host mode 0) plus the joining observers (+0x20
    // flags 4 without 8).
    int count_observers();
    // 0x005399F0: the clients destroyed (client 0's task +0xD0 removed unless it's local), the observer input handler
    // deleted, 0x007CCDF8 cleared, GameSpy's reporting shut down (0x005FBB20) and 0x005F9900(1003), the tasks +0xD4,
    // +0xC8, +0xEC removed, the queued scripts dropped, and no longer ready to play (the skaters' slot 11).
    void disconnect_clients();
    // 0x00539DA0: `player`'s custom tag texture (and its CAS logos: 0x96 its skater id, 0x98 the palette, 0x97 the
    // pixels, 0x9A the logos - group 10) streamed through the server to every other remote player who has one... (not
    // on a modem; nothing when `player` is local) - see the source for the exact conditions.
    void send_tag_textures_to(PlayerRecord* player);
    // 0x0053A040: the local skater's face texture (the profile's +0x54, if it has one - 0x00487510): on the server
    // kept on the local player (+0x1E8: a byte 0, then 0x4498 bytes); on a broadband client streamed to the server
    // (0x7E "Face Data", 0x4498 bytes). False on a modem, without a face, or a narrowband client.
    bool prepare_local_face();
    // 0x0053A1B0: every other player's face (+0x1E8; the local one's prepared first) streamed to `player` through the
    // server (0x7E, 0x4499 bytes), unless `player` is on a modem connection.
    void send_faces_to(PlayerRecord* player);
    // 0x0053A310: a joining player's record made from `info` (unless one with its object id is listed): copied (the
    // appearance through a 0x1000-byte buffer for those who will play), its time-out base (+0x18) the app's time plus
    // `timeout`; on the server the join announced (net_message_joining locally, or net_message_observing /
    // net_message_join_pending to everyone); listed (+0x130), in the network heap.
    void add_joining_player(JoinerRecord* info, int timeout);
    // 0x0053A580: the next player the observer camera can follow after viewed_player_ (from the start if there's none or
    // it's gone; wrapping round), skipping those with 0x00543330 / 0x00543440 set. Null if none.
    PlayerRecord* next_cam_player();

    // 0x00536D70: the destructor body: GameSpy shut down (0x005F9900 with 1003, 0x005F9BE0, its reporting if up), the
    // lobby, +0x14, the presence and the stats deleted, the tasks (+0xC0 ... +0xF0) deleted, the lists emptied, the
    // two preference sets destroyed.
    ~Hub();
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;
    // Every node after `head` taken out of its list (each linked to itself; none freed). No address of its own: the
    // original calls the list code folded at 0x00588B70 (lists::unlink_all).
    static void unlink_all_nodes(memory::ListNode* head);

    // --- The scores (score_list.cpp) ---
    // 0x005435F0: team `team`'s score: the sum of its players' (fully in or local) SkaterScore scores; in net goal
    // attack, for a player who isn't observing, the goal manager's count for the team (0x00558B10) instead.
    int get_squad_points(int team);
    // 0x005436E0: the score of the player with object id `id` (its SkaterScore's - no null checks); in net goal
    // attack, for a player who isn't observing, the goal manager's count (0x00558AB0).
    int get_entrant_points(uint32_t id);

    // --- The server list (server_list.cpp) ---
    // 0x005451B0 / 0x005451C0: the online service's next state (+0x1BC) / its state (+0x1B8).
    void set_next_service_state(int state);
    int get_service_state();
    // 0x005453E0: the state; 1 (wake): the service task added to the logic tasks and the next state taken (the
    // time 0x007CE444 cleared); 2: the task removed.
    void set_service_state(int state);
    // 0x005451E0 (PostGame): the lobby's 0x00540220; logged in to presence, the status "hosting" with the
    // location "10thug211:<local address>:<public address>:5153:<server name> (<lobby>)" (all 0 without a peer).
    void update_hosting_status();
    // 0x00545300: in LAN mode the server-list client restarted (0x00534D50, start_matchmaking_client - false if that
    // fails) and its task (+0xF0) added to the logic tasks if not in a list. True.
    bool start_server_list();
    // 0x00545370: the browsing stopped: on the internet GameSpy's (0x005FCA40, with a peer), else the server-list
    // client's handlers deleted and its task removed.
    void stop_server_list();
    // 0x005456B0: destroy_server_menu_children and add_no_servers_found_message (with refocus).
    void show_no_servers_found(bool refocus);
    // 0x00546010: no server chosen (+0x8B) / 0x00546070: the servers in the menu (+0x8A).
    void clear_chosen_servers();
    int num_servers_in_menu();
    // 0x005460D0: `server` added to the server list menu (the first one clears the "none found" message):
    // server_list_menu_add_item with its name, mode, id (internet: its SBServer; LAN: itself), the menu's
    // scripts and their parameters. (`index` isn't used.)
    void show_server_in_menu(void* server, int index);
    // 0x005462F0: every server (waiting and listed) deleted; in LAN mode 0x00534D50.
    void empty_server_listing();
    // 0x00546400: whether a server named `name` (case ignored) at `ip` is listed or waiting.
    bool has_server(const char* name, uint32_t ip);
    // 0x005464D0: the listed server with id `id` (internet: its SBServer; LAN: itself), or null.
    void* fetch_host(uint32_t id);
    // 0x00547320: the "none found" message, then each waiting server moved to the list and the menu;
    // update_lobby_server_list.
    void show_found_servers();
    // 0x00547E80: (at most every 5 s unless `force`) the list cleared and the search started again: on the internet
    // GameSpy's server browser (0x005FC9D0 with the keys 8, 10, 4, 6 and the callback 0x00547750), on the LAN
    // scan_local_network.
    void refresh_server_list(bool force);

    // --- The join (gamenet_join.cpp) ---

    // 0x0052F560: whether the player of connection request `msg` from `conn` may join; if not, `reason` (a
    // JoinRefusedReason). Banned address, full (players or observers), with `check_version` THUG2's version, the
    // rank limit (remote players), a game in progress (unless willing to wait or observing; a lobby or a finished
    // game is none), with `check_password` the password, with `check_cd_key` a CD-key response of 72 characters.
    // In the THUGPRO peer mode the CD-key check is left out: THUGPRO's hosts drop it, and its players send a made-up
    // key (TP-R12).
    bool joins_accepted(int& reason, ConnectInfo* msg, net::Conn* conn, bool check_password, bool check_version,
                        bool check_cd_key);
    // 0x00534480: join the server at ip:port with client `index` (a local client - the host's own - is connected
    // directly): the connection request sent, the client's join handlers added (index 0: the game's too).
    void join_server(uint32_t ip, uint16_t port, int index);
    // 0x00534AF0: the connection request again (state "joining"), on `client`.
    void retry_entry(net::AppHost* client);
    // 0x00534BD0: the connection request again with `password` (state "joining with password").
    void retry_entry_with_key(const char* password);
    // 0x00537220: client `index` made (THUG2's join port; broadcast, local - else dynamic resend and foreign
    // connections -, secure as asked); index 0 resets the ready / pro-set state. Returns it (null if it failed).
    net::AppHost* start_client_session(bool broadcast, bool local, bool secure, int index);
    // 0x00534430: the server-list client (broadcast, dynamic resend, foreign connections, secure) and its server
    // response handler.
    net::AppHost* start_matchmaking_client();
    // 0x0053C380: the server made (THUG2's host port, 20 connections), host mode "serve", its lobby handlers (the
    // connection and join requests among them), the starting points shuffled. Returns it (null if it failed).
    net::AppHost* start_host_session(bool local, bool secure);

    // The message handlers (cdecl, data: the manager).
    // 0x0052F790 (message 3, host): a connection request - ok_to_join (password, version, CD key), then a new
    // connection and join proceed, or join refused; on the internet the CD key is checked with GameSpy first (the
    // answer comes to client_authorized). In the THUGPRO peer mode that GameSpy check is left out (TP-R12).
    static int on_link_established(net::InboundMsgCtx* context);
    // 0x0052FA10 (message 80, host): a join request - ok_to_join again (remote players), then the new player
    // (deferred 10 frames; the host's own at once - and on the internet its own CD key checked, an answer nobody
    // reads: left out in the peer mode).
    static int on_admission_ask_msg(net::InboundMsgCtx* context);
    // 0x0052E490 (message 79, client): join proceed - the host's details kept, the join request sent (the
    // appearance; in the THUGPRO peer mode the profile is the machine id, as THUGPRO's - TP-R12).
    static int on_go_ahead_msg(net::InboundMsgCtx* context);
    // 0x0052E1E0 (message 71, client): join refused - the reason shown (or the password asked for, the "game in
    // progress" dialog, the CD-key challenge answered). NOT ORIGINAL: THUGPRO's reasons 31-37 shown in words (TP-R13).
    static int on_turned_away_msg(net::InboundMsgCtx* context);
    // 0x00536530: GameSpy's answer to a CD-key check (gcd_authenticate_user's callback; `instance` the
    // connection's {app, manager, connection}, freed here): join proceed, or refused (reason 8).
    static void client_authorized(int game_id, int local_id, int authenticated, char* error, void* instance);

    // --- The players and the observer's camera (gamenet_players.cpp; players are PlayerRecord, not rewritten yet) ---

    // 0x0053A910: the observer's camera onto `player`'s skater (viewed_player_): someone else's - the skater camera; the
    // local skater - the walk camera while it walks, else the skater camera; none (or no skater) - neither.
    void spectate_player(PlayerRecord* player);
    // 0x0053AA70: message 0x4A (observe) to the server, unless the local player observes already.
    void send_observe_request();
    // 0x0053AAC0: the first remote player with flag 0x20 gets "first come, first served" (flag 0x200 and message 0x5F
    // with the checksum of the network preference 0x043EE978's text).
    void assign_first_come_first_served();
    // 0x0053AC40: the lowest object id (0-15) no player's skater has and no joining player (observers aside) was
    // given; 16 if none.
    int reserve_obj_id_for_player();
    // 0x0053AE10: the player (observers too) of connection `conn`, or null.
    PlayerRecord* find_player_by_sender(net::Conn* conn);
    // 0x0053AEA0: the player (not an observer) whose skater has object id `id`, or null.
    PlayerRecord* find_player_slot_by_obj(uint16_t id);
    // 0x0053C320: the host mode (kHostMode*): auto-serve observes (message 0x4A) and starts its task (+0xF4) with
    // the time noted (0x007CCDEC); first come first served observes and chooses who serves. Unchanged: nothing.
    void set_host_mode(int mode);
    static constexpr int kHostModeServe = 0;
    static constexpr int kHostModeAutoServe = 1;
    static constexpr int kHostModeFcfs = 2;
    // 0x0053C770: a new player (PlayerRecord, 0x1F0 bytes from the network heap, constructor 0x00543740 with `flags`)
    // of `skater` and `conn`. A local observer gets the observer's controls (hide_panel_stuff, the camera onto the
    // first player); a local player clears viewed_player_; a remote one is watched if the local player observes and
    // watches nobody. Returns it.
    PlayerRecord* new_player(objects::Entity* skater, net::Conn* conn, uint32_t flags);
    // 0x0053C950: `player` deleted: the camera moved off it, its crown taken off, its flag returned (flag_returned,
    // hide_ctf_arrow for the local team); the lobby's server details and qr2 told.
    void remove_player_record(PlayerRecord* player);
    // 0x0053CB70: `player` out of the game for `reason` (0 quit, 1 timed out, 2 to observe, 3 banned, 4 kicked,
    // 5 dropped, 6 left out). A client only destroys it. The server: its GameSpy CD key released, message 0x2D to
    // the remote players, the stats told of an early leave, 'Q' (banned / kicked; banned: its address banned) or 'V'
    // (left out) to its address, its connection closed (not when observing), the panel message
    // (net_message_player_*); observing: crown and flag given up; otherwise destroyed - and who serves next chosen
    // again if it was that one (network_end_game_selected when nobody is left to). Then, when every playing player's
    // game has ended, message 0x5A (game over) to all.
    void evict_player(PlayerRecord* player, int reason);
    // 0x0053D120: the players who joined during the game (flag 8) added back as joining players (20, 30 ... frames
    // on), their skaters' appearance kept; the remote skaters hidden and reset. The lobby's server details told.
    void re_add_pending_players();
    // 0x0053D900: every player destroyed (its skater removed unless it's the first local one).
    void destroy_all_players();
    // 0x0053E910 / 0x0053EE70: destroy_all_players and the joining players (0x00536820); and the apps shut down
    // (0x005399F0).
    void remove_players();
    void remove_players_and_apps();
    // 0x0053E920 (DropPendingPlayers): the playing players neither local nor ready (flag 0x20) dropped as left out
    // (reason 6) and their skaters removed.
    void drop_players_not_ready();
    // 0x0053E650: the local player becomes an observer (not again): dropped as observing on the server, its crown and
    // flag given up, its skater removed, the observer's controls (its handler at priority -1), hide_panel_stuff.
    void enter_observe_mode();
    // 0x0053E440 (gamenet_newplayers.cpp): a client's joining player (a JoinerRecord) made real: its skater (none for
    // an observer; the local one / a remote one / split screen's), the player (an existing one kept; with flag 0x10
    // the old one destroyed and the players resynced), its score.
    void add_new_player(JoinerRecord* info);
    // NOT ORIGINAL (inlined in remove_player_record / re_add_pending_players): the lobby's peer, when hosting, told the game's
    // details changed (0x005FCCF0).
    void update_lobby_server();
    // NOT ORIGINAL (inlined in remove_player_record / evict_player): `player`'s flag returned (flag_returned with its team,
    // hide_ctf_arrow when it's the local player's team), the flag bits cleared.
    void flag_returned(PlayerRecord* player);

    // --- The game's course and the manager's tasks (gamenet_flow.cpp) ---

    // 0x0053AF90: back in the lobby: the level preference (the server), the game mode netlobby with the teams kept,
    // the crown and every flag taken away, the lobby time (+0x1B0) noted.
    void reset_to_lobby();
    // 0x0053B0D0: the server starts a game: the game type, level and the shuffled starting points; message 0x34 (the
    // StartGameInfo: time limit, target score, fireball level, a new game id...) to every player (split screen: the
    // first only); the leaders and the crown forgotten; the stats' game started.
    void start_network_game();
    // 0x0053C6E0: the join given up (not when already over: states 8-10): message 0x0B to the server, the apps shut
    // down, the timeout task off, the joining players gone, the presence back to "chatting" in the lobby.
    void cancel_join();
    // Tasks (tasks::Task code, cdecl; the task's +0x1C is the manager - the observer's is the player's, unused).
    // 0x0053BED0: the observer asked for the next player (spectate_request_ 1): the camera onto it.
    static void observer_task(tasks::Task<PlayerRecord>* task);
    // 0x00536890 (the observer's input handler's code, given the handler - Detail::SpectatorInputSink): the next
    // player asked for (spectate_request_ 1) on a press of the controller's button 0x40 (+0x20 of its place in the
    // input manager) - unless the root window's menu_state tag is on.
    static void observer_input(pad_input::Responder<Hub>* handler);
    // 0x0053BF00 (+0xF4, auto-serve): 2 s after the host mode was set, in the lobby with two players or more and 5 s
    // after the lobby began: LoadPendingPlayers and StartNetworkGame, once (awaiting_start_).
    static void auto_serve_task(tasks::Task<Hub>* task);
    // 0x0053BF90 (+0xE4): the join's state shown (the connecting / joining / password dialogs, entered_network_game,
    // dialog_box_exit, CreateJoinRefusedDialog with the reason) and moved on.
    static void join_progress_tick(tasks::Task<Hub>* task);
    // 0x0053EB70 (+0xC0, priority -2000): the link checked each second (create_link_unplugged_dialog /
    // _front_end_dialog when unplugged and no dialog is up); the server drops a remote player silent for 20 s (in the
    // game; 60 s joining) or resending over 20 times; a client joining or joined whose server is silent for 20 s gives
    // up (ShowJoinTimeoutNotice while going to the main menu, else CreateLostConnectionDialog and everything shut).
    static void connection_task(tasks::Task<Hub>* task);
    // 0x0053EA30 (+0xD0, gamenet_newplayers.cpp): the joining players whose time has come added (add_new_player), then
    // the connections' silence forgiven, the skaters reset, the ready response sent.
    static void guest_player_intake_tick(tasks::Task<Hub>* task);
    // 0x0053D9D0 (+0xCC, gamenet_newplayers.cpp): the server's: each joining player whose time has come becomes a
    // player; its details (message 0x22) to the others and theirs to it, the created park and rails, joined (0x53),
    // its skater, the ready query (0x25) all round; the lobby's server details and qr2 told.
    static void host_player_intake_tick(tasks::Task<Hub>* task);
    // 0x0053EE90 (gamenet_manager_create.cpp): the constructor: the preferences (loaded), the player lists, fifteen
    // tasks (the connection task started), the lobby / vault / presence / stats managers, the CD-key challenge (eight
    // random letters and digits).
    Hub();
    // 0x0053F520: the manager (made if `create` and there's none, 0xFE0C bytes); a reference counted (0x007CCE18).
    static Hub* fabricate(bool create);
    // 0x0053C1F0 (+0xE8): the start of a game: while players are still loading, the server's
    // CreateWaitForPlayersDialog (no menu up) or a client's net_message_game_will_start, once; when everyone is ready
    // and the time has come, StartNetworkGame and the task removed.
    static void start_game_task(tasks::Task<Hub>* task);

    // Fields.
    net::AppHost*& client_slot(int index) { return (&client_app_)[index]; }

    uint8_t flag_bits_{0}; // +0x00 (bit 0: on the server; bit 2: internet; bits 2-3: network mode)
    std::array<uint8_t, 3> unknown1_{};
    net::AppHost* host_;                                  // +0x04
    net::AppHost* client_app_;                            // +0x08 client 0 (the game's)
    net::AppHost* client1_;                               // +0x0C client 1
    memory::HeapUnique<RoomManager> lobby_service_;       // +0x10 the GameSpy lobby
    memory::HeapUnique<VaultMan> vault_man_;              // +0x14 the content vault and HTTP transfers
    memory::HeapUnique<FriendsManager> presence_service_; // +0x18 the GameSpy presence
    memory::HeapUnique<RankingsManager> rating_service_;  // +0x1C the player's rating and stats
    std::array<char, 12> challenge_{};                                // +0x20 the host's CD-key challenge
    net::AppHost* browser_client_;                        // +0x2C the server list's client
    uint32_t game_type_{};        // +0x30 the game_type preference's checksum when the game started
    uint32_t level_{};            // +0x34 the level's load script
    uint8_t game_id_{};           // +0x38 the current game's number (the start info's)
    uint8_t ended_game_id_{};     // +0x39 the game whose end was last reported (message 0x59)
    bool match_ended_{};          // +0x3A
    bool cheating_noted_;         // +0x3B
    int join_mode_;               // +0x3C (1: observe)
    int host_mode_;               // +0x40 (0: serve)
    bool final_scores_received_{}; // +0x44
    bool show_metrics_;           // +0x45 the net metrics shown (toggle_net_metrics)
    bool show_scores_;            // +0x46 the scores shown (toggle_scores)
    bool show_names_;             // +0x47 the players' names shown (toggle_player_names)
    std::array<int, 8> start_points_{};      // +0x48 the skaters' starting points (choose_start_points; the start info's)
    int crown_start_{};           // +0x68 the crown's starting point (sent as a signed byte)
    objects::CrownObject* crown_{}; // +0x6C king of the hill's crown
    std::array<std::array<uint32_t, 5>, 4> team_objects_{}; // +0x70 each team's objects (handle_team_object; zeroed when a game starts)
    std::unique_ptr<tasks::SchedulerTask> connection_job_;      // +0xC0 connection_task
    std::unique_ptr<tasks::SchedulerTask> metrics_job_;         // +0xC4 the net metrics' task
    std::unique_ptr<tasks::SchedulerTask> score_broadcast_job_; // +0xC8 score_broadcast_tick
    std::unique_ptr<tasks::SchedulerTask> host_admit_job_;      // +0xCC
    std::unique_ptr<tasks::SchedulerTask> guest_admit_job_;     // +0xD0
    std::unique_ptr<tasks::SchedulerTask> keyboard_job_;        // +0xD4 the chat keyboard's task
    std::unique_ptr<tasks::SchedulerTask> dialup_job_;          // +0xD8 the online state task (dialup_progress_tick)
    std::unique_ptr<tasks::SchedulerTask> join_expiry_job_;     // +0xDC
    std::unique_ptr<tasks::SchedulerTask> service_job_;         // +0xE0 the online service task (net::service_task)
    std::unique_ptr<tasks::SchedulerTask> join_phase_job_;      // +0xE4
    std::unique_ptr<tasks::SchedulerTask> start_game_job_;      // +0xE8 start_game_task
    std::unique_ptr<tasks::SchedulerTask> level_change_job_;    // +0xEC level_change_task
    std::unique_ptr<tasks::SchedulerTask> server_list_job_;     // +0xF0 server_list_task (the server-list client's)
    std::unique_ptr<tasks::SchedulerTask> auto_serve_job_;      // +0xF4 auto_serve_task
    std::unique_ptr<tasks::SchedulerTask> flag_carry_job_;      // +0xF8 carried_flags_task
    gsl::owner<pad_input::InputHandler*> spectator_pad_listener_{}; // +0xFC (deleted where observing ends)
    int spectate_request_{};                                           // +0x100
    // The network / taunt preferences (prefs::Preferences, made in place by make_new; 0xC bytes each as in the
    // original, whose sets had a table): get_online_settings / get_taunt_settings.
    prefs::Preferences net_options_;   // +0x104
    prefs::Preferences taunt_options_; // +0x110
    memory::ListNode players_{};              // +0x11C lists::Foremost<PlayerRecord>
    memory::ListNode joiners_{};              // +0x130 lists::Foremost<JoinerRecord>: the joining players
    memory::ListNode queued_scripts_{};       // +0x144 the scripts to run once the level is up
    memory::ListNode servers_{};              // +0x158 lists::Foremost<HostListing>: the listed servers
    memory::ListNode waiting_servers_{};      // +0x16C lists::Foremost<HostListing>: found, not shown yet
    uint32_t unknown180_;                   // +0x180 (zeroed by make_new)
    uint32_t refused_reason_{};             // +0x184 a global text's name (CreateJoinRefusedDialog's `reason`)
    uint32_t last_ready_probe_id_{};        // +0x188
    bool cleared_to_start_{};               // +0x18C
    uint32_t score_synced_at_{};            // +0x190
    uint32_t script_flag_bits_{0};          // +0x194 the pro set's bits (sent as a byte)
    int join_phase_{};                      // +0x198 EntryPhase
    uint32_t join_ip_{};                    // +0x19C
    uint32_t join_private_ip_{};            // +0x1A0
    int join_port_{};                       // +0x1A4
    int modem_state_shown_;                 // +0x1A8 the net library's modem state the online state task showed last
    int join_began_at_{};                   // +0x1AC
    uint32_t lobby_since_{};                // +0x1B0 when the lobby began (reset_to_lobby)
    bool match_being_set_up_{};             // +0x1B4
    bool awaiting_start_{};                 // +0x1B5
    int service_state_;                     // +0x1B8 the online service's state (service_task)
    int next_service_state_;                // +0x1BC state 0 moves on to it at the wake time
    uint32_t service_wake_at_{};            // +0x1C0 frame_clock::get_time() milliseconds; 0: none
    uint32_t online_script_{};              // +0x1C4 run once online (connect_to_internet)
    uint32_t online_fail_script_{};         // +0x1C8 run if going online fails
    PlayerRecord* viewed_player_{};         // +0x1CC
    PlayerRecord* last_leader_{};           // +0x1D0 the player last announced as the leader
    int last_leading_team_{};               // +0x1D4
    // The created goals' message (prepare_created_goals): the level, then the goals. The goal editor is given 6000
    // bytes after the level - 4 more than the buffer holds, as in the original (they would reach the size, which is
    // written after).
    std::array<uint8_t, 0x1770> created_goals_{}; // +0x1D8
    uint32_t created_goals_size_;   // +0x1948
    uint32_t created_goals_level_;  // +0x194C
    std::array<uint8_t, 0x8000> unknown1950_{}; // +0x1950
    uint32_t room_id_{};            // +0x9950 (set_room_id)
    std::array<std::array<char, 16>, 10> master_servers_{}; // +0x9954 the master servers' names (use_network_preferences)
    std::array<uint8_t, 0x6400> news_buffer_{}; // +0x99F4 the news / message of the day downloaded
    const char* motd_text_{};       // +0xFDF4 the message of the day in news_buffer_
    uint8_t* pending_data_{};       // +0xFDF8 the rest of the download after it
    uint32_t pending_data_size_{};  // +0xFDFC
    int request_{};                 // +0xFE00 the current ghttp request
    uint32_t request_started_at_{}; // +0xFE04
    bool download_done_{};          // +0xFE08 the news download has ended (or the MOTD was cancelled)
    bool motd_received_;            // +0xFE09
    bool services_available_{};     // +0xFE0A the GameSpy services are available
};

// --- gamenet_players.cpp: outside the class ---
// 0x00539610: the flag task's code (registered by the manager's constructor): in capture the flag, the carried flags
// put on their carriers' heads.
void carried_flags_task(tasks::Task<Hub>* task);
// 0x005395C0: the manager released (its reference count 0x007CCE18; the last one destroys it).
void release_manager();

extern uint32_t g_network_team_mode;
extern int g_net_skill_level;
extern int g_net_max_players;
extern uint32_t g_command_line_server;
extern bool g_online_active;
extern uint32_t g_asked_for;
extern bool g_join_server_ip_succeeded;
extern uint32_t g_level_change_time;
extern uint32_t g_auto_serve_start;
extern Hub* g_online_hub;
extern uint32_t g_game_net_references;
extern bool g_query_reporting;
extern bool g_open_waiting;
extern uint8_t g_game_reported;
extern bool g_listing_asked;
extern bool g_listing_found;
extern uint32_t g_connection_timer;
extern bool g_game_posted;
extern int g_queued_count;
extern bool g_cheat_flag;

// The command line's login (-email / -password, buddy_status.cpp) and a session's password to join with
// (-PASSWORD, gamenet_join.cpp) - set by winmain.cpp.
extern std::array<char, 256> g_command_line_email;
extern std::array<char, 16> g_command_line_password;
extern std::array<char, 10> g_pending_password;

} // namespace online
