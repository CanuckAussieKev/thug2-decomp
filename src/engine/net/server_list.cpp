// The network manager's server list and the online service's downloads (see server_list.h).

#include "engine/net/server_list.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamespy.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/net_app.h"
#include "engine/net/net_manager.h"
#include "engine/net/player_info.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "platform/code_address.h"
#include "engine/assets/resource_registry.h"
#include "engine/gfx/texture_cfuncs.h"
#include "engine/net/gamenet_lists.h"
#include "engine/core/stubs.h"
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
#include "formats/byte_stream.h"
#include "platform/compiler.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

THUG2_MSVC_WARNING(disable : 4996) // (the original's sprintf / strncpy, kept)

namespace online {

using namespace detail;

namespace {

// Names.
constexpr uint32_t kNumServers = 0x757610E1;             // num_servers
constexpr uint32_t kRefocus = 0xE5BFC29E;                // refocus
constexpr uint32_t kDestroyServerChildren = 0x152AAD5D;  // destroy_server_menu_children
constexpr uint32_t kNoServersFound = 0x79EE1731;         // add_no_servers_found_message
constexpr uint32_t kPrepareServerMenu = 0x56E20B51;      // prepare_server_menu_for_new_children
constexpr uint32_t kText = 0xC4745838;                   // text
constexpr uint32_t kMode = 0x6835B854;                   // Mode
constexpr uint32_t kId = 0x40C698AF;                     // id
constexpr uint32_t kParent = 0xC2719FB0;                 // parent
constexpr uint32_t kServerListMenu = 0x052140A3;         // server_list_menu
constexpr uint32_t kPadChooseScript = 0x2B0CFCA1;        // pad_choose_script
constexpr uint32_t kChooseSelectedServer = 0x822E996A;   // choose_selected_server
constexpr uint32_t kFocusScript = 0x5A8C82DB;            // focus_script
constexpr uint32_t kDescribeSelectedServer = 0x596BAB63; // describe_selected_server
constexpr uint32_t kPadChooseParams = 0xB8437568;        // pad_choose_params
constexpr uint32_t kFocusParams = 0xC9C30B12;            // focus_params
constexpr uint32_t kAddMenuItem = 0x1499767F;            // server_list_menu_add_item
constexpr uint32_t kServerName = 0x2A5CF019;             // server_name (a network preference)
constexpr uint32_t kUiString = 0x96875C0F;               // ui_string
constexpr uint32_t kUniqueId = 0x1C397CBC;               // unique_id (a network preference)
constexpr uint32_t kHostingStatus = 0xC5A16CE1;          // homie_status_hosting
constexpr uint32_t kUpdateServerList = 0x40C621E4;       // update_lobby_server_list

constexpr int kStateWake = 1;
constexpr int kStateStopped = 2;
constexpr int kStateNews = 7;
constexpr int kStateNewsDone = 8;
constexpr int kStateMotd = 9;
constexpr int kStateNewsFailed = 11;
constexpr int kPort = 0x1421;                // 5153
constexpr uint32_t kRefreshInterval = 5000;  // ms
constexpr uint32_t kListClock = 3000;        // ms
constexpr uint32_t kShowDelay = 500;         // ms
constexpr uint32_t kNewsBufferSize = 0x6400; // bytes

Hub* the_manager() { return g_online_hub; }
memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
tasks::PileUp* get_logic_tasks() { return (&assets::g_main_loop_manager->logic_tasks_); }
RoomManager* current_lobby(Hub* manager) { return manager->lobby_service_.get(); }

uint32_t g_last_refresh;
bool g_show_pending;
uint32_t g_list_clock;

uint32_t& last_refresh() { return g_last_refresh; }
bool& show_pending() { return g_show_pending; }
uint32_t& list_clock() { return g_list_clock; }

// Walking a list as the original does: a node whose data is -1 is the head (checked before null).
memory::ListNode* head_of_scene_list(memory::ListNode* head)
{
    memory::ListNode* node = head->next;
    return node->data == memory::ListNode::head_marker() ? nullptr : node;
}
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next;
    return next->data == memory::ListNode::head_marker() ? nullptr : next;
}
template <typename T>
T* data_of(memory::ListNode* node)
{
    return node ? static_cast<T*>(node->data) : nullptr;
}

// The nodes counted (the original's inlined count: data -1 checked before null).
int total_of(memory::ListNode* head)
{
    int count = 0;
    for (const memory::ListNode* node = head->next; node->data != memory::ListNode::head_marker() && node;
         node = node->next)
        ++count;
    return count;
}

// Every server of a list deleted.
void delete_all(memory::ListNode* head)
{
    memory::ListNode* node = head_of_scene_list(head);
    auto* item = data_of<HostListing>(node);
    while (item) {
        node = node ? get_next_node(node) : nullptr;
        auto* following = data_of<HostListing>(node);
        item->teardown(1);
        item = following;
    }
}

void unlink_node(memory::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}
void get_append(memory::ListNode* head, memory::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// A server's id in the menu and in fetch_host: its SBServer on the internet, itself on the LAN.
uint32_t server_id(Hub* manager, HostListing* server)
{
    return manager->in_internet_mode() ? cell::of(server->sb_server_)
                                       : cell::of(server);
}

// The waiting servers moved to the list (and the menu), after the "none found" message.
void move_waiting_servers(Hub* manager)
{
    manager->show_no_servers_found(true);
    memory::ListNode* node = head_of_scene_list(&manager->waiting_servers_);
    auto* server = data_of<HostListing>(node);
    while (server) {
        node = get_next_node(node);
        auto* following = data_of<HostListing>(node);
        unlink_node(server);
        memory::ListNode* list = &manager->servers_;
        manager->show_server_in_menu(server, total_of(list));
        get_append(list, server);
        server = following;
    }
}

} // namespace

// --- HostListing ---

// 0x00545F60
HostListing::HostListing() : ListNode()
{
    make_node(this);
    participants_.make_head();
    std::sprintf(level_.data(), "None");
    std::sprintf(mode_sel_.data(), "None");
}

// 0x00546640
HostListing::~HostListing()
{
    clear_players();
    unlink_node(&participants_);
    unlink_node(this);
}

// 0x00547F70
HostListing* HostListing::teardown(unsigned int flags)
{
    this->~HostListing();
    if (flags & 1)
        ram_stats()->dealloc(this);
    return this;
}

// 0x00545790
void HostListing::enroll_player(const char* name, int rating)
{
    ram_stats()->enter_heap_scope(the_manager()->in_internet_mode()
                                      ? static_cast<memory::MemSource*>(ram_stats()->internet_bottom_up_heap_)
                                      : static_cast<memory::MemSource*>(ram_stats()->network_heap_));
    void* block = memory::alloc_zeroed(sizeof(PlayerRecord));
    PlayerRecord* player = block ? memory::construct_in<PlayerRecord>(block, 0) : nullptr;
    player->skill_points_ = rating;
    std::strncpy(player->label_.data(), name, 15);
    core::at(player->label_, 15) = '\0';
    get_append(&participants_, player);
    ram_stats()->leave_heap_scope();
}

// 0x005465A0
void HostListing::clear_players()
{
    Hub* manager = the_manager();
    memory::ListNode* node = head_of_scene_list(&participants_);
    auto* player = data_of<PlayerRecord>(node);
    while (player) {
        node = get_next_node(node);
        auto* following = data_of<PlayerRecord>(node);
        manager->remove_player_record(player); // (the player destroyed)
        player = following;
    }
}

// 0x005466D0 / 0x00546730
const char* HostListing::get_entrant_name(int index)
{
    for (memory::ListNode* node = head_of_scene_list(&participants_); node; node = get_next_node(node)) {
        auto* player = static_cast<PlayerRecord*>(node->data);
        if (!player)
            return nullptr;
        if (index == 0)
            return player->label_.data();
        --index;
    }
    return nullptr;
}
int HostListing::get_entrant_rank(int index)
{
    for (memory::ListNode* node = head_of_scene_list(&participants_); node; node = get_next_node(node)) {
        auto* player = static_cast<PlayerRecord*>(node->data);
        if (!player)
            return 0;
        if (index == 0)
            return player->skill_points_;
        --index;
    }
    return 0;
}

// --- The manager's server list ---

// 0x005451B0 / 0x005451C0
void Hub::set_next_service_state(int state) { next_service_state_ = state; }
int Hub::get_service_state() { return service_state_; }

uint32_t g_news_message_count;

// 0x005453E0
void Hub::set_service_state(int state)
{
    service_state_ = state;
    if (state == kStateWake) {
        get_logic_tasks()->queue_job(service_job_.get());
        set_service_state(next_service_state_);
        g_news_message_count = 0;
    } else if (state == kStateStopped) {
        service_job_->drop_out();
    }
}

// 0x005451E0
void Hub::update_hosting_status()
{
    net::Hub* net = net::g_net_core.get();
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap_);
    lobby_service_->start_hosting();
    if (presence_service_->authenticated_) { // (0x005480B0: logged in to presence)
        const char* server_name = nullptr;
        get_online_settings()->fetch_setting(kServerName)->fetch_text(kUiString, &server_name, 1);
        std::array<char, 1024> location{};
        RoomManager* lobby = current_lobby(this);
        if (lobby->remote_) {
            const uint32_t private_ip = game_spy::peer_get_private_ip(lobby->remote_); // (the peer's private address)
            std::sprintf(location.data(), "10thug211:%d:%d:%d:%s (%s)", net->get_local_address(), private_ip, kPort,
                         server_name, lobby->room_name_.data());
        } else {
            std::sprintf(location.data(), "10thug211:0:0:0:%s (%s)", server_name, lobby->room_name_.data());
        }
        presence_service_->change_standing(2, script::fetch_text(kHostingStatus), location.data());
    }
    ram_stats()->leave_heap_scope();
}

// 0x00545300
bool Hub::start_server_list()
{
    tasks::PileUp* logic = get_logic_tasks();
    if (!is_local_network_play())
        return true;
    destroy_match_client();
    ram_stats()->enter_heap_scope(ram_stats()->network_heap_);
    const net::AppHost* client = start_matchmaking_client();
    ram_stats()->leave_heap_scope();
    if (!client)
        return false;
    tasks::SchedulerTask* task = server_list_job_.get();
    if (!task->is_on_task_list())
        logic->queue_job(task);
    return true;
}

// 0x00545370
void Hub::stop_server_list()
{
    if (in_internet_mode()) {
        ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap_);
        if (current_lobby(this)->remote_)
            game_spy::peer_stop_listing_games(current_lobby(this)->remote_); // (the browsing stopped)
        ram_stats()->leave_heap_scope();
        return;
    }
    if (net::AppHost* client = get_matchmaking_client())
        client->dispatcher_.tear_down_state();
    server_list_job_->drop_out();
}

// 0x005456B0
void Hub::show_no_servers_found(bool refocus)
{
    script::Struct* params = new_params();
    if (refocus)
        params->put_name_key(kRefocus, kRefocus);
    get_run(kDestroyServerChildren, params);
    get_run(kNoServersFound, params);
    delete_params(params);
}

// 0x00546010
void Hub::clear_chosen_servers()
{
    for (memory::ListNode* node = head_of_scene_list(&servers_); node; node = get_next_node(node)) {
        auto* server = static_cast<HostListing*>(node->data);
        if (!server)
            break;
        server->chosen_ = false;
    }
}

// 0x00546070
int Hub::num_servers_in_menu()
{
    int count = 0;
    for (memory::ListNode* node = head_of_scene_list(&servers_); node; node = get_next_node(node)) {
        auto* server = static_cast<HostListing*>(node->data);
        if (!server)
            break;
        if (server->in_menu_)
            ++count;
    }
    return count;
}

// 0x005460D0
void Hub::show_server_in_menu(void* item, int index)
{
    (void)index;
    auto* server = static_cast<HostListing*>(item);
    ram_stats()->enter_heap_scope(ram_stats()->front_end_heap_);
    if (num_servers_in_menu() == 0)
        get_run(kDestroyServerChildren);
    get_run(kPrepareServerMenu);
    script::Struct* params = new_params();
    params->append_word(kText, server->label_.data());
    params->append_word(kMode, server->mode_sel_.data());
    debug::printf("The game mode is: %s\n", server->mode_sel_.data());
    const uint32_t id = server_id(this, server);
    params->put_name_key(kId, id);
    params->put_name_key(kParent, kServerListMenu);
    params->put_name_key(kPadChooseScript, kChooseSelectedServer);
    params->put_name_key(kFocusScript, kDescribeSelectedServer);
    script::Struct* id_params = new_params();
    id_params->put_name_key(kId, id);
    params->put_sub_struct(kPadChooseParams, id_params);
    params->put_sub_struct(kFocusParams, id_params);
    get_run(kAddMenuItem, params);
    server->in_menu_ = true;
    delete_params(params);
    delete_params(id_params);
    ram_stats()->leave_heap_scope();
}

// 0x005462F0
void Hub::empty_server_listing()
{
    delete_all(&waiting_servers_);
    delete_all(&servers_);
    if (is_local_network_play())
        destroy_match_client();
}

// 0x00546400
bool Hub::has_server(const char* name, uint32_t ip)
{
    for (memory::ListNode* list :
         {static_cast<memory::ListNode*>(&servers_), static_cast<memory::ListNode*>(&waiting_servers_)}) {
        for (memory::ListNode* node = head_of_scene_list(list); node; node = get_next_node(node)) {
            auto* server = static_cast<HostListing*>(node->data);
            if (!server)
                break;
            if (server->remote_ip_ == ip && _stricmp(server->label_.data(), name) == 0)
                return true;
        }
    }
    return false;
}

// 0x005464D0
void* Hub::fetch_host(uint32_t id)
{
    for (memory::ListNode* node = head_of_scene_list(&servers_); node; node = get_next_node(node)) {
        auto* server = static_cast<HostListing*>(node->data);
        if (!server)
            return nullptr;
        if (id == server_id(this, server))
            return server;
    }
    return nullptr;
}

// 0x00547320
void Hub::show_found_servers()
{
    move_waiting_servers(this);
    script::launch_script("update_lobby_server_list", nullptr, nullptr, false);
}

// 0x00547E80
void Hub::refresh_server_list(bool force)
{
    if (!force && frame_clock::get_time() - last_refresh() < kRefreshInterval)
        return;
    last_refresh() = frame_clock::get_time();
    show_pending() = true;
    ram_stats()->enter_heap_scope(ram_stats()->network_heap_);
    stop_server_list();
    empty_server_listing();
    start_server_list();
    if (!in_internet_mode()) {
        scan_local_network();
        ram_stats()->leave_heap_scope();
        return;
    }
    const Hub* manager = the_manager();
    std::array<uint8_t, 4> keys = {{8, 10, 4, 6}}; // (in the original: over its argument's slot)
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap_);
    manager->lobby_service_->clear_lost_connection();
    game_spy::peer_start_listing_games(current_lobby(this)->remote_, keys.data(), 4, nullptr,
                                       code_address(listing_games_callback),
                                       this); // (the games listed)
    ram_stats()->leave_heap_scope();
    ram_stats()->leave_heap_scope();
}

// 0x00547440
void server_list_task(tasks::Task<Hub>* task)
{
    Hub* manager = task->content_;
    if (frame_clock::get_time() - list_clock() > kListClock) {
        list_clock() = frame_clock::get_time();
        return;
    }
    if (!show_pending() || frame_clock::get_time() - last_refresh() <= kShowDelay)
        return;
    move_waiting_servers(manager);
    get_run(kUpdateServerList);
    show_pending() = false;
}

// --- The online service's downloads ---

// 0x00545540
unsigned long __stdcall news_thread(void* argument)
{
    auto* manager = static_cast<Hub*>(argument);
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap_);
    game_spy::get_msleep(100);
    manager->service_state_ = kStateNews;
    manager->request_started_at_ = frame_clock::get_time();
    uint8_t* buffer = manager->news_buffer_.data();
    std::memset(buffer, 0, 0x1900 * 4);
    std::array<char, 256> url{};
    std::sprintf(url.data(), commands::g_texture_flag_ce441 ? "http://www.thug2online.com/testmotd.dat"
                                                     : "http://www.thug2online.com/motd.dat");
    manager->request_ =
        game_spy::http_get_ex(url.data(), nullptr, raw::chars(buffer), static_cast<int>(kNewsBufferSize), nullptr,
                              0, 0, code_address(stubs::do_nothing), &news_callback, manager); // (the download started)
    ram_stats()->leave_heap_scope();
    return 0;
}

// 0x00545620
unsigned long __stdcall motd_thread(void* argument)
{
    auto* manager = static_cast<Hub*>(argument);
    const char* unique_id = "123456789";
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap_);
    manager->get_online_settings()->fetch_setting(kUniqueId)->fetch_text(kUiString, &unique_id, 0); // (not used)
    manager->request_started_at_ = frame_clock::get_time();
    manager->request_ = game_spy::track_usage(0, 10368, "1.0", 0, 0); // (the usage report: the SDK's MOTD request)
    manager->service_state_ = kStateMotd;
    ram_stats()->leave_heap_scope();
    return 0;
}

// 0x00545490
int news_callback(void* request, int result, uint8_t* buffer, uint32_t size_low, int32_t size_high, void* param)
{
    (void)request;
    (void)param;
    Hub* manager = the_manager();
    if (result != 0) {
        manager->service_state_ = kStateNewsFailed;
        return 1;
    }
    int64_t size = (static_cast<int64_t>(size_high) << 32) | size_low;
    // NOT ORIGINAL: no more than the buffer holds (the download is limited to it; the original trusted the size)
    if (size < 0 || std::cmp_greater(size, static_cast<int64_t>(kNewsBufferSize)))
        size = kNewsBufferSize;
    for (int i = 0; static_cast<int64_t>(i) < size; ++i)
        buffer[i] ^= 0x6D;
    // The file (gamenet_messages.h: News). NOT ORIGINAL: a size past the download's end, or a text without its NUL,
    // fails the news (the original read past them).
    util::ByteReader reader(buffer, static_cast<size_t>(size));
    News news;
    if (!read_news(reader, &news) ||
        news.checksum != checksums::generate_crc_caseless(raw::chars(news.data), news.size)) {
        manager->service_state_ = kStateNewsFailed;
        return 1;
    }
    uint8_t* text = buffer + (news.data - buffer);
    manager->motd_text_ = text;
    manager->pending_data_size_ = news.size - news.motd_size;
    manager->pending_data_ = text + news.motd_size;
    manager->motd_received_ = true;
    manager->service_state_ = kStateNewsDone;
    return 1;
}

// 0x00548040
bool is_thug2_location(const char* location)
{
    std::array<char, 256> copy{};
    std::strcpy(copy.data(), location);
    return _stricmp(std::strtok(copy.data(), ":"), "10thug211") == 0;
}

// --- The script commands ---

// 0x00545440: PostGame - the hosting status, unless the lobby's +0x139 (0x0053F810).
bool cfunc_post_game(script::Struct* params, script::Script* script)
{
    (void)params;
    (void)script;
    Hub* manager = the_manager();
    if (!current_lobby(manager)->hosting_)
        manager->update_hosting_status();
    return true;
}

// 0x00545F20: num_servers = the listed servers.
bool cfunc_num_servers_in_lobby(script::Struct* params, script::Script* script)
{
    (void)params;
    script->args_->put_int(kNumServers, total_of(&the_manager()->servers_));
    return true;
}

} // namespace online

