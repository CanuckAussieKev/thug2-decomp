// The GameSpy lobby (see lobby.h).
//
// The GameSpy Peer SDK (docs/library_ranges.md) is called by address; what each call does is said where it's made.

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
#include "hook/game.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (the original's sprintf / strcpy, kept)

namespace GameNet {

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

inline void** const kLobbyListVtable = reinterpret_cast<void**>(0x0064CBFC);  // Lst::Head<LobbyRoom>, LobbyRoom
inline void** const kPlayerListVtable = reinterpret_cast<void**>(0x0064CBF8); // Lst::Head<LobbyPlayer>, LobbyPlayer
inline void** const kTaskVtable = reinterpret_cast<void**>(0x006476F0);       // Tsk::Task<LobbyMan>

Manager* TheManager() { return GameVariable<Manager*>(0x007CCE14); }
LobbyMan* TheLobby() { return static_cast<LobbyMan*>(TheManager()->m_lobbyMan); }
Mem::Manager* Memory() { return Mem::Manager::Get(); }

// The GameSpy SDK (cdecl).
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

// The listed player named `nick` (case ignored), or null.
LobbyPlayer* FindPlayer(LobbyMan* lobby, const char* nick)
{
    for (Mem::ListNode* node = FirstNode(&lobby->m_players); node; node = NextNode(node)) {
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
LobbyPlayer* AddPlayer(LobbyMan* lobby, void* peer, const char* nick, bool listed)
{
    auto* player = static_cast<LobbyPlayer*>(Mem::Calloc(sizeof(LobbyPlayer)));
    if (player) {
        player->m_node.data = player;
        player->m_node.priority = 0;
        player->m_node.next = &player->m_node;
        player->m_node.prev = &player->m_node;
        player->m_node.vtable = kPlayerListVtable;
        player->m_profile = 0;
        player->m_rating = 0;
        player->m_listed = false;
    }
    std::strcpy(player->m_nick, nick);
    Sdk(0x005FCBA0, peer, nick, 0, &player->m_profile); // (the player's profile id)
    if (listed)
        player->m_listed = true;
    Mem::ListNode* head = &lobby->m_players;
    player->m_node.prev = head->prev;
    player->m_node.next = head;
    head->prev->next = &player->m_node;
    head->prev = &player->m_node;
    return player;
}

// Every node of `head`'s list deleted through its vtable.
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

// A player's rating from a room key and its rank, told to the player list.
void UpdateRank(LobbyPlayer* player, const char* value)
{
    player->m_rating = std::atol(value);
    double rank = static_cast<double>(player->m_rating) * GameVariable<float>(0x0064CC1C) * GameVariable<float>(0x00646710);
    int shown = static_cast<int>(rank); // (truncated, as _ftol)
    if (shown > kMaxRank)
        shown = kMaxRank;
    Script::CStruct* params = NewParams();
    params->AddChecksum(kId, Crc::GenerateCRCFromString(player->m_nick));
    params->AddInteger(kRank, shown);
    Run(kUpdateRank, params);
    DeleteParams(params);
}

// "<nick> <text>" to the console, with the flag `kind` (by name).
void ConsoleMessage(const char* nick, uint32_t textName, bool byName)
{
    char line[1024];
    std::sprintf(line, "%s %s", nick, Script::GetString(textName));
    Script::CStruct* params = NewParams();
    if (byName) {
        params->AddString("text", line);
        params->AddChecksum("join", 0);
    } else {
        params->AddString(kText, line);
        params->AddChecksum(kLeft, 0);
    }
    Run(kConsoleMessage, params);
    DeleteParams(params);
}

// A Tsk::Task<LobbyMan> running `code` (priority 0; null if out of memory).
Tsk::BaseTask* NewTask(LobbyMan* lobby, uint32_t code)
{
    auto* task = static_cast<Tsk::BaseTask*>(Mem::Calloc(0x20));
    if (!task)
        return nullptr;
    task->Construct(0);
    task->m_vtable = kTaskVtable;
    Field<uint32_t>(task, 0x18) = code;
    Field<LobbyMan*>(task, 0x1C) = lobby;
    return task;
}

} // namespace

// 0x00545860
LobbyRoom* LobbyRoom::Construct()
{
    m_node.data = this;
    m_node.priority = 0;
    m_node.next = &m_node;
    m_node.prev = &m_node;
    m_node.vtable = kLobbyListVtable;
    m_full = false;
    m_neversoft = false;
    m_outOfRange = false;
    return this;
}

// 0x00541870
LobbyMan* LobbyMan::Construct()
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
    m_thinkTask = NewTask(this, 0x00540040);
    m_natTask = NewTask(this, 0x005400B0);
    m_hostingTask = NewTask(this, 0x005400E0);
    // The server-browsing keys (GameSpy's qr2 key registration).
    Sdk(0x005FF660, 0x33, "numobservers");
    Sdk(0x005FF660, 0x34, "maxobservers");
    Sdk(0x005FF660, 0x35, "skilllevel");
    Sdk(0x005FF660, 0x36, "started");
    Sdk(0x005FF660, 0x37, "hostmode");
    Sdk(0x005FF660, 0x38, "ranked");
    Sdk(0x005FF660, 0x39, "rating_");
    return this;
}

// 0x00541AC0
LobbyMan* LobbyMan::Destroy(unsigned int flags)
{
    reinterpret_cast<LobbyListNode*>(this)->DestroyBody(); // (the destructor body: 0x00540860, its base node unlinked)
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x00542DF0
void LobbyMan::InitPeer()
{
    Memory()->PushContext(Memory()->m_networkHeap);
    // The SDK's callbacks (0x74 bytes; those not set stay null).
    uint32_t callbacks[0x1D] = {};
    callbacks[0x00 / 4] = 0x0053F570; // disconnected
    callbacks[0x04 / 4] = 0x00540B60; // roomMessage
    callbacks[0x14 / 4] = 0x005480A0; // (nothing: the empty function)
    callbacks[0x24 / 4] = reinterpret_cast<uint32_t>(&PlayerJoinedCallback);
    callbacks[0x28 / 4] = reinterpret_cast<uint32_t>(&PlayerLeftCallback);
    callbacks[0x38 / 4] = reinterpret_cast<uint32_t>(&PlayerInfoCallback);
    callbacks[0x30 / 4] = reinterpret_cast<uint32_t>(&NewPlayerListCallback);
    callbacks[0x4C / 4] = reinterpret_cast<uint32_t>(&RoomKeyChangedCallback);
    callbacks[0x68 / 4] = 0x00540C90;
    callbacks[0x5C / 4] = 0x0053F620;
    callbacks[0x50 / 4] = 0x0053F8D0;
    callbacks[0x54 / 4] = 0x0053FB80;
    callbacks[0x60 / 4] = 0x0053FEC0;
    callbacks[0x64 / 4] = 0x005480A0;
    callbacks[0x6C / 4] = 0x0053FDA0;
    callbacks[0x70 / 4] = reinterpret_cast<uint32_t>(this); // (their parameter)
    m_peer = Sdk<void*>(0x005FCE60, callbacks); // (the peer made)
    m_inGroupRoom = false;
    m_expectingDisconnect = true;
    m_hosting = false;
    m_restrictedHosting = false;
    Memory()->PopContext();
}

// 0x00541A10
void LobbyMan::StartListingGames()
{
    Manager* manager = TheManager();
    Memory()->PushContext(Memory()->m_internetBottomUpHeap);
    manager->SetServiceState(14); // (the server list's state)
    GameVariable<uint32_t>(0x007CE430) = 0;
    GameVariable<uint8_t>(0x007CCE38) = 0;
    GameVariable<uint8_t>(0x007CCE39) = 0;
    Sdk(0x005FD3E0, m_peer, "\\maxplayers\\neversoft\\maxrating\\minrating", 0x00541690u, this, 0); // (games listed)
    Memory()->PopContext();
}

// 0x005427E0
void LobbyMan::ClearLobbies()
{
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    if (m_peer)
        Sdk(0x005FCA40, m_peer); // (the group list stopped)
    DeleteAll(&m_rooms);
    Memory()->PopContext();
}

// 0x005428B0
void LobbyMan::FillLobbyList()
{
    Run(kDialogBoxExit);
    Run(kCreateLobbyListMenu);
    int index = 0;
    for (Mem::ListNode* node = FirstNode(&m_rooms); node; node = NextNode(node)) {
        auto* lobby = static_cast<LobbyRoom*>(node->data);
        if (!lobby)
            break;
        AddRoomMenuItem(lobby, index); // (its menu item)
        ++index;
    }
}

// 0x00542970
LobbyRoom* LobbyMan::GetLobby(int index)
{
    for (Mem::ListNode* node = FirstNode(&m_rooms); node; node = NextNode(node)) {
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
void LobbyMan::ClearPlayers()
{
    DeleteAll(&m_players);
    Run(kUpdatePlayerList);
}

// 0x00541BA0
void LobbyMan::FillPlayerList()
{
    Run(kDestroyUserChildren);
    for (Mem::ListNode* node = FirstNode(&m_players); node; node = NextNode(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data);
        if (!player)
            break;
        if (player->m_listed)
            AddPlayerListItem(player); // (its menu item)
    }
    Run(kUpdatePlayerList);
}

// 0x00541C70
void LobbyMan::FillProspectiveBuddyList()
{
    Manager* manager = TheManager();
    Run(kDestroyBuddyChildren);
    for (Mem::ListNode* node = FirstNode(&m_players); node; node = NextNode(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data);
        if (!player)
            break;
        int profile = player->m_profile;
        if (profile != Field<int>(manager->m_buddyMan, 0x04) && profile > 0 && // (0x005480C0: our own profile)
            !static_cast<BuddyMan*>(manager->m_buddyMan)->IsBuddy(profile)) // (a buddy already)
            AddBuddyMenuItem(player);                                        // (a prospective buddy)
    }
}

// 0x00542B10
void LobbyMan::RequestPlayerList()
{
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    ClearPlayers();
    Run(kDestroyUserChildren);
    Sdk(0x005FCB10, m_peer, kGroupRoom, &EnumPlayersCallback, this); // (the room's players)
    Memory()->PopContext();
}

// 0x00541D50
void __cdecl LobbyMan::PlayerJoinedCallback(void* peer, int roomType, const char* nick, LobbyMan* lobby)
{
    (void)roomType;
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    if (LobbyPlayer* known = FindPlayer(lobby, nick)) {
        Sdk(0x005FCBA0, peer, nick, 0, &known->m_profile);
        Memory()->PopContext();
        known->m_listed = true;
        return;
    }
    LobbyPlayer* player = AddPlayer(lobby, peer, nick, true);
    Run(kPrepareUserList);
    lobby->AddPlayerListItem(player); // (its menu item)
    Run(kUpdatePlayerList);
    ConsoleMessage(nick, kStatusJoined, true);
    Memory()->PopContext();
}

// 0x00541FD0
void __cdecl LobbyMan::PlayerInfoCallback(void* peer, int roomType, const char* nick, uint32_t ip, int profileId,
                                          LobbyMan* lobby)
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
void __cdecl LobbyMan::PlayerLeftCallback(void* peer, int roomType, const char* nick, const char* reason,
                                          LobbyMan* lobby)
{
    (void)peer;
    (void)roomType;
    (void)reason;
    if (LobbyPlayer* player = FindPlayer(lobby, nick)) {
        Script::CStruct* params = NewParams();
        params->AddChecksum(kUserId, Crc::GenerateCRCFromString(nick));
        Run(kDestroyUser, params);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(player->m_node.vtable[0])(player, 1);
        DeleteParams(params);
        Run(kUpdatePlayerList);
    }
    ConsoleMessage(nick, kStatusLeft, false);
}

// 0x00542B70
void __cdecl LobbyMan::NewPlayerListCallback(void* peer, int roomType, LobbyMan* lobby)
{
    (void)peer;
    (void)roomType;
    lobby->RequestPlayerList();
}

// 0x00542650
void __cdecl LobbyMan::RoomKeyChangedCallback(void* peer, int roomType, const char* nick, const char* key,
                                              const char* value, LobbyMan* lobby)
{
    (void)peer;
    (void)roomType;
    (void)key;
    if (LobbyPlayer* player = FindPlayer(lobby, nick))
        UpdateRank(player, value);
}

// 0x00542330
void __cdecl LobbyMan::RoomKeysCallback(void* peer, int success, int roomType, const char* nick, int count,
                                        char** keys, char** values, LobbyMan* lobby)
{
    (void)peer;
    (void)success;
    (void)roomType;
    if (count <= 0 || !keys[0] || _stricmp(keys[0], "b_rating") != 0)
        return;
    for (Mem::ListNode* node = FirstNode(&lobby->m_players); node; node = NextNode(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data);
        if (!player)
            break;
        if (_stricmp(player->m_nick, nick) == 0)
            UpdateRank(player, values[0]); // (every player of that nick)
    }
}

// 0x005424F0
void __cdecl LobbyMan::EnumPlayersCallback(void* peer, int success, int roomType, int index, const char* nick,
                                           int flags, LobbyMan* lobby)
{
    (void)roomType;
    (void)flags;
    if (!success || index == -1)
        return;
    if (FindPlayer(lobby, nick))
        return;
    Memory()->PushContext(Memory()->m_internetBottomUpHeap);
    AddPlayer(lobby, peer, nick, false);
    Memory()->PopContext();
}

// 0x00542B80
void __cdecl LobbyMan::JoinRoomCallback(void* peer, int success, int result, int roomType, LobbyMan* lobby)
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
    static_cast<BuddyMan*>(manager->m_buddyMan)->SetStatus(4, Script::GetString("homie_status_chatting"), lobby->m_roomName);
    // Our room key b_rating: the stats' rating (0x0054CA80 of the stats manager, read through its pointer).
    char key[64];
    char value[64];
    std::sprintf(key, "b_rating");
    int* rating = &static_cast<StatsMan*>(manager->m_statsMan)->GetPersonalStats()->m_rating;
    std::sprintf(value, "%d", *rating);
    char* keys[1] = {key};
    char* values[1] = {value};
    const char* ourNick = Sdk<const char*>(0x005FC920, peer); // (our nick)
    Sdk(0x005FCDF0, peer, roomType, ourNick, 1, keys, values); // (our room keys set)
    Sdk(0x005FD4A0, peer, roomType, "*", 1, keys, &RoomKeysCallback, lobby, 0); // (everyone's asked for)
}

// --- The script commands ---

// 0x00541A70: num_players = the players listed in the lobby.
bool __cdecl CFunc_NumPlayersInLobby(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    int count = 0;
    for (Mem::ListNode* node = FirstNode(&TheLobby()->m_players); node; node = NextNode(node))
        ++count;
    script->m_params->AddInteger(kNumPlayers, count);
    return true;
}

// 0x005429C0: out of the lobby and GameSpy (the peer left, disconnected and shut down - 0x005FCCA0,
// 0x005FC7B0, 0x005FD670), the lists emptied, two of its tasks removed, then 0x00540750.
bool __cdecl CFunc_LobbyDisconnect(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = TheManager();
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    LobbyMan* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    while (lobby->m_negotiations) { // (counted down one at a time, as the original - a negative count takes long)
        --lobby->m_negotiations;
        lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    }
    static_cast<LobbyMan*>(manager->m_lobbyMan)->ClearLobbies();
    static_cast<LobbyMan*>(manager->m_lobbyMan)->ClearPlayers();
    static_cast<Tsk::BaseTask*>(static_cast<LobbyMan*>(manager->m_lobbyMan)->m_thinkTask)->Remove();
    static_cast<Tsk::BaseTask*>(static_cast<LobbyMan*>(manager->m_lobbyMan)->m_natTask)->Remove();
    lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    lobby->m_inGroupRoom = false;
    lobby->m_expectingDisconnect = true;
    if (lobby->m_peer) {
        Sdk(0x005FCCA0, lobby->m_peer); // (the room left)
        Sdk(0x005FC7B0, lobby->m_peer); // (disconnected)
        Sdk(0x005FD670, lobby->m_peer); // (shut down)
    }
    static_cast<LobbyMan*>(manager->m_lobbyMan)->Disconnect();
    Memory()->PopContext();
    return true;
}

// 0x00542AA0: the lobby's player list shown (the manager's singleton taken and given back - 0x0053F520 /
// 0x005395C0).
bool __cdecl CFunc_FillPlayerList(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = Manager::Create(false);
    static_cast<LobbyMan*>(manager->m_lobbyMan)->FillPlayerList();
    ReleaseManager();
    return true;
}

// 0x00542B00
bool __cdecl CFunc_FillLobbyProspectiveBuddyList(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    (void)script;
    TheLobby()->FillProspectiveBuddyList();
    return true;
}

// 0x00542CE0: back into the chosen lobby's group room, if any (GameSpy's 0x005FD220, JoinRoomCallback).
bool __cdecl CFunc_RejoinLobby(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = TheManager();
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    LobbyMan* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    if (lobby->m_groupId)
        Sdk(0x005FD220, lobby->m_peer, lobby->m_groupId, &LobbyMan::JoinRoomCallback, lobby, 0);
    Memory()->PopContext();
    return true;
}

// 0x00542D30: lobby `index` chosen (no null check: as the original) - its group room, numbers, name and flag kept,
// the room joined (JoinRoomCallback), the lobby list emptied.
bool __cdecl CFunc_ChooseLobby(Script::CStruct* params, Script::CScript* script)
{
    (void)script;
    Manager* manager = TheManager();
    int index = 0;
    Memory()->PushContext(Memory()->m_internetTopDownHeap);
    params->GetInteger(kIndex, &index, 0);
    LobbyMan* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    LobbyRoom* chosen = lobby->GetLobby(index);
    lobby->m_groupId = chosen->m_groupId;
    lobby->m_unknown30 = chosen->m_numPlayers;
    lobby->m_unknown2C = chosen->m_maxPlayers;
    std::strcpy(lobby->m_roomName, chosen->m_name);
    lobby->m_restrictedHosting = chosen->m_neversoft;
    Sdk(0x005FD220, lobby->m_peer, lobby->m_groupId, &LobbyMan::JoinRoomCallback, lobby, 0);
    lobby->ClearLobbies();
    Memory()->PopContext();
    return true;
}

// 0x00542EE0: the peer made, the title set (GameSpy's 0x005FCF30: the game name for the title and the server list -
// Net::GameName: OnlineGameName or thps6pc -, its secret key - stored bitwise-NOT at 0x0067F154, where
// online_identity.cpp writes OnlineGameKey -, version 6, 15 updates, NAT negotiation, pinging the group room only),
// the connecting thread started (0x00540AB0, suspended first so its id becomes the lobby id - 0x00533240), and the
// lobby's first task added to the main loop's logic tasks.
// (The original pushes the game name's address, 0x0064B1CC; online_identity.cpp wrote the configured name's address
// over those two operands before this was source.)
bool __cdecl CFunc_LobbyConnect(Script::CStruct* params, Script::CScript* script)
{
    (void)params;
    (void)script;
    Manager* manager = TheManager();
    auto* logic = reinterpret_cast<Tsk::Stack*>(GameVariable<uint8_t*>(0x00696D68) + 0x04); // (the logic tasks)
    DWORD threadId = 0;
    BOOL pingRooms[3] = {FALSE, TRUE, FALSE};
    BOOL crossPingRooms[3] = {FALSE, FALSE, FALSE};
    Memory()->PushContext(Memory()->m_internetBottomUpHeap);
    LobbyMan* lobby = static_cast<LobbyMan*>(manager->m_lobbyMan);
    lobby->InitPeer();
    const uint8_t* stored = reinterpret_cast<const uint8_t*>(0x0067F154);
    char key[7];
    for (int i = 0; i < 6; ++i)
        key[i] = static_cast<char>(~stored[i]);
    key[6] = '\0';
    const char* name = Net::GameName();
    Sdk(0x005FCF30, static_cast<LobbyMan*>(manager->m_lobbyMan)->m_peer, name, key, name, key, 6, 15, 1, pingRooms,
        crossPingRooms);
    HANDLE thread = CreateThread(nullptr, 0x8000, reinterpret_cast<LPTHREAD_START_ROUTINE>(0x00540AB0),
                                 manager->m_lobbyMan, CREATE_SUSPENDED, &threadId);
    manager->SetLobbyId(threadId);
    ResumeThread(thread);
    logic->AddTask(static_cast<Tsk::BaseTask*>(static_cast<LobbyMan*>(manager->m_lobbyMan)->m_thinkTask));
    Memory()->PopContext();
    return true;
}

} // namespace GameNet

using GameNet::LobbyMan;
REPLACE(0x00545860, &GameNet::LobbyRoom::Construct);
REPLACE(0x00541870, &LobbyMan::Construct);
REPLACE(0x00541AC0, &LobbyMan::Destroy);
REPLACE(0x00542DF0, &LobbyMan::InitPeer);
REPLACE(0x00541A10, &LobbyMan::StartListingGames);
REPLACE(0x005427E0, &LobbyMan::ClearLobbies);
REPLACE(0x005428B0, &LobbyMan::FillLobbyList);
REPLACE(0x00542970, &LobbyMan::GetLobby);
REPLACE(0x00541AF0, &LobbyMan::ClearPlayers);
REPLACE(0x00541BA0, &LobbyMan::FillPlayerList);
REPLACE(0x00541C70, &LobbyMan::FillProspectiveBuddyList);
REPLACE(0x00542B10, &LobbyMan::RequestPlayerList);
REPLACE(0x00541D50, LobbyMan::PlayerJoinedCallback);
REPLACE(0x00541FD0, LobbyMan::PlayerInfoCallback);
REPLACE(0x005420C0, LobbyMan::PlayerLeftCallback);
REPLACE(0x00542B70, LobbyMan::NewPlayerListCallback); // (not a Ghidra function)
REPLACE(0x00542650, LobbyMan::RoomKeyChangedCallback);
REPLACE(0x00542330, LobbyMan::RoomKeysCallback);      // (not a Ghidra function)
REPLACE(0x005424F0, LobbyMan::EnumPlayersCallback);
REPLACE(0x00542B80, LobbyMan::JoinRoomCallback);      // (not a Ghidra function)
REPLACE(0x00541A70, GameNet::CFunc_NumPlayersInLobby);
REPLACE(0x005429C0, GameNet::CFunc_LobbyDisconnect);
REPLACE(0x00542AA0, GameNet::CFunc_FillPlayerList);
REPLACE(0x00542B00, GameNet::CFunc_FillLobbyProspectiveBuddyList);
REPLACE(0x00542CE0, GameNet::CFunc_RejoinLobby);
REPLACE(0x00542D30, GameNet::CFunc_ChooseLobby);
REPLACE(0x00542EE0, GameNet::CFunc_LobbyConnect);
