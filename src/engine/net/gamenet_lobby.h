#pragma once

// The network manager's GameSpy lobby (online::RoomManager, the manager's lobby_service_): the peer SDK's connection to the
// chat lobby, its rooms and the game hosted from it. Here its small members and the peer SDK callbacks at
// 0x0053F520-0x0053FEE0 (gamenet_lobby.cpp); only the fields used so far are named. The peer SDK is library code
// (engine/net/gamespy.h).

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace script {
class Script;
class Struct;
} // namespace script
namespace tasks {
class SchedulerTask;
}

namespace online {

extern void** const kLobbyListNodeVtable; // 0x0064CBDC: lists::Elem<LobbyPlayer>
extern void** const kLobbyNodeVtable;     // 0x0064CBE0: lists::Elem<LobbyRoom>

struct LobbyPlayer;
struct LobbyRoom;

class RoomManager {
public:
    // 0x0053F7E0: hosting stopped: the peer's game stopped (peerStopGame) and the hosting task (+0x150) off.
    void stop_hosting();
    // 0x0053F810: hosting a game from the lobby.
    bool is_hosting();
    // 0x0053F820 (source only: 4 bytes): the room's name.
    char* get_room_name();
    // 0x0053F830 / 0x0053F850: the name of the server chosen / hosted.
    void change_host_name(const char* name);
    char* fetch_host_name();
    // 0x0053F860 / 0x0053F870: the lost connection to report (lost_connection_to_gamespy, by the peer task).
    void clear_lost_connection();
    void set_lost_connection();
    // 0x0053F880: the peer SDK's PEER (null when not connected).
    void* get_lobby_handle();
    // 0x0053F890: in a group room.
    bool in_group_room();

    // The peer SDK's callbacks (cdecl).
    // 0x0053F8D0: a server key for the peer's hosted game (peerQRServerKeyCallback): qr2's standard keys as the
    // manager's server_key_callback (mapname without a fallback, gamemode = gametype, teamplay 0) and THUG2's own: 51
    // the observers, 52 the most observers, 53 the skill level, 54 a game in progress, 55 the host mode, 56 logged in
    // to the presence. Every key notes the server reported (0x007CCE48).
    static void __cdecl server_key_callback(void* peer, int key, void* buffer, void* param);
    // 0x0053FEC0 (peerQRCountCallback): the server's keys 16, the players; else 0.
    static int __cdecl count_callback(void* peer, int type, void* param);
    // 0x0053F620 (peerQRKeyListCallback): the server's keys (4, 5, 6, 12, 8, 10, 19, 51, 52, 56, 53, 54, 55) and the
    // players' (21, 57) added to the key buffer.
    static void __cdecl key_list_callback(void* peer, int type, void* key_buffer, void* param);

    // --- gamenet_lobby_peer.cpp: the connection, hosting, NAT negotiation ---
    // 0x00540220: hosting from the lobby: the server's foreign packets queued (queue_packet), the queue emptied, the
    // peer's reporting started on the server's socket (port 5153; its details sent at once), the timers noted, the
    // query task (+0x150) started.
    void start_hosting();
    // 0x00540750 (LobbyDisconnect): the three tasks off, the peer shut down and forgotten (the internet heap).
    void hangup();
    // 0x00540AB0 (thread code, stdcall: `lobby`): the peer connects with the network_id preference as its nick and
    // the presence's profile (get_nick_error_callback, get_connect_callback). 0.
    static unsigned long __stdcall connect_thread(void* lobby);
    // The peer SDK's callbacks (cdecl).
    // 0x0053F570 (disconnected): while connecting - create_gamespy_connection_failure_dialog; not expected -
    // lost_connection_to_gamespy; either way a disconnect is expected from now on.
    static void __cdecl disconnected_callback(void* peer, const char* reason, void* param);
    // 0x0053FEE0 (nick error): the nick in use - retried as "a<time>_<network_id>"; invalid - retried fixed.
    static void __cdecl get_nick_error_callback(void* peer, int type, const char* nick, int suggestions,
                                                const char** suggested, void* param);
    // 0x0053FFC0 (connected): success - the local address from the peer, StartLobbyList, the connection time; else
    // create_gamespy_connection_failure_dialog.
    static void __cdecl get_connect_callback(void* peer, int success, int failure, void* param);
    // 0x0053FB80 (a player key of the hosted game): player_ (the name) / rating_ (57) of the `index`th player who
    // counts (playing, or observing with flag 8), then of the joining ones; ping_ empty. Notes the server reported.
    static void __cdecl player_key_callback(void* peer, int key, int index, void* buffer, void* param);
    // 0x0053FDA0 (the public address, hosting): the server's join address (`ip`, the peer's private one) and port;
    // the presence's status "hosting" with "10thug211:<ip>:<private ip>:<port>:<server name> (<room>)".
    static void __cdecl public_address_callback(void* peer, uint32_t ip, uint16_t port, void* param);
    // 0x00540C90 (NAT negotiation asked by a client): negotiation with the cookie on the server's socket (the first
    // starts the NAT negotiation task), counted (+0x13C).
    static void __cdecl hole_punch_result_fn(void* peer, int cookie, void* param);
    // 0x00540300 (NAT negotiation's end, `result` 0 = success): a client joins the server at the address found; the
    // server counts down (the task off at 0); a client's negotiation is cleaned up (failure:
    // 0x3A9FB574).
    static void __cdecl nat_negotiation_completed(int result, int socket, const void* address, void* user_data);
    // 0x0053F6C0 (an app's foreign packet): qr2 queries ("\" or FE FD) and NAT negotiation packets (FD FC 1E 66 6A
    // B2...) copied into the queue (at most 199) for the query task.
    static void __cdecl queue_packet(char* data, int length, const void* from);
    // Tasks (tasks::Task code: the task's +0x1C is the lobby).
    // 0x00540040 (+0x148): the peer thinks while connected; a lost connection reported (lost_connection_to_gamespy).
    static void __cdecl think_task(void* task);
    // 0x005400B0 (+0x14C): NAT negotiation thinks (the internet's bottom-up heap).
    static void __cdecl nat_negotiation_task(void* task);
    // 0x005400E0 (+0x150): the queued packets handed to the peer (queries) or NAT negotiation and freed; while the
    // server isn't reported: after 90 s CreateNotPostedDialog (once), else its details again every 5 s.
    static void __cdecl query_task(void* task);

    // --- gamenet_lobby_menus.cpp: the lobby's menus and chat (`this` unused) ---
    // 0x00540900: a buddy-list menu item for `player` (make_text_sub_menu_item: its nick, lobby_add_buddy with its
    // profile / nick / net_name).
    void add_buddy_menu_item(LobbyPlayer* player);
    // 0x00540FB0: a regions menu item for `room` ("<name> - <players>/<most>", "(net_lobby_full)" when full;
    // choose_selected_lobby with `index` - not focusable when full or out of the room's rating range).
    void add_room_menu_item(LobbyRoom* room, int index);
    // 0x005411E0: the lobby's player list item for `player`: its nick, rank (rating x 0.000285 x 10, at most 10), id,
    // and what choosing it does (add as a buddy - with a prompt for each reason it can't) and its colour.
    void add_player_list_item(LobbyPlayer* player);
    // 0x00540B60 (the peer's room message callback): "<nick>: <message>" to the console (create_console_message).
    static void __cdecl room_message_callback(void* peer, int room_type, const char* nick, const char* message,
                                              int message_type, void* param);
    // 0x00541690 (the peer's group room list callback): each room (a LobbyRoom: its name, players / most - full when
    // the waiting reach the most -, rating range, "neversoft") added to the list `param` by group (highest first); the
    // end of the list (group 0) or a failure sets the lobby state (0x005453E0: 15, or 16 on failure / when the lobby
    // asked for wasn't found).
    static void __cdecl group_room_callback(void* peer, int success, int group_id, void* server, const char* name,
                                            int num_waiting, int max_waiting, int num_games, int num_playing,
                                            void* param);
    // 0x00540D50: the destructor body: the three tasks deleted, the node at +0x14 and its own node unlinked.
    void destroy_body();

    // --- rooms.cpp (0x00541870-0x00543010) ---
    // 0x00541870: the empty lobby and player lists, no peer, its three tasks (tasks::Task<RoomManager>, priority 0:
    // 0x00540040, 0x005400B0, 0x005400E0) and the server-browsing keys registered with GameSpy's qr2 (0x005FF660:
    // 0x33 numobservers, 0x34 maxobservers, 0x35 skilllevel, 0x36 started, 0x37 hostmode, 0x38 ranked, 0x39 rating_).
    RoomManager* make_new();
    // 0x00541AC0 (vtable slot 0): the scalar deleting destructor (the body is 0x00540860).
    RoomManager* teardown(unsigned int flags);

    // 0x00542DF0: the GameSpy peer made (0x005FCE60) with the lobby's callbacks, not in a lobby.
    void init_peer();
    // 0x00541A10: the games in the lobby listed (GameSpy's 0x005FD3E0 with \maxplayers\neversoft\maxrating\minrating,
    // the callback 0x00541690); the server list state 14 (0x005453E0); the counts at 0x007CE430 / 0x007CCE38 /
    // 0x007CCE39 cleared.
    void start_listing_games();
    // 0x005427E0: the SDK's group list stopped (0x005FCA40, with a peer) and every lobby deleted.
    void clear_lobbies();
    // 0x005428B0: dialog_box_exit, create_lobby_list_menu, then each lobby added to the menu (0x00540FB0).
    void populate_room_list();
    // 0x00542970: lobby `index` of the list (null past the end).
    LobbyRoom* get_lobby(int index);
    // 0x00541AF0: every player deleted, then update_lobby_player_list.
    void clear_players();
    // 0x00541BA0: destroy_lobby_user_list_children, each listed player added to the menu (0x005411E0),
    // update_lobby_player_list.
    void fill_player_list();
    // 0x00541C70: destroy_lobby_buddy_list_children, then each player with a profile other than our own (the
    // buddy list's +4) who isn't a buddy yet (0x00549CB0) added to the prospective buddies (0x00540900).
    void fill_prospective_buddy_list();
    // 0x00542B10: the players forgotten (and the menu's), then the SDK asked for the group room's players
    // (0x005FCB10, enum_players_callback).
    void request_player_list();

    // The GameSpy callbacks (cdecl; `lobby` is the callback parameter).
    // 0x00541D50 (playerJoined): a player known already is asked for its profile again and listed; a new one is
    // added (its profile asked of the SDK, 0x005FCBA0), shown, and "<nick> <lobby_status_joined>" goes to the
    // console (create_console_message with text and join).
    static void __cdecl player_joined_callback(void* peer, int room_type, const char* nick, RoomManager* lobby);
    // 0x005420C0 (playerLeft): a known player taken off the menu (destroy_lobby_user, user_id = the nick's
    // checksum) and deleted; "<nick> <lobby_status_left>" to the console (with left) either way.
    static void __cdecl player_left_callback(void* peer, int room_type, const char* nick, const char* reason,
                                             RoomManager* lobby);
    // 0x00541FD0 (playerInfo): a known player's profile (if it had none) and listed; nothing for a null nick.
    static void __cdecl player_info_callback(void* peer, int room_type, const char* nick, uint32_t ip, int profile_id,
                                             RoomManager* lobby);
    // 0x00542B70 (newPlayerList): request_player_list.
    static void __cdecl new_player_list_callback(void* peer, int room_type, RoomManager* lobby);
    // 0x00542650 (roomKeyChanged): a known player's rating (atol of `value`; whatever the key) and its rank
    // (player_list_update_rank: id, rank = rating * 0.000286 * 10, at most 10).
    static void __cdecl room_key_changed_callback(void* peer, int room_type, const char* nick, const char* key,
                                                  const char* value, RoomManager* lobby);
    // 0x00542330 (the room keys asked for when joining): for a first key b_rating, each player named `nick` gets
    // the rating and rank, as room_key_changed_callback.
    static void __cdecl room_keys_callback(void* peer, int success, int room_type, const char* nick, int count,
                                           char** keys, char** values, RoomManager* lobby);
    // 0x005424F0 (EnumPlayers): each player not known yet added (as player_joined_callback, without the menu).
    static void __cdecl enum_players_callback(void* peer, int success, int room_type, int index, const char* nick,
                                              int flags, RoomManager* lobby);
    // 0x00542B80 (joining the group room): failed - CreateJoinLobbyFailedDialog; else dialog_box_exit,
    // create_network_select_games_menu, in the lobby, the players asked for, and when logged in to GameSpy
    // presence the status "chatting" in the lobby (0x00548320), our room key b_rating set from the stats and every
    // player's asked for (room_keys_callback).
    static void __cdecl join_room_callback(void* peer, int success, int result, int room_type, RoomManager* lobby);

    memory::ListNode rooms_;        // +0x00 lists::Foremost<LobbyRoom> (vtable 0x0064CBFC; its base's 0x0064CBE0)
    memory::ListNode participants_; // +0x14 lists::Foremost<LobbyPlayer> (vtable 0x0064CBF8; its base's 0x0064CBDC)
    int group_id_;                  // +0x28 the chosen room's group room (0: none; LeaveLobby passes it to 0x005FC9B0)
    int unknown2c_;                 // +0x2C the chosen room's most players (CanHostGame: +0x30 must be below it)
    int unknown30_;                 // +0x30 the chosen room's players
    char room_name_[0x80];          // +0x34
    char server_name_[0x80];        // +0xB4
    bool in_group_room_;            // +0x134
    bool restricted_hosting_;       // +0x135 only a few GameSpy profiles may host (CanHostGame)
    bool drop_is_expected_;         // +0x136
    bool lost_connection_;          // +0x137
    bool connecting_;               // +0x138
    bool hosting_;                  // +0x139
    uint8_t unknown13a_[2];
    int negotiations_;                   // +0x13C NAT negotiations going on (the server's)
    void* remote_;                       // +0x140
    int nat_cookie_;                     // +0x144 (a client's)
    tasks::SchedulerTask* think_task_;   // +0x148 think_task
    tasks::SchedulerTask* nat_task_;     // +0x14C nat_negotiation_task
    tasks::SchedulerTask* hosting_task_; // +0x150 query_task
};
static_assert(offsetof(RoomManager, group_id_) == 0x28 && offsetof(RoomManager, participants_) == 0x14 &&
                  offsetof(RoomManager, room_name_) == 0x34 && offsetof(RoomManager, server_name_) == 0xB4 &&
                  offsetof(RoomManager, in_group_room_) == 0x134 && offsetof(RoomManager, hosting_) == 0x139 &&
                  offsetof(RoomManager, negotiations_) == 0x13C && offsetof(RoomManager, remote_) == 0x140 &&
                  offsetof(RoomManager, hosting_task_) == 0x150,
              "layout must match the original");

// A group room the peer's room list found (0x6C bytes, constructor 0x00545860; a list node, priority -group).
struct LobbyRoom {
    // 0x00545860: an empty one (a node of nothing, its data itself; full / out of range / neversoft cleared). Returns
    // this. (rooms.cpp)
    LobbyRoom* make_new();

    memory::ListNode list_link_;
    char label_[0x40];  // +0x14 (63 characters)
    int group_id_;      // +0x54
    int num_players_;   // +0x58 (the room list's "num_games")
    int max_players_;   // +0x5C (its "maxWaiting"; at least num_players_)
    int min_rating_;    // +0x60
    int max_rating_;    // +0x64
    bool full_;         // +0x68
    bool out_of_range_; // +0x69 the player's rating outside the room's
    bool neversoft_;    // +0x6A
    uint8_t pad6b_;
};
static_assert(sizeof(LobbyRoom) == 0x6C && offsetof(LobbyRoom, group_id_) == 0x54 &&
              offsetof(LobbyRoom, full_) == 0x68);

// A player in the lobby's room (the lobby's player list).
// (0x60 bytes, vtable 0x0064CBF8; made by rooms.cpp's callbacks)
struct LobbyPlayer {
    memory::ListNode list_link_;
    char nick_[0x40];  // +0x14
    int account_;      // +0x54 the GameSpy profile
    int skill_points_; // +0x58 (the room key b_rating)
    bool listed_;      // +0x5C seen in the room (shown in the player list)
    uint8_t pad5d_[3];
};
static_assert(offsetof(LobbyPlayer, account_) == 0x54 && offsetof(LobbyPlayer, skill_points_) == 0x58 &&
              offsetof(LobbyPlayer, listed_) == 0x5C && sizeof(LobbyPlayer) == 0x60);

// The lobby's script commands from 0x00541870 on (rooms.cpp).
bool __cdecl cfunc_num_players_in_lobby(script::Struct* params, script::Script* script);              // 0x00541A70
bool __cdecl cfunc_lobby_disconnect(script::Struct* params, script::Script* script);                  // 0x005429C0
bool __cdecl cfunc_fill_player_list(script::Struct* params, script::Script* script);                  // 0x00542AA0
bool __cdecl cfunc_fill_lobby_prospective_buddy_list(script::Struct* params, script::Script* script); // 0x00542B00
bool __cdecl cfunc_rejoin_lobby(script::Struct* params, script::Script* script);                      // 0x00542CE0
bool __cdecl cfunc_choose_lobby(script::Struct* params, script::Script* script);                      // 0x00542D30
bool __cdecl cfunc_lobby_connect(script::Struct* params, script::Script* script);                     // 0x00542EE0

// The lobby's two list node types (memory::ListNode's layout): its own base (vtable 0x0064CBE0) and the node at its +0x14
// (vtable 0x0064CBDC). The destructor bodies unlink them (0x00540840 / 0x00540860); the scalar deleting destructors
// (each vtable's only slot) also free them (0x00540880 / 0x005408C0); 0x00540F80 is another scalar deleting destructor
// over the first body.
struct LobbyNode : memory::ListNode {
    void destroy_body();                          // 0x00540840
    LobbyNode* teardown(unsigned int flags);      // 0x00540880
    LobbyNode* destroy_other(unsigned int flags); // 0x00540F80
};
struct LobbyListNode : memory::ListNode {
    void destroy_body();                         // 0x00540860
    LobbyListNode* teardown(unsigned int flags); // 0x005408C0
};

// tasks::Task<T>'s scalar deleting destructor (vtable 0x006476F0 slot 2, 0x005407B0: its destructor, then freed).
struct TaskDeleter {
    tasks::SchedulerTask* teardown(unsigned int flags); // 0x005407B0
};

// 0x00540E00 (SendMessage text=): to the lobby's group room - or, with system_message, "<System> <text>" to the
// console only. True.
bool __cdecl cfunc_send_message(script::Struct* params, script::Script* script);

// The packets queued by RoomManager::queue_packet (0x007CCE50, 199 of 0x1C bytes; the count 0x007CE438).
struct QueuedPacket {
    int type;   // 1 a qr2 query, 2 NAT negotiation
    char* data; // a copy (the game's C runtime malloc), NUL-terminated
    int length;
    uint8_t from[16]; // the sender's sockaddr
};
static_assert(sizeof(QueuedPacket) == 0x1C);

// (Two list searches the lobby's code keeps on its stack have vtables, 0x0064CB6C / 0x0064CB70, holding only
// destructors that put them back - 0x0053F8B0 / 0x005407E0, 0x0053F8C0 / 0x00540810 - not kept.)

// 0x0053F8A0 (ResetConnectionTimer): the connection timer (0x007CCE3C) restarted now. True.
bool __cdecl cfunc_reset_connection_timer(script::Struct* params, script::Script* script);

// --- The lobby's script commands (gamenet_lobby_cfuncs.cpp) ---
// 0x00540430 (StartNatNegotiation <ip string> <port> cookie=): a client's NAT negotiation with the server: client 0
// made (secure), the cookie sent to the host through the peer, negotiation on the client's socket; its packets queued
// and the NAT negotiation / query tasks started. False if the negotiation can't start.
bool __cdecl cfunc_start_nat_negotiation(script::Struct* params, script::Script* script);
// 0x005403E0 (CancelNatNegotiation): the client's negotiation cancelled, the two tasks off. True.
bool __cdecl cfunc_cancel_nat_negotiation(script::Struct* params, script::Script* script);
// 0x00540590 (LeaveLobby): the group room left; the presence back to its idle status unless 0xB2AB4DAB is given. True.
bool __cdecl cfunc_leave_lobby(script::Struct* params, script::Script* script);
// 0x00540640 (CancelLobbyConnect): no longer connecting; a disconnect expected. True.
bool __cdecl cfunc_cancel_lobby_connect(script::Struct* params, script::Script* script);
// 0x00540660 (SetQuietMode): the peer's quiet mode on, or off with the flag 0xD443A2BC. True.
bool __cdecl cfunc_set_quiet_mode(script::Struct* params, script::Script* script);
// 0x005406C0 (CanHostGame): with restricted hosting only ten GameSpy profiles (logged in) may; otherwise whether the
// lobby's +0x30 is below its +0x2C.
bool __cdecl cfunc_can_host_game(script::Struct* params, script::Script* script);

extern QueuedPacket g_queued_packets[200];

} // namespace online
