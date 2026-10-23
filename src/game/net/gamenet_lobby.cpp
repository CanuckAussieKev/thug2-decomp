// The network manager's GameSpy lobby: small members and the peer SDK's hosting callbacks (0x0053F620-0x0053FEE0).
// See gamenet_lobby.h.
//
// NOT ORIGINAL: the game name reported (key 2) is the online identity's (Net::GameName()); the original pushes
// "thps6pc" and online_identity.cpp wrote the configured name over that push (0x0053F936), which this source replaces.

#include "game/net/gamenet_lobby.h"

#include <cstring>

#include "game/core/task.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/online_identity.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (strcpy: the original's copy of the server's name)

namespace GameNet {

using namespace Detail;

namespace {

// qr2's keys (the SDK's standard numbering) and THUG2's own.
enum PeerKey : int {
    kKeyHostname = 1,
    kKeyGamename = 2,
    kKeyHostport = 4,
    kKeyMapname = 5,
    kKeyGametype = 6,
    kKeyNumplayers = 8,
    kKeyMaxplayers = 10,
    kKeyGamemode = 11,
    kKeyTeamplay = 12,
    kKeyPassword = 19,
    kKeyPlayer = 21,
    kKeyObservers = 51,
    kKeyMaxObservers = 52,
    kKeySkillLevel = 53,
    kKeyInProgress = 54,
    kKeyHostMode = 55,
    kKeyLoggedIn = 56,
    kKeyPlayerRating = 57,
};
constexpr int kKeyTypeServer = 0;
constexpr int kKeyTypePlayer = 1;
constexpr int kServerKeyCount = 16;

constexpr uint32_t kServerName = 0x2A5CF019; // server_name
constexpr uint32_t kUiString = 0x96875C0F;   // ui_string
constexpr uint32_t kNetLobby = 0x1C471C60;   // netlobby
constexpr uint16_t kHostPort = 0x1421;       // 5153

void BufferAdd(void* buffer, const char* text) { GameFunction<void(__cdecl*)(void*, const char*)>(0x005FA010)(buffer, text); }
void BufferAddInt(void* buffer, int value) { GameFunction<void(__cdecl*)(void*, int)>(0x005FB350)(buffer, value); }
void KeyBufferAdd(void* keyBuffer, int key) { GameFunction<void(__cdecl*)(void*, int)>(0x005F9FE0)(keyBuffer, key); }

} // namespace

// 0x0053F7E0
void LobbyMan::StopHosting()
{
    if (m_peer) {
        GameFunction<void(__cdecl*)(void*)>(0x005FCCA0)(m_peer); // peerStopGame
        m_hostingTask->Remove();
    }
    m_hosting = false;
}

// 0x0053F810
bool LobbyMan::IsHosting() { return m_hosting; }
// 0x0053F820
char* LobbyMan::GetRoomName() { return m_roomName; }
// 0x0053F830
void LobbyMan::SetServerName(const char* name) { std::strcpy(m_serverName, name); }
// 0x0053F850
char* LobbyMan::GetServerName() { return m_serverName; }
// 0x0053F860
void LobbyMan::ClearLostConnection() { m_lostConnection = false; }
// 0x0053F870
void LobbyMan::SetLostConnection() { m_lostConnection = true; }
// 0x0053F880
void* LobbyMan::GetPeer() { return m_peer; }
// 0x0053F890
bool LobbyMan::InGroupRoom() { return m_inGroupRoom; }

// 0x0053F8B0
void LobbySearchA::DestroyBody() { m_vtable = reinterpret_cast<void**>(0x0064CB6C); }
// 0x0053F8C0
void LobbySearchB::DestroyBody() { m_vtable = reinterpret_cast<void**>(0x0064CB70); }

// 0x0053F8A0
bool __cdecl CFunc_ResetConnectionTimer(Script::CStruct*, Script::CScript*)
{
    GameVariable<uint32_t>(0x007CCE3C) = Tmr::GetTime();
    return true;
}

// 0x0053F8D0
void __cdecl LobbyMan::ServerKeyCallback(void*, int key, void* buffer, void*)
{
    Manager* manager = GameVariable<Manager*>(0x007CCE14);
    Prefs::Preferences* prefs = manager->GetNetworkPreferences();
    switch (key) {
    case kKeyHostname: {
        const char* text = nullptr;
        prefs->GetPreference(kServerName)->GetString(kUiString, &text, 1);
        BufferAdd(buffer, text);
        break;
    }
    case kKeyGamename:
        BufferAdd(buffer, Net::GameName()); // (the original: "thps6pc")
        break;
    case kKeyHostport:
        BufferAddInt(buffer, kHostPort);
        break;
    case kKeyMapname:
        BufferAdd(buffer, manager->GetLevelName(true)); // (null passed on, unlike the qr2 callback)
        break;
    case kKeyGametype:
    case kKeyGamemode:
        BufferAdd(buffer, manager->GetGameTypeName()); // the game type's name
        break;
    case kKeyNumplayers:
        BufferAddInt(buffer, manager->CountPlayers());
        break;
    case kKeyMaxplayers:
        BufferAddInt(buffer, manager->GetMaxPlayers());
        break;
    case kKeyTeamplay:
        BufferAddInt(buffer, 0);
        break;
    case kKeyPassword:
        BufferAddInt(buffer, *manager->GetPassword() != '\0' ? 1 : 0);
        break;
    case kKeyObservers:
        BufferAddInt(buffer, manager->CountObservers());
        break;
    case kKeyMaxObservers:
        BufferAddInt(buffer, manager->GetMaxObservers());
        break;
    case kKeySkillLevel:
        BufferAddInt(buffer, manager->GetSkillLevel());
        break;
    case kKeyInProgress:
        BufferAddInt(buffer, GameType() == kNetLobby ? 0 : 1);
        break;
    case kKeyHostMode:
        BufferAddInt(buffer, manager->GetHostMode());
        break;
    case kKeyLoggedIn:
        BufferAddInt(buffer, Field<bool>(manager->m_buddyMan, 0x10) ? 1 : 0); // (the presence)
        break;
    default:
        BufferAdd(buffer, "");
        break;
    }
    GameVariable<bool>(0x007CCE48) = true;
}

// 0x0053FEC0
int __cdecl LobbyMan::CountCallback(void*, int type, void*)
{
    if (type == kKeyTypeServer)
        return kServerKeyCount;
    if (type == kKeyTypePlayer)
        return GameVariable<Manager*>(0x007CCE14)->CountPlayers();
    return 0;
}

// 0x0053F620
void __cdecl LobbyMan::KeyListCallback(void*, int type, void* keyBuffer, void*)
{
    if (type == kKeyTypeServer) {
        for (int key : {kKeyHostport, kKeyMapname, kKeyGametype, kKeyTeamplay, kKeyNumplayers, kKeyMaxplayers,
                        kKeyPassword, kKeyObservers, kKeyMaxObservers, kKeyLoggedIn, kKeySkillLevel, kKeyInProgress,
                        kKeyHostMode})
            KeyBufferAdd(keyBuffer, key);
    } else if (type == kKeyTypePlayer) {
        KeyBufferAdd(keyBuffer, kKeyPlayer);
        KeyBufferAdd(keyBuffer, kKeyPlayerRating);
    }
}

} // namespace GameNet

REPLACE(0x0053F7E0, &GameNet::LobbyMan::StopHosting);
REPLACE(0x0053F810, &GameNet::LobbyMan::IsHosting);
REPLACE_SOURCE_ONLY(0x0053F820, &GameNet::LobbyMan::GetRoomName);
REPLACE(0x0053F830, &GameNet::LobbyMan::SetServerName);
REPLACE(0x0053F850, &GameNet::LobbyMan::GetServerName);
REPLACE(0x0053F860, &GameNet::LobbyMan::ClearLostConnection);
REPLACE(0x0053F870, &GameNet::LobbyMan::SetLostConnection);
REPLACE(0x0053F880, &GameNet::LobbyMan::GetPeer);
REPLACE(0x0053F890, &GameNet::LobbyMan::InGroupRoom);
REPLACE(0x0053F8A0, GameNet::CFunc_ResetConnectionTimer);
REPLACE(0x0053F8B0, &GameNet::LobbySearchA::DestroyBody);
REPLACE(0x0053F8C0, &GameNet::LobbySearchB::DestroyBody);
REPLACE(0x0053F8D0, GameNet::LobbyMan::ServerKeyCallback);
REPLACE(0x0053FEC0, GameNet::LobbyMan::CountCallback);
REPLACE(0x0053F620, GameNet::LobbyMan::KeyListCallback);
