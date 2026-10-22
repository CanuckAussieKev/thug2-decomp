// The network game's script commands: capture the flag, the teams, surveyor mode, the host mode, kick / ban, cancelling
// a join (0x0053B450-0x0053D900). See gamenet_game_cfuncs.h.

#include "game/net/gamenet_game_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/gamenet_observer.h"
#include "game/net/net_app.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/mlp.h"
#include "hook/game.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (strcpy: the original's copy of the player's name)

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint8_t kMsgTookFlag = 0x71;
constexpr uint8_t kMsgCapturedFlag = 0x72;
constexpr uint8_t kMsgKickRequest = 0x61; // (a client asks the server to kick or ban)

constexpr uint32_t kPlayer = 0x67E6859A;   // player
constexpr uint32_t kFlagTeam = 0xD61CFF21; // flag_team
constexpr uint32_t kObjId = 0x0830ECAF;    // objId
constexpr uint32_t kSkaterScore = 0xD262802F; // the skater's score component

// PlayerInfo flags (+0x1D0).
constexpr uint32_t kPlayerCaptureBlocked = 0x8000;
constexpr uint32_t kPlayerMarked = 0x400; // chosen in the kick / ban menu
constexpr uint32_t kPlayerSurveying = 0x10000;

// Why a player is dropped (0x0053CB70).
constexpr int kDropBanned = 3;
constexpr int kDropKicked = 4;

Manager* NetManager() { return GameVariable<Manager*>(0x007CCE14); }
Tsk::Stack* LogicTasks()
{
    return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(GameVariable<Mlp::Manager*>(0x00696D68)) + 4);
}

// The flag message (took / captured): the player's object id and the team, to every remote player through the server;
// `local` for this machine's.
template <typename Local>
void SendFlagMessage(Manager* manager, uint8_t id, int player, int team, Local local)
{
    uint8_t msg[2] = {static_cast<uint8_t>(player), static_cast<uint8_t>(team)};
    Net::MsgDesc desc;
    desc.Construct();
    desc.m_length = sizeof(msg);
    desc.m_data = msg;
    desc.m_id = id;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    Net::App* server = manager->m_server;
    PlayerSearch search;
    for (uint8_t* other = FirstPlayer(manager, &search, true); other; other = NextPlayer(manager, &search, true)) {
        if (IsLocalPlayer(other))
            local();
        else
            server->EnqueueMessage(PlayerHandle(other), &desc);
    }
}

// Kick or ban: the first player marked for it (counting every player before it).
void KickOrBan(Manager* manager, int drop, uint8_t request)
{
    PlayerSearch search;
    int index = 0;
    uint8_t* player = FirstPlayer(manager, &search, true);
    for (; player && !(Field<uint32_t>(player, 0x1D0) & kPlayerMarked); player = NextPlayer(manager, &search, true))
        ++index;
    if (!player)
        return;
    if (manager->OnServer()) {
        Obj::CObject* skater = PlayerSkater(player);
        bool observer = IsObserver(player);
        manager->DropPlayer(player, drop);
        if (!observer)
            Call(0x00565790, Skate(), skater); // its skater removed
        return;
    }
    struct {
        uint8_t request;
        uint8_t index;
        char name[kNameSize];
        uint8_t pad[2];
    } msg;
    Net::MsgDesc desc;
    desc.Construct();
    msg.request = request;
    msg.index = static_cast<uint8_t>(index);
    std::strcpy(msg.name, PlayerName(player));
    desc.m_data = &msg;
    desc.m_length = 2 + kNameSize;
    desc.m_id = kMsgKickRequest;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToServer(manager->m_client, &desc);
}

// A task node / input handler's scalar deleting destructor (vtable slot 2).
void DeleteNode(void* node)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(node))[2])(node, 1);
}

} // namespace

// 0x0053B450
bool __cdecl CFunc_PlayerTookFlag(Script::CStruct* params, Script::CScript*)
{
    Manager* manager = NetManager();
    int player = 0;
    int team = 0;
    params->GetInteger(kPlayer, &player, 0);
    params->GetInteger(kFlagTeam, &team, 0);
    uint8_t* taker = manager->PlayerByObjectId(static_cast<uint16_t>(player));
    if (taker)
        SendFlagMessage(manager, kMsgTookFlag, player, team, [&] { Call(0x00543F00, taker, team); });
    return true;
}

// 0x0053B5A0
bool __cdecl CFunc_PlayerCapturedFlag(Script::CStruct* params, Script::CScript*)
{
    Manager* manager = NetManager();
    int player = 0;
    params->GetInteger(kPlayer, &player, 0);
    uint8_t* capturer = manager->PlayerByObjectId(static_cast<uint16_t>(player));
    if (!capturer || (Field<uint32_t>(capturer, 0x1D0) & kPlayerCaptureBlocked))
        return true;
    int team = Call<int>(0x005433D0, capturer); // the flag it carries
    SendFlagMessage(manager, kMsgCapturedFlag, player, team, [&] {
        auto* skater = reinterpret_cast<Obj::CCompositeObject*>(PlayerSkater(capturer));
        uint8_t* score = Field<uint8_t*>(skater->GetComponent(kSkaterScore), 0x18);
        Call(0x00561A80, score, Call<int>(0x00561B40, score) + 1); // its captures
        Call(0x00543A90, capturer, team);
    });
    return true;
}

// 0x0053B710
bool __cdecl CFunc_PlayerRetrievedFlag(Script::CStruct* params, Script::CScript*)
{
    Manager* manager = NetManager();
    int player = 0;
    int team = 0;
    params->GetInteger("player", &player, 0);
    params->GetInteger("flag_team", &team, 0);
    uint8_t* retriever = manager->PlayerByObjectId(static_cast<uint16_t>(player));
    if (retriever) // (as a capture: message 0x72 and 0x00543A90 - not 0x73 / 0x00544110, the retrieve's)
        SendFlagMessage(manager, kMsgCapturedFlag, player, team, [&] { Call(0x00543A90, retriever, team); });
    return true;
}

// 0x0053B860
bool __cdecl CFunc_PlayerHasFlag(Script::CStruct* params, Script::CScript*)
{
    int player = 0;
    params->GetInteger("player", &player, 0);
    uint8_t* found = NetManager()->PlayerByObjectId(static_cast<uint16_t>(player));
    if (!found)
        return true;
    return Call<bool>(0x005433B0, found); // (flags 0x7800: a team's flag)
}

// 0x0053B8A0
bool __cdecl CFunc_GetNumPlayersOnTeam(Script::CStruct* params, Script::CScript* script)
{
    Manager* manager = NetManager();
    int team = 0;
    params->GetInteger("team", &team, 1);
    script->m_params->AddInteger("num_members", Call<int>(0x00539880, manager, team));
    return true;
}

// 0x0053B8F0
bool __cdecl CFunc_GetPlayersTeam(Script::CStruct* params, Script::CScript* script)
{
    int id = 0;
    params->GetInteger(kObjId, &id, 0);
    uint8_t* player = NetManager()->PlayerByObjectId(static_cast<uint16_t>(id));
    if (player)
        script->m_params->AddInteger("team", PlayerTeam(player));
    return true;
}

// 0x0053B940
bool __cdecl CFunc_ExitSurveyorMode(Script::CStruct*, Script::CScript*)
{
    Manager* manager = NetManager();
    uint8_t* local = LocalPlayer(manager);
    if (!local)
        return false;
    if (Field<uint32_t>(local, 0x1D0) & kPlayerSurveying) {
        manager->ObservePlayer(local);
        if (void* task = Field<void*>(local, 0x1EC))
            DeleteNode(task);
        Field<void*>(local, 0x1EC) = nullptr;
        if (manager->m_observerInputHandler)
            DeleteNode(manager->m_observerInputHandler);
        manager->m_observerInputHandler = nullptr;
        Field<uint32_t>(local, 0x1D0) &= ~kPlayerSurveying;
    }
    return true;
}

// 0x0053D400
bool __cdecl CFunc_SetHostMode(Script::CStruct* params, Script::CScript*)
{
    int mode = 0;
    params->GetInteger(0u, &mode, 0);
    NetManager()->SetHostMode(mode);
    return true;
}

// 0x0053D440
bool __cdecl CFunc_EnteredNetworkGame(Script::CStruct*, Script::CScript*)
{
    Manager* manager = NetManager();
    if (GameVariable<bool>(0x007CCA82)) // (on the internet: the server's GameSpy reporting)
        GameFunction<void(__cdecl*)(bool)>(0x0053BDC0)(false);
    Tsk::Stack* tasks = LogicTasks();
    tasks->AddTask(Field<Tsk::BaseTask*>(manager, 0xD4));
    GameVariable<bool>(0x007CCDF8) = true;
    tasks->AddTask(Field<Tsk::BaseTask*>(manager, 0xC8));
    Field<bool>(manager, 0x46) = true;
    return true;
}

// 0x0053D490
bool __cdecl CFunc_KickPlayer(Script::CStruct*, Script::CScript*)
{
    KickOrBan(NetManager(), kDropKicked, 0);
    return true;
}

// 0x0053D5E0
bool __cdecl CFunc_BanPlayer(Script::CStruct*, Script::CScript*)
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_networkHeap);
    KickOrBan(NetManager(), kDropBanned, 1);
    Mem::Manager::Get()->PopContext();
    return true;
}

// 0x0053D760
bool __cdecl CFunc_CancelJoinServer(Script::CStruct*, Script::CScript*)
{
    Manager* manager = NetManager();
    manager->CancelJoin();
    manager->m_joinState = 10;
    return true;
}

// 0x0053D780
bool __cdecl CFunc_EnterSurveyorMode(Script::CStruct*, Script::CScript*)
{
    Tsk::Stack* tasks = LogicTasks();
    Manager* manager = NetManager();
    uint8_t* input = GameVariable<uint8_t*>(0x00696D40); // the input manager
    uint8_t* local = LocalPlayer(manager);
    if (IsObserver(local) || (Field<uint32_t>(local, 0x1D0) & kPlayerSurveying))
        return false;
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_networkHeap);
    AddObserverControls(manager, local, tasks, input);
    Field<uint32_t>(local, 0x1D0) |= kPlayerSurveying;
    Mem::Manager::Get()->PopContext();
    return true;
}

// 0x0053EA20
bool __cdecl CFunc_DropPendingPlayers(Script::CStruct*, Script::CScript*)
{
    NetManager()->DropPlayersNotReady();
    return true;
}

} // namespace GameNet

REPLACE(0x0053B450, GameNet::CFunc_PlayerTookFlag);
REPLACE(0x0053B5A0, GameNet::CFunc_PlayerCapturedFlag);
REPLACE(0x0053B710, GameNet::CFunc_PlayerRetrievedFlag);
REPLACE(0x0053B860, GameNet::CFunc_PlayerHasFlag);
REPLACE(0x0053B8A0, GameNet::CFunc_GetNumPlayersOnTeam);
REPLACE(0x0053B8F0, GameNet::CFunc_GetPlayersTeam);
REPLACE(0x0053B940, GameNet::CFunc_ExitSurveyorMode);
REPLACE(0x0053D400, GameNet::CFunc_SetHostMode);
REPLACE(0x0053D440, GameNet::CFunc_EnteredNetworkGame);
REPLACE(0x0053D490, GameNet::CFunc_KickPlayer);
REPLACE(0x0053D5E0, GameNet::CFunc_BanPlayer);
REPLACE(0x0053D760, GameNet::CFunc_CancelJoinServer);
REPLACE(0x0053D780, GameNet::CFunc_EnterSurveyorMode);
REPLACE(0x0053EA20, GameNet::CFunc_DropPendingPlayers);
