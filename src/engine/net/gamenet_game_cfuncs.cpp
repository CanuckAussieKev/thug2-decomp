// The network game's script commands: capture the flag, the teams, surveyor mode, the host mode, kick / ban, cancelling
// a join (0x0053B450-0x0053D900). See gamenet_game_cfuncs.h.

#include "engine/net/gamenet_game_cfuncs.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_observer.h"
#include "engine/net/gamenet_qr2.h"
#include "engine/net/net_app.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "engine/sys/mlp.h"
#include "engine/core/code_address.h"
#include "engine/assets/resource_registry.h"
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

Hub* NetSessionSlot() { return g_onlineHub; }
Scheduling::PileUp* LogicTasks()
{
    return reinterpret_cast<Scheduling::PileUp*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 4);
}

// The flag message (took / captured): the player's object id and the team, to every remote player through the server;
// `local` for this machine's.
template <typename Local>
void SendFlagMessage(Hub* manager, uint8_t id, int player, int team, Local local)
{
    uint8_t msg[2] = {static_cast<uint8_t>(player), static_cast<uint8_t>(team)};
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    desc.m_byteCount = sizeof(msg);
    desc.m_content = msg;
    desc.m_ident = id;
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    NetLink::AppHost* server = manager->m_host;
    PlayerSearch search;
    for (uint8_t* other = StartRosterSweep(manager, &search, true); other; other = StepRosterSweep(manager, &search, true)) {
        if (IsOwnPlayer(other))
            local();
        else
            server->PutOnOutbox(PlayerHandle(other), &desc);
    }
}

// Kick or ban: the first player marked for it (counting every player before it).
void KickOrBan(Hub* manager, int drop, uint8_t request)
{
    PlayerSearch search;
    int index = 0;
    uint8_t* player = StartRosterSweep(manager, &search, true);
    for (; player && !(Prop<uint32_t>(player, 0x1D0) & kPlayerMarked); player = StepRosterSweep(manager, &search, true))
        ++index;
    if (!player)
        return;
    if (manager->OnServer()) {
        GameObjects::CEntity* skater = PlayerSkater(player);
        bool observer = IsObserver(player);
        manager->EvictPlayer(player, drop);
        if (!observer)
            Call(CodeAddress(&GameModules::Skate::DropSkaterFromRoster), Skate(), skater); // its skater removed
        return;
    }
    struct {
        uint8_t request;
        uint8_t index;
        char name[kNameSize];
        uint8_t pad[2];
    } msg;
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    msg.request = request;
    msg.index = static_cast<uint8_t>(index);
    std::strcpy(msg.name, PlayerName(player));
    desc.m_content = &msg;
    desc.m_byteCount = 2 + kNameSize;
    desc.m_ident = kMsgKickRequest;
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
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
    Hub* manager = NetSessionSlot();
    int player = 0;
    int team = 0;
    params->FetchIntValue(kPlayer, &player, 0);
    params->FetchIntValue(kFlagTeam, &team, 0);
    uint8_t* taker = manager->FindPlayerSlotByObj(static_cast<uint16_t>(player));
    if (taker)
        SendFlagMessage(manager, kMsgTookFlag, player, team, [&] { AsPlayer(taker)->NoteFlagPickup(team); });
    return true;
}

// 0x0053B5A0
bool __cdecl CFunc_PlayerCapturedFlag(Scripts::CStruct* params, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    int player = 0;
    params->FetchIntValue(kPlayer, &player, 0);
    uint8_t* capturer = manager->FindPlayerSlotByObj(static_cast<uint16_t>(player));
    if (!capturer || (Prop<uint32_t>(capturer, 0x1D0) & kPlayerCaptureBlocked))
        return true;
    int team = AsPlayer(capturer)->GetFlagTeam(); // the flag it carries
    SendFlagMessage(manager, kMsgCapturedFlag, player, team, [&] {
        auto* skater = reinterpret_cast<GameObjects::CGameEntity*>(PlayerSkater(capturer));
        uint8_t* score = Prop<uint8_t*>(skater->FetchModule(kSkaterScore), 0x18);
        Call(CodeAddress(&GameModules::PointTally::SetGrandTotal), score, Call<int>(CodeAddress(&GameModules::PointTally::GetTotalScore), score) + 1); // its captures
        AsPlayer(capturer)->ScoreFlagCapture(team);
    });
    return true;
}

// 0x0053B710
bool __cdecl CFunc_PlayerRetrievedFlag(Scripts::CStruct* params, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    int player = 0;
    int team = 0;
    params->FetchIntValue("player", &player, 0);
    params->FetchIntValue("flag_team", &team, 0);
    uint8_t* retriever = manager->FindPlayerSlotByObj(static_cast<uint16_t>(player));
    if (retriever) // (as a capture: message 0x72 and 0x00543A90 - not 0x73 / 0x00544110, the retrieve's)
        SendFlagMessage(manager, kMsgCapturedFlag, player, team, [&] { AsPlayer(retriever)->ScoreFlagCapture(team); });
    return true;
}

// 0x0053B860
bool __cdecl CFunc_PlayerHasFlag(Scripts::CStruct* params, Scripts::CScript*)
{
    int player = 0;
    params->FetchIntValue("player", &player, 0);
    uint8_t* found = NetSessionSlot()->FindPlayerSlotByObj(static_cast<uint16_t>(player));
    if (!found)
        return true;
    return AsPlayer(found)->IsCarryingFlag() != 0; // (flags 0x7800: a team's flag)
}

// 0x0053B8A0
bool __cdecl CFunc_GetNumPlayersOnTeam(Scripts::CStruct* params, Scripts::CScript* script)
{
    Hub* manager = NetSessionSlot();
    int team = 0;
    params->FetchIntValue("team", &team, 1);
    script->m_args->PutInt("num_members", manager->CountTeamPlayers(team));
    return true;
}

// 0x0053B8F0
bool __cdecl CFunc_GetPlayersTeam(Scripts::CStruct* params, Scripts::CScript* script)
{
    int id = 0;
    params->FetchIntValue(kObjId, &id, 0);
    uint8_t* player = NetSessionSlot()->FindPlayerSlotByObj(static_cast<uint16_t>(id));
    if (player)
        script->m_args->PutInt("team", PlayerTeam(player));
    return true;
}

// 0x0053B940
bool __cdecl CFunc_ExitSurveyorMode(Scripts::CStruct*, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    uint8_t* local = FindOwnPlayerRecord(manager);
    if (!local)
        return false;
    if (Prop<uint32_t>(local, 0x1D0) & kPlayerSurveying) {
        manager->SpectatePlayer(local);
        if (void* task = Prop<void*>(local, 0x1EC))
            UnlinkListNode(task);
        Prop<void*>(local, 0x1EC) = nullptr;
        if (manager->m_spectatorPadListener)
            UnlinkListNode(manager->m_spectatorPadListener);
        manager->m_spectatorPadListener = nullptr;
        Prop<uint32_t>(local, 0x1D0) &= ~kPlayerSurveying;
    }
    return true;
}

// 0x0053D400
bool __cdecl CFunc_SetHostMode(Scripts::CStruct* params, Scripts::CScript*)
{
    int mode = 0;
    params->FetchIntValue(0u, &mode, 0);
    NetSessionSlot()->SetHostMode(mode);
    return true;
}

bool g_textEntry;

// 0x0053D440
bool __cdecl CFunc_EnteredNetworkGame(Scripts::CStruct*, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    if (g_onlineActive) // (on the internet: the server's GameSpy reporting)
        StartQueryReporting(false);
    Scheduling::PileUp* tasks = LogicTasks();
    tasks->QueueJob(Prop<Scheduling::SchedulerTask*>(manager, 0xD4));
    g_textEntry = true;
    tasks->QueueJob(Prop<Scheduling::SchedulerTask*>(manager, 0xC8));
    Prop<bool>(manager, 0x46) = true;
    return true;
}

// 0x0053D490
bool __cdecl CFunc_KickPlayer(Scripts::CStruct*, Scripts::CScript*)
{
    KickOrBan(NetSessionSlot(), kDropKicked, 0);
    return true;
}

// 0x0053D5E0
bool __cdecl CFunc_BanPlayer(Scripts::CStruct*, Scripts::CScript*)
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_networkHeap);
    KickOrBan(NetSessionSlot(), kDropBanned, 1);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return true;
}

// 0x0053D760
bool __cdecl CFunc_CancelJoinServer(Scripts::CStruct*, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    manager->CancelJoin();
    manager->m_joinPhase = 10;
    return true;
}

// 0x0053D780
bool __cdecl CFunc_EnterSurveyorMode(Scripts::CStruct*, Scripts::CScript*)
{
    Scheduling::PileUp* tasks = LogicTasks();
    Hub* manager = NetSessionSlot();
    uint8_t* input = reinterpret_cast<uint8_t*>(PadInput::g_inputManager); // the input manager
    uint8_t* local = FindOwnPlayerRecord(manager);
    if (IsObserver(local) || (Prop<uint32_t>(local, 0x1D0) & kPlayerSurveying))
        return false;
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_networkHeap);
    AddObserverControls(manager, local, tasks, input);
    Prop<uint32_t>(local, 0x1D0) |= kPlayerSurveying;
    MemSys::Hub::Pull()->LeaveHeapScope();
    return true;
}

// 0x0053EA20
bool __cdecl CFunc_DropPendingPlayers(Scripts::CStruct*, Scripts::CScript*)
{
    NetSessionSlot()->DropPlayersNotReady();
    return true;
}

} // namespace OnlinePlay
