// The GameSpy lobby (see lobby.h).
//
// The GameSpy Peer SDK (engine/net/gamespy.h): what each call does is said where it's made.

#include "game/online/buddy_man.h"
#include "game/online/gamenet_lobby.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include "game/online/service.h"
#include <cstring>
#include <memory>

#include "base/core/checksums.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/task_list.h"
#include "game/online/gamenet_handler_util.h"
#include "game/online/gamenet_manager.h"
#include "game/online/online_identity.h"
#include "game/online/stats_man.h"
#include "engine/script/param_struct.h"
#include "platform/code_address.h"
#include "engine/gfx/render_constants.h"
#include "engine/assets/resource_registry.h"
#include "base/core/stubs.h"
#include "game/goals/objective.h"
#include "engine/script/fwd.h"
#include "engine/script/context.h"
#include "engine/memory/fwd.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "platform/gamespy.h"
#include "game/online/server_list.h"
#include "game/online/score_list.h"
#include "engine/sys/mlp.h"
#include <gsl/pointers>
#include <array>
#include "base/core/compiler.h"
#include "base/core/parse_number.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/format.h"
#include "game/online/net_game_state.h"
#include "base/core/gathered_call.h"
#include "base/core/str.h"

THUG2_MSVC_WARNING(disable : 4996) // (the original's strcpy, kept)

namespace online {

using namespace detail;

namespace {
// The server-browsing keys (qr2 key ids) the game registers.
constexpr uint8_t kQr2KeyNumObservers = 0x33;
constexpr uint8_t kQr2KeyMaxObservers = 0x34;
constexpr uint8_t kQr2KeySkillLevel = 0x35;
constexpr uint8_t kQr2KeyStarted = 0x36;
constexpr uint8_t kQr2KeyHostMode = 0x37;
constexpr uint8_t kQr2KeyRanked = 0x38;
constexpr uint8_t kQr2KeyRating = 0x39;
// peerSetTitle's game version and the most servers it updates at once.
constexpr int kPeerGameVersion = 6;
constexpr int kPeerMaxUpdates = 15;

// Names.
constexpr uint32_t kNumPlayers = 0x1CB2A6B0;            // num_players
constexpr uint32_t kUpdatePlayerList = 0x695C0037;      // update_lobby_player_list
constexpr uint32_t kDestroyUserChildren = 0xC380FBEE;   // destroy_lobby_user_list_children
constexpr uint32_t kDestroyBuddyChildren = 0x4C2FD924;  // destroy_lobby_buddy_list_children
constexpr uint32_t kPrepareUserList = 0xEBF04D48;       // prepare_lobby_user_list_for_new_children
constexpr uint32_t kConsoleMessage = 0xCCB19938;        // create_console_message
constexpr uint32_t kStatusJoined = 0x404CEABA;          // lobby_status_joined
constexpr uint32_t kStatusLeft = 0xBA252C57;            // lobby_status_left
constexpr uint32_t kUserId = 0x58912C6A;                // user_id
constexpr uint32_t kDestroyUser = 0x053A9B81;           // destroy_lobby_user
constexpr uint32_t kText = 0xC4745838;                  // text
constexpr uint32_t kLeft = 0x85981897;                  // left
constexpr uint32_t kId = 0x40C698AF;                    // id
constexpr uint32_t kRank = 0x7786171A;                  // rank
constexpr uint32_t kUpdateRank = 0xCD827195;            // player_list_update_rank
constexpr uint32_t kDialogBoxExit = 0x666B6A9D;         // dialog_box_exit
constexpr uint32_t kCreateLobbyListMenu = 0x3F584E2A;   // create_lobby_list_menu
constexpr uint32_t kCreateSelectGamesMenu = 0x204092CB; // create_network_select_games_menu
constexpr uint32_t kJoinLobbyFailed = 0x3C5FE392;       // CreateJoinLobbyFailedDialog
constexpr uint32_t kIndex = 0x7F8C98FE;                 // index

constexpr int kRoomGroupKind = 1; // (GameSpy's RoomType)
constexpr int kMaxRank = 10;

RoomManager* the_lobby() { return net_game_state().online_hub()->lobby_service().get(); }
memory::Hub* ram_stats() { return memory::Hub::get_pull(); }

// Walking a list as the original does: a node whose data is -1 is the head (checked before null).
memory::ListNode* head_of_scene_list(memory::ListNode* head)
{
    memory::ListNode* node = head->next();
    return node->data() == memory::ListNode::head_marker() ? nullptr : node;
}
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next();
    return next->data() == memory::ListNode::head_marker() ? nullptr : next;
}
template <typename T>
T* data_of(memory::ListNode* node)
{
    return node ? static_cast<T*>(node->data()) : nullptr;
}

// The listed player named `nick` (case ignored), or null.
LobbyPlayer* find_player(RoomManager* lobby, const char* nick)
{
    for (memory::ListNode* node = head_of_scene_list(&lobby->participants()); node; node = get_next_node(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data());
        if (!player) {
            break;
        }
        if (_stricmp(player->nick_.data(), nick) == 0) {
            return player;
        }
    }
    return nullptr;
}

// A new player named `nick` (null if out of memory: then written to anyway, as the original), its profile asked of
// the SDK, at the end of the list.
LobbyPlayer* enroll_player(RoomManager* lobby, void* peer, const char* nick, bool listed)
{
    auto* player = static_cast<LobbyPlayer*>(memory::alloc_zeroed(sizeof(LobbyPlayer)));
    if (player) {
        player->list_link_.make_node(player);
        player->account_ = 0;
        player->skill_points_ = 0;
        player->listed_ = false;
    }
    Expects(player != nullptr); // (out of memory: the original wrote through null)
    text::copy_cut(player->nick_, nick);
    game_spy::peer_get_player_info_no_wait(peer, nick, nullptr, &player->account_); // (the player's profile id)
    if (listed) {
        player->listed_ = true;
    }
    memory::ListNode* head = &lobby->participants();
    player->list_link_.set_prev(head->prev());
    player->list_link_.set_next(head);
    head->prev()->set_next(&player->list_link_);
    head->set_prev(&player->list_link_);
    return player;
}

// Every item of `head`'s list (a room or a player: its own node) deleted - out of the list, freed.
void delete_all(memory::ListNode* head)
{
    memory::ListNode* node = head_of_scene_list(head);
    auto* item = data_of<memory::ListNode>(node);
    while (item) {
        node = node ? get_next_node(node) : nullptr;
        auto* following = data_of<memory::ListNode>(node);
        memory::ListNode::destroy(item);
        item = following;
    }
}

// A player's rating from a room key and its rank, told to the player list.
void update_rank(LobbyPlayer* player, const char* value)
{
    player->skill_points_ = text::to_int(value);
    const double rank = static_cast<double>(player->skill_points_) * kRankScale * render::kFloatTen;
    int shown = static_cast<int>(rank); // (truncated, as _ftol)
    shown = std::min(shown, kMaxRank);
    const script::StructPtr params = script::make_struct();
    params->put_name_key(checksums::Name{kId}, checksums::hash_from_text(player->nick_.data()));
    params->put_int(checksums::Name{kRank}, shown);
    get_run(kUpdateRank, params.get());
}

// "<nick> <text>" to the console, with the flag `kind` (by name).
void console_message(const char* nick, uint32_t text_name, bool by_name)
{
    constexpr size_t kLineSize = 1024;
    std::array<char, kLineSize> line{};
    text::format_to(line, "%s %s", nick, script::fetch_text(text_name));
    const script::StructPtr params = script::make_struct();
    if (by_name) {
        params->append_word("text", line.data());
        params->put_name_key("join", 0);
    } else {
        params->append_word(kText, line.data());
        params->put_name_key(checksums::Name{kLeft}, 0);
    }
    get_run(kConsoleMessage, params.get());
}

// A task running `code` for the lobby (priority 0; null if out of memory).
std::unique_ptr<tasks::SchedulerTask> new_task(RoomManager* lobby, tasks::Task<RoomManager>::Code code)
{
    return std::make_unique<tasks::Task<RoomManager>>(code, lobby, 0);
}

} // namespace

// 0x00545860
LobbyRoom::LobbyRoom() { list_link_.make_node(this); }

// 0x00541870
RoomManager::RoomManager()
{
    rooms_.make_head();
    participants_.make_head();
    think_task_ = new_task(this, &online::RoomManager::think_task);
    nat_task_ = new_task(this, &online::RoomManager::nat_negotiation_task);
    hosting_task_ = new_task(this, &online::RoomManager::query_task);
    // The server-browsing keys (GameSpy's qr2 key registration).
    game_spy::call_qr2_register_key(kQr2KeyNumObservers, "numobservers");
    game_spy::call_qr2_register_key(kQr2KeyMaxObservers, "maxobservers");
    game_spy::call_qr2_register_key(kQr2KeySkillLevel, "skilllevel");
    game_spy::call_qr2_register_key(kQr2KeyStarted, "started");
    game_spy::call_qr2_register_key(kQr2KeyHostMode, "hostmode");
    game_spy::call_qr2_register_key(kQr2KeyRanked, "ranked");
    game_spy::call_qr2_register_key(kQr2KeyRating, "rating_");
}

// 0x00542DF0
void RoomManager::init_peer()
{
    ram_stats()->enter_heap_scope(ram_stats()->network_heap());
    // The SDK's callbacks (those not set stay null).
    game_spy::PeerCallbacks callbacks;
    callbacks.disconnected = &online::RoomManager::disconnected_callback;
    callbacks.room_message_ = &online::RoomManager::room_message_callback;
    callbacks.player_message_ = &stubs::do_nothing; // (nothing: the empty function)
    callbacks.player_joined_ = &player_joined_callback;
    callbacks.player_left_ = &core::gathered<PlayerLeft, &player_left_callback, void, void*, int, const char*, const char*,
                                             RoomManager*>;
    callbacks.player_info_ = &core::gathered<PlayerInfo, &player_info_callback, void, void*, int, const char*, uint32_t, int,
                                             RoomManager*>;
    callbacks.new_player_list_ = &new_player_list_callback;
    callbacks.room_key_changed_ = &core::gathered<RoomKeyChanged, &room_key_changed_callback, void, void*, int, const char*,
                                                  const char*, const char*, RoomManager*>;
    callbacks.qr_nat_negotiate_ = &online::RoomManager::hole_punch_result_fn;
    callbacks.qr_key_list_ = &online::RoomManager::key_list_callback;
    callbacks.qr_server_key_ = &online::RoomManager::server_key_callback;
    callbacks.qr_player_key_ = &core::gathered<online::RoomManager::PlayerKeyQuery, &online::RoomManager::player_key_callback,
                                               void, void*, int, int, void*, void*>;
    callbacks.qr_count_ = &online::RoomManager::count_callback;
    callbacks.qr_add_error_ = &stubs::do_nothing;
    callbacks.qr_public_address_ = &online::RoomManager::public_address_callback;
    callbacks.param = this; // (their parameter)
    remote_ = game_spy::peer_initialize(callbacks);         // (the peer made)
    in_group_room_ = false;
    drop_is_expected_ = true;
    hosting_ = false;
    restricted_hosting_ = false;
    ram_stats()->leave_heap_scope();
}

namespace {
uint32_t& unknowne430()
{
    static uint32_t unknowne430{};
    return unknowne430;
}
} // namespace

// 0x00541A10
void RoomManager::start_listing_games()
{
    Hub* manager = net_game_state().online_hub();
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap());
    manager->set_service_state(online::ServiceState::kLobbyListIdle); // (the server list's state)
    unknowne430() = 0;
    net_game_state().set_listing_asked(false);
    net_game_state().set_listing_found(false);
    game_spy::peer_list_group_rooms(remote_, R"(\maxplayers\neversoft\maxrating\minrating)",
                                    code_address(&core::gathered<online::RoomManager::GroupRoomInfo, &online::RoomManager::group_room_callback, void, void*,
                                                 int, int, void*, const char*, int, int, int, int, void*>), this,
                                    0); // (the group rooms listed)
    ram_stats()->leave_heap_scope();
}

// 0x005427E0
void RoomManager::clear_lobbies()
{
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    if (remote_) {
        game_spy::peer_stop_listing_games(remote_); // (the games' listing stopped)
    }
    delete_all(&rooms_);
    ram_stats()->leave_heap_scope();
}

// 0x005428B0
void RoomManager::populate_room_list()
{
    get_run(kDialogBoxExit);
    get_run(kCreateLobbyListMenu);
    int index = 0;
    for (memory::ListNode* node = head_of_scene_list(&rooms_); node; node = get_next_node(node)) {
        auto* lobby = static_cast<LobbyRoom*>(node->data());
        if (!lobby) {
            break;
        }
        add_room_menu_item(lobby, index); // (its menu item)
        ++index;
    }
}

// 0x00542970
LobbyRoom* RoomManager::get_lobby(int index)
{
    for (memory::ListNode* node = head_of_scene_list(&rooms_); node; node = get_next_node(node)) {
        auto* lobby = static_cast<LobbyRoom*>(node->data());
        if (!lobby) {
            return nullptr;
        }
        if (index == 0) {
            return lobby;
        }
        --index;
    }
    return nullptr;
}

// 0x00541AF0
void RoomManager::clear_players()
{
    delete_all(&participants_);
    get_run(kUpdatePlayerList);
}

// 0x00541BA0
void RoomManager::fill_player_list()
{
    get_run(kDestroyUserChildren);
    for (memory::ListNode* node = head_of_scene_list(&participants_); node; node = get_next_node(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data());
        if (!player) {
            break;
        }
        if (player->listed_) {
            add_player_list_item(player); // (its menu item)
        }
    }
    get_run(kUpdatePlayerList);
}

// 0x00541C70
void RoomManager::fill_prospective_buddy_list()
{
    const Hub* manager = net_game_state().online_hub();
    get_run(kDestroyBuddyChildren);
    for (memory::ListNode* node = head_of_scene_list(&participants_); node; node = get_next_node(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data());
        if (!player) {
            break;
        }
        const int profile = player->account_;
        if (profile != manager->presence_service()->account() && profile > 0 && // (0x005480C0: our own profile)
            !manager->presence_service()->is_buddy(profile)) {                  // (a buddy already)
            add_buddy_menu_item(player);                                      // (a prospective buddy)
        }
    }
}

// 0x00542B10
void RoomManager::request_player_list()
{
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    clear_players();
    get_run(kDestroyUserChildren);
    game_spy::peer_enum_players(remote_, kRoomGroupKind, &core::gathered<EnumPlayer, &enum_players_callback, void, void*, int, int, int, const char*, int, RoomManager*>,
                                 this); // (the room's players)
    ram_stats()->leave_heap_scope();
}

// 0x00541D50
void RoomManager::player_joined_callback(void* peer, int room_type, const char* nick, RoomManager* lobby)
{
    (void)room_type;
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    if (LobbyPlayer* known = find_player(lobby, nick)) {
        game_spy::peer_get_player_info_no_wait(peer, nick, nullptr, &known->account_);
        ram_stats()->leave_heap_scope();
        known->listed_ = true;
        return;
    }
    LobbyPlayer* player = enroll_player(lobby, peer, nick, true);
    get_run(kPrepareUserList);
    online::RoomManager::add_player_list_item(player); // (its menu item)
    get_run(kUpdatePlayerList);
    console_message(nick, kStatusJoined, true);
    ram_stats()->leave_heap_scope();
}

// 0x00541FD0
void RoomManager::player_info_callback(const PlayerInfo& info)
{
    auto* const peer = info.peer;
    const auto room_type = info.room_type_;
    const auto* const nick = info.nick;
    const auto ip = info.ip;
    const auto profile_id = info.profile_id_;
    auto* const lobby = info.lobby;
    (void)peer;
    (void)room_type;
    (void)ip;
    if (!nick) {
        return;
    }
    LobbyPlayer* player = find_player(lobby, nick);
    if (!player) {
        return;
    }
    if (player->account_ == 0) {
        player->account_ = profile_id;
    }
    player->listed_ = true;
    online::RoomManager::add_player_list_item(player); // (its menu item)
    get_run(kUpdatePlayerList);
}

// 0x005420C0
void RoomManager::player_left_callback(const PlayerLeft& left)
{
    auto* const peer = left.peer;
    const auto room_type = left.room_type_;
    const auto* const nick = left.nick;
    const auto* const reason = left.reason;
    auto* const lobby = left.lobby;
    (void)peer;
    (void)room_type;
    (void)reason;
    if (LobbyPlayer* player = find_player(lobby, nick)) {
        script::StructPtr params = script::make_struct();
        params->put_name_key(checksums::Name{kUserId}, checksums::hash_from_text(nick));
        get_run(kDestroyUser, params.get());
        memory::ListNode::destroy(&player->list_link_); // (0x00541AC0)
        params.reset();
        get_run(kUpdatePlayerList);
    }
    console_message(nick, kStatusLeft, false);
}

// 0x00542B70
void RoomManager::new_player_list_callback(void* peer, int room_type, RoomManager* lobby)
{
    (void)peer;
    (void)room_type;
    lobby->request_player_list();
}

// 0x00542650
void RoomManager::room_key_changed_callback(const RoomKeyChanged& changed)
{
    auto* const peer = changed.peer;
    const auto room_type = changed.room_type_;
    const auto* const nick = changed.nick;
    const auto* const key = changed.key;
    const auto* const value = changed.value;
    auto* const lobby = changed.lobby;
    (void)peer;
    (void)room_type;
    (void)key;
    if (LobbyPlayer* player = find_player(lobby, nick)) {
        update_rank(player, value);
    }
}

// 0x00542330
void RoomManager::room_keys_callback(const RoomKeys& room_keys)
{
    auto* const peer = room_keys.peer;
    const auto success = room_keys.success;
    const auto room_type = room_keys.room_type_;
    const auto* const nick = room_keys.nick;
    const auto count = room_keys.count;
    auto* const keys = room_keys.keys;
    auto* const values = room_keys.values;
    auto* const lobby = room_keys.lobby;
    (void)peer;
    (void)success;
    (void)room_type;
    if (count <= 0 || !*keys || _stricmp(*keys, "b_rating") != 0) {
        return;
    }
    for (memory::ListNode* node = head_of_scene_list(&lobby->participants_); node; node = get_next_node(node)) {
        auto* player = static_cast<LobbyPlayer*>(node->data());
        if (!player) {
            break;
        }
        if (_stricmp(player->nick_.data(), nick) == 0) {
            update_rank(player, *values); // (every player of that nick)
        }
    }
}

// 0x005424F0
void RoomManager::enum_players_callback(const EnumPlayer& player)
{
    auto* const peer = player.peer;
    const auto success = player.success;
    const auto room_type = player.room_type_;
    const auto index = player.index;
    const auto* const nick = player.nick;
    const auto flags = player.flags;
    auto* const lobby = player.lobby;
    (void)room_type;
    (void)flags;
    if ((success == 0) || index == -1) {
        return;
    }
    if (find_player(lobby, nick)) {
        return;
    }
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap());
    enroll_player(lobby, peer, nick, false);
    ram_stats()->leave_heap_scope();
}

// 0x00542B80
void RoomManager::join_room_callback(const JoinRoomResult& joined)
{
    auto* const peer = joined.peer;
    const auto success = joined.success;
    const auto result = joined.result;
    const auto room_type = joined.room_type_;
    auto* const lobby = joined.lobby;
    (void)result;
    const Hub* manager = net_game_state().online_hub();
    if (success == 0) {
        get_run(kJoinLobbyFailed);
        return;
    }
    get_run(kDialogBoxExit);
    get_run(kCreateSelectGamesMenu);
    lobby->in_group_room_ = true;
    lobby->request_player_list();
    if (!manager->presence_service()->authenticated()) { // (0x005480B0: logged in to GameSpy presence)
        return;
    }
    // Our status: chatting in the lobby (0x00548320).
    manager->presence_service()->change_standing(4, script::fetch_text("homie_status_chatting"), lobby->room_name_.data());
    // Our room key b_rating: the stats' rating (0x0054CA80 of the stats manager, read through its pointer).
    constexpr size_t kKeySize = 64;
    std::array<char, kKeySize> key{};
    constexpr size_t kValueSize = 64;
    std::array<char, kValueSize> value{};
    text::format_to(key, "b_rating");
    const int* rating = &manager->rating_service()->get_personal_stats()->skill_points_;
    text::format_to(value, "%d", *rating);
    std::array<const char*, 1> keys = {{key.data()}};
    std::array<const char*, 1> values = {{value.data()}};
    const char* our_nick = game_spy::peer_get_nick(peer);                     // (our nick)
    game_spy::peer_set_room_keys(peer, room_type, our_nick, 1, keys.data(), values.data()); // (our room keys set)
    game_spy::peer_get_room_keys(peer, room_type, "*", 1, keys.data(), &core::gathered<RoomKeys, &room_keys_callback, void, void*, int, int, const char*, int, char**, char**, RoomManager*>, lobby,
                                 0); // (everyone's asked for)
}

// --- The script commands ---

// 0x00541A70: num_players = the players listed in the lobby.
bool cfunc_num_players_in_lobby(script::Struct* params, script::Context context)
{
    (void)params;
    int count = 0;
    for (memory::ListNode* node = head_of_scene_list(&the_lobby()->participants()); node; node = get_next_node(node)) {
        ++count;
    }
    context.args()->put_int(checksums::Name{kNumPlayers}, count);
    return true;
}

// 0x005429C0: out of the lobby and GameSpy (the peer's game stopped, its title cleared and the peer disconnected -
// peerStopGame, peerClearTitle, peerDisconnect), the lists emptied, two of its tasks removed, then 0x00540750.
bool cfunc_lobby_disconnect(script::Struct* params, script::Context context)
{
    (void)params;
    (void)context;
    const Hub* manager = net_game_state().online_hub();
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    RoomManager* lobby = manager->lobby_service().get();
    while (lobby->negotiations() != 0) { // (counted down one at a time, as the original - a negative count takes long)
        --lobby->negotiations();
        lobby = manager->lobby_service().get();
    }
    manager->lobby_service()->clear_lobbies();
    manager->lobby_service()->clear_players();
    manager->lobby_service()->get_think_task()->drop_out();
    manager->lobby_service()->nat_task()->drop_out();
    lobby = manager->lobby_service().get();
    lobby->set_in_group_room(false);
    lobby->set_drop_is_expected(true);
    if (lobby->remote()) {
        game_spy::peer_stop_game(lobby->remote());
        game_spy::peer_clear_title(lobby->remote());
        game_spy::peer_disconnect(lobby->remote());
    }
    manager->lobby_service()->hangup();
    ram_stats()->leave_heap_scope();
    return true;
}

// 0x00542AA0: the lobby's player list shown (the manager's singleton taken and given back - 0x0053F520 /
// 0x005395C0).
bool cfunc_fill_player_list(script::Struct* params, script::Context context)
{
    (void)params;
    (void)context;
    const Hub* manager = Hub::fabricate(false);
    manager->lobby_service()->fill_player_list();
    release_manager();
    return true;
}

// 0x00542B00
bool cfunc_fill_lobby_prospective_buddy_list(script::Struct* params, script::Context context)
{
    (void)params;
    (void)context;
    the_lobby()->fill_prospective_buddy_list();
    return true;
}

// 0x00542CE0: back into the chosen lobby's group room, if any (peerJoinGroupRoom, join_room_callback).
bool cfunc_rejoin_lobby(script::Struct* params, script::Context context)
{
    (void)params;
    (void)context;
    const Hub* manager = net_game_state().online_hub();
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    RoomManager* lobby = manager->lobby_service().get();
    if (lobby->group_id() != 0) {
        game_spy::peer_join_group_room(lobby->remote(), lobby->group_id(), &core::gathered<RoomManager::JoinRoomResult, &RoomManager::join_room_callback, void, void*, int, int, int, RoomManager*>, lobby, 0);
    }
    ram_stats()->leave_heap_scope();
    return true;
}

// 0x00542D30: lobby `index` chosen (no null check: as the original) - its group room, numbers, name and flag kept,
// the room joined (join_room_callback), the lobby list emptied.
bool cfunc_choose_lobby(script::Struct* params, script::Context context)
{
    (void)context;
    const Hub* manager = net_game_state().online_hub();
    int index = 0;
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    params->fetch_int_value(kIndex, &index, 0);
    RoomManager* lobby = manager->lobby_service().get();
    LobbyRoom* chosen = lobby->get_lobby(index);
    lobby->set_group_id(chosen->group_id());
    lobby->set_unknown30(chosen->num_players());
    lobby->set_unknown2c(chosen->max_players());
    text::copy_into(lobby->room_name(), chosen->label().data());
    lobby->set_restricted_hosting(chosen->neversoft());
    game_spy::peer_join_group_room(lobby->remote(), lobby->group_id(), &core::gathered<RoomManager::JoinRoomResult, &RoomManager::join_room_callback, void, void*, int, int, int, RoomManager*>, lobby, 0);
    lobby->clear_lobbies();
    ram_stats()->leave_heap_scope();
    return true;
}

// 0x00542EE0: the peer made, the title set (peerSetTitle: the game name for the title and the server list -
// NetLink::GameName: OnlineGameName or thps6pc -, its secret key - stored bitwise-NOT at 0x0067F154, where
// online_identity.cpp writes OnlineGameKey -, version 6, 15 updates, NAT negotiation, pinging the group room only),
// the connecting thread started (0x00540AB0, suspended first so its id becomes the lobby id - 0x00533240), and the
// lobby's first task added to the main loop's logic tasks.
// (The original pushes the game name's address, 0x0064B1CC; online_identity.cpp wrote the configured name's address
// over those two operands before this was source.)
bool cfunc_lobby_connect(script::Struct* params, script::Context context)
{
    (void)params;
    (void)context;
    Hub* manager = net_game_state().online_hub();
    auto* logic = (&assets::get_main_loop_manager()->logic_tasks()); // (the logic tasks)
    DWORD thread_id = 0;
    std::array<BOOL, 3> ping_rooms = {{FALSE, TRUE, FALSE}};
    std::array<BOOL, 3> cross_ping_rooms = {{FALSE, FALSE, FALSE}};
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap());
    RoomManager* lobby = manager->lobby_service().get();
    lobby->init_peer();
    const char* key = net::game_key(); // (given to the SDK: nothing written into data)
    const char* name = net::get_game_name();
    game_spy::peer_set_title(manager->lobby_service()->remote(), name, key, name, key, kPeerGameVersion, kPeerMaxUpdates, 1, ping_rooms.data(),
                             cross_ping_rooms.data());
    HANDLE thread = CreateThread(nullptr, net::kSdkThreadStackBytes, &online::RoomManager::connect_thread, manager->lobby_service().get(),
                                 CREATE_SUSPENDED, &thread_id);
    manager->set_room_id(thread_id);
    ResumeThread(thread);
    logic->queue_job(manager->lobby_service()->get_think_task().get());
    ram_stats()->leave_heap_scope();
    return true;
}

} // namespace online

