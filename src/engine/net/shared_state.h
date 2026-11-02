#pragma once

// NOT ORIGINAL - the state the files of this folder share (the original's globals), one record per file that
// defines it, each reached through its accessor (made on first use, never destroyed). The header is the folder's
// own: other subsystems use the folder's functions.

#include <array>
#include <cstdint>
#include "engine/net/gamenet_commands.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/server_list.h"
#include "engine/net/gamenet_game_cfuncs.h"
#include "engine/net/net_manager.h"

namespace online {

// NOT ORIGINAL grouping - gamenet_commands.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (gamenet_commands_shared()).
struct GamenetCommandsShared {
    uint32_t net_game_start_time_{};
};
GamenetCommandsShared& gamenet_commands_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_flow.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (gamenet_flow_shared()).
struct GamenetFlowShared {
    uint32_t auto_serve_start_{};
    bool cheat_flag_{};
};
GamenetFlowShared& gamenet_flow_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_game_commands.cpp's state shared with the folder's other files (the original's
// globals). Made on first use, never destroyed (gamenet_game_commands_shared()).
struct GamenetGameCommandsShared {
    uint8_t game_reported_{};
};
GamenetGameCommandsShared& gamenet_game_commands_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_lobby_cfuncs.cpp's state shared with the folder's other files (the original's
// globals). Made on first use, never destroyed (gamenet_lobby_cfuncs_shared()).
struct GamenetLobbyCfuncsShared {
    int queued_count_{};
    std::array<QueuedPacket, 200> queued_packets_{};
};
GamenetLobbyCfuncsShared& gamenet_lobby_cfuncs_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_lobby_menus.cpp's state shared with the folder's other files (the original's
// globals). Made on first use, never destroyed (gamenet_lobby_menus_shared()).
struct GamenetLobbyMenusShared {
    bool listing_asked_{};
    bool listing_found_{};
};
GamenetLobbyMenusShared& gamenet_lobby_menus_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_lobby_peer.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (gamenet_lobby_peer_shared()).
struct GamenetLobbyPeerShared {
    uint32_t connection_timer_{};
    bool game_posted_{};
};
GamenetLobbyPeerShared& gamenet_lobby_peer_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_manager_create.cpp's state shared with the folder's other files (the original's
// globals). Made on first use, never destroyed (gamenet_manager_create_shared()).
struct GamenetManagerCreateShared {
    uint32_t game_net_references_{};
};
GamenetManagerCreateShared& gamenet_manager_create_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_members.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (gamenet_members_shared()).
struct GamenetMembersShared {
    bool query_reporting_{};
};
GamenetMembersShared& gamenet_members_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_players.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (gamenet_players_shared()).
struct GamenetPlayersShared {
    bool open_waiting_{};
    bool online_active_{};
};
GamenetPlayersShared& gamenet_players_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_session_handlers.cpp's state shared with the folder's other files (the original's
// globals). Made on first use, never destroyed (gamenet_session_handlers_shared()).
struct GamenetSessionHandlersShared {
    uint32_t level_change_time_{};
};
GamenetSessionHandlersShared& gamenet_session_handlers_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - gamenet_settings.cpp's state shared with the folder's other files (the original's globals).
// Made on first use, never destroyed (gamenet_settings_shared()).
struct GamenetSettingsShared {
    uint32_t asked_for_{};
    uint32_t network_team_mode_{};
    int net_skill_level_{3};
    int net_max_players_{8};
    bool join_server_ip_succeeded_{};
};
GamenetSettingsShared& gamenet_settings_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - server_list.cpp's state shared with the folder's other files (the original's globals). Made
// on first use, never destroyed (server_list_shared()).
struct ServerListShared {
    uint32_t news_message_count_{};
};
ServerListShared& server_list_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed
// (gamenet_game_cfuncs_shared()).
struct GamenetGameCfuncsShared {
    bool text_entry_{};
};
GamenetGameCfuncsShared& gamenet_game_cfuncs_shared();

} // namespace online

namespace online {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed
// (gamenet_state_shared()).
struct GamenetStateShared {
    uint32_t command_line_server_{0xFFFFFFFF};
};
GamenetStateShared& gamenet_state_shared();

} // namespace online

namespace net {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed
// (net_manager_apps_shared()).
struct NetManagerAppsShared {
    memory::HeapUnique<Hub> net_core_{};
};
NetManagerAppsShared& net_manager_apps_shared();

} // namespace net

namespace online {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed
// (gamenet_player_lists_shared()).
struct GamenetPlayerListsShared {
    Hub* online_hub_{};
};
GamenetPlayerListsShared& gamenet_player_lists_shared();

} // namespace online
