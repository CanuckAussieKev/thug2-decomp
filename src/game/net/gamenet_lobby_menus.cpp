// The GameSpy lobby's menus and chat (GameNet::LobbyMan): the buddy / player list / regions menu items, the room
// messages, SendMessage, the group room list; and the lobby's destructors (0x005407B0-0x00541690). See gamenet_lobby.h.

#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/lst/head.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/stats_man.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (sprintf / strncpy: the original's own)

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint32_t kText = 0xC4745838;                // text
constexpr uint32_t kPadChooseScript = 0x2B0CFCA1;     // pad_choose_script
constexpr uint32_t kPadChooseParams = 0xB8437568;     // pad_choose_params
constexpr uint32_t kParentMenuId = 0x416062AB;        // parent_menu_id
constexpr uint32_t kFocusScript = 0x5A8C82DB;         // focus_script
constexpr uint32_t kUnfocusParams = 0xF974BF7C;       // unfocus_params
constexpr uint32_t kProfile = 0x7EA855F0;             // profile
constexpr uint32_t kNick = 0xD6F4D0C8;                // nick
constexpr uint32_t kNetName = 0xD3A5478A;             // net_name
constexpr uint32_t kIndex = 0x7F8C98FE;               // index
constexpr uint32_t kRank = 0x7786171A;                // rank
constexpr uint32_t kId = 0x40C698AF;                  // id
constexpr uint32_t kScale = 0x13B9DA7B;               // scale
constexpr uint32_t kRgba = 0x3F6BCDBA;                // rgba
constexpr uint32_t kSystemMessage = 0xA531B313;       // system_message
// Scripts and menus.
constexpr uint32_t kLobbyAddBuddy = 0xD7059EC7;       // lobby_add_buddy
constexpr uint32_t kBuddyMenu = 0x750C8BC0;           // (the buddy menu the item goes in)
constexpr uint32_t kMakeTextSubMenuItem = 0xBEED50B6; // make_text_sub_menu_item
constexpr uint32_t kCreateConsoleMessage = 0xCCB19938; // create_console_message
constexpr uint32_t kRegionsMenuAddItem = 0xB0DA8BB7;  // regions_menu_add_item
constexpr uint32_t kChooseSelectedLobby = 0x8DE96379; // choose_selected_lobby
constexpr uint32_t kRegionsMenuSetFocus = 0x36258921; // regions_menu_set_focus
constexpr uint32_t kNotFocusable = 0x1D944426;        // not_focusable
constexpr uint32_t kUnavailableLobby = 0xBA468C44;    // (chosen on a full / out of range room)
constexpr uint32_t kNetLobbyFull = 0x2767F99B;        // net_lobby_full
constexpr uint32_t kAddBuddyPrompt = 0xD094CCB0;      // add_buddy_prompt
constexpr uint32_t kCantAddBuddyPrompt1 = 0x077E365A; // cant_add_buddy_prompt_1 (logged in, the player has no profile)
constexpr uint32_t kCantAddBuddyPrompt2 = 0x9E7767E0; // cant_add_buddy_prompt_2 (not logged in)
constexpr uint32_t kCantAddBuddyPrompt3 = 0xE9705776; // cant_add_buddy_prompt_3 (30 buddies)
constexpr uint32_t kCantAddSelfPrompt = 0xB11CCDA6;   // cant_add_self_to_buddy_prompt
constexpr uint32_t kAlreadyBuddyPrompt = 0x68635516;  // already_buddy_prompt
constexpr uint32_t kLobbyPlayerListMenu = 0xECA96FAF; // lobby_player_list_menu
constexpr uint32_t kAddLobbyPlayer = 0x459148C5;      // (the player list's item script)

constexpr int kMaxBuddies = 30;
constexpr int kMaxRank = 10;
constexpr int kGroupRoom = 1;
constexpr int kLobbyStateListed = 15;
constexpr int kLobbyStateFailed = 16;
constexpr float kItemScale = 1.1f; // (0x3F8CCCCD)

Manager* NetManager() { return g_gameNetManager; }
void PushInternetHeap()
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_internetTopDownHeap);
}
void PopHeap() { Mem::Manager::Get()->PopContext(); }

// A script array of four integers (an rgba).
Script::CArray* NewScriptArray()
{
    Script::CArray* memory = Script::NewArray();
    return memory ? memory->Init() : nullptr;
}
void DeleteScriptArray(Script::CArray* array)
{
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
}
void SetRgba(Script::CArray* rgba, int r, int g, int b, int a)
{
    rgba->SetElement(0, r);
    rgba->SetElement(1, g);
    rgba->SetElement(2, b);
    rgba->SetElement(3, a);
}

// The presence (GameNet's BuddyMan: +4 the player's profile, +0x10 logged in).
bool LoggedIn(void* buddy) { return Field<bool>(buddy, 0x10); }
int OwnProfile(void* buddy) { return Field<int>(buddy, 4); }

} // namespace

// 0x00540900
void LobbyMan::AddBuddyMenuItem(LobbyPlayer* player)
{
    PushInternetHeap();
    Script::CStruct* params = NewParams();
    params->AddString(kText, player->m_nick);
    params->AddChecksum(kPadChooseScript, kLobbyAddBuddy);
    params->AddChecksum(kParentMenuId, kBuddyMenu);
    Script::CStruct* choose = NewParams();
    choose->AddInteger(kProfile, player->m_profile);
    choose->AddString(kNick, player->m_nick);
    choose->AddString(kNetName, player->m_nick);
    params->AddStructure(kPadChooseParams, choose);
    Run(kMakeTextSubMenuItem, params);
    DeleteParams(params);
    DeleteParams(choose);
    PopHeap();
}

// 0x00540B60
void __cdecl LobbyMan::RoomMessageCallback(void*, int, const char* nick, const char* message, int, void*)
{
    PushInternetHeap();
    char text[1024];
    std::sprintf(text, "%s: %s", nick, message);
    Script::CStruct* params = NewParams();
    params->AddString(kText, text);
    Run(kCreateConsoleMessage, params);
    DeleteParams(params);
    PopHeap();
}

// 0x00540E00
bool __cdecl CFunc_SendMessage(Script::CStruct* params, Script::CScript*)
{
    Manager* manager = NetManager();
    const char* message = nullptr;
    if (!params->GetString(kText, &message, 0))
        return true;
    if (params->ContainsFlag(kSystemMessage)) {
        char text[1024];
        std::sprintf(text, "<System> %s", message);
        Script::CStruct* console = NewParams();
        console->AddString(kText, text);
        Run(kCreateConsoleMessage, console);
        DeleteParams(console);
        return true;
    }
    PushInternetHeap();
    GameFunction<void(__cdecl*)(void*, int, const char*, int)>(0x005FCA80)(
        static_cast<LobbyMan*>(manager->m_lobbyMan)->m_peer, kGroupRoom, message, 0); // to the group room
    PopHeap();
    return true;
}

// 0x00540FB0
void LobbyMan::AddRoomMenuItem(LobbyRoom* room, int index)
{
    PushInternetHeap();
    char text[64];
    if (room->m_full)
        std::sprintf(text, "%s - %d/%d (%s)", room->m_name, room->m_numPlayers, room->m_maxPlayers,
                     Script::GetString(kNetLobbyFull));
    else
        std::sprintf(text, "%s - %d/%d", room->m_name, room->m_numPlayers, room->m_maxPlayers);
    Script::CStruct* params = NewParams();
    params->AddString(kText, text);
    bool available = !room->m_full && !room->m_outOfRange;
    if (available) {
        params->AddChecksum(kPadChooseScript, kChooseSelectedLobby);
        params->AddChecksum(kFocusScript, kRegionsMenuSetFocus);
    } else {
        params->AddChecksum(0u, kNotFocusable);
        params->AddChecksum(kPadChooseScript, kUnavailableLobby);
    }
    Script::CStruct* choose = NewParams();
    choose->AddInteger(kIndex, index);
    if (available)
        params->AddStructure(kPadChooseParams, choose);
    Run(kRegionsMenuAddItem, params);
    DeleteParams(params);
    DeleteParams(choose);
    PopHeap();
}

// 0x005411E0
void LobbyMan::AddPlayerListItem(LobbyPlayer* player)
{
    Manager* manager = NetManager();
    PushInternetHeap();
    Script::CStruct* choose = NewParams();
    Script::CStruct* params = NewParams();
    Script::CArray* rgba = NewScriptArray();
    Script::CStruct* unfocus = NewParams();
    params->AddString(kText, player->m_nick);
    rgba->SetSizeAndType(4, Script::ESYMBOLTYPE_INTEGER);
    void* buddy = manager->m_buddyMan;
    uint32_t script;
    if (player->m_profile && LoggedIn(buddy)) {
        if (player->m_profile != OwnProfile(buddy) &&
            !static_cast<BuddyMan*>(buddy)->IsBuddy(player->m_profile)) { // not a buddy yet
            SetRgba(rgba, 0, 128, 0, 128);
            choose->AddInteger(kProfile, player->m_profile);
            choose->AddString(kNick, player->m_nick);
            choose->AddString(kNetName, player->m_nick);
            params->AddStructure(kPadChooseParams, choose);
            script = static_cast<BuddyMan*>(buddy)->CountBuddies() < kMaxBuddies ? kAddBuddyPrompt : kCantAddBuddyPrompt3;
        } else if (OwnProfile(buddy) == player->m_profile) {
            SetRgba(rgba, 0, 0, 128, 128);
            script = kCantAddSelfPrompt;
        } else {
            SetRgba(rgba, 128, 128, 0, 128);
            script = kAlreadyBuddyPrompt;
        }
    } else {
        SetRgba(rgba, 128, 128, 128, 128);
        script = LoggedIn(buddy) ? kCantAddBuddyPrompt1 : kCantAddBuddyPrompt2;
    }
    params->AddChecksum(kPadChooseScript, script);
    // (x87: the rating times two floats, truncated)
    int rank = static_cast<int>(static_cast<double>(player->m_rating) * static_cast<double>(g_rankScale) *
                                static_cast<double>(GameVariable<float>(0x00646710)));
    if (rank > kMaxRank)
        rank = kMaxRank;
    params->AddInteger(kRank, rank);
    params->AddChecksum(kId, Crc::GenerateCRCFromString(player->m_nick));
    params->AddChecksum(kParentMenuId, kLobbyPlayerListMenu);
    params->AddFloat(kScale, kItemScale);
    unfocus->AddArray(kRgba, rgba);
    params->AddArray(kRgba, rgba);
    params->AddStructure(kUnfocusParams, unfocus);
    Run(kAddLobbyPlayer, params);
    DeleteScriptArray(rgba);
    DeleteParams(choose);
    DeleteParams(params);
    DeleteParams(unfocus);
    PopHeap();
}

GAME_GLOBAL(bool, g_listingAsked, 0x007CCE38);
GAME_GLOBAL(bool, g_listingFound, 0x007CCE39);

// 0x00541690
void __cdecl LobbyMan::GroupRoomCallback(void*, int success, int groupId, void* server, const char* name,
                                         int numWaiting, int maxWaiting, int numGames, int, void* param)
{
    Manager* manager = NetManager();
    auto setState = [manager](int state) { manager->SetServiceState(state); };
    if (!success) {
        setState(kLobbyStateFailed);
        return;
    }
    bool& asked = g_listingAsked;    // a room was asked for (by its lobby_num)
    bool& found = g_listingFound;
    if (groupId == 0) { // the list's end
        setState(!asked || found ? kLobbyStateListed : kLobbyStateFailed);
        return;
    }
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_internetBottomUpHeap);
    void* block = Mem::Calloc(sizeof(LobbyRoom));
    auto* room = block ? static_cast<LobbyRoom*>(block)->Construct() : nullptr;
    std::strncpy(room->m_name, name, sizeof(room->m_name) - 1);
    room->m_name[sizeof(room->m_name) - 1] = '\0';
    room->m_numPlayers = numGames;
    room->m_groupId = groupId;
    room->m_maxPlayers = maxWaiting;
    if (numWaiting >= maxWaiting)
        room->m_full = true;
    auto serverInt = [server](const char* key, int fallback) {
        return GameFunction<int(__cdecl*)(void*, const char*, int)>(0x005FF560)(server, key, fallback);
    };
    int lobby = serverInt("lobby_num", 0);
    if (lobby) {
        uint32_t size;
        void* data = manager->GetPendingData(&size);
        if (static_cast<uint32_t>(lobby) == Crc::GenerateCRCCaseless(static_cast<const char*>(data), static_cast<int>(size)))
            found = true;
        asked = true;
    }
    room->m_maxRating = serverInt("maxrating", 100000);
    room->m_minRating = serverInt("minrating", 0);
    auto rating = [manager] {
        // (the original reads the stats' first word, the rating, through the folded getter 0x00469490 -
        // Spt::PointerMember::Get)
        return static_cast<StatsMan*>(manager->m_statsMan)->GetPersonalStats()->m_rating;
    };
    if (rating() > room->m_maxRating)
        room->m_outOfRange = true;
    if (rating() < room->m_minRating)
        room->m_outOfRange = true;
    if (serverInt("neversoft", 0))
        room->m_neversoft = true;
    if (room->m_numPlayers > room->m_maxPlayers)
        room->m_maxPlayers = room->m_numPlayers;
    reinterpret_cast<Mem::ListNode*>(room)->priority = -groupId;
    static_cast<Lst::Head*>(param)->AddNode(reinterpret_cast<Mem::ListNode*>(room));
    Mem::Manager::Get()->PopContext();
}

// --- the destructors ---

// 0x00540D50
void LobbyMan::DestroyBody()
{
    for (Tsk::BaseTask* task : {m_thinkTask, m_natTask, m_hostingTask})
        if (task) // (the scalar deleting destructor, vtable slot 2)
            reinterpret_cast<void(__thiscall*)(Tsk::BaseTask*, unsigned int)>(task->m_vtable[2])(task, 1);
    m_players.UnlinkAs(reinterpret_cast<void**>(0x0064CBDC));
    reinterpret_cast<Mem::ListNode*>(this)->UnlinkAs(reinterpret_cast<void**>(0x0064CBE0));
}

// 0x00540840
void LobbyNode::DestroyBody() { UnlinkAs(reinterpret_cast<void**>(0x0064CBE0)); }
// 0x00540860
void LobbyListNode::DestroyBody() { UnlinkAs(reinterpret_cast<void**>(0x0064CBDC)); }

// 0x00540880
LobbyNode* LobbyNode::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005408C0
LobbyListNode* LobbyListNode::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00540F80
LobbyNode* LobbyNode::DestroyOther(unsigned int flags) { return Destroy(flags); }

// 0x005407E0
LobbySearchA* LobbySearchA::Destroy(unsigned int flags)
{
    m_vtable = reinterpret_cast<void**>(0x0064CB6C);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00540810
LobbySearchB* LobbySearchB::Destroy(unsigned int flags)
{
    m_vtable = reinterpret_cast<void**>(0x0064CB70);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005407B0
Tsk::BaseTask* TaskDeleter::Destroy(unsigned int flags)
{
    auto* task = reinterpret_cast<Tsk::BaseTask*>(this);
    task->Destruct(); // (Task<T>'s destructor, 0x00577290, is a jump to it)
    if (flags & 1)
        Mem::Manager::Get()->Free(task);
    return task;
}

} // namespace GameNet

REPLACE(0x00540900, &GameNet::LobbyMan::AddBuddyMenuItem);
REPLACE(0x00540B60, GameNet::LobbyMan::RoomMessageCallback);
REPLACE(0x00540E00, GameNet::CFunc_SendMessage);
REPLACE(0x00540FB0, &GameNet::LobbyMan::AddRoomMenuItem);
REPLACE(0x005411E0, &GameNet::LobbyMan::AddPlayerListItem);
REPLACE(0x00541690, GameNet::LobbyMan::GroupRoomCallback);
REPLACE(0x00540D50, &GameNet::LobbyMan::DestroyBody);
REPLACE(0x00540840, &GameNet::LobbyNode::DestroyBody);
REPLACE(0x00540860, &GameNet::LobbyListNode::DestroyBody);
REPLACE(0x00540880, &GameNet::LobbyNode::Destroy);
REPLACE(0x005408C0, &GameNet::LobbyListNode::Destroy);
REPLACE(0x00540F80, &GameNet::LobbyNode::DestroyOther);
REPLACE(0x005407E0, &GameNet::LobbySearchA::Destroy);
REPLACE(0x00540810, &GameNet::LobbySearchB::Destroy);
REPLACE(0x005407B0, &GameNet::TaskDeleter::Destroy);
