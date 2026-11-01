// The network manager's GameSpy lobby: small members and the peer SDK's hosting callbacks (0x0053F620-0x0053FEE0).
// See gamenet_lobby.h.
//
// NOT ORIGINAL: the game name reported (key 2) is the online identity's (NetLink::GameName()); the original pushes
// "thps6pc" and online_identity.cpp wrote the configured name over that push (0x0053F936), which this source replaces.

#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamespy.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "engine/core/task_list.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/buddy_man.h"
#include "engine/net/online_identity.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "platform/compiler.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's copy of the server's name)

namespace online {

using namespace detail;

namespace {

// qr2's keys (the SDK's standard numbering) and THUG2's own.
enum PeerKey : int {
    kKeyHostname = 1,
    kKeyGamename = 2,
    kKeyHostport = 4,
    kKeyMapname = 5,
    kKeyGametype = 6,
    kKeyNumplayers = 8,
    kKeyMaxplayers = 10,
    kKeyGamemode = 11,
    kKeyTeamplay = 12,
    kKeyPassword = 19,
    kKeyForPlayer = 21,
    kKeyObservers = 51,
    kKeyMaxObservers = 52,
    kKeySkillLevel = 53,
    kKeyInProgress = 54,
    kKeyHostMode = 55,
    kKeyLoggedIn = 56,
    kKeyPlayerRating = 57,
};
constexpr int kKeyTypeServer = 0;
constexpr int kKeyTypePlayer = 1;
constexpr int kServerKeyCount = 16;

constexpr uint32_t kServerName = 0x2A5CF019; // server_name
constexpr uint32_t kUiString = 0x96875C0F;   // ui_string
constexpr uint32_t kNetLobby = 0x1C471C60;   // netlobby
constexpr uint16_t kHostPort = 0x1421;       // 5153

void buffer_add(void* buffer, const char* text) { game_spy::call_qr2_buffer_add(buffer, text); }
void buffer_add_int(void* buffer, int value) { game_spy::get_qr2_buffer_add_int(buffer, value); }
void key_buffer_add(void* key_buffer, int key) { game_spy::qr2_key_buffer_add(key_buffer, key); }

} // namespace

// 0x0053F7E0
void RoomManager::stop_hosting()
{
    if (remote_) {
        game_spy::peer_stop_game(remote_); // peerStopGame
        hosting_task_->drop_out();
    }
    hosting_ = false;
}

// 0x0053F810
bool RoomManager::is_hosting() { return hosting_; }
// 0x0053F820
char* RoomManager::get_room_name() { return room_name_; }
// 0x0053F830
void RoomManager::change_host_name(const char* name) { std::strcpy(server_name_, name); }
// 0x0053F850
char* RoomManager::fetch_host_name() { return server_name_; }
// 0x0053F860
void RoomManager::clear_lost_connection() { lost_connection_ = false; }
// 0x0053F870
void RoomManager::set_lost_connection() { lost_connection_ = true; }
// 0x0053F880
void* RoomManager::get_lobby_handle() { return remote_; }
// 0x0053F890
bool RoomManager::in_group_room() { return in_group_room_; }

// 0x0053F8A0
bool __cdecl cfunc_reset_connection_timer(script::Struct*, script::Script*)
{
    g_connection_timer = frame_clock::get_time();
    return true;
}

// 0x0053F8D0
void __cdecl RoomManager::server_key_callback(void*, int key, void* buffer, void*)
{
    Hub* manager = g_online_hub;
    prefs::Preferences* prefs = manager->get_online_settings();
    switch (key) {
    case kKeyHostname: {
        const char* text = nullptr;
        prefs->fetch_setting(kServerName)->fetch_text(kUiString, &text, 1);
        buffer_add(buffer, text);
        break;
    }
    case kKeyGamename:
        buffer_add(buffer, net::get_game_name()); // (the original: "thps6pc")
        break;
    case kKeyHostport:
        buffer_add_int(buffer, kHostPort);
        break;
    case kKeyMapname:
        buffer_add(buffer, manager->get_map_name(true)); // (null passed on, unlike the qr2 callback)
        break;
    case kKeyGametype:
    case kKeyGamemode:
        buffer_add(buffer, manager->get_game_type_name()); // the game type's name
        break;
    case kKeyNumplayers:
        buffer_add_int(buffer, manager->count_players());
        break;
    case kKeyMaxplayers:
        buffer_add_int(buffer, manager->get_player_limit());
        break;
    case kKeyTeamplay:
        buffer_add_int(buffer, 0);
        break;
    case kKeyPassword:
        buffer_add_int(buffer, *manager->fetch_secret() != '\0' ? 1 : 0);
        break;
    case kKeyObservers:
        buffer_add_int(buffer, manager->count_observers());
        break;
    case kKeyMaxObservers:
        buffer_add_int(buffer, manager->get_spectator_limit());
        break;
    case kKeySkillLevel:
        buffer_add_int(buffer, manager->get_difficulty_tier());
        break;
    case kKeyInProgress:
        buffer_add_int(buffer, get_game_type() == kNetLobby ? 0 : 1);
        break;
    case kKeyHostMode:
        buffer_add_int(buffer, manager->get_hosting_role());
        break;
    case kKeyLoggedIn:
        buffer_add_int(buffer, manager->presence_service_->authenticated_ ? 1 : 0); // (the presence)
        break;
    default:
        buffer_add(buffer, "");
        break;
    }
    g_game_posted = true;
}

// 0x0053FEC0
int __cdecl RoomManager::count_callback(void*, int type, void*)
{
    if (type == kKeyTypeServer)
        return kServerKeyCount;
    if (type == kKeyTypePlayer)
        return g_online_hub->count_players();
    return 0;
}

// 0x0053F620
void __cdecl RoomManager::key_list_callback(void*, int type, void* key_buffer, void*)
{
    if (type == kKeyTypeServer) {
        for (int key :
             {kKeyHostport, kKeyMapname, kKeyGametype, kKeyTeamplay, kKeyNumplayers, kKeyMaxplayers, kKeyPassword,
              kKeyObservers, kKeyMaxObservers, kKeyLoggedIn, kKeySkillLevel, kKeyInProgress, kKeyHostMode})
            key_buffer_add(key_buffer, key);
    } else if (type == kKeyTypePlayer) {
        key_buffer_add(key_buffer, kKeyForPlayer);
        key_buffer_add(key_buffer, kKeyPlayerRating);
    }
}

} // namespace online
