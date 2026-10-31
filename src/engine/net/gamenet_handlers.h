#pragma once

// The network manager's message handlers (online::Hub, 0x0052D6A0-0x00532460): the client's (registered by
// 0x00534480, the join) and the server's (0x0053C380, launch_host). Each gets a NetLink::InboundMsgCtx whose
// content_ is the manager and returns a NetLink::HandlerResult. The chat handler (0x00532050) is in chat.h.
// Players are online::PlayerRecord (not rewritten yet): +0x14 the skater, +0x18 the connection, +0x30 the team,
// +0x38 the name, +0x1D0 flags (bit 0 local, bit 2 observer, bit 7 has the crown, 0x800 << team carries that team's
// flag, 0x40 told the game's state, 0x100 ready, 0x8000 ...), +0x1D4 the acknowledgement it owes.

#include <cstdint>

namespace net {
struct InboundMsgCtx;
} // namespace net

namespace online {

// 0x0052D6A0: the CD key ("key" under HKEY_CURRENT_USER, else HKEY_LOCAL_MACHINE, Software\Activision\Tony Hawk's
// Underground 2 - each key created if missing) copied into `key` (at most 100 bytes; empty if none). Quirk kept: when
// HKEY_CURRENT_USER's key opens but has no value, it is never closed.
void __cdecl get_cd_key(char* key);

// The lists::Hunt<PlayerRecord> / lists::Hunt<JoinerRecord> destructors (vtables 0x0064C828 / 0x0064C82C):
// 0x0052DB60 / 0x0052DB70 reset the vtable, 0x0052F730 / 0x0052F760 the scalar deleting ones.
struct PlayerSearchDestructors {
    void** vtable_;
    void* list_link_;
    void destroy_body();                                   // 0x0052DB60
    PlayerSearchDestructors* teardown(unsigned int flags); // 0x0052F730
};
struct NewPlayerSearchDestructors {
    void** vtable_;
    void* list_link_;
    void destroy_body();                                      // 0x0052DB70
    NewPlayerSearchDestructors* teardown(unsigned int flags); // 0x0052F760
};

// --- The client's handlers (gamenet_handlers.cpp) ---
// 0x0052D7B0 (0x25): the server's ready query: its id (the message's word) kept in last_ready_probe_id_, for
// send_ready_response. 1.
int __cdecl on_ready_ask_msg(net::InboundMsgCtx* context);
// 0x0052D7D0 (0x26, the server's too): a ready response: the packet acknowledged; the sender's player (by
// connection) is given the id - if it's the query it was sent, its connection is ready again. 1.
int __cdecl on_ready_reply_msg(net::InboundMsgCtx* context);
// 0x0052D800 (0x4D): out of surveyor mode (the script command ExitSurveyorMode), into observing (0x0053E650). 1.
int __cdecl on_spectate_msg(net::InboundMsgCtx* context);
// 0x0052D830 (0x79): a remote connection's important and sequenced queues emptied. 1.
int __cdecl handle_flush_important(net::InboundMsgCtx* context);
// 0x0052D850 (0x74) / 0x0052D880 (0x71) / 0x0052D8B0 (0x72): the player of object id msg[0] stole / took /
// captured team msg[1]'s flag (0x00543C70 / 0x00543F00 / 0x00543A90); 0x0052D8E0 (0x73): retrieved its own
// (0x00544110). Nothing for an unknown player. 1.
int __cdecl on_flag_swiped_msg(net::InboundMsgCtx* context);
int __cdecl on_flag_grabbed_msg(net::InboundMsgCtx* context);
int __cdecl on_flag_scored_msg(net::InboundMsgCtx* context);
int __cdecl on_flag_returned_msg(net::InboundMsgCtx* context);
// 0x0052D910 (0x5E) / 0x0052D930 (0x7A): the script launch_auto_server_notification / Kill_Team_Flags. 1.
int __cdecl handle_auto_server_notice(net::InboundMsgCtx* context);
int __cdecl handle_kill_team_flags(net::InboundMsgCtx* context);
// 0x0052D950 (0x3F): a panel message: +4 its text's checksum, +8 its time, +0x0C / +0x1C two strings. 1.
int __cdecl on_hud_notice_msg(net::InboundMsgCtx* context);
// 0x0052D980 (0x23, before the skate module's): the object update message's length, from its bits: 8 skaters'
// bits, then for each one present a 16-bit time, 9 flags, and the fields they say are there. 1.
int __cdecl handle_object_update_length(net::InboundMsgCtx* context);
// 0x0052DAC0 (0x70, and 0x46 after the level change): nothing. 1.
int __cdecl handle_nothing(net::InboundMsgCtx* context);
// 0x0052DAD0 (0x7C): the CD key challenge: the answer (33 bytes, from the profile_password preference - through the
// online service at +0x1C, 0x0054CA10) as message 0x7D. 1.
int __cdecl handle_cd_key_challenge(net::InboundMsgCtx* context);
// 0x0052DCA0 (0x7B): the connection's last communication pushed msg[0] seconds on. 1.
int __cdecl handle_keep_alive(net::InboundMsgCtx* context);
// 0x0052DCC0 (0x53): joined: the join state 6, message 0x54 to the server, and (on a client) the skate module's
// handlers on each local client. 1.
int __cdecl on_admitted_msg(net::InboundMsgCtx* context);
// 0x0052E1A0 (0x42): the server has sent the game's state: ready to play (0x00537140(true): cleared_to_start_, the
// skaters told), and (not a local client) the add-new-players task added to the logic tasks. 1.
int __cdecl handle_ready_to_play(net::InboundMsgCtx* context);
// 0x0052EB70 (0x6C, the server's too): the created park's map (0x0058D6B0 with the message), its CRC computed (and
// dropped). 1.
int __cdecl handle_park_map(net::InboundMsgCtx* context);
// 0x0052ED80 (0x5A): game over (every player's game has ended); an observer of a goal attack gets the rankings. 1.
int __cdecl on_match_end_msg(net::InboundMsgCtx* context);
// 0x0052EDD0 (0x6A): the game has ended: the trick objects reset (0x005889F0), create_game_ended_dialog,
// 0x00537620. 1.
int __cdecl handle_game_ended(net::InboundMsgCtx* context);
// 0x0052F2D0 (0x69): the number of teams (msg[0]); a first team count in a game without teams asks for a team
// (ChooseTeamMessage) and readies the local skater for a move. 1.
int __cdecl handle_choose_team(net::InboundMsgCtx* context);
// 0x0052F370 (0x80): the message (8 bytes) sent back as 0x81 with the skate module's checksum at +4 (written into
// the received message). 1.
int __cdecl handle_checksum_request(net::InboundMsgCtx* context);

// --- The client's handlers about the game (gamenet_game_handlers.cpp) ---
// 0x0052DB80 (0x22): a new player: a JoinerRecord from the message (name, object id, team, flags, the appearance into
// its profile unless flags 5), announced (net_message_new_player) and added 10 frames on once ready to play, else at
// once. 1.
int __cdecl on_player_added_msg(net::InboundMsgCtx* context);
// 0x0052E760 (0x55): a new king: the crown off the old one (or a waiting new player's flag 0x80), onto the player of
// object id msg[0] (or the waiting new player of that id). 1.
int __cdecl on_crown_change_msg(net::InboundMsgCtx* context);
// 0x0052E870 (0x58): the crown dropped: the king no longer one (the drop announced to him, to everyone in a network
// game, else to the other local player), then the crown put at the msg[0]'th crown node (counting on across up to 49
// walks of the nodes) and taken off. 1.
int __cdecl handle_crown_spawn(net::InboundMsgCtx* context);
// 0x005300C0 (0x5F): the local player's flag 0x200 set, launch_fcfs_notification with checksum = the message's word. 1.
int __cdecl handle_fcfs_notice(net::InboundMsgCtx* context);
// 0x00530CB0 (0x75): a flag carrier's flag returned: flag_returned (Team, String0 = team_<n>_name's text), the CTF
// panel message gone for the carrier, the arrow hidden for the flag's team; the carrier carries none. 1.
int __cdecl handle_flag_returned(net::InboundMsgCtx* context);
// 0x00530E50 (0x88, the server's too) / 0x00530FF0 (0x87): the created rails / goals arrived: read into the rail
// editor (in the heap at the memory manager's +0x114) / the goal editor (InitialiseCreatedGoals). Damaged (checksum
// mismatch): asked for again (0x8B / 0x8A; 4) - or, when leaving for the main menu, the join cancelled
// (cancel_join_server show_timeout; 5).
int __cdecl handle_rails_data(net::InboundMsgCtx* context);
int __cdecl on_goal_sync_msg(net::InboundMsgCtx* context);
// 0x00531950 (0x6D): the goals chosen (0x00558580, then each named one's virtual +0x74 and 0x00554440); msg[0] = 1
// spawns wait_and_create_view_selected_goals_menu goal_summary. 1.
int __cdecl handle_selected_goals(net::InboundMsgCtx* context);
// 0x00531A80 (0x68): a player's team (msg[1]) set and announced (joined_team_you / _other, in team games); a warning
// when everyone else is on that team too. 1.
int __cdecl handle_joined_team(net::InboundMsgCtx* context);
// 0x00531C80 (0x6E) / 0x00531F10 (0x6F): goal attack: a player completed / started a goal (msg +4): its team and
// skater noted on the goal; the goals left announced to the player, or to teammates (the goal - or the end of its
// chain - ending for them too). 1.
int __cdecl handle_goal_completed(net::InboundMsgCtx* context);
int __cdecl handle_goal_started(net::InboundMsgCtx* context);
// 0x00532280 (0x8D; 0x8E nothing): elimiskate: the player of object id (the message's word) eliminated, or none
// (negative). 1.
int __cdecl handle_eliminated(net::InboundMsgCtx* context);
// 0x00532390 (0x4E): the join refused, the observers full (CreateJoinRefusedDialog). 1.
int __cdecl handle_observers_full(net::InboundMsgCtx* context);

// --- The server's handlers (gamenet_handlers.cpp) ---
// 0x0052EBB0 (0x90): in a team game, the object (the message's word) noted for the sender's team (five at most);
// 4 if the sender is unknown or its team has it already, else 1.
int __cdecl handle_team_object(net::InboundMsgCtx* context);
// 0x0052F3D0 (0x7D): the sender's CD key answer checked by the online service (0x0054CA30, its skater's id). 1.
int __cdecl handle_cd_key_answer(net::InboundMsgCtx* context);
// 0x0052F400 (0x8B / 0x8A): the rail editor's rails / the created goals (if any) streamed to the sender (0x88 /
// 0x87). 1.
int __cdecl handle_created_data_request(net::InboundMsgCtx* context);

// --- More of the server's handlers (gamenet_server_handlers.cpp) ---
// 0x0052E060 (0x4A): a player asks to observe: 4 if unknown or observing already; a remote one when the observers are
// full gets 0x4E; else (a remote one leaves the game - 0x0053CB70 with 2, its skater removed, flagged an observer, the
// camera moved off it) 0x4D. 1.
int __cdecl handle_observe_request(net::InboundMsgCtx* context);
// 0x0052E9E0 (0x6B): a joining player's data streamed to it: the park map (0x6C, 15000 bytes), the rails (0x88), the
// created goals for the message's level (0x87, if any); with msg[4] 'F' also the level change (0x46) and a ready
// query (0x25, the time - the player owes it), sent at once. 1.
int __cdecl handle_request_game_data(net::InboundMsgCtx* context);
// 0x0052EC40 (0x59): a player's game has ended (flag 0x100; 4 if again or for another game); when every player's has,
// game over (0x5A) to all. 1.
int __cdecl handle_game_ended_report(net::InboundMsgCtx* context);
// 0x0052EE10 (0x67): a team change (object id, team) passed on to everyone as 0x68 - in the lobby or after the game
// only (else 4; 4 for an unknown player or one without a skater). 1.
int __cdecl on_switch_squad_msg(net::InboundMsgCtx* context);
// 0x0052EF40 / 0x0052F1A0 (0x6E / 0x6F, for the current game - else 4): goal attack: a goal completed (once per team
// in team games; marked done by the sender's skater) / started (team games), passed on to the players / teammates. 1.
int __cdecl handle_goal_completed_report(net::InboundMsgCtx* context);
int __cdecl handle_goal_started_report(net::InboundMsgCtx* context);
// 0x0052FCC0 (0x0B): a connection is closing: its player out of the game (a remote one; its skater removed unless an
// observer) - 5; or a new player waiting to be added dropped with its connection - 5; else 2.
int __cdecl on_peer_left_msg(net::InboundMsgCtx* context);
// 0x0052FDA0 (0x57): the sender drops what it carries (its flag 0x8000 cleared): in CTF its flag returned (0x75 to
// the remote players), in king of the hill the crown to a random spot (Rnd(10) into +0x68, 0x58 to all). 1.
int __cdecl handle_drop_carried(net::InboundMsgCtx* context);
// 0x00532170 (0x81): the checksum answer: word 0 ^ 0xDEADFACE must be word 1; else, the first time,
// notify_client_cheating with the sender's name. 1.
int __cdecl handle_checksum_response(net::InboundMsgCtx* context);

// --- The session (gamenet_session_handlers.cpp) ---
// 0x0052DD40 (0x11, server; 12 bytes - else 4): a LAN search answered (0x10, 0xE0 bytes, to the asker's address): the
// player and observer counts and limits, password, in a game, host mode, logged in, the server's name, level, mode,
// skill level, the asker's time, the players' names (observers only with 0x005434C0) and the waiting new players'
// (their names used as sprintf formats, the original's). 1.
int __cdecl on_server_query_msg(net::InboundMsgCtx* context);
// 0x005301A0 (0x60-0x66, server; from the host's own player only - else 4): 0x60 the game's options into the network
// preferences, then chosen_start_game; 0x61 kick (msg[0] = 1) or ban the msg[1]'th player named msg + 2; 0x62
// change_level; 0x64 0x00558960 on the goal manager; 0x65 end the game (0x6A to the others); 0x66 the team count.
int __cdecl handle_host_options(net::InboundMsgCtx* context);
// 0x005311F0 (0x46, client): a level change (msg + 4 nonzero): the level kept; unless one is pending (4), the change
// task added (when: now + 3 s, + 1 s more for the created park), the level preference set on the server, announced. 4.
int __cdecl on_level_switch_msg(net::InboundMsgCtx* context);
// 0x005313C0 (0x34, client): a game starts: the start info words, the crown's spawn point, the skaters reset, the game
// mode set (victory conditions, time, teams...), StartingNewNetGame, the players' flags and scores reset, the crown
// taken off, the mode's goal_script run. 1.
int __cdecl handle_start_game(net::InboundMsgCtx* context);
// 0x00532460 (0x54, server): a player ready to receive the game's state (once - flag 0x40): the game info (0x33), the
// created goals, the spawned scripts (0x49), the skate module's state, an observer's graffiti, the chosen goals (0x6D),
// the king (0x55); the local player is just made ready to play; then 0x42 to all of them. 1.
int __cdecl handle_send_game_state(net::InboundMsgCtx* context);

// --- The tasks and GameSpy callbacks (gamenet_tasks.cpp; tasks get their tasks::Task, the manager at +0x1C) ---
// 0x0052F4A0: the level change, once its time (0x007CCDE8) has come: the skate module changes level (0x00565A00), the
// king loses the crown, nobody carries a flag, and the task (+0xEC) takes itself off.
void __cdecl level_change_task(void* task);
// 0x00533810: the online connection's state (NetLink::Hub's modem state) shown when it changes (+0x1A8 the last
// shown): connecting / dialing, connected, logged in (with the speed on a modem), disconnecting, hanging up,
// disconnected, a redial pause (state 8 for 5 s, then ConnectToInternet - its parameters never deleted).
void __cdecl dialup_progress_tick(void* task);
// 0x00533B50: update_net_metrics for each remote connection of the client (if not local) and the server - each list
// twice (the original's loop).
void __cdecl net_metrics_task(void* task);
// 0x00533ED0: joining for over a minute (and not in a game): the join state task removed, ShowJoinTimeoutNotice.
void __cdecl join_deadline_tick(void* task);
// 0x00533400 / 0x00533420 / 0x005334B0: the GameSpy query callbacks - every player / team key "", the key lists (the
// server's 1, 2, 4, 5, 6, 8, 20, 10, 11, 19; the players' 21, 24, 25), an error adding the server (debug output).
void __cdecl player_team_key_callback(int key, int index, void* buffer, void* data);
void __cdecl key_list_callback(int type, void* keys, void* data);
void __cdecl add_error_callback(int error, const char* message, void* data);

} // namespace online
