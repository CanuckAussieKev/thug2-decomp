// The buddy list's GP requests, messages and status, its list types, and the folded getters (see buddy_status.h).
// GameSpy presence (GP) is library code, called by address.

#include "engine/net/buddy_status.h"
#include "platform/gamespy.h"

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/memory/arena_alloc.h"
#include "base/core/checksums.h"
#include "engine/memory/manager.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h" // IWYU pragma: keep - the detail namespace (using-directive)
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/process_flags.h"
#include "base/core/format.h"
#include "engine/net/net_game_state.h"

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kUiString = 0x96875C0F; // ui_string
constexpr uint32_t kString = 0x61414D56;   // string
constexpr int kStatusOnline = 1;
constexpr int kStatusChatting = 4;

memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
FriendsManager* get_buddies() { return net_game_state().online_hub()->presence_service().get(); }

void unlink_node(memory::ListNode* node)
{
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
}

} // namespace

// 0x00548100
void FriendsManager::on_buddy_request(const BuddyRequest& request)
{
    auto* const connection = request.connection;
    const auto* const arg = request.arg;
    auto* const man = request.man;
    (void)connection;
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    game_spy::gp_auth_buddy_request(man, *static_cast<const int*>(arg)); // (the request's profile accepted)
    ram_stats()->leave_heap_scope();
}

std::array<char, kCommandLineEmailBytes>& command_line_email()
{
    constexpr size_t kCommandLineEmailSize = 256;
    static std::array<char, kCommandLineEmailSize> command_line_email{};
    return command_line_email;
}

std::array<char, kCommandLinePasswordBytes>& command_line_password()
{
    constexpr size_t kCommandLinePasswordSize = 16;
    static std::array<char, kCommandLinePasswordSize> command_line_password{};
    return command_line_password;
}

// 0x00548140
unsigned long __stdcall FriendsManager::login_thread(void* man)
{
    Hub* manager = net_game_state().online_hub();
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap());
    prefs::Preferences* preferences = manager->get_online_settings();
    const char* email = nullptr;
    const char* password = nullptr;
    if (sys::internet_game_host() || sys::joining_internet_game()) {
        email = command_line_email().data();
        password = command_line_password().data();
    } else {
        email = preferences->get_preference_string({.preference = checksums::hash_from_text("profile_email"), .field = checksums::hash_from_text("ui_string")});
        password = preferences->get_preference_string({.preference = checksums::hash_from_text("profile_password"), .field = checksums::hash_from_text("ui_string")});
    }
    game_spy::gp_connect(man, "proskater", email, password, 1, 0, &FriendsManager::on_connect_new_user,
                         man); // (gpConnect)
    ram_stats()->leave_heap_scope();
    return 0;
}

// 0x00548200
void FriendsManager::send_buddy_message(int profile, const char* text)
{
    const char* nick = nullptr;
    net_game_state().online_hub()
        ->get_online_settings()
        ->fetch_setting(checksums::hash_from_text("network_id"))
        ->fetch_text(kUiString, &nick, 1);
    constexpr size_t kMessageSize = 128;
    std::array<char, kMessageSize> message{};
    text::format_to(message, "%s\n%s", nick, text);
    game_spy::gp_send_buddy_message(this, profile, message.data()); // (gpSendBuddyMessage)
}

// 0x005482A0
void FriendsManager::change_online_flag(int profile)
{
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    authenticated_ = true;
    account_ = profile;
    cur_phase_ = 4;
    script::launch_script("profile_logged_in", nullptr, nullptr, false);
    const char* location = script::fetch_text("homie_status_logging_in");
    game_spy::gp_set_status(this, kStatusOnline, script::fetch_text("homie_status_online"), location); // (gpSetStatus)
    ram_stats()->leave_heap_scope();
}

// 0x00548320
void FriendsManager::change_standing(int status, const char* text, const char* location)
{
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    game_spy::gp_set_status(this, status, text, location); // (gpSetStatus)
    ram_stats()->leave_heap_scope();
}

// --- The script commands ---

// 0x00548360: whether we're logged in to presence.
bool profile_logged_in(script::Struct* params, script::Script* script)
{
    (void)params;
    (void)script;
    return get_buddies()->authenticated();
}

// 0x00548370: the message `string` sent to `profile` (false for none or an empty one).
bool send_buddy_message(script::Struct* params, script::Script* script)
{
    (void)script;
    const Hub* manager = net_game_state().online_hub();
    const char* text = nullptr;
    params->fetch_text(kString, &text, 0);
    if (!text || (*text == '\0')) {
        return false;
    }
    int profile = 0;
    params->fetch_int_value("profile", &profile, 1);
    manager->presence_service()->send_buddy_message(profile, text);
    return true;
}

// 0x005483E0: our presence status: chatting in the lobby.
bool set_lobby_status(script::Struct* params, script::Script* script)
{
    (void)params;
    (void)script;
    const Hub* manager = net_game_state().online_hub();
    const char* lobby = manager->lobby_service()->room_name().data(); // (0x0053F820)
    const char* text = script::fetch_text("homie_status_chatting");
    FriendsManager* buddies = manager->presence_service().get();
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    game_spy::gp_set_status(buddies, kStatusChatting, text, lobby); // (gpSetStatus)
    ram_stats()->leave_heap_scope();
    return true;
}

} // namespace online

