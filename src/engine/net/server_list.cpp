// The network manager's server list and the online service's downloads (see server_list.h).

#include "engine/net/server_list.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamespy.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/net_app.h"
#include "engine/net/net_manager.h"
#include "engine/net/player_info.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/assets/resource_registry.h"
#include "engine/gfx/texture_cfuncs.h"
#include "engine/net/gamenet_lists.h"
#include "engine/core/stubs.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/score_list.h"
#include "engine/net/stats_man.h"

#pragma warning(disable : 4996) // (the original's sprintf / strncpy, kept)

namespace OnlinePlay {

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

static VtableSlot kServerVtable_slots[] = {&HostListing::Teardown};
void** const kServerVtable = reinterpret_cast<void**>(kServerVtable_slots); // HostListing

// The manager's fields not named in gamenet_manager.h yet.
constexpr uint32_t kServiceTask = 0xE0;     // Scheduling::SchedulerTask*: the online service task
constexpr uint32_t kMatchTask = 0xF0;       // Scheduling::SchedulerTask*: the server-list client's task
constexpr uint32_t kServiceState = 0x1B8;
constexpr uint32_t kNextServiceState = 0x1BC;
constexpr uint32_t kServers = 0x158;        // ListKit::Foremost<HostListing>: the listed servers
constexpr uint32_t kWaitingServers = 0x16C; // ListKit::Foremost<HostListing>: found, not shown yet
constexpr uint32_t kNewsBuffer = 0x99F4;
constexpr uint32_t kNewsText = 0xFDF4;
constexpr uint32_t kNewsRest = 0xFDF8;
constexpr uint32_t kNewsRestSize = 0xFDFC;
constexpr uint32_t kRequest = 0xFE00;
constexpr uint32_t kRequestStart = 0xFE04;
constexpr uint32_t kNewsOk = 0xFE09;

Hub* TheManager() { return g_onlineHub; }
MemSys::Hub* RamStats() { return MemSys::Hub::Pull(); }
Scheduling::PileUp* LogicTasks() { return reinterpret_cast<Scheduling::PileUp*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 0x04); }
RoomManager* Lobby(Hub* manager) { return static_cast<RoomManager*>(manager->m_lobbyService); }

uint32_t g_lastRefresh;
bool g_showPending;
uint32_t g_listClock;

uint32_t& LastRefresh() { return g_lastRefresh; }
bool& ShowPending() { return g_showPending; }
uint32_t& ListClock() { return g_listClock; }

// Walking a list as the original does: a node whose data is -1 is the head (checked before null).
MemSys::ListNode* HeadOfSceneList(MemSys::ListNode* head)
{
    MemSys::ListNode* node = head->next;
    return node->data == reinterpret_cast<void*>(-1) ? nullptr : node;
}
MemSys::ListNode* NextNode(MemSys::ListNode* node)
{
    MemSys::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
template <typename T>
T* DataOf(MemSys::ListNode* node)
{
    return node ? static_cast<T*>(node->data) : nullptr;
}
MemSys::ListNode* Foremost(Hub* manager, uint32_t offset) { return &Prop<MemSys::ListNode>(manager, offset); }

// The nodes counted (the original's inlined count: data -1 checked before null).
int TotalOf(MemSys::ListNode* head)
{
    int count = 0;
    for (MemSys::ListNode* node = head->next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
        ++count;
    return count;
}

void DeleteAll(MemSys::ListNode* head)
{
    MemSys::ListNode* node = HeadOfSceneList(head);
    auto* item = DataOf<MemSys::ListNode>(node);
    while (item) {
        node = node ? NextNode(node) : nullptr;
        auto* following = DataOf<MemSys::ListNode>(node);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(item->vtable[0])(item, 1);
        item = following;
    }
}

void UnlinkNode(MemSys::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}
void Append(MemSys::ListNode* head, MemSys::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// A server's id in the menu and in FetchHost: its SBServer on the internet, itself on the LAN.
uint32_t ServerId(Hub* manager, HostListing* server)
{
    return manager->InInternetMode() ? reinterpret_cast<uint32_t>(server->m_sbServer) : reinterpret_cast<uint32_t>(server);
}

// The waiting servers moved to the list (and the menu), after the "none found" message.
void MoveWaitingServers(Hub* manager)
{
    manager->ShowNoServersFound(true);
    MemSys::ListNode* node = HeadOfSceneList(Foremost(manager, kWaitingServers));
    auto* server = DataOf<HostListing>(node);
    while (server) {
        node = NextNode(node);
        auto* following = DataOf<HostListing>(node);
        UnlinkNode(server);
        MemSys::ListNode* list = Foremost(manager, kServers);
        manager->ShowServerInMenu(server, TotalOf(list));
        Append(list, server);
        server = following;
    }
}

} // namespace

// --- HostListing ---

// 0x00545F60
HostListing* HostListing::MakeNew()
{
    data = this;
    priority = 0;
    next = this;
    prev = this;
    vtable = kServerVtable;
    m_participants.data = reinterpret_cast<void*>(-1);
    m_participants.priority = 0;
    m_participants.next = &m_participants;
    m_participants.prev = &m_participants;
    m_participants.vtable = kPlayerHeadVtable;
    m_inMenu = false;
    m_chosen = false;
    m_ping = 0;
    std::sprintf(m_level, "None");
    std::sprintf(m_modeSel, "None");
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
void HostListing::DestroyBody()
{
    vtable = kServerVtable;
    ClearPlayers();
    m_participants.vtable = kPlayerNodeVtable;
    UnlinkNode(&m_participants);
    vtable = kServerNodeVtable;
    UnlinkNode(this);
}

// 0x00547F70
HostListing* HostListing::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x00545790
void HostListing::EnrollPlayer(const char* name, int rating)
{
    RamStats()->EnterHeapScope(TheManager()->InInternetMode() ? static_cast<MemSys::MemSource*>(RamStats()->m_internetBottomUpHeap)
                                                         : static_cast<MemSys::MemSource*>(RamStats()->m_networkHeap));
    void* block = MemSys::AllocZeroed(sizeof(PlayerRecord));
    PlayerRecord* player = block ? static_cast<PlayerRecord*>(block)->MakeNew(0) : nullptr;
    player->m_skillPoints = rating;
    std::strncpy(player->m_label, name, 15);
    player->m_label[15] = '\0';
    Append(&m_participants, player);
    RamStats()->LeaveHeapScope();
}

// 0x005465A0
void HostListing::ClearPlayers()
{
    Hub* manager = TheManager();
    MemSys::ListNode* node = HeadOfSceneList(&m_participants);
    auto* player = DataOf<PlayerRecord>(node);
    while (player) {
        node = NextNode(node);
        auto* following = DataOf<PlayerRecord>(node);
        manager->RemovePlayerRecord(reinterpret_cast<uint8_t*>(player)); // (the player destroyed)
        player = following;
    }
}

// 0x005466D0 / 0x00546730
const char* HostListing::GetEntrantName(int index)
{
    for (MemSys::ListNode* node = HeadOfSceneList(&m_participants); node; node = NextNode(node)) {
        auto* player = static_cast<PlayerRecord*>(node->data);
        if (!player)
            return nullptr;
        if (index == 0)
            return player->m_label;
        --index;
    }
    return nullptr;
}
int HostListing::GetEntrantRank(int index)
{
    for (MemSys::ListNode* node = HeadOfSceneList(&m_participants); node; node = NextNode(node)) {
        auto* player = static_cast<PlayerRecord*>(node->data);
        if (!player)
            return 0;
        if (index == 0)
            return player->m_skillPoints;
        --index;
    }
    return 0;
}

// 0x005451D0 / 0x00545460
static VtableSlot kServerSearchVtable_slots[] = {&OnlinePlay::ServerSearch::Teardown};
void** const kServerSearchVtable = reinterpret_cast<void**>(kServerSearchVtable_slots);

void ServerSearch::DestroyBody() { m_vtable = kServerSearchVtable; }
ServerSearch* ServerSearch::Teardown(unsigned int flags)
{
    m_vtable = kServerSearchVtable;
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// --- The manager's server list ---

// 0x005451B0 / 0x005451C0
void Hub::SetNextServiceState(int state) { Prop<int>(this, kNextServiceState) = state; }
int Hub::GetServiceState() { return Prop<int>(this, kServiceState); }

uint32_t g_newsMessageCount;

// 0x005453E0
void Hub::SetServiceState(int state)
{
    Prop<int>(this, kServiceState) = state;
    if (state == kStateWake) {
        LogicTasks()->QueueJob(Prop<Scheduling::SchedulerTask*>(this, kServiceTask));
        SetServiceState(Prop<int>(this, kNextServiceState));
        g_newsMessageCount = 0;
    } else if (state == kStateStopped) {
        Prop<Scheduling::SchedulerTask*>(this, kServiceTask)->DropOut();
    }
}

// 0x005451E0
void Hub::UpdateHostingStatus()
{
    NetLink::Hub* net = NetLink::g_netCore;
    RamStats()->EnterHeapScope(RamStats()->m_internetTopDownHeap);
    static_cast<RoomManager*>(m_lobbyService)->StartHosting();
    if (Prop<bool>(m_presenceService, 0x10)) { // (0x005480B0: logged in to presence)
        const char* serverName;
        GetOnlineSettings()->FetchSetting(kServerName)->FetchText(kUiString, &serverName, 1);
        char location[1024];
        RoomManager* lobby = Lobby(this);
        if (lobby->m_remote) {
            uint32_t privateIp = GameSpy::PeerGetPrivateIp(lobby->m_remote); // (the peer's private address)
            std::sprintf(location, "10thug211:%d:%d:%d:%s (%s)", net->GetLocalAddress(), privateIp, kPort, serverName,
                         lobby->m_roomName);
        } else {
            std::sprintf(location, "10thug211:0:0:0:%s (%s)", serverName, lobby->m_roomName);
        }
        static_cast<FriendsManager*>(m_presenceService)->ChangeStanding(2, Scripts::FetchText(kHostingStatus), location);
    }
    RamStats()->LeaveHeapScope();
}

// 0x00545300
bool Hub::StartServerList()
{
    Scheduling::PileUp* logic = LogicTasks();
    if (!IsLocalNetworkPlay())
        return true;
    DestroyMatchClient();
    RamStats()->EnterHeapScope(RamStats()->m_networkHeap);
    NetLink::AppHost* client = StartMatchmakingClient();
    RamStats()->LeaveHeapScope();
    if (!client)
        return false;
    Scheduling::SchedulerTask* task = Prop<Scheduling::SchedulerTask*>(this, kMatchTask);
    if (!task->IsOnTaskList())
        logic->QueueJob(task);
    return true;
}

// 0x00545370
void Hub::StopServerList()
{
    if (InInternetMode()) {
        RamStats()->EnterHeapScope(RamStats()->m_internetBottomUpHeap);
        if (Lobby(this)->m_remote)
            GameSpy::PeerStopListingGames(Lobby(this)->m_remote); // (the browsing stopped)
        RamStats()->LeaveHeapScope();
        return;
    }
    if (NetLink::AppHost* client = GetMatchmakingClient())
        client->m_dispatcher.TearDownState();
    Prop<Scheduling::SchedulerTask*>(this, kMatchTask)->DropOut();
}

// 0x005456B0
void Hub::ShowNoServersFound(bool refocus)
{
    Scripts::CStruct* params = NewParams();
    if (refocus)
        params->PutNameKey(kRefocus, kRefocus);
    Run(kDestroyServerChildren, params);
    Run(kNoServersFound, params);
    DeleteParams(params);
}

// 0x00546010
void Hub::ClearChosenServers()
{
    for (MemSys::ListNode* node = HeadOfSceneList(Foremost(this, kServers)); node; node = NextNode(node)) {
        auto* server = static_cast<HostListing*>(node->data);
        if (!server)
            break;
        server->m_chosen = false;
    }
}

// 0x00546070
int Hub::NumServersInMenu()
{
    int count = 0;
    for (MemSys::ListNode* node = HeadOfSceneList(Foremost(this, kServers)); node; node = NextNode(node)) {
        auto* server = static_cast<HostListing*>(node->data);
        if (!server)
            break;
        if (server->m_inMenu)
            ++count;
    }
    return count;
}

// 0x005460D0
void Hub::ShowServerInMenu(void* item, int index)
{
    (void)index;
    auto* server = static_cast<HostListing*>(item);
    RamStats()->EnterHeapScope(RamStats()->m_frontEndHeap);
    if (NumServersInMenu() == 0)
        Run(kDestroyServerChildren);
    Run(kPrepareServerMenu);
    Scripts::CStruct* params = NewParams();
    params->AppendWord(kText, server->m_label);
    params->AppendWord(kMode, server->m_modeSel);
    DebugOut::Printf("The game mode is: %s\n", server->m_modeSel);
    uint32_t id = ServerId(this, server);
    params->PutNameKey(kId, id);
    params->PutNameKey(kParent, kServerListMenu);
    params->PutNameKey(kPadChooseScript, kChooseSelectedServer);
    params->PutNameKey(kFocusScript, kDescribeSelectedServer);
    Scripts::CStruct* idParams = NewParams();
    idParams->PutNameKey(kId, id);
    params->PutSubStruct(kPadChooseParams, idParams);
    params->PutSubStruct(kFocusParams, idParams);
    Run(kAddMenuItem, params);
    server->m_inMenu = true;
    DeleteParams(params);
    DeleteParams(idParams);
    RamStats()->LeaveHeapScope();
}

// 0x005462F0
void Hub::EmptyServerListing()
{
    DeleteAll(Foremost(this, kWaitingServers));
    DeleteAll(Foremost(this, kServers));
    if (IsLocalNetworkPlay())
        DestroyMatchClient();
}

// 0x00546400
bool Hub::HasServer(const char* name, uint32_t ip)
{
    for (uint32_t list : {kServers, kWaitingServers}) {
        for (MemSys::ListNode* node = HeadOfSceneList(Foremost(this, list)); node; node = NextNode(node)) {
            auto* server = static_cast<HostListing*>(node->data);
            if (!server)
                break;
            if (server->m_remoteIp == ip && _stricmp(server->m_label, name) == 0)
                return true;
        }
    }
    return false;
}

// 0x005464D0
void* Hub::FetchHost(uint32_t id)
{
    for (MemSys::ListNode* node = HeadOfSceneList(Foremost(this, kServers)); node; node = NextNode(node)) {
        auto* server = static_cast<HostListing*>(node->data);
        if (!server)
            return nullptr;
        if (id == ServerId(this, server))
            return server;
    }
    return nullptr;
}

// 0x00547320
void Hub::ShowFoundServers()
{
    MoveWaitingServers(this);
    Scripts::LaunchScript("update_lobby_server_list", nullptr, nullptr, false);
}

// 0x00547E80
void Hub::RefreshServerList(bool force)
{
    if (!force && FrameClock::GetTime() - LastRefresh() < kRefreshInterval)
        return;
    LastRefresh() = FrameClock::GetTime();
    ShowPending() = true;
    RamStats()->EnterHeapScope(RamStats()->m_networkHeap);
    StopServerList();
    EmptyServerListing();
    StartServerList();
    if (!InInternetMode()) {
        ScanLocalNetwork();
        RamStats()->LeaveHeapScope();
        return;
    }
    Hub* manager = TheManager();
    uint8_t keys[4] = {8, 10, 4, 6}; // (in the original: over its argument's slot)
    RamStats()->EnterHeapScope(RamStats()->m_internetBottomUpHeap);
    static_cast<RoomManager*>(manager->m_lobbyService)->ClearLostConnection();
    GameSpy::PeerStartListingGames(Lobby(this)->m_remote, keys, 4, nullptr, CodeAddress(ListingGamesCallback),
                                   this); // (the games listed)
    RamStats()->LeaveHeapScope();
    RamStats()->LeaveHeapScope();
}

// 0x00547440
void __cdecl ServerListTask(void* task)
{
    Hub* manager = Prop<Hub*>(task, 0x1C);
    if (FrameClock::GetTime() - ListClock() > kListClock) {
        ListClock() = FrameClock::GetTime();
        return;
    }
    if (!ShowPending() || FrameClock::GetTime() - LastRefresh() <= kShowDelay)
        return;
    MoveWaitingServers(manager);
    Run(kUpdateServerList);
    ShowPending() = false;
}

// --- The online service's downloads ---

// 0x00545540
unsigned long __stdcall NewsThread(void* argument)
{
    auto* manager = static_cast<Hub*>(argument);
    RamStats()->EnterHeapScope(RamStats()->m_internetBottomUpHeap);
    GameSpy::Msleep(100);
    Prop<int>(manager, kServiceState) = kStateNews;
    Prop<uint32_t>(manager, kRequestStart) = FrameClock::GetTime();
    uint8_t* buffer = &Prop<uint8_t>(manager, kNewsBuffer);
    std::memset(buffer, 0, 0x1900 * 4);
    char url[256];
    std::sprintf(url, ScriptCommands::g_textureFlagCe441 ? "http://www.thug2online.com/testmotd.dat"
                                                     : "http://www.thug2online.com/motd.dat");
    Prop<int>(manager, kRequest) =
        GameSpy::HttpGetEx(url, nullptr, reinterpret_cast<char*>(buffer), static_cast<int>(kNewsBufferSize), nullptr, 0, 0,
                           CodeAddress(Stubs::DoNothing), &NewsCallback, manager); // (the download started)
    RamStats()->LeaveHeapScope();
    return 0;
}

// 0x00545620
unsigned long __stdcall MotdThread(void* argument)
{
    auto* manager = static_cast<Hub*>(argument);
    const char* uniqueId = "123456789";
    RamStats()->EnterHeapScope(RamStats()->m_internetBottomUpHeap);
    manager->GetOnlineSettings()->FetchSetting(kUniqueId)->FetchText(kUiString, &uniqueId, 0); // (not used)
    Prop<uint32_t>(manager, kRequestStart) = FrameClock::GetTime();
    Prop<int>(manager, kRequest) = GameSpy::TrackUsage(0, 10368, "1.0", 0, 0); // (the usage report: the SDK's MOTD request)
    Prop<int>(manager, kServiceState) = kStateMotd;
    RamStats()->LeaveHeapScope();
    return 0;
}

// 0x00545490
int __cdecl NewsCallback(void* request, int result, uint8_t* buffer, uint32_t sizeLow, int32_t sizeHigh, void* param)
{
    (void)request;
    (void)param;
    Hub* manager = TheManager();
    if (result != 0) {
        Prop<int>(manager, kServiceState) = kStateNewsFailed;
        return 1;
    }
    int64_t size = (static_cast<int64_t>(sizeHigh) << 32) | sizeLow;
    for (int i = 0; static_cast<int64_t>(i) < size; ++i)
        buffer[i] ^= 0x6D;
    uint32_t checksum = *reinterpret_cast<uint32_t*>(buffer);
    int remaining = *reinterpret_cast<int*>(buffer + 4);
    uint8_t* text = buffer + 8;
    if (checksum != Checksums::GenerateCRCCaseless(reinterpret_cast<const char*>(text), remaining)) {
        Prop<int>(manager, kServiceState) = kStateNewsFailed;
        return 1;
    }
    Prop<uint8_t*>(manager, kNewsText) = text;
    int length = (static_cast<int>(std::strlen(reinterpret_cast<const char*>(text))) + 4) & ~3; // (with its NUL, a word)
    Prop<int>(manager, kNewsRestSize) = remaining - length;
    Prop<uint8_t*>(manager, kNewsRest) = text + length;
    Prop<bool>(manager, kNewsOk) = true;
    Prop<int>(manager, kServiceState) = kStateNewsDone;
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
bool __cdecl CFunc_PostGame(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    (void)script;
    Hub* manager = TheManager();
    if (!Lobby(manager)->m_hosting)
        manager->UpdateHostingStatus();
    return true;
}

// 0x00545F20: num_servers = the listed servers.
bool __cdecl CFunc_NumServersInLobby(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    script->m_args->PutInt(kNumServers, TotalOf(Foremost(TheManager(), kServers)));
    return true;
}

} // namespace OnlinePlay

using OnlinePlay::Hub;
using OnlinePlay::HostListing;
