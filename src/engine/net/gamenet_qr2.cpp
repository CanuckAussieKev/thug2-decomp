// The server's reporting to GameSpy's master server through the query & reporting SDK (qr2; library code, called by
// address): its set-up and the callbacks that answer the master server's queries (0x0053B9C0-0x0053BEC5). See
// gamenet_qr2.h.
//
// NOT ORIGINAL: the game name reported is the online identity's (NetLink::GameName(), [Miscellaneous] OnlineGameName -
// online_identity.h); the original pushes "thps6pc" here and online_identity.cpp wrote the configured name over those
// pushes (0x0053BE86, 0x0053BA1F), which this source replaces. The secret key is still read from where the identity
// writes it (0x0067F120, stored bitwise-NOT).

#include "engine/net/gamenet_qr2.h"
#include "platform/gamespy.h"

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_conn.h"
#include "engine/net/online_identity.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/code_address.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "platform/compiler.h"
#include "engine/net/net_game_state.h"
#include "engine/core/gathered_call.h"
THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's copy of the level's name)

namespace online {

using namespace detail;

namespace {
constexpr size_t kFallbackMapNameBytes = 128;

// qr2's keys (the SDK's standard numbering).
constexpr int kKeyHostname = 1;
constexpr int kKeyGamename = 2;
constexpr int kKeyGamever = 3;
constexpr int kKeyHostport = 4;
constexpr int kKeyMapname = 5;
constexpr int kKeyGametype = 6;
constexpr int kKeyNumplayers = 8;
constexpr int kKeyNumteams = 9;
constexpr int kKeyMaxplayers = 10;
constexpr int kKeyGamemode = 11;
constexpr int kKeyPassword = 19;
constexpr int kKeyGroupid = 20;
constexpr int kKeyForPlayer = 21; // player_
constexpr int kKeyPing = 24;      // ping_
constexpr int kKeyTeam = 25;      // team_
constexpr int kKeyTypePlayer = 1;
constexpr int kKeyTypeTeam = 2;

constexpr uint32_t kServerName = 0x2A5CF019; // server_name
constexpr uint32_t kLevel = 0x651533EC;      // level
constexpr uint16_t kHostPort = 0x1421;       // 5153 (reported)
constexpr int kQueryPort = 0x1423;           // 5155 (qr2's own)

// The SDK.
void buffer_add(void* buffer, const char* text) { game_spy::call_qr2_buffer_add(buffer, text); }
void buffer_add_int(void* buffer, int value) { game_spy::get_qr2_buffer_add_int(buffer, value); }


// The level's name for qr2's mapname when the level select has none (0x0067F0A0, "Boston" until set up).
std::array<char, kFallbackMapNameBytes>& fallback_map_name()
{
    constexpr size_t kFallbackMapNameSize = 128;
    static std::array<char, kFallbackMapNameSize> fallback_map_name{{"Boston"}};
    return fallback_map_name;
}

} // namespace

namespace {

int& stored_lobby_group_id()
{
    static int lobby_group_id{};
    return lobby_group_id;
}

} // namespace

int lobby_group_id() { return stored_lobby_group_id(); }
void set_lobby_group_id(int value) { stored_lobby_group_id() = value; }

// 0x0053B9C0
void server_key_callback(int key, void* buffer, void*)
{
    Hub* manager = net_game_state().online_hub();
    switch (key) {
    case kKeyHostname: {
        const char* text = nullptr;
        manager->net_options().fetch_setting(kServerName)->fetch_text("ui_string", &text, 0);
        buffer_add(buffer, text);
        return;
    }
    case kKeyGamename:
        buffer_add(buffer, net::get_game_name()); // (the original: "thps6pc")
        return;
    case kKeyGamever:
        buffer_add(buffer, "1.00");
        return;
    case kKeyHostport:
        buffer_add_int(buffer, kHostPort);
        return;
    case kKeyMapname: {
        const char* name = manager->get_map_name(true);
        buffer_add(buffer, name ? name : fallback_map_name().data());
        return;
    }
    case kKeyGametype:
        buffer_add(buffer, manager->get_game_type_name()); // the game type's name
        return;
    case kKeyNumplayers:
        buffer_add_int(buffer, manager->count_players());
        return;
    case kKeyNumteams:
        buffer_add_int(buffer, manager->get_num_teams());
        return;
    case kKeyMaxplayers:
        buffer_add_int(buffer, manager->get_player_limit());
        return;
    case kKeyGamemode:
        if (net_game_state().game_reported())
            buffer_add(buffer, "openplaying");
        else if (net_game_state().open_waiting())
            buffer_add(buffer, "openwaiting");
        else
            buffer_add(buffer, "closedwaiting");
        return;
    case kKeyPassword:
        buffer_add_int(buffer, *manager->fetch_secret() != '\0' ? 1 : 0);
        return;
    case kKeyGroupid:
        buffer_add_int(buffer, stored_lobby_group_id()); // (the lobby's group room)
        return;
    default:
        buffer_add(buffer, "");
        return;
    }
}

// 0x0053BBC0
void player_key_callback(const QrPlayerKey& query)
{
    const auto key = query.key;
    const auto index = query.index;
    const auto buffer = query.buffer;
    Hub* manager = net_game_state().online_hub();
    if (index >= manager->count_players()) {
        buffer_add(buffer, "");
        return;
    }
    // QUIRK kept: the original never counts `index` down - only player 0 is found; for the others the walk runs off
    // the end and they're reported empty.
    PlayerSearch search;
    const PlayerRecord* player = start_roster_sweep(manager, &search, false);
    if (index != 0)
        while (player)
            player = step_roster_sweep(manager, &search, false);
    if (!player) {
        buffer_add(buffer, "");
        return;
    }
    switch (key) {
    case kKeyForPlayer:
        buffer_add(buffer, player_name(player));
        return;
    case kKeyPing: {
        net::Conn* conn = player->conn();
        buffer_add_int(buffer, conn->is_host_local() ? 0 : conn->ping_probe().average_latency());
        return;
    }
    case kKeyTeam:
        buffer_add_int(buffer, player_team(player));
        return;
    default:
        buffer_add(buffer, "");
        return;
    }
}

// 0x0053BD90
int count_callback(int type, void*)
{
    if (type == kKeyTypePlayer)
        return net_game_state().online_hub()->count_players();
    if (type == kKeyTypeTeam)
        return net_game_state().online_hub()->get_num_teams();
    return 0;
}

// 0x0053BDC0
void start_query_reporting(bool keep_map_name)
{
    if (!keep_map_name) { // the level's name, for mapname when the level select has none
        const char* text = nullptr;
        net_game_state().online_hub()->net_options().fetch_setting(kLevel)->fetch_text("ui_string", &text, 0);
        std::strcpy(fallback_map_name().data(), text);
    }
    if (net_game_state().query_reporting())
        return;
    const char* key = net::game_key(); // (given to the SDK: nothing written into data)
    const int error = game_spy::call_qr2_init(
        nullptr, nullptr, kQueryPort, net::get_game_name(), key, 1, 0, &server_key_callback, &core::gathered<QrPlayerKey, &player_key_callback, void, int, int, void*, void*>,
        code_address(online::player_team_key_callback), code_address(online::key_list_callback), &count_callback,
        code_address(online::add_error_callback), nullptr);
    if (error == 0)
        net_game_state().set_query_reporting(true);
}

} // namespace online
