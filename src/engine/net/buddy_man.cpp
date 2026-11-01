// The online profile and buddy list (online::FriendsManager). See buddy_man.h.

// NOLINTNEXTLINE(bugprone-reserved-identifier, readability-identifier-naming): Winsock's own macro
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_ntoa: what the game uses

#include "engine/net/buddy_man.h"
#include "engine/net/gamespy.h"

#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
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

namespace online {

namespace {

using detail::delete_params;
using detail::new_params;

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

bool g_creating_profile;
uint32_t g_buddy_login_start;
uint32_t g_last_process;

// The login's start (the task's 10 s limit), when GP was last processed once logged in, and "a new profile is being
// made" (its gpConnectNewUser answer "nick taken" logs in instead).
uint32_t& login_start() { return g_buddy_login_start; }
uint32_t& last_process() { return g_last_process; }
bool& creating_profile() { return g_creating_profile; }

Hub* net_session_slot() { return g_online_hub; }
memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
void push_top_down() { ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap_); }
void push_bottom_up() { ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap_); }
void pop_heap() { ram_stats()->leave_heap_scope(); }
tasks::PileUp* get_logic_tasks() { return (&assets::g_main_loop_manager->logic_tasks_); }

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
struct GpBuddyStatus {
    int profile;
    int status;
    char status_string_[256];
    char location_string_[256];
    uint32_t ip;
    int port;
    uint8_t rest[0x20];
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
void destroy_buddy_node(Buddy* buddy) { buddy->list_link_.uncouple(); }            // (0x005484A0)
void destroy_message_node(FriendNote* message) { message->list_link_.uncouple(); } // (0x005484C0)
// 0x00548100: GP's buddy request callback (the request accepted); 0x00548140: the login thread.
const void* const kOnBuddyRequest = code_address(FriendsManager::on_buddy_request);
const auto kLoginThread = reinterpret_cast<LPTHREAD_START_ROUTINE>(code_address(FriendsManager::login_thread));
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
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
// The items of a list (the original's count checks each node's data for the head before checking it for null).
int total_of(const memory::ListNode* head)
{
    int count = 0;
    for (const memory::ListNode* node = head->next; node->data != reinterpret_cast<void*>(-1) && node;
         node = node->next)
        ++count;
    return count;
}

// A new buddy (the constructor inlined in the original): offline, not pending, no location. (A failed allocation
// isn't checked by the callers.)
Buddy* alloc_friend_entry()
{
    auto* buddy = static_cast<Buddy*>(memory::alloc_zeroed(sizeof(Buddy)));
    if (buddy) {
        init_node(&buddy->list_link_);
        buddy->pending_ = false;
        buddy->cur_standing_ = 0;
        buddy->location_[0] = '\0';
        std::snprintf(buddy->status_text_, sizeof(buddy->status_text_), "%s", "Offline");
    }
    return buddy;
}

// A THUG2 location string's parts into `buddy`: the game's name (after the fourth ':' - is_thug2_location has checked
// there are four) and the address. `checked`: the original checks each ':' is found when updating a buddy (and
// copies from address 0 if not - a crash), not when making one.
void parse_location(Buddy* buddy, char* location, bool checked)
{
    char* name = std::strchr(location, ':');
    if (checked) {
        // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): the original's, as written
        if (name && (name = std::strchr(name + 1, ':')) && (name = std::strchr(name + 1, ':')) &&
            // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): the original's, as written
            (name = std::strchr(name + 1, ':')))
            ++name;
    } else {
        name = std::strchr(name + 1, ':');
        name = std::strchr(name + 1, ':');
        name = std::strchr(name + 1, ':') + 1;
    }
    text_tools::dup_from(buddy->location_, name);
    text_tools::get_token(location, ":"); // "10thug211"
    buddy->join_ip_ = static_cast<uint32_t>(std::atol(text_tools::get_token(nullptr, ":")));
    buddy->join_private_ip_ = static_cast<uint32_t>(std::atol(text_tools::get_token(nullptr, ":")));
    buddy->join_port_ = std::atol(text_tools::get_token(nullptr, ":"));
}

} // namespace

// 0x00548CB0
FriendNote::FriendNote()
{
    init_node(&list_link_);
    account_ = 0;
    std::snprintf(nick_, sizeof(nick_), "%s", "Proskater");
    text_[0] = '\0';
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
    ram_stats()->enter_heap_scope(ram_stats()->network_heap_);
    gp_initialize(this, 0x2880, 0);
    gp_set_callback(this, 0, reinterpret_cast<const void*>(&on_error), this);
    gp_set_callback(this, 1, kOnBuddyRequest, this);
    gp_set_callback(this, 2, reinterpret_cast<const void*>(&on_buddy_status), this);
    gp_set_callback(this, 3, reinterpret_cast<const void*>(&on_buddy_message), this);
    gp_set_callback(this, 4, reinterpret_cast<const void*>(&stubs::do_nothing), this);
    gp_set_callback(this, 5, reinterpret_cast<const void*>(&stubs::do_nothing), this);
    task_ = std::make_unique<tasks::Task<FriendsManager>>(&buddy_task, this, 0);
    authenticated_ = false;
    cur_phase_ = 0;
    account_ = 0;
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
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0x8000, kLoginThread, this, CREATE_SUSPENDED, &id);
    net_session_slot()->set_room_id(id);
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
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0x8000, &create_profile_thread, this, CREATE_SUSPENDED, &id);
    net_session_slot()->set_room_id(id);
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
    text_tools::dup_from(buddy->nick_, nick);
    buddy->account_ = profile;
    buddy->cur_standing_ = 0;
    buddy->pending_ = true;
    get_append(&friends_, &buddy->list_link_);
    gp_send_buddy_request(this, profile, "");
    pop_heap();
}

// 0x00549A50
void FriendsManager::remove_buddy(int profile)
{
    lists::Hunt<Buddy> search;
    for (Buddy* buddy = search.head_element(&friends_); buddy; buddy = search.following_item()) {
        if (buddy->account_ == profile) {
            unlink_list_node(buddy);
            break;
        }
    }
    gp_delete_buddy(this, profile);
    script::Struct* params = new_params();
    params->put_name_key("id", get_checksums("first_lobby_buddy") + profile);
    get_run("removed_buddy", params);
    delete_params(params);
}

// 0x005499A0
void FriendsManager::cancel_add_buddy()
{
    lists::Hunt<Buddy> search;
    for (Buddy* buddy = search.head_element(&friends_); buddy; buddy = search.following_item()) {
        if (buddy->pending_) {
            unlink_list_node(buddy);
            return;
        }
    }
}

// 0x00549CB0
bool FriendsManager::is_buddy(int profile)
{
    lists::Hunt<Buddy> search;
    for (Buddy* buddy = search.head_element(&friends_); buddy; buddy = search.following_item()) {
        if (buddy->account_ == profile)
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
        if (!buddy->pending_)
            show_buddy(buddy, allow_join, allow_remove);
    }
}

// 0x00549F50
void FriendsManager::fill_prospective_buddy_list()
{
    Hub* manager = net_session_slot();
    detail::PlayerSearch search;
    push_top_down();
    for (PlayerRecord* player = detail::start_roster_sweep(manager, &search, true); player;
         player = detail::step_roster_sweep(manager, &search, true)) {
        int profile = player->profile_id_;
        if (detail::is_own_player(player) || profile == 0 || is_buddy(profile))
            continue;
        const char* name = detail::player_name(player);
        script::Struct* params = new_params();
        params->append_word("text", name);
        params->put_name_key("id", static_cast<uint32_t>(profile));
        params->put_name_key("pad_choose_script", get_checksums("add_buddy"));
        params->put_name_key("parent_menu_id", get_checksums("lobby_buddy_list_menu"));
        script::Struct* choose = new_params();
        choose->put_int("profile", profile);
        choose->append_word("nick", name);
        params->put_sub_struct("pad_choose_params", choose);
        params->push_float("scale", 1.1f);
        get_run("make_text_sub_menu_item", params);
        delete_params(params);
        delete_params(choose);
    }
    pop_heap();
}

// 0x00548560
void FriendsManager::show_buddy(Buddy* buddy, bool allow_join, bool allow_remove)
{
    push_top_down();
    script::Struct* params = new_params();
    // (the original uses the nick as sprintf's format: a '%' in a nick would be expanded)
    char nick[1024];
    std::snprintf(nick, sizeof(nick), "%s", buddy->nick_);
    params->append_word("name", buddy->nick_);
    params->append_word("status", buddy->status_text_);
    params->append_word("location", buddy->location_);
    params->put_name_key("id", get_checksums("first_lobby_buddy") + buddy->account_);
    const char* choose_script;
    if (!allow_join && !allow_remove) {
        choose_script = "do_nothing";
    } else {
        script::Struct* choose = new_params();
        choose->put_int("profile", buddy->account_);
        text::to_capitals(nick);
        choose->append_word("nick", nick);
        choose->append_word("status", buddy->status_text_);
        choose->append_word("location", buddy->location_);
        choose->put_int("join_ip", static_cast<int>(buddy->join_ip_));
        choose->put_int("join_private_ip", static_cast<int>(buddy->join_private_ip_));
        choose->put_int("join_port", buddy->join_port_);
        if (allow_remove)
            choose->put_name_key(0u, get_checksums("allow_remove"));
        if (allow_join) {
            if (buddy->cur_standing_ == kStatusPlaying)
                choose->put_name_key(0u, get_checksums("allow_join"));
            choose->put_name_key(0u, get_checksums("in_lobby"));
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
        params->push_float("scale", 1.1f);
    get_run("homie_list_add_item", params);
    delete_params(params);
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
    GpBuddyStatus status;
    gp_get_buddy_status(man, arg->index, &status);
    lists::Hunt<Buddy> search;
    for (Buddy* buddy = search.head_element(&man->friends_); buddy; buddy = search.following_item()) {
        if (buddy->account_ != status.profile)
            continue;
        buddy->cur_standing_ = status.status;
        text_tools::dup_from(buddy->status_text_, status.status_string_);
        if (buddy->cur_standing_ == kStatusPlaying) {
            if (!is_thug2_location(status.location_string_)) {
                pop_heap();
                return;
            }
            parse_location(buddy, status.location_string_, true);
        } else {
            text_tools::dup_from(buddy->location_, status.location_string_);
        }
        if (buddy->pending_) {
            // An answer to our request: a buddy now, kept in the buddy_array preference too.
            script::Struct* params = new_params();
            params->append_word("net_name", buddy->nick_);
            get_run("added_buddy", params);
            delete_params(params);
            buddy->pending_ = false;
            prefs::Preferences* prefs = net_session_slot()->get_online_settings();
            script::Array* old = prefs->get_setting_list(get_checksums("buddy_array"));
            script::Array* memory = script::alloc_script_array();
            script::Array* buddies = memory ? new (memory) script::Array : nullptr;
            buddies->configure_size_kind(old->extent_ + 1, script::kQbvalStruct);
            uint32_t i = 0;
            for (; i < old->extent_; ++i) {
                auto* entry = reinterpret_cast<script::Struct*>(old->fetch_part(i));
                script::Struct* copy = new_params();
                copy->get_assign(entry);
                buddies->set_element(i, reinterpret_cast<uint32_t>(copy));
            }
            script::Struct* entry = new_params();
            entry->put_int("profile", buddy->account_);
            entry->append_word("nick", buddy->nick_);
            buddies->set_element(i, reinterpret_cast<uint32_t>(entry));
            prefs->change_option_list(get_checksums("buddy_array"), buddies);
        }
        script::Struct* params = new_params();
        params->append_word(get_checksums("name"), script::kQbvalString, buddy->nick_);
        params->append_word(get_checksums("status"), script::kQbvalString, buddy->status_text_);
        params->append_word(get_checksums("location"), script::kQbvalString, buddy->location_);
        params->add_component_of_type(get_checksums("id"), script::kQbvalName,
                                      get_checksums("first_lobby_buddy") + buddy->account_);
        get_run("update_buddy_status", params);
        delete_params(params);
        pop_heap();
        return;
    }

    // Not on the list: someone who made us their buddy.
    prefs::Preferences* prefs = net_session_slot()->get_online_settings();
    Buddy* buddy = alloc_friend_entry();
    buddy->account_ = status.profile;
    buddy->cur_standing_ = status.status;
    text_tools::dup_from(buddy->status_text_, status.status_string_);
    std::snprintf(buddy->nick_, sizeof(buddy->nick_), "%s", "ProSkater");
    if (buddy->cur_standing_ == kStatusPlaying) {
        if (!is_thug2_location(status.location_string_)) {
            unlink_list_node(buddy);
            pop_heap();
            return;
        }
        parse_location(buddy, status.location_string_, false);
    } else {
        text_tools::dup_from(buddy->location_, status.location_string_);
    }
    get_append(&man->friends_, &buddy->list_link_);
    // Its nick, if the buddy_array preference knows the profile.
    script::Array* buddies = prefs->get_setting_list(get_checksums("buddy_array"));
    if (buddies) {
        for (uint32_t i = 0; i < buddies->extent_; ++i) {
            auto* entry = reinterpret_cast<script::Struct*>(buddies->fetch_part(i));
            int profile;
            entry->fetch_int_value("profile", &profile, 1);
            if (profile == status.profile) {
                const char* nick = nullptr;
                entry->fetch_text("nick", &nick, 1);
                text_tools::dup_from(buddy->nick_, nick);
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
    size_t length = std::strlen(arg->message);
    void* memory = memory::alloc_zeroed(sizeof(FriendNote));
    FriendNote* message = memory ? new (memory) FriendNote : nullptr;
    message->account_ = arg->profile;
    char* text = text_tools::get_token(arg->message, "\n");
    if (length == std::strlen(text)) {
        // One line: from a buddy (else dropped). (A nick of 16 or more characters runs into the text, as in the
        // original.)
        text_tools::dup_from(message->text_, text);
        Buddy* sender = nullptr;
        lists::Hunt<Buddy> search;
        for (Buddy* buddy = search.head_element(&man->friends_); buddy; buddy = search.following_item()) {
            if (buddy->account_ == arg->profile) {
                sender = buddy;
                break;
            }
        }
        if (!sender) {
            unlink_list_node(message);
            pop_heap();
            return;
        }
        text_tools::dup_from(message->nick_, sender->nick_);
    } else {
        // "nick\ntext"
        text_tools::dup_from(message->nick_, text);
        text_tools::dup_from(message->text_, text + std::strlen(text) + 1);
    }
    script::launch_script(0x10C3647Cu, nullptr, nullptr, false, nullptr);
    get_append(&man->messages_, &message->list_link_);
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
    FriendsManager* man = task->content_;
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
    prefs::Preferences* prefs = net_session_slot()->get_online_settings();
    const char* email = prefs->get_preference_string(get_checksums("profile_email"), get_checksums("ui_string"));
    const char* password = prefs->get_preference_string(get_checksums("profile_password"), get_checksums("ui_string"));
    creating_profile() = true;
    gp_connect_new_user(man, "proskater", nullptr, email, password, nullptr, 1, 0,
                        reinterpret_cast<const void*>(&on_connect_new_user), man);
    pop_heap();
    return 0;
}

// --- The script commands ---

// 0x00548940: the unique_id preference: the time now (ms) as text.
bool set_unique_id(script::Struct*, script::Script*)
{
    uint32_t now = frame_clock::get_time();
    prefs::Preferences* prefs = net_session_slot()->get_online_settings();
    script::Struct* params = new_params();
    char text[32];
    std::snprintf(text, sizeof(text), "%d", static_cast<int>(now));
    params->append_word("ui_string", text);
    prefs->change_setting(get_checksums("unique_id"), params);
    delete_params(params);
    return true;
}

// 0x00548A40: join (or with `observe` watch) the game of the buddy chosen (its location, join_ip,
// join_private_ip, join_port): the status set playing / observing there, then net_chosen_join_server with the port
// and address - a buddy behind this machine's public address is joined at its private address on THUG2's port.
bool join_buddy(script::Struct* params, script::Script*)
{
    Hub* manager = net_session_slot();
    bool observe = params->has_bare_flag("observe");
    const char* location;
    int ip, private_ip, port;
    params->fetch_text("location", &location, 1);
    params->fetch_int_value("join_ip", &ip, 1);
    params->fetch_int_value("join_private_ip", &private_ip, 1);
    params->fetch_int_value("join_port", &port, 1);
    char status[1024];
    std::snprintf(status, sizeof(status), "10thug211:%d:%d:%d:%s", ip, private_ip, port, location);
    bool same_address = false;
    if (static_cast<uint32_t>(ip) == peer_public_ip(lobby_peer(manager->lobby_service_.get()))) {
        ip = private_ip;
        port = kJoinPort;
        same_address = true;
    }
    const char* text;
    if (observe) {
        manager->set_join_mode(1);
        text = script::fetch_text("homie_status_observing");
    } else {
        manager->set_join_mode(0);
        text = script::fetch_text("homie_status_playing");
    }
    FriendsManager* man = manager->presence_service_.get();
    push_top_down();
    gp_set_status(man, kStatusPlaying, text, status);
    pop_heap();
    script::Struct* join = new_params();
    in_addr address;
    address.S_un.S_addr = static_cast<uint32_t>(ip);
    const char* dotted = inet_ntoa(address);
    join->add_component_of_type(0, script::kQbvalInteger, static_cast<uint32_t>(port));
    join->append_word(0, script::kQbvalString, dotted);
    if (!same_address)
        join->add_component_of_type(0x751F4599, script::kQbvalName, static_cast<uint32_t>(maths::rnd(0xFFFF)));
    get_run("net_chosen_join_server", join);
    delete_params(join);
    return true;
}

namespace {
FriendsManager* get_buddies() { return net_session_slot()->presence_service_.get(); }
} // namespace

// 0x00548E40: AddBuddy profile nick
bool add_buddy(script::Struct* params, script::Script*)
{
    int profile;
    const char* nick;
    params->fetch_int_value("profile", &profile, 1);
    params->fetch_text("nick", &nick, 1);
    get_buddies()->add_buddy(nick, profile);
    return true;
}

// 0x00548E90
bool has_buddies(script::Struct*, script::Script*) { return total_of(&get_buddies()->friends_) > 0; }

// 0x00548ED0
bool buddy_list_full(script::Struct*, script::Script*) { return total_of(&get_buddies()->friends_) >= kMaxBuddies; }

// 0x00548F10
bool any_buddy_messages_left(script::Struct*, script::Script*) { return total_of(&get_buddies()->messages_) != 0; }

// 0x00549D10
bool profile_log_off(script::Struct*, script::Script*)
{
    get_buddies()->log_off();
    return true;
}

// 0x00549D20: FillBuddyList [clear_list] [allow_join] [allow_remove]
bool fill_buddy_list(script::Struct* params, script::Script*)
{
    bool clear = params->has_bare_flag("clear_list");
    bool allow_join = params->has_bare_flag("allow_join");
    bool allow_remove = params->has_bare_flag("allow_remove");
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
    int profile;
    params->fetch_int_value("profile", &profile, 1);
    get_buddies()->remove_buddy(profile);
    return true;
}

// 0x00549DC0: the oldest buddy message into the script's parameters (its nick twice, the text, the sender's
// profile) and deleted. (There must be one: AnyBuddyMessagesLeft.)
bool show_next_buddy_message(script::Struct*, script::Script* script)
{
    lists::Hunt<FriendNote> search;
    FriendNote* message = search.head_element(&get_buddies()->messages_);
    script->args_->append_word(0xD6F4D0C8, message->nick_);
    script->args_->append_word(0xD3A5478A, message->nick_);
    script->args_->append_word(0x59EDEC1C, message->text_);
    script->args_->put_int(0x7EA855F0, message->account_);
    unlink_list_node(message);
    // (the original then counts the messages left and ignores the count)
    return true;
}

// 0x00549EA0: IsAlreadyMyBuddy <profile 0x7EA855F0>
bool is_already_my_buddy(script::Struct* params, script::Script*)
{
    int profile = 0; // (left unset in the original when the parameter is missing)
    params->fetch_int_value(0x7EA855F0, &profile, 0);
    return get_buddies()->is_buddy(profile);
}

// 0x0054A250: log in if the profile_success preference is boolean_true (else false).
bool profile_log_in(script::Struct*, script::Script*)
{
    prefs::Preferences* prefs = net_session_slot()->get_online_settings();
    if (prefs->get_preference_checksum(get_checksums("profile_success"), get_checksums("checksum")) !=
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

