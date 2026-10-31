#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "hook/game_global.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script
namespace Net {
struct MsgHandlerContext;
} // namespace Net

// The network manager's server list (0x005451B0-0x00548040): the servers found (on the LAN through the server-list
// client, on the internet through GameSpy's server browser - the SDK is a library, called by address), shown in
// the server list menu, described and chosen; and the online service's download threads (the news, the message of
// the day) with their state (the manager's +0x1B8).
namespace GameNet {

extern void** const kServerSearchVtable; // 0x0064CD70: Lst::Search<ServerInfo>

// A server found (0xA8 bytes, vtable 0x0064CDFC): a node of the manager's server list (+0x158) or of the servers
// waiting to be shown (+0x16C).
struct ServerInfo : Mem::ListNode {
    char m_name[16];              // +0x14
    uint32_t m_ip;                // +0x24
    int m_port;                   // +0x28
    int m_ping;                   // +0x2C (9999: not known)
    int m_numPlayers;             // +0x30
    int m_maxPlayers;             // +0x34
    int m_numObservers;           // +0x38
    int m_maxObservers;           // +0x3C
    char m_level[32];             // +0x40 ("None")
    char m_mode[32];              // +0x60 ("None")
    bool m_password;              // +0x80
    bool m_started;               // +0x81 a game in progress
    int8_t m_skillLevel;          // +0x82
    uint8_t pad83;
    int m_hostMode;               // +0x84
    bool m_ranked;                // +0x88
    bool m_canReach;              // +0x89 (cleared when it can only be reached with NAT negotiation)
    bool m_inMenu;                // +0x8A added to the server list menu
    bool m_chosen;                // +0x8B its details asked for (RetrieveServerInfo)
    bool m_inList;                // +0x8C counted in the lobby's server list
    bool m_detailed;              // +0x8D the full details have come
    uint8_t pad8E[2];
    void* m_sbServer;             // +0x90 GameSpy's SBServer (internet)
    Mem::ListNode m_players;      // +0x94 (Lst::Head<PlayerInfo>, vtable 0x0064C9F0): its players' names and ratings

    // 0x00545F60: an empty one: one of eight players, one of one observer, skill level 2, mode and level "None",
    // reachable. Returns this.
    ServerInfo* Construct();
    // 0x00546640: its players freed (0x005465A0), then the heads' destructors.
    void DestroyBody();
    // 0x00547F70 (vtable slot 0): the scalar deleting destructor.
    ServerInfo* Destroy(unsigned int flags);
    // 0x00545790: a player (a PlayerInfo with only its name - 15 characters - and rating) added (on the internet
    // or the network heap).
    void AddPlayer(const char* name, int rating);
    // 0x005465A0: each player destroyed by the manager (0x0053C950).
    void ClearPlayers();
    // 0x005466D0 / 0x00546730: player `index`'s name / rating (null / 0 past the end).
    const char* GetPlayerName(int index);
    int GetPlayerRating(int index);
};
static_assert(offsetof(ServerInfo, m_ip) == 0x24 && offsetof(ServerInfo, m_level) == 0x40 &&
                  offsetof(ServerInfo, m_hostMode) == 0x84 && offsetof(ServerInfo, m_sbServer) == 0x90 &&
                  offsetof(ServerInfo, m_players) == 0x94 && sizeof(ServerInfo) == 0xA8,
              "layout must match the original");

// Lst::Search<ServerInfo> (vtable 0x0064CD70): 0x005451D0 the destructor body (the vtable reset), 0x00545460 the
// scalar deleting one.
struct ServerSearch {
    void** m_vtable;
    Mem::ListNode* m_node;
    void DestroyBody();
    ServerSearch* Destroy(unsigned int flags);
};

// 0x00547440 (the task's callback, the manager at its +0x1C; made by 0x0053EE90): every 3 seconds the clock
// restarts; within them, if a refresh asked for it (0x007CE44C) and more than 500 ms after the refresh, the waiting
// servers are shown as ShowFoundServers does (update_lobby_server_list by checksum).
void __cdecl ServerListTask(void* task);

// The online service's download threads (stdcall, the manager as the argument; started by Net::ServiceTask).
// 0x00545540: the news (state 7): motd.dat (testmotd.dat when 0x007CE441 is set) from www.thug2online.com into
// the manager's buffer (+0x99F4, 25600 bytes cleared first), NewsCallback when done. 0x00545620: the message of the
// day (state 9): GameSpy's usage report (ptTrackUsage, which asks motd.<domain>/motd/motd.asp: product 10368,
// version "1.0"; the unique_id preference is read but not passed, as the original). Both note the request (+0xFE00) and the time (+0xFE04). 0.
unsigned long __stdcall NewsThread(void* manager);
unsigned long __stdcall MotdThread(void* manager);
// 0x00545490 (GHTTP's completion callback, `param` the... not used: the manager is the singleton): a failed
// download is state 11; else the data XOR 0x6D, its checksum (the first word, over the size in the second word -
// 0x00401B30) checked (wrong: state 11), the text noted (+0xFDF4) with what follows it (+0xFDF8 / +0xFDFC), the
// news ok (+0xFE09) and state 8. 1.
int __cdecl NewsCallback(void* request, int result, uint8_t* buffer, uint32_t sizeLow, int32_t sizeHigh, void* param);

// 0x00548040: whether a GameSpy location string is THUG2's ("10thug211" before the first ':').
bool __cdecl IsThug2Location(const char* location);

// The script commands.
bool __cdecl CFunc_PostGame(Script::CStruct* params, Script::CScript* script);            // 0x00545440
bool __cdecl CFunc_NumServersInLobby(Script::CStruct* params, Script::CScript* script);   // 0x00545F20

// --- server_browser.cpp ---
class Manager;
// 0x00546790 / 0x005470D0 / 0x00547FA0: the chosen server described / joined / its details asked for (see the .cpp).
bool __cdecl CFunc_DescribeServer(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_ChooseServer(Script::CStruct* params, Script::CScript* script);
bool __cdecl CFunc_RetrieveServerInfo(Script::CStruct* params, Script::CScript* script);
// 0x005475C0: the server-list client's handler for a LAN server's reply (a Net message handler).
int __cdecl HandleServerResponse(Net::MsgHandlerContext* context);
// 0x00547750: GameSpy's game listing callback (add / update / remove / clear), the manager its parameter.
void __cdecl ListingGamesCallback(void* peer, int success, const char* name, void* sb, int staging, int message,
                                  int progress, Manager* manager);

GAME_GLOBAL_EXTERN(uint32_t, g_newsMessageCount);

} // namespace GameNet
