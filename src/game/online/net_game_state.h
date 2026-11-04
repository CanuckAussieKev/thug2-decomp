#pragma once

#include <array>
#include <cstdint>
#include "game/online/gamenet_commands.h"
#include "game/online/gamenet_manager.h"
#include "game/online/gamenet_lobby.h"
#include "game/online/server_list.h"
#include "game/online/gamenet_game_cfuncs.h"
#include "engine/net/net_manager.h"

// NOT ORIGINAL - the online game's state the network files share: the settings (team mode, skill level, most players),
// the session's times and flags, the lobby's queued packets, the online hub (the original's globals).
namespace online {

constexpr size_t kNetGameStateGetQueueSize = 200;
constexpr size_t kNetGameStateQueuedPacketsSize = 200;
// The online game's state the network folder's files share (the rest of the game reaches it through the folder's
// functions). Trivially destructible: the accessor is a constant-initialised static, no guard.
constexpr uint32_t kNoCommandLineServer = 0xFFFFFFFFU;
constexpr int kDefaultMaxPlayers = 8;
class NetGameState {
public:
    [[nodiscard]] uint32_t net_game_start_time() const { return net_game_start_time_; }
    void set_net_game_start_time(uint32_t value) { net_game_start_time_ = value; }
    [[nodiscard]] uint32_t auto_serve_start() const { return auto_serve_start_; }
    void set_auto_serve_start(uint32_t value) { auto_serve_start_ = value; }
    [[nodiscard]] bool cheat_flag() const { return cheat_flag_; }
    void set_cheat_flag(bool value) { cheat_flag_ = value; }
    uint8_t& game_reported() { return game_reported_; }
    int& queued_count() { return queued_count_; }
    std::array<QueuedPacket, kNetGameStateGetQueueSize>& get_queue() { return queued_packets_; }
    [[nodiscard]] bool listing_asked() const { return listing_asked_; }
    void set_listing_asked(bool value) { listing_asked_ = value; }
    [[nodiscard]] bool listing_found() const { return listing_found_; }
    void set_listing_found(bool value) { listing_found_ = value; }
    void set_connection_timer(uint32_t value) { connection_timer_ = value; }
    [[nodiscard]] bool game_posted() const { return game_posted_; }
    void set_game_posted(bool value) { game_posted_ = value; }
    uint32_t& game_net_references() { return game_net_references_; }
    [[nodiscard]] bool query_reporting() const { return query_reporting_; }
    void set_query_reporting(bool value) { query_reporting_ = value; }
    [[nodiscard]] bool open_waiting() const { return open_waiting_; }
    void set_open_waiting(bool value) { open_waiting_ = value; }
    [[nodiscard]] bool online_active() const { return online_active_; }
    void set_online_active(bool value) { online_active_ = value; }
    uint32_t& level_change_time() { return level_change_time_; }
    [[nodiscard]] uint32_t asked_for() const { return asked_for_; }
    void set_asked_for(uint32_t value) { asked_for_ = value; }
    [[nodiscard]] uint32_t network_team_mode() const { return network_team_mode_; }
    void set_network_team_mode(uint32_t value) { network_team_mode_ = value; }
    [[nodiscard]] int net_skill_level() const { return net_skill_level_; }
    void set_net_skill_level(int value) { net_skill_level_ = value; }
    [[nodiscard]] int net_max_players() const { return net_max_players_; }
    void set_net_max_players(int value) { net_max_players_ = value; }
    [[nodiscard]] bool join_server_ip_succeeded() const { return join_server_ip_succeeded_; }
    void set_join_server_ip_succeeded(bool value) { join_server_ip_succeeded_ = value; }
    [[nodiscard]] uint32_t news_message_count() const { return news_message_count_; }
    void set_news_message_count(uint32_t value) { news_message_count_ = value; }
    [[nodiscard]] bool text_entry() const { return text_entry_; }
    void set_text_entry(bool value) { text_entry_ = value; }
    [[nodiscard]] uint32_t command_line_server() const { return command_line_server_; }
    void set_command_line_server(uint32_t value) { command_line_server_ = value; }
    [[nodiscard]] Hub* online_hub() const { return online_hub_; }
    void set_online_hub(Hub* value) { online_hub_ = value; }

private:
    // (gamenet_commands.cpp)
    uint32_t net_game_start_time_{};

    // (gamenet_flow.cpp)
    uint32_t auto_serve_start_{};
    bool cheat_flag_{};

    // (gamenet_game_commands.cpp)
    uint8_t game_reported_{};

    // (gamenet_lobby_cfuncs.cpp)
    int queued_count_{};
    std::array<QueuedPacket, kNetGameStateQueuedPacketsSize> queued_packets_{};

    // (gamenet_lobby_menus.cpp)
    bool listing_asked_{};
    bool listing_found_{};

    // (gamenet_lobby_peer.cpp)
    uint32_t connection_timer_{};
    bool game_posted_{};

    // (gamenet_manager_create.cpp)
    uint32_t game_net_references_{};

    // (gamenet_members.cpp)
    bool query_reporting_{};

    // (gamenet_players.cpp)
    bool open_waiting_{};
    bool online_active_{};

    // (gamenet_session_handlers.cpp)
    uint32_t level_change_time_{};

    // (gamenet_settings.cpp)
    uint32_t asked_for_{};
    uint32_t network_team_mode_{};
    int net_skill_level_{3};
    int net_max_players_{kDefaultMaxPlayers};
    bool join_server_ip_succeeded_{};

    // (server_list.cpp)
    uint32_t news_message_count_{};

    // (gamenet_game_cfuncs.cpp)
    bool text_entry_{};

    // (gamenet_state.cpp)
    uint32_t command_line_server_{kNoCommandLineServer};

    // (gamenet_player_lists.cpp)
    Hub* online_hub_{};
};

// The state (made on first use, never destroyed).
NetGameState& net_game_state();
// The online manager (0x007CCE14; null: none).
[[nodiscard]] inline Hub* online_hub() { return net_game_state().online_hub(); }

} // namespace online
