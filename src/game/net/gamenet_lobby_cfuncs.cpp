// The GameSpy lobby's script commands: NAT negotiation started / cancelled, leaving the lobby, a cancelled connect,
// the quiet mode, whether this player may host (0x005403E0-0x00540750). See gamenet_lobby.h.

#define _WINSOCK_DEPRECATED_NO_WARNINGS // (inet_addr: the original's own call)
#include <winsock2.h>

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/task.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamespy.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/ass/assman.h"
#include "game/core/stubs.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/stats_man.h"
#include "game/sys/crt.h"

namespace GameNet {

using namespace Detail;

int g_queuedCount;

QueuedPacket g_queuedPackets[200];

namespace {

constexpr uint32_t kCookie = 0x751F4599;               // cookie
constexpr uint32_t kPreserveStatus = 0xB2AB4DAB;       // preserve_status
constexpr uint32_t kHomieStatusOnline = 0xFF257E72;    // homie_status_online
constexpr uint32_t kHomieStatusLoggingIn = 0x1E40E143; // homie_status_logging_in
constexpr uint32_t kOff = 0xD443A2BC;                  // off
constexpr int kStatusOnline = 1;
constexpr int kGroupRoom = 1; // (the peer SDK's room types)
constexpr int kClientNatIndex = 1;
// The GameSpy profiles that may host while hosting is restricted (+0x135).
constexpr int kHostProfiles[] = {0x01A7665B, 0x01A76689, 0x01A766AB, 0x01A76701, 0x01A766CC,
                                 0x01A7672E, 0x01A7675B, 0x01A76773, 0x01A76794, 0x01A767B1};

Manager* NetManager() { return g_gameNetManager; }
LobbyMan* Lobby() { return static_cast<LobbyMan*>(NetManager()->m_lobbyMan); }
Tsk::Stack* LogicTasks()
{
    return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(Ass::g_mainLoopManager)) + 4);
}
void PushInternetHeap()
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_internetTopDownHeap);
}
void PopHeap() { Mem::Manager::Get()->PopContext(); }
void NatCancel(int cookie) { GameSpy::NatCancel(cookie); }
void NatThink() { GameSpy::NatThink(); }
void NatFreeNegotiations() { GameSpy::NatFreeNegotiations(); }
QueuedPacket* Queue() { return g_queuedPackets; }
int& QueuedCount() { return g_queuedCount; }

} // namespace

// 0x00540430
bool __cdecl CFunc_StartNatNegotiation(Script::CStruct* params, Script::CScript*)
{
    Tsk::Stack* tasks = LogicTasks();
    Manager* manager = NetManager();
    const char* address = nullptr;
    int port = 0;
    PushInternetHeap();
    auto* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    params->GetString(0u, &address, 0);
    params->GetInteger(0u, &port, 0);
    params->GetChecksum(kCookie, reinterpret_cast<uint32_t*>(&lobby->m_natCookie), 0);
    Net::App* client = manager->SpawnClient(false, false, true, 0);
    GameSpy::PeerSendNatNegotiateCookie(lobby->m_peer, static_cast<uint32_t>(inet_addr(address)), port, lobby->m_natCookie); // the cookie to the host
    int error = GameSpy::NatBeginWithSocket(client->Socket(), lobby->m_natCookie, kClientNatIndex,
                                            CodeAddress(Stubs::DoNothing) /* no progress callback */,
                                            &LobbyMan::NatNegotiationCompleted,
                                            reinterpret_cast<void*>(static_cast<intptr_t>(lobby->m_natCookie))); // negotiation on the client's socket
    if (error) {
        NatFreeNegotiations();
        PopHeap();
        return false;
    }
    tasks->AddTask(lobby->m_natTask);
    client->SetForeignPacketHandler(reinterpret_cast<Net::ForeignPacketHandler>(&LobbyMan::QueuePacket));
    tasks->AddTask(lobby->m_hostingTask);
    for (int i = 0; i < QueuedCount(); ++i)
        Crt::Free(Queue()[i].data); // (the game's C runtime free)
    QueuedCount() = 0;
    PopHeap();
    return true;
}

// 0x005403E0
bool __cdecl CFunc_CancelNatNegotiation(Script::CStruct*, Script::CScript*)
{
    LobbyMan* lobby = Lobby();
    NatCancel(lobby->m_natCookie);
    NatThink();
    NatFreeNegotiations();
    Lobby()->m_natTask->Remove();
    Lobby()->m_hostingTask->Remove();
    return true;
}

// 0x00540590
bool __cdecl CFunc_LeaveLobby(Script::CStruct* params, Script::CScript*)
{
    PushInternetHeap();
    Manager* manager = NetManager();
    auto* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    void* buddy = manager->m_buddyMan;
    GameSpy::PeerLeaveRoom(lobby->m_peer, kGroupRoom, nullptr); // the group room left
    lobby->m_inGroupRoom = false;
    if (Field<bool>(buddy, 0x10) && !params->ContainsComponentNamed(kPreserveStatus)) {
        const char* text = Script::GetString(kHomieStatusLoggingIn);
        const char* status = Script::GetString(kHomieStatusOnline);
        static_cast<BuddyMan*>(buddy)->SetStatus(kStatusOnline, status, text); // the presence's status (its text, location)
    }
    GameSpy::PeerSetGroupId(lobby->m_peer, lobby->m_groupId);
    PopHeap();
    return true;
}

// 0x00540640
bool __cdecl CFunc_CancelLobbyConnect(Script::CStruct*, Script::CScript*)
{
    Lobby()->m_connecting = false;
    Lobby()->m_expectingDisconnect = true;
    return true;
}

// 0x00540660
bool __cdecl CFunc_SetQuietMode(Script::CStruct* params, Script::CScript*)
{
    PushInternetHeap();
    Manager* manager = NetManager();
    int quiet = params->ContainsFlag(kOff) ? 0 : 1;
    GameSpy::PeerSetQuietMode(static_cast<LobbyMan*>(manager->m_lobbyMan)->m_peer, quiet);
    PopHeap();
    return true;
}

// 0x005406C0
bool __cdecl CFunc_CanHostGame(Script::CStruct*, Script::CScript*)
{
    Manager* manager = NetManager();
    LobbyMan* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    if (!lobby->m_restrictedHosting)
        return lobby->m_unknown30 < lobby->m_unknown2C;
    if (!Field<bool>(manager->m_buddyMan, 0x10)) // (logged in to the presence)
        return false;
    int profile = Field<int>(manager->m_buddyMan, 4);
    for (int allowed : kHostProfiles)
        if (profile == allowed)
            return true;
    return false;
}

} // namespace GameNet
