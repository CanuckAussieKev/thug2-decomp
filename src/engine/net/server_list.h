#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/memory/arena_alloc.h"

namespace script {
class Script;
class Struct;
} // namespace script
namespace net {
struct InboundMsgCtx;
} // namespace net

// The network manager's server list (0x005451B0-0x00548040): the servers found (on the LAN through the server-list
// client, on the internet through GameSpy's server browser - the SDK is a library, called by address), shown in
// the server list menu, described and chosen; and the online service's download threads (the news, the message of
// the day) with their state (the manager's +0x1B8).
namespace tasks {
template <typename Owner>
class Task;
} // namespace tasks
namespace online {

class Hub;

constexpr size_t kHostListingLabelSize = 16;
constexpr size_t kHostListingLevelSize = 32;
constexpr size_t kHostListingModeSelSize = 32;
// A server found (0xA8 bytes): a node of the manager's server list (+0x158) or of the servers waiting to be shown
// (+0x16C). (The offsets are the original's.)
constexpr int kHostListingMaxPlayers = 8;
constexpr size_t kLabelLength = 15; // a listed server's and player's name, at most (its buffer one more)
struct HostListing : memory::ListNode {
    // The fields other code reads and sets.
    std::array<char, kHostListingLabelSize>& label() { return label_; }
    [[nodiscard]] const std::array<char, kHostListingLabelSize>& label() const { return label_; }
    [[nodiscard]] uint32_t remote_ip() const { return remote_ip_; }
    void set_remote_ip(uint32_t value) { remote_ip_ = value; }
    [[nodiscard]] int port_no() const { return port_no_; }
    void set_port_no(int value) { port_no_ = value; }
    [[nodiscard]] int ping() const { return ping_; }
    void set_ping(int value) { ping_ = value; }
    [[nodiscard]] int num_players() const { return num_players_; }
    void set_num_players(int value) { num_players_ = value; }
    [[nodiscard]] int max_players() const { return max_players_; }
    void set_max_players(int value) { max_players_ = value; }
    [[nodiscard]] int num_observers() const { return num_observers_; }
    void set_num_observers(int value) { num_observers_ = value; }
    [[nodiscard]] int max_observers() const { return max_observers_; }
    void set_max_observers(int value) { max_observers_ = value; }
    std::array<char, kHostListingLevelSize>& level() { return level_; }
    [[nodiscard]] const std::array<char, kHostListingLevelSize>& level() const { return level_; }
    std::array<char, kHostListingModeSelSize>& mode_sel() { return mode_sel_; }
    [[nodiscard]] const std::array<char, kHostListingModeSelSize>& mode_sel() const { return mode_sel_; }
    void set_password(bool value) { password_ = value; }
    [[nodiscard]] bool started() const { return started_; }
    void set_started(bool value) { started_ = value; }
    [[nodiscard]] int8_t skill_level() const { return skill_level_; }
    void set_skill_level(int8_t value) { skill_level_ = value; }
    [[nodiscard]] int host_mode() const { return host_mode_; }
    void set_host_mode(int value) { host_mode_ = value; }
    [[nodiscard]] bool ranked() const { return ranked_; }
    void set_ranked(bool value) { ranked_ = value; }
    [[nodiscard]] bool can_reach() const { return can_reach_; }
    void set_can_reach(bool value) { can_reach_ = value; }
    [[nodiscard]] bool in_menu() const { return in_menu_; }
    void set_in_menu(bool value) { in_menu_ = value; }
    [[nodiscard]] bool chosen() const { return chosen_; }
    void set_chosen(bool value) { chosen_ = value; }
    [[nodiscard]] bool in_list() const { return in_list_; }
    void set_in_list(bool value) { in_list_ = value; }
    [[nodiscard]] bool detailed() const { return detailed_; }
    void set_detailed(bool value) { detailed_ = value; }
    [[nodiscard]] void* sb_server() const { return sb_server_; }
    void set_sb_server(void* value) { sb_server_ = value; }
    [[nodiscard]] uint32_t id() const { return id_; }

private:
    std::array<char, kHostListingLabelSize> label_{};     // +0x14
    uint32_t remote_ip_{}; // +0x24
    int port_no_{};        // +0x28
    int ping_{0};          // +0x2C (9999: not known)
    int num_players_{1};   // +0x30
    int max_players_{kHostListingMaxPlayers};   // +0x34
    int num_observers_{0}; // +0x38
    int max_observers_{1}; // +0x3C
    std::array<char, kHostListingLevelSize> level_{};   // +0x40 ("None")
    std::array<char, kHostListingModeSelSize> mode_sel_{}; // +0x60 ("None")
    bool password_{};     // +0x80
    bool started_{false}; // +0x81 a game in progress
    int8_t skill_level_{2}; // +0x82
    int host_mode_{0};      // +0x84
    bool ranked_{false};    // +0x88
    bool can_reach_{true};  // +0x89 (cleared when it can only be reached with NAT negotiation)
    bool in_menu_{false};   // +0x8A added to the server list menu
    bool chosen_{false};    // +0x8B its details asked for (RetrieveServerInfo)
    bool in_list_{false};   // +0x8C counted in the lobby's server list
    bool detailed_{false};  // +0x8D the full details have come
    std::array<uint8_t, 2> pad8e_{};
    void* sb_server_{nullptr};        // +0x90 GameSpy's SBServer (internet)
    memory::ListNode participants_{}; // +0x94 (lists::Foremost<PlayerRecord>): its players' names and ratings
    // NOT ORIGINAL: its id in the server menu and for fetch_host - a running number (the original used its SBServer's
    // address on the internet and its own on the LAN: ids that were addresses).
    uint32_t id_{0};

public:

    // 0x00545F60: an empty one: one of eight players, one of one observer, skill level 2, mode and level "None",
    // reachable.
    HostListing();
    // 0x00546640: its players freed (0x005465A0), then the heads' destructors.
    ~HostListing();
    HostListing(const HostListing&) = delete;
    HostListing& operator=(const HostListing&) = delete;
    // 0x00547F70: the scalar deleting destructor.
    HostListing* teardown(unsigned int flags);
    // 0x00545790: a player (a PlayerRecord with only its name - 15 characters - and rating) added (on the internet
    // or the network heap).
    void enroll_player(const char* name, int rating);
    // 0x005465A0: each player destroyed by the manager (0x0053C950).
    void clear_players();
    // 0x005466D0 / 0x00546730: player `index`'s name / rating (null / 0 past the end).
    const char* get_entrant_name(int index);
    int get_entrant_rank(int index);
};

// (lists::Hunt<HostListing>'s vtable, 0x0064CD70, holds only a destructor that resets it: 0x005451D0 / 0x00545460.)

// 0x00547440 (the task's callback, the manager at its +0x1C; made by 0x0053EE90): every 3 seconds the clock
// restarts; within them, if a refresh asked for it (0x007CE44C) and more than 500 ms after the refresh, the waiting
// servers are shown as show_found_servers does (update_lobby_server_list by checksum).
void server_list_task(tasks::Task<Hub>* task);

// The online service's download threads (stdcall, the manager as the argument; started by NetLink::service_task).
// 0x00545540: the news (state 7): motd.dat (testmotd.dat when 0x007CE441 is set) from www.thug2online.com into
// the manager's buffer (+0x99F4, 25600 bytes cleared first), news_callback when done. 0x00545620: the message of the
// day (state 9): GameSpy's usage report (ptTrackUsage, which asks motd.<domain>/motd/motd.asp: product 10368,
// version "1.0"; the unique_id preference is read but not passed, as the original). Both note the request (+0xFE00) and the time (+0xFE04). 0.
unsigned long __stdcall news_thread(void* argument);
unsigned long __stdcall motd_thread(void* argument);
// 0x00545490 (GHTTP's completion callback, `param` the... not used: the manager is the singleton): a failed
// download is state 11; else the data XOR 0x6D, its checksum (the first word, over the size in the second word -
// 0x00401B30) checked (wrong: state 11), the text noted (+0xFDF4) with what follows it (+0xFDF8 / +0xFDFC), the
// news ok (+0xFE09) and state 8. 1.
int news_callback(void* request, int result, uint8_t* buffer, uint32_t size_low, int32_t size_high, void* param);

// 0x00548040: whether a GameSpy location string is THUG2's ("10thug211" before the first ':').
bool is_thug2_location(const char* location);

// The script commands.
bool cfunc_post_game(script::Struct* params, script::Script* script);            // 0x00545440
bool cfunc_num_servers_in_lobby(script::Struct* params, script::Script* script); // 0x00545F20

// --- server_browser.cpp ---
class Hub;
// 0x00546790 / 0x005470D0 / 0x00547FA0: the chosen server described / joined / its details asked for (see the .cpp).
bool cfunc_describe_server(script::Struct* params, script::Script* script);
bool cfunc_choose_server(script::Struct* params, script::Script* script);
bool cfunc_retrieve_server_info(script::Struct* params, script::Script* script);
// 0x005475C0: the server-list client's handler for a LAN server's reply (a Net message handler).
int on_server_reply_msg(net::InboundMsgCtx* context);
// A step of the SDK's game listing (its callback's arguments, in order).
struct GamesListing {
    void* peer{};
    int success{};
    const char* name{};
    void* sb{};
    int staging{};
    int message{};
    int progress{};
    Hub* manager{};
};

// 0x00547750: GameSpy's game listing callback (add / update / remove / clear), the manager its parameter.
void listing_games_callback(const GamesListing& listing);

} // namespace online
