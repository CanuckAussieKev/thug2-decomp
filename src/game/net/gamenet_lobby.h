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

    uint8_t m_unknown0[0x34];
    char m_roomName[0x80];        // +0x34
    char m_serverName[0x80];      // +0xB4
    bool m_inGroupRoom;           // +0x134
    uint8_t m_unknown135;
    bool m_expectingDisconnect;   // +0x136
    bool m_lostConnection;        // +0x137
    bool m_connecting;            // +0x138
    bool m_hosting;               // +0x139
    uint8_t m_unknown13A[6];
    void* m_peer;                 // +0x140
    uint8_t m_unknown144[0xC];
    Tsk::BaseTask* m_hostingTask; // +0x150
};
static_assert(offsetof(LobbyMan, m_roomName) == 0x34 && offsetof(LobbyMan, m_serverName) == 0xB4 &&
                  offsetof(LobbyMan, m_inGroupRoom) == 0x134 && offsetof(LobbyMan, m_hosting) == 0x139 &&
                  offsetof(LobbyMan, m_peer) == 0x140 && offsetof(LobbyMan, m_hostingTask) == 0x150,
              "layout must match the original");

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

} // namespace GameNet
