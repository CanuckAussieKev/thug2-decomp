#pragma once

// The network manager's GameSpy lobby (GameNet::LobbyMan, the manager's m_lobbyMan): the peer SDK's connection to the
// chat lobby, its rooms and the game hosted from it. Here its small members and the peer SDK callbacks at
// 0x0053F520-0x0053FEE0 (gamenet_lobby.cpp); only the fields used so far are named. The peer SDK is library code
// (game/net/gamespy.h).

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script
namespace Tsk {
class BaseTask;
}

namespace GameNet {

extern void** const kLobbySearchAVtable;  // 0x0064CB6C
extern void** const kLobbySearchBVtable;  // 0x0064CB70
extern void** const kLobbyListNodeVtable; // 0x0064CBDC: Lst::Node<LobbyPlayer>
extern void** const kLobbyNodeVtable;     // 0x0064CBE0: Lst::Node<LobbyRoom>

struct LobbyPlayer;
struct LobbyRoom;

class LobbyMan {
public:
    // 0x0053F7E0: hosting stopped: the peer's game stopped (peerStopGame) and the hosting task (+0x150) off.
    void StopHosting();
    // 0x0053F810: hosting a game from the lobby.
    bool IsHosting();
    // 0x0053F820 (source only: 4 bytes): the room's name.
    char* GetRoomName();
    // 0x0053F830 / 0x0053F850: the name of the server chosen / hosted.
    void SetServerName(const char* name);
    char* GetServerName();
    // 0x0053F860 / 0x0053F870: the lost connection to report (lost_connection_to_gamespy, by the peer task).
    void ClearLostConnection();
    void SetLostConnection();
    // 0x0053F880: the peer SDK's PEER (null when not connected).
    void* GetPeer();
    // 0x0053F890: in a group room.
    bool InGroupRoom();

    // The peer SDK's callbacks (cdecl).
    // 0x0053F8D0: a server key for the peer's hosted game (peerQRServerKeyCallback): qr2's standard keys as the
    // manager's ServerKeyCallback (mapname without a fallback, gamemode = gametype, teamplay 0) and THUG2's own: 51
    // the observers, 52 the most observers, 53 the skill level, 54 a game in progress, 55 the host mode, 56 logged in
    // to the presence. Every key notes the server reported (0x007CCE48).
    static void __cdecl ServerKeyCallback(void* peer, int key, void* buffer, void* param);
    // 0x0053FEC0 (peerQRCountCallback): the server's keys 16, the players; else 0.
    static int __cdecl CountCallback(void* peer, int type, void* param);
    // 0x0053F620 (peerQRKeyListCallback): the server's keys (4, 5, 6, 12, 8, 10, 19, 51, 52, 56, 53, 54, 55) and the
    // players' (21, 57) added to the key buffer.
    static void __cdecl KeyListCallback(void* peer, int type, void* keyBuffer, void* param);

    // --- gamenet_lobby_peer.cpp: the connection, hosting, NAT negotiation ---
    // 0x00540220: hosting from the lobby: the server's foreign packets queued (QueuePacket), the queue emptied, the
    // peer's reporting started on the server's socket (port 5153; its details sent at once), the timers noted, the
    // query task (+0x150) started.
    void StartHosting();
    // 0x00540750 (LobbyDisconnect): the three tasks off, the peer shut down and forgotten (the internet heap).
    void Disconnect();
    // 0x00540AB0 (thread code, stdcall: `lobby`): the peer connects with the network_id preference as its nick and
    // the presence's profile (NickErrorCallback, ConnectCallback). 0.
    static unsigned long __stdcall ConnectThread(void* lobby);
    // The peer SDK's callbacks (cdecl).
    // 0x0053F570 (disconnected): while connecting - create_gamespy_connection_failure_dialog; not expected -
    // lost_connection_to_gamespy; either way a disconnect is expected from now on.
    static void __cdecl DisconnectedCallback(void* peer, const char* reason, void* param);
    // 0x0053FEE0 (nick error): the nick in use - retried as "a<time>_<network_id>"; invalid - retried fixed.
    static void __cdecl NickErrorCallback(void* peer, int type, const char* nick, int suggestions,
                                          const char** suggested, void* param);
    // 0x0053FFC0 (connected): success - the local address from the peer, StartLobbyList, the connection time; else
    // create_gamespy_connection_failure_dialog.
    static void __cdecl ConnectCallback(void* peer, int success, int failure, void* param);
    // 0x0053FB80 (a player key of the hosted game): player_ (the name) / rating_ (57) of the `index`th player who
    // counts (playing, or observing with flag 8), then of the joining ones; ping_ empty. Notes the server reported.
    static void __cdecl PlayerKeyCallback(void* peer, int key, int index, void* buffer, void* param);
    // 0x0053FDA0 (the public address, hosting): the server's join address (`ip`, the peer's private one) and port;
    // the presence's status "hosting" with "10thug211:<ip>:<private ip>:<port>:<server name> (<room>)".
    static void __cdecl PublicAddressCallback(void* peer, uint32_t ip, uint16_t port, void* param);
    // 0x00540C90 (NAT negotiation asked by a client): negotiation with the cookie on the server's socket (the first
    // starts the NAT negotiation task), counted (+0x13C).
    static void __cdecl NatNegotiateCallback(void* peer, int cookie, void* param);
    // 0x00540300 (NAT negotiation's end, `result` 0 = success): a client joins the server at the address found; the
    // server counts down (the task off at 0); a client's negotiation is cleaned up (failure:
    // 0x3A9FB574).
    static void __cdecl NatNegotiationCompleted(int result, int socket, const void* address, void* userData);
    // 0x0053F6C0 (an app's foreign packet): qr2 queries ("\" or FE FD) and NAT negotiation packets (FD FC 1E 66 6A
    // B2...) copied into the queue (at most 199) for the query task.
    static void __cdecl QueuePacket(char* data, int length, const void* from);
    // Tasks (Tsk::Task code: the task's +0x1C is the lobby).
    // 0x00540040 (+0x148): the peer thinks while connected; a lost connection reported (lost_connection_to_gamespy).
    static void __cdecl ThinkTask(void* task);
    // 0x005400B0 (+0x14C): NAT negotiation thinks (the internet's bottom-up heap).
    static void __cdecl NatNegotiationTask(void* task);
    // 0x005400E0 (+0x150): the queued packets handed to the peer (queries) or NAT negotiation and freed; while the
    // server isn't reported: after 90 s CreateNotPostedDialog (once), else its details again every 5 s.
    static void __cdecl QueryTask(void* task);

    // --- gamenet_lobby_menus.cpp: the lobby's menus and chat (`this` unused) ---
    // 0x00540900: a buddy-list menu item for `player` (make_text_sub_menu_item: its nick, lobby_add_buddy with its
    // profile / nick / net_name).
    void AddBuddyMenuItem(LobbyPlayer* player);
    // 0x00540FB0: a regions menu item for `room` ("<name> - <players>/<most>", "(net_lobby_full)" when full;
    // choose_selected_lobby with `index` - not focusable when full or out of the room's rating range).
    void AddRoomMenuItem(LobbyRoom* room, int index);
    // 0x005411E0: the lobby's player list item for `player`: its nick, rank (rating x 0.000285 x 10, at most 10), id,
    // and what choosing it does (add as a buddy - with a prompt for each reason it can't) and its colour.
    void AddPlayerListItem(LobbyPlayer* player);
    // 0x00540B60 (the peer's room message callback): "<nick>: <message>" to the console (create_console_message).
    static void __cdecl RoomMessageCallback(void* peer, int roomType, const char* nick, const char* message,
                                            int messageType, void* param);
    // 0x00541690 (the peer's group room list callback): each room (a LobbyRoom: its name, players / most - full when
    // the waiting reach the most -, rating range, "neversoft") added to the list `param` by group (highest first); the
    // end of the list (group 0) or a failure sets the lobby state (0x005453E0: 15, or 16 on failure / when the lobby
    // asked for wasn't found).
    static void __cdecl GroupRoomCallback(void* peer, int success, int groupId, void* server, const char* name,
                                          int numWaiting, int maxWaiting, int numGames, int numPlaying, void* param);
    // 0x00540D50: the destructor body: the three tasks deleted, the node at +0x14 and its own node unlinked.
    void DestroyBody();

    // --- lobby.cpp (0x00541870-0x00543010) ---
    // 0x00541870: the empty lobby and player lists, no peer, its three tasks (Tsk::Task<LobbyMan>, priority 0:
    // 0x00540040, 0x005400B0, 0x005400E0) and the server-browsing keys registered with GameSpy's qr2 (0x005FF660:
    // 0x33 numobservers, 0x34 maxobservers, 0x35 skilllevel, 0x36 started, 0x37 hostmode, 0x38 ranked, 0x39 rating_).
    LobbyMan* Construct();
    // 0x00541AC0 (vtable slot 0): the scalar deleting destructor (the body is 0x00540860).
    LobbyMan* Destroy(unsigned int flags);

    // 0x00542DF0: the GameSpy peer made (0x005FCE60) with the lobby's callbacks, not in a lobby.
    void InitPeer();
    // 0x00541A10: the games in the lobby listed (GameSpy's 0x005FD3E0 with \maxplayers\neversoft\maxrating\minrating,
    // the callback 0x00541690); the server list state 14 (0x005453E0); the counts at 0x007CE430 / 0x007CCE38 /
    // 0x007CCE39 cleared.
    void StartListingGames();
    // 0x005427E0: the SDK's group list stopped (0x005FCA40, with a peer) and every lobby deleted.
    void ClearLobbies();
    // 0x005428B0: dialog_box_exit, create_lobby_list_menu, then each lobby added to the menu (0x00540FB0).
    void FillLobbyList();
    // 0x00542970: lobby `index` of the list (null past the end).
    LobbyRoom* GetLobby(int index);
    // 0x00541AF0: every player deleted, then update_lobby_player_list.
    void ClearPlayers();
    // 0x00541BA0: destroy_lobby_user_list_children, each listed player added to the menu (0x005411E0),
    // update_lobby_player_list.
    void FillPlayerList();
    // 0x00541C70: destroy_lobby_buddy_list_children, then each player with a profile other than our own (the
    // buddy list's +4) who isn't a buddy yet (0x00549CB0) added to the prospective buddies (0x00540900).
    void FillProspectiveBuddyList();
    // 0x00542B10: the players forgotten (and the menu's), then the SDK asked for the group room's players
    // (0x005FCB10, EnumPlayersCallback).
    void RequestPlayerList();

    // The GameSpy callbacks (cdecl; `lobby` is the callback parameter).
    // 0x00541D50 (playerJoined): a player known already is asked for its profile again and listed; a new one is
    // added (its profile asked of the SDK, 0x005FCBA0), shown, and "<nick> <lobby_status_joined>" goes to the
    // console (create_console_message with text and join).
    static void __cdecl PlayerJoinedCallback(void* peer, int roomType, const char* nick, LobbyMan* lobby);
    // 0x005420C0 (playerLeft): a known player taken off the menu (destroy_lobby_user, user_id = the nick's
    // checksum) and deleted; "<nick> <lobby_status_left>" to the console (with left) either way.
    static void __cdecl PlayerLeftCallback(void* peer, int roomType, const char* nick, const char* reason,
                                           LobbyMan* lobby);
    // 0x00541FD0 (playerInfo): a known player's profile (if it had none) and listed; nothing for a null nick.
    static void __cdecl PlayerInfoCallback(void* peer, int roomType, const char* nick, uint32_t ip, int profileId,
                                           LobbyMan* lobby);
    // 0x00542B70 (newPlayerList): RequestPlayerList.
    static void __cdecl NewPlayerListCallback(void* peer, int roomType, LobbyMan* lobby);
    // 0x00542650 (roomKeyChanged): a known player's rating (atol of `value`; whatever the key) and its rank
    // (player_list_update_rank: id, rank = rating * 0.000286 * 10, at most 10).
    static void __cdecl RoomKeyChangedCallback(void* peer, int roomType, const char* nick, const char* key,
                                               const char* value, LobbyMan* lobby);
    // 0x00542330 (the room keys asked for when joining): for a first key b_rating, each player named `nick` gets
    // the rating and rank, as RoomKeyChangedCallback.
    static void __cdecl RoomKeysCallback(void* peer, int success, int roomType, const char* nick, int count,
                                         char** keys, char** values, LobbyMan* lobby);
    // 0x005424F0 (EnumPlayers): each player not known yet added (as PlayerJoinedCallback, without the menu).
    static void __cdecl EnumPlayersCallback(void* peer, int success, int roomType, int index, const char* nick,
                                            int flags, LobbyMan* lobby);
    // 0x00542B80 (joining the group room): failed - CreateJoinLobbyFailedDialog; else dialog_box_exit,
    // create_network_select_games_menu, in the lobby, the players asked for, and when logged in to GameSpy
    // presence the status "chatting" in the lobby (0x00548320), our room key b_rating set from the stats and every
    // player's asked for (RoomKeysCallback).
    static void __cdecl JoinRoomCallback(void* peer, int success, int result, int roomType, LobbyMan* lobby);


    Mem::ListNode m_rooms;        // +0x00 Lst::Head<LobbyRoom> (vtable 0x0064CBFC; its base's 0x0064CBE0)
    Mem::ListNode m_players;      // +0x14 Lst::Head<LobbyPlayer> (vtable 0x0064CBF8; its base's 0x0064CBDC)
    int m_groupId;                // +0x28 the chosen room's group room (0: none; LeaveLobby passes it to 0x005FC9B0)
    int m_unknown2C;              // +0x2C the chosen room's most players (CanHostGame: +0x30 must be below it)
    int m_unknown30;              // +0x30 the chosen room's players
    char m_roomName[0x80];        // +0x34
    char m_serverName[0x80];      // +0xB4
    bool m_inGroupRoom;           // +0x134
    bool m_restrictedHosting;     // +0x135 only a few GameSpy profiles may host (CanHostGame)
    bool m_expectingDisconnect;   // +0x136
    bool m_lostConnection;        // +0x137
    bool m_connecting;            // +0x138
    bool m_hosting;               // +0x139
    uint8_t m_unknown13A[2];
    int m_negotiations;           // +0x13C NAT negotiations going on (the server's)
    void* m_peer;                 // +0x140
    int m_natCookie;              // +0x144 (a client's)
    Tsk::BaseTask* m_thinkTask;   // +0x148 ThinkTask
    Tsk::BaseTask* m_natTask;     // +0x14C NatNegotiationTask
    Tsk::BaseTask* m_hostingTask; // +0x150 QueryTask
};
static_assert(offsetof(LobbyMan, m_groupId) == 0x28 && offsetof(LobbyMan, m_players) == 0x14 && offsetof(LobbyMan, m_roomName) == 0x34 &&
                  offsetof(LobbyMan, m_serverName) == 0xB4 && offsetof(LobbyMan, m_inGroupRoom) == 0x134 &&
                  offsetof(LobbyMan, m_hosting) == 0x139 && offsetof(LobbyMan, m_negotiations) == 0x13C &&
                  offsetof(LobbyMan, m_peer) == 0x140 && offsetof(LobbyMan, m_hostingTask) == 0x150,
              "layout must match the original");

// A group room the peer's room list found (0x6C bytes, constructor 0x00545860; a list node, priority -group).
struct LobbyRoom {
    // 0x00545860: an empty one (a node of nothing, its data itself; full / out of range / neversoft cleared). Returns
    // this. (lobby.cpp)
    LobbyRoom* Construct();

    Mem::ListNode m_node;
    char m_name[0x40];       // +0x14 (63 characters)
    int m_groupId;           // +0x54
    int m_numPlayers;        // +0x58 (the room list's "numGames")
    int m_maxPlayers;        // +0x5C (its "maxWaiting"; at least m_numPlayers)
    int m_minRating;         // +0x60
    int m_maxRating;         // +0x64
    bool m_full;             // +0x68
    bool m_outOfRange;       // +0x69 the player's rating outside the room's
    bool m_neversoft;        // +0x6A
    uint8_t m_pad6B;
};
static_assert(sizeof(LobbyRoom) == 0x6C && offsetof(LobbyRoom, m_groupId) == 0x54 && offsetof(LobbyRoom, m_full) == 0x68);

// A player in the lobby's room (the lobby's player list).
// (0x60 bytes, vtable 0x0064CBF8; made by lobby.cpp's callbacks)
struct LobbyPlayer {
    Mem::ListNode m_node;
    char m_nick[0x40];       // +0x14
    int m_profile;           // +0x54 the GameSpy profile
    int m_rating;            // +0x58 (the room key b_rating)
    bool m_listed;           // +0x5C seen in the room (shown in the player list)
    uint8_t m_pad5D[3];
};
static_assert(offsetof(LobbyPlayer, m_profile) == 0x54 && offsetof(LobbyPlayer, m_rating) == 0x58 &&
              offsetof(LobbyPlayer, m_listed) == 0x5C && sizeof(LobbyPlayer) == 0x60);

// The lobby's script commands from 0x00541870 on (lobby.cpp).
bool __cdecl CFunc_NumPlayersInLobby(Script::CStruct* params, Script::CScript* script);              // 0x00541A70
bool __cdecl CFunc_LobbyDisconnect(Script::CStruct* params, Script::CScript* script);                // 0x005429C0
bool __cdecl CFunc_FillPlayerList(Script::CStruct* params, Script::CScript* script);                 // 0x00542AA0
bool __cdecl CFunc_FillLobbyProspectiveBuddyList(Script::CStruct* params, Script::CScript* script);  // 0x00542B00
bool __cdecl CFunc_RejoinLobby(Script::CStruct* params, Script::CScript* script);                    // 0x00542CE0
bool __cdecl CFunc_ChooseLobby(Script::CStruct* params, Script::CScript* script);                    // 0x00542D30
bool __cdecl CFunc_LobbyConnect(Script::CStruct* params, Script::CScript* script);                   // 0x00542EE0

// The lobby's two list node types (Mem::ListNode's layout): its own base (vtable 0x0064CBE0) and the node at its +0x14
// (vtable 0x0064CBDC). The destructor bodies unlink them (0x00540840 / 0x00540860); the scalar deleting destructors
// (each vtable's only slot) also free them (0x00540880 / 0x005408C0); 0x00540F80 is another scalar deleting destructor
// over the first body.
struct LobbyNode : Mem::ListNode {
    void DestroyBody();                         // 0x00540840
    LobbyNode* Destroy(unsigned int flags);     // 0x00540880
    LobbyNode* DestroyOther(unsigned int flags); // 0x00540F80
};
struct LobbyListNode : Mem::ListNode {
    void DestroyBody();                         // 0x00540860
    LobbyListNode* Destroy(unsigned int flags); // 0x005408C0
};

// Tsk::Task<T>'s scalar deleting destructor (vtable 0x006476F0 slot 2, 0x005407B0: its destructor, then freed).
struct TaskDeleter {
    Tsk::BaseTask* Destroy(unsigned int flags); // 0x005407B0
};

// 0x00540E00 (SendMessage text=): to the lobby's group room - or, with system_message, "<System> <text>" to the
// console only. True.
bool __cdecl CFunc_SendMessage(Script::CStruct* params, Script::CScript* script);

// The packets queued by LobbyMan::QueuePacket (0x007CCE50, 199 of 0x1C bytes; the count 0x007CE438).
struct QueuedPacket {
    int type;          // 1 a qr2 query, 2 NAT negotiation
    char* data;        // a copy (the game's C runtime malloc), NUL-terminated
    int length;
    uint8_t from[16];  // the sender's sockaddr
};
static_assert(sizeof(QueuedPacket) == 0x1C);

// The destructor bodies of two list searches the lobby's code keeps on its stack (vtables 0x0064CB6C / 0x0064CB70;
// called by the exception unwinding): 0x0053F8B0 / 0x0053F8C0 put the vtable back.
// 0x005407E0 / 0x00540810: their scalar deleting destructors.
struct LobbySearchA {
    void** m_vtable;
    void* m_node;
    void DestroyBody();                        // 0x0053F8B0
    LobbySearchA* Destroy(unsigned int flags); // 0x005407E0
};
struct LobbySearchB {
    void** m_vtable;
    void* m_node;
    void DestroyBody();                        // 0x0053F8C0
    LobbySearchB* Destroy(unsigned int flags); // 0x00540810
};

// 0x0053F8A0 (ResetConnectionTimer): the connection timer (0x007CCE3C) restarted now. True.
bool __cdecl CFunc_ResetConnectionTimer(Script::CStruct* params, Script::CScript* script);

// --- The lobby's script commands (gamenet_lobby_cfuncs.cpp) ---
// 0x00540430 (StartNatNegotiation <ip string> <port> cookie=): a client's NAT negotiation with the server: client 0
// made (secure), the cookie sent to the host through the peer, negotiation on the client's socket; its packets queued
// and the NAT negotiation / query tasks started. False if the negotiation can't start.
bool __cdecl CFunc_StartNatNegotiation(Script::CStruct* params, Script::CScript* script);
// 0x005403E0 (CancelNatNegotiation): the client's negotiation cancelled, the two tasks off. True.
bool __cdecl CFunc_CancelNatNegotiation(Script::CStruct* params, Script::CScript* script);
// 0x00540590 (LeaveLobby): the group room left; the presence back to its idle status unless 0xB2AB4DAB is given. True.
bool __cdecl CFunc_LeaveLobby(Script::CStruct* params, Script::CScript* script);
// 0x00540640 (CancelLobbyConnect): no longer connecting; a disconnect expected. True.
bool __cdecl CFunc_CancelLobbyConnect(Script::CStruct* params, Script::CScript* script);
// 0x00540660 (SetQuietMode): the peer's quiet mode on, or off with the flag 0xD443A2BC. True.
bool __cdecl CFunc_SetQuietMode(Script::CStruct* params, Script::CScript* script);
// 0x005406C0 (CanHostGame): with restricted hosting only ten GameSpy profiles (logged in) may; otherwise whether the
// lobby's +0x30 is below its +0x2C.
bool __cdecl CFunc_CanHostGame(Script::CStruct* params, Script::CScript* script);

extern QueuedPacket g_queuedPackets[200];

} // namespace GameNet
