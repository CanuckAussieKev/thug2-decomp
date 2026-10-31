// The network manager's network settings (GameNet::Manager, 0x00535040-0x00535CB0): the network preferences handed
// to the net library (master servers, addresses, DNS, dial-up, the device), the preferences of the game being set up,
// and connecting to / disconnecting from the internet.

#include <cstdint>
#include "hook/game_code.h"
#include <cstring>

#pragma warning(disable : 4996) // (strcpy: the original's copies into its fixed fields)

#include "game/core/task.h"
#include "game/net/gamespy.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_manager.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"
#include "game/ass/assman.h"

namespace GameNet {

GAME_GLOBAL(uint32_t, g_networkTeamMode, 0x007CCBE0);

GAME_GLOBAL(int, g_netSkillLevel, 0x0067EFD4, 3);

GAME_GLOBAL(int, g_netMaxPlayers, 0x0067EFD0, 8);

using namespace Detail;

namespace {

constexpr uint32_t kMasterServers = 0x9954;  // char[10][16]: the master servers' names
constexpr int kMasterServerCount = 10;
constexpr uint32_t kConnectTask = 0xD8;      // Tsk::BaseTask* (the online state's)
constexpr uint32_t kOnlineState = 0x1A8;     // int (-1: starting)
constexpr uint32_t kOnlineScript = 0x1C4;    // uint32_t: run once online
constexpr uint32_t kOfflineScript = 0x1C8;   // uint32_t: run when connecting fails / after disconnecting
constexpr uint8_t kMsgFindServer = 0x11;
constexpr uint16_t kLanPort = 5153;          // the LAN lobby's port

// Checksums.
constexpr uint32_t kChecksum = 0x21902065;            // checksum
constexpr uint32_t kUiString = 0x96875C0F;            // ui_string
constexpr uint32_t kName = 0xA1DC81F9;                // name
constexpr uint32_t kBooleanTrue = 0xF81BC89B;         // boolean_true
constexpr uint32_t kDefaultMasterServers = 0xE8DA8722; // default_master_servers
constexpr uint32_t kUseDefaultMasterServers = 0xE7A3CB08; // use_default_master_servers
constexpr uint32_t kMasterServer1 = 0xF480C7DC;       // master_server1
constexpr uint32_t kMasterServer2 = 0x6D899666;       // master_server2
constexpr uint32_t kBroadbandType = 0xBB2EB924;       // broadband_type
constexpr uint32_t kIpDhcp = 0x2189CFF8;              // ip_dhcp
constexpr uint32_t kIpAddress = 0xDD002A73;           // ip_address
constexpr uint32_t kGateway = 0xEB012280;             // gateway
constexpr uint32_t kSubnetMask = 0xF9794735;          // subnet_mask
constexpr uint32_t kDnsServer = 0x4C3512CD;           // dns_server
constexpr uint32_t kDnsServer2 = 0x2D4EDA9F;          // dns_server2
constexpr uint32_t kHostName = 0x7F91069B;            // host_name
constexpr uint32_t kDomainName = 0x0C00AC9E;          // domain_name
constexpr uint32_t kAutoDns = 0x5DDFCAD3;             // auto_dns
constexpr uint32_t kDialupNumber[3] = {0x3510C289, 0x5C867AC6, 0x2B814A50}; // dialup_number, 2, 3
constexpr uint32_t kDialupUsername = 0x7573A233;      // dialup_username
constexpr uint32_t kDialupPassword = 0xB8EFE291;      // dialup_password
constexpr uint32_t kDeviceType = 0xFA187DEC;          // device_type
constexpr uint32_t kDialupAuthentication = 0xE5ADE642; // dialup_authentication
constexpr uint32_t kDeviceBroadbandPc = 0x7A8EAB6D;   // device_broadband_pc
constexpr uint32_t kDeviceBroadbandUsb = 0xDCCC4380;  // device_broadband_usb
constexpr uint32_t kDeviceBroadbandUsbPppoe = 0x5D92DB35; // device_broadband_usb_pppoe
constexpr uint32_t kDeviceUsbModem = 0x3DBCBD95;      // device_usb_modem
constexpr uint32_t kDeviceSonyModem = 0x221B8CA0;     // device_sony_modem
constexpr uint32_t kDeviceBroadbandPcPppoe = 0x0304EE13; // device_broadband_pc_pppoe
constexpr uint32_t kSpoofPcmciaAdaptorSetup = 0x9ADC64AF; // spoof_pcmcia_adaptor_setup
constexpr uint32_t kSpoofUsbAdaptorSetup = 0x5D423821;    // spoof_usb_adaptor_setup
constexpr uint32_t kTeamGraffitiMode = 0xA8774BF7;    // team_graffiti_mode
constexpr uint32_t kGraffitiIndividualScoring = 0xC8059823; // graffiti_individual_scoring
constexpr uint32_t kGoals = 0x38DBE1D0;               // goals
constexpr uint32_t kGameType = 0x9834C4FA;            // game_type
constexpr uint32_t kModeInfo = 0xCA5F7FFB;            // mode_info
// Global strings overriding the preferences (their names aren't known).
constexpr uint32_t kIpAddressOverride = 0x5F6FDED5;
constexpr uint32_t kGatewayOverride = 0x3E683519;
constexpr uint32_t kSubnetMaskOverride = 0x676BC0CB;
constexpr uint32_t kDnsServerOverride = 0x93CF2232;
constexpr uint32_t kForceBroadbandPc = 0x504ACA1F;    // (non-empty: the PCMCIA adaptor's settings)
constexpr uint32_t kForcePcmcia = 0xF3780073;         // (non-empty: the PCMCIA adaptor, a dial-up type 4)

const char* const kNoAddress = GAME_TEXT(0x0064C9A0, "0.0.0.0");   // "0.0.0.0"
const char* const kDefaultDns1 = GAME_TEXT(0x0064C990, "205.147.0.100"); // "205.147.0.100"
const char* const kDefaultDns2 = GAME_TEXT(0x0064C980, "205.147.0.102"); // "205.147.0.102"
const char* const kEmpty = GAME_TEXT(0x006469B4, "");       // ""
const char* const kChecksumName = GAME_TEXT(0x0064C9A8, "checksum"); // "checksum"

Tsk::Stack* LogicTasks() { return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager) + 4 /* the main loop's task stacks (+4: the logic tasks) */); }

bool Filled(const char* text) { return text && *text; }

// A preference's ui_string set to `text` (the parameters freed again).
void SetPreferenceText(Prefs::Preferences* preferences, uint32_t name, const char* text)
{
    Script::CStruct* values = NewParams();
    values->AddString(kUiString, Script::ESYMBOLTYPE_STRING, text);
    preferences->SetPreference(name, values);
    DeleteParams(values);
}

} // namespace

// 0x00535040
void Manager::UseNetworkPreferences()
{
    Net::Manager* net = Net::Manager::Instance();
    char(*servers)[16] = reinterpret_cast<char(*)[16]>(reinterpret_cast<uint8_t*>(this) + kMasterServers);
    for (int i = 0; i < kMasterServerCount; ++i)
        servers[i][0] = 0;
    Prefs::Preferences* preferences = GetNetworkPreferences();

    // (the original's locals, each reused as it reuses its stack slots: a missing value leaves the last one read -
    // uninitialised at first, zero here)
    const char* text = kEmpty;
    uint32_t check = 0;

    // The master servers: the default list, or the two set (unless "0.0.0.0") before it.
    Script::CArray* defaults = Script::GetArray(kDefaultMasterServers);
    preferences->GetPreference(kUseDefaultMasterServers)->GetChecksum(kChecksum, &check, 1);
    int next = 0;
    if (check != kBooleanTrue) {
        preferences->GetPreference(kMasterServer1)->GetString(kUiString, &text, 1);
        if (std::strcmp(text, kNoAddress) != 0)
            std::strcpy(servers[next++], text);
        preferences->GetPreference(kMasterServer2)->GetString(kUiString, &text, 1);
        if (std::strcmp(text, kNoAddress) != 0)
            std::strcpy(servers[next++], text);
    }
    // (no bounds check: more than 10 names run over what follows, as in the original)
    for (int i = 0; i < static_cast<int>(defaults->m_size); ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(defaults->GetElement(i));
        entry->GetString(kName, &text, 1);
        std::strcpy(servers[next + i], text);
    }

    uint32_t broadbandType = 0;
    preferences->GetPreference(kBroadbandType)->GetChecksum(kChecksum, &broadbandType, 1);
    bool dhcp = broadbandType == kIpDhcp;

    // Addresses given outside the preferences (set into them; an address of our own means no DHCP).
    const char* ipOverride = Script::GetString(kIpAddressOverride);
    const char* gatewayOverride = Script::GetString(kGatewayOverride);
    const char* subnetOverride = Script::GetString(kSubnetMaskOverride);
    const char* dnsOverride = Script::GetString(kDnsServerOverride);
    if (ipOverride) {
        if (!*ipOverride) {
            ipOverride = nullptr;
        } else {
            dhcp = false;
            SetPreferenceText(preferences, kIpAddress, ipOverride);
            if (Filled(gatewayOverride))
                SetPreferenceText(preferences, kGateway, gatewayOverride);
            if (Filled(subnetOverride))
                SetPreferenceText(preferences, kSubnetMask, subnetOverride);
        }
    }
    if (Filled(dnsOverride))
        SetPreferenceText(preferences, kDnsServer, dnsOverride);

    net->SetDHCP(dhcp);
    if (dhcp) {
        const char* hostName = nullptr;
        const char* domainName = nullptr;
        preferences->GetPreference(kHostName)->GetString(kUiString, &hostName, 1);
        net->SetHostName(hostName);
        preferences->GetPreference(kDomainName)->GetString(kUiString, &domainName, 1);
        net->SetDomainName(domainName);
    } else {
        preferences->GetPreference(kIpAddress)->GetString(kUiString, &text, 1);
        net->SetLocalIP(text);
        preferences->GetPreference(kGateway)->GetString(kUiString, &text, 1);
        net->SetGateway(text);
        preferences->GetPreference(kSubnetMask)->GetString(kUiString, &text, 1);
        net->SetSubnetMask(text);
    }

    // DNS: none with auto_dns (and no address of our own); with an address of our own but no DNS server, the two
    // defaults; else the preferences'.
    preferences->GetPreference(kAutoDns)->GetChecksum(kChecksum, &check, 1);
    if (check == kBooleanTrue && !ipOverride) {
        net->SetDNSServer(0, kEmpty);
        net->SetDNSServer(1, kEmpty);
        net->SetDNSServer(2, kEmpty);
    } else if (ipOverride && !Filled(dnsOverride)) {
        net->SetDNSServer(0, kDefaultDns1);
        net->SetDNSServer(1, kDefaultDns2);
    } else {
        preferences->GetPreference(kDnsServer)->GetString(kUiString, &text, 1);
        net->SetDNSServer(0, text);
        preferences->GetPreference(kDnsServer2)->GetString(kUiString, &text, 1);
        net->SetDNSServer(1, text);
    }

    // Dial-up.
    const char* number = nullptr;
    for (int i = 0; i < 3; ++i) {
        preferences->GetPreference(kDialupNumber[i])->GetString(kUiString, &number, 1);
        net->SetDialupNumber(i, number);
    }
    const char* username = nullptr;
    preferences->GetPreference(kDialupUsername)->GetString(kUiString, &username, 1);
    net->SetDialupUserName(username);
    const char* password = nullptr;
    preferences->GetPreference(kDialupPassword)->GetString(kUiString, &password, 1);
    net->SetDialupPassword(password);

    // The device: its type and the connection type.
    uint32_t device = 0;
    preferences->GetPreference(kDeviceType)->GetChecksum(kChecksum, &device, 1);
    bool forceBroadbandPc = *Script::GetString(kForceBroadbandPc) != 0;
    if (*Script::GetString(kForcePcmcia)) {
        if (ipOverride)
            Run(kSpoofPcmciaAdaptorSetup);
        net->SetDeviceType(3);
        net->SetConnectionType(4);
    } else if (device == kDeviceBroadbandPc || forceBroadbandPc) {
        Run(kSpoofPcmciaAdaptorSetup);
        net->SetDeviceType(3);
        net->SetConnectionType(2);
    } else if (device == kDeviceBroadbandUsb || ipOverride) {
        if (ipOverride)
            Run(kSpoofUsbAdaptorSetup);
        net->SetDeviceType(1);
        net->SetConnectionType(2);
    } else if (device == kDeviceBroadbandUsbPppoe) {
        net->SetDeviceType(1);
        net->SetConnectionType(3);
    } else if (device == kDeviceUsbModem || device == kDeviceSonyModem) {
        // A modem: at most 3 players, no observers.
        net->SetDeviceType(device == kDeviceUsbModem ? 2 : 4);
        net->SetConnectionType(1);
        if (GetMaxPlayers() > 3)
            SetMaxPlayersPreference(3);
        SetMaxObserversPreference(0);
    } else if (device == kDeviceBroadbandPcPppoe) {
        net->SetDeviceType(3);
        net->SetConnectionType(3);
    } else {
        net->SetConnectionType(0);
        net->SetDeviceType(0);
    }

    preferences->GetPreference(kDialupAuthentication)->GetChecksum(kChecksum, &check, 1);
    net->SetFlag2E6(check == kBooleanTrue);
}

// 0x00535AD0
uint32_t Manager::GetTeamGraffitiMode()
{
    uint32_t mode = kGraffitiIndividualScoring;
    if (InNetGame() && IsTeamGame())
        GetNetworkPreferences()->GetPreference(kTeamGraffitiMode)->GetChecksum(kChecksum, &mode, 1);
    return mode;
}

// The preferences of the game being set up: the network game's, else the skate module's (+0x58: split screen's).
Prefs::Preferences* Manager::GetGamePreferences()
{
    uint8_t* skate = Detail::Skate();
    if (InNetGame())
        return GetNetworkPreferences();
    return Call<Prefs::Preferences*>(GAME_CODE(0x00564D80, &Mdl::Skate::GetSplitScreenPreferences), skate);
}

// 0x00535B20
uint32_t Manager::GetGoalsPreference()
{
    // On a miss the original returns its uninitialised local: the slot ECX was pushed into - the manager.
    auto goals = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    GetGamePreferences()->GetPreference(kGoals)->GetChecksum(kChecksumName, &goals, 1);
    return goals;
}

// 0x00535B70
uint32_t Manager::GetGameTypePreference()
{
    auto type = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)); // (as above)
    GetGamePreferences()->GetPreference(kGameType)->GetChecksum(kChecksum, &type, 1);
    return type;
}

// 0x00535C10
bool Manager::ConnectToInternet(uint32_t onlineScript, uint32_t failScript)
{
    Tsk::Stack* tasks = LogicTasks();
    Net::Manager* net = Net::Manager::Instance();
    Field<uint32_t>(this, kOfflineScript) = failScript;
    Field<uint32_t>(this, kOnlineScript) = onlineScript;
    bool online;
    if (net->IsOnline()) {
        online = true;
    } else {
        Field<int>(this, kOnlineState) = -1;
        online = net->ConnectToInternet();
        if (!online) {
            // A modem or a PPPoE connection carries on in the online state's task.
            if (net->GetConnectionType() == 1 || net->GetConnectionType() == 3)
                tasks->AddTask(Field<Tsk::BaseTask*>(this, kConnectTask));
            return online;
        }
    }
    if (Field<uint32_t>(this, kOnlineScript))
        Run(Field<uint32_t>(this, kOnlineScript));
    return online;
}

// 0x00535CB0
void Manager::DisconnectFromInternet(uint32_t script)
{
    Tsk::Stack* tasks = LogicTasks();
    Net::Manager* net = Net::Manager::Instance();
    Field<int>(this, kOnlineState) = -1;
    if (script)
        Field<uint32_t>(this, kOfflineScript) = script;
    tasks->AddTask(Field<Tsk::BaseTask*>(this, kConnectTask));
    SetServiceState(2);
    net->DisconnectFromInternet();
}

// 0x00536C10
void __cdecl Manager::CancelConnectToInternet()
{
    Manager* manager = g_gameNetManager;
    Net::Manager* net = Net::Manager::Instance();
    Tsk::BaseTask* task = Field<Tsk::BaseTask*>(manager, kConnectTask);
    switch (net->GetModemState()) {
    case 0:
    case 1:
        task->Remove();
        Field<int>(manager, kOnlineState) = -1;
        LogicTasks()->AddTask(Field<Tsk::BaseTask*>(manager, kConnectTask));
        manager->SetServiceState(2);
        Net::Manager::Instance()->DisconnectFromInternet();
        break;
    case 2:
        net->SetModemState(6);
        break;
    case 5:
        task->Remove();
        Run(Field<uint32_t>(manager, kOfflineScript));
        break;
    case 9:
        task->Remove();
        break;
    default:
        break;
    }
}

GAME_GLOBAL(uint32_t, g_askedFor, 0x007CCA84);
GAME_GLOBAL(bool, g_joinServerIPSucceeded, 0x007CCA88);

// 0x00536490
void Manager::FindServerAt(uint32_t ip)
{
    StopServerList(); // (the server lists cleared)
    ClearServerList();
    StartServerList();
    g_joinServerIPSucceeded = false;
    if (!m_matchClient)
        SpawnMatchClient();
    if (!ip) {
        g_askedFor = 0;
        return;
    }
    if (m_matchClient) {
        uint32_t request[3]; // (the time; the rest is the original's stack)
        request[0] = Tmr::GetTime();
        request[1] = request[2] = 0;
        g_askedFor = ip;
        m_matchClient->SendMessageTo(kMsgFindServer, sizeof(request), request, ip, kLanPort, 0);
    }
}

// 0x00536640
void __cdecl Manager::ServiceGameSpy()
{
    GameSpy::CdKeyThink();
    if (g_netEnabled && g_gameNetManager->InNetGame() &&
        g_queryReporting)
        GameSpy::Qr2Think(nullptr);
}

// 0x00536A40
const char* Manager::GetGameTypeName()
{
    Script::CArray* modes = Script::GetArray(kModeInfo);
    // (the original's locals, reused: a missing checksum compares the last value read - uninitialised at first)
    uint32_t type = 0;
    uint32_t mode = 0;
    for (int i = 0; i < static_cast<int>(modes->m_size); ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(modes->GetElement(i));
        GetGamePreferences()->GetPreference(kGameType)->GetChecksum(kChecksum, &type, 1);
        entry->GetChecksum(kChecksum, &mode, 0);
        if (type == mode) {
            const char* name = reinterpret_cast<const char*>(type);
            entry->GetString(kName, &name, 1);
            return name;
        }
    }
    return nullptr;
}

// 0x00536B90
void Manager::SetLevel(uint32_t level)
{
    m_level = level;
    void* peer = static_cast<LobbyMan*>(m_lobbyMan)->GetPeer();
    if (peer && GameSpy::PeerIsConnected(peer) && static_cast<LobbyMan*>(m_lobbyMan)->IsHosting())
        GameSpy::PeerStateChanged(peer); // (the peer's state changed: the GameSpy SDK)
    // (cleared just before it's tested: the reporting's state-changed call below never happens)
    g_openWaiting = false;
    if (g_netEnabled && g_gameNetManager->InNetGame() &&
        g_queryReporting && g_openWaiting)
        GameSpy::Qr2SendStateChanged(nullptr);
}

REPLACE(0x00535040, &Manager::UseNetworkPreferences);
REPLACE(0x00535AD0, &Manager::GetTeamGraffitiMode);
REPLACE(0x00535B20, &Manager::GetGoalsPreference);
REPLACE(0x00535B70, &Manager::GetGameTypePreference);
REPLACE(0x00535C10, &Manager::ConnectToInternet);
REPLACE(0x00535CB0, &Manager::DisconnectFromInternet);
REPLACE(0x00536C10, Manager::CancelConnectToInternet);
REPLACE(0x00536490, &Manager::FindServerAt);
REPLACE(0x00536640, Manager::ServiceGameSpy);
REPLACE(0x00536A40, &Manager::GetGameTypeName);
REPLACE(0x00536B90, &Manager::SetLevel);

} // namespace GameNet
