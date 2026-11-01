// The network manager's network settings (online::Hub, 0x00535040-0x00535CB0): the network preferences handed
// to the net library (master servers, addresses, DNS, dial-up, the device), the preferences of the game being set up,
// and connecting to / disconnecting from the internet.

#include <cstdint>
#include "platform/code_address.h"
#include "platform/compiler.h"
#include <cstring>

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's copies into its fixed fields)

#include "engine/core/task_list.h"
#include "engine/net/gamespy.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_manager.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
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
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"
#include "engine/assets/resource_registry.h"
#include "engine/sys/mlp.h"
#include "engine/core/ints.h"
#include "engine/core/pointer_cell.h"

namespace online {

uint32_t g_network_team_mode;

int g_net_skill_level{3};

int g_net_max_players{8};

using namespace detail;

namespace {

constexpr int kMasterServerCount = 10;
constexpr uint8_t kMsgServerQuery = 0x11;
constexpr uint16_t kLanPort = 5153; // the LAN lobby's port

// Checksums.
constexpr uint32_t kChecksum = 0x21902065;                                  // checksum
constexpr uint32_t kUiString = 0x96875C0F;                                  // ui_string
constexpr uint32_t kName = 0xA1DC81F9;                                      // name
constexpr uint32_t kBooleanTrue = 0xF81BC89B;                               // boolean_true
constexpr uint32_t kDefaultMasterServers = 0xE8DA8722;                      // default_master_servers
constexpr uint32_t kUseDefaultMasterServers = 0xE7A3CB08;                   // use_default_master_servers
constexpr uint32_t kMasterServer1 = 0xF480C7DC;                             // master_server1
constexpr uint32_t kMasterServer2 = 0x6D899666;                             // master_server2
constexpr uint32_t kBroadbandType = 0xBB2EB924;                             // broadband_type
constexpr uint32_t kIpDhcp = 0x2189CFF8;                                    // ip_dhcp
constexpr uint32_t kIpAddress = 0xDD002A73;                                 // ip_address
constexpr uint32_t kGateway = 0xEB012280;                                   // gateway
constexpr uint32_t kSubnetMask = 0xF9794735;                                // subnet_mask
constexpr uint32_t kDnsServer = 0x4C3512CD;                                 // dns_server
constexpr uint32_t kDnsServer2 = 0x2D4EDA9F;                                // dns_server2
constexpr uint32_t kHostName = 0x7F91069B;                                  // host_name
constexpr uint32_t kDomainName = 0x0C00AC9E;                                // domain_name
constexpr uint32_t kAutoDns = 0x5DDFCAD3;                                   // auto_dns
constexpr uint32_t kDialupNumber[3] = {0x3510C289, 0x5C867AC6, 0x2B814A50}; // dialup_number, 2, 3
constexpr uint32_t kDialupUsername = 0x7573A233;                            // dialup_username
constexpr uint32_t kDialupPassword = 0xB8EFE291;                            // dialup_password
constexpr uint32_t kDeviceType = 0xFA187DEC;                                // device_type
constexpr uint32_t kDialupAuthentication = 0xE5ADE642;                      // dialup_authentication
constexpr uint32_t kDeviceBroadbandPc = 0x7A8EAB6D;                         // device_broadband_pc
constexpr uint32_t kDeviceBroadbandUsb = 0xDCCC4380;                        // device_broadband_usb
constexpr uint32_t kDeviceBroadbandUsbPppoe = 0x5D92DB35;                   // device_broadband_usb_pppoe
constexpr uint32_t kDeviceUsbModem = 0x3DBCBD95;                            // device_usb_modem
constexpr uint32_t kDeviceSonyModem = 0x221B8CA0;                           // device_sony_modem
constexpr uint32_t kDeviceBroadbandPcPppoe = 0x0304EE13;                    // device_broadband_pc_pppoe
constexpr uint32_t kSpoofPcmciaAdaptorSetup = 0x9ADC64AF;                   // spoof_pcmcia_adaptor_setup
constexpr uint32_t kSpoofUsbAdaptorSetup = 0x5D423821;                      // spoof_usb_adaptor_setup
constexpr uint32_t kTeamGraffitiMode = 0xA8774BF7;                          // team_graffiti_mode
constexpr uint32_t kGraffitiIndividualScoring = 0xC8059823;                 // graffiti_individual_scoring
constexpr uint32_t kGoals = 0x38DBE1D0;                                     // goals
constexpr uint32_t kGameType = 0x9834C4FA;                                  // game_type
constexpr uint32_t kModeInfo = 0xCA5F7FFB;                                  // mode_info
// Global strings overriding the preferences (their names aren't known).
constexpr uint32_t kIpAddressOverride = 0x5F6FDED5;
constexpr uint32_t kGatewayOverride = 0x3E683519;
constexpr uint32_t kSubnetMaskOverride = 0x676BC0CB;
constexpr uint32_t kDnsServerOverride = 0x93CF2232;
constexpr uint32_t kForceBroadbandPc = 0x504ACA1F; // (non-empty: the PCMCIA adaptor's settings)
constexpr uint32_t kForcePcmcia = 0xF3780073;      // (non-empty: the PCMCIA adaptor, a dial-up type 4)

const char* const kNoAddress = "0.0.0.0";         // "0.0.0.0"
const char* const kDefaultDns1 = "205.147.0.100"; // "205.147.0.100"
const char* const kDefaultDns2 = "205.147.0.102"; // "205.147.0.102"
const char* const kEmpty = "";                    // ""
const char* const kHashFieldName = "checksum";    // "checksum"

tasks::PileUp* get_logic_tasks() { return (&assets::g_main_loop_manager->logic_tasks_); }

bool get_filled(const char* text) { return text && *text; }

// A preference's ui_string set to `text` (the parameters freed again).
void set_preference_text(prefs::Preferences* preferences, uint32_t name, const char* text)
{
    script::Struct* values = new_params();
    values->append_word(kUiString, script::kQbvalString, text);
    preferences->change_setting(name, values);
    delete_params(values);
}

} // namespace

// 0x00535040
void Hub::use_network_preferences()
{
    net::Hub* net = net::Hub::sole();
    char (*servers)[16] = master_servers_;
    for (int i = 0; i < kMasterServerCount; ++i)
        servers[i][0] = 0;
    prefs::Preferences* preferences = get_online_settings();

    // (the original's locals, each reused as it reuses its stack slots: a missing value leaves the last one read -
    // uninitialised at first, zero here)
    const char* text = kEmpty;
    uint32_t check = 0;

    // The master servers: the default list, or the two set (unless "0.0.0.0") before it.
    script::Array* defaults = script::fetch_list(kDefaultMasterServers);
    preferences->fetch_setting(kUseDefaultMasterServers)->read_hash_value(kChecksum, &check, 1);
    int next = 0;
    if (check != kBooleanTrue) {
        preferences->fetch_setting(kMasterServer1)->fetch_text(kUiString, &text, 1);
        if (std::strcmp(text, kNoAddress) != 0)
            std::strcpy(servers[next++], text);
        preferences->fetch_setting(kMasterServer2)->fetch_text(kUiString, &text, 1);
        if (std::strcmp(text, kNoAddress) != 0)
            std::strcpy(servers[next++], text);
    }
    // (no bounds check: more than 10 names run over what follows, as in the original)
    for (int i = 0; i < ints::as_signed(defaults->extent_); ++i) {
        auto* entry = defaults->struct_at(i);
        entry->fetch_text(kName, &text, 1);
        std::strcpy(servers[next + i], text);
    }

    uint32_t broadband_type = 0;
    preferences->fetch_setting(kBroadbandType)->read_hash_value(kChecksum, &broadband_type, 1);
    bool dhcp = broadband_type == kIpDhcp;

    // Addresses given outside the preferences (set into them; an address of our own means no DHCP).
    const char* ip_override = script::fetch_text(kIpAddressOverride);
    const char* gateway_override = script::fetch_text(kGatewayOverride);
    const char* subnet_override = script::fetch_text(kSubnetMaskOverride);
    const char* dns_override = script::fetch_text(kDnsServerOverride);
    if (ip_override) {
        if (!*ip_override) {
            ip_override = nullptr;
        } else {
            dhcp = false;
            set_preference_text(preferences, kIpAddress, ip_override);
            if (get_filled(gateway_override))
                set_preference_text(preferences, kGateway, gateway_override);
            if (get_filled(subnet_override))
                set_preference_text(preferences, kSubnetMask, subnet_override);
        }
    }
    if (get_filled(dns_override))
        set_preference_text(preferences, kDnsServer, dns_override);

    net->set_auto_address(dhcp);
    if (dhcp) {
        const char* host_name = nullptr;
        const char* domain_name = nullptr;
        preferences->fetch_setting(kHostName)->fetch_text(kUiString, &host_name, 1);
        net->set_machine_label(host_name);
        preferences->fetch_setting(kDomainName)->fetch_text(kUiString, &domain_name, 1);
        net->set_network_domain(domain_name);
    } else {
        preferences->fetch_setting(kIpAddress)->fetch_text(kUiString, &text, 1);
        net->set_own_address(text);
        preferences->fetch_setting(kGateway)->fetch_text(kUiString, &text, 1);
        net->set_router_addr(text);
        preferences->fetch_setting(kSubnetMask)->fetch_text(kUiString, &text, 1);
        net->set_address_mask(text);
    }

    // DNS: none with auto_dns (and no address of our own); with an address of our own but no DNS server, the two
    // defaults; else the preferences'.
    preferences->fetch_setting(kAutoDns)->read_hash_value(kChecksum, &check, 1);
    if (check == kBooleanTrue && !ip_override) {
        net->set_name_resolver(0, kEmpty);
        net->set_name_resolver(1, kEmpty);
        net->set_name_resolver(2, kEmpty);
    } else if (ip_override && !get_filled(dns_override)) {
        net->set_name_resolver(0, kDefaultDns1);
        net->set_name_resolver(1, kDefaultDns2);
    } else {
        preferences->fetch_setting(kDnsServer)->fetch_text(kUiString, &text, 1);
        net->set_name_resolver(0, text);
        preferences->fetch_setting(kDnsServer2)->fetch_text(kUiString, &text, 1);
        net->set_name_resolver(1, text);
    }

    // Dial-up.
    const char* number = nullptr;
    for (int i = 0; i < 3; ++i) {
        preferences->fetch_setting(kDialupNumber[i])->fetch_text(kUiString, &number, 1);
        net->set_dialup_number(i, number);
    }
    const char* username = nullptr;
    preferences->fetch_setting(kDialupUsername)->fetch_text(kUiString, &username, 1);
    net->set_dialup_user_name(username);
    const char* password = nullptr;
    preferences->fetch_setting(kDialupPassword)->fetch_text(kUiString, &password, 1);
    net->set_dialup_password(password);

    // The device: its type and the connection type.
    uint32_t device = 0;
    preferences->fetch_setting(kDeviceType)->read_hash_value(kChecksum, &device, 1);
    const bool force_broadband_pc = *script::fetch_text(kForceBroadbandPc) != 0;
    if (*script::fetch_text(kForcePcmcia)) {
        if (ip_override)
            get_run(kSpoofPcmciaAdaptorSetup);
        net->set_adapter_kind(3);
        net->set_net_link_kind(4);
    } else if (device == kDeviceBroadbandPc || force_broadband_pc) {
        get_run(kSpoofPcmciaAdaptorSetup);
        net->set_adapter_kind(3);
        net->set_net_link_kind(2);
    } else if (device == kDeviceBroadbandUsb || ip_override) {
        if (ip_override)
            get_run(kSpoofUsbAdaptorSetup);
        net->set_adapter_kind(1);
        net->set_net_link_kind(2);
    } else if (device == kDeviceBroadbandUsbPppoe) {
        net->set_adapter_kind(1);
        net->set_net_link_kind(3);
    } else if (device == kDeviceUsbModem || device == kDeviceSonyModem) {
        // A modem: at most 3 players, no observers.
        net->set_adapter_kind(device == kDeviceUsbModem ? 2 : 4);
        net->set_net_link_kind(1);
        if (get_player_limit() > 3)
            set_max_players_preference(3);
        set_max_observers_preference(0);
    } else if (device == kDeviceBroadbandPcPppoe) {
        net->set_adapter_kind(3);
        net->set_net_link_kind(3);
    } else {
        net->set_net_link_kind(0);
        net->set_adapter_kind(0);
    }

    preferences->fetch_setting(kDialupAuthentication)->read_hash_value(kChecksum, &check, 1);
    net->set_flag_2e_6(check == kBooleanTrue);
}

// 0x00535AD0
uint32_t Hub::get_team_graffiti_mode()
{
    uint32_t mode = kGraffitiIndividualScoring;
    if (in_net_game() && is_squad_match())
        get_online_settings()->fetch_setting(kTeamGraffitiMode)->read_hash_value(kChecksum, &mode, 1);
    return mode;
}

// The preferences of the game being set up: the network game's, else the skate module's (+0x58: split screen's).
prefs::Preferences* Hub::get_game_preferences()
{
    auto* skate = detail::get_skate();
    if (in_net_game())
        return get_online_settings();
    return static_cast<modules::Skate*>(skate)->get_split_view_settings();
}

// 0x00535B20
uint32_t Hub::get_goals_preference()
{
    // On a miss the original returns its uninitialised local: the slot ECX was pushed into - the manager.
    auto goals = static_cast<uint32_t>(cell::address(this));
    get_game_preferences()->fetch_setting(kGoals)->read_hash_value(kHashFieldName, &goals, 1);
    return goals;
}

// 0x00535B70
uint32_t Hub::get_game_type_preference()
{
    auto type = static_cast<uint32_t>(cell::address(this)); // (as above)
    get_game_preferences()->fetch_setting(kGameType)->read_hash_value(kChecksum, &type, 1);
    return type;
}

// 0x00535C10
bool Hub::connect_to_internet(uint32_t online_script, uint32_t fail_script)
{
    tasks::PileUp* tasks = get_logic_tasks();
    net::Hub* net = net::Hub::sole();
    online_fail_script_ = fail_script;
    online_script_ = online_script;
    bool online = false;
    if (net->is_online()) {
        online = true;
    } else {
        modem_state_shown_ = -1;
        online = net->connect_to_internet();
        if (!online) {
            // A modem or a PPPoE connection carries on in the online state's task.
            if (net->get_net_link_kind() == 1 || net->get_net_link_kind() == 3)
                tasks->queue_job(dialup_job_.get());
            return online;
        }
    }
    if (online_script_)
        get_run(online_script_);
    return online;
}

// 0x00535CB0
void Hub::disconnect_from_internet(uint32_t script)
{
    tasks::PileUp* tasks = get_logic_tasks();
    net::Hub* net = net::Hub::sole();
    modem_state_shown_ = -1;
    if (script)
        online_fail_script_ = script;
    tasks->queue_job(dialup_job_.get());
    set_service_state(2);
    net->disconnect_from_internet();
}

// 0x00536C10
void Hub::cancel_connect_to_internet()
{
    Hub* manager = g_online_hub;
    net::Hub* net = net::Hub::sole();
    tasks::SchedulerTask* task = manager->dialup_job_.get();
    switch (net->get_dialup_status()) {
    case 0:
    case 1:
        task->drop_out();
        manager->modem_state_shown_ = -1;
        get_logic_tasks()->queue_job(manager->dialup_job_.get());
        manager->set_service_state(2);
        net::Hub::sole()->disconnect_from_internet();
        break;
    case 2:
        net->set_dialup_status(6);
        break;
    case 5:
        task->drop_out();
        get_run(manager->online_fail_script_);
        break;
    case 9:
        task->drop_out();
        break;
    default:
        break;
    }
}

uint32_t g_asked_for;
bool g_join_server_ip_succeeded;

// 0x00536490
void Hub::find_server_at(uint32_t ip)
{
    stop_server_list(); // (the server lists cleared)
    empty_server_listing();
    start_server_list();
    g_join_server_ip_succeeded = false;
    if (!browser_client_)
        start_matchmaking_client();
    if (!ip) {
        g_asked_for = 0;
        return;
    }
    if (browser_client_) {
        uint32_t request[3]; // (the time; the rest is the original's stack)
        request[0] = frame_clock::get_time();
        request[1] = request[2] = 0;
        g_asked_for = ip;
        browser_client_->deliver_to(kMsgServerQuery, sizeof(request), request, ip, kLanPort, 0);
    }
}

// 0x00536640
void Hub::service_game_spy()
{
    game_spy::cd_key_think();
    if (g_online_active && g_online_hub->in_net_game() && g_query_reporting)
        game_spy::get_qr2_think(nullptr);
}

// 0x00536A40
const char* Hub::get_game_type_name()
{
    script::Array* modes = script::fetch_list(kModeInfo);
    // (the original's locals, reused: a missing checksum compares the last value read - uninitialised at first)
    uint32_t type = 0;
    uint32_t mode = 0;
    for (int i = 0; i < ints::as_signed(modes->extent_); ++i) {
        auto* entry = modes->struct_at(i);
        get_game_preferences()->fetch_setting(kGameType)->read_hash_value(kChecksum, &type, 1);
        entry->read_hash_value(kChecksum, &mode, 0);
        if (type == mode) {
            const char* name = cell::pointer<const char>(type);
            entry->fetch_text(kName, &name, 1);
            return name;
        }
    }
    return nullptr;
}

// 0x00536B90
void Hub::change_arena(uint32_t level)
{
    level_ = level;
    void* peer = lobby_service_->get_lobby_handle();
    if (peer && game_spy::peer_is_connected(peer) && lobby_service_->is_hosting())
        game_spy::peer_state_changed(peer); // (the peer's state changed: the GameSpy SDK)
    // (cleared just before it's tested: the reporting's state-changed call below never happens)
    g_open_waiting = false;
    if (g_online_active && g_online_hub->in_net_game() && g_query_reporting && g_open_waiting)
        game_spy::qr2_send_state_changed(nullptr);
}

} // namespace online
