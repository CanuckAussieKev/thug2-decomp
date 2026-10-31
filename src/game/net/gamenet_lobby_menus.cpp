// The GameSpy lobby's menus and chat (OnlinePlay::RoomManager): the buddy / player list / regions menu items, the room
// messages, SendMessage, the group room list; and the lobby's destructors (0x005407B0-0x00541690). See gamenet_lobby.h.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "game/core/checksums.h"
#include "game/net/gamespy.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task_list.h"
#include "game/lists/chain_start.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/stats_man.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/gfx/loading_bar.h"

#pragma warning(disable : 4996) // (sprintf / strncpy: the original's own)

namespace OnlinePlay {

static VtableSlot kLobbySearchAVtable_slots[] = {&OnlinePlay::LobbySearchA::Teardown};
void** const kLobbySearchAVtable = reinterpret_cast<void**>(kLobbySearchAVtable_slots);
static VtableSlot kLobbySearchBVtable_slots[] = {&OnlinePlay::LobbySearchB::Teardown};
void** const kLobbySearchBVtable = reinterpret_cast<void**>(kLobbySearchBVtable_slots);
static VtableSlot kLobbyListNodeVtable_slots[] = {&OnlinePlay::LobbyListNode::Teardown};
void** const kLobbyListNodeVtable = reinterpret_cast<void**>(kLobbyListNodeVtable_slots);
static VtableSlot kLobbyNodeVtable_slots[] = {&OnlinePlay::LobbyNode::Teardown};
void** const kLobbyNodeVtable = reinterpret_cast<void**>(kLobbyNodeVtable_slots);

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

Hub* NetManager() { return g_gameNetManager; }
void PushInternetHeap()
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_internetTopDownHeap);
}
void PopHeap() { MemSys::Hub::Pull()->LeaveHeapScope(); }

// A script array of four integers (an rgba).
Scripts::CArray* NewScriptArray()
{
    Scripts::CArray* memory = Scripts::NewArray();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteScriptArray(Scripts::CArray* array)
{
    if (array) {
        array->Clear();
        Scripts::DeleteArray(array);
    }
}
void SetRgba(Scripts::CArray* rgba, int r, int g, int b, int a)
{
    rgba->SetElement(0, r);
    rgba->SetElement(1, g);
    rgba->SetElement(2, b);
    rgba->SetElement(3, a);
}

// The presence (OnlinePlay's FriendsManager: +4 the player's profile, +0x10 logged in).
bool LoggedIn(void* buddy) { return Prop<bool>(buddy, 0x10); }
int OwnProfile(void* buddy) { return Prop<int>(buddy, 4); }

} // namespace

// 0x00540900
void RoomManager::AddBuddyMenuItem(LobbyPlayer* player)
{
    PushInternetHeap();
    Scripts::CStruct* params = NewParams();
    params->AppendWord(kText, player->m_nick);
    params->PutNameKey(kPadChooseScript, kLobbyAddBuddy);
    params->PutNameKey(kParentMenuId, kBuddyMenu);
    Scripts::CStruct* choose = NewParams();
    choose->PutInt(kProfile, player->m_account);
    choose->AppendWord(kNick, player->m_nick);
    choose->AppendWord(kNetName, player->m_nick);
    params->PutSubStruct(kPadChooseParams, choose);
    Run(kMakeTextSubMenuItem, params);
    DeleteParams(params);
    DeleteParams(choose);
    PopHeap();
}

// 0x00540B60
void __cdecl RoomManager::RoomMessageCallback(void*, int, const char* nick, const char* message, int, void*)
{
    PushInternetHeap();
    char text[1024];
    std::sprintf(text, "%s: %s", nick, message);
    Scripts::CStruct* params = NewParams();
    params->AppendWord(kText, text);
    Run(kCreateConsoleMessage, params);
    DeleteParams(params);
    PopHeap();
}

// 0x00540E00
bool __cdecl CFunc_SendMessage(Scripts::CStruct* params, Scripts::CScript*)
{
    Hub* manager = NetManager();
    const char* message = nullptr;
    if (!params->FetchText(kText, &message, 0))
        return true;
    if (params->HasBareFlag(kSystemMessage)) {
        char text[1024];
        std::sprintf(text, "<System> %s", message);
        Scripts::CStruct* console = NewParams();
        console->AppendWord(kText, text);
        Run(kCreateConsoleMessage, console);
        DeleteParams(console);
        return true;
    }
    PushInternetHeap();
    GameSpy::PeerMessageRoom(
        static_cast<RoomManager*>(manager->m_lobbyMan)->m_remote, kGroupRoom, message, 0); // to the group room
    PopHeap();
    return true;
}

// 0x00540FB0
void RoomManager::AddRoomMenuItem(LobbyRoom* room, int index)
{
    PushInternetHeap();
    char text[64];
    if (room->m_full)
        std::sprintf(text, "%s - %d/%d (%s)", room->m_label, room->m_numPlayers, room->m_maxPlayers,
                     Scripts::FetchText(kNetLobbyFull));
    else
        std::sprintf(text, "%s - %d/%d", room->m_label, room->m_numPlayers, room->m_maxPlayers);
    Scripts::CStruct* params = NewParams();
    params->AppendWord(kText, text);
    bool available = !room->m_full && !room->m_outOfRange;
    if (available) {
        params->PutNameKey(kPadChooseScript, kChooseSelectedLobby);
        params->PutNameKey(kFocusScript, kRegionsMenuSetFocus);
    } else {
        params->PutNameKey(0u, kNotFocusable);
        params->PutNameKey(kPadChooseScript, kUnavailableLobby);
    }
    Scripts::CStruct* choose = NewParams();
    choose->PutInt(kIndex, index);
    if (available)
        params->PutSubStruct(kPadChooseParams, choose);
    Run(kRegionsMenuAddItem, params);
    DeleteParams(params);
    DeleteParams(choose);
    PopHeap();
}

// 0x005411E0
void RoomManager::AddPlayerListItem(LobbyPlayer* player)
{
    Hub* manager = NetManager();
    PushInternetHeap();
    Scripts::CStruct* choose = NewParams();
    Scripts::CStruct* params = NewParams();
    Scripts::CArray* rgba = NewScriptArray();
    Scripts::CStruct* unfocus = NewParams();
    params->AppendWord(kText, player->m_nick);
    rgba->ConfigureSizeKind(4, Scripts::QBVAL_INTEGER);
    void* buddy = manager->m_buddyMan;
    uint32_t script;
    if (player->m_account && LoggedIn(buddy)) {
        if (player->m_account != OwnProfile(buddy) &&
            !static_cast<FriendsManager*>(buddy)->IsBuddy(player->m_account)) { // not a buddy yet
            SetRgba(rgba, 0, 128, 0, 128);
            choose->PutInt(kProfile, player->m_account);
            choose->AppendWord(kNick, player->m_nick);
            choose->AppendWord(kNetName, player->m_nick);
            params->PutSubStruct(kPadChooseParams, choose);
            script = static_cast<FriendsManager*>(buddy)->CountBuddies() < kMaxBuddies ? kAddBuddyPrompt : kCantAddBuddyPrompt3;
        } else if (OwnProfile(buddy) == player->m_account) {
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
    params->PutNameKey(kPadChooseScript, script);
    // (x87: the rating times two floats, truncated)
    int rank = static_cast<int>(static_cast<double>(player->m_skillPoints) * static_cast<double>(g_rankScale) *
                                static_cast<double>(Engine3D::g_floatTen));
    if (rank > kMaxRank)
        rank = kMaxRank;
    params->PutInt(kRank, rank);
    params->PutNameKey(kId, Checksums::HashFromText(player->m_nick));
    params->PutNameKey(kParentMenuId, kLobbyPlayerListMenu);
    params->PushFloat(kScale, kItemScale);
    unfocus->AppendArrayValue(kRgba, rgba);
    params->AppendArrayValue(kRgba, rgba);
    params->PutSubStruct(kUnfocusParams, unfocus);
    Run(kAddLobbyPlayer, params);
    DeleteScriptArray(rgba);
    DeleteParams(choose);
    DeleteParams(params);
    DeleteParams(unfocus);
    PopHeap();
}

bool g_listingAsked;
bool g_listingFound;

// 0x00541690
void __cdecl RoomManager::GroupRoomCallback(void*, int success, int groupId, void* server, const char* name,
                                         int numWaiting, int maxWaiting, int numGames, int, void* param)
{
    Hub* manager = NetManager();
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
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_internetBottomUpHeap);
    void* block = MemSys::AllocZeroed(sizeof(LobbyRoom));
    auto* room = block ? static_cast<LobbyRoom*>(block)->MakeNew() : nullptr;
    std::strncpy(room->m_label, name, sizeof(room->m_label) - 1);
    room->m_label[sizeof(room->m_label) - 1] = '\0';
    room->m_numPlayers = numGames;
    room->m_groupId = groupId;
    room->m_maxPlayers = maxWaiting;
    if (numWaiting >= maxWaiting)
        room->m_full = true;
    auto serverInt = [server](const char* key, int fallback) {
        return GameSpy::ServerGetIntValue(server, key, fallback);
    };
    int lobby = serverInt("lobby_num", 0);
    if (lobby) {
        uint32_t size;
        void* data = manager->GetPendingData(&size);
        if (static_cast<uint32_t>(lobby) == Checksums::GenerateCRCCaseless(static_cast<const char*>(data), static_cast<int>(size)))
            found = true;
        asked = true;
    }
    room->m_maxRating = serverInt("maxrating", 100000);
    room->m_minRating = serverInt("minrating", 0);
    auto rating = [manager] {
        // (the original reads the stats' first word, the rating, through the folded getter 0x00469490 -
        // Singletons::PointerMember::Pull)
        return static_cast<RankingsManager*>(manager->m_statsMan)->GetPersonalStats()->m_skillPoints;
    };
    if (rating() > room->m_maxRating)
        room->m_outOfRange = true;
    if (rating() < room->m_minRating)
        room->m_outOfRange = true;
    if (serverInt("neversoft", 0))
        room->m_neversoft = true;
    if (room->m_numPlayers > room->m_maxPlayers)
        room->m_maxPlayers = room->m_numPlayers;
    reinterpret_cast<MemSys::ListNode*>(room)->priority = -groupId;
    static_cast<ListKit::Foremost*>(param)->AppendVertex(reinterpret_cast<MemSys::ListNode*>(room));
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// --- the destructors ---

// 0x00540D50
void RoomManager::DestroyBody()
{
    for (TaskList::SchedulerTask* task : {m_thinkTask, m_natTask, m_hostingTask})
        if (task) // (the scalar deleting destructor, vtable slot 2)
            reinterpret_cast<void(__thiscall*)(TaskList::SchedulerTask*, unsigned int)>(task->m_vtable[2])(task, 1);
    m_participants.UnlinkAs(kLobbyListNodeVtable);
    reinterpret_cast<MemSys::ListNode*>(this)->UnlinkAs(kLobbyNodeVtable);
}

// 0x00540840
void LobbyNode::DestroyBody() { UnlinkAs(kLobbyNodeVtable); }
// 0x00540860
void LobbyListNode::DestroyBody() { UnlinkAs(kLobbyListNodeVtable); }

// 0x00540880
LobbyNode* LobbyNode::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005408C0
LobbyListNode* LobbyListNode::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00540F80
LobbyNode* LobbyNode::DestroyOther(unsigned int flags) { return Teardown(flags); }

// 0x005407E0
LobbySearchA* LobbySearchA::Teardown(unsigned int flags)
{
    m_vtable = kLobbySearchAVtable;
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00540810
LobbySearchB* LobbySearchB::Teardown(unsigned int flags)
{
    m_vtable = kLobbySearchBVtable;
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005407B0
TaskList::SchedulerTask* TaskDeleter::Teardown(unsigned int flags)
{
    auto* task = reinterpret_cast<TaskList::SchedulerTask*>(this);
    task->Destruct(); // (Task<T>'s destructor, 0x00577290, is a jump to it)
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(task);
    return task;
}

} // namespace OnlinePlay
