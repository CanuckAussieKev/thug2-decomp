// The network manager's server list and the online service's downloads (see server_list.h).

#include "game/online/server_list.h"
#include "game/online/service.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <span>
#include <utility>
#include <array>
#include <gsl/assert>

#include "base/core/checksums.h"
#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/task_list.h"
#include "game/online/buddy_man.h"
#include "game/online/gamenet_handler_util.h"
#include "platform/gamespy.h"
#include "game/online/gamenet_manager.h"
#include "game/online/gamenet_lobby.h"
#include "engine/net/net_app.h"
#include "engine/net/net_manager.h"
#include "game/online/player_info.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "engine/sys/stopwatch.h"
#include "platform/code_address.h"
#include "engine/assets/resource_registry.h"
#include "engine/gfx/texture_cfuncs.h"
#include "base/core/stubs.h"
#include "game/goals/objective.h"
#include "engine/script/fwd.h"
#include "engine/script/context.h"
#include "engine/memory/fwd.h"
#include "game/online/fwd.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/online/score_list.h"
#include "game/online/stats_man.h"
#include "engine/sys/mlp.h"
#include "formats/byte_stream.h"
#include "base/core/compiler.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "wire/gamenet_messages.h"
#include "base/core/no_destroy.h"
#include "game/online/net_game_state.h"
#include "base/core/gathered_call.h"
#include "base/core/str.h"
#include "game/online/online_identity.h"

THUG2_MSVC_WARNING(disable : 4996) // (the original's strncpy, kept)

namespace online {

using namespace detail;

namespace {
// A name is kept to 15 characters (the label holds 16); the SDK's qr2 key ids a listing asks for.
constexpr uint8_t kQr2KeyNumPlayers = 8;
constexpr uint8_t kQr2KeyMaxPlayers = 10;
constexpr uint8_t kQr2KeyHostPort = 4;
constexpr uint8_t kQr2KeyGameType = 6;
constexpr int kListingPollMilliseconds = 100;
constexpr size_t kNewsClearBytes = 0x1900 * 4;
constexpr int kHighWordShift = 32;
constexpr uint8_t kNewsScramble = 0x6D;

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

constexpr int kPort = 0x1421;                // 5153
constexpr uint32_t kRefreshInterval = 5000;  // ms
constexpr uint32_t kListClock = 3000;        // ms
constexpr uint32_t kShowDelay = 500;         // ms
constexpr uint32_t kNewsBufferSize = 0x6400; // bytes

memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks()); }

// NOT ORIGINAL grouping - server_list.cpp's state (the original's globals). Made on first use, never destroyed
// (server_list_state()).
struct ServerListState {
    uint32_t last_refresh_{};
    bool show_pending_{};
    uint32_t list_clock_{};
};

ServerListState& server_list_state()
{
    static core::NoDestroy<ServerListState> state;
    return state.get();
}

uint32_t& last_refresh() { return server_list_state().last_refresh_; }
bool& show_pending() { return server_list_state().show_pending_; }
uint32_t& list_clock() { return server_list_state().list_clock_; }

// Walking a list as the original does: a node whose data is -1 is the head (checked before null).
memory::ListNode* head_of_scene_list(memory::ListNode* head)
{
    memory::ListNode* node = head->next();
    return node->data() == memory::ListNode::head_marker() ? nullptr : node;
}
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next();
    return next->data() == memory::ListNode::head_marker() ? nullptr : next;
}
template <typename T>
T* data_of(memory::ListNode* node)
{
    return node ? static_cast<T*>(node->data()) : nullptr;
}

// The nodes counted (the original's inlined count: data -1 checked before null).
int total_of(memory::ListNode* head)
{
    int count = 0;
    for (const memory::ListNode* node = head->next(); node->data() != memory::ListNode::head_marker() && node;
         node = node->next()) {
        ++count;
    }
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
    node->prev()->set_next(node->next());
    node->next()->set_prev(node->prev());
    node->set_next(node);
    node->set_prev(node);
}
void get_append(memory::ListNode* head, memory::ListNode* node)
{
    node->set_prev(head->prev());
    node->set_next(head);
    head->prev()->set_next(node);
    head->set_prev(node);
}

// A server's id in the menu and in fetch_host (HostListing::id_).
uint32_t server_id(const HostListing* server) { return server->id(); }

// NOT ORIGINAL: the next server's id (never 0).
uint32_t next_server_id()
{
    static uint32_t last = 0;
    if (++last == 0) {
        ++last;
    }
    return last;
}

// The waiting servers moved to the list (and the menu), after the "none found" message.
void move_waiting_servers(Hub* manager)
{
    online::Hub::show_no_servers_found(true);
    memory::ListNode* node = head_of_scene_list(&manager->waiting_servers());
    auto* server = data_of<HostListing>(node);
    while (server) {
        node = get_next_node(node);
        auto* following = data_of<HostListing>(node);
        unlink_node(server);
        memory::ListNode* list = &manager->servers();
        manager->show_server_in_menu(server, total_of(list));
        get_append(list, server);
        server = following;
    }
}

} // namespace

// --- HostListing ---

// 0x00545F60
HostListing::HostListing() : ListNode(), id_(next_server_id())
{
    make_node(this);
    participants_.make_head();
    text::format_to(level_, "None");
    text::format_to(mode_sel_, "None");
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
    if ((flags & 1) != 0U) {
        memory::Hub::dealloc(this);
    }
    return this;
}

// 0x00545790
void HostListing::enroll_player(const char* name, int rating)
{
    ram_stats()->enter_heap_scope(net_game_state().online_hub()->in_internet_mode()
                                      ? static_cast<memory::MemSource*>(ram_stats()->internet_bottom_up_heap())
                                      : static_cast<memory::MemSource*>(ram_stats()->network_heap()));
    void* block = memory::alloc_zeroed(sizeof(PlayerRecord));
    PlayerRecord* player = block ? memory::construct_in<PlayerRecord>(block, PlayerFlag::kNone) : nullptr;
    Expects(player != nullptr);
    player->set_skill_points(rating);
    std::strncpy(player->label().data(), name, kLabelLength);
    core::at(player->label(), kLabelLength) = '\0';
    get_append(&participants_, player);
    ram_stats()->leave_heap_scope();
}

// 0x005465A0
void HostListing::clear_players()
{
    Hub* manager = net_game_state().online_hub();
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
        auto* player = static_cast<PlayerRecord*>(node->data());
        if (!player) {
            return nullptr;
        }
        if (index == 0) {
            return player->label().data();
        }
        --index;
    }
    return nullptr;
}
int HostListing::get_entrant_rank(int index)
{
    for (memory::ListNode* node = head_of_scene_list(&participants_); node; node = get_next_node(node)) {
        auto* player = static_cast<PlayerRecord*>(node->data());
        if (!player) {
            return 0;
        }
        if (index == 0) {
            return player->skill_points();
        }
        --index;
    }
    return 0;
}

// --- The manager's server list ---

// 0x005451B0 / 0x005451C0
void Hub::set_next_service_state(ServiceState state) { assign_next_service_state(state); }
ServiceState Hub::get_service_state() const { return service_state(); }

// 0x005453E0
void Hub::set_service_state(ServiceState state)
{
    assign_service_state(state);
    if (state == ServiceState::kWake) {
        get_logic_tasks()->queue_job(service_job_.get());
        set_service_state(next_service_state());
        net_game_state().set_news_message_count(0);
    } else if (state == ServiceState::kStopped) {
        service_job_->drop_out();
    }
}

// 0x005451E0
void Hub::update_hosting_status()
{
    net::Hub* net = net::net_core();
    ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap());
    lobby_service_->start_hosting();
    if (presence_service_->authenticated()) { // (0x005480B0: logged in to presence)
        const char* server_name = nullptr;
        get_online_settings()->fetch_setting(kServerName)->fetch_text(kUiString, &server_name, 1);
        constexpr size_t kLocationSize = 1024;
        std::array<char, kLocationSize> location{};
        RoomManager* lobby = lobby_service_.get();
        if (lobby->remote()) {
            const uint32_t private_ip = game_spy::peer_get_private_ip(lobby->remote()); // (the peer's private address)
            text::format_to(location, "10thug211:%d:%d:%d:%s (%s)", net->get_local_address(), private_ip, kPort,
                            server_name, lobby->room_name().data());
        } else {
            text::format_to(location, "10thug211:0:0:0:%s (%s)", server_name, lobby->room_name().data());
        }
        presence_service_->change_standing(2, script::fetch_text(kHostingStatus), location.data());
    }
    ram_stats()->leave_heap_scope();
}

// 0x00545300
bool Hub::start_server_list()
{
    tasks::PileUp* logic = get_logic_tasks();
    if (is_local_network_play() == 0) {
        return true;
    }
    destroy_match_client();
    ram_stats()->enter_heap_scope(ram_stats()->network_heap());
    const net::AppHost* client = start_matchmaking_client();
    ram_stats()->leave_heap_scope();
    if (!client) {
        return false;
    }
    tasks::SchedulerTask* task = server_list_job_.get();
    if (!task->is_on_task_list()) {
        logic->queue_job(task);
    }
    return true;
}

// 0x00545370
void Hub::stop_server_list()
{
    if (in_internet_mode()) {
        ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap());
        if (lobby_service_.get()->remote()) {
            game_spy::peer_stop_listing_games(lobby_service_.get()->remote()); // (the browsing stopped)
        }
        ram_stats()->leave_heap_scope();
        return;
    }
    if (net::AppHost* client = get_matchmaking_client()) {
        client->dispatcher().tear_down_state();
    }
    server_list_job_->drop_out();
}

// 0x005456B0
void Hub::show_no_servers_found(bool refocus)
{
    const script::StructPtr params = script::make_struct();
    if (refocus) {
        params->put_name_key(checksums::Name{kRefocus}, kRefocus);
    }
    get_run(kDestroyServerChildren, params.get());
    get_run(kNoServersFound, params.get());
}

// 0x00546010
void Hub::clear_chosen_servers()
{
    for (memory::ListNode* node = head_of_scene_list(&servers_); node; node = get_next_node(node)) {
        auto* server = static_cast<HostListing*>(node->data());
        if (!server) {
            break;
        }
        server->set_chosen(false);
    }
}

// 0x00546070
int Hub::num_servers_in_menu()
{
    int count = 0;
    for (memory::ListNode* node = head_of_scene_list(&servers_); node; node = get_next_node(node)) {
        auto* server = static_cast<HostListing*>(node->data());
        if (!server) {
            break;
        }
        if (server->in_menu()) {
            ++count;
        }
    }
    return count;
}

// 0x005460D0
void Hub::show_server_in_menu(void* item, int index)
{
    (void)index;
    auto* server = static_cast<HostListing*>(item);
    ram_stats()->enter_heap_scope(ram_stats()->front_end_heap());
    if (num_servers_in_menu() == 0) {
        get_run(kDestroyServerChildren);
    }
    get_run(kPrepareServerMenu);
    script::StructPtr params = script::make_struct();
    params->append_word(kText, server->label().data());
    params->append_word(kMode, server->mode_sel().data());
    debug::printf("The game mode is: %s\n", server->mode_sel().data());
    const uint32_t id = server_id(server);
    params->put_name_key(checksums::Name{kId}, id);
    params->put_name_key(checksums::Name{kParent}, kServerListMenu);
    params->put_name_key(checksums::Name{kPadChooseScript}, kChooseSelectedServer);
    params->put_name_key(checksums::Name{kFocusScript}, kDescribeSelectedServer);
    script::StructPtr id_params = script::make_struct();
    id_params->put_name_key(checksums::Name{kId}, id);
    params->put_sub_struct(kPadChooseParams, id_params.get());
    params->put_sub_struct(kFocusParams, id_params.get());
    get_run(kAddMenuItem, params.get());
    server->set_in_menu(true);
    params.reset();
    id_params.reset();
    ram_stats()->leave_heap_scope();
}

// 0x005462F0
void Hub::empty_server_listing()
{
    delete_all(&waiting_servers_);
    delete_all(&servers_);
    if (is_local_network_play() != 0) {
        destroy_match_client();
    }
}

// 0x00546400
bool Hub::has_server(const char* name, uint32_t ip)
{
    for (memory::ListNode* list :
         {&servers_, &waiting_servers_}) {
        for (memory::ListNode* node = head_of_scene_list(list); node; node = get_next_node(node)) {
            auto* server = static_cast<HostListing*>(node->data());
            if (!server) {
                break;
            }
            if (server->remote_ip() == ip && _stricmp(server->label().data(), name) == 0) {
                return true;
            }
        }
    }
    return false;
}

// 0x005464D0
void* Hub::fetch_host(uint32_t id)
{
    for (memory::ListNode* node = head_of_scene_list(&servers_); node; node = get_next_node(node)) {
        auto* server = static_cast<HostListing*>(node->data());
        if (!server) {
            return nullptr;
        }
        if (id == server_id(server)) {
            return server;
        }
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
    if (!force && frame_clock::get_time() - last_refresh() < kRefreshInterval) {
        return;
    }
    last_refresh() = frame_clock::get_time();
    show_pending() = true;
    ram_stats()->enter_heap_scope(ram_stats()->network_heap());
    stop_server_list();
    empty_server_listing();
    start_server_list();
    if (!in_internet_mode()) {
        scan_local_network();
        ram_stats()->leave_heap_scope();
        return;
    }
    const Hub* manager = net_game_state().online_hub();
    std::array<uint8_t, 4> keys = {{kQr2KeyNumPlayers, kQr2KeyMaxPlayers, kQr2KeyHostPort, kQr2KeyGameType}}; // (in the original: over its argument's slot)
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap());
    manager->lobby_service_->clear_lost_connection();
    game_spy::peer_start_listing_games(lobby_service_.get()->remote(), keys.data(), 4, nullptr,
                                       code_address(&core::gathered<GamesListing, &listing_games_callback, void, void*, int, const char*, void*, int, int, int, Hub*>),
                                       this); // (the games listed)
    ram_stats()->leave_heap_scope();
    ram_stats()->leave_heap_scope();
}

// 0x00547440
void server_list_task(tasks::Task<Hub>* task)
{
    Hub* manager = task->content();
    if (frame_clock::get_time() - list_clock() > kListClock) {
        list_clock() = frame_clock::get_time();
        return;
    }
    if (!show_pending() || frame_clock::get_time() - last_refresh() <= kShowDelay) {
        return;
    }
    move_waiting_servers(manager);
    get_run(kUpdateServerList);
    show_pending() = false;
}

// --- The online service's downloads ---

// 0x00545540
unsigned long __stdcall news_thread(void* argument)
{
    auto* manager = static_cast<Hub*>(argument);
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap());
    game_spy::get_msleep(kListingPollMilliseconds);
    manager->assign_service_state(ServiceState::kNewsDownload);
    manager->set_request_started_at(frame_clock::get_time());
    auto& buffer = manager->news_buffer();
    std::memset(buffer.data(), 0, kNewsClearBytes);
    constexpr size_t kUrlSize = 256;
    std::array<char, kUrlSize> url{};
    text::format_to(url, "%s",
                    commands::texture_flag_ce441() ? "http://www.thug2online.com/testmotd.dat"
                                                   : "http://www.thug2online.com/motd.dat");
    manager->set_request(game_spy::http_get_ex(url.data(), nullptr, buffer.data(), static_cast<int>(kNewsBufferSize), nullptr,
                              0, 0, code_address(stubs::do_nothing), &news_callback, manager)); // (the download started)
    ram_stats()->leave_heap_scope();
    return 0;
}

// 0x00545620
unsigned long __stdcall motd_thread(void* argument)
{
    auto* manager = static_cast<Hub*>(argument);
    const char* unique_id = "123456789";
    ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap());
    manager->get_online_settings()->fetch_setting(kUniqueId)->fetch_text(kUiString, &unique_id, 0); // (not used)
    manager->set_request_started_at(frame_clock::get_time());
    manager->set_request(game_spy::track_usage(0, net::kGameSpyProductId, "1.0", 0, 0)); // (the usage report: the SDK's MOTD request)
    manager->assign_service_state(ServiceState::kMotdShow);
    ram_stats()->leave_heap_scope();
    return 0;
}

// 0x00545490
int news_callback(void* request, int result, uint8_t* buffer, uint32_t size_low, int32_t size_high, void* param)
{
    (void)request;
    (void)param;
    Hub* manager = net_game_state().online_hub();
    if (result != 0) {
        manager->assign_service_state(ServiceState::kNewsEnded);
        return 1;
    }
    int64_t size = (static_cast<int64_t>(size_high) << kHighWordShift) | size_low;
    // NOT ORIGINAL: no more than the buffer holds (the download is limited to it; the original trusted the size)
    if (size < 0 || std::cmp_greater(size, static_cast<int64_t>(kNewsBufferSize))) {
        size = kNewsBufferSize;
    }
    const std::span<uint8_t> downloaded(buffer, static_cast<size_t>(size));
    for (uint8_t& byte : downloaded) {
        byte ^= kNewsScramble;
    }
    // The file (gamenet_messages.h: News). NOT ORIGINAL: a size past the download's end, or a text without its NUL,
    // fails the news (the original read past them).
    util::ByteReader reader(buffer, static_cast<size_t>(size));
    News news;
    if (!read_news(reader, &news) ||
        news.checksum != checksums::generate_crc_caseless(news.motd, news.size)) { // (motd: the data's start)
        manager->assign_service_state(ServiceState::kNewsEnded);
        return 1;
    }
    const auto text_at = static_cast<size_t>(news.data - buffer);
    manager->set_motd_text(news.motd);
    manager->set_pending_data_size(news.size - news.motd_size);
    manager->set_pending_data(downloaded.subspan(text_at + news.motd_size).data());
    manager->set_motd_received(true);
    manager->assign_service_state(ServiceState::kMotdStart);
    return 1;
}

// 0x00548040
bool is_thug2_location(const char* location)
{
    constexpr size_t kCopySize = 256;
    std::array<char, kCopySize> copy{};
    text::copy_cut(copy, location);
    return _stricmp(std::strtok(copy.data(), ":"), "10thug211") == 0;
}

// --- The script commands ---

// 0x00545440: PostGame - the hosting status, unless the lobby's +0x139 (0x0053F810).
bool cfunc_post_game(script::Struct* params, script::Context context)
{
    (void)params;
    (void)context;
    Hub* manager = net_game_state().online_hub();
    if (!manager->lobby_service().get()->hosting()) {
        manager->update_hosting_status();
    }
    return true;
}

// 0x00545F20: num_servers = the listed servers.
bool cfunc_num_servers_in_lobby(script::Struct* params, script::Context context)
{
    (void)params;
    context.args()->put_int(checksums::Name{kNumServers}, total_of(&net_game_state().online_hub()->servers()));
    return true;
}

} // namespace online

