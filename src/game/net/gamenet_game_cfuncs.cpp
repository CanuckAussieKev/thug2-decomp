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
#include "game/net/gamenet_qr2.h"
#include "game/net/net_app.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sio/inpman.h"
#include "game/sys/mlp.h"
#include "game/core/code_address.h"
#include "game/ass/assman.h"
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

#pragma warning(disable : 4996) // (strcpy: the original's copy of the player's name)

namespace OnlinePlay {

using namespace Detail;

namespace {

constexpr uint8_t kMsgTookFlag = 0x71;
constexpr uint8_t kMsgCapturedFlag = 0x72;
constexpr uint8_t kMsgKickRequest = 0x61; // (a client asks the server to kick or ban)

constexpr uint32_t kPlayer = 0x67E6859A;   // player
constexpr uint32_t kFlagTeam = 0xD61CFF21; // flag_team
constexpr uint32_t kObjId = 0x0830ECAF;    // objId
constexpr uint32_t kSkaterScore = 0xD262802F; // the skater's score component

// PlayerRecord flags (+0x1D0).
constexpr uint32_t kPlayerCaptureBlocked = 0x8000;
constexpr uint32_t kPlayerMarked = 0x400; // chosen in the kick / ban menu
// (kPlayerSurveying, 0x10000: player_info.h)

// Why a player is dropped (0x0053CB70).
constexpr int kDropBanned = 3;
constexpr int kDropKicked = 4;

Manager* NetManager() { return g_gameNetManager; }
TaskList::Stack* LogicTasks()
{
    return reinterpret_cast<TaskList::Stack*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 4);
}

// The flag message (took / captured): the player's object id and the team, to every remote player through the server;
// `local` for this machine's.
template <typename Local>
void SendFlagMessage(Manager* manager, uint8_t id, int player, int team, Local local)
{
    uint8_t msg[2] = {static_cast<uint8_t>(player), static_cast<uint8_t>(team)};
    NetLink::NetMessageInfo desc;
    desc.Construct();
    desc.m_byteCount = sizeof(msg);
    desc.m_data = msg;
    desc.m_id = id;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    NetLink::App* server = manager->m_server;
    PlayerSearch search;
    for (uint8_t* other = FirstPlayer(manager, &search, true); other; other = NextPlayer(manager, &search, true)) {
        if (IsOwnPlayer(other))
            local();
        else
            server->PutOnOutbox(PlayerHandle(other), &desc);
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
        GameObjects::CObject* skater = PlayerSkater(player);
        bool observer = IsObserver(player);
        manager->EvictPlayer(player, drop);
        if (!observer)
            Call(CodeAddress(&GameModules::Skate::RemoveSkater), Skate(), skater); // its skater removed
        return;
    }
    struct {
        uint8_t request;
        uint8_t index;
        char name[kNameSize];
        uint8_t pad[2];
    } msg;
    NetLink::NetMessageInfo desc;
    desc.Construct();
    msg.request = request;
    msg.index = static_cast<uint8_t>(index);
    std::strcpy(msg.name, PlayerName(player));
    desc.m_data = &msg;
    desc.m_byteCount = 2 + kNameSize;
    desc.m_id = kMsgKickRequest;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToServer(manager->m_clientApp, &desc);
}

// A task node / input handler's scalar deleting destructor (vtable slot 2).
void UnlinkListNode(void* node)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(node))[2])(node, 1);
}

} // namespace

// 0x0053B450
bool __cdecl CFunc_PlayerTookFlag(Scripts::CStruct* params, Scripts::CScript*)
{
    Manager* manager = NetManager();
    int player = 0;
    int team = 0;
    params->FetchIntValue(kPlayer, &player, 0);
    params->FetchIntValue(kFlagTeam, &team, 0);
    uint8_t* taker = manager->PlayerByObjectId(static_cast<uint16_t>(player));
    if (taker)
        SendFlagMessage(manager, kMsgTookFlag, player, team, [&] { AsPlayer(taker)->NoteFlagPickup(team); });
    return true;
}

// 0x0053B5A0
bool __cdecl CFunc_PlayerCapturedFlag(Scripts::CStruct* params, Scripts::CScript*)
{
    Manager* manager = NetManager();
    int player = 0;
    params->FetchIntValue(kPlayer, &player, 0);
    uint8_t* capturer = manager->PlayerByObjectId(static_cast<uint16_t>(player));
    if (!capturer || (Field<uint32_t>(capturer, 0x1D0) & kPlayerCaptureBlocked))
        return true;
    int team = AsPlayer(capturer)->GetFlagTeam(); // the flag it carries
    SendFlagMessage(manager, kMsgCapturedFlag, player, team, [&] {
        auto* skater = reinterpret_cast<GameObjects::CGameEntity*>(PlayerSkater(capturer));
        uint8_t* score = Field<uint8_t*>(skater->GetComponent(kSkaterScore), 0x18);
        Call(CodeAddress(&GameModules::Score::SetGrandTotal), score, Call<int>(CodeAddress(&GameModules::Score::GetTotalScore), score) + 1); // its captures
        AsPlayer(capturer)->ScoreFlagCapture(team);
    });
    return true;
}

// 0x0053B710
bool __cdecl CFunc_PlayerRetrievedFlag(Scripts::CStruct* params, Scripts::CScript*)
{
    Manager* manager = NetManager();
    int player = 0;
    int team = 0;
    params->FetchIntValue("player", &player, 0);
    params->FetchIntValue("flag_team", &team, 0);
    uint8_t* retriever = manager->PlayerByObjectId(static_cast<uint16_t>(player));
    if (retriever) // (as a capture: message 0x72 and 0x00543A90 - not 0x73 / 0x00544110, the retrieve's)
        SendFlagMessage(manager, kMsgCapturedFlag, player, team, [&] { AsPlayer(retriever)->ScoreFlagCapture(team); });
    return true;
}

// 0x0053B860
bool __cdecl CFunc_PlayerHasFlag(Scripts::CStruct* params, Scripts::CScript*)
{
    int player = 0;
    params->FetchIntValue("player", &player, 0);
    uint8_t* found = NetManager()->PlayerByObjectId(static_cast<uint16_t>(player));
    if (!found)
        return true;
    return AsPlayer(found)->IsCarryingFlag() != 0; // (flags 0x7800: a team's flag)
}

// 0x0053B8A0
bool __cdecl CFunc_GetNumPlayersOnTeam(Scripts::CStruct* params, Scripts::CScript* script)
{
    Manager* manager = NetManager();
    int team = 0;
    params->FetchIntValue("team", &team, 1);
    script->m_params->PutInt("num_members", manager->CountTeamPlayers(team));
    return true;
}

// 0x0053B8F0
bool __cdecl CFunc_GetPlayersTeam(Scripts::CStruct* params, Scripts::CScript* script)
{
    int id = 0;
    params->FetchIntValue(kObjId, &id, 0);
    uint8_t* player = NetManager()->PlayerByObjectId(static_cast<uint16_t>(id));
    if (player)
        script->m_params->PutInt("team", PlayerTeam(player));
    return true;
}

// 0x0053B940
bool __cdecl CFunc_ExitSurveyorMode(Scripts::CStruct*, Scripts::CScript*)
{
    Manager* manager = NetManager();
    uint8_t* local = LocalPlayer(manager);
    if (!local)
        return false;
    if (Field<uint32_t>(local, 0x1D0) & kPlayerSurveying) {
        manager->SpectatePlayer(local);
        if (void* task = Field<void*>(local, 0x1EC))
            UnlinkListNode(task);
        Field<void*>(local, 0x1EC) = nullptr;
        if (manager->m_observerInputHandler)
            UnlinkListNode(manager->m_observerInputHandler);
        manager->m_observerInputHandler = nullptr;
        Field<uint32_t>(local, 0x1D0) &= ~kPlayerSurveying;
    }
    return true;
}

// 0x0053D400
bool __cdecl CFunc_SetHostMode(Scripts::CStruct* params, Scripts::CScript*)
{
    int mode = 0;
    params->FetchIntValue(0u, &mode, 0);
    NetManager()->SetHostMode(mode);
    return true;
}

bool g_textEntry;

// 0x0053D440
bool __cdecl CFunc_EnteredNetworkGame(Scripts::CStruct*, Scripts::CScript*)
{
    Manager* manager = NetManager();
    if (g_netEnabled) // (on the internet: the server's GameSpy reporting)
        StartQueryReporting(false);
    TaskList::Stack* tasks = LogicTasks();
    tasks->AddTask(Field<TaskList::SchedulerTask*>(manager, 0xD4));
    g_textEntry = true;
    tasks->AddTask(Field<TaskList::SchedulerTask*>(manager, 0xC8));
    Field<bool>(manager, 0x46) = true;
    return true;
}

// 0x0053D490
bool __cdecl CFunc_KickPlayer(Scripts::CStruct*, Scripts::CScript*)
{
    KickOrBan(NetManager(), kDropKicked, 0);
    return true;
}

// 0x0053D5E0
bool __cdecl CFunc_BanPlayer(Scripts::CStruct*, Scripts::CScript*)
{
    MemSys::Manager* memory = MemSys::Manager::Get();
    memory->EnterHeapScope(memory->m_networkHeap);
    KickOrBan(NetManager(), kDropBanned, 1);
    MemSys::Manager::Get()->LeaveHeapScope();
    return true;
}

// 0x0053D760
bool __cdecl CFunc_CancelJoinServer(Scripts::CStruct*, Scripts::CScript*)
{
    Manager* manager = NetManager();
    manager->CancelJoin();
    manager->m_joinState = 10;
    return true;
}

// 0x0053D780
bool __cdecl CFunc_EnterSurveyorMode(Scripts::CStruct*, Scripts::CScript*)
{
    TaskList::Stack* tasks = LogicTasks();
    Manager* manager = NetManager();
    uint8_t* input = reinterpret_cast<uint8_t*>(PadInput::g_inputManager); // the input manager
    uint8_t* local = LocalPlayer(manager);
    if (IsObserver(local) || (Field<uint32_t>(local, 0x1D0) & kPlayerSurveying))
        return false;
    MemSys::Manager* memory = MemSys::Manager::Get();
    memory->EnterHeapScope(memory->m_networkHeap);
    AddObserverControls(manager, local, tasks, input);
    Field<uint32_t>(local, 0x1D0) |= kPlayerSurveying;
    MemSys::Manager::Get()->LeaveHeapScope();
    return true;
}

// 0x0053EA20
bool __cdecl CFunc_DropPendingPlayers(Scripts::CStruct*, Scripts::CScript*)
{
    NetManager()->DropPlayersNotReady();
    return true;
}

} // namespace OnlinePlay
