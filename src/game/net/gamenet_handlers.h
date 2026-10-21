#pragma once

// The network manager's message handlers (GameNet::Manager, 0x0052D6A0-0x00532460): the client's (registered by
// 0x00534480, the join) and the server's (0x0053C380, StartServer). Each gets a Net::MsgHandlerContext whose
// m_data is the manager and returns a Net::HandlerResult. The chat handler (0x00532050) is in chat.h.
// Players are GameNet::PlayerInfo (not rewritten yet): +0x14 the skater, +0x18 the connection, +0x30 the team,
// +0x38 the name, +0x1D0 flags (bit 0 local, bit 2 observer, bit 7 has the crown, 0x800 << team carries that team's
// flag, 0x40 told the game's state, 0x100 ready, 0x8000 ...), +0x1D4 the acknowledgement it owes.

#include <cstdint>

namespace Net {
struct MsgHandlerContext;
} // namespace Net

namespace GameNet {

// 0x0052D6A0: the CD key ("key" under HKEY_CURRENT_USER, else HKEY_LOCAL_MACHINE, Software\Activision\Tony Hawk's
// Underground 2 - each key created if missing) copied into `key` (at most 100 bytes; empty if none). Quirk kept: when
// HKEY_CURRENT_USER's key opens but has no value, it is never closed.
void __cdecl GetCdKey(char* key);

// The Lst::Search<PlayerInfo> / Lst::Search<NewPlayerInfo> destructors (vtables 0x0064C828 / 0x0064C82C):
// 0x0052DB60 / 0x0052DB70 reset the vtable, 0x0052F730 / 0x0052F760 the scalar deleting ones.
struct PlayerSearchDestructors {
    void** m_vtable;
    void* m_node;
    void DestroyBody();                                // 0x0052DB60
    PlayerSearchDestructors* Destroy(unsigned int flags); // 0x0052F730
};
struct NewPlayerSearchDestructors {
    void** m_vtable;
    void* m_node;
    void DestroyBody();                                // 0x0052DB70
    NewPlayerSearchDestructors* Destroy(unsigned int flags); // 0x0052F760
};

// --- The client's handlers (gamenet_handlers.cpp) ---
// 0x0052D7B0 (0x25): the server's ready query: its id (the message's word) kept in m_latestReadyQuery, for
// SendReadyResponse. 1.
int __cdecl HandleReadyQuery(Net::MsgHandlerContext* context);
// 0x0052D7D0 (0x26, the server's too): a ready response: the packet acknowledged; the sender's player (by
// connection) is given the id - if it's the query it was sent, its connection is ready again. 1.
int __cdecl HandleReadyResponse(Net::MsgHandlerContext* context);
// 0x0052D800 (0x4D): out of surveyor mode (the script command ExitSurveyorMode), into observing (0x0053E650). 1.
int __cdecl HandleObserve(Net::MsgHandlerContext* context);
// 0x0052D830 (0x79): a remote connection's important and sequenced queues emptied. 1.
int __cdecl HandleFlushImportant(Net::MsgHandlerContext* context);
// 0x0052D850 (0x74) / 0x0052D880 (0x71) / 0x0052D8B0 (0x72): the player of object id msg[0] stole / took /
// captured team msg[1]'s flag (0x00543C70 / 0x00543F00 / 0x00543A90); 0x0052D8E0 (0x73): retrieved its own
// (0x00544110). Nothing for an unknown player. 1.
int __cdecl HandleStoleFlag(Net::MsgHandlerContext* context);
int __cdecl HandleTookFlag(Net::MsgHandlerContext* context);
int __cdecl HandleCapturedFlag(Net::MsgHandlerContext* context);
int __cdecl HandleRetrievedFlag(Net::MsgHandlerContext* context);
// 0x0052D910 (0x5E) / 0x0052D930 (0x7A): the script launch_auto_server_notification / Kill_Team_Flags. 1.
int __cdecl HandleAutoServerNotice(Net::MsgHandlerContext* context);
int __cdecl HandleKillTeamFlags(Net::MsgHandlerContext* context);
// 0x0052D950 (0x3F): a panel message: +4 its text's checksum, +8 its time, +0x0C / +0x1C two strings. 1.
int __cdecl HandlePanelMessage(Net::MsgHandlerContext* context);
// 0x0052D980 (0x23, before the skate module's): the object update message's length, from its bits: 8 skaters'
// bits, then for each one present a 16-bit time, 9 flags, and the fields they say are there. 1.
int __cdecl HandleObjectUpdateLength(Net::MsgHandlerContext* context);
// 0x0052DAC0 (0x70, and 0x46 after the level change): nothing. 1.
int __cdecl HandleNothing(Net::MsgHandlerContext* context);
// 0x0052DAD0 (0x7C): the CD key challenge: the answer (33 bytes, from the profile_password preference - through the
// online service at +0x1C, 0x0054CA10) as message 0x7D. 1.
int __cdecl HandleCdKeyChallenge(Net::MsgHandlerContext* context);
// 0x0052DCA0 (0x7B): the connection's last communication pushed msg[0] seconds on. 1.
int __cdecl HandleKeepAlive(Net::MsgHandlerContext* context);
// 0x0052DCC0 (0x53): joined: the join state 6, message 0x54 to the server, and (on a client) the skate module's
// handlers on each local client. 1.
int __cdecl HandleJoinAccepted(Net::MsgHandlerContext* context);
// 0x0052E1A0 (0x42): the server has sent the game's state: ready to play (0x00537140(true): m_readyToPlay, the
// skaters told), and (not a local client) the add-new-players task added to the logic tasks. 1.
int __cdecl HandleReadyToPlay(Net::MsgHandlerContext* context);
// 0x0052EB70 (0x6C, the server's too): the created park's map (0x0058D6B0 with the message), its CRC computed (and
// dropped). 1.
int __cdecl HandleParkMap(Net::MsgHandlerContext* context);
// 0x0052ED80 (0x5A): game over (every player's game has ended); an observer of a goal attack gets the rankings. 1.
int __cdecl HandleGameOver(Net::MsgHandlerContext* context);
// 0x0052EDD0 (0x6A): the game has ended: the trick objects reset (0x005889F0), create_game_ended_dialog,
// 0x00537620. 1.
int __cdecl HandleGameEnded(Net::MsgHandlerContext* context);
// 0x0052F2D0 (0x69): the number of teams (msg[0]); a first team count in a game without teams asks for a team
// (ChooseTeamMessage) and readies the local skater for a move. 1.
int __cdecl HandleChooseTeam(Net::MsgHandlerContext* context);
// 0x0052F370 (0x80): the message (8 bytes) sent back as 0x81 with the skate module's checksum at +4 (written into
// the received message). 1.
int __cdecl HandleChecksumRequest(Net::MsgHandlerContext* context);

// --- The client's handlers about the game (gamenet_game_handlers.cpp) ---
// 0x0052DB80 (0x22): a new player: a NewPlayerInfo from the message (name, object id, team, flags, the appearance into
// its profile unless flags 5), announced (net_message_new_player) and added 10 frames on once ready to play, else at
// once. 1.
int __cdecl HandleNewPlayer(Net::MsgHandlerContext* context);
// 0x0052E870 (0x58): the crown dropped: the king no longer one (the drop announced to him, to everyone in a network
// game, else to the other local player), then the crown put at the msg[0]'th crown node (counting on across up to 49
// walks of the nodes) and taken off. 1.
int __cdecl HandleCrownSpawn(Net::MsgHandlerContext* context);
// 0x005300C0 (0x5F): the local player's flag 0x200 set, launch_fcfs_notification with checksum = the message's word. 1.
int __cdecl HandleFcfsNotice(Net::MsgHandlerContext* context);
// 0x00530CB0 (0x75): a flag carrier's flag returned: flag_returned (Team, String0 = team_<n>_name's text), the CTF
// panel message gone for the carrier, the arrow hidden for the flag's team; the carrier carries none. 1.
int __cdecl HandleFlagReturned(Net::MsgHandlerContext* context);
// 0x00530E50 (0x88, the server's too) / 0x00530FF0 (0x87): the created rails / goals arrived: read into the rail
// editor (in the heap at the memory manager's +0x114) / the goal editor (InitialiseCreatedGoals). Damaged (checksum
// mismatch): asked for again (0x8B / 0x8A; 4) - or, when leaving for the main menu, the join cancelled
// (cancel_join_server show_timeout; 5).
int __cdecl HandleRailsData(Net::MsgHandlerContext* context);
int __cdecl HandleGoalsData(Net::MsgHandlerContext* context);
// 0x00531950 (0x6D): the goals chosen (0x00558580, then each named one's virtual +0x74 and 0x00554440); msg[0] = 1
// spawns wait_and_create_view_selected_goals_menu goal_summary. 1.
int __cdecl HandleSelectedGoals(Net::MsgHandlerContext* context);
// 0x00531A80 (0x68): a player's team (msg[1]) set and announced (joined_team_you / _other, in team games); a warning
// when everyone else is on that team too. 1.
int __cdecl HandleJoinedTeam(Net::MsgHandlerContext* context);
// 0x00531C80 (0x6E) / 0x00531F10 (0x6F): goal attack: a player completed / started a goal (msg +4): its team and
// skater noted on the goal; the goals left announced to the player, or to teammates (the goal - or the end of its
// chain - ending for them too). 1.
int __cdecl HandleGoalCompleted(Net::MsgHandlerContext* context);
int __cdecl HandleGoalStarted(Net::MsgHandlerContext* context);
// 0x00532280 (0x8D; 0x8E nothing): elimiskate: the player of object id (the message's word) eliminated, or none
// (negative). 1.
int __cdecl HandleEliminated(Net::MsgHandlerContext* context);
// 0x00532390 (0x4E): the join refused, the observers full (CreateJoinRefusedDialog). 1.
int __cdecl HandleObserversFull(Net::MsgHandlerContext* context);

// --- The server's handlers (gamenet_handlers.cpp) ---
// 0x0052EBB0 (0x90): in a team game, the object (the message's word) noted for the sender's team (five at most);
// 4 if the sender is unknown or its team has it already, else 1.
int __cdecl HandleTeamObject(Net::MsgHandlerContext* context);
// 0x0052F3D0 (0x7D): the sender's CD key answer checked by the online service (0x0054CA30, its skater's id). 1.
int __cdecl HandleCdKeyAnswer(Net::MsgHandlerContext* context);
// 0x0052F400 (0x8B / 0x8A): the rail editor's rails / the created goals (if any) streamed to the sender (0x88 /
// 0x87). 1.
int __cdecl HandleCreatedDataRequest(Net::MsgHandlerContext* context);

// --- More of the server's handlers (gamenet_server_handlers.cpp) ---
// 0x0052E060 (0x4A): a player asks to observe: 4 if unknown or observing already; a remote one when the observers are
// full gets 0x4E; else (a remote one leaves the game - 0x0053CB70 with 2, its skater removed, flagged an observer, the
// camera moved off it) 0x4D. 1.
int __cdecl HandleObserveRequest(Net::MsgHandlerContext* context);
// 0x0052E9E0 (0x6B): a joining player's data streamed to it: the park map (0x6C, 15000 bytes), the rails (0x88), the
// created goals for the message's level (0x87, if any); with msg[4] 'F' also the level change (0x46) and a ready
// query (0x25, the time - the player owes it), sent at once. 1.
int __cdecl HandleRequestGameData(Net::MsgHandlerContext* context);
// 0x0052EC40 (0x59): a player's game has ended (flag 0x100; 4 if again or for another game); when every player's has,
// game over (0x5A) to all. 1.
int __cdecl HandleGameEndedReport(Net::MsgHandlerContext* context);
// 0x0052EE10 (0x67): a team change (object id, team) passed on to everyone as 0x68 - in the lobby or after the game
// only (else 4; 4 for an unknown player or one without a skater). 1.
int __cdecl HandleTeamChangeRequest(Net::MsgHandlerContext* context);
// 0x0052EF40 / 0x0052F1A0 (0x6E / 0x6F, for the current game - else 4): goal attack: a goal completed (once per team
// in team games; marked done by the sender's skater) / started (team games), passed on to the players / teammates. 1.
int __cdecl HandleGoalCompletedReport(Net::MsgHandlerContext* context);
int __cdecl HandleGoalStartedReport(Net::MsgHandlerContext* context);
// 0x0052FCC0 (0x0B): a connection is closing: its player out of the game (a remote one; its skater removed unless an
// observer) - 5; or a new player waiting to be added dropped with its connection - 5; else 2.
int __cdecl HandleDisconnect(Net::MsgHandlerContext* context);
// 0x0052FDA0 (0x57): the sender drops what it carries (its flag 0x8000 cleared): in CTF its flag returned (0x75 to
// the remote players), in king of the hill the crown to a random spot (Rnd(10) into +0x68, 0x58 to all). 1.
int __cdecl HandleDropCarried(Net::MsgHandlerContext* context);
// 0x00532170 (0x81): the checksum answer: word 0 ^ 0xDEADFACE must be word 1; else, the first time,
// notify_client_cheating with the sender's name. 1.
int __cdecl HandleChecksumResponse(Net::MsgHandlerContext* context);

} // namespace GameNet
