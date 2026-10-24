// Script commands (CFunctions) of the skate game, 0x005A0880-0x005A0F00: starting, joining and leaving a server, the
// network mode and setup, going online, the controllers' vibration, observing, slow motion, player names (see
// game_cfuncs.h).

#include "game/script/game_cfuncs.h"

#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/stubs.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/skate/skate_net.h"
#include "game/sys/game_clock.h"
#include "game/sys/mlp.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

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

uint8_t* Skate() { return GameVariable<uint8_t*>(0x007CE478); }
template <typename R = void, typename... A>
R CallSkate(uintptr_t address, A... args)
{
    return GameFunction<R(__thiscall*)(uint8_t*, A...)>(address)(Skate(), args...);
}
Mdl::SkateNet* SkateNet() { return reinterpret_cast<Mdl::SkateNet*>(Skate()); }
Obj::CCompositeObject* GetLocalSkater()
{
    return GameFunction<Obj::CCompositeObject*(__thiscall*)(uint8_t*)>(0x00566520)(Skate());
}
GameNet::Manager* GameNetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
Net::Manager* NetManager() { return GameVariable<Net::Manager*>(0x006EFDD8); }
Mem::Manager* MemManager() { return GameVariable<Mem::Manager*>(0x007D1800); }
// The game mode's player count (0x00553190: its integer 0x6DC3D6F1).
uint32_t GameModePlayers() { return GameFunction<uint32_t(__thiscall*)(void*)>(0x00553190)(GameVariable<void*>(0x007CE470)); }

// inet_addr through the game's import (0x005BEAAE).
uint32_t InetAddr(const char* address) { return GameFunction<uint32_t(__stdcall*)(const char*)>(0x005BEAAE)(address); }

} // namespace

// 0x005A0880: StartServer (in the bottom-up heap): the skate module's server; the network manager's LAN mode asked
// when it isn't on the internet (the answer unused).
bool __cdecl CFunc_StartServer(Script::CStruct*, Script::CScript*)
{
    Mem::Manager* memory = MemManager();
    GameNet::Manager* gameNet = GameNetManager();
    memory->PushContext(reinterpret_cast<Mem::Allocator*>(memory->m_bottomUpHeap));
    SkateNet()->StartServer();
    if (!gameNet->InInternetMode())
        gameNet->InLanMode();
    MemManager()->PopContext();
    return true;
}

// 0x005A08D0: LeaveServer (0x0056B890).
bool __cdecl CFunc_LeaveServer(Script::CStruct*, Script::CScript*)
{
    CallSkate(0x0056B890);
    return true;
}

// 0x005A08E0: JoinServer ["<ip>" <port>] (in the bottom-up heap): the script 0xFF5C39B6 and the pending symbols
// first. On the server, a local client per player of the game mode, each joined and given the skaters' handlers
// (in a network game, 0xFF898BE6 run after each); else a client joining the address given (both needed).
bool __cdecl CFunc_JoinServer(Script::CStruct* params, Script::CScript*)
{
    Mem::Manager* memory = MemManager();
    GameNet::Manager* gameNet = GameNetManager();
    memory->PushContext(reinterpret_cast<Mem::Allocator*>(memory->m_bottomUpHeap));
    Script::RunScript(kJoinScript, nullptr, nullptr, false, nullptr);
    Script::ProcessPendingSymbols();
    if (gameNet->OnServer()) {
        uint32_t players = GameModePlayers();
        for (uint32_t i = 0; i < players; ++i) {
            Net::App* client = gameNet->SpawnClient(false, true, true, static_cast<int>(i));
            gameNet->JoinServer(0, 0, static_cast<int>(i));
            SkateNet()->AddClientHandlers(client, static_cast<int>(i));
            if (gameNet->InNetGame())
                Script::RunScript(kJoinedNetGameScript, nullptr, nullptr, false, nullptr);
        }
    } else {
        const char* address = nullptr;
        int port = 0;
        params->GetString(0u, &address, false);
        params->GetInteger(0u, &port, false);
        if (port && address) {
            gameNet->SpawnClient(false, false, true, 0);
            gameNet->JoinServer(InetAddr(address), static_cast<uint16_t>(port), 0);
        }
    }
    MemManager()->PopContext();
    return true;
}

// 0x005A0A00: SetNetworkMode <0 | 1 | 2> (other values ignored).
bool __cdecl CFunc_SetNetworkMode(Script::CStruct* params, Script::CScript*)
{
    GameNet::Manager* gameNet = GameNetManager();
    int mode = 0;
    params->GetInteger(0u, &mode, 0);
    if (mode == 0 || mode == 1 || mode == 2)
        gameNet->SetNetworkMode(mode);
    return true;
}

// 0x005A0A60: ProfileTasks [frames (1)]: the main loop's task profile over that many frames.
bool __cdecl CFunc_ProfileTasks(Script::CStruct* params, Script::CScript*)
{
    auto* mainLoop = GameVariable<Mlp::Manager*>(0x00696D68);
    int frames = 1;
    params->GetInteger(0u, &frames, 0);
    Dbg::Printf(reinterpret_cast<const char*>(0x0064FD00), frames);
    mainLoop->ProfileFrames(frames);
    return true;
}

// 0x005A0AB0: UseNetworkPreferences.
bool __cdecl CFunc_UseNetworkPreferences(Script::CStruct*, Script::CScript*)
{
    GameNetManager()->UseNetworkPreferences();
    return true;
}

// 0x005A0AC0: TestNetSetup success_script=<script>: whether the network is set up (on PC always: 0x005B9760).
// Set up: the script run with these parameters; not: the error's text (by the net library's last error) added as
// 0xC4745838 and the script 0x6D51F203 run with them.
bool __cdecl CFunc_TestNetSetup(Script::CStruct* params, Script::CScript*)
{
    Net::Manager* net = NetManager();
    bool ok = Stubs::ReturnTrue(); // (the net manager's setup test, a shared stub)
    if (ok) {
        uint32_t script = 0; // (unset in the original when missing - an asserted parameter)
        params->GetChecksum(reinterpret_cast<const char*>(0x0064FD24), &script, true); // "success_script"
        Script::RunScript(script, params, nullptr, false, nullptr);
        return ok;
    }
    uintptr_t text;
    if (net->GetLastError() == 5)
        text = 0x0064FDC8;
    else if (net->GetLastError() == 4)
        text = 0x0064FDB0;
    else if (net->GetLastError() == 2)
        text = 0x0064FD98;
    else if (net->GetLastError() == 6)
        text = 0x0064FD80;
    else if (net->GetLastError() == 7)
        text = 0x0064FD64;
    else if (net->GetLastError() == 3)
        text = 0x0064FD4C;
    else
        text = 0x0064FD34;
    params->AddString(kNetSetupError, Script::ESYMBOLTYPE_STRING, Script::GetString(reinterpret_cast<const char*>(text)));
    Script::RunScript(kNetSetupFailedScript, params, nullptr, false, nullptr);
    return ok;
}

// 0x005A0BD0 / 0x005A0BE0: NeedToTestNetSetup (no, on PC) / CanChangeDevices (yes): shared stubs.
bool __cdecl CFunc_NeedToTestNetSetup(Script::CStruct*, Script::CScript*) { return Stubs::ReturnFalse(); }
bool __cdecl CFunc_CanChangeDevices(Script::CStruct*, Script::CScript*) { return Stubs::ReturnTrue(); }

// 0x005A0BF0: LoadNetworkDrivers: with the global string use_deci2 set, the globals viewer_ip, viewer_subnet and
// viewer_gateway; else none. The net manager's answer.
bool __cdecl CFunc_LoadNetworkDrivers(Script::CStruct*, Script::CScript*)
{
    Net::Manager* net = NetManager();
    const char* use = Script::GetString(reinterpret_cast<const char*>(0x0064FE0C));
    if (!use || !*use)
        return net->LoadNetworkDrivers(nullptr, nullptr, nullptr);
    const char* ip = Script::GetString(reinterpret_cast<const char*>(0x0064FE00));
    const char* gateway = Script::GetString(reinterpret_cast<const char*>(0x0064FDF0));
    const char* subnetMask = Script::GetString(reinterpret_cast<const char*>(0x0064FDE0));
    return net->LoadNetworkDrivers(ip, subnetMask, gateway);
}

// 0x005A0C60: ConnectToInternet 0x90FF204D=<script> 0xDE64FC3E=<script>: the network manager's answer.
bool __cdecl CFunc_ConnectToInternet(Script::CStruct* params, Script::CScript*)
{
    GameNet::Manager* gameNet = GameNetManager();
    uint32_t onlineScript = 0;
    uint32_t failScript = 0;
    params->GetChecksum(kOnlineScript, &onlineScript, 0);
    params->GetChecksum(kFailScript, &failScript, 0);
    return gameNet->ConnectToInternet(onlineScript, failScript);
}

// 0x005A0CC0: CancelConnectToInternet.
bool __cdecl CFunc_CancelConnectToInternet(Script::CStruct*, Script::CScript*)
{
    GameNet::Manager::CancelConnectToInternet();
    return true;
}

// 0x005A0CD0: IsOnline.
bool __cdecl CFunc_IsOnline(Script::CStruct*, Script::CScript*) { return NetManager()->IsOnline(); }

// 0x005A0CE0: DisconnectFromInternet [callback=<script>].
bool __cdecl CFunc_DisconnectFromInternet(Script::CStruct* params, Script::CScript*)
{
    GameNet::Manager* gameNet = GameNetManager();
    uint32_t callback = 0;
    params->GetChecksum(kCallback, &callback, 0);
    gameNet->DisconnectFromInternet(callback);
    return true;
}

// 0x005A0D20: EnableActuators [0 | 1 (1)]: the vibration of each of the two controllers the skate module uses
// (its +0x394 entries, 8 bytes apart) on or off.
bool __cdecl CFunc_EnableActuators(Script::CStruct* params, Script::CScript*)
{
    int enable = 1;
    params->GetInteger(0u, &enable, 0);
    auto* input = GameVariable<Inp::Manager*>(0x00696D40);
    for (int i = 0; i < 2; ++i) {
        if (Skate()[0x394 + 8 * i]) {
            if (enable)
                input->EnableActuators(i);
            else
                input->DisableActuators(i);
        }
    }
    return true;
}

// 0x005A0D80: InNetGame.
bool __cdecl CFunc_InNetGame(Script::CStruct*, Script::CScript*) { return GameNetManager()->InNetGame(); }

// 0x005A0D90: IsObserving (4 callers): the local player's an observer; with none, a joining player that is local
// and observing (flags 1 and 4), else whether the join mode is observing (1).
bool __cdecl CFunc_IsObserving(Script::CStruct*, Script::CScript*)
{
    GameNet::Manager* gameNet = GameNetManager();
    GameNet::Detail::NewPlayerSearch search;
    if (void* player = gameNet->GetLocalPlayer())
        return static_cast<GameNet::PlayerInfo*>(player)->IsObserver() != 0;
    for (auto* player = static_cast<uint8_t*>(gameNet->FirstNewPlayerInfo(&search)); player;
         player = static_cast<uint8_t*>(gameNet->NextNewPlayerInfo(&search))) {
        uint32_t flags = *reinterpret_cast<uint32_t*>(player + 0x20);
        if ((flags & 1) && (flags & 4))
            return true;
    }
    return gameNet->GetJoinMode() == 1;
}

// 0x005A0E50: SetSlomo [speed (1)]: the game clock's speed, and the local skater's component 0x5BD63B29's (+0x2E8).
bool __cdecl CFunc_SetSlomo(Script::CStruct* params, Script::CScript*)
{
    float speed = 1.0f;
    params->GetFloat(0u, &speed, 0);
    Tmr::SetGameClockSpeed(speed);
    if (GetLocalSkater()) {
        if (Obj::CBaseComponent* component = GetLocalSkater()->GetComponent(kSlomoComponent))
            *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(component) + 0x2E8) = speed;
    }
    return true;
}

// 0x005A0EB0: TogglePlayerNames: toggled; turned off, the script 0x271D435C runs.
bool __cdecl CFunc_TogglePlayerNames(Script::CStruct*, Script::CScript*)
{
    GameNet::Manager* gameNet = GameNetManager();
    gameNet->TogglePlayerNames();
    if (!gameNet->ShouldDrawPlayerNames())
        Script::RunScript(kPlayerNamesOffScript, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x005A0EF0: SetCurrentGameType (the skate module's 0x00564FD0).
bool __cdecl CFunc_SetCurrentGameType(Script::CStruct*, Script::CScript*)
{
    CallSkate(0x00564FD0);
    return true;
}

} // namespace CFuncs

REPLACE(0x005A0880, CFuncs::CFunc_StartServer);
REPLACE(0x005A08D0, CFuncs::CFunc_LeaveServer);
REPLACE(0x005A08E0, CFuncs::CFunc_JoinServer);
REPLACE(0x005A0A00, CFuncs::CFunc_SetNetworkMode);
REPLACE(0x005A0A60, CFuncs::CFunc_ProfileTasks);
REPLACE(0x005A0AB0, CFuncs::CFunc_UseNetworkPreferences);
REPLACE(0x005A0AC0, CFuncs::CFunc_TestNetSetup);
REPLACE(0x005A0BD0, CFuncs::CFunc_NeedToTestNetSetup);
REPLACE(0x005A0BE0, CFuncs::CFunc_CanChangeDevices);
REPLACE(0x005A0BF0, CFuncs::CFunc_LoadNetworkDrivers);
REPLACE(0x005A0C60, CFuncs::CFunc_ConnectToInternet);
REPLACE(0x005A0CC0, CFuncs::CFunc_CancelConnectToInternet);
REPLACE(0x005A0CD0, CFuncs::CFunc_IsOnline);
REPLACE(0x005A0CE0, CFuncs::CFunc_DisconnectFromInternet);
REPLACE(0x005A0D20, CFuncs::CFunc_EnableActuators);
REPLACE(0x005A0D80, CFuncs::CFunc_InNetGame);
REPLACE(0x005A0D90, CFuncs::CFunc_IsObserving);
REPLACE(0x005A0E50, CFuncs::CFunc_SetSlomo);
REPLACE(0x005A0EB0, CFuncs::CFunc_TogglePlayerNames);
REPLACE(0x005A0EF0, CFuncs::CFunc_SetCurrentGameType);
