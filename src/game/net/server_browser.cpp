// The server list's browsing (see server_list.h): the servers' details shown and chosen, the LAN servers' replies
// and GameSpy's game listing callback. GameSpy's server browsing (SBServer) calls are library code, called by
// address; what each returns is said where it's used.

#include <winsock2.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/mth/random.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/net/server_list.h"
#include "game/net/stats_man.h"
#include "game/net/vault_man.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/gfx/anim_blend.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/gfx/loading_bar.h"

#pragma warning(disable : 4996) // (the original's sprintf / strcpy / strncpy, kept)

namespace GameNet {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kId = 0x40C698AF;                    // id
constexpr uint32_t kText = 0xC4745838;                  // text
constexpr uint32_t kName = 0xA1DC81F9;                  // name
constexpr uint32_t kValue = 0xE288A7CB;                 // value
constexpr uint32_t kRank = 0x7786171A;                  // rank
constexpr uint32_t kDestroyDescChildren = 0xDD6DDFD6;   // destroy_server_desc_children
constexpr uint32_t kAddInfoItem = 0x676C4DB8;           // net_game_info_add_item
constexpr uint32_t kUpdatePlayerTitle = 0xC755D58E;     // net_game_info_update_player_title
constexpr uint32_t kAddPlayer = 0x62F4E2E8;             // net_game_info_add_player
constexpr uint32_t kGameInProgress = 0xC7EDAA8E;        // netoptions_str_game_in_progress
constexpr uint32_t kAutoServe = 0xA37CBD43;             // netoptions_str_auto_serve
constexpr uint32_t kFcfs = 0x850E7F29;                  // netoptions_str_fcfs
constexpr uint32_t kTitlePing = 0x7E63A240;             // sort_title_ping
constexpr uint32_t kTitleMode = 0xCC7CD816;             // sort_title_mode
constexpr uint32_t kTitleSkill = 0x39B47254;            // sort_title_skill
constexpr uint32_t kTitleLevel = 0xFD635A30;            // sort_title_level
constexpr uint32_t kTitleRanked = 0xEE56DAAF;           // sort_title_ranked
constexpr uint32_t kYes = 0x47B25601;                   // netoptions_str_yes
constexpr uint32_t kNo = 0xE5CF1315;                    // netoptions_str_no
constexpr uint32_t kSkillLevelInfo = 0x48DAE9CB;        // skill_level_info
constexpr uint32_t kReasonFull = 0x7782B5B7;            // net_reason_full
constexpr uint32_t kReasonFullObservers = 0xDDE5676F;   // net_reason_full_observers
constexpr uint32_t kReason = 0xC44777F3;                // reason
constexpr uint32_t kJustDialog = 0xF6694E96;            // just_dialog
constexpr uint32_t kJoinRefusedDialog = 0x92528E98;     // CreateJoinRefusedDialog
constexpr uint32_t kCookie = 0x751F4599;                // cookie
constexpr uint32_t kMaxPlayers = 0xB7E39B53;            // MaxPlayers
constexpr uint32_t kChosenJoinServer = 0xBFF7C13B;      // net_chosen_join_server
constexpr uint32_t kServerId = 0xE7BB1948;              // server_id
constexpr uint32_t kDestroyLobbyServer = 0x10FA2FCB;    // destroy_lobby_server
constexpr uint32_t kUpdateServerList = 0x40C621E4;      // update_lobby_server_list
constexpr uint32_t kDescribeSelectedServer = 0x596BAB63; // describe_selected_server

constexpr int kNoPing = 9999;
constexpr int kPort = 0x1421; // 5153
constexpr int kMaxRank = 10;
constexpr uint16_t kServerResponseSize = 0xE0;

// GameSpy's listing messages (peerListingGamesCallback's msg).
enum ListingMessage : int { kListingAdd = 0, kListingUpdate = 1, kListingRemove = 2, kListingClear = 3 };

Manager* TheManager() { return g_gameNetManager; }
Mem::Manager* Memory() { return Mem::Manager::Get(); }
LobbyMan* Lobby(Manager* manager) { return static_cast<LobbyMan*>(manager->m_lobbyMan); }

template <typename R = void, typename... A>
R Sdk(uint32_t address, A... args)
{
    return GameFunction<R(__cdecl*)(A...)>(address)(args...);
}

Mem::ListNode* Head(Manager* manager, uint32_t offset) { return &Field<Mem::ListNode>(manager, offset); }
constexpr uint32_t kServers = 0x158;
constexpr uint32_t kWaitingServers = 0x16C;

void Append(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// A line of the server's description: "<title>:" as name and the value `value` makes (if any - after the name, as
// the original), net_game_info_add_item.
template <typename F>
void InfoLine(uint32_t title, F value)
{
    char text[128];
    Script::CStruct* params = NewParams();
    std::sprintf(text, "%s:", Script::GetString(title));
    params->AddString(kName, text);
    if (const char* shown = value(text))
        params->AddString(kValue, shown);
    Run(kAddInfoItem, params);
    DeleteParams(params);
}
const char* NoValue(char*) { return nullptr; }

// A rating's rank (as the lobby's: rating * 1/3500 * 10, truncated, at most 10).
int RankOf(int rating)
{
    double rank = static_cast<double>(rating) * g_rankScale * Nx::g_floatTen;
    int shown = static_cast<int>(rank);
    return shown > kMaxRank ? kMaxRank : shown;
}

// A server's details from GameSpy (the add and update messages alike): level, mode, counts, flags, skill.
void ReadServerKeys(ServerInfo* server, void* sb)
{
    std::strcpy(server->m_level, Sdk<const char*>(0x005FED50, sb, "mapname", "..."));  // (string keys)
    std::strcpy(server->m_mode, Sdk<const char*>(0x005FED50, sb, "gametype", "..."));
    server->m_numPlayers = Sdk<int>(0x005FF560, sb, "numplayers", 1);                   // (integer keys)
    server->m_maxPlayers = Sdk<int>(0x005FF560, sb, "maxplayers", 8);
    server->m_numObservers = Sdk<int>(0x005FF560, sb, "numobservers", 0);
    server->m_maxObservers = Sdk<int>(0x005FF560, sb, "maxobservers", 1);
    server->m_password = Sdk<int>(0x005FEDD0, sb, "password", 0) != 0;                 // (boolean keys)
    server->m_started = Sdk<int>(0x005FEDD0, sb, "started", 0) != 0;
    server->m_hostMode = Sdk<int>(0x005FF560, sb, "hostmode", 0);
    server->m_ranked = Sdk<int>(0x005FF560, sb, "ranked", 0) != 0;
    server->m_skillLevel = static_cast<int8_t>(Sdk<int>(0x005FF560, sb, "skilllevel", 2));
}
void ReadServerPlayers(ServerInfo* server, void* sb)
{
    for (int i = 0; i < server->m_numPlayers; ++i) {
        int rating = Sdk<int>(0x005FF5C0, sb, i, "rating", 0);                       // (a player's integer key)
        const char* name = Sdk<const char*>(0x005FEE30, sb, i, "player", "...");     // (a player's string key)
        server->AddPlayer(name, rating);
    }
}

// The listed server whose SBServer is `sb` (the original walks it with a Lst::Search<ServerInfo>; its first item is
// the searches' shared code, VaultSearch::FirstItem - the same layout).
ServerInfo* FindBySb(Manager* manager, void* sb)
{
    ServerSearch search;
    search.m_vtable = kServerSearchVtable;
    search.m_node = nullptr;
    auto* server = static_cast<ServerInfo*>(
        reinterpret_cast<VaultSearch*>(&search)->FirstItem(Head(manager, kServers))); // (the first)
    while (server) {
        Mem::ListNode* next = search.m_node->next;
        Mem::ListNode* node = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
        search.m_node = node;
        auto* following = node ? static_cast<ServerInfo*>(node->data) : nullptr;
        if (server->m_sbServer == sb)
            return server;
        server = following;
    }
    return nullptr;
}

} // namespace

// 0x00546790: the chosen server's description (destroy_server_desc_children, then net_game_info_add_item per line:
// game in progress, the host mode, ping - "..." unknown -, mode, skill - skill_level_info's name -, level, ranked
// (internet), the players' count title (": n/max") and each player with its rank). No null check on the server.
bool __cdecl CFunc_DescribeServer(Script::CStruct* params, Script::CScript* script)
{
    (void)script;
    Manager* manager = TheManager();
    uint32_t id = 0;
    params->GetChecksum(kId, &id, 0);
    auto* server = static_cast<ServerInfo*>(manager->GetServer(id));
    Run(kDestroyDescChildren);
    Memory()->PushContext(Memory()->m_frontEndHeap);
    if (server->m_started)
        InfoLine(kGameInProgress, NoValue);
    if (server->m_hostMode == 1)
        InfoLine(kAutoServe, NoValue);
    else if (server->m_hostMode == 2)
        InfoLine(kFcfs, NoValue);
    InfoLine(kTitlePing, [server](char* text) {
        if (server->m_ping == kNoPing)
            std::sprintf(text, "...");
        else
            std::sprintf(text, "%d", server->m_ping);
        return const_cast<const char*>(text);
    });
    InfoLine(kTitleMode, [server](char* text) {
        std::sprintf(text, "%s", server->m_mode);
        return const_cast<const char*>(text);
    });
    const char* skill;
    Script::CArray* skills = Script::GetArray(kSkillLevelInfo);
    reinterpret_cast<Script::CStruct*>(skills->GetElement(server->m_skillLevel))->GetString(kName, &skill, 0);
    InfoLine(kTitleSkill, [skill](char* text) {
        std::sprintf(text, "%s", skill);
        return const_cast<const char*>(text);
    });
    InfoLine(kTitleLevel, [server](char* text) {
        std::sprintf(text, "%s", server->m_level);
        return const_cast<const char*>(text);
    });
    if (manager->InInternetMode()) {
        InfoLine(kTitleRanked, [server](char* text) {
            std::sprintf(text, "%s", Script::GetString(server->m_ranked ? kYes : kNo));
            return const_cast<const char*>(text);
        });
    }
    char value[128];
    Script::CStruct* title = NewParams();
    std::sprintf(value, ": %d/%d", server->m_numPlayers, server->m_maxPlayers);
    title->AddString(kText, value);
    Run(kUpdatePlayerTitle, title);
    DeleteParams(title);
    for (int i = 0; i < server->m_numPlayers; ++i) {
        int rank = RankOf(server->GetPlayerRating(i));
        Script::CStruct* player = NewParams();
        std::sprintf(value, "%d:", i + 1);
        player->AddInteger(kRank, rank);
        player->AddString(kName, value);
        std::sprintf(value, "%s", server->GetPlayerName(i));
        player->AddString(kValue, value);
        Run(kAddPlayer, player);
        DeleteParams(player);
    }
    Memory()->PopContext();
    return true;
}

// 0x005470D0: the chosen server joined (net_chosen_join_server with its port, address and maximum players - and a
// random cookie when it can only be reached through NAT negotiation; presence told the server's name, 0x0053F830),
// unless it's full for how we join (CreateJoinRefusedDialog with the reason, the join-state task removed).
bool __cdecl CFunc_ChooseServer(Script::CStruct* params, Script::CScript* script)
{
    (void)script;
    Manager* manager = TheManager();
    uint32_t id = 0;
    params->GetChecksum(kId, &id, 0);
    manager->ClearChosenServers();
    auto* server = static_cast<ServerInfo*>(manager->GetServer(id));
    uint32_t refused = 0;
    if (manager->GetJoinMode() == 0) {
        if (server->m_numPlayers >= server->m_maxPlayers)
            refused = kReasonFull;
    } else if (manager->GetJoinMode() == 1) {
        if (server->m_numObservers >= server->m_maxObservers)
            refused = kReasonFullObservers;
    }
    Script::CStruct* reply = NewParams();
    if (refused) {
        reply->AddChecksum(kReason, refused);
        reply->AddChecksum(kJustDialog, 0);
        manager->m_joinStateTask->Remove();
        Run(kJoinRefusedDialog, reply);
    } else {
        if (Field<bool>(manager->m_buddyMan, 0x10)) // (0x005480B0: logged in to presence)
            static_cast<LobbyMan*>(manager->m_lobbyMan)->SetServerName(server->m_name);
        in_addr address;
        address.s_addr = server->m_ip;
        const char* ip = GameFunction<const char*(__stdcall*)(in_addr)>(0x005BEAB4)(address); // (inet_ntoa)
        if (!server->m_canReach)
            reply->AddComponentOfType(kCookie, Script::ESYMBOLTYPE_NAME, Mth::Rnd(0x7FFF) * Mth::Rnd(0x7FFF));
        reply->AddComponentOfType(0, Script::ESYMBOLTYPE_INTEGER, server->m_port);
        reply->AddString(0, Script::ESYMBOLTYPE_STRING, ip);
        reply->AddInteger(kMaxPlayers, server->m_maxPlayers);
        Run(kChosenJoinServer, reply);
    }
    DeleteParams(reply);
    return true;
}

// 0x00547FA0: the chosen server's full details asked of GameSpy (0x005FCA60) unless they're here (true) or on the
// LAN (true); false while asking.
bool __cdecl CFunc_RetrieveServerInfo(Script::CStruct* params, Script::CScript* script)
{
    (void)script;
    Manager* manager = TheManager();
    if ((manager->m_flags >> 3) & 1) // (0x00533F70: LAN mode)
        return true;
    uint32_t id = 0;
    params->GetChecksum(kId, &id, 0);
    auto* server = static_cast<ServerInfo*>(manager->GetServer(id));
    manager->ClearChosenServers();
    server->m_chosen = true;
    if (server->m_detailed)
        return true;
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    Sdk(0x005FCA60, Lobby(manager)->m_peer, server->m_sbServer, 1); // (the game's details updated)
    Memory()->PopContext();
    return false;
}

// 0x005475C0 (the server-list client's handler for a LAN server's reply, 224 bytes): a join-by-address search
// notes the replying address (0x007CCA84, 0x007CCA88); a server not listed yet is added to the waiting servers with
// the reply's name, level, mode, counts, flags, skill and players (the ping: the time since the time it carries).
// 4 for a reply of another size or a server known already, else 1.
int __cdecl HandleServerResponse(Net::MsgHandlerContext* context)
{
    auto* manager = static_cast<Manager*>(context->m_data);
    if (g_askedFor) {
        g_askedFor = context->m_Conn->m_ip;
        g_joinServerIPSucceeded = true;
    }
    if (context->m_MsgLength != kServerResponseSize)
        return kDone;
    const char* reply = context->m_Msg;
    if (manager->HasServer(reply + 0x08, context->m_Conn->m_ip))
        return kDone;
    Memory()->PushContext(Memory()->m_networkHeap);
    void* block = Mem::Calloc(sizeof(ServerInfo));
    ServerInfo* server = block ? static_cast<ServerInfo*>(block)->Construct() : nullptr;
    std::strcpy(server->m_name, reply + 0x08);
    server->m_ip = context->m_Conn->m_ip;
    server->m_ping = static_cast<int>(Tmr::GetTime() - Field<uint32_t>(reply, 0x5C));
    std::strcpy(server->m_level, reply + 0x18);
    std::strcpy(server->m_mode, reply + 0x38);
    server->m_maxPlayers = static_cast<int8_t>(reply[1]);
    server->m_numPlayers = static_cast<int8_t>(reply[0]);
    server->m_maxObservers = static_cast<int8_t>(reply[3]);
    server->m_numObservers = static_cast<int8_t>(reply[2]);
    server->m_password = reply[4] != 0;
    server->m_started = reply[5] != 0;
    server->m_hostMode = static_cast<int8_t>(reply[6]);
    server->m_ranked = reply[7] != 0;
    server->m_skillLevel = static_cast<int8_t>(reply[0x58]);
    server->m_port = kPort;
    for (int i = 0; i < server->m_numPlayers; ++i)
        server->AddPlayer(reply + 0x60 + 0x10 * i, 0);
    Append(Head(manager, kWaitingServers), server);
    Memory()->PopContext();
    return kContinue;
}

// 0x00547750 (GameSpy's game listing callback, `manager` its parameter): on failure the lobby's 0x0053F870; add - a new server
// from the SBServer (its address: the private one when its public address is ours, else the public one and port -
// unreachable without NAT negotiation when it has no private address; the keys, the players), listed, and shown once
// it has its basic keys (0x005FEEC0); update - a listed one's name, ping (if reachable), keys and players again,
// shown if not yet, and with its full keys (0x005FEEE0) the description asked for when it's the chosen one; remove -
// destroy_lobby_server and the server deleted; clear - "none found" and every server deleted. Each but a failure ends
// with update_lobby_server_list (the update message only when it shows the server).
void __cdecl ListingGamesCallback(void* peer, int success, const char* name, void* sb, int staging, int message,
                                  int progress, Manager* manager)
{
    (void)staging;
    (void)progress;
    if (!success) {
        static_cast<LobbyMan*>(TheManager()->m_lobbyMan)->SetLostConnection(); // (the lobby told the listing failed)
        return;
    }
    switch (message) {
    case kListingAdd: {
        Memory()->PushContext(Memory()->m_internetTopDownHeap);
        void* block = Mem::Calloc(sizeof(ServerInfo));
        ServerInfo* server = block ? static_cast<ServerInfo*>(block)->Construct() : nullptr;
        server->m_sbServer = sb;
        std::strncpy(server->m_name, name, 15);
        server->m_name[15] = '\0';
        server->m_ping = Sdk<int>(0x005FF410, sb); // (its ping)
        bool sameNetwork = false;
        if (Sdk<int>(0x005FEF30, sb)) { // (it answered directly)
            uint32_t ours = Sdk<uint32_t>(0x005FC940, peer); // (our public address)
            if (Sdk<uint32_t>(0x00603360, sb) == ours) {      // (its public address)
                server->m_ip = Sdk<uint32_t>(0x005FEF70, sb); // (its private address)
                server->m_port = kPort;
                sameNetwork = true;
            }
        }
        if (!sameNetwork) {
            bool hasPrivate = Sdk<int>(0x005FEF50, sb) != 0; // (it has a private address)
            server->m_ip = Sdk<uint32_t>(0x00603360, sb);
            uint16_t port = static_cast<uint16_t>(Sdk<uint32_t>(0x005FEF00, sb)); // (its public port)
            if (!hasPrivate) {
                server->m_canReach = false;
                server->m_ping = kNoPing;
            }
            server->m_port = port;
        }
        ReadServerKeys(server, sb);
        ReadServerPlayers(server, sb);
        Mem::ListNode* list = Head(manager, kServers);
        Append(list, server);
        if (Sdk<int>(0x005FEEC0, sb)) { // (it has its basic keys)
            int index = reinterpret_cast<Gfx::AnimListHead*>(list)->CountItems() - 1; // (the list's count)
            manager->AddServerToMenu(server, index);
            Run(kUpdateServerList);
            server->m_inList = true;
        }
        Memory()->PopContext();
        break;
    }
    case kListingRemove: {
        Script::CStruct* params = NewParams();
        params->AddChecksum(kServerId, reinterpret_cast<uint32_t>(sb));
        Run(kDestroyLobbyServer, params);
        DeleteParams(params);
        if (ServerInfo* server = FindBySb(manager, sb))
            reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(server->vtable[0])(server, 1);
        Run(kUpdateServerList);
        break;
    }
    case kListingClear:
        manager->ShowNoServersFound(false);
        manager->ClearServerList();
        Run(kUpdateServerList);
        break;
    case kListingUpdate: {
        Sdk<int>(0x005FEEE0, sb); // (asked whether it has its full keys: the answer unused here)
        ServerInfo* server = FindBySb(manager, sb);
        if (!server)
            break;
        std::strncpy(server->m_name, name, 15);
        server->m_name[15] = '\0';
        if (server->m_canReach)
            server->m_ping = Sdk<int>(0x005FF410, sb);
        ReadServerKeys(server, sb);
        server->ClearPlayers();
        ReadServerPlayers(server, sb);
        if (!server->m_inList && Sdk<int>(0x005FEEC0, sb)) {
            manager->AddServerToMenu(server, 0);
            Run(kUpdateServerList);
            server->m_inList = true;
        }
        if (!server->m_detailed && Sdk<int>(0x005FEEE0, sb)) {
            server->m_detailed = true;
            if (server->m_chosen) {
                Script::CStruct* params = NewParams();
                params->AddChecksum(kId, reinterpret_cast<uint32_t>(sb));
                Run(kDescribeSelectedServer, params);
                DeleteParams(params);
            }
        }
        break;
    }
    default:
        break;
    }
}

} // namespace GameNet

REPLACE(0x00546790, GameNet::CFunc_DescribeServer);
REPLACE(0x005470D0, GameNet::CFunc_ChooseServer);
REPLACE(0x00547FA0, GameNet::CFunc_RetrieveServerInfo);
REPLACE(0x005475C0, GameNet::HandleServerResponse);  // (not a Ghidra function)
REPLACE(0x00547750, GameNet::ListingGamesCallback);  // (not a Ghidra function)
