// The GameSpy lobby's connection, hosting and NAT negotiation (OnlinePlay::RoomManager, 0x0053F570-0x00540C90): the peer
// SDK's callbacks, the lobby's tasks and the packet queue they share. The peer and NAT negotiation SDKs are library
// code, called by address. See gamenet_lobby.h.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/task_list.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamespy.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_manager.h"
#include "game/script/preferences.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/script/game_cfuncs.h"
#include "game/core/code_address.h"
#include "game/ass/resource_registry.h"
#include "game/core/stubs.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/rig.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/stats_man.h"
#include "game/sys/crt.h"

#pragma warning(disable : 4996) // (sprintf / strcpy: the original's own)

namespace OnlinePlay {

using namespace Detail;

namespace {

// Scripts.
constexpr uint32_t kConnectionFailureDialog = 0x209F813A; // create_gamespy_connection_failure_dialog
constexpr uint32_t kLostConnectionToGameSpy = 0xFB1C18E4; // lost_connection_to_gamespy
constexpr uint32_t kCreateNotPostedDialog = 0xE1B4ADEB;   // CreateNotPostedDialog
constexpr uint32_t kNatNegotiationFailed = 0x3A9FB574;  // show_nat_timeout
constexpr uint32_t kHomieStatusHosting = 0xC5A16CE1;      // homie_status_hosting
constexpr uint32_t kServerName = 0x2A5CF019;              // server_name
constexpr uint32_t kUiString = 0x96875C0F;                // ui_string
constexpr uint32_t kNetworkId = 0xCBED746E;               // network_id

constexpr int kStatusHosting = 2;
constexpr uint16_t kHostPort = 0x1421; // 5153
constexpr int kMaxQueued = 199;
constexpr int kNickInUse = 0;
constexpr int kNickInvalid = 1;
constexpr int kNatSuccess = 0;
constexpr uint32_t kNotPostedTime = 90000; // ms hosting without being reported
constexpr uint32_t kReportInterval = 5000;
// qr2's keys.
constexpr int kKeyPlayer = 21;
constexpr int kKeyPing = 24;
constexpr int kKeyRating = 57;

constexpr int kPacketQuery = 1;
constexpr int kPacketNatNegotiation = 2;
constexpr uint8_t kNatMagic[6] = {0xFD, 0xFC, 0x1E, 0x66, 0x6A, 0xB2};

// PlayerRecord flags (+0x1D0) / JoinerRecord flags (+0x20).
constexpr uint32_t kPlayerObserverCounted = 0x08; // (0x005434C0: an observer reported as a player)
constexpr uint32_t kNewPlayerCounted = 0x18;
constexpr uint32_t kNewPlayerObserver = 0x04;

Hub* NetManager() { return g_gameNetManager; }
TaskList::PileUp* LogicTasks()
{
    return reinterpret_cast<TaskList::PileUp*>(reinterpret_cast<uint8_t*>(reinterpret_cast<void*>(AssetStore::g_mainLoopManager)) + 4);
}
QueuedPacket* Queue() { return g_queuedPackets; }
int& QueuedCount() { return g_queuedCount; }
RoomManager* TaskLobby(void* task) { return Prop<RoomManager*>(task, 0x1C); }

// The heaps the lobby's SDK work allocates from.
void PushInternetHeap()
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_internetTopDownHeap);
}
void PushInternetBottomUpHeap()
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_internetBottomUpHeap);
}
void PopHeap() { MemSys::Hub::Pull()->LeaveHeapScope(); }

// The game's C runtime (the queue's copies are freed by the original's code too).
void* GameMalloc(size_t size) { return Crt::AllocBlock(size); }
void GameFree(void* memory) { Crt::Dealloc(memory); }

// The queue emptied (its copies freed).
void ClearQueue()
{
    for (int i = 0; i < QueuedCount(); ++i)
        GameFree(Queue()[i].data);
    QueuedCount() = 0;
}

// The SDKs (gamespy.h).
void BufferAdd(void* buffer, const char* text) { GameSpy::Qr2BufferAdd(buffer, text); }
void BufferAddInt(void* buffer, int value) { GameSpy::Qr2BufferAddInt(buffer, value); }
uint32_t PeerPrivateIp(void* peer) { return GameSpy::PeerGetPrivateIp(peer); }
void PeerStateChanged(void* peer) { GameSpy::PeerStateChanged(peer); } // (the hosted game's details sent again)
void NatThink() { GameSpy::NatThink(); }
void NatCancel(int cookie) { GameSpy::NatCancel(cookie); }
void NatFreeNegotiations() { GameSpy::NatFreeNegotiations(); }
// (the cookie is the user data)
int NatBegin(uint32_t socket, int cookie, int clientIndex, void* progress, GameSpy::HookFn completed, int userData)
{
    return GameSpy::NatBeginWithSocket(socket, cookie, clientIndex, progress, completed,
                                       reinterpret_cast<void*>(static_cast<intptr_t>(userData)));
}
void* const kNoProgress = CodeAddress(Stubs::DoNothing); // (an empty function: no progress callback)

} // namespace

bool g_notPostedShown;
uint32_t g_connectionTimer;
uint32_t g_lastReportTime;
uint32_t g_hostingSince;
bool g_gamePosted;
uint32_t g_connectedTime;

// 0x0053F570
void __cdecl RoomManager::DisconnectedCallback(void*, const char* reason, void* param)
{
    uint32_t elapsed = FrameClock::GetTime() - g_connectedTime; // since connected
    DebugOut::Printf("Disconnected at %d after %d ms. Reason: %s\n", FrameClock::GetTime(), elapsed, reason);
    auto* lobby = static_cast<RoomManager*>(param);
    if (lobby->m_connecting) {
        DebugOut::Printf("******* Connect Callback FAILED!\n");
        Run(kConnectionFailureDialog);
        lobby->m_connecting = false;
    } else if (!lobby->m_expectingDisconnect) {
        DebugOut::Printf("Wasn't expecting discon.  Attempting to stop hosting game\n");
        Run(kLostConnectionToGameSpy);
    } else {
        DebugOut::Printf("Was expecting discon. Ignore\n");
    }
    lobby->m_expectingDisconnect = true;
}

// 0x0053F6C0
void __cdecl RoomManager::QueuePacket(char* data, int length, const void* from)
{
    if (QueuedCount() >= kMaxQueued)
        return;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    int type;
    if ((bytes[0] == 0xFE && bytes[1] == 0xFD) || bytes[0] == '\\')
        type = kPacketQuery;
    else if (length >= 6 && std::memcmp(bytes, kNatMagic, sizeof(kNatMagic)) == 0)
        type = kPacketNatNegotiation;
    else
        return;
    QueuedPacket& packet = Queue()[QueuedCount()];
    packet.data = static_cast<char*>(GameMalloc(length + 1));
    std::memcpy(packet.data, data, length);
    packet.data[length] = '\0';
    std::memcpy(packet.from, from, sizeof(packet.from));
    packet.type = type;
    packet.length = length;
    ++QueuedCount();
}

// 0x0053FB80
void __cdecl RoomManager::PlayerKeyCallback(void*, int key, int index, void* buffer, void*)
{
    g_gamePosted = true;
    if (key == kKeyPing) {
        BufferAdd(buffer, "");
        return;
    }
    if (key != kKeyPlayer && key != kKeyRating)
        return;
    Hub* manager = NetManager();
    int count = 0;
    PlayerSearch search;
    NewPlayerSearch newSearch;
    for (uint8_t* p = FirstPlayer(manager, &search, true); p; p = NextPlayer(manager, &search, true)) {
        if (IsObserver(p) && !(Prop<uint32_t>(p, 0x1D0) & kPlayerObserverCounted))
            continue;
        if (count == index) {
            if (key == kKeyPlayer)
                BufferAdd(buffer, PlayerName(p));
            else
                BufferAddInt(buffer, Prop<int>(p, 0x2C)); // its rating
            return;
        }
        ++count;
    }
    for (uint8_t* info = FirstNewPlayer(manager, &newSearch); info; info = NextNewPlayer(manager, &newSearch)) {
        uint32_t flags = Prop<uint32_t>(info, 0x20);
        if (!(flags & kNewPlayerCounted) && (flags & kNewPlayerObserver))
            continue;
        if (count == index) {
            if (key == kKeyPlayer)
                BufferAdd(buffer, reinterpret_cast<const char*>(info)); // (its name, at +0)
            else
                BufferAddInt(buffer, Prop<int>(info, 0x30));
            return;
        }
        ++count;
    }
    BufferAdd(buffer, "");
}

// 0x0053FDA0
void __cdecl RoomManager::PublicAddressCallback(void*, uint32_t ip, uint16_t port, void* param)
{
    Hub* manager = NetManager();
    void* buddy = manager->m_buddyMan;
    if (!manager->OnServer())
        return;
    auto* lobby = static_cast<RoomManager*>(param);
    if (!lobby->m_remote)
        return;
    manager->SetEntryPort(port);
    manager->SetJoinIp(ip);
    manager->SetJoinPrivateIp(PeerPrivateIp(lobby->m_remote));
    const char* name = nullptr;
    manager->GetOnlineSettings()->FetchSetting(kServerName)->FetchText(kUiString, &name, 1);
    char location[1024];
    std::sprintf(location, "10thug211:%d:%d:%d:%s (%s)", ip, PeerPrivateIp(lobby->m_remote), port, name, lobby->m_roomName);
    if (Prop<bool>(buddy, 0x10)) // (logged in to the presence)
        static_cast<FriendsManager*>(buddy)->ChangeStanding(kStatusHosting, Scripts::FetchText(kHomieStatusHosting), location);
}

// 0x0053FEE0
void __cdecl RoomManager::NickErrorCallback(void* peer, int type, const char* nick, int, const char**, void*)
{
    PushInternetHeap();
    char retry[128];
    if (type == kNickInUse) {
        const char* id = NetManager()->GetOnlineSettings()->GetPreferenceString(kNetworkId, kUiString);
        DebugOut::Printf("Nick Error. Nick %s In Use\n", nick);
        std::sprintf(retry, "a%d_%s", FrameClock::GetTime(), id);
        GameSpy::PeerRetryWithNick(peer, static_cast<const char*>(retry)); // retried with that nick
    } else if (type == kNickInvalid) {
        DebugOut::Printf("Nick Error. Nick %s is Invalid\n", nick);
        GameSpy::PeerFixNick(static_cast<char*>(retry), nick); // the nick made valid
        GameSpy::PeerRetryWithNick(peer, static_cast<const char*>(retry));
    }
    PopHeap();
}

// 0x0053FFC0
void __cdecl RoomManager::ConnectCallback(void* peer, int success, int, void* param)
{
    auto* lobby = static_cast<RoomManager*>(param);
    lobby->m_connecting = false;
    if (!success) {
        DebugOut::Printf("******* Connect Callback FAILED!\n");
        Run(kConnectionFailureDialog);
        return;
    }
    NetLink::Hub* net = NetLink::Hub::Sole();
    DebugOut::Printf("****** Connect Callback Successful\n");
    net->SetLocalAddress(GameSpy::PeerGetPublicIp(peer)); // the peer's public address
    ScriptCommands::CFunc_StartLobbyList(nullptr, nullptr);
    lobby->m_expectingDisconnect = false;
    g_connectedTime = FrameClock::GetTime(); // connected
}

// 0x00540040
void __cdecl RoomManager::ThinkTask(void* task)
{
    RoomManager* lobby = TaskLobby(task);
    PushInternetHeap();
    if (GameSpy::PeerHasChat(lobby->m_remote)) // (its chat connection made)
        GameSpy::PeerThink(lobby->m_remote);
    if (lobby->m_lostConnection) {
        lobby->m_lostConnection = false;
        Run(kLostConnectionToGameSpy);
    }
    PopHeap();
}

// 0x005400B0
void __cdecl RoomManager::NatNegotiationTask(void*)
{
    PushInternetBottomUpHeap();
    NatThink();
    PopHeap();
}

// 0x005400E0
void __cdecl RoomManager::QueryTask(void* task)
{
    RoomManager* lobby = TaskLobby(task);
    uint32_t now = FrameClock::GetTime();
    PushInternetHeap();
    for (int i = 0; i < QueuedCount(); ++i) {
        QueuedPacket& packet = Queue()[i];
        if (packet.type == kPacketQuery)
            GameSpy::PeerParseQuery(lobby->m_remote, static_cast<char*>(packet.data), packet.length, static_cast<void*>(packet.from));
        else if (packet.type == kPacketNatNegotiation)
            GameSpy::NatProcessData(static_cast<char*>(packet.data), packet.length, static_cast<void*>(packet.from));
        GameFree(packet.data);
    }
    QueuedCount() = 0;
    Hub* manager = NetManager();
    if (!g_gamePosted && manager->InNetGame() && manager->OnServer()) { // not reported yet
        if (now - g_hostingSince > kNotPostedTime) {
            if (!g_notPostedShown) {
                Run(kCreateNotPostedDialog);
                g_notPostedShown = true;
            }
        } else if (now - g_lastReportTime > kReportInterval) {
            PeerStateChanged(lobby->m_remote);
            g_lastReportTime = now;
        }
    }
    PopHeap();
}

// 0x00540220
void RoomManager::StartHosting()
{
    TaskList::PileUp* tasks = LogicTasks();
    NetLink::AppHost* server = NetManager()->m_host;
    server->SetOutsidePacketHook(reinterpret_cast<NetLink::ForeignPacketHandler>(&RoomManager::QueuePacket));
    ClearQueue();
    if (GameSpy::PeerStartReportingWithSocket(m_remote, server->Socket(), static_cast<int>(kHostPort)) == 1) // reporting on its socket
        PeerStateChanged(m_remote);
    uint32_t now = FrameClock::GetTime();
    g_hostingSince = now; // hosting since
    g_connectionTimer = now; // the connection timer
    g_lastReportTime = now; // reported last
    g_notPostedShown = false;
    g_gamePosted = false;
    tasks->QueueJob(m_hostingTask);
    m_hosting = true;
}

// 0x00540300
void __cdecl RoomManager::NatNegotiationCompleted(int result, int, const void* address, void* userData)
{
    Hub* manager = NetManager();
    if (result == kNatSuccess && !manager->OnServer()) { // the client joins at the address found
        const auto* sockaddr = static_cast<const uint8_t*>(address);
        uint16_t port = static_cast<uint16_t>(sockaddr[2] << 8 | sockaddr[3]); // (ntohs)
        manager->JoinServer(*reinterpret_cast<const uint32_t*>(sockaddr + 4), port, 0);
    }
    auto* lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    if (manager->OnServer()) {
        if (--lobby->m_negotiations == 0)
            lobby->m_natTask->DropOut();
        return;
    }
    NatCancel(static_cast<int>(reinterpret_cast<intptr_t>(userData)));
    NatThink();
    NatFreeNegotiations();
    manager->FetchPatron(0)->SetOutsidePacketHook(nullptr);
    lobby->m_natTask->DropOut();
    lobby->m_hostingTask->DropOut();
    if (result != kNatSuccess)
        Run(kNatNegotiationFailed);
}

// 0x00540750
void RoomManager::Hangup()
{
    PushInternetHeap();
    m_thinkTask->DropOut();
    m_natTask->DropOut();
    m_hostingTask->DropOut();
    if (m_remote)
        GameSpy::PeerShutdown(m_remote); // the peer shut down
    m_remote = nullptr;
    PopHeap();
}

// 0x00540AB0
unsigned long __stdcall RoomManager::ConnectThread(void* lobbyData)
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_internetBottomUpHeap);
    Hub* manager = NetManager();
    char nick[128];
    std::strcpy(nick, manager->GetOnlineSettings()->GetPreferenceString(kNetworkId, kUiString));
    auto* lobby = static_cast<RoomManager*>(lobbyData);
    lobby->m_connecting = true;
    auto nickError = &RoomManager::NickErrorCallback;
    auto connected = &RoomManager::ConnectCallback;
    GameSpy::PeerConnect(lobby->m_remote, static_cast<const char*>(nick), Prop<int>(manager->m_buddyMan, 4) /* its profile */,
                         nickError, connected, lobbyData, 0); // peer connect (not blocking)
    MemSys::Hub::Pull()->LeaveHeapScope();
    return 0;
}

// 0x00540C90
void __cdecl RoomManager::NatNegotiateCallback(void*, int cookie, void*)
{
    TaskList::PileUp* tasks = LogicTasks();
    Hub* manager = NetManager();
    if (static_cast<RoomManager*>(manager->m_lobbyMan)->m_negotiations != 0)
        return; // (one at a time)
    NetLink::AppHost* server = manager->m_host;
    PushInternetHeap();
    if (NatBegin(server->Socket(), cookie, 0, kNoProgress, &RoomManager::NatNegotiationCompleted, cookie)) {
        NatCancel(cookie);
        NatThink();
        PopHeap();
        return;
    }
    auto* lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    if (lobby->m_negotiations == 0)
        tasks->QueueJob(lobby->m_natTask);
    ++lobby->m_negotiations;
    PopHeap();
}

} // namespace OnlinePlay
