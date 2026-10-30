// The GameSpy lobby's connection, hosting and NAT negotiation (GameNet::LobbyMan, 0x0053F570-0x00540C90): the peer
// SDK's callbacks, the lobby's tasks and the packet queue they share. The peer and NAT negotiation SDKs are library
// code, called by address. See gamenet_lobby.h.

#include <cstdio>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/task.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_manager.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/script/game_cfuncs.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (sprintf / strcpy: the original's own)

namespace GameNet {

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

// PlayerInfo flags (+0x1D0) / NewPlayerInfo flags (+0x20).
constexpr uint32_t kPlayerObserverCounted = 0x08; // (0x005434C0: an observer reported as a player)
constexpr uint32_t kNewPlayerCounted = 0x18;
constexpr uint32_t kNewPlayerObserver = 0x04;

Manager* NetManager() { return g_gameNetManager; }
Tsk::Stack* LogicTasks()
{
    return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(GameVariable<void*>(0x00696D68)) + 4);
}
QueuedPacket* Queue() { return g_queuedPackets; }
int& QueuedCount() { return g_queuedCount; }
LobbyMan* TaskLobby(void* task) { return Field<LobbyMan*>(task, 0x1C); }

// The heaps the lobby's SDK work allocates from.
void PushInternetHeap()
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_internetTopDownHeap);
}
void PushInternetBottomUpHeap()
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_internetBottomUpHeap);
}
void PopHeap() { Mem::Manager::Get()->PopContext(); }

// The game's C runtime (the queue's copies are freed by the original's code too).
void* GameMalloc(size_t size) { return GameFunction<void*(__cdecl*)(size_t)>(0x00624031)(size); }
void GameFree(void* memory) { GameFunction<void(__cdecl*)(void*)>(0x0062411B)(memory); }

// The queue emptied (its copies freed).
void ClearQueue()
{
    for (int i = 0; i < QueuedCount(); ++i)
        GameFree(Queue()[i].data);
    QueuedCount() = 0;
}

// The SDKs.
template <typename R = void, typename... A>
R Sdk(uint32_t address, A... args)
{
    return GameFunction<R(__cdecl*)(A...)>(address)(args...);
}
void BufferAdd(void* buffer, const char* text) { Sdk(0x005FA010, buffer, text); }
void BufferAddInt(void* buffer, int value) { Sdk(0x005FB350, buffer, value); }
uint32_t PeerPrivateIp(void* peer) { return Sdk<uint32_t>(0x005FC950, peer); }
void PeerStateChanged(void* peer) { Sdk(0x005FCCF0, peer); } // (the hosted game's details sent again)
void NatThink() { Sdk(0x005FC660); }
void NatCancel(int cookie) { Sdk(0x005FC130, cookie); }
void NatFreeNegotiations() { Sdk(0x005FBCD0); }
using NatProgress = void(__cdecl*)();
using NatCompleted = void(__cdecl*)(int, int, const void*, void*);
int NatBegin(uint32_t socket, int cookie, int clientIndex, void* progress, NatCompleted completed, int userData)
{
    return Sdk<int>(0x005FC090, socket, cookie, clientIndex, progress, completed, userData);
}
void* const kNoProgress = reinterpret_cast<void*>(0x005480A0); // (an empty function: no progress callback)

} // namespace

GAME_GLOBAL(bool, g_notPostedShown, 0x007CCE3A);
GAME_GLOBAL(uint32_t, g_connectionTimer, 0x007CCE3C);
GAME_GLOBAL(uint32_t, g_lastReportTime, 0x007CCE40);
GAME_GLOBAL(uint32_t, g_hostingSince, 0x007CCE44);
GAME_GLOBAL(bool, g_gamePosted, 0x007CCE48);
GAME_GLOBAL(uint32_t, g_connectedTime, 0x007CE434);

// 0x0053F570
void __cdecl LobbyMan::DisconnectedCallback(void*, const char* reason, void* param)
{
    uint32_t elapsed = Tmr::GetTime() - g_connectedTime; // since connected
    Dbg::Printf("Disconnected at %d after %d ms. Reason: %s\n", Tmr::GetTime(), elapsed, reason);
    auto* lobby = static_cast<LobbyMan*>(param);
    if (lobby->m_connecting) {
        Dbg::Printf("******* Connect Callback FAILED!\n");
        Run(kConnectionFailureDialog);
        lobby->m_connecting = false;
    } else if (!lobby->m_expectingDisconnect) {
        Dbg::Printf("Wasn't expecting discon.  Attempting to stop hosting game\n");
        Run(kLostConnectionToGameSpy);
    } else {
        Dbg::Printf("Was expecting discon. Ignore\n");
    }
    lobby->m_expectingDisconnect = true;
}

// 0x0053F6C0
void __cdecl LobbyMan::QueuePacket(char* data, int length, const void* from)
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
void __cdecl LobbyMan::PlayerKeyCallback(void*, int key, int index, void* buffer, void*)
{
    g_gamePosted = true;
    if (key == kKeyPing) {
        BufferAdd(buffer, "");
        return;
    }
    if (key != kKeyPlayer && key != kKeyRating)
        return;
    Manager* manager = NetManager();
    int count = 0;
    PlayerSearch search;
    NewPlayerSearch newSearch;
    for (uint8_t* p = FirstPlayer(manager, &search, true); p; p = NextPlayer(manager, &search, true)) {
        if (IsObserver(p) && !(Field<uint32_t>(p, 0x1D0) & kPlayerObserverCounted))
            continue;
        if (count == index) {
            if (key == kKeyPlayer)
                BufferAdd(buffer, PlayerName(p));
            else
                BufferAddInt(buffer, Field<int>(p, 0x2C)); // its rating
            return;
        }
        ++count;
    }
    for (uint8_t* info = FirstNewPlayer(manager, &newSearch); info; info = NextNewPlayer(manager, &newSearch)) {
        uint32_t flags = Field<uint32_t>(info, 0x20);
        if (!(flags & kNewPlayerCounted) && (flags & kNewPlayerObserver))
            continue;
        if (count == index) {
            if (key == kKeyPlayer)
                BufferAdd(buffer, reinterpret_cast<const char*>(info)); // (its name, at +0)
            else
                BufferAddInt(buffer, Field<int>(info, 0x30));
            return;
        }
        ++count;
    }
    BufferAdd(buffer, "");
}

// 0x0053FDA0
void __cdecl LobbyMan::PublicAddressCallback(void*, uint32_t ip, uint16_t port, void* param)
{
    Manager* manager = NetManager();
    void* buddy = manager->m_buddyMan;
    if (!manager->OnServer())
        return;
    auto* lobby = static_cast<LobbyMan*>(param);
    if (!lobby->m_peer)
        return;
    manager->SetJoinPort(port);
    manager->SetJoinIp(ip);
    manager->SetJoinPrivateIp(PeerPrivateIp(lobby->m_peer));
    const char* name = nullptr;
    manager->GetNetworkPreferences()->GetPreference(kServerName)->GetString(kUiString, &name, 1);
    char location[1024];
    std::sprintf(location, "10thug211:%d:%d:%d:%s (%s)", ip, PeerPrivateIp(lobby->m_peer), port, name, lobby->m_roomName);
    if (Field<bool>(buddy, 0x10)) // (logged in to the presence)
        static_cast<BuddyMan*>(buddy)->SetStatus(kStatusHosting, Script::GetString(kHomieStatusHosting), location);
}

// 0x0053FEE0
void __cdecl LobbyMan::NickErrorCallback(void* peer, int type, const char* nick, int, const char**, void*)
{
    PushInternetHeap();
    char retry[128];
    if (type == kNickInUse) {
        const char* id = NetManager()->GetNetworkPreferences()->GetPreferenceString(kNetworkId, kUiString);
        Dbg::Printf("Nick Error. Nick %s In Use\n", nick);
        std::sprintf(retry, "a%d_%s", Tmr::GetTime(), id);
        Sdk(0x005FC6F0, peer, static_cast<const char*>(retry)); // retried with that nick
    } else if (type == kNickInvalid) {
        Dbg::Printf("Nick Error. Nick %s is Invalid\n", nick);
        Sdk(0x005FD9F0, static_cast<char*>(retry), nick); // the nick made valid
        Sdk(0x005FC6F0, peer, static_cast<const char*>(retry));
    }
    PopHeap();
}

// 0x0053FFC0
void __cdecl LobbyMan::ConnectCallback(void* peer, int success, int, void* param)
{
    auto* lobby = static_cast<LobbyMan*>(param);
    lobby->m_connecting = false;
    if (!success) {
        Dbg::Printf("******* Connect Callback FAILED!\n");
        Run(kConnectionFailureDialog);
        return;
    }
    Net::Manager* net = Net::Manager::Instance();
    Dbg::Printf("****** Connect Callback Successful\n");
    net->SetLocalAddress(Sdk<uint32_t>(0x005FC940, peer)); // the peer's public address
    CFuncs::CFunc_StartLobbyList(nullptr, nullptr);
    lobby->m_expectingDisconnect = false;
    g_connectedTime = Tmr::GetTime(); // connected
}

// 0x00540040
void __cdecl LobbyMan::ThinkTask(void* task)
{
    LobbyMan* lobby = TaskLobby(task);
    PushInternetHeap();
    if (Sdk<int>(0x00603360, lobby->m_peer)) // (connected)
        Sdk(0x005FD1F0, lobby->m_peer);     // the peer thinks
    if (lobby->m_lostConnection) {
        lobby->m_lostConnection = false;
        Run(kLostConnectionToGameSpy);
    }
    PopHeap();
}

// 0x005400B0
void __cdecl LobbyMan::NatNegotiationTask(void*)
{
    PushInternetBottomUpHeap();
    NatThink();
    PopHeap();
}

// 0x005400E0
void __cdecl LobbyMan::QueryTask(void* task)
{
    LobbyMan* lobby = TaskLobby(task);
    uint32_t now = Tmr::GetTime();
    PushInternetHeap();
    for (int i = 0; i < QueuedCount(); ++i) {
        QueuedPacket& packet = Queue()[i];
        if (packet.type == kPacketQuery)
            Sdk(0x005FC970, lobby->m_peer, static_cast<char*>(packet.data), packet.length, static_cast<void*>(packet.from));
        else if (packet.type == kPacketNatNegotiation)
            Sdk(0x005FC3B0, static_cast<char*>(packet.data), packet.length, static_cast<void*>(packet.from));
        GameFree(packet.data);
    }
    QueuedCount() = 0;
    Manager* manager = NetManager();
    if (!g_gamePosted && manager->InNetGame() && manager->OnServer()) { // not reported yet
        if (now - g_hostingSince > kNotPostedTime) {
            if (!g_notPostedShown) {
                Run(kCreateNotPostedDialog);
                g_notPostedShown = true;
            }
        } else if (now - g_lastReportTime > kReportInterval) {
            PeerStateChanged(lobby->m_peer);
            g_lastReportTime = now;
        }
    }
    PopHeap();
}

// 0x00540220
void LobbyMan::StartHosting()
{
    Tsk::Stack* tasks = LogicTasks();
    Net::App* server = NetManager()->m_server;
    server->SetForeignPacketHandler(reinterpret_cast<Net::ForeignPacketHandler>(&LobbyMan::QueuePacket));
    ClearQueue();
    if (Sdk<int>(0x005FCC70, m_peer, server->Socket(), static_cast<int>(kHostPort)) == 1) // reporting on its socket
        PeerStateChanged(m_peer);
    uint32_t now = Tmr::GetTime();
    g_hostingSince = now; // hosting since
    g_connectionTimer = now; // the connection timer
    g_lastReportTime = now; // reported last
    g_notPostedShown = false;
    g_gamePosted = false;
    tasks->AddTask(m_hostingTask);
    m_hosting = true;
}

// 0x00540300
void __cdecl LobbyMan::NatNegotiationCompleted(int result, int, const void* address, void* userData)
{
    Manager* manager = NetManager();
    if (result == kNatSuccess && !manager->OnServer()) { // the client joins at the address found
        const auto* sockaddr = static_cast<const uint8_t*>(address);
        uint16_t port = static_cast<uint16_t>(sockaddr[2] << 8 | sockaddr[3]); // (ntohs)
        manager->JoinServer(*reinterpret_cast<const uint32_t*>(sockaddr + 4), port, 0);
    }
    auto* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    if (manager->OnServer()) {
        if (--lobby->m_negotiations == 0)
            lobby->m_natTask->Remove();
        return;
    }
    NatCancel(static_cast<int>(reinterpret_cast<intptr_t>(userData)));
    NatThink();
    NatFreeNegotiations();
    manager->GetClient(0)->SetForeignPacketHandler(nullptr);
    lobby->m_natTask->Remove();
    lobby->m_hostingTask->Remove();
    if (result != kNatSuccess)
        Run(kNatNegotiationFailed);
}

// 0x00540750
void LobbyMan::Disconnect()
{
    PushInternetHeap();
    m_thinkTask->Remove();
    m_natTask->Remove();
    m_hostingTask->Remove();
    if (m_peer)
        Sdk(0x005FD130, m_peer); // the peer shut down
    m_peer = nullptr;
    PopHeap();
}

// 0x00540AB0
unsigned long __stdcall LobbyMan::ConnectThread(void* lobbyData)
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_internetBottomUpHeap);
    Manager* manager = NetManager();
    char nick[128];
    std::strcpy(nick, manager->GetNetworkPreferences()->GetPreferenceString(kNetworkId, kUiString));
    auto* lobby = static_cast<LobbyMan*>(lobbyData);
    lobby->m_connecting = true;
    auto nickError = &LobbyMan::NickErrorCallback;
    auto connected = &LobbyMan::ConnectCallback;
    Sdk(0x005FD580, lobby->m_peer, static_cast<const char*>(nick), Field<int>(manager->m_buddyMan, 4) /* its profile */,
        nickError, connected, lobbyData, 0); // peer connect (not blocking)
    Mem::Manager::Get()->PopContext();
    return 0;
}

// 0x00540C90
void __cdecl LobbyMan::NatNegotiateCallback(void*, int cookie, void*)
{
    Tsk::Stack* tasks = LogicTasks();
    Manager* manager = NetManager();
    if (static_cast<LobbyMan*>(manager->m_lobbyMan)->m_negotiations != 0)
        return; // (one at a time)
    Net::App* server = manager->m_server;
    PushInternetHeap();
    if (NatBegin(server->Socket(), cookie, 0, kNoProgress, &LobbyMan::NatNegotiationCompleted, cookie)) {
        NatCancel(cookie);
        NatThink();
        PopHeap();
        return;
    }
    auto* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    if (lobby->m_negotiations == 0)
        tasks->AddTask(lobby->m_natTask);
    ++lobby->m_negotiations;
    PopHeap();
}

} // namespace GameNet

REPLACE(0x0053F570, GameNet::LobbyMan::DisconnectedCallback);
REPLACE(0x0053F6C0, GameNet::LobbyMan::QueuePacket);
REPLACE(0x0053FB80, GameNet::LobbyMan::PlayerKeyCallback);
REPLACE(0x0053FDA0, GameNet::LobbyMan::PublicAddressCallback);
REPLACE(0x0053FEE0, GameNet::LobbyMan::NickErrorCallback);
REPLACE(0x0053FFC0, GameNet::LobbyMan::ConnectCallback);
REPLACE(0x00540040, GameNet::LobbyMan::ThinkTask);
REPLACE(0x005400B0, GameNet::LobbyMan::NatNegotiationTask);
REPLACE(0x005400E0, GameNet::LobbyMan::QueryTask);
REPLACE(0x00540220, &GameNet::LobbyMan::StartHosting);
REPLACE(0x00540300, GameNet::LobbyMan::NatNegotiationCompleted);
REPLACE(0x00540750, &GameNet::LobbyMan::Disconnect);
REPLACE(0x00540AB0, GameNet::LobbyMan::ConnectThread);
REPLACE(0x00540C90, GameNet::LobbyMan::NatNegotiateCallback);
