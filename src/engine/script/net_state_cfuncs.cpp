// Script commands (CFunctions) of the skate game, 0x005A0880-0x005A0F00: starting, joining and leaving a server, the
// network mode and setup, going online, the controllers' vibration, observing, slow motion, player names (see
// game_cfuncs.h).

#include "engine/script/game_cfuncs.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <array>

#include "engine/core/enum_ops.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/stubs.h"
#include "platform/crt.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_client.h"
#include "engine/net/net_conn.h"
#include "engine/net/net_lists.h"
#include "engine/net/net_manager.h"
#include "engine/net/net_metrics.h"
#include "game/rider/rider.h"
#include "engine/net/player_info.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "game/skate/skate_net.h"
#include "platform/game_clock.h"
#include "engine/sys/mlp.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"
#include "engine/assets/resource_registry.h"
#include "game/rider/core_physics.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include "engine/net/service.h"
#include "engine/script/script_state.h"
#include "wire/message_ids.h"
#include <gsl/pointers>

namespace {
// The adapter's last error, as the net layer numbers them.
// The skill choice with the most trouble ("5: Bring It On!").
constexpr int kSkillBringItOn = 5;
constexpr int kNetErrorNotConnected = 5;
constexpr int kNetErrorDhcp = 6;
constexpr int kNetErrorChangedDevice = 7;
constexpr int kMessageTypeCount = 256;
// The message a local skater's client sends the host when its script says so; the sequenced group of the game's messages.
// "no server / no address" from the command line and from inet_addr.
constexpr uint32_t kNoAddress = 0xFFFFFFFFu;

// Names (the checksums of their text).
constexpr uint32_t kAutoDns = 0x5DDFCAD3u; // auto_dns
constexpr uint32_t kBooleanTrue = 0xF81BC89Bu; // boolean_true
constexpr uint32_t kBroadbandType = 0xBB2EB924u; // broadband_type
constexpr uint32_t kChecksum = 0x21902065u; // checksum
constexpr uint32_t kDeviceNone = 0x09562A0Au; // device_none
constexpr uint32_t kDeviceType = 0xFA187DECu; // device_type
constexpr uint32_t kIpDhcp = 0x2189CFF8u; // ip_dhcp
constexpr uint32_t kLoadMainMenu = 0xE92ECAFEu; // Load_MainMenu
constexpr uint32_t kNum4 = 0x1FA67C3Du; // num_4
constexpr uint32_t kSTRING = 0x61414D56u; // STRING
constexpr uint32_t kUseDefaultMasterServers = 0xE7A3CB08u; // use_default_master_servers

} // namespace

namespace commands {

std::array<char, kNetworkIdBytes>& network_id()
{
    constexpr size_t kNetworkIdSize = 16;
    static std::array<char, kNetworkIdSize> network_id{};
    return network_id;
}

std::array<char, kNetworkPasswordBytes>& network_password()
{
    constexpr size_t kNetworkPasswordSize = 12;
    static std::array<char, kNetworkPasswordSize> network_password{};
    return network_password;
}

// The hosted game's name: "Beenox" unless -HOSTNAME gives another (winmain.cpp).
std::array<char, kHostedGameNameBytes>& hosted_game_name()
{
    constexpr size_t kHostedGameNameSize = 16;
    static std::array<char, kHostedGameNameSize> hosted_game_name{{"Beenox"}};
    return hosted_game_name;
}

namespace {

constexpr uint32_t kJoinScript = 0xFF5C39B6;           // (names unknown) JoinServer's scripts: before joining,
constexpr uint32_t kJoinedNetGameScript = 0xFF898BE6;  // and after each client of a network game
constexpr uint32_t kNetSetupFailedScript = 0x6D51F203; // (name unknown) TestNetSetup's when the setup failed
constexpr uint32_t kNetSetupError = 0xC4745838;        // (name unknown) its error text
constexpr uint32_t kOnlineScript = 0x90FF204D;         // (names unknown) ConnectToInternet's scripts
constexpr uint32_t kFailScript = 0xDE64FC3E;
constexpr uint32_t kCallback = 0x86068BD9;             // "callback"
constexpr uint32_t kSlomoComponent = 0x5BD63B29;       // (name unknown) the local skater's component SetSlomo tells
constexpr uint32_t kPlayerNamesOffScript = 0x271D435C; // (name unknown)
constexpr uint32_t kSkaterType = 0xBAF9B341;           // (name unknown) the skaters' object type

objects::GameEntity* get_home_rider() { return modules::Skate::sole()->get_home_rider(); }
memory::Hub* mem_manager() { return memory::Hub::get_pull(); }
// The game mode's player count (0x00553190: its integer 0x6DC3D6F1).
uint32_t game_mode_players() { return static_cast<uint32_t>(goals::game_mode()->get_starting_rider_count()); }

// inet_addr through the game's import (0x005BEAAE).
uint32_t get_inet_addr(const char* address) { return crt::get_inet_addr(address); }

} // namespace

// 0x005A0880: launch_host (in the bottom-up heap): the skate module's server; the network manager's LAN mode asked
// when it isn't on the internet (the answer unused).
bool cfunc_start_server(script::Struct*, script::Script*)
{
    memory::Hub* memory = mem_manager();
    online::Hub* game_net = online::online_hub();
    memory->enter_heap_scope(static_cast<memory::MemSource*>(memory->bottom_up_heap()));
    modules::SkateNet::launch_host(modules::Skate::sole());
    if (!game_net->in_internet_mode())
        game_net->is_local_network_play();
    mem_manager()->leave_heap_scope();
    return true;
}

// 0x005A08D0: LeaveServer (0x0056B890).
bool cfunc_leave_server(script::Struct*, script::Script*)
{
    modules::Skate::sole()->leave_server();
    return true;
}

// 0x005A08E0: JoinServer ["<ip>" <port>] (in the bottom-up heap): the script 0xFF5C39B6 and the pending symbols
// first. On the server, a local client per player of the game mode, each joined and given the skaters' handlers
// (in a network game, 0xFF898BE6 run after each); else a client joining the address given (both needed).
bool cfunc_join_server(script::Struct* params, script::Script*)
{
    memory::Hub* memory = mem_manager();
    online::Hub* game_net = online::online_hub();
    memory->enter_heap_scope(static_cast<memory::MemSource*>(memory->bottom_up_heap()));
    script::launch_script(kJoinScript, nullptr, nullptr, false, nullptr);
    script::process_pending_symbols();
    if (game_net->get_on_server()) {
        const uint32_t players = game_mode_players();
        for (uint32_t i = 0; i < players; ++i) {
            net::AppHost* client = game_net->start_client_session(false, true, true, static_cast<int>(i));
            game_net->join_server({.ip = 0, .port = 0}, static_cast<int>(i));
            modules::SkateNet::add_client_handlers(modules::Skate::sole(), client, static_cast<int>(i));
            if (game_net->in_net_game())
                script::launch_script(kJoinedNetGameScript, nullptr, nullptr, false, nullptr);
        }
    } else {
        const char* address = nullptr;
        int port = 0;
        params->fetch_text(0u, &address, false);
        params->fetch_int_value(0u, &port, false);
        if (port && address) {
            game_net->start_client_session(false, false, true, 0);
            game_net->join_server({.ip = get_inet_addr(address), .port = static_cast<uint16_t>(port)}, 0);
        }
    }
    mem_manager()->leave_heap_scope();
    return true;
}

// 0x005A0A00: SetNetworkMode <0 | 1 | 2> (other values ignored).
bool cfunc_set_network_mode(script::Struct* params, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    int mode = 0;
    params->fetch_int_value(0u, &mode, 0);
    if (mode == 0 || mode == 1 || mode == 2)
        game_net->set_network_mode(mode);
    return true;
}

// 0x005A0A60: ProfileTasks [frames (1)]: the main loop's task profile over that many frames.
bool cfunc_profile_tasks(script::Struct* params, script::Script*)
{
    auto* main_loop = assets::get_main_loop_manager();
    int frames = 1;
    params->fetch_int_value(0u, &frames, 0);
    debug::printf("\n\nProfiling task for %d frames:\n", frames);
    main_loop->profile_frames(frames);
    return true;
}

// 0x005A0AB0: UseNetworkPreferences.
bool cfunc_use_network_preferences(script::Struct*, script::Script*)
{
    online::online_hub()->use_network_preferences();
    return true;
}

// 0x005A0AC0: TestNetSetup success_script=<script>: whether the network is set up (on PC always: 0x005B9760).
// Set up: the script run with these parameters; not: the error's text (by the net library's last error) added as
// 0xC4745838 and the script 0x6D51F203 run with them.
bool cfunc_test_net_setup(script::Struct* params, script::Script*)
{
    net::Hub* net = net::net_core();
    const bool ok = stubs::return_true(); // (the net manager's setup test, a shared stub)
    if (ok) {
        uint32_t script = 0; // (unset in the original when missing - an asserted parameter)
        params->read_hash_value("success_script", &script, true); // "success_script"
        script::launch_script(script, params, nullptr, false, nullptr);
        return ok;
    }
    const char* text = nullptr;
    if (net->get_last_error() == kNetErrorNotConnected)
        text = "net_error_not_connected";
    else if (net->get_last_error() == 4)
        text = "net_error_device_error";
    else if (net->get_last_error() == 2)
        text = "net_error_not_detected";
    else if (net->get_last_error() == kNetErrorDhcp)
        text = "net_error_dhcp_error";
    else if (net->get_last_error() == kNetErrorChangedDevice)
        text = "net_error_changed_device";
    else if (net->get_last_error() == 3)
        text = "net_error_invalid_irx";
    else
        text = "net_error_general_error";
    params->append_word(kNetSetupError, script::QbValueKind::kString, script::fetch_text(text));
    script::launch_script(kNetSetupFailedScript, params, nullptr, false, nullptr);
    return ok;
}

// 0x005A0BD0 / 0x005A0BE0: NeedToTestNetSetup (no, on PC) / CanChangeDevices (yes): shared stubs.
bool cfunc_need_to_test_net_setup(script::Struct*, script::Script*) { return stubs::return_false(); }
bool cfunc_can_change_devices(script::Struct*, script::Script*) { return stubs::return_true(); }

// 0x005A0BF0: LoadNetworkDrivers: with the global string use_deci2 set, the globals viewer_ip, viewer_subnet and
// viewer_gateway; else none. The net manager's answer.
bool cfunc_load_network_drivers(script::Struct*, script::Script*)
{
    const net::Hub* net = net::net_core();
    const char* use = script::fetch_text("use_deci2");
    if (!use || !*use)
        return net->load_network_drivers(nullptr, nullptr, nullptr);
    const char* ip = script::fetch_text("viewer_ip");
    const char* gateway = script::fetch_text("viewer_gateway");
    const char* subnet_mask = script::fetch_text("viewer_subnet");
    return net->load_network_drivers(ip, subnet_mask, gateway);
}

// 0x005A0C60: ConnectToInternet 0x90FF204D=<script> 0xDE64FC3E=<script>: the network manager's answer.
bool cfunc_connect_to_internet(script::Struct* params, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    uint32_t online_script = 0;
    uint32_t fail_script = 0;
    params->read_hash_value(kOnlineScript, &online_script, 0);
    params->read_hash_value(kFailScript, &fail_script, 0);
    return game_net->connect_to_internet({.online_script_ = online_script, .fail_script_ = fail_script});
}

// 0x005A0CC0: CancelConnectToInternet.
bool cfunc_cancel_connect_to_internet(script::Struct*, script::Script*)
{
    online::Hub::cancel_connect_to_internet();
    return true;
}

// 0x005A0CD0: IsOnline.
bool cfunc_is_online(script::Struct*, script::Script*) { return net::net_core()->is_online(); }

// 0x005A0CE0: DisconnectFromInternet [callback=<script>].
bool cfunc_disconnect_from_internet(script::Struct* params, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    uint32_t callback = 0;
    params->read_hash_value(kCallback, &callback, 0);
    game_net->disconnect_from_internet(callback);
    return true;
}

// 0x005A0D20: EnableActuators [0 | 1 (1)]: the vibration of each of the two controllers the skate module uses
// (its +0x394 entries, 8 bytes apart) on or off.
bool cfunc_enable_actuators(script::Struct* params, script::Script*)
{
    int enable = 1;
    params->fetch_int_value(0u, &enable, 0);
    auto* input = pad_input::get_input_manager();
    for (int i = 0; i < 2; ++i) {
        if (core::at(modules::Skate::sole()->participants(), i).vibration_) {
            if (enable)
                input->enable_actuators(i);
            else
                input->stop_rumble(i);
        }
    }
    return true;
}

// 0x005A0D80: InNetGame.
bool cfunc_in_net_game(script::Struct*, script::Script*) { return online::online_hub()->in_net_game(); }

// 0x005A0D90: IsObserving (4 callers): the local player's an observer; with none, a joining player that is local
// and observing (flags 1 and 4), else whether the join mode is observing (1).
bool cfunc_is_observing(script::Struct*, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    online::detail::NewPlayerSearch search;
    if (online::PlayerRecord* player = game_net->get_own_player())
        return player->is_observer() != 0;
    for (const online::JoinerRecord* player = game_net->begin_joiner_walk(&search); player;
         player = game_net->step_joiner_walk(&search)) {
        const online::PlayerFlag flags = player->flag_bits();
        if (core::any(flags & online::PlayerFlag::kLocal) && core::any(flags & online::PlayerFlag::kObserver))
            return true;
    }
    return game_net->get_entry_mode() == 1;
}

// 0x005A0E50: SetSlomo [speed (1)]: the game clock's speed, and the local skater's component 0x5BD63B29's (+0x2E8).
bool cfunc_set_slomo(script::Struct* params, script::Script*)
{
    float speed = 1.0f;
    params->fetch_float(0u, &speed, 0);
    frame_clock::set_game_clock_speed(speed);
    if (get_home_rider()) {
        if (objects::ObjectModule* component = get_home_rider()->fetch_module(kSlomoComponent))
            core::down_cast<skater::SkaterPhysicsModule>(component)->slomo_speed() = speed;
    }
    return true;
}

// 0x005A0EB0: TogglePlayerNames: toggled; turned off, the script 0x271D435C runs.
bool cfunc_toggle_player_names(script::Struct*, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    game_net->toggle_player_names();
    if (!game_net->wants_name_tags())
        script::launch_script(kPlayerNamesOffScript, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x005A0EF0: SetCurrentGameType (the skate module's 0x00564FD0).
bool cfunc_set_current_game_type(script::Struct*, script::Script*)
{
    modules::Skate::sole()->set_current_game_type();
    return true;
}

namespace {
// DumpNetMessageStats' output - compiled out of this build (only the metrics' getters are still called).
inline void print_message_stat(int /*id*/, int /*value*/) {}
} // namespace

// 0x005A0F00: DumpNetMessageStats: for each connection of the server (or else client 0), each message id's received
// and sent totals read - twice each (the printing is compiled out). Always true.
bool cfunc_dump_net_message_stats(script::Struct*, script::Script*)
{
    net::ConnSearch search;
    online::Hub* game_net = online::online_hub();
    net::AppHost* app = game_net->host();
    if (!app) {
        app = game_net->fetch_patron(0);
        if (!app)
            return true;
    }
    for (net::Conn* conn = app->begin_conn_walk(&search); conn; conn = app->step_conn_walk(&search)) {
        for (net::Gauges* metrics : {&conn->receive_metrics(), &conn->send_metrics()}) {
            for (int pass = 0; pass < 2; ++pass) {
                for (int id = 0; id < kMessageTypeCount; ++id) {
                    print_message_stat(id, metrics->get_msg_byte_total(id));
                    print_message_stat(id, metrics->get_msg_count_for_type(id));
                }
            }
        }
    }
    return true;
}

// 0x005A1020: SetServerMode [off]: the server's side on (off without the flag "off").
bool cfunc_set_server_mode(script::Struct* params, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    game_net->set_server_mode(!params->has_bare_flag("off")); // "off"
    return true;
}

// 0x005A1050: NotifyBailDone: run on a local skater, its client tells the server (message 0x45, sequenced, group 8).
bool cfunc_notify_bail_done(script::Struct*, script::Script* script)
{
    online::Hub* game_net = online::online_hub();
    objects::Entity* object = script ? script->entity().get_pull() : nullptr;
    if (!object || object->kind_tag() != kSkaterType)
        return true;
    auto* skater = core::down_cast<objects::Rider>(object);
    if (!skater->is_local_skater())
        return true;
    if (net::AppHost* client = game_net->fetch_patron(skater->view_slot())) {
        net::NetMessageInfo desc;
        desc.set_ident(online::MessageId::kJoined);
        desc.set_queue(net::NetMessageInfo::kLaneOrdered);
        desc.set_group_id(online::MessageGroup::kGame);
        core::down_cast<net::Guest>(client)->send_to_host(&desc);
    }
    return true;
}

// 0x005A10D0: EnterObserverMode: the observe request sent.
bool cfunc_enter_observer_mode(script::Struct*, script::Script*)
{
    online::online_hub()->send_observe_request();
    return true;
}

// 0x005A10E0: ObserveNextSkater (the skate module's 0x005651D0).
bool cfunc_observe_next_skater(script::Struct*, script::Script*)
{
    modules::Skate::observe_next_skater();
    return true;
}

// 0x005A10F0: RefreshServerList [force_refresh].
bool cfunc_refresh_server_list(script::Struct* params, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    game_net->refresh_server_list(params->has_bare_flag("force_refresh")); // "force_refresh"
    return true;
}

// 0x005A1120: StartServerList.
bool cfunc_start_server_list(script::Struct*, script::Script*)
{
    online::online_hub()->start_server_list();
    return true;
}

// 0x005A1130: StartLobbyList: the service state 13 (the lobby list) - after the current one (state 1 next) when
// the service is in state 2.
bool cfunc_start_lobby_list(script::Struct*, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    if (game_net->get_service_state() == online::ServiceState::kStopped) {
        game_net->set_next_service_state(online::ServiceState::kLobbyListStart);
        game_net->set_service_state(online::ServiceState::kWake);
    } else {
        game_net->set_service_state(online::ServiceState::kLobbyListStart);
    }
    return true;
}

// 0x005A1160 / 0x005A1170: StopServerList / FreeServerList.
bool cfunc_stop_server_list(script::Struct*, script::Script*)
{
    online::online_hub()->stop_server_list();
    return true;
}

bool cfunc_free_server_list(script::Struct*, script::Script*)
{
    online::online_hub()->empty_server_listing();
    return true;
}

// 0x005A1180: InSplitScreenGame (12 callers): a multiplayer game (the skate module's 0x005651F0) that isn't a network
// game.
bool cfunc_in_split_screen_game(script::Struct*, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    return modules::Skate::is_multiplayer() != 0 && !game_net->in_net_game();
}

// 0x005A11B0: InMultiplayerGame (5 callers): the skate module's 0x005651F0 (a tail jump: its EAX).
bool cfunc_in_multiplayer_game(script::Struct*, script::Script*) { return modules::Skate::is_multiplayer() != 0; }

// 0x005A1220: OnServer.
bool cfunc_on_server(script::Struct*, script::Script*) { return online::online_hub()->get_on_server(); }

// 0x005A12D0: ToggleScores.
bool cfunc_toggle_scores(script::Struct*, script::Script*)
{
    online::online_hub()->toggle_scores();
    return true;
}

// 0x005A1680: JoinWithPassword 0x61414D56="<password>": the join tried again with it.
bool cfunc_join_with_password(script::Struct* params, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    const char* password = nullptr; // (unset in the original when missing - an asserted parameter)
    params->fetch_text(kSTRING, &password, true);
    game_net->retry_entry_with_key(password);
    return true;
}

// 0x005A1820 / 0x005A1830: LoadPendingPlayers / LaunchQueuedScripts.
bool cfunc_load_pending_players(script::Struct*, script::Script*)
{
    online::online_hub()->re_add_pending_players();
    return true;
}

bool cfunc_launch_queued_scripts(script::Struct*, script::Script*)
{
    online::online_hub()->launch_queued_scripts();
    return true;
}

// 0x005A1AC0: StartNetworkLobby: back to the lobby.
bool cfunc_start_network_lobby(script::Struct*, script::Script*)
{
    online::online_hub()->reset_to_lobby();
    return true;
}

// 0x005A1AD0: ObserversAllowed: unless the connection is a modem (type 1).
bool cfunc_observers_allowed(script::Struct*, script::Script*) { return net::net_core()->get_net_link_kind() != 1; }

// 0x005A1AF0: NumPlayersAllowed <num_n>: over a modem, every count but 0x1FA67C3D; otherwise all.
bool cfunc_num_players_allowed(script::Struct* params, script::Script*)
{
    uint32_t players = 0;
    if (net::net_core()->get_net_link_kind() != 1)
        return true;
    params->read_hash_value(0u, &players, 0);
    return players != kNum4;
}

namespace {
// A network preference's checksum (asserting it's there).
uint32_t network_preference(uint32_t name)
{
    script::Struct* preference = online::online_hub()->get_online_settings()->fetch_setting(name);
    uint32_t value = 0; // (unset in the original when missing - an asserted parameter)
    preference->read_hash_value(kChecksum, &value, true); // "checksum"
    return value;
}
} // namespace

// 0x005A1B30 / 0x005A1B70 / 0x005A1BB0: AutoDNS / UsingDefaultMasterServers / UsingDHCP: the network preferences
// 0x5DDFCAD3 / 0xE7A3CB08 / 0xBB2EB924 are 0xF81BC89B / 0xF81BC89B / 0x2189CFF8.
bool cfunc_auto_dns(script::Struct*, script::Script*) { return network_preference(kAutoDns) == kBooleanTrue; }
bool cfunc_using_default_master_servers(script::Struct*, script::Script*)
{
    return network_preference(kUseDefaultMasterServers) == kBooleanTrue;
}
bool cfunc_using_dhcp(script::Struct*, script::Script*) { return network_preference(kBroadbandType) == kIpDhcp; }

// 0x005A1BF0: InInternetMode.
bool cfunc_in_internet_mode(script::Struct*, script::Script*) { return online::online_hub()->in_internet_mode(); }

// 0x005A1C00: EnteringNetGame: a network game, and the skate module's +0x37C is 0xE92ECAFE.
bool cfunc_entering_net_game(script::Struct*, script::Script*)
{
    online::Hub* game_net = online::online_hub();
    return game_net->in_net_game() && modules::Skate::sole()->previous_level() == kLoadMainMenu;
}

// 0x005A1C30: DeviceChosen: the network preference 0xFA187DEC isn't 0x09562A0A.
bool cfunc_device_chosen(script::Struct*, script::Script*) { return network_preference(kDeviceType) != kDeviceNone; }

// 0x005A1C70: GameIsOver.
bool cfunc_game_is_over(script::Struct*, script::Script*) { return online::online_hub()->has_match_ended(); }

namespace {

// gethostbyname through the game's import (0x005BEABA).
uint32_t host_first_address(const char* name) { return crt::host_first_address(name); }
} // namespace

// 0x005A28A0: TryJoinInternetGame: the server chosen in the list (0x0067EFB8, unless -1) looked for; a join is being
// tried from now on.
bool cfunc_try_join_internet_game(script::Struct*, script::Script*)
{
    const uint32_t server = online::net_game_state().command_line_server();
    if (server != online::kNoCommandLineServer)
        online::online_hub()->find_server_at(server);
    script::script_state().set_trying_to_join(true);
    debug::printf("ScriptTryJoinInternetGame\n"); // "ScriptTryJoinInternetGame\n"
    return true;
}

// 0x005A28D0: TryJoinServerIP string="<address or host name>": the address (inet_addr, else the host's first
// address) looked for and a join tried; nothing if neither gives one.
bool cfunc_try_join_server_ip(script::Struct* params, script::Script*)
{
    const char* address = nullptr;                // (unset in the original when missing - an asserted parameter)
    params->fetch_text("string", &address, true); // "string"
    uint32_t ip = get_inet_addr(address);
    if (ip == kNoAddress) {
        ip = host_first_address(address); // (its h_addr_list[0], or none)
    }
    if (ip != kNoAddress) {
        script::script_state().set_trying_to_join(true);
        online::online_hub()->find_server_at(ip);
    }
    debug::printf("ScriptTryJoinServerIP\n"); // "ScriptTryJoinServerIP\n"
    return true;
}

// 0x005A2960: TryJoinServerIPCancel.
bool cfunc_try_join_server_ip_cancel(script::Struct*, script::Script*)
{
    online::online_hub()->find_server_at(0);
    script::script_state().set_trying_to_join(false);
    debug::printf("ScriptTryJoinServerIPCancel\n"); // "ScriptTryJoinServerIPCancel\n"
    return true;
}

// 0x005A2990: ForceServerListRefresh: the servers found so far shown.
bool cfunc_force_server_list_refresh(script::Struct*, script::Script*)
{
    online::online_hub()->show_found_servers();
    return true;
}

namespace {

// A new, empty script structure (from the structures' pool) and its deletion.
uint32_t get_checksums(const char* name) { return checksums::hash_from_text(name); }
// The network preference `name` set to {ui_string="<text>"}.
void set_string_preference(prefs::Preferences* prefs, uint32_t name, const char* text)
{
    const script::StructPtr preference = script::make_struct();
    preference->append_word(get_checksums("ui_string"), script::QbValueKind::kString, text); // "ui_string"
    prefs->change_setting(name, preference.get());
}
// ... set to {ui_string="<text>" checksum=<the checksum of `value`>}.
void set_choice_preference(script::Struct* preference, const char* text, uint32_t value)
{
    preference->append_word("ui_string", text); // "ui_string"
    preference->put_name_key("checksum", value);                // "checksum"
}

} // namespace

// 0x005A4900: the network preferences set from the host settings (PartyMod's / the Beenox port's globals): the
// server name "Beenox", the password (0x007CCBD4), the network id (0x007CCBC4, if any), the player count (0x0067EFD0),
// the team mode (0x007CCBE0: 0 none, 2 / 3 / 4 teams; others leave it empty) and the skill level (0x0067EFD4, 1-5).
void init_beenox_preferences()
{
    online::Hub* game_net = online::online_hub();
    prefs::Preferences* prefs = game_net->get_online_settings();
    set_string_preference(prefs, get_checksums("server_name"), hosted_game_name().data()); // "server_name"
    set_string_preference(prefs, get_checksums("password"), network_password().data());    // "password"
    if (core::at(network_id(), 0) != '\0')
        set_string_preference(prefs, get_checksums("network_id"), network_id().data()); // "network_id"
    game_net->set_max_players_preference(online::net_game_state().net_max_players());
    gsl::owner<script::Struct*> teams = script::new_struct();
    switch (online::net_game_state().network_team_mode()) {
    case 0:
        set_choice_preference(teams, "None", get_checksums("teams_none"));
        break; // "None", teams_none
    case 2:
        set_choice_preference(teams, "2", get_checksums("teams_two"));
        break; // "2", teams_two
    case 3:
        set_choice_preference(teams, "3", get_checksums("teams_three"));
        break; // "3", teams_three
    case 4:
        set_choice_preference(teams, "4", get_checksums("teams_four"));
        break; // "4", teams_four
    default:
        break;
    }
    prefs->change_setting(get_checksums("team_mode"), teams); // "team_mode"
    script::free_struct(teams);
    gsl::owner<script::Struct*> skill = script::new_struct();
    switch (online::net_game_state().net_skill_level()) {
    case 1:
        set_choice_preference(skill, "1: Baby Steps", get_checksums("num_1"));
        break; // "1: Baby Steps", num_1
    case 2:
        set_choice_preference(skill, "2: I Can Crawl!", get_checksums("num_2"));
        break; // "2: I Can Crawl!", num_2
    case 3:
        set_choice_preference(skill, "3: Hold My Own", get_checksums("num_3"));
        break; // "3: Hold My Own", num_3
    case 4:
        set_choice_preference(skill, "4: Let's Do This", get_checksums("num_4"));
        break; // "4: Let's Do This", num_4
    case kSkillBringItOn:
        set_choice_preference(skill, "5: Bring It On!", get_checksums("num_5"));
        break; // "5: Bring It On!", num_5
    default:
        break;
    }
    prefs->change_setting(get_checksums("skill_level"), skill); // "skill_level"
    script::free_struct(skill);
}

// 0x005A4D90: InitPrefsBeenox: init_beenox_preferences. True.
bool cfunc_init_prefs_beenox(script::Struct*, script::Script*)
{
    init_beenox_preferences();
    return true;
}

} // namespace commands
