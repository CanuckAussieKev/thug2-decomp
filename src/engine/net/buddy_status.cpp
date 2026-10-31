// The buddy list's GP requests, messages and status, its list types, and the folded getters (see buddy_status.h).
// GameSpy presence (GP) is library code, called by address.

#include "engine/net/buddy_status.h"
#include "engine/net/gamespy.h"

#include <cstdint>
#include <cstdio>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/winmain.h"
#include "engine/core/code_address.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace online {

using namespace detail;

namespace {

constexpr uint32_t kUiString = 0x96875C0F; // ui_string
constexpr uint32_t kString = 0x61414D56;   // string
constexpr int kStatusOnline = 1;
constexpr int kStatusChatting = 4;

Hub* the_manager() { return g_online_hub; }
memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
FriendsManager* get_buddies() { return static_cast<FriendsManager*>(the_manager()->presence_service_); }

void unlink_node(memory::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

static VtableSlot g_buddy_node_vtable_slots[] = {&online::BuddyNode::teardown};
void** const kBuddyNodeVtable = reinterpret_cast<void**>(g_buddy_node_vtable_slots);
static VtableSlot g_message_node_vtable_slots[] = {&online::BuddyMessageNode::teardown};
void** const kMessageNodeVtable = reinterpret_cast<void**>(g_message_node_vtable_slots);

// 0x00548100
void __cdecl FriendsManager::on_buddy_request(void* connection, const void* arg, FriendsManager* man)
{
    (void)connection;
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap_);
    game_spy::gp_auth_buddy_request(man, *static_cast<const int*>(arg)); // (the request's profile accepted)
    ram_stats()->leave_heap_scope();
}

char g_command_line_email[256];
char g_command_line_password[16];

// 0x00548140
unsigned long __stdcall FriendsManager::login_thread(void* man)
{
    Hub* manager = the_manager();
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap_);
    prefs::Preferences* preferences = manager->get_online_settings();
    const char* email;
    const char* password;
    if (sys::g_internet_game_host || sys::g_joining_internet_game) {
        email = g_command_line_email;
        password = g_command_line_password;
    } else {
        email = preferences->get_preference_string(checksums::hash_from_text("profile_email"),
                                                   checksums::hash_from_text("ui_string"));
        password = preferences->get_preference_string(checksums::hash_from_text("profile_password"),
                                                      checksums::hash_from_text("ui_string"));
    }
    game_spy::gp_connect(man, "proskater", email, password, 1, 0, &FriendsManager::on_connect_new_user,
                         man); // (gpConnect)
    ram_stats()->leave_heap_scope();
    return 0;
}

// 0x00548200
void FriendsManager::send_buddy_message(int profile, const char* text)
{
    const char* nick;
    the_manager()
        ->get_online_settings()
        ->fetch_setting(checksums::hash_from_text("network_id"))
        ->fetch_text(kUiString, &nick, 1);
    char message[128];
    std::sprintf(message, "%s\n%s", nick, text);
    game_spy::gp_send_buddy_message(this, profile, message); // (gpSendBuddyMessage)
}

// 0x005482A0
void FriendsManager::change_online_flag(int profile)
{
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap_);
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
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap_);
    game_spy::gp_set_status(this, status, text, location); // (gpSetStatus)
    ram_stats()->leave_heap_scope();
}

// --- The list types ---

// 0x005484A0 / 0x005484E0
void BuddyNode::destroy_body()
{
    vtable = kBuddyNodeVtable;
    unlink_node(this);
}
BuddyNode* BuddyNode::teardown(unsigned int flags)
{
    vtable = kBuddyNodeVtable;
    unlink_node(this);
    if (flags & 1)
        ram_stats()->dealloc(this);
    return this;
}
// 0x005484C0 / 0x00548520
void BuddyMessageNode::destroy_body()
{
    vtable = kMessageNodeVtable;
    unlink_node(this);
}
BuddyMessageNode* BuddyMessageNode::teardown(unsigned int flags)
{
    vtable = kMessageNodeVtable;
    unlink_node(this);
    if (flags & 1)
        ram_stats()->dealloc(this);
    return this;
}

// --- The folded getters ---

// 0x005480B0 / 0x005480C0 / 0x005480F0
bool FoldedGetters::byte10() { return prop<bool>(this, 0x10); }
uint32_t FoldedGetters::word04() { return prop<uint32_t>(this, 0x04); }
uint32_t FoldedGetters::word18() { return prop<uint32_t>(this, 0x18); }

// --- The script commands ---

// 0x00548360: whether we're logged in to presence.
bool __cdecl profile_logged_in(script::Struct* params, script::Script* script)
{
    (void)params;
    (void)script;
    return get_buddies()->authenticated_;
}

// 0x00548370: the message `string` sent to `profile` (false for none or an empty one).
bool __cdecl send_buddy_message(script::Struct* params, script::Script* script)
{
    (void)script;
    Hub* manager = the_manager();
    const char* text = nullptr;
    params->fetch_text(kString, &text, 0);
    if (!text || !*text)
        return false;
    int profile;
    params->fetch_int_value("profile", &profile, 1);
    static_cast<FriendsManager*>(manager->presence_service_)->send_buddy_message(profile, text);
    return true;
}

// 0x005483E0: our presence status: chatting in the lobby.
bool __cdecl set_lobby_status(script::Struct* params, script::Script* script)
{
    (void)params;
    (void)script;
    Hub* manager = the_manager();
    const char* lobby = static_cast<RoomManager*>(manager->lobby_service_)->room_name_; // (0x0053F820)
    const char* text = script::fetch_text("homie_status_chatting");
    FriendsManager* buddies = static_cast<FriendsManager*>(manager->presence_service_);
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap_);
    game_spy::gp_set_status(buddies, kStatusChatting, text, lobby); // (gpSetStatus)
    ram_stats()->leave_heap_scope();
    return true;
}

} // namespace online

using online::FriendsManager;
