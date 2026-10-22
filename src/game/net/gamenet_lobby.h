#pragma once

// The network manager's GameSpy lobby (GameNet::LobbyMan, the manager's m_lobbyMan): the peer SDK's connection to the
// chat lobby, its rooms and the game hosted from it. Here its small members and the peer SDK callbacks at
// 0x0053F520-0x0053FEE0 (gamenet_lobby.cpp); only the fields used so far are named. The peer SDK is library code
// (docs/library_ranges.md), called by address.

#include <cstddef>
#include <cstdint>

namespace Script {
class CScript;
class CStruct;
} // namespace Script
namespace Tsk {
class BaseTask;
}

namespace GameNet {

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

    uint8_t m_unknown0[0x28];
    int m_unknown28;              // +0x28 (LeaveLobby passes it to the peer SDK's 0x005FC9B0)
    int m_unknown2C;              // +0x2C (CanHostGame: +0x30 must be below it)
    int m_unknown30;              // +0x30
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
static_assert(offsetof(LobbyMan, m_unknown28) == 0x28 && offsetof(LobbyMan, m_roomName) == 0x34 &&
                  offsetof(LobbyMan, m_serverName) == 0xB4 && offsetof(LobbyMan, m_inGroupRoom) == 0x134 &&
                  offsetof(LobbyMan, m_hosting) == 0x139 && offsetof(LobbyMan, m_negotiations) == 0x13C &&
                  offsetof(LobbyMan, m_peer) == 0x140 && offsetof(LobbyMan, m_hostingTask) == 0x150,
              "layout must match the original");

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
struct LobbySearchA {
    void** m_vtable;
    void* m_node;
    void DestroyBody(); // 0x0053F8B0
};
struct LobbySearchB {
    void** m_vtable;
    void* m_node;
    void DestroyBody(); // 0x0053F8C0
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

} // namespace GameNet
