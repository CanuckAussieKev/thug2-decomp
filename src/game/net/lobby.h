#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The GameSpy lobby (GameNet::LobbyMan, the network manager's +0x10; 0x00540000-0x00543010 - this file: from
// 0x00541870, the rest elsewhere): the lobbies (GameSpy group rooms) to choose from, the chosen lobby's players, and
// the GameSpy Peer SDK object it talks through (the SDK is a library: called, not decompiled).
namespace GameNet {

// A lobby to choose from (vtable 0x0064CBFC, a node of LobbyMan's lobby list).
struct LobbyInfo : Mem::ListNode {
    char m_name[64];        // +0x14
    int m_groupId;          // +0x54 the GameSpy group room
    int m_unknown58;        // +0x58
    int m_unknown5C;        // +0x5C
    uint8_t m_unknown60[8];
    bool m_unknown68;       // +0x68
    bool m_unknown69;       // +0x69
    bool m_unknown6A;       // +0x6A (copied to the lobby manager's +0x135 when chosen)

    // 0x00545860: an empty one (a node of nothing; its data itself, the three flags cleared). Returns this.
    LobbyInfo* Construct();
};
static_assert(offsetof(LobbyInfo, m_groupId) == 0x54 && offsetof(LobbyInfo, m_unknown6A) == 0x6A,
              "layout must match the original");

// A player in the chosen lobby (0x60 bytes, vtable 0x0064CBF8, a node of LobbyMan's player list).
struct LobbyPlayer : Mem::ListNode {
    char m_nick[64];        // +0x14
    int m_profileId;        // +0x54 its GameSpy profile (asked of the SDK when it's added)
    int m_rating;           // +0x58 (the room key b_rating)
    bool m_listed;          // +0x5C seen in the room (shown in the player list)
    uint8_t pad5D[3];
};
static_assert(offsetof(LobbyPlayer, m_profileId) == 0x54 && sizeof(LobbyPlayer) == 0x60,
              "layout must match the original");

class LobbyMan {
public:
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
    LobbyInfo* GetLobby(int index);
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

    Mem::ListNode m_lobbies;          // +0x00 (Lst::Head<LobbyInfo>, vtable 0x0064CBFC)
    Mem::ListNode m_players;          // +0x14 (Lst::Head<LobbyPlayer>, vtable 0x0064CBF8)
    int m_groupId;                    // +0x28 the chosen lobby's group room (0: none)
    int m_unknown2C;                  // +0x2C (the chosen lobby's +0x5C)
    int m_unknown30;                  // +0x30 (the chosen lobby's +0x58)
    char m_lobbyName[256];            // +0x34
    bool m_inLobby;                   // +0x134
    bool m_unknown135;                // +0x135 (the chosen lobby's +0x6A)
    bool m_unknown136;                // +0x136 (1 when not connected)
    bool m_unknown137;                // +0x137
    uint8_t m_unknown138;
    bool m_unknown139;                // +0x139
    uint8_t pad13A[2];
    int m_pending;                    // +0x13C (LobbyDisconnect counts it down to 0)
    void* m_peer;                     // +0x140 the GameSpy peer
    uint32_t m_unknown144;
    void* m_tasks[3];                 // +0x148 Tsk::Task<LobbyMan>
};
static_assert(offsetof(LobbyMan, m_groupId) == 0x28 && offsetof(LobbyMan, m_lobbyName) == 0x34 &&
                  offsetof(LobbyMan, m_inLobby) == 0x134 && offsetof(LobbyMan, m_pending) == 0x13C &&
                  offsetof(LobbyMan, m_peer) == 0x140 && offsetof(LobbyMan, m_tasks) == 0x148,
              "layout must match the original");

// The lobby's script commands.
bool __cdecl CFunc_NumPlayersInLobby(Script::CStruct* params, Script::CScript* script);              // 0x00541A70
bool __cdecl CFunc_LobbyDisconnect(Script::CStruct* params, Script::CScript* script);                // 0x005429C0
bool __cdecl CFunc_FillPlayerList(Script::CStruct* params, Script::CScript* script);                 // 0x00542AA0
bool __cdecl CFunc_FillLobbyProspectiveBuddyList(Script::CStruct* params, Script::CScript* script);  // 0x00542B00
bool __cdecl CFunc_RejoinLobby(Script::CStruct* params, Script::CScript* script);                    // 0x00542CE0
bool __cdecl CFunc_ChooseLobby(Script::CStruct* params, Script::CScript* script);                    // 0x00542D30
bool __cdecl CFunc_LobbyConnect(Script::CStruct* params, Script::CScript* script);                   // 0x00542EE0

} // namespace GameNet
