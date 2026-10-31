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

namespace OnlinePlay {

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

Hub* NetManager() { return g_gameNetManager; }
RoomManager* Lobby() { return static_cast<RoomManager*>(NetManager()->m_lobbyMan); }
TaskList::PileUp* LogicTasks()
{
    return reinterpret_cast<TaskList::PileUp*>(reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(AssetStore::g_mainLoopManager)) + 4);
}
void PushInternetHeap()
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_internetTopDownHeap);
}
void PopHeap() { MemSys::Hub::Pull()->LeaveHeapScope(); }
void NatCancel(int cookie) { GameSpy::NatCancel(cookie); }
void NatThink() { GameSpy::NatThink(); }
void NatFreeNegotiations() { GameSpy::NatFreeNegotiations(); }
QueuedPacket* Queue() { return g_queuedPackets; }
int& QueuedCount() { return g_queuedCount; }

} // namespace

// 0x00540430
bool __cdecl CFunc_StartNatNegotiation(Scripts::CStruct* params, Scripts::CScript*)
{
    TaskList::PileUp* tasks = LogicTasks();
    Hub* manager = NetManager();
    const char* address = nullptr;
    int port = 0;
    PushInternetHeap();
    auto* lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    params->FetchText(0u, &address, 0);
    params->FetchIntValue(0u, &port, 0);
    params->ReadHashValue(kCookie, reinterpret_cast<uint32_t*>(&lobby->m_natCookie), 0);
    NetLink::AppHost* client = manager->StartClientSession(false, false, true, 0);
    GameSpy::PeerSendNatNegotiateCookie(lobby->m_remote, static_cast<uint32_t>(inet_addr(address)), port, lobby->m_natCookie); // the cookie to the host
    int error = GameSpy::NatBeginWithSocket(client->Socket(), lobby->m_natCookie, kClientNatIndex,
                                            CodeAddress(Stubs::DoNothing) /* no progress callback */,
                                            &RoomManager::NatNegotiationCompleted,
                                            reinterpret_cast<void*>(static_cast<intptr_t>(lobby->m_natCookie))); // negotiation on the client's socket
    if (error) {
        NatFreeNegotiations();
        PopHeap();
        return false;
    }
    tasks->QueueJob(lobby->m_natTask);
    client->SetOutsidePacketHook(reinterpret_cast<NetLink::ForeignPacketHandler>(&RoomManager::QueuePacket));
    tasks->QueueJob(lobby->m_hostingTask);
    for (int i = 0; i < QueuedCount(); ++i)
        Crt::Dealloc(Queue()[i].data); // (the game's C runtime free)
    QueuedCount() = 0;
    PopHeap();
    return true;
}

// 0x005403E0
bool __cdecl CFunc_CancelNatNegotiation(Scripts::CStruct*, Scripts::CScript*)
{
    RoomManager* lobby = Lobby();
    NatCancel(lobby->m_natCookie);
    NatThink();
    NatFreeNegotiations();
    Lobby()->m_natTask->DropOut();
    Lobby()->m_hostingTask->DropOut();
    return true;
}

// 0x00540590
bool __cdecl CFunc_LeaveLobby(Scripts::CStruct* params, Scripts::CScript*)
{
    PushInternetHeap();
    Hub* manager = NetManager();
    auto* lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    void* buddy = manager->m_buddyMan;
    GameSpy::PeerLeaveRoom(lobby->m_remote, kGroupRoom, nullptr); // the group room left
    lobby->m_inGroupRoom = false;
    if (Prop<bool>(buddy, 0x10) && !params->HasEntryNamed(kPreserveStatus)) {
        const char* text = Scripts::FetchText(kHomieStatusLoggingIn);
        const char* status = Scripts::FetchText(kHomieStatusOnline);
        static_cast<FriendsManager*>(buddy)->ChangeStanding(kStatusOnline, status, text); // the presence's status (its text, location)
    }
    GameSpy::PeerSetGroupId(lobby->m_remote, lobby->m_groupId);
    PopHeap();
    return true;
}

// 0x00540640
bool __cdecl CFunc_CancelLobbyConnect(Scripts::CStruct*, Scripts::CScript*)
{
    Lobby()->m_connecting = false;
    Lobby()->m_expectingDisconnect = true;
    return true;
}

// 0x00540660
bool __cdecl CFunc_SetQuietMode(Scripts::CStruct* params, Scripts::CScript*)
{
    PushInternetHeap();
    Hub* manager = NetManager();
    int quiet = params->HasBareFlag(kOff) ? 0 : 1;
    GameSpy::PeerSetQuietMode(static_cast<RoomManager*>(manager->m_lobbyMan)->m_remote, quiet);
    PopHeap();
    return true;
}

// 0x005406C0
bool __cdecl CFunc_CanHostGame(Scripts::CStruct*, Scripts::CScript*)
{
    Hub* manager = NetManager();
    RoomManager* lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    if (!lobby->m_restrictedHosting)
        return lobby->m_unknown30 < lobby->m_unknown2C;
    if (!Prop<bool>(manager->m_buddyMan, 0x10)) // (logged in to the presence)
        return false;
    int profile = Prop<int>(manager->m_buddyMan, 4);
    for (int allowed : kHostProfiles)
        if (profile == allowed)
            return true;
    return false;
}

} // namespace OnlinePlay
