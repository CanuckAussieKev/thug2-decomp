// The network manager's server-side message handlers (OnlinePlay::Hub, registered by 0x0053C380): observers, the
// game's data for a joining player, the end of a game, team changes, goal attack, leaving, cheating. See
// gamenet_handlers.h.

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "game/park_editing/park_editor.h"
#include "engine/maths/random.h"
#include "engine/net/gamenet_handler_util.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_conn.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "engine/core/code_address.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "engine/net/server_list.h"
#include "engine/net/score_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/stats_man.h"

namespace OnlinePlay {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;             // netlobby
constexpr uint32_t kNetCtf = 0x6C5FF266;               // netctf
constexpr uint32_t kNetKing = 0x6EF8FDA0;              // netking
constexpr uint32_t kKing = 0x5D32129C;                 // king
constexpr uint32_t kTeam = 0x3B1F59E0;                 // Team
constexpr uint32_t kString0 = 0xA4B08520;              // String0
constexpr uint32_t kFlagReturned = 0x3B1F2478;         // flag_returned
constexpr uint32_t kDestroyCtfPanelMessage = 0x514A53A6; // destroy_ctf_panel_message
constexpr uint32_t kHideCtfArrow = 0x1C318F8C;         // hide_ctf_arrow
constexpr uint32_t kNotifyClientCheating = 0x62DDBE0A; // notify_client_cheating

// Messages.
constexpr uint8_t kMsgAckQuery = 0x25;       // (the ready query)
constexpr uint8_t kMsgLevelSwitch = 0x46;
constexpr uint8_t kMsgObserve = 0x4D;
constexpr uint8_t kMsgObserversFull = 0x4E;
constexpr uint8_t kMsgCrownSpawn = 0x58;
constexpr uint8_t kMsgGameOver = 0x5A;
constexpr uint8_t kMsgJoinedTeam = 0x68;
constexpr uint8_t kMsgParkMap = 0x6C;
constexpr uint8_t kMsgGoalCompleted = 0x6E;
constexpr uint8_t kMsgGoalStarted = 0x6F;
constexpr uint8_t kMsgFlagReturned = 0x75;
constexpr uint8_t kMsgGoalsData = 0x87;
constexpr uint8_t kMsgRailsData = 0x88;

constexpr int kParkMapSize = 15000;
constexpr uint16_t kRailDataSize = 0x1132;

constexpr uint32_t kFlagReady = 0x100;       // a player's: its game has ended (message 0x59)
constexpr uint32_t kFlag8000 = 0x8000;

// A goal's virtuals: +0x50 has the skater `id` done it, +0x6C done by `id`.
bool GoalDoneBy(uint8_t* goal, uint32_t id)
{
    return reinterpret_cast<bool(__thiscall*)(uint8_t*, uint32_t)>(Prop<void**>(goal, 0)[0x50 / 4])(goal, id);
}
void MarkGoalDoneBy(uint8_t* goal, uint32_t id)
{
    reinterpret_cast<void(__thiscall*)(uint8_t*, uint32_t)>(Prop<void**>(goal, 0)[0x6C / 4])(goal, id);
}

// The goal attack message to other players: the skater's id, then the goal's name (8 bytes; bytes 1-3 left as the
// original leaves them - zeroed here).
struct GoalMessage {
    uint8_t id;
    uint8_t pad[3];
    uint32_t goal;
};

} // namespace

// 0x0052E060
int __cdecl HandleObserveRequest(NetLink::InboundMsgCtx* context)
{
    uint8_t* skate = Skate();
    Hub* manager = ManagerOf(context);
    uint8_t* player = FindPlayerBySender(manager, context->m_senderConn);
    if (!player || IsObserver(player))
        return kDone;
    NetLink::NetMessageInfo desc;
    if (!IsOwnPlayer(player) &&
        manager->CountObservers() >= manager->GetSpectatorLimit()) { // the observers so far
        desc.MakeNew();
        desc.m_ident = kMsgObserversFull;
        manager->m_host->PutOnOutbox(context->m_senderConn->m_hdl, &desc);
        return kContinue;
    }
    desc.MakeNew();
    if (!IsOwnPlayer(player)) {
        GameObjects::CEntity* skater = PlayerSkater(player);
        manager->EvictPlayer(player, 2);         // the player out of the game (2: to observe)
        Call(CodeAddress(&GameModules::Skate::DropSkaterFromRoster), skate, skater);        // its skater gone
        Prop<uint32_t>(player, 0x1D0) |= 4;    // an observer now
        Prop<GameObjects::CEntity*>(player, 0x14) = nullptr;
        if (AsPlayer(player)->IsHostPlayer()) {
            AsPlayer(player)->ClearServerPlayer();
            manager->AssignFirstComeFirstServed();
        }
        if (player == manager->m_viewedPlayer) { // the camera onto the next player (none if that's this one again)
            manager->m_viewedPlayer = manager->NextCamPlayer();
            if (player == manager->m_viewedPlayer)
                manager->m_viewedPlayer = nullptr;
            manager->SpectatePlayer(static_cast<uint8_t*>(manager->m_viewedPlayer));
        }
    }
    desc.m_ident = kMsgObserve;
    manager->m_host->PutOnOutbox(context->m_senderConn->m_hdl, &desc);
    return kContinue;
}

// 0x0052E9E0
int __cdecl HandleRequestGameData(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    uint8_t* player = FindPlayerBySender(manager, context->m_senderConn);
    const uint8_t* msg = Message(context);
    NetLink::AppHost* app = context->m_receiverApp;
    int handle = context->m_senderConn->m_hdl;
    void* parkMap = ParkEditing::CParkBuilderMode::Sole()->GetPackedParkData(false);
    app->SendBulkMessage(handle, kMsgParkMap, kParkMapSize, parkMap, "level data", kGroupGame, false, true);
    handle = context->m_senderConn->m_hdl;
    auto railEditor = [] { return ParkEditing::CRailAuthoring::Sole(); };
    railEditor(); // (looked up once for nothing)
    void* rails = Call<void*>(CodeAddress(&ParkEditing::CRailAuthoring::GetPacked), railEditor());
    app->SendBulkMessage(handle, kMsgRailsData, kRailDataSize, rails, "rail data", kGroupGame, false, true);
    if (manager->HasCreatedGoals()) {
        manager->PrepareCreatedGoals(Prop<uint32_t>(msg, 0));
        handle = context->m_senderConn->m_hdl;
        void* goals = manager->GetCreatedGoalsMessage();
        auto size = static_cast<uint16_t>(manager->GetCreatedGoalsSize());
        app->SendBulkMessage(handle, kMsgGoalsData, size, goals, "goals data", kGroupGame, false, true);
    }
    if (msg[4] != 'F')
        return kContinue;
    // The level change ('F', the level; 8 bytes - bytes 5-7 left as the original leaves them, zeroed here), then a
    // ready query (the time) the player owes the answer to.
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    uint32_t level[2] = {Prop<uint32_t>(msg, 0), 0};
    uint32_t query = FrameClock::GetTime();
    desc.m_content = level;
    desc.m_byteCount = 8;
    desc.m_ident = kMsgLevelSwitch;
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    app->PutOnOutbox(PlayerHandle(player), &desc);
    desc.m_content = &query;
    desc.m_byteCount = 4;
    desc.m_ident = kMsgAckQuery;
    app->PutOnOutbox(PlayerHandle(player), &desc);
    AsPlayer(player)->AwaitReadyResponse(query); // (the query it owes)
    app->Transmit(false);
    return kContinue;
}

// 0x0052EC40
int __cdecl HandleGameEndedReport(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    uint8_t* player = FindPlayerBySender(manager, context->m_senderConn);
    uint8_t game = Message(context)[0];
    if (!player)
        return kContinue;
    PlayerSearch search;
    if ((Prop<uint32_t>(player, 0x1D0) & kFlagReady) || game != manager->GetGameId())
        return kDone;
    Prop<uint32_t>(player, 0x1D0) |= kFlagReady;
    for (uint8_t* other = StartRosterSweep(manager, &search, false); other; other = StepRosterSweep(manager, &search, false)) {
        if (!(Prop<uint32_t>(other, 0x1D0) & kFlagReady))
            return kContinue; // someone is still playing
    }
    // Everyone's game has ended: game over, to all (observers too).
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    NetLink::AppHost* server = manager->m_host;
    desc.m_ident = kMsgGameOver;
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    for (uint8_t* other = StartRosterSweep(manager, &search, true); other; other = StepRosterSweep(manager, &search, true))
        server->PutOnOutbox(PlayerHandle(other), &desc);
    return kContinue;
}

// 0x0052EE10
int __cdecl OnSwitchSquadMsg(NetLink::InboundMsgCtx* context)
{
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    PlayerSearch search;
    Hub* manager = ManagerOf(context);
    const uint8_t* msg = Message(context);
    uint8_t* player = FindPlayerSlotByObj(manager, msg);
    if (!player || !PlayerSkater(player))
        return kDone;
    if (GameType() != kNetLobby && !manager->HasMatchEnded())
        return kDone;
    uint8_t change[2] = {msg[0], msg[1]};
    NetLink::AppHost* server = manager->m_host;
    desc.m_content = change;
    desc.m_byteCount = 2;
    desc.m_ident = kMsgJoinedTeam;
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    for (uint8_t* other = StartRosterSweep(manager, &search, true); other; other = StepRosterSweep(manager, &search, true))
        server->PutOnOutbox(PlayerHandle(other), &desc);
    return kContinue;
}

// 0x0052EF40
int __cdecl HandleGoalCompletedReport(NetLink::InboundMsgCtx* context)
{
    const uint8_t* msg = Message(context);
    Hub* manager = ManagerOf(context);
    if (msg[0] != manager->GetGameId())
        return kDone;
    uint8_t* goal = GetGoal(ObjectiveBoard(), Prop<uint32_t>(msg, 4));
    if (!goal)
        return kContinue;
    uint8_t* sender = FindPlayerBySender(manager, context->m_senderConn);
    if (!PlayerSkater(sender))
        return kContinue;
    PlayerSearch search;
    GoalMessage message;
    NetLink::NetMessageInfo desc;
    auto prepare = [&] {
        message.goal = Prop<uint32_t>(msg, 4);
        message.id = static_cast<uint8_t>(SkaterId(PlayerSkater(sender)));
        std::memset(message.pad, 0, sizeof(message.pad));
        desc.m_content = &message;
        desc.m_byteCount = 8;
        desc.m_ident = kMsgGoalCompleted;
        desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
        desc.m_groupId = kGroupGame;
    };
    if (IsSquadMatch()) {
        // Already done by the sender's team: nothing.
        for (uint8_t* p = StartRosterSweep(manager, &search, false); p; p = StepRosterSweep(manager, &search, false)) {
            if (PlayerTeam(sender) == PlayerTeam(p) && GoalDoneBy(goal, SkaterId(PlayerSkater(p))))
                return kContinue;
        }
        MarkGoalDoneBy(goal, SkaterId(PlayerSkater(sender)));
        for (uint8_t* p = StartRosterSweep(manager, &search, false); p; p = StepRosterSweep(manager, &search, false)) {
            desc.MakeNew();
            prepare();
            context->m_receiverApp->PutOnOutbox(PlayerHandle(p), &desc);
        }
        return kContinue;
    }
    if (GoalDoneBy(goal, SkaterId(PlayerSkater(sender))))
        return kContinue;
    desc.MakeNew();
    MarkGoalDoneBy(goal, SkaterId(PlayerSkater(sender)));
    prepare();
    for (uint8_t* p = StartRosterSweep(manager, &search, false); p; p = StepRosterSweep(manager, &search, false))
        context->m_receiverApp->PutOnOutbox(PlayerHandle(p), &desc);
    return kContinue;
}

// 0x0052F1A0
int __cdecl HandleGoalStartedReport(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    const uint8_t* msg = Message(context);
    if (msg[0] != manager->GetGameId())
        return kDone;
    if (!GetGoal(ObjectiveBoard(), Prop<uint32_t>(msg, 4)) || !IsSquadMatch())
        return kContinue;
    uint8_t* sender = FindPlayerBySender(manager, context->m_senderConn);
    if (!PlayerSkater(sender))
        return kContinue;
    PlayerSearch search;
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    GoalMessage message;
    message.goal = Prop<uint32_t>(msg, 4);
    message.id = static_cast<uint8_t>(SkaterId(PlayerSkater(sender)));
    std::memset(message.pad, 0, sizeof(message.pad));
    desc.m_content = &message;
    desc.m_byteCount = 8;
    desc.m_ident = kMsgGoalStarted;
    for (uint8_t* p = StartRosterSweep(manager, &search, false); p; p = StepRosterSweep(manager, &search, false)) {
        if (p != sender && PlayerTeam(sender) == PlayerTeam(p))
            context->m_receiverApp->PutOnOutbox(PlayerHandle(p), &desc);
    }
    return kContinue;
}

// 0x0052FCC0
int __cdecl OnPeerLeftMsg(NetLink::InboundMsgCtx* context)
{
    uint8_t* skate = Skate();
    Hub* manager = ManagerOf(context);
    if (uint8_t* player = FindPlayerBySender(manager, context->m_senderConn)) {
        if (!IsOwnPlayer(player)) {
            GameObjects::CEntity* skater = PlayerSkater(player);
            bool observer = IsObserver(player);
            manager->EvictPlayer(player, 0); // the player out of the game
            if (!observer)
                Call(CodeAddress(&GameModules::Skate::DropSkaterFromRoster), skate, skater);
        }
        return kKeep;
    }
    // Not a player yet: one of the new players waiting to be added - dropped with its connection.
    auto* node = Prop<MemSys::ListNode*>(manager, 0x13C); // (the new players' list, its first node)
    if (node->data == reinterpret_cast<void*>(-1))
        return kHalt;
    for (; node;) {
        MemSys::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
        auto* info = static_cast<uint8_t*>(node->data);
        if (Prop<NetLink::Conn*>(info, 0x14) == context->m_senderConn) {
            context->m_receiverApp->SeverLink(Prop<NetLink::Conn*>(info, 0x14));
            reinterpret_cast<void(__thiscall*)(MemSys::ListNode*, int)>(node->vtable[0])(node, 1); // the node deleted
            reinterpret_cast<JoinerRecord*>(info)->DestroyBody();
            MemSys::Hub::Pull()->Dealloc(info);
            return kKeep;
        }
        node = next;
    }
    return kHalt;
}

// 0x0052FDA0
int __cdecl HandleDropCarried(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    uint8_t* player = FindPlayerBySender(manager, context->m_senderConn);
    if (player)
        Prop<uint32_t>(player, 0x1D0) &= ~kFlag8000;
    PlayerSearch search;
    NetLink::NetMessageInfo desc;
    if (GameType() == kNetCtf) {
        // Its flag returned.
        if (!player || !AsPlayer(player)->IsCarryingFlag())
            return kContinue;
        desc.MakeNew();
        int team = AsPlayer(player)->GetFlagTeam();
        char name[64];
        TeamNameKey(name, team);
        Scripts::CStruct* params = NewParams();
        params->PutInt(kTeam, team);
        params->AppendWord(kString0, Scripts::QBVAL_STRING, Scripts::FetchText(name));
        Run(kFlagReturned, params);
        DeleteParams(params);
        uint8_t returned[2] = {static_cast<uint8_t>(SkaterId(PlayerSkater(player))), static_cast<uint8_t>(team)};
        if (IsOwnPlayer(player))
            Run(kDestroyCtfPanelMessage);
        uint8_t* local = FindOwnPlayerRecord(manager);
        if (local && !IsObserver(local) && PlayerTeam(local) == team)
            Run(kHideCtfArrow);
        AsPlayer(player)->ClearFlags(); // carries no flag
        desc.m_content = returned;
        desc.m_byteCount = 2;
        desc.m_ident = kMsgFlagReturned;
        desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
        desc.m_groupId = kGroupGame;
        for (uint8_t* p = StartRosterSweep(manager, &search, true); p; p = StepRosterSweep(manager, &search, true)) {
            if (!IsOwnPlayer(p))
                context->m_receiverApp->PutOnOutbox(PlayerHandle(p), &desc);
        }
        return kContinue;
    }
    if (GameType() != kNetKing && GameType() != kKing)
        return kContinue;
    // The crown dropped: a new spot for it.
    if (!player || !AsPlayer(player)->HoldsCrown()) // (has the crown)
        return kContinue;
    desc.MakeNew();
    AsPlayer(player)->GiveCrown(false);
    int spot = Maths::Rnd(10);
    Prop<int>(manager, 0x68) = spot;
    uint8_t message = static_cast<uint8_t>(spot);
    desc.m_content = &message;
    desc.m_byteCount = 1;
    desc.m_ident = kMsgCrownSpawn;
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    for (uint8_t* p = StartRosterSweep(manager, &search, true); p; p = StepRosterSweep(manager, &search, true))
        context->m_receiverApp->PutOnOutbox(PlayerHandle(p), &desc);
    return kContinue;
}

// 0x00532170
int __cdecl HandleChecksumResponse(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    uint8_t* player = FindPlayerBySender(manager, context->m_senderConn);
    if ((MessageWord(context, 0) ^ 0xDEADFACE) == MessageWord(context, 4) || manager->CheatingNoted())
        return kContinue;
    Scripts::CStruct* params = NewParams();
    params->AppendWord(kString0, PlayerName(player));
    Run(kNotifyClientCheating, params);
    DeleteParams(params);
    manager->NoteCheating();
    return kContinue;
}

} // namespace OnlinePlay
