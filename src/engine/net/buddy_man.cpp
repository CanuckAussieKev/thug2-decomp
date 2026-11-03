// The online profile and buddy list (online::FriendsManager). See buddy_man.h.


#include "engine/net/buddy_man.h"
#include "platform/gamespy.h"

#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <iterator>
#include <memory>
#include <string_view>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "engine/core/str.h"
#include "engine/core/task_list.h"
#include "engine/lists/list_walk.h"
#include "engine/maths/random.h"
#include "engine/net/buddy_status.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/online_text.h"
#include "engine/net/server_list.h"
#include "engine/script/cell.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "platform/code_address.h"
#include "engine/assets/resource_registry.h"
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
#include "engine/sys/mlp.h"
#include "engine/core/parse_number.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/net/net_game_state.h"
#include "engine/core/gathered_call.h"
#include "engine/net/online_identity.h"

namespace {
constexpr int kGpCallbackError = 0;
constexpr int kGpCallbackBuddyRequest = 1;
constexpr int kGpCallbackBuddyStatus = 2;
constexpr int kGpCallbackBuddyMessage = 3;
constexpr int kGpCallbackUnused4 = 4;
constexpr int kGpCallbackUnused5 = 5;
constexpr float kDefaultMenuScale = 1.1f;
constexpr int kCookieRange = 0xFFFF;

// Names (the checksums of their text).
constexpr uint32_t kCookie = 0x751F4599u; // cookie
constexpr uint32_t kFlashBuddyMessageIcon = 0x10C3647Cu; // flash_buddy_message_icon
constexpr uint32_t kMsgBody = 0x59EDEC1Cu; // msg_body
constexpr uint32_t kNetName = 0xD3A5478Au; // net_name
constexpr uint32_t kNick = 0xD6F4D0C8u; // Nick
constexpr uint32_t kPROFILE = 0x7EA855F0u; // PROFILE

} // namespace

namespace online {

namespace {

constexpr int kMaxBuddies = 30;             // BuddyListFull
constexpr int kMaxMessages = 20;            // more than this waiting: new ones are dropped
constexpr uint32_t kLoginTimeout = 10000;   // ms
constexpr uint32_t kProcessInterval = 1000; // ms between gpProcess calls once logged in
constexpr int kJoinPort = 0x1421;           // THUG2's join port (a buddy behind the same public address)
constexpr int kStatusPlaying = 2;           // GP_PLAYING

// GP error codes (on_error).
constexpr int kErrorLogin = 0x107;
constexpr int kErrorNewUserNick = 0x201;
constexpr int kErrorPassword = 0x202;
constexpr int kErrorAlreadyBuddy = 0x603;

// NOT ORIGINAL grouping - buddy_man.cpp's state (the original's globals). Made on first use, never destroyed
// (buddy_man_state()).
struct BuddyManState {
    bool creating_profile_{};
    uint32_t buddy_login_start_{};
    uint32_t last_process_{};
};

BuddyManState& buddy_man_state()
{
    static core::NoDestroy<BuddyManState> state;
    return state.get();
}

// The login's start (the task's 10 s limit), when GP was last processed once logged in, and "a new profile is being
// made" (its gpConnectNewUser answer "nick taken" logs in instead).
uint32_t& login_start() { return buddy_man_state().buddy_login_start_; }
uint32_t& last_process() { return buddy_man_state().last_process_; }
bool& creating_profile() { return buddy_man_state().creating_profile_; }

memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
void push_top_down() { ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap()); }
void push_bottom_up() { ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap()); }
void pop_heap() { ram_stats()->leave_heap_scope(); }
tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks()); }

uint32_t get_checksums(const char* name) { return checksums::hash_from_text(name); }
void get_run(const char* script, script::Struct* params = nullptr)
{
    script::launch_script(script, params, nullptr, false);
}

// The GameSpy presence SDK (library code, not decompiled; cdecl).
void gp_initialize(void* connection, int product_id, int namespace_id)
{
    game_spy::gp_initialize(connection, product_id, namespace_id);
}
void gp_set_callback(void* connection, int type, const void* callback, void* param)
{
    game_spy::gp_set_callback(connection, type, callback, param);
}
void gp_process(void* connection) { game_spy::gp_process(connection); }
void gp_disconnect(void* connection) { game_spy::gp_disconnect(connection); }
void gp_send_buddy_request(void* connection, int profile, const char* reason)
{
    game_spy::gp_send_buddy_request(connection, profile, reason);
}
void gp_delete_buddy(void* connection, int profile) { game_spy::gp_delete_buddy(connection, profile); }
void gp_set_status(void* connection, int status, const char* text, const char* location)
{
    game_spy::gp_set_status(connection, status, text, location);
}
void gp_connect_new_user(void* connection, const char* nick, const char* unique_nick, const char* email,
                         const char* password, const char* cd_key, int firewall, int blocking, const void* callback,
                         void* param)
{
    game_spy::gp_connect_new_user(connection, nick, unique_nick, email, password, cd_key, firewall, blocking, callback,
                                  param);
}

// GPBuddyStatus as gpGetBuddyStatus fills it (the end padded: the SDK's own size isn't known exactly).
constexpr size_t kRestSize = 0x20;
constexpr size_t kLocationStringSize = 256;
constexpr size_t kStatusStringSize = 256;
struct GpBuddyStatus {
    int profile;
    int status;
    std::array<char, kStatusStringSize> status_string_;
    std::array<char, kLocationStringSize> location_string_;
    uint32_t ip;
    int port;
    std::array<uint8_t, kRestSize> rest;
};
void gp_get_buddy_status(void* connection, int index, GpBuddyStatus* status)
{
    game_spy::gp_get_buddy_status(connection, index, status);
}

// The callbacks' arguments (the parts read).
struct GpErrorArg {
    int result;
    int error_code_;
};
struct GpConnectResponseArg {
    int result;
    int profile;
};
struct GpRecvBuddyStatusArg {
    int profile;
    uint32_t date;
    int index;
};
struct GpRecvBuddyMessageArg {
    int profile;
    uint32_t date;
    char* message;
};

// The lobby code before this class (server_list.cpp, buddy_status.cpp).
// 0x00548040: whether a GP location string is a THUG2 game's: "10thug211:<ip>:<private ip>:<port>:<name>".
// (is_thug2_location itself, from server_list.h)
// 0x005482A0: logged in as `profile` (state 4, the status set online).
void mark_friend_online(FriendsManager* man, int profile) { man->change_online_flag(profile); }
// 0x005484A0 / 0x005484C0: lists::Elem<Buddy> / lists::Elem<FriendNote>'s destructors (unlinked).
void destroy_buddy_node(Buddy* buddy) { buddy->list_link().uncouple(); }            // (0x005484A0)
void destroy_message_node(FriendNote* message) { message->list_link().uncouple(); } // (0x005484C0)
// 0x00548100: GP's buddy request callback (the request accepted); 0x00548140: the login thread.
const void* const kOnBuddyRequest =
    code_address(&core::gathered<FriendsManager::BuddyRequest, &FriendsManager::on_buddy_request, void, void*, const void*, FriendsManager*>);
const auto kLoginThread = &FriendsManager::login_thread;
// The lobby (lobby_service_): its peer (0x0053F880), and the peer SDK's public address of this machine (0x005FC940).
void* lobby_peer(void* lobby) { return static_cast<RoomManager*>(lobby)->get_lobby_handle(); }
uint32_t peer_public_ip(void* peer) { return game_spy::peer_get_public_ip(peer); }

// A buddy or a message deleted (its scalar deleting destructor).
template <typename Item>
void unlink_list_node(Item* item)
{
    item->teardown(1);
}

void init_node(memory::ListNode* node) { node->make_node(node); }
// `node` at the end of the list.
void get_append(memory::ListNode* head, memory::ListNode* node)
{
    node->set_prev(head->prev());
    node->set_next(head);
    head->prev()->set_next(node);
    head->set_prev(node);
}
// The items of a list (the original's count checks each node's data for the head before checking it for null).
int total_of(const memory::ListNode* head)
{
    int count = 0;
    for (const memory::ListNode* node = head->next(); node->data() != memory::ListNode::head_marker() && node;
         node = node->next())
        ++count;
    return count;
}

// A new buddy (the constructor inlined in the original): offline, not pending, no location. (A failed allocation
// isn't checked by the callers.)
Buddy* alloc_friend_entry()
{
    auto* buddy = static_cast<Buddy*>(memory::alloc_zeroed(sizeof(Buddy)));
    if (buddy) {
        init_node(&buddy->list_link());
        buddy->set_pending(false);
        buddy->set_cur_standing(0);
        core::at(buddy->location(), 0) = '\0';
        text::format_to(buddy->status_text(), "%s", "Offline");
    }
    return buddy;
}

// A THUG2 location string's parts into `buddy`: the game's name (after the fourth ':' - is_thug2_location has checked
// there are four) and the address. `checked`: the original checks each ':' is found when updating a buddy (and
// copies from address 0 if not - a crash), not when making one.
void parse_location(Buddy* buddy, char* location, bool checked)
{
    // The text after the fourth ':'. (Fewer than four: null - the original copied from address 0 and crashed when it
    // checked, and went through a null pointer finding the next ':' when it did not. A crash in both.)
    (void)checked;
    const std::string_view whole(location);
    const char* name = nullptr;
    size_t colon = 0;
    int found = 0;
    for (; found < 4; ++found) {
        colon = whole.find(':', found == 0 ? 0 : colon + 1);
        if (colon == std::string_view::npos)
            break;
    }
    if (found == 4)
        name = whole.substr(colon + 1).data();
    text_tools::dup_from(buddy->location().data(), name);
    text_tools::get_token(location, ":"); // "10thug211"
    buddy->set_join_ip(static_cast<uint32_t>(text::to_int(text_tools::get_token(nullptr, ":"))));
    buddy->set_join_private_ip(static_cast<uint32_t>(text::to_int(text_tools::get_token(nullptr, ":"))));
    buddy->set_join_port(text::to_int(text_tools::get_token(nullptr, ":")));
}

} // namespace

// 0x00548CB0
FriendNote::FriendNote()
{
    init_node(&list_link_);
    text::format_to(nick_, "%s", "Proskater");
    core::at(text_, 0) = '\0';
}

// 0x00548CF0
Buddy* Buddy::teardown(unsigned int flags)
{
    destroy_buddy_node(this);
    if (flags & 1)
        ram_stats()->dealloc(this);
    return this;
}

// 0x00548D20
FriendNote* FriendNote::teardown(unsigned int flags)
{
    destroy_message_node(this);
    if (flags & 1)
        ram_stats()->dealloc(this);
    return this;
}

// 0x0054A480
FriendsManager::FriendsManager()
{
    friends_.make_head();
    messages_.make_head();
    ram_stats()->enter_heap_scope(ram_stats()->network_heap());
    gp_initialize(this, net::kGameSpyProductId, 0);
    gp_set_callback(this, kGpCallbackError, code_address(&on_error), this);
    gp_set_callback(this, kGpCallbackBuddyRequest, kOnBuddyRequest, this);
    gp_set_callback(this, kGpCallbackBuddyStatus, code_address(&on_buddy_status), this);
    gp_set_callback(this, kGpCallbackBuddyMessage, code_address(&on_buddy_message), this);
    gp_set_callback(this, kGpCallbackUnused4, code_address(&stubs::do_nothing), this);
    gp_set_callback(this, kGpCallbackUnused5, code_address(&stubs::do_nothing), this);
    task_ = std::make_unique<tasks::Task<FriendsManager>>(&buddy_task, this, 0);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    authenticated_ = false;
    cur_phase_ = 0;
    account_ = 0;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    pop_heap();
}

// 0x005488B0
FriendsManager::~FriendsManager()
{
    task_.reset();
    messages_.uncouple();
    friends_.uncouple();
}

// 0x0054A1A0
void FriendsManager::log_in()
{
    if (authenticated_)
        log_off();
    push_bottom_up();
    cur_phase_ = 2;
    login_start() = frame_clock::get_time();
    get_run("log_in_profile");
    DWORD id = 0;
    HANDLE thread = CreateThread(nullptr, net::kSdkThreadStackBytes, kLoginThread, this, CREATE_SUSPENDED, &id);
    net_game_state().online_hub()->set_room_id(id);
    ResumeThread(thread); // (the handle is never closed)
    if (!task_->is_on_task_list())
        get_logic_tasks()->queue_job(task_.get());
    pop_heap();
}

// 0x0054A5C0
void FriendsManager::spawn_account()
{
    if (authenticated_)
        log_off();
    cur_phase_ = 1;
    login_start() = frame_clock::get_time();
    push_top_down();
    DWORD id = 0;
    HANDLE thread = CreateThread(nullptr, net::kSdkThreadStackBytes, &create_profile_thread, this, CREATE_SUSPENDED, &id);
    net_game_state().online_hub()->set_room_id(id);
    ResumeThread(thread);
    get_logic_tasks()->queue_job(task_.get()); // (not checked for being in the list already, unlike LogIn)
    pop_heap();
}

// 0x00549B90
void FriendsManager::log_off()
{
    gp_disconnect(this);
    task_->drop_out();
    authenticated_ = false;
    lists::Hunt<Buddy> buddies;
    for (Buddy* buddy = buddies.head_element(&friends_); buddy;) {
        Buddy* next = buddies.following_item();
        unlink_list_node(buddy);
        buddy = next;
    }
    lists::Hunt<FriendNote> messages;
    for (FriendNote* message = messages.head_element(&messages_); message;) {
        FriendNote* next = messages.following_item();
        unlink_list_node(message);
        message = next;
    }
}

// 0x00548D50
void FriendsManager::add_buddy(const char* nick, int profile)
{
    push_bottom_up();
    Buddy* buddy = alloc_friend_entry();
    text_tools::dup_from(buddy->nick().data(), nick);
    buddy->set_account(profile);
    buddy->set_cur_standing(0);
    buddy->set_pending(true);
    get_append(&friends_, &buddy->list_link());
    gp_send_buddy_request(this, profile, "");
    pop_heap();
}

// 0x00549A50
void FriendsManager::remove_buddy(int profile)
{
    lists::Hunt<Buddy> search;
    for (Buddy* buddy = search.head_element(&friends_); buddy; buddy = search.following_item()) {
        if (buddy->account() == profile) {
            unlink_list_node(buddy);
            break;
        }
    }
    gp_delete_buddy(this, profile);
    const script::StructPtr params = script::make_struct();
    params->put_name_key("id", get_checksums("first_lobby_buddy") + profile);
    get_run("removed_buddy", params.get());
}

// 0x005499A0
void FriendsManager::cancel_add_buddy()
{
    lists::Hunt<Buddy> search;
    for (Buddy* buddy = search.head_element(&friends_); buddy; buddy = search.following_item()) {
        if (buddy->pending()) {
            unlink_list_node(buddy);
            return;
        }
    }
}

// 0x00549CB0
bool FriendsManager::is_buddy(int profile)
{
    lists::Hunt<Buddy> search;
    for (const Buddy* buddy = search.head_element(&friends_); buddy; buddy = search.following_item()) {
        if (buddy->account() == profile)
            return true;
    }
    return false;
}

// 0x00548E10
int FriendsManager::count_buddies() { return total_of(&friends_); }

// 0x005498E0
void FriendsManager::fill_buddy_list(bool clear, bool allow_join, bool allow_remove)
{
    if (clear)
        get_run("destroy_lobby_buddy_list_children");
    lists::Hunt<Buddy> search;
    for (Buddy* buddy = search.head_element(&friends_); buddy; buddy = search.following_item()) {
        if (!buddy->pending())
            show_buddy(buddy, allow_join, allow_remove);
    }
}

// 0x00549F50
void FriendsManager::fill_prospective_buddy_list()
{
    Hub* manager = net_game_state().online_hub();
    detail::PlayerSearch search;
    push_top_down();
    for (PlayerRecord* player = detail::start_roster_sweep(manager, &search, true); player;
         player = detail::step_roster_sweep(manager, &search, true)) {
        const int profile = player->profile_id();
        if (detail::is_own_player(player) || profile == 0 || is_buddy(profile))
            continue;
        const char* name = detail::player_name(player);
        script::StructPtr params = script::make_struct();
        params->append_word("text", name);
        params->put_name_key("id", static_cast<uint32_t>(profile));
        params->put_name_key("pad_choose_script", get_checksums("add_buddy"));
        params->put_name_key("parent_menu_id", get_checksums("lobby_buddy_list_menu"));
        const script::StructPtr choose = script::make_struct();
        choose->put_int("profile", profile);
        choose->append_word("nick", name);
        params->put_sub_struct("pad_choose_params", choose.get());
        params->push_float("scale", kDefaultMenuScale);
        get_run("make_text_sub_menu_item", params.get());
        params.reset();
    }
    pop_heap();
}

// 0x00548560
void FriendsManager::show_buddy(Buddy* buddy, bool allow_join, bool allow_remove)
{
    push_top_down();
    script::StructPtr params = script::make_struct();
    // (the original uses the nick as sprintf's format: a '%' in a nick would be expanded)
    constexpr size_t kNickSize = 1024;
    std::array<char, kNickSize> nick{};
    text::format_to(nick, "%s", buddy->nick().data());
    params->append_word("name", buddy->nick().data());
    params->append_word("status", buddy->status_text().data());
    params->append_word("location", buddy->location().data());
    params->put_name_key("id", get_checksums("first_lobby_buddy") + buddy->account());
    const char* choose_script = nullptr;
    if (!allow_join && !allow_remove) {
        choose_script = "do_nothing";
    } else {
        gsl::owner<script::Struct*> choose = script::new_struct();
        choose->put_int("profile", buddy->account());
        text::to_capitals(nick.data());
        choose->append_word("nick", nick.data());
        choose->append_word("status", buddy->status_text().data());
        choose->append_word("location", buddy->location().data());
        choose->put_int("join_ip", static_cast<int>(buddy->join_ip()));
        choose->put_int("join_private_ip", static_cast<int>(buddy->join_private_ip()));
        choose->put_int("join_port", buddy->join_port());
        if (allow_remove)
            choose->put_name_key(checksums::Name{0u}, get_checksums("allow_remove"));
        if (allow_join) {
            if (buddy->cur_standing() == kStatusPlaying)
                choose->put_name_key(checksums::Name{0u}, get_checksums("allow_join"));
            choose->put_name_key(checksums::Name{0u}, get_checksums("in_lobby"));
        } else {
            choose->put_name_key("back_script", get_checksums("back_from_shell_buddy_options"));
            choose->put_name_key("remove_script", get_checksums("shell_remove_buddy"));
        }
        params->put_sub_struct("pad_choose_params", choose); // (choose is never freed - as in the original)
        choose_script = "present_buddy_options";
    }
    params->put_name_key("pad_choose_script", get_checksums(choose_script));
    params->put_name_key("parent_menu_id", get_checksums("lobby_buddy_list_menu"));
    if (allow_join)
        params->push_float("scale", kDefaultMenuScale);
    get_run("homie_list_add_item", params.get());
    params.reset();
    pop_heap();
}

// 0x00549ED0
void FriendsManager::on_error(void*, const void* arg, FriendsManager* man)
{
    switch (static_cast<const GpErrorArg*>(arg)->error_code_) {
    case kErrorNewUserNick:
        man->cur_phase_ = 3;
        break;
    case kErrorLogin:
        man->log_off();
        get_run("CreateBuddyLoginFailedDialog");
        break;
    case kErrorPassword:
        get_run("create_wrong_profile_password_dialog");
        man->log_off();
        break;
    case kErrorAlreadyBuddy:
        get_run("failed_add_buddy_already_buddy");
        break;
    default:
        break;
    }
}

// 0x00548F50
void FriendsManager::on_buddy_status(void*, const void* argument, FriendsManager* man)
{
    auto* arg = static_cast<const GpRecvBuddyStatusArg*>(argument);
    push_bottom_up();
    GpBuddyStatus status{};
    gp_get_buddy_status(man, arg->index, &status);
    lists::Hunt<Buddy> search;
    for (Buddy* buddy = search.head_element(&man->friends_); buddy; buddy = search.following_item()) {
        if (buddy->account() != status.profile)
            continue;
        buddy->set_cur_standing(status.status);
        text_tools::dup_from(buddy->status_text().data(), status.status_string_.data());
        if (buddy->cur_standing() == kStatusPlaying) {
            if (!is_thug2_location(status.location_string_.data())) {
                pop_heap();
                return;
            }
            parse_location(buddy, status.location_string_.data(), true);
        } else {
            text_tools::dup_from(buddy->location().data(), status.location_string_.data());
        }
        if (buddy->pending()) {
            // An answer to our request: a buddy now, kept in the buddy_array preference too.
            script::StructPtr params = script::make_struct();
            params->append_word("net_name", buddy->nick().data());
            get_run("added_buddy", params.get());
            params.reset();
            buddy->set_pending(false);
            prefs::Preferences* prefs = net_game_state().online_hub()->get_online_settings();
            script::Array* old = prefs->get_setting_list(get_checksums("buddy_array"));
            gsl::owner<script::Array*> memory = script::alloc_script_array();
            script::Array* buddies = memory ? memory::construct_in<script::Array>(memory) : nullptr;
            buddies->configure_size_kind(old->extent() + 1, script::kQbvalStruct);
            uint32_t i = 0;
            for (; i < old->extent(); ++i) {
                auto* entry = old->struct_at(i);
                gsl::owner<script::Struct*> copy = script::new_struct();
                copy->get_assign(entry);
                buddies->set_cell(i, script::Cell::of(copy));
            }
            gsl::owner<script::Struct*> entry = script::new_struct();
            entry->put_int("profile", buddy->account());
            entry->append_word("nick", buddy->nick().data());
            buddies->set_cell(i, script::Cell::of(entry));
            prefs->change_option_list(get_checksums("buddy_array"), buddies);
        }
        script::StructPtr params = script::make_struct();
        params->append_word(get_checksums("name"), script::kQbvalString, buddy->nick().data());
        params->append_word(get_checksums("status"), script::kQbvalString, buddy->status_text().data());
        params->append_word(get_checksums("location"), script::kQbvalString, buddy->location().data());
        params->add_component_of_type(get_checksums("id"), script::kQbvalName,
                                      get_checksums("first_lobby_buddy") + buddy->account());
        get_run("update_buddy_status", params.get());
        params.reset();
        pop_heap();
        return;
    }

    // Not on the list: someone who made us their buddy.
    prefs::Preferences* prefs = net_game_state().online_hub()->get_online_settings();
    Buddy* buddy = alloc_friend_entry();
    buddy->set_account(status.profile);
    buddy->set_cur_standing(status.status);
    text_tools::dup_from(buddy->status_text().data(), status.status_string_.data());
    text::format_to(buddy->nick(), "%s", "ProSkater");
    if (buddy->cur_standing() == kStatusPlaying) {
        if (!is_thug2_location(status.location_string_.data())) {
            unlink_list_node(buddy);
            pop_heap();
            return;
        }
        parse_location(buddy, status.location_string_.data(), false);
    } else {
        text_tools::dup_from(buddy->location().data(), status.location_string_.data());
    }
    get_append(&man->friends_, &buddy->list_link());
    // Its nick, if the buddy_array preference knows the profile.
    script::Array* buddies = prefs->get_setting_list(get_checksums("buddy_array"));
    if (buddies) {
        for (uint32_t i = 0; i < buddies->extent(); ++i) {
            auto* entry = buddies->struct_at(i);
            int profile = 0;
            entry->fetch_int_value("profile", &profile, 1);
            if (profile == status.profile) {
                const char* nick = nullptr;
                entry->fetch_text("nick", &nick, 1);
                text_tools::dup_from(buddy->nick().data(), nick);
            }
        }
    }
    pop_heap();
}

// 0x005496F0
void FriendsManager::on_buddy_message(void*, const void* argument, FriendsManager* man)
{
    auto* arg = static_cast<const GpRecvBuddyMessageArg*>(argument);
    if (total_of(&man->messages_) > kMaxMessages)
        return;
    push_top_down();
    const size_t length = std::strlen(arg->message);
    void* memory = memory::alloc_zeroed(sizeof(FriendNote));
    FriendNote* message = memory ? memory::construct_in<FriendNote>(memory) : nullptr;
    message->set_account(arg->profile);
    const char* text = text_tools::get_token(arg->message, "\n");
    if (length == std::strlen(text)) {
        // One line: from a buddy (else dropped). (A nick of 16 or more characters runs into the text, as in the
        // original.)
        text_tools::dup_from(message->text().data(), text);
        Buddy* sender = nullptr;
        lists::Hunt<Buddy> search;
        for (Buddy* buddy = search.head_element(&man->friends_); buddy; buddy = search.following_item()) {
            if (buddy->account() == arg->profile) {
                sender = buddy;
                break;
            }
        }
        if (!sender) {
            unlink_list_node(message);
            pop_heap();
            return;
        }
        text_tools::dup_from(message->nick().data(), sender->nick().data());
    } else {
        // "nick\ntext"
        text_tools::dup_from(message->nick().data(), text);
        text_tools::dup_from(message->text().data(), std::next(text, static_cast<std::ptrdiff_t>(std::strlen(text) + 1)));
    }
    script::launch_script(kFlashBuddyMessageIcon, nullptr, nullptr, false, nullptr);
    get_append(&man->messages_, &message->list_link());
    pop_heap();
}

// 0x0054A2C0
void FriendsManager::on_connect_new_user(void*, const void* argument, FriendsManager* man)
{
    auto* arg = static_cast<const GpConnectResponseArg*>(argument);
    if (arg->result == 0) {
        mark_friend_online(man, arg->profile);
    } else if (arg->result == 4 && creating_profile()) { // the nick is taken: the profile exists - log in
        creating_profile() = false;
        man->log_in();
    }
}

// 0x0054A300
void FriendsManager::buddy_task(tasks::Task<FriendsManager>* task)
{
    push_top_down();
    FriendsManager* man = task->content();
    switch (man->cur_phase_) {
    case 1: // creating the profile
    case 2: // logging in
        if (frame_clock::get_time() - login_start() > kLoginTimeout) {
            man->log_off();
            get_run("CreateBuddyLoginFailedDialog");
        } else {
            gp_process(man);
        }
        break;
    case 3: // the nick was taken by a new user: log in
        man->log_in();
        break;
    case 4: // logged in
        if (frame_clock::get_time() - last_process() > kProcessInterval) {
            gp_process(man);
            last_process() = frame_clock::get_time();
        }
        break;
    default:
        break;
    }
    pop_heap();
}

// 0x0054A3D0
unsigned long __stdcall FriendsManager::create_profile_thread(void* parameter)
{
    auto* man = static_cast<FriendsManager*>(parameter);
    push_bottom_up();
    prefs::Preferences* prefs = net_game_state().online_hub()->get_online_settings();
    const char* email = prefs->get_preference_string({.preference = get_checksums("profile_email"), .field = get_checksums("ui_string")});
    const char* password = prefs->get_preference_string({.preference = get_checksums("profile_password"), .field = get_checksums("ui_string")});
    creating_profile() = true;
    gp_connect_new_user(man, "proskater", nullptr, email, password, nullptr, 1, 0,
                        code_address(&on_connect_new_user), man);
    pop_heap();
    return 0;
}

// --- The script commands ---

// 0x00548940: the unique_id preference: the time now (ms) as text.
bool set_unique_id(script::Struct*, script::Script*)
{
    const uint32_t now = frame_clock::get_time();
    prefs::Preferences* prefs = net_game_state().online_hub()->get_online_settings();
    script::StructPtr params = script::make_struct();
    constexpr size_t kTextSize = 32;
    std::array<char, kTextSize> text{};
    text::format_to(text, "%d", static_cast<int>(now));
    params->append_word("ui_string", text.data());
    prefs->change_setting(get_checksums("unique_id"), params.get());
    params.reset();
    return true;
}

// 0x00548A40: join (or with `observe` watch) the game of the buddy chosen (its location, join_ip,
// join_private_ip, join_port): the status set playing / observing there, then net_chosen_join_server with the port
// and address - a buddy behind this machine's public address is joined at its private address on THUG2's port.
bool join_buddy(script::Struct* params, script::Script*)
{
    Hub* manager = net_game_state().online_hub();
    const bool observe = params->has_bare_flag("observe");
    const char* location = nullptr;
    int ip = 0, private_ip = 0, port = 0;
    params->fetch_text("location", &location, 1);
    params->fetch_int_value("join_ip", &ip, 1);
    params->fetch_int_value("join_private_ip", &private_ip, 1);
    params->fetch_int_value("join_port", &port, 1);
    constexpr size_t kStatusSize = 1024;
    std::array<char, kStatusSize> status{};
    text::format_to(status, "10thug211:%d:%d:%d:%s", ip, private_ip, port, location);
    bool same_address = false;
    if (static_cast<uint32_t>(ip) == peer_public_ip(lobby_peer(manager->lobby_service().get()))) {
        ip = private_ip;
        port = kJoinPort;
        same_address = true;
    }
    const char* text = nullptr;
    if (observe) {
        manager->set_join_mode(1);
        text = script::fetch_text("homie_status_observing");
    } else {
        manager->set_join_mode(0);
        text = script::fetch_text("homie_status_playing");
    }
    FriendsManager* man = manager->presence_service().get();
    push_top_down();
    gp_set_status(man, kStatusPlaying, text, status.data());
    pop_heap();
    script::StructPtr join = script::make_struct();
    in_addr address{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): Winsock's in_addr (an API union)
    address.S_un.S_addr = static_cast<uint32_t>(ip);
    const char* dotted = inet_ntoa(address);
    join->add_component_of_type(0, script::kQbvalInteger, static_cast<uint32_t>(port));
    join->append_word(0, script::kQbvalString, dotted);
    if (!same_address)
        join->add_component_of_type(kCookie, script::kQbvalName, static_cast<uint32_t>(maths::rnd(kCookieRange)));
    get_run("net_chosen_join_server", join.get());
    join.reset();
    return true;
}

namespace {
FriendsManager* get_buddies() { return net_game_state().online_hub()->presence_service().get(); }
} // namespace

// 0x00548E40: AddBuddy profile nick
bool add_buddy(script::Struct* params, script::Script*)
{
    int profile = 0;
    const char* nick = nullptr;
    params->fetch_int_value("profile", &profile, 1);
    params->fetch_text("nick", &nick, 1);
    get_buddies()->add_buddy(nick, profile);
    return true;
}

// 0x00548E90
bool has_buddies(script::Struct*, script::Script*) { return total_of(&get_buddies()->friends()) > 0; }

// 0x00548ED0
bool buddy_list_full(script::Struct*, script::Script*) { return total_of(&get_buddies()->friends()) >= kMaxBuddies; }

// 0x00548F10
bool any_buddy_messages_left(script::Struct*, script::Script*) { return total_of(&get_buddies()->messages()) != 0; }

// 0x00549D10
bool profile_log_off(script::Struct*, script::Script*)
{
    get_buddies()->log_off();
    return true;
}

// 0x00549D20: FillBuddyList [clear_list] [allow_join] [allow_remove]
bool fill_buddy_list(script::Struct* params, script::Script*)
{
    const bool clear = params->has_bare_flag("clear_list");
    const bool allow_join = params->has_bare_flag("allow_join");
    const bool allow_remove = params->has_bare_flag("allow_remove");
    get_buddies()->fill_buddy_list(clear, allow_join, allow_remove);
    return true;
}

// 0x00549D80
bool cancel_add_buddy(script::Struct*, script::Script*)
{
    get_buddies()->cancel_add_buddy();
    return true;
}

// 0x00549D90: RemoveBuddy profile
bool remove_buddy(script::Struct* params, script::Script*)
{
    int profile = 0;
    params->fetch_int_value("profile", &profile, 1);
    get_buddies()->remove_buddy(profile);
    return true;
}

// 0x00549DC0: the oldest buddy message into the script's parameters (its nick twice, the text, the sender's
// profile) and deleted. (There must be one: AnyBuddyMessagesLeft.)
bool show_next_buddy_message(script::Struct*, script::Script* script)
{
    lists::Hunt<FriendNote> search;
    FriendNote* message = search.head_element(&get_buddies()->messages());
    script->args()->append_word(kNick, message->nick().data());
    script->args()->append_word(kNetName, message->nick().data());
    script->args()->append_word(kMsgBody, message->text().data());
    script->args()->put_int(checksums::Name{kPROFILE}, message->account());
    unlink_list_node(message);
    // (the original then counts the messages left and ignores the count)
    return true;
}

// 0x00549EA0: IsAlreadyMyBuddy <profile 0x7EA855F0>
bool is_already_my_buddy(script::Struct* params, script::Script*)
{
    int profile = 0; // (left unset in the original when the parameter is missing)
    params->fetch_int_value(kPROFILE, &profile, 0);
    return get_buddies()->is_buddy(profile);
}

// 0x0054A250: log in if the profile_success preference is boolean_true (else false).
bool profile_log_in(script::Struct*, script::Script*)
{
    prefs::Preferences* prefs = net_game_state().online_hub()->get_online_settings();
    if (prefs->get_preference_checksum({.preference = get_checksums("profile_success"), .field = get_checksums("checksum")}) !=
        get_checksums("boolean_true"))
        return false;
    get_buddies()->log_in();
    return true;
}

// 0x0054A2B0
bool fill_prospective_buddy_list(script::Struct*, script::Script*)
{
    get_buddies()->fill_prospective_buddy_list();
    return true;
}

// 0x0054A650
bool spawn_account(script::Struct*, script::Script*)
{
    get_buddies()->spawn_account();
    return true;
}

} // namespace online

