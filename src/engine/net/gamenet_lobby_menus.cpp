// The GameSpy lobby's menus and chat (online::RoomManager): the buddy / player list / regions menu items, the room
// messages, SendMessage, the group room list; and the lobby's destructors (0x005407B0-0x00541690). See gamenet_lobby.h.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "engine/core/checksums.h"
#include "engine/net/gamespy.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/lists/chain_start.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/stats_man.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/gfx/loading_bar.h"

#pragma warning(disable : 4996) // (sprintf / strncpy: the original's own)

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kText = 0xC4745838;            // text
constexpr uint32_t kPadChooseScript = 0x2B0CFCA1; // pad_choose_script
constexpr uint32_t kPadChooseParams = 0xB8437568; // pad_choose_params
constexpr uint32_t kParentMenuId = 0x416062AB;    // parent_menu_id
constexpr uint32_t kFocusScript = 0x5A8C82DB;     // focus_script
constexpr uint32_t kUnfocusParams = 0xF974BF7C;   // unfocus_params
constexpr uint32_t kProfile = 0x7EA855F0;         // profile
constexpr uint32_t kNick = 0xD6F4D0C8;            // nick
constexpr uint32_t kNetName = 0xD3A5478A;         // net_name
constexpr uint32_t kIndex = 0x7F8C98FE;           // index
constexpr uint32_t kRank = 0x7786171A;            // rank
constexpr uint32_t kId = 0x40C698AF;              // id
constexpr uint32_t kScale = 0x13B9DA7B;           // scale
constexpr uint32_t kRgba = 0x3F6BCDBA;            // rgba
constexpr uint32_t kSystemMessage = 0xA531B313;   // system_message
// Scripts and menus.
constexpr uint32_t kLobbyAddBuddy = 0xD7059EC7;        // lobby_add_buddy
constexpr uint32_t kBuddyMenu = 0x750C8BC0;            // (the buddy menu the item goes in)
constexpr uint32_t kMakeTextSubMenuItem = 0xBEED50B6;  // make_text_sub_menu_item
constexpr uint32_t kCreateConsoleMessage = 0xCCB19938; // create_console_message
constexpr uint32_t kRegionsMenuAddItem = 0xB0DA8BB7;   // regions_menu_add_item
constexpr uint32_t kChooseSelectedLobby = 0x8DE96379;  // choose_selected_lobby
constexpr uint32_t kRegionsMenuSetFocus = 0x36258921;  // regions_menu_set_focus
constexpr uint32_t kNotFocusable = 0x1D944426;         // not_focusable
constexpr uint32_t kUnavailableLobby = 0xBA468C44;     // (chosen on a full / out of range room)
constexpr uint32_t kNetLobbyFull = 0x2767F99B;         // net_lobby_full
constexpr uint32_t kAddBuddyPrompt = 0xD094CCB0;       // add_buddy_prompt
constexpr uint32_t kCantAddBuddyPrompt1 = 0x077E365A;  // cant_add_buddy_prompt_1 (logged in, the player has no profile)
constexpr uint32_t kCantAddBuddyPrompt2 = 0x9E7767E0;  // cant_add_buddy_prompt_2 (not logged in)
constexpr uint32_t kCantAddBuddyPrompt3 = 0xE9705776;  // cant_add_buddy_prompt_3 (30 buddies)
constexpr uint32_t kCantAddSelfPrompt = 0xB11CCDA6;    // cant_add_self_to_buddy_prompt
constexpr uint32_t kAlreadyBuddyPrompt = 0x68635516;   // already_buddy_prompt
constexpr uint32_t kLobbyPlayerListMenu = 0xECA96FAF;  // lobby_player_list_menu
constexpr uint32_t kAddLobbyPlayer = 0x459148C5;       // (the player list's item script)

constexpr int kMaxBuddies = 30;
constexpr int kMaxRank = 10;
constexpr int kRoomGroupKind = 1;
constexpr int kLobbyStateListed = 15;
constexpr int kLobbyStateFailed = 16;
constexpr float kItemScale = 1.1f; // (0x3F8CCCCD)

Hub* net_session_slot() { return g_online_hub; }
void push_internet_heap()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_top_down_heap_);
}
void pop_heap() { memory::Hub::get_pull()->leave_heap_scope(); }

// A script array of four integers (an rgba).
script::Array* new_script_array()
{
    script::Array* memory = script::alloc_script_array();
    return memory ? memory->prepare_all() : nullptr;
}
void delete_script_array(script::Array* array)
{
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
}
void set_rgba(script::Array* rgba, int r, int g, int b, int a)
{
    rgba->set_element(0, r);
    rgba->set_element(1, g);
    rgba->set_element(2, b);
    rgba->set_element(3, a);
}

// The presence (online's FriendsManager: +4 the player's profile, +0x10 logged in).
bool mark_friend_online(void* buddy) { return prop<bool>(buddy, 0x10); }
int own_profile(void* buddy) { return prop<int>(buddy, 4); }

} // namespace

// 0x00540900
void RoomManager::add_buddy_menu_item(LobbyPlayer* player)
{
    push_internet_heap();
    script::Struct* params = new_params();
    params->append_word(kText, player->nick_);
    params->put_name_key(kPadChooseScript, kLobbyAddBuddy);
    params->put_name_key(kParentMenuId, kBuddyMenu);
    script::Struct* choose = new_params();
    choose->put_int(kProfile, player->account_);
    choose->append_word(kNick, player->nick_);
    choose->append_word(kNetName, player->nick_);
    params->put_sub_struct(kPadChooseParams, choose);
    get_run(kMakeTextSubMenuItem, params);
    delete_params(params);
    delete_params(choose);
    pop_heap();
}

// 0x00540B60
void __cdecl RoomManager::room_message_callback(void*, int, const char* nick, const char* message, int, void*)
{
    push_internet_heap();
    char text[1024];
    std::sprintf(text, "%s: %s", nick, message);
    script::Struct* params = new_params();
    params->append_word(kText, text);
    get_run(kCreateConsoleMessage, params);
    delete_params(params);
    pop_heap();
}

// 0x00540E00
bool __cdecl cfunc_send_message(script::Struct* params, script::Script*)
{
    Hub* manager = net_session_slot();
    const char* message = nullptr;
    if (!params->fetch_text(kText, &message, 0))
        return true;
    if (params->has_bare_flag(kSystemMessage)) {
        char text[1024];
        std::sprintf(text, "<System> %s", message);
        script::Struct* console = new_params();
        console->append_word(kText, text);
        get_run(kCreateConsoleMessage, console);
        delete_params(console);
        return true;
    }
    push_internet_heap();
    game_spy::peer_message_room(static_cast<RoomManager*>(manager->lobby_service_)->remote_, kRoomGroupKind, message,
                                0); // to the group room
    pop_heap();
    return true;
}

// 0x00540FB0
void RoomManager::add_room_menu_item(LobbyRoom* room, int index)
{
    push_internet_heap();
    char text[64];
    if (room->full_)
        std::sprintf(text, "%s - %d/%d (%s)", room->label_, room->num_players_, room->max_players_,
                     script::fetch_text(kNetLobbyFull));
    else
        std::sprintf(text, "%s - %d/%d", room->label_, room->num_players_, room->max_players_);
    script::Struct* params = new_params();
    params->append_word(kText, text);
    bool available = !room->full_ && !room->out_of_range_;
    if (available) {
        params->put_name_key(kPadChooseScript, kChooseSelectedLobby);
        params->put_name_key(kFocusScript, kRegionsMenuSetFocus);
    } else {
        params->put_name_key(0u, kNotFocusable);
        params->put_name_key(kPadChooseScript, kUnavailableLobby);
    }
    script::Struct* choose = new_params();
    choose->put_int(kIndex, index);
    if (available)
        params->put_sub_struct(kPadChooseParams, choose);
    get_run(kRegionsMenuAddItem, params);
    delete_params(params);
    delete_params(choose);
    pop_heap();
}

// 0x005411E0
void RoomManager::add_player_list_item(LobbyPlayer* player)
{
    Hub* manager = net_session_slot();
    push_internet_heap();
    script::Struct* choose = new_params();
    script::Struct* params = new_params();
    script::Array* rgba = new_script_array();
    script::Struct* unfocus = new_params();
    params->append_word(kText, player->nick_);
    rgba->configure_size_kind(4, script::kQbvalInteger);
    void* buddy = manager->presence_service_;
    uint32_t script;
    if (player->account_ && mark_friend_online(buddy)) {
        if (player->account_ != own_profile(buddy) &&
            !static_cast<FriendsManager*>(buddy)->is_buddy(player->account_)) { // not a buddy yet
            set_rgba(rgba, 0, 128, 0, 128);
            choose->put_int(kProfile, player->account_);
            choose->append_word(kNick, player->nick_);
            choose->append_word(kNetName, player->nick_);
            params->put_sub_struct(kPadChooseParams, choose);
            script = static_cast<FriendsManager*>(buddy)->count_buddies() < kMaxBuddies ? kAddBuddyPrompt
                                                                                        : kCantAddBuddyPrompt3;
        } else if (own_profile(buddy) == player->account_) {
            set_rgba(rgba, 0, 0, 128, 128);
            script = kCantAddSelfPrompt;
        } else {
            set_rgba(rgba, 128, 128, 0, 128);
            script = kAlreadyBuddyPrompt;
        }
    } else {
        set_rgba(rgba, 128, 128, 128, 128);
        script = mark_friend_online(buddy) ? kCantAddBuddyPrompt1 : kCantAddBuddyPrompt2;
    }
    params->put_name_key(kPadChooseScript, script);
    // (x87: the rating times two floats, truncated)
    int rank = static_cast<int>(static_cast<double>(player->skill_points_) * static_cast<double>(g_rank_scale) *
                                static_cast<double>(render::g_float_ten));
    if (rank > kMaxRank)
        rank = kMaxRank;
    params->put_int(kRank, rank);
    params->put_name_key(kId, checksums::hash_from_text(player->nick_));
    params->put_name_key(kParentMenuId, kLobbyPlayerListMenu);
    params->push_float(kScale, kItemScale);
    unfocus->append_array_value(kRgba, rgba);
    params->append_array_value(kRgba, rgba);
    params->put_sub_struct(kUnfocusParams, unfocus);
    get_run(kAddLobbyPlayer, params);
    delete_script_array(rgba);
    delete_params(choose);
    delete_params(params);
    delete_params(unfocus);
    pop_heap();
}

bool g_listing_asked;
bool g_listing_found;

// 0x00541690
void __cdecl RoomManager::group_room_callback(void*, int success, int group_id, void* server, const char* name,
                                              int num_waiting, int max_waiting, int num_games, int, void* param)
{
    Hub* manager = net_session_slot();
    auto set_state = [manager](int state) { manager->set_service_state(state); };
    if (!success) {
        set_state(kLobbyStateFailed);
        return;
    }
    bool& asked = g_listing_asked; // a room was asked for (by its lobby_num)
    bool& found = g_listing_found;
    if (group_id == 0) { // the list's end
        set_state(!asked || found ? kLobbyStateListed : kLobbyStateFailed);
        return;
    }
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_bottom_up_heap_);
    void* block = memory::alloc_zeroed(sizeof(LobbyRoom));
    auto* room = block ? static_cast<LobbyRoom*>(block)->make_new() : nullptr;
    std::strncpy(room->label_, name, sizeof(room->label_) - 1);
    room->label_[sizeof(room->label_) - 1] = '\0';
    room->num_players_ = num_games;
    room->group_id_ = group_id;
    room->max_players_ = max_waiting;
    if (num_waiting >= max_waiting)
        room->full_ = true;
    auto server_int = [server](const char* key, int fallback) {
        return game_spy::server_get_int_value(server, key, fallback);
    };
    int lobby = server_int("lobby_num", 0);
    if (lobby) {
        uint32_t size;
        void* data = manager->get_pending_data(&size);
        if (static_cast<uint32_t>(lobby) ==
            checksums::generate_crc_caseless(static_cast<const char*>(data), static_cast<int>(size)))
            found = true;
        asked = true;
    }
    room->max_rating_ = server_int("maxrating", 100000);
    room->min_rating_ = server_int("minrating", 0);
    auto rating = [manager] {
        // (the original reads the stats' first word, the rating, through the folded getter 0x00469490 -
        // single::PointerMember::get_pull)
        return static_cast<RankingsManager*>(manager->rating_service_)->get_personal_stats()->skill_points_;
    };
    if (rating() > room->max_rating_)
        room->out_of_range_ = true;
    if (rating() < room->min_rating_)
        room->out_of_range_ = true;
    if (server_int("neversoft", 0))
        room->neversoft_ = true;
    if (room->num_players_ > room->max_players_)
        room->max_players_ = room->num_players_;
    reinterpret_cast<memory::ListNode*>(room)->priority = -group_id;
    static_cast<lists::Foremost*>(param)->append_vertex(reinterpret_cast<memory::ListNode*>(room));
    memory::Hub::get_pull()->leave_heap_scope();
}

// --- the destructors ---

// 0x00540D50
void RoomManager::destroy_body()
{
    for (tasks::SchedulerTask* task : {think_task_, nat_task_, hosting_task_})
        delete task;
    participants_.uncouple();
    rooms_.uncouple(); // (its own base: the room list's head)
}

} // namespace online
