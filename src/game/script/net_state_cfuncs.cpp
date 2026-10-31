// Script commands (CFunctions) of the skate game, 0x005A0880-0x005A0F00: starting, joining and leaving a server, the
// network mode and setup, going online, the controllers' vibration, observing, slow motion, player names (see
// game_cfuncs.h).

#include "game/script/game_cfuncs.h"

#include <cstdint>
#include <initializer_list>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/stubs.h"
#include "game/sys/crt.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "game/net/net_manager.h"
#include "game/net/net_metrics.h"
#include "game/skater/skater.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/skate/skate_net.h"
#include "game/sys/game_clock.h"
#include "game/sys/mlp.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"
#include "game/ass/assman.h"

namespace ScriptCommands {

char g_networkId[16];
char g_networkPassword[12];
// The hosted game's name: "Beenox" unless -HOSTNAME gives another (winmain.cpp).
char g_hostedGameName[16]{"Beenox"};

namespace {

constexpr uint32_t kJoinScript = 0xFF5C39B6;        // (names unknown) JoinServer's scripts: before joining,
constexpr uint32_t kJoinedNetGameScript = 0xFF898BE6; // and after each client of a network game
constexpr uint32_t kNetSetupFailedScript = 0x6D51F203; // (name unknown) TestNetSetup's when the setup failed
constexpr uint32_t kNetSetupError = 0xC4745838;     // (name unknown) its error text
constexpr uint32_t kOnlineScript = 0x90FF204D;      // (names unknown) ConnectToInternet's scripts
constexpr uint32_t kFailScript = 0xDE64FC3E;
constexpr uint32_t kCallback = 0x86068BD9;          // "callback"
constexpr uint32_t kSlomoComponent = 0x5BD63B29;    // (name unknown) the local skater's component SetSlomo tells
constexpr uint32_t kPlayerNamesOffScript = 0x271D435C; // (name unknown)
constexpr uint32_t kSkaterType = 0xBAF9B341;        // (name unknown) the skaters' object type

uint8_t* Skate() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); }
GameModules::SkateNet* SkateNet() { return reinterpret_cast<GameModules::SkateNet*>(Skate()); }
GameObjects::CGameEntity* GetHomeRider()
{
    return reinterpret_cast<GameModules::Skate*>(Skate())->GetHomeRider();
}
OnlinePlay::Hub* GameNetManager() { return OnlinePlay::g_gameNetManager; }
NetLink::Hub* NetManager() { return NetLink::g_netManager; }
MemSys::Hub* MemManager() { return MemSys::Hub::Pull(); }
// The game mode's player count (0x00553190: its integer 0x6DC3D6F1).
uint32_t GameModePlayers()
{
    return static_cast<uint32_t>(GoalSystem::g_gameMode->GetStartingRiderCount());
}

// inet_addr through the game's import (0x005BEAAE).
uint32_t InetAddr(const char* address) { return Crt::InetAddr(address); }

} // namespace

// 0x005A0880: LaunchHost (in the bottom-up heap): the skate module's server; the network manager's LAN mode asked
// when it isn't on the internet (the answer unused).
bool __cdecl CFunc_StartServer(Scripts::CStruct*, Scripts::CScript*)
{
    MemSys::Hub* memory = MemManager();
    OnlinePlay::Hub* gameNet = GameNetManager();
    memory->EnterHeapScope(reinterpret_cast<MemSys::MemSource*>(memory->m_bottomUpHeap));
    SkateNet()->LaunchHost();
    if (!gameNet->InInternetMode())
        gameNet->IsLocalNetworkPlay();
    MemManager()->LeaveHeapScope();
    return true;
}

// 0x005A08D0: LeaveServer (0x0056B890).
bool __cdecl CFunc_LeaveServer(Scripts::CStruct*, Scripts::CScript*)
{
    reinterpret_cast<GameModules::Skate*>(Skate())->LeaveServer();
    return true;
}

// 0x005A08E0: JoinServer ["<ip>" <port>] (in the bottom-up heap): the script 0xFF5C39B6 and the pending symbols
// first. On the server, a local client per player of the game mode, each joined and given the skaters' handlers
// (in a network game, 0xFF898BE6 run after each); else a client joining the address given (both needed).
bool __cdecl CFunc_JoinServer(Scripts::CStruct* params, Scripts::CScript*)
{
    MemSys::Hub* memory = MemManager();
    OnlinePlay::Hub* gameNet = GameNetManager();
    memory->EnterHeapScope(reinterpret_cast<MemSys::MemSource*>(memory->m_bottomUpHeap));
    Scripts::LaunchScript(kJoinScript, nullptr, nullptr, false, nullptr);
    Scripts::ProcessPendingSymbols();
    if (gameNet->OnServer()) {
        uint32_t players = GameModePlayers();
        for (uint32_t i = 0; i < players; ++i) {
            NetLink::AppHost* client = gameNet->StartClientSession(false, true, true, static_cast<int>(i));
            gameNet->JoinServer(0, 0, static_cast<int>(i));
            SkateNet()->AddClientHandlers(client, static_cast<int>(i));
            if (gameNet->InNetGame())
                Scripts::LaunchScript(kJoinedNetGameScript, nullptr, nullptr, false, nullptr);
        }
    } else {
        const char* address = nullptr;
        int port = 0;
        params->FetchText(0u, &address, false);
        params->FetchIntValue(0u, &port, false);
        if (port && address) {
            gameNet->StartClientSession(false, false, true, 0);
            gameNet->JoinServer(InetAddr(address), static_cast<uint16_t>(port), 0);
        }
    }
    MemManager()->LeaveHeapScope();
    return true;
}

// 0x005A0A00: SetNetworkMode <0 | 1 | 2> (other values ignored).
bool __cdecl CFunc_SetNetworkMode(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    int mode = 0;
    params->FetchIntValue(0u, &mode, 0);
    if (mode == 0 || mode == 1 || mode == 2)
        gameNet->SetNetworkMode(mode);
    return true;
}

// 0x005A0A60: ProfileTasks [frames (1)]: the main loop's task profile over that many frames.
bool __cdecl CFunc_ProfileTasks(Scripts::CStruct* params, Scripts::CScript*)
{
    auto* mainLoop = AssetStore::g_mainLoopManager;
    int frames = 1;
    params->FetchIntValue(0u, &frames, 0);
    DebugOut::Printf("\n\nProfiling task for %d frames:\n", frames);
    mainLoop->ProfileFrames(frames);
    return true;
}

// 0x005A0AB0: UseNetworkPreferences.
bool __cdecl CFunc_UseNetworkPreferences(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->UseNetworkPreferences();
    return true;
}

// 0x005A0AC0: TestNetSetup success_script=<script>: whether the network is set up (on PC always: 0x005B9760).
// Set up: the script run with these parameters; not: the error's text (by the net library's last error) added as
// 0xC4745838 and the script 0x6D51F203 run with them.
bool __cdecl CFunc_TestNetSetup(Scripts::CStruct* params, Scripts::CScript*)
{
    NetLink::Hub* net = NetManager();
    bool ok = Stubs::ReturnTrue(); // (the net manager's setup test, a shared stub)
    if (ok) {
        uint32_t script = 0; // (unset in the original when missing - an asserted parameter)
        params->ReadHashValue("success_script", &script, true); // "success_script"
        Scripts::LaunchScript(script, params, nullptr, false, nullptr);
        return ok;
    }
    const char* text;
    if (net->GetLastError() == 5)
        text = "net_error_not_connected";
    else if (net->GetLastError() == 4)
        text = "net_error_device_error";
    else if (net->GetLastError() == 2)
        text = "net_error_not_detected";
    else if (net->GetLastError() == 6)
        text = "net_error_dhcp_error";
    else if (net->GetLastError() == 7)
        text = "net_error_changed_device";
    else if (net->GetLastError() == 3)
        text = "net_error_invalid_irx";
    else
        text = "net_error_general_error";
    params->AppendWord(kNetSetupError, Scripts::QBVAL_STRING, Scripts::FetchText(text));
    Scripts::LaunchScript(kNetSetupFailedScript, params, nullptr, false, nullptr);
    return ok;
}

// 0x005A0BD0 / 0x005A0BE0: NeedToTestNetSetup (no, on PC) / CanChangeDevices (yes): shared stubs.
bool __cdecl CFunc_NeedToTestNetSetup(Scripts::CStruct*, Scripts::CScript*) { return Stubs::ReturnFalse(); }
bool __cdecl CFunc_CanChangeDevices(Scripts::CStruct*, Scripts::CScript*) { return Stubs::ReturnTrue(); }

// 0x005A0BF0: LoadNetworkDrivers: with the global string use_deci2 set, the globals viewer_ip, viewer_subnet and
// viewer_gateway; else none. The net manager's answer.
bool __cdecl CFunc_LoadNetworkDrivers(Scripts::CStruct*, Scripts::CScript*)
{
    NetLink::Hub* net = NetManager();
    const char* use = Scripts::FetchText("use_deci2");
    if (!use || !*use)
        return net->LoadNetworkDrivers(nullptr, nullptr, nullptr);
    const char* ip = Scripts::FetchText("viewer_ip");
    const char* gateway = Scripts::FetchText("viewer_gateway");
    const char* subnetMask = Scripts::FetchText("viewer_subnet");
    return net->LoadNetworkDrivers(ip, subnetMask, gateway);
}

// 0x005A0C60: ConnectToInternet 0x90FF204D=<script> 0xDE64FC3E=<script>: the network manager's answer.
bool __cdecl CFunc_ConnectToInternet(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    uint32_t onlineScript = 0;
    uint32_t failScript = 0;
    params->ReadHashValue(kOnlineScript, &onlineScript, 0);
    params->ReadHashValue(kFailScript, &failScript, 0);
    return gameNet->ConnectToInternet(onlineScript, failScript);
}

// 0x005A0CC0: CancelConnectToInternet.
bool __cdecl CFunc_CancelConnectToInternet(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub::CancelConnectToInternet();
    return true;
}

// 0x005A0CD0: IsOnline.
bool __cdecl CFunc_IsOnline(Scripts::CStruct*, Scripts::CScript*) { return NetManager()->IsOnline(); }

// 0x005A0CE0: DisconnectFromInternet [callback=<script>].
bool __cdecl CFunc_DisconnectFromInternet(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    uint32_t callback = 0;
    params->ReadHashValue(kCallback, &callback, 0);
    gameNet->DisconnectFromInternet(callback);
    return true;
}

// 0x005A0D20: EnableActuators [0 | 1 (1)]: the vibration of each of the two controllers the skate module uses
// (its +0x394 entries, 8 bytes apart) on or off.
bool __cdecl CFunc_EnableActuators(Scripts::CStruct* params, Scripts::CScript*)
{
    int enable = 1;
    params->FetchIntValue(0u, &enable, 0);
    auto* input = PadInput::g_inputManager;
    for (int i = 0; i < 2; ++i) {
        if (Skate()[0x394 + 8 * i]) {
            if (enable)
                input->EnableActuators(i);
            else
                input->StopRumble(i);
        }
    }
    return true;
}

// 0x005A0D80: InNetGame.
bool __cdecl CFunc_InNetGame(Scripts::CStruct*, Scripts::CScript*) { return GameNetManager()->InNetGame(); }

// 0x005A0D90: IsObserving (4 callers): the local player's an observer; with none, a joining player that is local
// and observing (flags 1 and 4), else whether the join mode is observing (1).
bool __cdecl CFunc_IsObserving(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    OnlinePlay::Detail::NewPlayerSearch search;
    if (void* player = gameNet->GetOwnPlayer())
        return static_cast<OnlinePlay::PlayerRecord*>(player)->IsObserver() != 0;
    for (auto* player = static_cast<uint8_t*>(gameNet->BeginJoinerWalk(&search)); player;
         player = static_cast<uint8_t*>(gameNet->StepJoinerWalk(&search))) {
        uint32_t flags = *reinterpret_cast<uint32_t*>(player + 0x20);
        if ((flags & 1) && (flags & 4))
            return true;
    }
    return gameNet->GetEntryMode() == 1;
}

// 0x005A0E50: SetSlomo [speed (1)]: the game clock's speed, and the local skater's component 0x5BD63B29's (+0x2E8).
bool __cdecl CFunc_SetSlomo(Scripts::CStruct* params, Scripts::CScript*)
{
    float speed = 1.0f;
    params->FetchFloat(0u, &speed, 0);
    FrameClock::SetGameClockSpeed(speed);
    if (GetHomeRider()) {
        if (GameObjects::CObjectModule* component = GetHomeRider()->FetchModule(kSlomoComponent))
            *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(component) + 0x2E8) = speed;
    }
    return true;
}

// 0x005A0EB0: TogglePlayerNames: toggled; turned off, the script 0x271D435C runs.
bool __cdecl CFunc_TogglePlayerNames(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    gameNet->TogglePlayerNames();
    if (!gameNet->WantsNameTags())
        Scripts::LaunchScript(kPlayerNamesOffScript, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x005A0EF0: SetCurrentGameType (the skate module's 0x00564FD0).
bool __cdecl CFunc_SetCurrentGameType(Scripts::CStruct*, Scripts::CScript*)
{
    reinterpret_cast<GameModules::Skate*>(Skate())->SetCurrentGameType();
    return true;
}

namespace {
// DumpNetMessageStats' output - compiled out of this build (only the metrics' getters are still called).
inline void PrintMessageStat(int /*id*/, int /*value*/) {}
} // namespace

// 0x005A0F00: DumpNetMessageStats: for each connection of the server (or else client 0), each message id's received
// and sent totals read - twice each (the printing is compiled out). Always true.
bool __cdecl CFunc_DumpNetMessageStats(Scripts::CStruct*, Scripts::CScript*)
{
    NetLink::ConnSearch search = {NetLink::kConnSearchVtable, nullptr};
    OnlinePlay::Hub* gameNet = GameNetManager();
    NetLink::AppHost* app = gameNet->m_host;
    if (!app) {
        app = gameNet->FetchPatron(0);
        if (!app)
            return true;
    }
    for (NetLink::Conn* conn = app->BeginConnWalk(&search); conn; conn = app->StepConnWalk(&search)) {
        for (NetLink::Gauges* metrics : {&conn->m_receiveMetrics, &conn->m_sendMetrics}) {
            for (int pass = 0; pass < 2; ++pass) {
                for (int id = 0; id < 256; ++id) {
                    PrintMessageStat(id, metrics->GetMsgByteTotal(id));
                    PrintMessageStat(id, metrics->GetMsgCountForType(id));
                }
            }
        }
    }
    return true;
}

// 0x005A1020: SetServerMode [off]: the server's side on (off without the flag "off").
bool __cdecl CFunc_SetServerMode(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    gameNet->SetServerMode(!params->HasBareFlag("off")); // "off"
    return true;
}

// 0x005A1050: NotifyBailDone: run on a local skater, its client tells the server (message 0x45, sequenced, group 8).
bool __cdecl CFunc_NotifyBailDone(Scripts::CStruct*, Scripts::CScript* script)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    GameObjects::CEntity* object = script ? script->m_entity.Pull() : nullptr;
    if (!object || object->m_kindTag != kSkaterType)
        return true;
    auto* skater = static_cast<GameObjects::CRider*>(object);
    if (!skater->m_unknownBC)
        return true;
    if (NetLink::AppHost* client = gameNet->FetchPatron(skater->m_viewSlot)) {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        desc.m_ident = 0x45;
        desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
        desc.m_groupId = 8;
        static_cast<NetLink::Guest*>(client)->SendToHost(&desc);
    }
    return true;
}

// 0x005A10D0: EnterObserverMode: the observe request sent.
bool __cdecl CFunc_EnterObserverMode(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->SendObserveRequest();
    return true;
}

// 0x005A10E0: ObserveNextSkater (the skate module's 0x005651D0).
bool __cdecl CFunc_ObserveNextSkater(Scripts::CStruct*, Scripts::CScript*)
{
    GameModules::Skate::ObserveNextSkater();
    return true;
}

// 0x005A10F0: RefreshServerList [force_refresh].
bool __cdecl CFunc_RefreshServerList(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    gameNet->RefreshServerList(params->HasBareFlag("force_refresh")); // "force_refresh"
    return true;
}

// 0x005A1120: StartServerList.
bool __cdecl CFunc_StartServerList(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->StartServerList();
    return true;
}

// 0x005A1130: StartLobbyList: the service state 13 (the lobby list) - after the current one (state 1 next) when
// the service is in state 2.
bool __cdecl CFunc_StartLobbyList(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    if (gameNet->GetServiceState() == 2) {
        gameNet->SetNextServiceState(13);
        gameNet->SetServiceState(1);
    } else {
        gameNet->SetServiceState(13);
    }
    return true;
}

// 0x005A1160 / 0x005A1170: StopServerList / FreeServerList.
bool __cdecl CFunc_StopServerList(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->StopServerList();
    return true;
}

bool __cdecl CFunc_FreeServerList(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->EmptyServerListing();
    return true;
}

// 0x005A1180: InSplitScreenGame (12 callers): a multiplayer game (the skate module's 0x005651F0) that isn't a network
// game. 1 or 0 in EAX.
int __cdecl CFunc_InSplitScreenGame(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    if (GameModules::Skate::IsMultiplayer() != 0 && !gameNet->InNetGame())
        return 1;
    return 0;
}

// 0x005A11B0: InMultiplayerGame (5 callers): the skate module's 0x005651F0 (a tail jump: its EAX).
int __cdecl CFunc_InMultiplayerGame(Scripts::CStruct*, Scripts::CScript*) { return GameModules::Skate::IsMultiplayer(); }

// 0x005A1220: OnServer.
bool __cdecl CFunc_OnServer(Scripts::CStruct*, Scripts::CScript*) { return GameNetManager()->OnServer(); }

// 0x005A12D0: ToggleScores.
bool __cdecl CFunc_ToggleScores(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->ToggleScores();
    return true;
}

// 0x005A1680: JoinWithPassword 0x61414D56="<password>": the join tried again with it.
bool __cdecl CFunc_JoinWithPassword(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    const char* password = nullptr; // (unset in the original when missing - an asserted parameter)
    params->FetchText(0x61414D56u, &password, true);
    gameNet->RetryEntryWithKey(password);
    return true;
}

// 0x005A1820 / 0x005A1830: LoadPendingPlayers / LaunchQueuedScripts.
bool __cdecl CFunc_LoadPendingPlayers(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->ReAddPendingPlayers();
    return true;
}

bool __cdecl CFunc_LaunchQueuedScripts(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->LaunchQueuedScripts();
    return true;
}

// 0x005A1AC0: StartNetworkLobby: back to the lobby.
bool __cdecl CFunc_StartNetworkLobby(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->ResetToLobby();
    return true;
}

// 0x005A1AD0: ObserversAllowed: unless the connection is a modem (type 1).
bool __cdecl CFunc_ObserversAllowed(Scripts::CStruct*, Scripts::CScript*) { return NetManager()->GetNetLinkKind() != 1; }

// 0x005A1AF0: NumPlayersAllowed <num_n>: over a modem, every count but 0x1FA67C3D; otherwise all.
bool __cdecl CFunc_NumPlayersAllowed(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t players = 0;
    if (NetManager()->GetNetLinkKind() != 1)
        return true;
    params->ReadHashValue(0u, &players, 0);
    return players != 0x1FA67C3D;
}

namespace {
// A network preference's checksum (asserting it's there).
uint32_t NetworkPreference(uint32_t name)
{
    Scripts::CStruct* preference = GameNetManager()->GetOnlineSettings()->FetchSetting(name);
    uint32_t value = 0; // (unset in the original when missing - an asserted parameter)
    preference->ReadHashValue(0x21902065u, &value, true); // "checksum"
    return value;
}
} // namespace

// 0x005A1B30 / 0x005A1B70 / 0x005A1BB0: AutoDNS / UsingDefaultMasterServers / UsingDHCP: the network preferences
// 0x5DDFCAD3 / 0xE7A3CB08 / 0xBB2EB924 are 0xF81BC89B / 0xF81BC89B / 0x2189CFF8.
bool __cdecl CFunc_AutoDNS(Scripts::CStruct*, Scripts::CScript*) { return NetworkPreference(0x5DDFCAD3) == 0xF81BC89B; }
bool __cdecl CFunc_UsingDefaultMasterServers(Scripts::CStruct*, Scripts::CScript*)
{
    return NetworkPreference(0xE7A3CB08) == 0xF81BC89B;
}
bool __cdecl CFunc_UsingDHCP(Scripts::CStruct*, Scripts::CScript*) { return NetworkPreference(0xBB2EB924) == 0x2189CFF8; }

// 0x005A1BF0: InInternetMode.
bool __cdecl CFunc_InInternetMode(Scripts::CStruct*, Scripts::CScript*) { return GameNetManager()->InInternetMode(); }

// 0x005A1C00: EnteringNetGame: a network game, and the skate module's +0x37C is 0xE92ECAFE.
bool __cdecl CFunc_EnteringNetGame(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    return gameNet->InNetGame() && *reinterpret_cast<uint32_t*>(Skate() + 0x37C) == 0xE92ECAFE;
}

// 0x005A1C30: DeviceChosen: the network preference 0xFA187DEC isn't 0x09562A0A.
bool __cdecl CFunc_DeviceChosen(Scripts::CStruct*, Scripts::CScript*) { return NetworkPreference(0xFA187DEC) != 0x09562A0A; }

// 0x005A1C70: GameIsOver.
bool __cdecl CFunc_GameIsOver(Scripts::CStruct*, Scripts::CScript*) { return GameNetManager()->IsGameOver(); }

namespace {
bool& TryingToJoin() { return g_tryingToJoin; }
// gethostbyname through the game's import (0x005BEABA).
void* GetHostByName(const char* name) { return Crt::GetHostByName(name); }
} // namespace

// 0x005A28A0: TryJoinInternetGame: the server chosen in the list (0x0067EFB8, unless -1) looked for; a join is being
// tried from now on.
bool __cdecl CFunc_TryJoinInternetGame(Scripts::CStruct*, Scripts::CScript*)
{
    uint32_t server = OnlinePlay::g_commandLineServer;
    if (server != 0xFFFFFFFF)
        GameNetManager()->FindServerAt(server);
    TryingToJoin() = true;
    DebugOut::Printf("ScriptTryJoinInternetGame\n"); // "ScriptTryJoinInternetGame\n"
    return true;
}

// 0x005A28D0: TryJoinServerIP string="<address or host name>": the address (inet_addr, else the host's first
// address) looked for and a join tried; nothing if neither gives one.
bool __cdecl CFunc_TryJoinServerIP(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* address = nullptr; // (unset in the original when missing - an asserted parameter)
    params->FetchText("string", &address, true); // "string"
    uint32_t ip = InetAddr(address);
    if (ip == 0xFFFFFFFF) {
        auto* host = static_cast<uint8_t*>(GetHostByName(address));
        ip = 0xFFFFFFFF;
        if (host) {
            const uint8_t* first = **reinterpret_cast<uint8_t***>(host + 0xC); // h_addr_list[0]
            ip = first[0] | first[1] << 8 | first[2] << 16 | static_cast<uint32_t>(first[3]) << 24;
        }
    }
    if (ip != 0xFFFFFFFF) {
        TryingToJoin() = true;
        GameNetManager()->FindServerAt(ip);
    }
    DebugOut::Printf("ScriptTryJoinServerIP\n"); // "ScriptTryJoinServerIP\n"
    return true;
}

// 0x005A2960: TryJoinServerIPCancel.
bool __cdecl CFunc_TryJoinServerIPCancel(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->FindServerAt(0);
    TryingToJoin() = false;
    DebugOut::Printf("ScriptTryJoinServerIPCancel\n"); // "ScriptTryJoinServerIPCancel\n"
    return true;
}

// 0x005A2990: ForceServerListRefresh: the servers found so far shown.
bool __cdecl CFunc_ForceServerListRefresh(Scripts::CStruct*, Scripts::CScript*)
{
    GameNetManager()->ShowFoundServers();
    return true;
}

namespace {

// A new, empty script structure (from the structures' pool) and its deletion.
Scripts::CStruct* NewPreference()
{
    auto* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
void DeletePreference(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
uint32_t Checksums(const char* name) { return Checksums::HashFromText(name); }
// The network preference `name` set to {ui_string="<text>"}.
void SetStringPreference(OptionStore::Preferences* prefs, const char* name, const char* text)
{
    Scripts::CStruct* preference = NewPreference();
    preference->AppendWord(Checksums("ui_string"), Scripts::QBVAL_STRING, text); // "ui_string"
    prefs->ChangeSetting(Checksums(name), preference);
    DeletePreference(preference);
}
// ... set to {ui_string="<text>" checksum=<the checksum of `value`>}.
void SetChoicePreference(Scripts::CStruct* preference, const char* text, const char* value)
{
    preference->AppendWord("ui_string", reinterpret_cast<const char*>(text)); // "ui_string"
    preference->PutNameKey("checksum", Checksums(value));                        // "checksum"
}

} // namespace

// 0x005A4900: the network preferences set from the host settings (PartyMod's / the Beenox port's globals): the
// server name "Beenox", the password (0x007CCBD4), the network id (0x007CCBC4, if any), the player count (0x0067EFD0),
// the team mode (0x007CCBE0: 0 none, 2 / 3 / 4 teams; others leave it empty) and the skill level (0x0067EFD4, 1-5).
void __cdecl InitBeenoxPreferences()
{
    OnlinePlay::Hub* gameNet = GameNetManager();
    OptionStore::Preferences* prefs = gameNet->GetOnlineSettings();
    SetStringPreference(prefs, "server_name", g_hostedGameName); // "server_name"
    SetStringPreference(prefs, "password", g_networkPassword);           // "password"
    if (g_networkId[0] != '\0')
        SetStringPreference(prefs, "network_id", g_networkId); // "network_id"
    gameNet->SetMaxPlayersPreference(OnlinePlay::g_netMaxPlayers);
    Scripts::CStruct* teams = NewPreference();
    switch (OnlinePlay::g_networkTeamMode) {
    case 0: SetChoicePreference(teams, "None", "teams_none"); break; // "None", teams_none
    case 2: SetChoicePreference(teams, "2", "teams_two"); break; // "2", teams_two
    case 3: SetChoicePreference(teams, "3", "teams_three"); break; // "3", teams_three
    case 4: SetChoicePreference(teams, "4", "teams_four"); break; // "4", teams_four
    default: break;
    }
    prefs->ChangeSetting(Checksums("team_mode"), teams); // "team_mode"
    DeletePreference(teams);
    Scripts::CStruct* skill = NewPreference();
    switch (OnlinePlay::g_netSkillLevel) {
    case 1: SetChoicePreference(skill, "1: Baby Steps", "num_1"); break; // "1: Baby Steps", num_1
    case 2: SetChoicePreference(skill, "2: I Can Crawl!", "num_2"); break; // "2: I Can Crawl!", num_2
    case 3: SetChoicePreference(skill, "3: Hold My Own", "num_3"); break; // "3: Hold My Own", num_3
    case 4: SetChoicePreference(skill, "4: Let's Do This", "num_4"); break; // "4: Let's Do This", num_4
    case 5: SetChoicePreference(skill, "5: Bring It On!", "num_5"); break; // "5: Bring It On!", num_5
    default: break;
    }
    prefs->ChangeSetting(Checksums("skill_level"), skill); // "skill_level"
    DeletePreference(skill);
}

// 0x005A4D90: InitPrefsBeenox: InitBeenoxPreferences. True.
bool __cdecl CFunc_InitPrefsBeenox(Scripts::CStruct*, Scripts::CScript*)
{
    InitBeenoxPreferences();
    return true;
}

} // namespace ScriptCommands
