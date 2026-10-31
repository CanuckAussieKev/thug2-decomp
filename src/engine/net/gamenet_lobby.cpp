// The network manager's GameSpy lobby: small members and the peer SDK's hosting callbacks (0x0053F620-0x0053FEE0).
// See gamenet_lobby.h.
//
// NOT ORIGINAL: the game name reported (key 2) is the online identity's (NetLink::GameName()); the original pushes
// "thps6pc" and online_identity.cpp wrote the configured name over that push (0x0053F936), which this source replaces.

#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamespy.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "engine/core/task_list.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/online_identity.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"

#pragma warning(disable : 4996) // (strcpy: the original's copy of the server's name)

namespace OnlinePlay {

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
    kKeyForPlayer = 21,
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

void BufferAdd(void* buffer, const char* text) { GameSpy::Qr2BufferAdd(buffer, text); }
void BufferAddInt(void* buffer, int value) { GameSpy::Qr2BufferAddInt(buffer, value); }
void KeyBufferAdd(void* keyBuffer, int key) { GameSpy::Qr2KeyBufferAdd(keyBuffer, key); }

} // namespace

// 0x0053F7E0
void RoomManager::StopHosting()
{
    if (m_remote) {
        GameSpy::PeerStopGame(m_remote); // peerStopGame
        m_hostingTask->DropOut();
    }
    m_hosting = false;
}

// 0x0053F810
bool RoomManager::IsHosting() { return m_hosting; }
// 0x0053F820
char* RoomManager::GetRoomName() { return m_roomName; }
// 0x0053F830
void RoomManager::ChangeHostName(const char* name) { std::strcpy(m_serverName, name); }
// 0x0053F850
char* RoomManager::FetchHostName() { return m_serverName; }
// 0x0053F860
void RoomManager::ClearLostConnection() { m_lostConnection = false; }
// 0x0053F870
void RoomManager::SetLostConnection() { m_lostConnection = true; }
// 0x0053F880
void* RoomManager::GetLobbyHandle() { return m_remote; }
// 0x0053F890
bool RoomManager::InGroupRoom() { return m_inGroupRoom; }

// 0x0053F8B0
void LobbySearchA::DestroyBody() { m_vtable = kLobbySearchAVtable; }
// 0x0053F8C0
void LobbySearchB::DestroyBody() { m_vtable = kLobbySearchBVtable; }

// 0x0053F8A0
bool __cdecl CFunc_ResetConnectionTimer(Scripts::CStruct*, Scripts::CScript*)
{
    g_connectionTimer = FrameClock::GetTime();
    return true;
}

// 0x0053F8D0
void __cdecl RoomManager::ServerKeyCallback(void*, int key, void* buffer, void*)
{
    Hub* manager = g_onlineHub;
    OptionStore::Preferences* prefs = manager->GetOnlineSettings();
    switch (key) {
    case kKeyHostname: {
        const char* text = nullptr;
        prefs->FetchSetting(kServerName)->FetchText(kUiString, &text, 1);
        BufferAdd(buffer, text);
        break;
    }
    case kKeyGamename:
        BufferAdd(buffer, NetLink::GameName()); // (the original: "thps6pc")
        break;
    case kKeyHostport:
        BufferAddInt(buffer, kHostPort);
        break;
    case kKeyMapname:
        BufferAdd(buffer, manager->GetMapName(true)); // (null passed on, unlike the qr2 callback)
        break;
    case kKeyGametype:
    case kKeyGamemode:
        BufferAdd(buffer, manager->GetGameTypeName()); // the game type's name
        break;
    case kKeyNumplayers:
        BufferAddInt(buffer, manager->CountPlayers());
        break;
    case kKeyMaxplayers:
        BufferAddInt(buffer, manager->GetPlayerLimit());
        break;
    case kKeyTeamplay:
        BufferAddInt(buffer, 0);
        break;
    case kKeyPassword:
        BufferAddInt(buffer, *manager->FetchSecret() != '\0' ? 1 : 0);
        break;
    case kKeyObservers:
        BufferAddInt(buffer, manager->CountObservers());
        break;
    case kKeyMaxObservers:
        BufferAddInt(buffer, manager->GetSpectatorLimit());
        break;
    case kKeySkillLevel:
        BufferAddInt(buffer, manager->GetDifficultyTier());
        break;
    case kKeyInProgress:
        BufferAddInt(buffer, GameType() == kNetLobby ? 0 : 1);
        break;
    case kKeyHostMode:
        BufferAddInt(buffer, manager->GetHostingRole());
        break;
    case kKeyLoggedIn:
        BufferAddInt(buffer, Prop<bool>(manager->m_presenceService, 0x10) ? 1 : 0); // (the presence)
        break;
    default:
        BufferAdd(buffer, "");
        break;
    }
    g_gamePosted = true;
}

// 0x0053FEC0
int __cdecl RoomManager::CountCallback(void*, int type, void*)
{
    if (type == kKeyTypeServer)
        return kServerKeyCount;
    if (type == kKeyTypePlayer)
        return g_onlineHub->CountPlayers();
    return 0;
}

// 0x0053F620
void __cdecl RoomManager::KeyListCallback(void*, int type, void* keyBuffer, void*)
{
    if (type == kKeyTypeServer) {
        for (int key : {kKeyHostport, kKeyMapname, kKeyGametype, kKeyTeamplay, kKeyNumplayers, kKeyMaxplayers,
                        kKeyPassword, kKeyObservers, kKeyMaxObservers, kKeyLoggedIn, kKeySkillLevel, kKeyInProgress,
                        kKeyHostMode})
            KeyBufferAdd(keyBuffer, key);
    } else if (type == kKeyTypePlayer) {
        KeyBufferAdd(keyBuffer, kKeyForPlayer);
        KeyBufferAdd(keyBuffer, kKeyPlayerRating);
    }
}

} // namespace OnlinePlay
