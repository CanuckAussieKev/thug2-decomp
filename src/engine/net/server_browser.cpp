// The server list's browsing (see server_list.h): the servers' details shown and chosen, the LAN servers' replies
// and GameSpy's game listing callback. GameSpy's server browsing (SBServer) calls are library code, called by
// address; what each returns is said where it's used.

#include <winsock2.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/maths/random.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "wire/gamenet_messages.h"
#include "formats/byte_stream.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_lobby.h"
#include "platform/gamespy.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/player_info.h"
#include "engine/net/server_list.h"
#include "engine/net/stats_man.h"
#include "engine/net/vault_man.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/crt.h"
#include "platform/stopwatch.h"
#include "engine/gfx/anim_blend.h"
#include "engine/gfx/loading_bar.h"
#include "platform/compiler.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

THUG2_MSVC_WARNING(disable : 4996) // (the original's sprintf / strcpy / strncpy, kept)

namespace online {

using namespace detail;

namespace {

// Names.
constexpr uint32_t kId = 0x40C698AF;                     // id
constexpr uint32_t kText = 0xC4745838;                   // text
constexpr uint32_t kName = 0xA1DC81F9;                   // name
constexpr uint32_t kValue = 0xE288A7CB;                  // value
constexpr uint32_t kRank = 0x7786171A;                   // rank
constexpr uint32_t kDestroyDescChildren = 0xDD6DDFD6;    // destroy_server_desc_children
constexpr uint32_t kAddInfoItem = 0x676C4DB8;            // net_game_info_add_item
constexpr uint32_t kUpdatePlayerTitle = 0xC755D58E;      // net_game_info_update_player_title
constexpr uint32_t kInfoAddPlayerText = 0x62F4E2E8;      // net_game_info_add_player
constexpr uint32_t kMidGameText = 0xC7EDAA8E;            // netoptions_str_game_in_progress
constexpr uint32_t kAutoServe = 0xA37CBD43;              // netoptions_str_auto_serve
constexpr uint32_t kFcfs = 0x850E7F29;                   // netoptions_str_fcfs
constexpr uint32_t kTitlePing = 0x7E63A240;              // sort_title_ping
constexpr uint32_t kTitleMode = 0xCC7CD816;              // sort_title_mode
constexpr uint32_t kTitleSkill = 0x39B47254;             // sort_title_skill
constexpr uint32_t kTitleLevel = 0xFD635A30;             // sort_title_level
constexpr uint32_t kTitleRanked = 0xEE56DAAF;            // sort_title_ranked
constexpr uint32_t kYes = 0x47B25601;                    // netoptions_str_yes
constexpr uint32_t kNo = 0xE5CF1315;                     // netoptions_str_no
constexpr uint32_t kSkillLevelInfo = 0x48DAE9CB;         // skill_level_info
constexpr uint32_t kReasonFull = 0x7782B5B7;             // net_reason_full
constexpr uint32_t kReasonFullObservers = 0xDDE5676F;    // net_reason_full_observers
constexpr uint32_t kReason = 0xC44777F3;                 // reason
constexpr uint32_t kJustDialog = 0xF6694E96;             // just_dialog
constexpr uint32_t kJoinRefusedDialog = 0x92528E98;      // CreateJoinRefusedDialog
constexpr uint32_t kCookie = 0x751F4599;                 // cookie
constexpr uint32_t kMaxPlayers = 0xB7E39B53;             // MaxPlayers
constexpr uint32_t kChosenJoinServer = 0xBFF7C13B;       // net_chosen_join_server
constexpr uint32_t kServerId = 0xE7BB1948;               // server_id
constexpr uint32_t kDestroyLobbyServer = 0x10FA2FCB;     // destroy_lobby_server
constexpr uint32_t kUpdateServerList = 0x40C621E4;       // update_lobby_server_list
constexpr uint32_t kDescribeSelectedServer = 0x596BAB63; // describe_selected_server

constexpr int kNoPing = 9999;
constexpr int kPort = 0x1421; // 5153
constexpr int kMaxRank = 10;
constexpr auto kServerResponseSize = static_cast<uint16_t>(kServerInfoSize); // (the server info: gamenet_messages.h)

// GameSpy's listing messages (peerListingGamesCallback's msg: the SDK's number, as given).
constexpr int kListingAdd = 0;
constexpr int kListingUpdate = 1;
constexpr int kListingRemove = 2;
constexpr int kListingClear = 3;

Hub* the_manager() { return g_online_hub; }
memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
RoomManager* current_lobby(Hub* manager) { return manager->lobby_service_.get(); }

void get_append(memory::ListNode* head, memory::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// A line of the server's description: "<title>:" as name and the value `value` makes (if any - after the name, as
// the original), net_game_info_add_item.
template <typename F>
void info_line(uint32_t title, F value)
{
    std::array<char, 128> text{};
    script::Struct* params = script::new_struct();
    std::sprintf(text.data(), "%s:", script::fetch_text(title));
    params->append_word(kName, text.data());
    if (const char* shown = value(text.data()))
        params->append_word(kValue, shown);
    get_run(kAddInfoItem, params);
    script::free_struct(params);
}
const char* no_value(char*) { return nullptr; }

// A rating's rank (as the lobby's: rating * 1/3500 * 10, truncated, at most 10).
int rank_of(int rating)
{
    const double rank = static_cast<double>(rating) * g_rank_scale * render::g_float_ten;
    const int shown = static_cast<int>(rank);
    return shown > kMaxRank ? kMaxRank : shown;
}

// A server's details from GameSpy (the add and update messages alike): level, mode, counts, flags, skill.
void read_server_keys(HostListing* server, void* sb)
{
    std::strcpy(server->level_.data(), game_spy::server_get_string_value(sb, "mapname", "...")); // (string keys)
    std::strcpy(server->mode_sel_.data(), game_spy::server_get_string_value(sb, "gametype", "..."));
    server->num_players_ = game_spy::server_get_int_value(sb, "numplayers", 1); // (integer keys)
    server->max_players_ = game_spy::server_get_int_value(sb, "maxplayers", 8);
    server->num_observers_ = game_spy::server_get_int_value(sb, "numobservers", 0);
    server->max_observers_ = game_spy::server_get_int_value(sb, "maxobservers", 1);
    server->password_ = game_spy::server_get_bool_value(sb, "password", 0) != 0; // (boolean keys)
    server->started_ = game_spy::server_get_bool_value(sb, "started", 0) != 0;
    server->host_mode_ = game_spy::server_get_int_value(sb, "hostmode", 0);
    server->ranked_ = game_spy::server_get_int_value(sb, "ranked", 0) != 0;
    server->skill_level_ = static_cast<int8_t>(game_spy::server_get_int_value(sb, "skilllevel", 2));
}
void read_server_players(HostListing* server, void* sb)
{
    for (int i = 0; i < server->num_players_; ++i) {
        const int rating = game_spy::server_get_player_int_value(sb, i, "rating", 0);        // (a player's integer key)
        const char* name = game_spy::server_get_player_string_value(sb, i, "player", "..."); // (a player's string key)
        server->enroll_player(name, rating);
    }
}

// The listed server whose SBServer is `sb` (the original walks it with a lists::Hunt<HostListing>; its first item is
// the searches' shared code, VaultSearch::head_element).
HostListing* find_by_sb(Hub* manager, void* sb)
{
    VaultSearch search;
    auto* server = static_cast<HostListing*>(search.head_element(&manager->servers_)); // (the first)
    while (server) {
        memory::ListNode* next = search.list_link_->next;
        memory::ListNode* node = next->data == memory::ListNode::head_marker() ? nullptr : next;
        search.list_link_ = node;
        auto* following = node ? static_cast<HostListing*>(node->data) : nullptr;
        if (server->sb_server_ == sb)
            return server;
        server = following;
    }
    return nullptr;
}

} // namespace

// 0x00546790: the chosen server's description (destroy_server_desc_children, then net_game_info_add_item per line:
// game in progress, the host mode, ping - "..." unknown -, mode, skill - skill_level_info's name -, level, ranked
// (internet), the players' count title (": n/max") and each player with its rank). No null check on the server.
bool cfunc_describe_server(script::Struct* params, script::Script* script)
{
    (void)script;
    Hub* manager = the_manager();
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    auto* server = static_cast<HostListing*>(manager->fetch_host(id));
    get_run(kDestroyDescChildren);
    ram_stats()->enter_heap_scope(ram_stats()->front_end_heap_);
    if (server->started_)
        info_line(kMidGameText, no_value);
    if (server->host_mode_ == 1)
        info_line(kAutoServe, no_value);
    else if (server->host_mode_ == 2)
        info_line(kFcfs, no_value);
    info_line(kTitlePing, [server](char* text) {
        if (server->ping_ == kNoPing)
            std::sprintf(text, "...");
        else
            std::sprintf(text, "%d", server->ping_);
        return text;
    });
    info_line(kTitleMode, [server](char* text) {
        std::sprintf(text, "%s", server->mode_sel_.data());
        return text;
    });
    const char* skill = nullptr;
    script::Array* skills = script::fetch_list(kSkillLevelInfo);
    skills->struct_at(server->skill_level_)->fetch_text(kName, &skill, 0);
    info_line(kTitleSkill, [skill](char* text) {
        std::sprintf(text, "%s", skill);
        return text;
    });
    info_line(kTitleLevel, [server](char* text) {
        std::sprintf(text, "%s", server->level_.data());
        return text;
    });
    if (manager->in_internet_mode()) {
        info_line(kTitleRanked, [server](char* text) {
            std::sprintf(text, "%s", script::fetch_text(server->ranked_ ? kYes : kNo));
            return text;
        });
    }
    std::array<char, 128> value{};
    script::Struct* title = script::new_struct();
    std::sprintf(value.data(), ": %d/%d", server->num_players_, server->max_players_);
    title->append_word(kText, value.data());
    get_run(kUpdatePlayerTitle, title);
    script::free_struct(title);
    for (int i = 0; i < server->num_players_; ++i) {
        const int rank = rank_of(server->get_entrant_rank(i));
        script::Struct* player = script::new_struct();
        std::sprintf(value.data(), "%d:", i + 1);
        player->put_int(kRank, rank);
        player->append_word(kName, value.data());
        std::sprintf(value.data(), "%s", server->get_entrant_name(i));
        player->append_word(kValue, value.data());
        get_run(kInfoAddPlayerText, player);
        script::free_struct(player);
    }
    ram_stats()->leave_heap_scope();
    return true;
}

// 0x005470D0: the chosen server joined (net_chosen_join_server with its port, address and maximum players - and a
// random cookie when it can only be reached through NAT negotiation; presence told the server's name, 0x0053F830),
// unless it's full for how we join (CreateJoinRefusedDialog with the reason, the join-state task removed).
bool cfunc_choose_server(script::Struct* params, script::Script* script)
{
    (void)script;
    Hub* manager = the_manager();
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    manager->clear_chosen_servers();
    auto* server = static_cast<HostListing*>(manager->fetch_host(id));
    uint32_t refused = 0;
    if (manager->get_entry_mode() == 0) {
        if (server->num_players_ >= server->max_players_)
            refused = kReasonFull;
    } else if (manager->get_entry_mode() == 1) {
        if (server->num_observers_ >= server->max_observers_)
            refused = kReasonFullObservers;
    }
    script::Struct* reply = script::new_struct();
    if (refused) {
        reply->put_name_key(kReason, refused);
        reply->put_name_key(kJustDialog, 0);
        manager->join_phase_job_->drop_out();
        get_run(kJoinRefusedDialog, reply);
    } else {
        if (manager->presence_service_->authenticated_) // (0x005480B0: logged in to presence)
            manager->lobby_service_->change_host_name(server->label_.data());
        const char* ip = crt::get_inet_ntoa(server->remote_ip_);
        if (!server->can_reach_)
            reply->add_component_of_type(kCookie, script::kQbvalName, maths::rnd(0x7FFF) * maths::rnd(0x7FFF));
        reply->add_component_of_type(0, script::kQbvalInteger, server->port_no_);
        reply->append_word(0, script::kQbvalString, ip);
        reply->put_int(kMaxPlayers, server->max_players_);
        get_run(kChosenJoinServer, reply);
    }
    script::free_struct(reply);
    return true;
}

// 0x00547FA0: the chosen server's full details asked of GameSpy (peerUpdateGame) unless they're here (true) or on the
// LAN (true); false while asking.
bool cfunc_retrieve_server_info(script::Struct* params, script::Script* script)
{
    (void)script;
    Hub* manager = the_manager();
    if ((manager->flag_bits_ >> 3) & 1) // (0x00533F70: LAN mode)
        return true;
    uint32_t id = 0;
    params->read_hash_value(kId, &id, 0);
    auto* server = static_cast<HostListing*>(manager->fetch_host(id));
    manager->clear_chosen_servers();
    server->chosen_ = true;
    if (server->detailed_)
        return true;
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap_);
    game_spy::peer_update_game(current_lobby(manager)->remote_, server->sb_server_, 1); // (the game's details updated)
    ram_stats()->leave_heap_scope();
    return false;
}

// 0x005475C0 (the server-list client's handler for a LAN server's reply, 224 bytes): a join-by-address search
// notes the replying address (0x007CCA84, 0x007CCA88); a server not listed yet is added to the waiting servers with
// the reply's name, level, mode, counts, flags, skill and players (the ping: the time since the time it carries).
// 4 for a reply of another size or a server known already, else 1.
int on_server_reply_msg(net::InboundMsgCtx* context)
{
    auto* manager = static_cast<Hub*>(context->content_);
    if (g_asked_for) {
        g_asked_for = context->sender_conn_->remote_ip_;
        g_join_server_ip_succeeded = true;
    }
    if (context->body_size_ != kServerResponseSize)
        return kDone;
    // (the reply: gamenet_messages.h. NOT ORIGINAL: its texts cut to their fields - the original copied up to a
    // terminator, past them when there was none - and no more players than it has names)
    util::ByteReader reader(context->body_, context->body_size_);
    auto reply = std::make_unique<ServerInfo>();
    read_server_info(reader, reply.get());
    if (manager->has_server(reply->name.data(), context->sender_conn_->remote_ip_))
        return kDone;
    ram_stats()->enter_heap_scope(ram_stats()->network_heap_);
    void* block = memory::alloc_zeroed(sizeof(HostListing));
    HostListing* server = block ? memory::construct_in<HostListing>(block) : nullptr;
    std::snprintf(server->label_.data(), sizeof(server->label_), "%s", reply->name.data());
    server->remote_ip_ = context->sender_conn_->remote_ip_;
    server->ping_ = static_cast<int>(frame_clock::get_time() - reply->time);
    std::snprintf(server->level_.data(), sizeof(server->level_), "%s", reply->level.data());
    std::snprintf(server->mode_sel_.data(), sizeof(server->mode_sel_), "%s", reply->mode.data());
    server->max_players_ = int{reply->max_players};
    server->num_players_ = int{reply->num_players};
    server->max_observers_ = int{reply->max_observers};
    server->num_observers_ = int{reply->num_observers};
    server->password_ = reply->password;
    server->started_ = reply->in_game;
    server->host_mode_ = int{static_cast<int8_t>(reply->host_mode)};
    server->ranked_ = reply->logged_in;
    server->skill_level_ = static_cast<int8_t>(reply->skill_level);
    server->port_no_ = kPort;
    for (int i = 0; i < server->num_players_ && i < kServerInfoPlayers; ++i)
        server->enroll_player(core::at(reply->players, i).data(), 0);
    get_append(&manager->waiting_servers_, server);
    ram_stats()->leave_heap_scope();
    return kContinue;
}

// 0x00547750 (GameSpy's game listing callback, `manager` its parameter): on failure the lobby's 0x0053F870; add - a new server
// from the SBServer (its address: the private one when its public address is ours, else the public one and port -
// unreachable without NAT negotiation when it has no private address; the keys, the players), listed, and shown once
// it has its basic keys (SBServerHasBasicKeys); update - a listed one's name, ping (if reachable), keys and players again,
// shown if not yet, and with its full keys (SBServerHasFullKeys) the description asked for when it's the chosen one; remove -
// destroy_lobby_server and the server deleted; clear - "none found" and every server deleted. Each but a failure ends
// with update_lobby_server_list (the update message only when it shows the server).
void listing_games_callback(void* peer, int success, const char* name, void* sb, int staging, int message, int progress,
                            Hub* manager)
{
    (void)staging;
    (void)progress;
    if (!success) {
        the_manager()->lobby_service_->set_lost_connection(); // (the lobby told the listing failed)
        return;
    }
    switch (message) {
    case kListingAdd: {
        ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap_);
        void* block = memory::alloc_zeroed(sizeof(HostListing));
        HostListing* server = block ? memory::construct_in<HostListing>(block) : nullptr;
        server->sb_server_ = sb;
        std::strncpy(server->label_.data(), name, 15);
        core::at(server->label_, 15) = '\0';
        server->ping_ = game_spy::server_get_ping(sb); // (its ping)
        bool same_network = false;
        if (game_spy::server_direct_connect(sb)) {                             // (it answered directly)
            const uint32_t ours = game_spy::peer_get_public_ip(peer);          // (our public address)
            if (game_spy::server_get_public_address(sb) == ours) {             // (its public address)
                server->remote_ip_ = game_spy::server_get_private_address(sb); // (its private address)
                server->port_no_ = kPort;
                same_network = true;
            }
        }
        if (!same_network) {
            const bool has_private = game_spy::server_has_private_address(sb) != 0; // (it has a private address)
            server->remote_ip_ = game_spy::server_get_public_address(sb);
            auto port = static_cast<uint16_t>(game_spy::server_get_public_query_port(sb)); // (its public port)
            if (!has_private) {
                server->can_reach_ = false;
                server->ping_ = kNoPing;
            }
            server->port_no_ = port;
        }
        read_server_keys(server, sb);
        read_server_players(server, sb);
        memory::ListNode* list = &manager->servers_;
        get_append(list, server);
        if (game_spy::server_has_basic_keys(sb)) {                                       // (it has its basic keys)
            const int index = gfx::count_list_items(list) - 1; // (the list's count)
            manager->show_server_in_menu(server, index);
            get_run(kUpdateServerList);
            server->in_list_ = true;
        }
        ram_stats()->leave_heap_scope();
        break;
    }
    case kListingRemove: {
        script::Struct* params = script::new_struct();
        params->put_name_key(kServerId, cell::of(sb));
        get_run(kDestroyLobbyServer, params);
        script::free_struct(params);
        if (HostListing* server = find_by_sb(manager, sb))
            server->teardown(1);
        get_run(kUpdateServerList);
        break;
    }
    case kListingClear:
        manager->show_no_servers_found(false);
        manager->empty_server_listing();
        get_run(kUpdateServerList);
        break;
    case kListingUpdate: {
        game_spy::server_has_full_keys(sb); // (asked whether it has its full keys: the answer unused here)
        HostListing* server = find_by_sb(manager, sb);
        if (!server)
            break;
        std::strncpy(server->label_.data(), name, 15);
        core::at(server->label_, 15) = '\0';
        if (server->can_reach_)
            server->ping_ = game_spy::server_get_ping(sb);
        read_server_keys(server, sb);
        server->clear_players();
        read_server_players(server, sb);
        if (!server->in_list_ && game_spy::server_has_basic_keys(sb)) {
            manager->show_server_in_menu(server, 0);
            get_run(kUpdateServerList);
            server->in_list_ = true;
        }
        if (!server->detailed_ && game_spy::server_has_full_keys(sb)) {
            server->detailed_ = true;
            if (server->chosen_) {
                script::Struct* params = script::new_struct();
                params->put_name_key(kId, cell::of(sb));
                get_run(kDescribeSelectedServer, params);
                script::free_struct(params);
            }
        }
        break;
    }
    default:
        break;
    }
}

} // namespace online
