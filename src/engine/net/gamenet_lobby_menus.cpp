// The GameSpy lobby's menus and chat (online::RoomManager): the buddy / player list / regions menu items, the room
// messages, SendMessage, the group room list; and the lobby's destructors (0x005407B0-0x00541690). See gamenet_lobby.h.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/checksums.h"
#include "engine/net/service.h"
#include "platform/gamespy.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/chain_start.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/stats_man.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/loading_bar.h"
#include "platform/compiler.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include <gsl/assert>
#include "engine/core/format.h"
#include "engine/net/net_game_state.h"

THUG2_MSVC_WARNING(disable : 4996) // (strncpy: the original's own)

namespace online {

using namespace detail;

namespace {
constexpr int kPromptHalf = 128; // the buddy prompt's colour channels
constexpr int kMaxRating = 100000;

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
constexpr float kItemScale = 1.1F; // (0x3F8CCCCD)

void push_internet_heap()
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_top_down_heap());
}
void pop_heap() { memory::Hub::get_pull()->leave_heap_scope(); }

// A script array of four integers (an rgba).
void delete_script_array(script::Array* array)
{
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
}
void set_rgba(script::Array* rgba, int red, int green, int b, int a)
{
    rgba->set_element(0, red);
    rgba->set_element(1, green);
    rgba->set_element(2, b);
    rgba->set_element(3, a);
}

// The presence (online's FriendsManager: +4 the player's profile, +0x10 logged in).
bool mark_friend_online(FriendsManager* buddy) { return buddy->authenticated(); }
int own_profile(FriendsManager* buddy) { return buddy->account(); }

} // namespace

// 0x00540900
void RoomManager::add_buddy_menu_item(LobbyPlayer* player)
{
    push_internet_heap();
    script::StructPtr params = script::make_struct();
    params->append_word(kText, player->nick_.data());
    params->put_name_key(checksums::Name{kPadChooseScript}, kLobbyAddBuddy);
    params->put_name_key(checksums::Name{kParentMenuId}, kBuddyMenu);
    script::StructPtr choose = script::make_struct();
    choose->put_int(checksums::Name{kProfile}, player->account_);
    choose->append_word(kNick, player->nick_.data());
    choose->append_word(kNetName, player->nick_.data());
    params->put_sub_struct(kPadChooseParams, choose.get());
    get_run(kMakeTextSubMenuItem, params.get());
    params.reset();
    choose.reset();
    pop_heap();
}

// 0x00540B60
void RoomManager::room_message_callback(void* /*unused*/, int /*unused*/, const char* nick, const char* message,
                                        int /*unused*/, void* /*unused*/)
{
    push_internet_heap();
    constexpr size_t kTextSize = 1024;
    std::array<char, kTextSize> text{};
    text::format_to(text, "%s: %s", nick, message);
    script::StructPtr params = script::make_struct();
    params->append_word(kText, text.data());
    get_run(kCreateConsoleMessage, params.get());
    params.reset();
    pop_heap();
}

// 0x00540E00
bool cfunc_send_message(script::Struct* params, script::Script* /*script*/)
{
    const Hub* manager = net_game_state().online_hub();
    const char* message = nullptr;
    if (!params->fetch_text(kText, &message, 0)) {
        return true;
    }
    if (params->has_bare_flag(kSystemMessage)) {
        constexpr size_t kTextSize = 1024;
        std::array<char, kTextSize> text{};
        text::format_to(text, "<System> %s", message);
        script::StructPtr console = script::make_struct();
        console->append_word(kText, text.data());
        get_run(kCreateConsoleMessage, console.get());
        console.reset();
        return true;
    }
    push_internet_heap();
    game_spy::peer_message_room(manager->lobby_service()->remote(), kRoomGroupKind, message,
                                0); // to the group room
    pop_heap();
    return true;
}

// 0x00540FB0
void RoomManager::add_room_menu_item(LobbyRoom* room, int index)
{
    push_internet_heap();
    constexpr size_t kTextSize = 64;
    std::array<char, kTextSize> text{};
    if (room->full()) {
        text::format_to(text, "%s - %d/%d (%s)", room->label().data(), room->num_players(), room->max_players(),
                        script::fetch_text(kNetLobbyFull));
    } else {
        text::format_to(text, "%s - %d/%d", room->label().data(), room->num_players(), room->max_players());
    }
    script::StructPtr params = script::make_struct();
    params->append_word(kText, text.data());
    const bool available = !room->full() && !room->out_of_range();
    if (available) {
        params->put_name_key(checksums::Name{kPadChooseScript}, kChooseSelectedLobby);
        params->put_name_key(checksums::Name{kFocusScript}, kRegionsMenuSetFocus);
    } else {
        params->put_name_key(checksums::Name{0U}, kNotFocusable);
        params->put_name_key(checksums::Name{kPadChooseScript}, kUnavailableLobby);
    }
    script::StructPtr choose = script::make_struct();
    choose->put_int(checksums::Name{kIndex}, index);
    if (available) {
        params->put_sub_struct(kPadChooseParams, choose.get());
    }
    get_run(kRegionsMenuAddItem, params.get());
    params.reset();
    choose.reset();
    pop_heap();
}

// 0x005411E0
void RoomManager::add_player_list_item(LobbyPlayer* player)
{
    const Hub* manager = net_game_state().online_hub();
    push_internet_heap();
    script::StructPtr choose = script::make_struct();
    script::StructPtr params = script::make_struct();
    gsl::owner<script::Array*> rgba = script::new_array();
    script::StructPtr unfocus = script::make_struct();
    params->append_word(kText, player->nick_.data());
    rgba->configure_size_kind(4, script::QbValueKind::kInteger);
    FriendsManager* buddy = manager->presence_service().get();
    uint32_t script = 0;
    if ((player->account_ != 0) && mark_friend_online(buddy)) {
        if (player->account_ != own_profile(buddy) && !buddy->is_buddy(player->account_)) { // not a buddy yet
            set_rgba(rgba, 0, kPromptHalf, 0, kPromptHalf);
            choose->put_int(checksums::Name{kProfile}, player->account_);
            choose->append_word(kNick, player->nick_.data());
            choose->append_word(kNetName, player->nick_.data());
            params->put_sub_struct(kPadChooseParams, choose.get());
            script = buddy->count_buddies() < kMaxBuddies ? kAddBuddyPrompt : kCantAddBuddyPrompt3;
        } else if (own_profile(buddy) == player->account_) {
            set_rgba(rgba, 0, 0, kPromptHalf, kPromptHalf);
            script = kCantAddSelfPrompt;
        } else {
            set_rgba(rgba, kPromptHalf, kPromptHalf, 0, kPromptHalf);
            script = kAlreadyBuddyPrompt;
        }
    } else {
        set_rgba(rgba, kPromptHalf, kPromptHalf, kPromptHalf, kPromptHalf);
        script = mark_friend_online(buddy) ? kCantAddBuddyPrompt1 : kCantAddBuddyPrompt2;
    }
    params->put_name_key(checksums::Name{kPadChooseScript}, script);
    // (x87: the rating times two floats, truncated)
    int rank = static_cast<int>(static_cast<double>(player->skill_points_) * static_cast<double>(kRankScale) *
                                static_cast<double>(render::kFloatTen));
    rank = std::min(rank, kMaxRank);
    params->put_int(checksums::Name{kRank}, rank);
    params->put_name_key(checksums::Name{kId}, checksums::hash_from_text(player->nick_.data()));
    params->put_name_key(checksums::Name{kParentMenuId}, kLobbyPlayerListMenu);
    params->push_float(checksums::Name{kScale}, kItemScale);
    unfocus->append_array_value(kRgba, rgba);
    params->append_array_value(kRgba, rgba);
    params->put_sub_struct(kUnfocusParams, unfocus.get());
    get_run(kAddLobbyPlayer, params.get());
    delete_script_array(rgba);
    choose.reset();
    params.reset();
    unfocus.reset();
    pop_heap();
}

// 0x00541690
void RoomManager::group_room_callback(const GroupRoomInfo& info)
{
    const auto success = info.success;
    const auto group_id = info.group_id_;
    auto* const server = info.server;
    const auto* const name = info.name;
    const auto num_waiting = info.num_waiting_;
    const auto max_waiting = info.max_waiting_;
    const auto num_games = info.num_games_;
    auto* const param = info.param;
    Hub* manager = net_game_state().online_hub();
    auto set_state = [manager](ServiceState state) { manager->set_service_state(state); };
    if (success == 0) {
        set_state(ServiceState::kLobbyListEmpty);
        return;
    }
    // (the listing's flags: a room was asked for by its lobby_num, and found)
    if (group_id == 0) { // the list's end
        set_state(!net_game_state().listing_asked() || net_game_state().listing_found() ? ServiceState::kLobbyListCheck : ServiceState::kLobbyListEmpty);
        return;
    }
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->internet_bottom_up_heap());
    void* block = memory::alloc_zeroed(sizeof(LobbyRoom));
    auto* room = block ? memory::construct_in<LobbyRoom>(block) : nullptr;
    Expects(room != nullptr);
    std::strncpy(room->label().data(), name, sizeof(room->label()) - 1);
    core::at(room->label(), sizeof(room->label()) - 1) = '\0';
    room->set_num_players(num_games);
    room->set_group_id(group_id);
    room->set_max_players(max_waiting);
    if (num_waiting >= max_waiting) {
        room->set_full(true);
    }
    auto server_int = [server](const char* key, int fallback) {
        return game_spy::server_get_int_value(server, key, fallback);
    };
    const int lobby = server_int("lobby_num", 0);
    if (lobby != 0) {
        uint32_t size = 0;
        const void* data = manager->get_pending_data(&size);
        if (static_cast<uint32_t>(lobby) ==
            checksums::generate_crc_caseless(static_cast<const char*>(data), static_cast<int>(size))) {
            net_game_state().set_listing_found(true);
        }
        net_game_state().set_listing_asked(true);
    }
    room->set_max_rating(server_int("maxrating", kMaxRating));
    room->set_min_rating(server_int("minrating", 0));
    auto rating = [manager] {
        // (the original reads the stats' first word, the rating, through the folded getter 0x00469490 -
        // single::PointerMember::get_pull)
        return manager->rating_service()->get_personal_stats()->skill_points_;
    };
    if (rating() > room->max_rating()) {
        room->set_out_of_range(true);
    }
    if (rating() < room->min_rating()) {
        room->set_out_of_range(true);
    }
    if (server_int("neversoft", 0) != 0) {
        room->set_neversoft(true);
    }
    if (room->num_players() > room->max_players()) {
        room->set_max_players(room->num_players());
    }
    room->list_link().set_priority(-group_id);
    static_cast<lists::Foremost*>(param)->append_vertex(&room->list_link());
    memory::Hub::get_pull()->leave_heap_scope();
}

// --- the destructors ---

// 0x00540D50
RoomManager::~RoomManager()
{
    think_task_.reset();
    nat_task_.reset();
    hosting_task_.reset();
    participants_.uncouple();
    rooms_.uncouple(); // (its own base: the room list's head)
}

} // namespace online
