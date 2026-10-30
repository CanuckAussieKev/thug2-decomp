// The network manager's server list and the online service's downloads (see server_list.h).

#include "game/net/server_list.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/net_app.h"
#include "game/net/net_manager.h"
#include "game/net/player_info.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/ass/assman.h"
#include "game/gfx/texture_cfuncs.h"

#pragma warning(disable : 4996) // (the original's sprintf / strncpy, kept)

namespace GameNet {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kNumServers = 0x757610E1;             // num_servers
constexpr uint32_t kRefocus = 0xE5BFC29E;                // refocus
constexpr uint32_t kDestroyServerChildren = 0x152AAD5D;  // destroy_server_menu_children
constexpr uint32_t kNoServersFound = 0x79EE1731;         // add_no_servers_found_message
constexpr uint32_t kPrepareServerMenu = 0x56E20B51;      // prepare_server_menu_for_new_children
constexpr uint32_t kText = 0xC4745838;                   // text
constexpr uint32_t kMode = 0x6835B854;                   // Mode
constexpr uint32_t kId = 0x40C698AF;                     // id
constexpr uint32_t kParent = 0xC2719FB0;                 // parent
constexpr uint32_t kServerListMenu = 0x052140A3;         // server_list_menu
constexpr uint32_t kPadChooseScript = 0x2B0CFCA1;        // pad_choose_script
constexpr uint32_t kChooseSelectedServer = 0x822E996A;   // choose_selected_server
constexpr uint32_t kFocusScript = 0x5A8C82DB;            // focus_script
constexpr uint32_t kDescribeSelectedServer = 0x596BAB63; // describe_selected_server
constexpr uint32_t kPadChooseParams = 0xB8437568;        // pad_choose_params
constexpr uint32_t kFocusParams = 0xC9C30B12;            // focus_params
constexpr uint32_t kAddMenuItem = 0x1499767F;            // server_list_menu_add_item
constexpr uint32_t kServerName = 0x2A5CF019;             // server_name (a network preference)
constexpr uint32_t kUiString = 0x96875C0F;               // ui_string
constexpr uint32_t kUniqueId = 0x1C397CBC;               // unique_id (a network preference)
constexpr uint32_t kHostingStatus = 0xC5A16CE1;          // homie_status_hosting
constexpr uint32_t kUpdateServerList = 0x40C621E4;       // update_lobby_server_list

constexpr int kStateWake = 1;
constexpr int kStateStopped = 2;
constexpr int kStateNews = 7;
constexpr int kStateNewsDone = 8;
constexpr int kStateMotd = 9;
constexpr int kStateNewsFailed = 11;
constexpr int kPort = 0x1421; // 5153
constexpr uint32_t kRefreshInterval = 5000;   // ms
constexpr uint32_t kListClock = 3000;         // ms
constexpr uint32_t kShowDelay = 500;          // ms
constexpr uint32_t kNewsBufferSize = 0x6400;  // bytes

inline void** const kServerVtable = reinterpret_cast<void**>(0x0064CDFC);       // ServerInfo
inline void** const kServerBaseVtable = reinterpret_cast<void**>(0x0064C9EC);   // Lst::Node<ServerInfo>
inline void** const kPlayerHeadVtable = reinterpret_cast<void**>(0x0064C9F0);   // Lst::Head<PlayerInfo>
inline void** const kPlayerNodeVtable = reinterpret_cast<void**>(0x0064C9E8);   // Lst::Node<PlayerInfo>
inline void** const kServerSearchVtable = reinterpret_cast<void**>(0x0064CD70); // Lst::Search<ServerInfo>

// The manager's fields not named in gamenet_manager.h yet.
constexpr uint32_t kServiceTask = 0xE0;     // Tsk::BaseTask*: the online service task
constexpr uint32_t kMatchTask = 0xF0;       // Tsk::BaseTask*: the server-list client's task
constexpr uint32_t kServiceState = 0x1B8;
constexpr uint32_t kNextServiceState = 0x1BC;
constexpr uint32_t kServers = 0x158;        // Lst::Head<ServerInfo>: the listed servers
constexpr uint32_t kWaitingServers = 0x16C; // Lst::Head<ServerInfo>: found, not shown yet
constexpr uint32_t kNewsBuffer = 0x99F4;
constexpr uint32_t kNewsText = 0xFDF4;
constexpr uint32_t kNewsRest = 0xFDF8;
constexpr uint32_t kNewsRestSize = 0xFDFC;
constexpr uint32_t kRequest = 0xFE00;
constexpr uint32_t kRequestStart = 0xFE04;
constexpr uint32_t kNewsOk = 0xFE09;

Manager* TheManager() { return g_gameNetManager; }
Mem::Manager* Memory() { return Mem::Manager::Get(); }
Tsk::Stack* LogicTasks() { return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager) + 0x04); }
LobbyMan* Lobby(Manager* manager) { return static_cast<LobbyMan*>(manager->m_lobbyMan); }

GAME_GLOBAL(uint32_t, g_lastRefresh, 0x007CE448);
GAME_GLOBAL(bool, g_showPending, 0x007CE44C);
GAME_GLOBAL(uint32_t, g_listClock, 0x007CE450);

uint32_t& LastRefresh() { return g_lastRefresh; }
bool& ShowPending() { return g_showPending; }
uint32_t& ListClock() { return g_listClock; }

template <typename R = void, typename... A>
R Sdk(uint32_t address, A... args)
{
    return GameFunction<R(__cdecl*)(A...)>(address)(args...);
}

// Walking a list as the original does: a node whose data is -1 is the head (checked before null).
Mem::ListNode* FirstNode(Mem::ListNode* head)
{
    Mem::ListNode* node = head->next;
    return node->data == reinterpret_cast<void*>(-1) ? nullptr : node;
}
Mem::ListNode* NextNode(Mem::ListNode* node)
{
    Mem::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
template <typename T>
T* DataOf(Mem::ListNode* node)
{
    return node ? static_cast<T*>(node->data) : nullptr;
}
Mem::ListNode* Head(Manager* manager, uint32_t offset) { return &Field<Mem::ListNode>(manager, offset); }

// The nodes counted (the original's inlined count: data -1 checked before null).
int Count(Mem::ListNode* head)
{
    int count = 0;
    for (Mem::ListNode* node = head->next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
        ++count;
    return count;
}

void DeleteAll(Mem::ListNode* head)
{
    Mem::ListNode* node = FirstNode(head);
    auto* item = DataOf<Mem::ListNode>(node);
    while (item) {
        node = node ? NextNode(node) : nullptr;
        auto* following = DataOf<Mem::ListNode>(node);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(item->vtable[0])(item, 1);
        item = following;
    }
}

void UnlinkNode(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}
void Append(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// A server's id in the menu and in GetServer: its SBServer on the internet, itself on the LAN.
uint32_t ServerId(Manager* manager, ServerInfo* server)
{
    return manager->InInternetMode() ? reinterpret_cast<uint32_t>(server->m_sbServer) : reinterpret_cast<uint32_t>(server);
}

// The waiting servers moved to the list (and the menu), after the "none found" message.
void MoveWaitingServers(Manager* manager)
{
    manager->ShowNoServersFound(true);
    Mem::ListNode* node = FirstNode(Head(manager, kWaitingServers));
    auto* server = DataOf<ServerInfo>(node);
    while (server) {
        node = NextNode(node);
        auto* following = DataOf<ServerInfo>(node);
        UnlinkNode(server);
        Mem::ListNode* list = Head(manager, kServers);
        manager->AddServerToMenu(server, Count(list));
        Append(list, server);
        server = following;
    }
}

} // namespace

// --- ServerInfo ---

// 0x00545F60
ServerInfo* ServerInfo::Construct()
{
    data = this;
    priority = 0;
    next = this;
    prev = this;
    vtable = kServerVtable;
    m_players.data = reinterpret_cast<void*>(-1);
    m_players.priority = 0;
    m_players.next = &m_players;
    m_players.prev = &m_players;
    m_players.vtable = kPlayerHeadVtable;
    m_inMenu = false;
    m_chosen = false;
    m_ping = 0;
    std::sprintf(m_level, "None");
    std::sprintf(m_mode, "None");
    m_numPlayers = 1;
    m_maxObservers = 1;
    m_canReach = true;
    m_numObservers = 0;
    m_hostMode = 0;
    m_ranked = false;
    m_started = false;
    m_inList = false;
    m_detailed = false;
    m_sbServer = nullptr;
    m_maxPlayers = 8;
    m_skillLevel = 2;
    return this;
}

// 0x00546640
void ServerInfo::DestroyBody()
{
    vtable = kServerVtable;
    ClearPlayers();
    m_players.vtable = kPlayerNodeVtable;
    UnlinkNode(&m_players);
    vtable = kServerBaseVtable;
    UnlinkNode(this);
}

// 0x00547F70
ServerInfo* ServerInfo::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x00545790
void ServerInfo::AddPlayer(const char* name, int rating)
{
    Memory()->PushContext(TheManager()->InInternetMode() ? static_cast<Mem::Allocator*>(Memory()->m_internetBottomUpHeap)
                                                         : static_cast<Mem::Allocator*>(Memory()->m_networkHeap));
    void* block = Mem::Calloc(sizeof(PlayerInfo));
    PlayerInfo* player = block ? static_cast<PlayerInfo*>(block)->Construct(0) : nullptr;
    player->m_rating = rating;
    std::strncpy(player->m_name, name, 15);
    player->m_name[15] = '\0';
    Append(&m_players, player);
    Memory()->PopContext();
}

// 0x005465A0
void ServerInfo::ClearPlayers()
{
    Manager* manager = TheManager();
    Mem::ListNode* node = FirstNode(&m_players);
    auto* player = DataOf<PlayerInfo>(node);
    while (player) {
        node = NextNode(node);
        auto* following = DataOf<PlayerInfo>(node);
        manager->DestroyPlayer(reinterpret_cast<uint8_t*>(player)); // (the player destroyed)
        player = following;
    }
}

// 0x005466D0 / 0x00546730
const char* ServerInfo::GetPlayerName(int index)
{
    for (Mem::ListNode* node = FirstNode(&m_players); node; node = NextNode(node)) {
        auto* player = static_cast<PlayerInfo*>(node->data);
        if (!player)
            return nullptr;
        if (index == 0)
            return player->m_name;
        --index;
    }
    return nullptr;
}
int ServerInfo::GetPlayerRating(int index)
{
    for (Mem::ListNode* node = FirstNode(&m_players); node; node = NextNode(node)) {
        auto* player = static_cast<PlayerInfo*>(node->data);
        if (!player)
            return 0;
        if (index == 0)
            return player->m_rating;
        --index;
    }
    return 0;
}

// 0x005451D0 / 0x00545460
void ServerSearch::DestroyBody() { m_vtable = kServerSearchVtable; }
ServerSearch* ServerSearch::Destroy(unsigned int flags)
{
    m_vtable = kServerSearchVtable;
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// --- The manager's server list ---

// 0x005451B0 / 0x005451C0
void Manager::SetNextServiceState(int state) { Field<int>(this, kNextServiceState) = state; }
int Manager::GetServiceState() { return Field<int>(this, kServiceState); }

GAME_GLOBAL(uint32_t, g_newsMessageCount, 0x007CE444);

// 0x005453E0
void Manager::SetServiceState(int state)
{
    Field<int>(this, kServiceState) = state;
    if (state == kStateWake) {
        LogicTasks()->AddTask(Field<Tsk::BaseTask*>(this, kServiceTask));
        SetServiceState(Field<int>(this, kNextServiceState));
        g_newsMessageCount = 0;
    } else if (state == kStateStopped) {
        Field<Tsk::BaseTask*>(this, kServiceTask)->Remove();
    }
}

// 0x005451E0
void Manager::UpdateHostingStatus()
{
    Net::Manager* net = Net::g_netManager;
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    static_cast<LobbyMan*>(m_lobbyMan)->StartHosting();
    if (Field<bool>(m_buddyMan, 0x10)) { // (0x005480B0: logged in to presence)
        const char* serverName;
        GetNetworkPreferences()->GetPreference(kServerName)->GetString(kUiString, &serverName, 1);
        char location[1024];
        LobbyMan* lobby = Lobby(this);
        if (lobby->m_peer) {
            uint32_t publicIp = Sdk<uint32_t>(0x005FC950, lobby->m_peer); // (the peer's public address)
            std::sprintf(location, "10thug211:%d:%d:%d:%s (%s)", net->GetLocalAddress(), publicIp, kPort, serverName,
                         lobby->m_roomName);
        } else {
            std::sprintf(location, "10thug211:0:0:0:%s (%s)", serverName, lobby->m_roomName);
        }
        static_cast<BuddyMan*>(m_buddyMan)->SetStatus(2, Script::GetString(kHostingStatus), location);
    }
    Memory()->PopContext();
}

// 0x00545300
bool Manager::StartServerList()
{
    Tsk::Stack* logic = LogicTasks();
    if (!InLanMode())
        return true;
    DestroyMatchClient();
    Memory()->PushContext(Memory()->m_networkHeap);
    Net::App* client = SpawnMatchClient();
    Memory()->PopContext();
    if (!client)
        return false;
    Tsk::BaseTask* task = Field<Tsk::BaseTask*>(this, kMatchTask);
    if (!task->InList())
        logic->AddTask(task);
    return true;
}

// 0x00545370
void Manager::StopServerList()
{
    if (InInternetMode()) {
        Memory()->PushContext(Memory()->m_internetBottomUpHeap);
        if (Lobby(this)->m_peer)
            Sdk(0x005FCA40, Lobby(this)->m_peer); // (the browsing stopped)
        Memory()->PopContext();
        return;
    }
    if (Net::App* client = GetMatchClient())
        client->m_dispatcher.Deinit();
    Field<Tsk::BaseTask*>(this, kMatchTask)->Remove();
}

// 0x005456B0
void Manager::ShowNoServersFound(bool refocus)
{
    Script::CStruct* params = NewParams();
    if (refocus)
        params->AddChecksum(kRefocus, kRefocus);
    Run(kDestroyServerChildren, params);
    Run(kNoServersFound, params);
    DeleteParams(params);
}

// 0x00546010
void Manager::ClearChosenServers()
{
    for (Mem::ListNode* node = FirstNode(Head(this, kServers)); node; node = NextNode(node)) {
        auto* server = static_cast<ServerInfo*>(node->data);
        if (!server)
            break;
        server->m_chosen = false;
    }
}

// 0x00546070
int Manager::NumServersInMenu()
{
    int count = 0;
    for (Mem::ListNode* node = FirstNode(Head(this, kServers)); node; node = NextNode(node)) {
        auto* server = static_cast<ServerInfo*>(node->data);
        if (!server)
            break;
        if (server->m_inMenu)
            ++count;
    }
    return count;
}

// 0x005460D0
void Manager::AddServerToMenu(void* item, int index)
{
    (void)index;
    auto* server = static_cast<ServerInfo*>(item);
    Memory()->PushContext(Memory()->m_frontEndHeap);
    if (NumServersInMenu() == 0)
        Run(kDestroyServerChildren);
    Run(kPrepareServerMenu);
    Script::CStruct* params = NewParams();
    params->AddString(kText, server->m_name);
    params->AddString(kMode, server->m_mode);
    Dbg::Printf("The game mode is: %s\n", server->m_mode);
    uint32_t id = ServerId(this, server);
    params->AddChecksum(kId, id);
    params->AddChecksum(kParent, kServerListMenu);
    params->AddChecksum(kPadChooseScript, kChooseSelectedServer);
    params->AddChecksum(kFocusScript, kDescribeSelectedServer);
    Script::CStruct* idParams = NewParams();
    idParams->AddChecksum(kId, id);
    params->AddStructure(kPadChooseParams, idParams);
    params->AddStructure(kFocusParams, idParams);
    Run(kAddMenuItem, params);
    server->m_inMenu = true;
    DeleteParams(params);
    DeleteParams(idParams);
    Memory()->PopContext();
}

// 0x005462F0
void Manager::ClearServerList()
{
    DeleteAll(Head(this, kWaitingServers));
    DeleteAll(Head(this, kServers));
    if (InLanMode())
        DestroyMatchClient();
}

// 0x00546400
bool Manager::HasServer(const char* name, uint32_t ip)
{
    for (uint32_t list : {kServers, kWaitingServers}) {
        for (Mem::ListNode* node = FirstNode(Head(this, list)); node; node = NextNode(node)) {
            auto* server = static_cast<ServerInfo*>(node->data);
            if (!server)
                break;
            if (server->m_ip == ip && _stricmp(server->m_name, name) == 0)
                return true;
        }
    }
    return false;
}

// 0x005464D0
void* Manager::GetServer(uint32_t id)
{
    for (Mem::ListNode* node = FirstNode(Head(this, kServers)); node; node = NextNode(node)) {
        auto* server = static_cast<ServerInfo*>(node->data);
        if (!server)
            return nullptr;
        if (id == ServerId(this, server))
            return server;
    }
    return nullptr;
}

// 0x00547320
void Manager::ShowFoundServers()
{
    MoveWaitingServers(this);
    Script::RunScript("update_lobby_server_list", nullptr, nullptr, false);
}

// 0x00547E80
void Manager::RefreshServerList(bool force)
{
    if (!force && Tmr::GetTime() - LastRefresh() < kRefreshInterval)
        return;
    LastRefresh() = Tmr::GetTime();
    ShowPending() = true;
    Memory()->PushContext(Memory()->m_networkHeap);
    StopServerList();
    ClearServerList();
    StartServerList();
    if (!InInternetMode()) {
        FindServersOnLan();
        Memory()->PopContext();
        return;
    }
    Manager* manager = TheManager();
    uint8_t keys[4] = {8, 10, 4, 6}; // (in the original: over its argument's slot)
    Memory()->PushContext(Memory()->m_internetBottomUpHeap);
    static_cast<LobbyMan*>(manager->m_lobbyMan)->ClearLostConnection();
    Sdk(0x005FC9D0, Lobby(this)->m_peer, keys, 4, 0, 0x00547750u, this); // (the server browser updated)
    Memory()->PopContext();
    Memory()->PopContext();
}

// 0x00547440
void __cdecl ServerListTask(void* task)
{
    Manager* manager = Field<Manager*>(task, 0x1C);
    if (Tmr::GetTime() - ListClock() > kListClock) {
        ListClock() = Tmr::GetTime();
        return;
    }
    if (!ShowPending() || Tmr::GetTime() - LastRefresh() <= kShowDelay)
        return;
    MoveWaitingServers(manager);
    Run(kUpdateServerList);
    ShowPending() = false;
}

// --- The online service's downloads ---

// 0x00545540
unsigned long __stdcall NewsThread(void* argument)
{
    auto* manager = static_cast<Manager*>(argument);
    Memory()->PushContext(Memory()->m_internetBottomUpHeap);
    Sdk(0x005F8160, 100); // (GameSpy's set-up)
    Field<int>(manager, kServiceState) = kStateNews;
    Field<uint32_t>(manager, kRequestStart) = Tmr::GetTime();
    uint8_t* buffer = &Field<uint8_t>(manager, kNewsBuffer);
    std::memset(buffer, 0, 0x1900 * 4);
    char url[256];
    std::sprintf(url, CFuncs::g_textureFlagCe441 ? "http://www.thug2online.com/testmotd.dat"
                                                     : "http://www.thug2online.com/motd.dat");
    Field<int>(manager, kRequest) = Sdk<int>(0x005FF8A0, url, 0, buffer, kNewsBufferSize, 0, 0, 0, 0x005480A0u,
                                             &NewsCallback, manager); // (the download started)
    Memory()->PopContext();
    return 0;
}

// 0x00545620
unsigned long __stdcall MotdThread(void* argument)
{
    auto* manager = static_cast<Manager*>(argument);
    const char* uniqueId = "123456789";
    Memory()->PushContext(Memory()->m_internetBottomUpHeap);
    manager->GetNetworkPreferences()->GetPreference(kUniqueId)->GetString(kUiString, &uniqueId, 0); // (not used)
    Field<uint32_t>(manager, kRequestStart) = Tmr::GetTime();
    Field<int>(manager, kRequest) = Sdk<int>(0x005FFCD0, 0, 10368, "1.0", 0, 0); // (the patch check)
    Field<int>(manager, kServiceState) = kStateMotd;
    Memory()->PopContext();
    return 0;
}

// 0x00545490
int __cdecl NewsCallback(void* request, int result, uint8_t* buffer, uint32_t sizeLow, int32_t sizeHigh, void* param)
{
    (void)request;
    (void)param;
    Manager* manager = TheManager();
    if (result != 0) {
        Field<int>(manager, kServiceState) = kStateNewsFailed;
        return 1;
    }
    int64_t size = (static_cast<int64_t>(sizeHigh) << 32) | sizeLow;
    for (int i = 0; static_cast<int64_t>(i) < size; ++i)
        buffer[i] ^= 0x6D;
    uint32_t checksum = *reinterpret_cast<uint32_t*>(buffer);
    int remaining = *reinterpret_cast<int*>(buffer + 4);
    uint8_t* text = buffer + 8;
    if (checksum != Crc::GenerateCRCCaseless(reinterpret_cast<const char*>(text), remaining)) {
        Field<int>(manager, kServiceState) = kStateNewsFailed;
        return 1;
    }
    Field<uint8_t*>(manager, kNewsText) = text;
    int length = (static_cast<int>(std::strlen(reinterpret_cast<const char*>(text))) + 4) & ~3; // (with its NUL, a word)
    Field<int>(manager, kNewsRestSize) = remaining - length;
    Field<uint8_t*>(manager, kNewsRest) = text + length;
    Field<bool>(manager, kNewsOk) = true;
    Field<int>(manager, kServiceState) = kStateNewsDone;
    return 1;
}

// 0x00548040
bool __cdecl IsThug2Location(const char* location)
{
    char copy[256];
    std::strcpy(copy, location);
    return _stricmp(std::strtok(copy, ":"), "10thug211") == 0;
}

// --- The script commands ---

// 0x00545440: PostGame - the hosting status, unless the lobby's +0x139 (0x0053F810).
bool __cdecl CFunc_PostGame(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = TheManager();
    if (!Lobby(manager)->m_hosting)
        manager->UpdateHostingStatus();
    return true;
}

// 0x00545F20: num_servers = the listed servers.
bool __cdecl CFunc_NumServersInLobby(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    script->m_params->AddInteger(kNumServers, Count(Head(TheManager(), kServers)));
    return true;
}

} // namespace GameNet

using GameNet::Manager;
using GameNet::ServerInfo;
REPLACE(0x00545F60, &ServerInfo::Construct);
REPLACE(0x00546640, &ServerInfo::DestroyBody);
REPLACE(0x00547F70, &ServerInfo::Destroy);
REPLACE(0x00545790, &ServerInfo::AddPlayer);
REPLACE(0x005465A0, &ServerInfo::ClearPlayers);
REPLACE(0x005466D0, &ServerInfo::GetPlayerName);
REPLACE(0x00546730, &ServerInfo::GetPlayerRating);
REPLACE(0x005451D0, &GameNet::ServerSearch::DestroyBody);
REPLACE(0x00545460, &GameNet::ServerSearch::Destroy);
REPLACE(0x005451B0, &Manager::SetNextServiceState);
REPLACE(0x005451C0, &Manager::GetServiceState);
REPLACE(0x005453E0, &Manager::SetServiceState);
REPLACE(0x005451E0, &Manager::UpdateHostingStatus);
REPLACE(0x00545300, &Manager::StartServerList);
REPLACE(0x00545370, &Manager::StopServerList);
REPLACE(0x005456B0, &Manager::ShowNoServersFound);
REPLACE(0x00546010, &Manager::ClearChosenServers);
REPLACE(0x00546070, &Manager::NumServersInMenu);
REPLACE(0x005460D0, &Manager::AddServerToMenu);
REPLACE(0x005462F0, &Manager::ClearServerList);
REPLACE(0x00546400, &Manager::HasServer);
REPLACE(0x005464D0, &Manager::GetServer);
REPLACE(0x00547320, &Manager::ShowFoundServers);
REPLACE(0x00547E80, &Manager::RefreshServerList);
REPLACE(0x00547440, GameNet::ServerListTask);
REPLACE(0x00545540, GameNet::NewsThread);   // (not a Ghidra function)
REPLACE(0x00545620, GameNet::MotdThread);   // (not a Ghidra function)
REPLACE(0x00545490, GameNet::NewsCallback); // (not a Ghidra function)
REPLACE(0x00548040, GameNet::IsThug2Location);
REPLACE(0x00545440, GameNet::CFunc_PostGame);
REPLACE(0x00545F20, GameNet::CFunc_NumServersInLobby);
