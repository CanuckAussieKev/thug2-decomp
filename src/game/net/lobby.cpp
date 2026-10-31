// The GameSpy lobby (see lobby.h).
//
// The GameSpy Peer SDK (game/net/gamespy.h): what each call does is said where it's made.

#include "game/net/buddy_man.h"
#include "game/net/gamenet_lobby.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/online_identity.h"
#include "game/net/stats_man.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/gfx/loading_bar.h"
#include "game/ass/assman.h"
#include "game/net/task_vtable.h"
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
#include "game/net/gamespy.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"

#pragma warning(disable : 4996) // (the original's sprintf / strcpy, kept)

namespace OnlinePlay {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kNumPlayers = 0x1CB2A6B0;              // num_players
constexpr uint32_t kUpdatePlayerList = 0x695C0037;        // update_lobby_player_list
constexpr uint32_t kDestroyUserChildren = 0xC380FBEE;     // destroy_lobby_user_list_children
constexpr uint32_t kDestroyBuddyChildren = 0x4C2FD924;    // destroy_lobby_buddy_list_children
constexpr uint32_t kPrepareUserList = 0xEBF04D48;         // prepare_lobby_user_list_for_new_children
constexpr uint32_t kConsoleMessage = 0xCCB19938;          // create_console_message
constexpr uint32_t kStatusJoined = 0x404CEABA;            // lobby_status_joined
constexpr uint32_t kStatusLeft = 0xBA252C57;              // lobby_status_left
constexpr uint32_t kUserId = 0x58912C6A;                  // user_id
constexpr uint32_t kDestroyUser = 0x053A9B81;             // destroy_lobby_user
constexpr uint32_t kText = 0xC4745838;                    // text
constexpr uint32_t kLeft = 0x85981897;                    // left
constexpr uint32_t kId = 0x40C698AF;                      // id
constexpr uint32_t kRank = 0x7786171A;                    // rank
constexpr uint32_t kUpdateRank = 0xCD827195;              // player_list_update_rank
constexpr uint32_t kDialogBoxExit = 0x666B6A9D;           // dialog_box_exit
constexpr uint32_t kCreateLobbyListMenu = 0x3F584E2A;     // create_lobby_list_menu
constexpr uint32_t kCreateSelectGamesMenu = 0x204092CB;   // create_network_select_games_menu
constexpr uint32_t kJoinLobbyFailed = 0x3C5FE392;         // CreateJoinLobbyFailedDialog
constexpr uint32_t kIndex = 0x7F8C98FE;                   // index

constexpr int kGroupRoom = 1; // (GameSpy's RoomType)
constexpr int kMaxRank = 10;

static VtableSlot kLobbyListVtable_slots[] = {&OnlinePlay::LobbyNode::DestroyOther};
void** const kLobbyListVtable = reinterpret_cast<void**>(kLobbyListVtable_slots); // ListKit::Head<LobbyRoom>, LobbyRoom
static VtableSlot kPlayerListVtable_slots[] = {&RoomManager::Destroy};
void** const kPlayerListVtable = reinterpret_cast<void**>(kPlayerListVtable_slots); // ListKit::Head<LobbyPlayer>, LobbyPlayer

Manager* TheManager() { return g_gameNetManager; }
RoomManager* TheLobby() { return static_cast<RoomManager*>(TheManager()->m_lobbyMan); }
MemSys::Manager* Memory() { return MemSys::Manager::Get(); }

// Walking a list as the original does: a node whose data is -1 is the head (checked before null).
MemSys::ListNode* FirstNode(MemSys::ListNode* head)
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

// The listed player named `nick` (case ignored), or null.
LobbyPlayer* FindPlayer(RoomManager* lobby, const char* nick)
{
    for (MemSys::ListNode* node = FirstNode(&lobby->m_players); node; node = NextNode(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data);
        if (!player)
            break;
        if (_stricmp(player->m_nick, nick) == 0)
            return player;
    }
    return nullptr;
}

// A new player named `nick` (null if out of memory: then written to anyway, as the original), its profile asked of
// the SDK, at the end of the list.
LobbyPlayer* EnrollPlayer(RoomManager* lobby, void* peer, const char* nick, bool listed)
{
    auto* player = static_cast<LobbyPlayer*>(MemSys::AllocZeroed(sizeof(LobbyPlayer)));
    if (player) {
        player->m_listLink.data = player;
        player->m_listLink.priority = 0;
        player->m_listLink.next = &player->m_listLink;
        player->m_listLink.prev = &player->m_listLink;
        player->m_listLink.vtable = kPlayerListVtable;
        player->m_profile = 0;
        player->m_skillPoints = 0;
        player->m_listed = false;
    }
    std::strcpy(player->m_nick, nick);
    GameSpy::PeerGetPlayerInfoNoWait(peer, nick, nullptr, &player->m_profile); // (the player's profile id)
    if (listed)
        player->m_listed = true;
    MemSys::ListNode* head = &lobby->m_players;
    player->m_listLink.prev = head->prev;
    player->m_listLink.next = head;
    head->prev->next = &player->m_listLink;
    head->prev = &player->m_listLink;
    return player;
}

// Every node of `head`'s list deleted through its vtable.
void DeleteAll(MemSys::ListNode* head)
{
    MemSys::ListNode* node = FirstNode(head);
    auto* item = DataOf<MemSys::ListNode>(node);
    while (item) {
        node = node ? NextNode(node) : nullptr;
        auto* following = DataOf<MemSys::ListNode>(node);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(item->vtable[0])(item, 1);
        item = following;
    }
}

// A player's rating from a room key and its rank, told to the player list.
void UpdateRank(LobbyPlayer* player, const char* value)
{
    player->m_skillPoints = std::atol(value);
    double rank = static_cast<double>(player->m_skillPoints) * g_rankScale * Engine3D::g_floatTen;
    int shown = static_cast<int>(rank); // (truncated, as _ftol)
    if (shown > kMaxRank)
        shown = kMaxRank;
    Scripts::CStruct* params = NewParams();
    params->PutNameKey(kId, Checksums::HashFromText(player->m_nick));
    params->PutInt(kRank, shown);
    Run(kUpdateRank, params);
    DeleteParams(params);
}

// "<nick> <text>" to the console, with the flag `kind` (by name).
void ConsoleMessage(const char* nick, uint32_t textName, bool byName)
{
    char line[1024];
    std::sprintf(line, "%s %s", nick, Scripts::GetString(textName));
    Scripts::CStruct* params = NewParams();
    if (byName) {
        params->AddString("text", line);
        params->PutNameKey("join", 0);
    } else {
        params->AddString(kText, line);
        params->PutNameKey(kLeft, 0);
    }
    Run(kConsoleMessage, params);
    DeleteParams(params);
}

// A TaskList::Task<RoomManager> running `code` (priority 0; null if out of memory).
TaskList::SchedulerTask* NewTask(RoomManager* lobby, uint32_t code)
{
    auto* task = static_cast<TaskList::SchedulerTask*>(MemSys::AllocZeroed(0x20));
    if (!task)
        return nullptr;
    task->Construct(0);
    task->m_vtable = GameCode::kTaskVtable;
    Field<uint32_t>(task, 0x18) = code;
    Field<RoomManager*>(task, 0x1C) = lobby;
    return task;
}

} // namespace

// 0x00545860
LobbyRoom* LobbyRoom::Construct()
{
    m_listLink.data = this;
    m_listLink.priority = 0;
    m_listLink.next = &m_listLink;
    m_listLink.prev = &m_listLink;
    m_listLink.vtable = kLobbyListVtable;
    m_full = false;
    m_neversoft = false;
    m_outOfRange = false;
    return this;
}

// 0x00541870
RoomManager* RoomManager::Construct()
{
    m_rooms.data = reinterpret_cast<void*>(-1);
    m_rooms.priority = 0;
    m_rooms.next = &m_rooms;
    m_rooms.prev = &m_rooms;
    m_rooms.vtable = kLobbyListVtable;
    m_players.data = reinterpret_cast<void*>(-1);
    m_players.priority = 0;
    m_players.next = &m_players;
    m_players.prev = &m_players;
    m_players.vtable = kPlayerListVtable;
    m_negotiations = 0;
    m_inGroupRoom = false;
    m_expectingDisconnect = true;
    m_peer = nullptr;
    m_lostConnection = false;
    m_thinkTask = NewTask(this, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::ThinkTask)));
    m_natTask = NewTask(this, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::NatNegotiationTask)));
    m_hostingTask = NewTask(this, reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::QueryTask)));
    // The server-browsing keys (GameSpy's qr2 key registration).
    GameSpy::Qr2RegisterKey(0x33, "numobservers");
    GameSpy::Qr2RegisterKey(0x34, "maxobservers");
    GameSpy::Qr2RegisterKey(0x35, "skilllevel");
    GameSpy::Qr2RegisterKey(0x36, "started");
    GameSpy::Qr2RegisterKey(0x37, "hostmode");
    GameSpy::Qr2RegisterKey(0x38, "ranked");
    GameSpy::Qr2RegisterKey(0x39, "rating_");
    return this;
}

// 0x00541AC0
RoomManager* RoomManager::Destroy(unsigned int flags)
{
    reinterpret_cast<LobbyListNode*>(this)->DestroyBody(); // (the destructor body: 0x00540860, its base node unlinked)
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x00542DF0
void RoomManager::InitPeer()
{
    Memory()->EnterHeapScope(Memory()->m_networkHeap);
    // The SDK's callbacks (0x74 bytes; those not set stay null).
    uint32_t callbacks[0x1D] = {};
    callbacks[0x00 / 4] = reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::DisconnectedCallback)); // disconnected
    callbacks[0x04 / 4] = reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::RoomMessageCallback)); // roomMessage
    callbacks[0x14 / 4] = reinterpret_cast<uint32_t>(CodeAddress(Stubs::DoNothing)); // (nothing: the empty function)
    callbacks[0x24 / 4] = reinterpret_cast<uint32_t>(&PlayerJoinedCallback);
    callbacks[0x28 / 4] = reinterpret_cast<uint32_t>(&PlayerLeftCallback);
    callbacks[0x38 / 4] = reinterpret_cast<uint32_t>(&PlayerInfoCallback);
    callbacks[0x30 / 4] = reinterpret_cast<uint32_t>(&NewPlayerListCallback);
    callbacks[0x4C / 4] = reinterpret_cast<uint32_t>(&RoomKeyChangedCallback);
    callbacks[0x68 / 4] = reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::NatNegotiateCallback));
    callbacks[0x5C / 4] = reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::KeyListCallback));
    callbacks[0x50 / 4] = reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::ServerKeyCallback));
    callbacks[0x54 / 4] = reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::PlayerKeyCallback));
    callbacks[0x60 / 4] = reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::CountCallback));
    callbacks[0x64 / 4] = reinterpret_cast<uint32_t>(CodeAddress(Stubs::DoNothing));
    callbacks[0x6C / 4] = reinterpret_cast<uint32_t>(CodeAddress(OnlinePlay::RoomManager::PublicAddressCallback));
    callbacks[0x70 / 4] = reinterpret_cast<uint32_t>(this); // (their parameter)
    m_peer = GameSpy::PeerInitialize(callbacks); // (the peer made)
    m_inGroupRoom = false;
    m_expectingDisconnect = true;
    m_hosting = false;
    m_restrictedHosting = false;
    Memory()->LeaveHeapScope();
}

uint32_t g_unknownE430;

// 0x00541A10
void RoomManager::StartListingGames()
{
    Manager* manager = TheManager();
    Memory()->EnterHeapScope(Memory()->m_internetBottomUpHeap);
    manager->SetServiceState(14); // (the server list's state)
    g_unknownE430 = 0;
    g_listingAsked = 0;
    g_listingFound = 0;
    GameSpy::PeerListGroupRooms(m_peer, "\\maxplayers\\neversoft\\maxrating\\minrating",
                                CodeAddress(OnlinePlay::RoomManager::GroupRoomCallback), this, 0); // (the group rooms listed)
    Memory()->LeaveHeapScope();
}

// 0x005427E0
void RoomManager::ClearLobbies()
{
    Memory()->EnterHeapScope(Memory()->m_internetTopDownHeap);
    if (m_peer)
        GameSpy::PeerStopListingGames(m_peer); // (the games' listing stopped)
    DeleteAll(&m_rooms);
    Memory()->LeaveHeapScope();
}

// 0x005428B0
void RoomManager::PopulateRoomList()
{
    Run(kDialogBoxExit);
    Run(kCreateLobbyListMenu);
    int index = 0;
    for (MemSys::ListNode* node = FirstNode(&m_rooms); node; node = NextNode(node)) {
        auto* lobby = static_cast<LobbyRoom*>(node->data);
        if (!lobby)
            break;
        AddRoomMenuItem(lobby, index); // (its menu item)
        ++index;
    }
}

// 0x00542970
LobbyRoom* RoomManager::GetLobby(int index)
{
    for (MemSys::ListNode* node = FirstNode(&m_rooms); node; node = NextNode(node)) {
        auto* lobby = static_cast<LobbyRoom*>(node->data);
        if (!lobby)
            return nullptr;
        if (index == 0)
            return lobby;
        --index;
    }
    return nullptr;
}

// 0x00541AF0
void RoomManager::ClearPlayers()
{
    DeleteAll(&m_players);
    Run(kUpdatePlayerList);
}

// 0x00541BA0
void RoomManager::FillPlayerList()
{
    Run(kDestroyUserChildren);
    for (MemSys::ListNode* node = FirstNode(&m_players); node; node = NextNode(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data);
        if (!player)
            break;
        if (player->m_listed)
            AddPlayerListItem(player); // (its menu item)
    }
    Run(kUpdatePlayerList);
}

// 0x00541C70
void RoomManager::FillProspectiveBuddyList()
{
    Manager* manager = TheManager();
    Run(kDestroyBuddyChildren);
    for (MemSys::ListNode* node = FirstNode(&m_players); node; node = NextNode(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data);
        if (!player)
            break;
        int profile = player->m_profile;
        if (profile != Field<int>(manager->m_buddyMan, 0x04) && profile > 0 && // (0x005480C0: our own profile)
            !static_cast<FriendsManager*>(manager->m_buddyMan)->IsBuddy(profile)) // (a buddy already)
            AddBuddyMenuItem(player);                                        // (a prospective buddy)
    }
}

// 0x00542B10
void RoomManager::RequestPlayerList()
{
    Memory()->EnterHeapScope(Memory()->m_internetTopDownHeap);
    ClearPlayers();
    Run(kDestroyUserChildren);
    GameSpy::PeerEnumPlayers(m_peer, kGroupRoom, &EnumPlayersCallback, this); // (the room's players)
    Memory()->LeaveHeapScope();
}

// 0x00541D50
void __cdecl RoomManager::PlayerJoinedCallback(void* peer, int roomType, const char* nick, RoomManager* lobby)
{
    (void)roomType;
    Memory()->EnterHeapScope(Memory()->m_internetTopDownHeap);
    if (LobbyPlayer* known = FindPlayer(lobby, nick)) {
        GameSpy::PeerGetPlayerInfoNoWait(peer, nick, nullptr, &known->m_profile);
        Memory()->LeaveHeapScope();
        known->m_listed = true;
        return;
    }
    LobbyPlayer* player = EnrollPlayer(lobby, peer, nick, true);
    Run(kPrepareUserList);
    lobby->AddPlayerListItem(player); // (its menu item)
    Run(kUpdatePlayerList);
    ConsoleMessage(nick, kStatusJoined, true);
    Memory()->LeaveHeapScope();
}

// 0x00541FD0
void __cdecl RoomManager::PlayerInfoCallback(void* peer, int roomType, const char* nick, uint32_t ip, int profileId,
                                          RoomManager* lobby)
{
    (void)peer;
    (void)roomType;
    (void)ip;
    if (!nick)
        return;
    LobbyPlayer* player = FindPlayer(lobby, nick);
    if (!player)
        return;
    if (player->m_profile == 0)
        player->m_profile = profileId;
    player->m_listed = true;
    lobby->AddPlayerListItem(player); // (its menu item)
    Run(kUpdatePlayerList);
}

// 0x005420C0
void __cdecl RoomManager::PlayerLeftCallback(void* peer, int roomType, const char* nick, const char* reason,
                                          RoomManager* lobby)
{
    (void)peer;
    (void)roomType;
    (void)reason;
    if (LobbyPlayer* player = FindPlayer(lobby, nick)) {
        Scripts::CStruct* params = NewParams();
        params->PutNameKey(kUserId, Checksums::HashFromText(nick));
        Run(kDestroyUser, params);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(player->m_listLink.vtable[0])(player, 1);
        DeleteParams(params);
        Run(kUpdatePlayerList);
    }
    ConsoleMessage(nick, kStatusLeft, false);
}

// 0x00542B70
void __cdecl RoomManager::NewPlayerListCallback(void* peer, int roomType, RoomManager* lobby)
{
    (void)peer;
    (void)roomType;
    lobby->RequestPlayerList();
}

// 0x00542650
void __cdecl RoomManager::RoomKeyChangedCallback(void* peer, int roomType, const char* nick, const char* key,
                                              const char* value, RoomManager* lobby)
{
    (void)peer;
    (void)roomType;
    (void)key;
    if (LobbyPlayer* player = FindPlayer(lobby, nick))
        UpdateRank(player, value);
}

// 0x00542330
void __cdecl RoomManager::RoomKeysCallback(void* peer, int success, int roomType, const char* nick, int count,
                                        char** keys, char** values, RoomManager* lobby)
{
    (void)peer;
    (void)success;
    (void)roomType;
    if (count <= 0 || !keys[0] || _stricmp(keys[0], "b_rating") != 0)
        return;
    for (MemSys::ListNode* node = FirstNode(&lobby->m_players); node; node = NextNode(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data);
        if (!player)
            break;
        if (_stricmp(player->m_nick, nick) == 0)
            UpdateRank(player, values[0]); // (every player of that nick)
    }
}

// 0x005424F0
void __cdecl RoomManager::EnumPlayersCallback(void* peer, int success, int roomType, int index, const char* nick,
                                           int flags, RoomManager* lobby)
{
    (void)roomType;
    (void)flags;
    if (!success || index == -1)
        return;
    if (FindPlayer(lobby, nick))
        return;
    Memory()->EnterHeapScope(Memory()->m_internetBottomUpHeap);
    EnrollPlayer(lobby, peer, nick, false);
    Memory()->LeaveHeapScope();
}

// 0x00542B80
void __cdecl RoomManager::JoinRoomCallback(void* peer, int success, int result, int roomType, RoomManager* lobby)
{
    (void)result;
    Manager* manager = TheManager();
    if (!success) {
        Run(kJoinLobbyFailed);
        return;
    }
    Run(kDialogBoxExit);
    Run(kCreateSelectGamesMenu);
    lobby->m_inGroupRoom = true;
    lobby->RequestPlayerList();
    if (!Field<bool>(manager->m_buddyMan, 0x10)) // (0x005480B0: logged in to GameSpy presence)
        return;
    // Our status: chatting in the lobby (0x00548320).
    static_cast<FriendsManager*>(manager->m_buddyMan)->SetStatus(4, Scripts::GetString("homie_status_chatting"), lobby->m_roomName);
    // Our room key b_rating: the stats' rating (0x0054CA80 of the stats manager, read through its pointer).
    char key[64];
    char value[64];
    std::sprintf(key, "b_rating");
    int* rating = &static_cast<RankingsManager*>(manager->m_statsMan)->GetPersonalStats()->m_skillPoints;
    std::sprintf(value, "%d", *rating);
    const char* keys[1] = {key};
    const char* values[1] = {value};
    const char* ourNick = GameSpy::PeerGetNick(peer); // (our nick)
    GameSpy::PeerSetRoomKeys(peer, roomType, ourNick, 1, keys, values); // (our room keys set)
    GameSpy::PeerGetRoomKeys(peer, roomType, "*", 1, keys, &RoomKeysCallback, lobby, 0); // (everyone's asked for)
}

// --- The script commands ---

// 0x00541A70: num_players = the players listed in the lobby.
bool __cdecl CFunc_NumPlayersInLobby(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    int count = 0;
    for (MemSys::ListNode* node = FirstNode(&TheLobby()->m_players); node; node = NextNode(node))
        ++count;
    script->m_params->PutInt(kNumPlayers, count);
    return true;
}

// 0x005429C0: out of the lobby and GameSpy (the peer's game stopped, its title cleared and the peer disconnected -
// peerStopGame, peerClearTitle, peerDisconnect), the lists emptied, two of its tasks removed, then 0x00540750.
bool __cdecl CFunc_LobbyDisconnect(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = TheManager();
    Memory()->EnterHeapScope(Memory()->m_internetTopDownHeap);
    RoomManager* lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    while (lobby->m_negotiations) { // (counted down one at a time, as the original - a negative count takes long)
        --lobby->m_negotiations;
        lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    }
    static_cast<RoomManager*>(manager->m_lobbyMan)->ClearLobbies();
    static_cast<RoomManager*>(manager->m_lobbyMan)->ClearPlayers();
    static_cast<TaskList::SchedulerTask*>(static_cast<RoomManager*>(manager->m_lobbyMan)->m_thinkTask)->Remove();
    static_cast<TaskList::SchedulerTask*>(static_cast<RoomManager*>(manager->m_lobbyMan)->m_natTask)->Remove();
    lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    lobby->m_inGroupRoom = false;
    lobby->m_expectingDisconnect = true;
    if (lobby->m_peer) {
        GameSpy::PeerStopGame(lobby->m_peer);
        GameSpy::PeerClearTitle(lobby->m_peer);
        GameSpy::PeerDisconnect(lobby->m_peer);
    }
    static_cast<RoomManager*>(manager->m_lobbyMan)->Disconnect();
    Memory()->LeaveHeapScope();
    return true;
}

// 0x00542AA0: the lobby's player list shown (the manager's singleton taken and given back - 0x0053F520 /
// 0x005395C0).
bool __cdecl CFunc_FillPlayerList(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = Manager::Create(false);
    static_cast<RoomManager*>(manager->m_lobbyMan)->FillPlayerList();
    ReleaseManager();
    return true;
}

// 0x00542B00
bool __cdecl CFunc_FillLobbyProspectiveBuddyList(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    (void)script;
    TheLobby()->FillProspectiveBuddyList();
    return true;
}

// 0x00542CE0: back into the chosen lobby's group room, if any (peerJoinGroupRoom, JoinRoomCallback).
bool __cdecl CFunc_RejoinLobby(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = TheManager();
    Memory()->EnterHeapScope(Memory()->m_internetTopDownHeap);
    RoomManager* lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    if (lobby->m_groupId)
        GameSpy::PeerJoinGroupRoom(lobby->m_peer, lobby->m_groupId, &RoomManager::JoinRoomCallback, lobby, 0);
    Memory()->LeaveHeapScope();
    return true;
}

// 0x00542D30: lobby `index` chosen (no null check: as the original) - its group room, numbers, name and flag kept,
// the room joined (JoinRoomCallback), the lobby list emptied.
bool __cdecl CFunc_ChooseLobby(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)script;
    Manager* manager = TheManager();
    int index = 0;
    Memory()->EnterHeapScope(Memory()->m_internetTopDownHeap);
    params->FetchIntValue(kIndex, &index, 0);
    RoomManager* lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    LobbyRoom* chosen = lobby->GetLobby(index);
    lobby->m_groupId = chosen->m_groupId;
    lobby->m_unknown30 = chosen->m_numPlayers;
    lobby->m_unknown2C = chosen->m_maxPlayers;
    std::strcpy(lobby->m_roomName, chosen->m_name);
    lobby->m_restrictedHosting = chosen->m_neversoft;
    GameSpy::PeerJoinGroupRoom(lobby->m_peer, lobby->m_groupId, &RoomManager::JoinRoomCallback, lobby, 0);
    lobby->ClearLobbies();
    Memory()->LeaveHeapScope();
    return true;
}

// 0x00542EE0: the peer made, the title set (peerSetTitle: the game name for the title and the server list -
// NetLink::GameName: OnlineGameName or thps6pc -, its secret key - stored bitwise-NOT at 0x0067F154, where
// online_identity.cpp writes OnlineGameKey -, version 6, 15 updates, NAT negotiation, pinging the group room only),
// the connecting thread started (0x00540AB0, suspended first so its id becomes the lobby id - 0x00533240), and the
// lobby's first task added to the main loop's logic tasks.
// (The original pushes the game name's address, 0x0064B1CC; online_identity.cpp wrote the configured name's address
// over those two operands before this was source.)
bool __cdecl CFunc_LobbyConnect(Scripts::CStruct* params, Scripts::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = TheManager();
    auto* logic = reinterpret_cast<TaskList::Stack*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 0x04); // (the logic tasks)
    DWORD threadId = 0;
    BOOL pingRooms[3] = {FALSE, TRUE, FALSE};
    BOOL crossPingRooms[3] = {FALSE, FALSE, FALSE};
    Memory()->EnterHeapScope(Memory()->m_internetBottomUpHeap);
    RoomManager* lobby = static_cast<RoomManager*>(manager->m_lobbyMan);
    lobby->InitPeer();
    const char* key = NetLink::GameKey(); // (given to the SDK: nothing written into data)
    const char* name = NetLink::GameName();
    GameSpy::PeerSetTitle(static_cast<RoomManager*>(manager->m_lobbyMan)->m_peer, name, key, name, key, 6, 15, 1, pingRooms,
                          crossPingRooms);
    HANDLE thread = CreateThread(nullptr, 0x8000, reinterpret_cast<LPTHREAD_START_ROUTINE>(CodeAddress(OnlinePlay::RoomManager::ConnectThread)),
                                 manager->m_lobbyMan, CREATE_SUSPENDED, &threadId);
    manager->SetRoomId(threadId);
    ResumeThread(thread);
    logic->AddTask(static_cast<TaskList::SchedulerTask*>(static_cast<RoomManager*>(manager->m_lobbyMan)->m_thinkTask));
    Memory()->LeaveHeapScope();
    return true;
}

} // namespace OnlinePlay

using OnlinePlay::RoomManager;
