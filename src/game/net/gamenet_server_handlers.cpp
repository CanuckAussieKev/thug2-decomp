// The network manager's server-side message handlers (GameNet::Manager, registered by 0x0053C380): observers, the
// game's data for a joining player, the end of a game, team changes, goal attack, leaving, cheating. See
// gamenet_handlers.h.

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/ed/park_editor.h"
#include "game/mth/random.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/ed/rail_editor.h"
#include "game/core/code_address.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

namespace GameNet {

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
constexpr uint8_t kMsgChangeLevel = 0x46;
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
    return reinterpret_cast<bool(__thiscall*)(uint8_t*, uint32_t)>(Field<void**>(goal, 0)[0x50 / 4])(goal, id);
}
void MarkGoalDoneBy(uint8_t* goal, uint32_t id)
{
    reinterpret_cast<void(__thiscall*)(uint8_t*, uint32_t)>(Field<void**>(goal, 0)[0x6C / 4])(goal, id);
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
int __cdecl HandleObserveRequest(Net::MsgHandlerContext* context)
{
    uint8_t* skate = Skate();
    Manager* manager = ManagerOf(context);
    uint8_t* player = PlayerByConnection(manager, context->m_Conn);
    if (!player || IsObserver(player))
        return kDone;
    Net::MsgDesc desc;
    if (!IsLocalPlayer(player) &&
        manager->CountObservers() >= manager->GetMaxObservers()) { // the observers so far
        desc.Construct();
        desc.m_id = kMsgObserversFull;
        manager->m_server->EnqueueMessage(context->m_Conn->m_handle, &desc);
        return kContinue;
    }
    desc.Construct();
    if (!IsLocalPlayer(player)) {
        Obj::CObject* skater = PlayerSkater(player);
        manager->DropPlayer(player, 2);         // the player out of the game (2: to observe)
        Call(CodeAddress(&Mdl::Skate::RemoveSkater), skate, skater);        // its skater gone
        Field<uint32_t>(player, 0x1D0) |= 4;    // an observer now
        Field<Obj::CObject*>(player, 0x14) = nullptr;
        if (AsPlayer(player)->IsServerPlayer()) {
            AsPlayer(player)->ClearServerPlayer();
            manager->AssignFirstComeFirstServed();
        }
        if (player == manager->m_camPlayer) { // the camera onto the next player (none if that's this one again)
            manager->m_camPlayer = manager->NextCamPlayer();
            if (player == manager->m_camPlayer)
                manager->m_camPlayer = nullptr;
            manager->ObservePlayer(static_cast<uint8_t*>(manager->m_camPlayer));
        }
    }
    desc.m_id = kMsgObserve;
    manager->m_server->EnqueueMessage(context->m_Conn->m_handle, &desc);
    return kContinue;
}

// 0x0052E9E0
int __cdecl HandleRequestGameData(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    uint8_t* player = PlayerByConnection(manager, context->m_Conn);
    const uint8_t* msg = Message(context);
    Net::App* app = context->m_App;
    int handle = context->m_Conn->m_handle;
    void* parkMap = Ed::CParkEditor::Instance()->GetCompressedMapBuffer(false);
    app->StreamMessage(handle, kMsgParkMap, kParkMapSize, parkMap, "level data", kGroupGame, false, true);
    handle = context->m_Conn->m_handle;
    auto railEditor = [] { return Ed::CRailEditorComponent::Instance(); };
    railEditor(); // (looked up once for nothing)
    void* rails = Call<void*>(CodeAddress(&Ed::CRailEditorComponent::GetPacked), railEditor());
    app->StreamMessage(handle, kMsgRailsData, kRailDataSize, rails, "rail data", kGroupGame, false, true);
    if (manager->HasCreatedGoals()) {
        manager->PrepareCreatedGoals(Field<uint32_t>(msg, 0));
        handle = context->m_Conn->m_handle;
        void* goals = manager->GetCreatedGoalsMessage();
        auto size = static_cast<uint16_t>(manager->GetCreatedGoalsSize());
        app->StreamMessage(handle, kMsgGoalsData, size, goals, "goals data", kGroupGame, false, true);
    }
    if (msg[4] != 'F')
        return kContinue;
    // The level change ('F', the level; 8 bytes - bytes 5-7 left as the original leaves them, zeroed here), then a
    // ready query (the time) the player owes the answer to.
    Net::MsgDesc desc;
    desc.Construct();
    uint32_t level[2] = {Field<uint32_t>(msg, 0), 0};
    uint32_t query = Tmr::GetTime();
    desc.m_data = level;
    desc.m_length = 8;
    desc.m_id = kMsgChangeLevel;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    app->EnqueueMessage(PlayerHandle(player), &desc);
    desc.m_data = &query;
    desc.m_length = 4;
    desc.m_id = kMsgAckQuery;
    app->EnqueueMessage(PlayerHandle(player), &desc);
    AsPlayer(player)->AwaitReadyResponse(query); // (the query it owes)
    app->SendData(false);
    return kContinue;
}

// 0x0052EC40
int __cdecl HandleGameEndedReport(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    uint8_t* player = PlayerByConnection(manager, context->m_Conn);
    uint8_t game = Message(context)[0];
    if (!player)
        return kContinue;
    PlayerSearch search;
    if ((Field<uint32_t>(player, 0x1D0) & kFlagReady) || game != manager->GetGameId())
        return kDone;
    Field<uint32_t>(player, 0x1D0) |= kFlagReady;
    for (uint8_t* other = FirstPlayer(manager, &search, false); other; other = NextPlayer(manager, &search, false)) {
        if (!(Field<uint32_t>(other, 0x1D0) & kFlagReady))
            return kContinue; // someone is still playing
    }
    // Everyone's game has ended: game over, to all (observers too).
    Net::MsgDesc desc;
    desc.Construct();
    Net::App* server = manager->m_server;
    desc.m_id = kMsgGameOver;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    for (uint8_t* other = FirstPlayer(manager, &search, true); other; other = NextPlayer(manager, &search, true))
        server->EnqueueMessage(PlayerHandle(other), &desc);
    return kContinue;
}

// 0x0052EE10
int __cdecl HandleTeamChangeRequest(Net::MsgHandlerContext* context)
{
    Net::MsgDesc desc;
    desc.Construct();
    PlayerSearch search;
    Manager* manager = ManagerOf(context);
    const uint8_t* msg = Message(context);
    uint8_t* player = PlayerByObjectId(manager, msg);
    if (!player || !PlayerSkater(player))
        return kDone;
    if (GameType() != kNetLobby && !manager->IsGameOver())
        return kDone;
    uint8_t change[2] = {msg[0], msg[1]};
    Net::App* server = manager->m_server;
    desc.m_data = change;
    desc.m_length = 2;
    desc.m_id = kMsgJoinedTeam;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    for (uint8_t* other = FirstPlayer(manager, &search, true); other; other = NextPlayer(manager, &search, true))
        server->EnqueueMessage(PlayerHandle(other), &desc);
    return kContinue;
}

// 0x0052EF40
int __cdecl HandleGoalCompletedReport(Net::MsgHandlerContext* context)
{
    const uint8_t* msg = Message(context);
    Manager* manager = ManagerOf(context);
    if (msg[0] != manager->GetGameId())
        return kDone;
    uint8_t* goal = GetGoal(GoalManager(), Field<uint32_t>(msg, 4));
    if (!goal)
        return kContinue;
    uint8_t* sender = PlayerByConnection(manager, context->m_Conn);
    if (!PlayerSkater(sender))
        return kContinue;
    PlayerSearch search;
    GoalMessage message;
    Net::MsgDesc desc;
    auto prepare = [&] {
        message.goal = Field<uint32_t>(msg, 4);
        message.id = static_cast<uint8_t>(SkaterId(PlayerSkater(sender)));
        std::memset(message.pad, 0, sizeof(message.pad));
        desc.m_data = &message;
        desc.m_length = 8;
        desc.m_id = kMsgGoalCompleted;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
    };
    if (IsTeamGame()) {
        // Already done by the sender's team: nothing.
        for (uint8_t* p = FirstPlayer(manager, &search, false); p; p = NextPlayer(manager, &search, false)) {
            if (PlayerTeam(sender) == PlayerTeam(p) && GoalDoneBy(goal, SkaterId(PlayerSkater(p))))
                return kContinue;
        }
        MarkGoalDoneBy(goal, SkaterId(PlayerSkater(sender)));
        for (uint8_t* p = FirstPlayer(manager, &search, false); p; p = NextPlayer(manager, &search, false)) {
            desc.Construct();
            prepare();
            context->m_App->EnqueueMessage(PlayerHandle(p), &desc);
        }
        return kContinue;
    }
    if (GoalDoneBy(goal, SkaterId(PlayerSkater(sender))))
        return kContinue;
    desc.Construct();
    MarkGoalDoneBy(goal, SkaterId(PlayerSkater(sender)));
    prepare();
    for (uint8_t* p = FirstPlayer(manager, &search, false); p; p = NextPlayer(manager, &search, false))
        context->m_App->EnqueueMessage(PlayerHandle(p), &desc);
    return kContinue;
}

// 0x0052F1A0
int __cdecl HandleGoalStartedReport(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    const uint8_t* msg = Message(context);
    if (msg[0] != manager->GetGameId())
        return kDone;
    if (!GetGoal(GoalManager(), Field<uint32_t>(msg, 4)) || !IsTeamGame())
        return kContinue;
    uint8_t* sender = PlayerByConnection(manager, context->m_Conn);
    if (!PlayerSkater(sender))
        return kContinue;
    PlayerSearch search;
    Net::MsgDesc desc;
    desc.Construct();
    GoalMessage message;
    message.goal = Field<uint32_t>(msg, 4);
    message.id = static_cast<uint8_t>(SkaterId(PlayerSkater(sender)));
    std::memset(message.pad, 0, sizeof(message.pad));
    desc.m_data = &message;
    desc.m_length = 8;
    desc.m_id = kMsgGoalStarted;
    for (uint8_t* p = FirstPlayer(manager, &search, false); p; p = NextPlayer(manager, &search, false)) {
        if (p != sender && PlayerTeam(sender) == PlayerTeam(p))
            context->m_App->EnqueueMessage(PlayerHandle(p), &desc);
    }
    return kContinue;
}

// 0x0052FCC0
int __cdecl HandleDisconnect(Net::MsgHandlerContext* context)
{
    uint8_t* skate = Skate();
    Manager* manager = ManagerOf(context);
    if (uint8_t* player = PlayerByConnection(manager, context->m_Conn)) {
        if (!IsLocalPlayer(player)) {
            Obj::CObject* skater = PlayerSkater(player);
            bool observer = IsObserver(player);
            manager->DropPlayer(player, 0); // the player out of the game
            if (!observer)
                Call(CodeAddress(&Mdl::Skate::RemoveSkater), skate, skater);
        }
        return kKeep;
    }
    // Not a player yet: one of the new players waiting to be added - dropped with its connection.
    auto* node = Field<Mem::ListNode*>(manager, 0x13C); // (the new players' list, its first node)
    if (node->data == reinterpret_cast<void*>(-1))
        return kHalt;
    for (; node;) {
        Mem::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
        auto* info = static_cast<uint8_t*>(node->data);
        if (Field<Net::Conn*>(info, 0x14) == context->m_Conn) {
            context->m_App->TerminateConnection(Field<Net::Conn*>(info, 0x14));
            reinterpret_cast<void(__thiscall*)(Mem::ListNode*, int)>(node->vtable[0])(node, 1); // the node deleted
            reinterpret_cast<NewPlayerInfo*>(info)->DestroyBody();
            Mem::Manager::Get()->Free(info);
            return kKeep;
        }
        node = next;
    }
    return kHalt;
}

// 0x0052FDA0
int __cdecl HandleDropCarried(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    uint8_t* player = PlayerByConnection(manager, context->m_Conn);
    if (player)
        Field<uint32_t>(player, 0x1D0) &= ~kFlag8000;
    PlayerSearch search;
    Net::MsgDesc desc;
    if (GameType() == kNetCtf) {
        // Its flag returned.
        if (!player || !AsPlayer(player)->IsCarryingFlag())
            return kContinue;
        desc.Construct();
        int team = AsPlayer(player)->GetFlagTeam();
        char name[64];
        TeamNameKey(name, team);
        Script::CStruct* params = NewParams();
        params->AddInteger(kTeam, team);
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, Script::GetString(name));
        Run(kFlagReturned, params);
        DeleteParams(params);
        uint8_t returned[2] = {static_cast<uint8_t>(SkaterId(PlayerSkater(player))), static_cast<uint8_t>(team)};
        if (IsLocalPlayer(player))
            Run(kDestroyCtfPanelMessage);
        uint8_t* local = LocalPlayer(manager);
        if (local && !IsObserver(local) && PlayerTeam(local) == team)
            Run(kHideCtfArrow);
        AsPlayer(player)->ClearFlags(); // carries no flag
        desc.m_data = returned;
        desc.m_length = 2;
        desc.m_id = kMsgFlagReturned;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        for (uint8_t* p = FirstPlayer(manager, &search, true); p; p = NextPlayer(manager, &search, true)) {
            if (!IsLocalPlayer(p))
                context->m_App->EnqueueMessage(PlayerHandle(p), &desc);
        }
        return kContinue;
    }
    if (GameType() != kNetKing && GameType() != kKing)
        return kContinue;
    // The crown dropped: a new spot for it.
    if (!player || !AsPlayer(player)->IsKing()) // (has the crown)
        return kContinue;
    desc.Construct();
    AsPlayer(player)->MarkAsKing(false);
    int spot = Mth::Rnd(10);
    Field<int>(manager, 0x68) = spot;
    uint8_t message = static_cast<uint8_t>(spot);
    desc.m_data = &message;
    desc.m_length = 1;
    desc.m_id = kMsgCrownSpawn;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    for (uint8_t* p = FirstPlayer(manager, &search, true); p; p = NextPlayer(manager, &search, true))
        context->m_App->EnqueueMessage(PlayerHandle(p), &desc);
    return kContinue;
}

// 0x00532170
int __cdecl HandleChecksumResponse(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    uint8_t* player = PlayerByConnection(manager, context->m_Conn);
    if ((MessageWord(context, 0) ^ 0xDEADFACE) == MessageWord(context, 4) || manager->CheatingNoted())
        return kContinue;
    Script::CStruct* params = NewParams();
    params->AddString(kString0, PlayerName(player));
    Run(kNotifyClientCheating, params);
    DeleteParams(params);
    manager->NoteCheating();
    return kContinue;
}

} // namespace GameNet
