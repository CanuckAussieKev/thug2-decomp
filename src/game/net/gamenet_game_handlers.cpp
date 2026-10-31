// The network manager's message handlers about the game itself (OnlinePlay::Hub): new players, the crown, CTF teams,
// goal attack goals, the created rails and goals arriving, eliminations, join refusals. See gamenet_handlers.h.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/task_list.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/obj/game_objects.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "game/core/code_address.h"
#include "game/park_editing/park.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/rig.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

namespace OnlinePlay {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kNetMessageNewPlayer = 0x640D3C87;       // net_message_new_player
constexpr uint32_t kDroppedCrownYou = 0xD10CDB7C;           // net_message_dropped_crown_you
constexpr uint32_t kDroppedCrownOther = 0x484CE1C7;         // net_message_dropped_crown_other
constexpr uint32_t kType = 0x7321A8D6;                      // Type
constexpr uint32_t kCrownSpawnNode = 0xAF86421B;            // (a node type: where the crown may appear)
constexpr uint32_t kChecksum = 0x21902065;                  // checksum
constexpr uint32_t kLaunchFcfsNotification = 0xEB123AF2;    // launch_fcfs_notification
constexpr uint32_t kTeam = 0x3B1F59E0;                      // Team
constexpr uint32_t kString0 = 0xA4B08520;                   // String0
constexpr uint32_t kString1 = 0xD3B7B5B6;                   // String1
constexpr uint32_t kFlagReturned = 0x3B1F2478;              // flag_returned
constexpr uint32_t kDestroyCtfPanelMessage = 0x514A53A6;    // destroy_ctf_panel_message
constexpr uint32_t kHideCtfArrow = 0x1C318F8C;              // hide_ctf_arrow
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;              // Load_MainMenu
constexpr uint32_t kShowTimeout = 0x19ECA78E;               // show_timeout
constexpr uint32_t kCancelJoinServer = 0x60B653DB;          // cancel_join_server
constexpr uint32_t kDoNotCreateGoalPeds = 0x88001327;       // DoNotCreateGoalPeds
constexpr uint32_t kInitialiseCreatedGoals = 0x6F4180D0;    // InitialiseCreatedGoals
constexpr uint32_t kGoalSummary = 0x40FC9B60;               // goal_summary
constexpr uint32_t kViewSelectedGoalsMenu = 0x8DF3084E;     // wait_and_create_view_selected_goals_menu
constexpr uint32_t kJoinedTeamYou = 0x92EFAD9E;             // joined_team_you
constexpr uint32_t kJoinedTeamOther = 0x9D62E743;           // joined_team_other
constexpr uint32_t kWarnAllSameTeam = 0x16C9B0DC;           // warn_all_same_team
constexpr uint32_t kNumGoalsLeft = 0x684A8396;              // NumGoalsLeft
constexpr uint32_t kCompletedGoal = 0xFFB3D309;             // goal_attack_completed_goal
constexpr uint32_t kCompletedGoalSameTeam = 0xF4748E47;     // goal_attack_completed_goal_other_same_team
constexpr uint32_t kStartedGoalSameTeam = 0xA77758DE;       // goal_attack_started_goal_other_same_team
constexpr uint32_t kPlayerName = 0x9196D920;                // PlayerName
constexpr uint32_t kGoalText = 0xB8A88B50;                  // GoalText
constexpr uint32_t kEliminatedNone = 0x4BA23859;            // EliminatedNone
constexpr uint32_t kEliminatedYou = 0x35E06259;             // EliminatedYou
constexpr uint32_t kEliminatedOther = 0x1202DEE4;           // EliminatedOther
constexpr uint32_t kReason = 0xC44777F3;                    // reason
constexpr uint32_t kFullObservers = 0xDDE5676F;             // net_reason_full_observers
constexpr uint32_t kJustDialog = 0xF6694E96;                // just_dialog
constexpr uint32_t kCreateJoinRefusedDialog = 0x92528E98;   // CreateJoinRefusedDialog

constexpr uint8_t kMsgEliminated = 0x8D;
constexpr uint8_t kMsgRequestRails = 0x8B;
constexpr uint8_t kMsgRequestGoals = 0x8A;

constexpr int kPanelTime = 2000;

// 0x005376F0: a panel message (broadcast, text, string 1, string 2, object, properties, important, time).
void PanelMessage(Hub* manager, uint32_t text, const char* string1, GameObjects::CEntity* object)
{
    manager->BuildNetNotice(false, text, string1, nullptr, object, nullptr, false, kPanelTime);
}

// The rail editor (0x004F4970) and the goal editor (0x004E8470).
void* RailEditor() { return ParkEditing::CRailAuthoring::Sole(); }
void* GoalEditor() { return ParkEditing::CObjectiveAuthoring::Pull(); }

// Leaving for the main menu while the created data couldn't come: the join cancelled with a time-out notice.
void CancelJoin(Hub* manager, bool notifyFirst)
{
    Scripts::CStruct* params = nullptr;
    if (notifyFirst) {
        params = NewParams();
        params->PutNameKey(0u, kShowTimeout);
    }
    manager->m_joinPhaseJob->DropOut();
    manager->SetEntryPhase(2);
    if (!notifyFirst) {
        params = NewParams();
        params->PutNameKey(0u, kShowTimeout);
    }
    Run(kCancelJoinServer, params);
    DeleteParams(params);
}

} // namespace

// 0x0052DB80
int __cdecl OnPlayerAddedMsg(NetLink::InboundMsgCtx* context)
{
    const uint8_t* msg = Message(context);
    Hub* manager = ManagerOf(context);
    alignas(4) uint8_t info[0x44]; // a JoinerRecord
    reinterpret_cast<JoinerRecord*>(info)->MakeNew(); // (its constructor: a skater profile at +0x1C)
    // (strncpy: 15 characters at most, the rest of the 15 zeroed)
    size_t length = strnlen(reinterpret_cast<const char*>(msg + 0x20), 15);
    std::memcpy(info, msg + 0x20, length);
    std::memset(info + length, 0, 15 - length);
    info[0x0F] = 0;
    info[0x10] = msg[0x18];                                  // the object id
    Prop<uint32_t>(info, 0x14) = 0;                         // the connection
    Prop<uint32_t>(info, 0x20) = Prop<uint32_t>(msg, 0x08); // flags
    Prop<int>(info, 0x28) = static_cast<int8_t>(msg[0x19]);  // the team
    Prop<uint32_t>(info, 0x2C) = Prop<uint32_t>(msg, 0x00);
    Prop<uint32_t>(info, 0x30) = Prop<uint32_t>(msg, 0x04);
    Prop<uint32_t>(info, 0x38) = Prop<uint32_t>(msg, 0x14);
    Prop<uint32_t>(info, 0x40) = Prop<uint32_t>(msg, 0x1C);
    Prop<uint32_t>(info, 0x34) = Prop<uint32_t>(msg, 0x0C);
    if (!(Prop<uint32_t>(info, 0x20) & 5))
        Prop<GameObjects::CRiderIdentity*>(info, 0x1C)->DecodeFromBytes(const_cast<uint8_t*>(msg + 0x30)); // the appearance into its profile
    int delay = 0;
    if (manager->m_clearedToStart) {
        manager->BuildNetNotice(false, kNetMessageNewPlayer, reinterpret_cast<const char*>(info), nullptr, nullptr,
                                       nullptr, false, kPanelTime);
        delay = 10;
    }
    manager->AddJoiningPlayer(info, delay);                   // the new player (added in `delay` frames)
    reinterpret_cast<JoinerRecord*>(info)->DestroyBody(); // (its destructor)
    return kContinue;
}

// 0x0052E870
int __cdecl HandleCrownSpawn(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    uint8_t* king = IsCrownRuleActive(manager);
    char spot = static_cast<char>(Message(context)[0]);
    if (king) {
        AsPlayer(king)->GiveCrown(false); // no longer the king
    }
    if (king && IsOwnPlayer(king)) {
        PanelMessage(manager, kDroppedCrownYou, nullptr, PlayerSkater(king));
    } else if (manager->InNetGame()) {
        PanelMessage(manager, kDroppedCrownOther, nullptr, nullptr);
    } else if (king && PlayerSkater(king)) {
        // the other local player (id 1 if this one's is 0, else 0)
        uint8_t* other = FindPlayerSlotByObj(manager, static_cast<uint16_t>(SkaterId(PlayerSkater(king)) == 0));
        if (other)
            PanelMessage(manager, kDroppedCrownOther, nullptr, PlayerSkater(other));
    }
    auto* crown = static_cast<GameObjects::CCrownObject*>(manager->GetCrownHolder());
    if (!crown)
        return kContinue;
    // The crown to the spot'th crown node (counted on across up to 49 walks of the nodes).
    bool placed = false;
    for (int walks = 1; walks != 50 && !placed; ++walks) {
        LevelNodes::StartNodeWalk(0);
        for (Scripts::CStruct* node = LevelNodes::NextNode(); node; node = LevelNodes::NextNode()) {
            uint32_t type = 0;
            node->ReadHashValue(kType, &type, 0);
            if (type != kCrownSpawnNode)
                continue;
            if (spot == 0) {
                Maths::Vec4 position;
                LevelNodes::FetchPlace(node, &position);
                crown->m_atPoint = position;
                placed = true;
                break;
            }
            --spot;
        }
        LevelNodes::EndNodeWalk();
    }
    crown->TakeOff();
    return kContinue;
}

// 0x005300C0
int __cdecl HandleFcfsNotice(NetLink::InboundMsgCtx* context)
{
    uint8_t* local = LocalPlayer(ManagerOf(context));
    const uint8_t* msg = Message(context);
    AsPlayer(local)->SetHostPlayer(); // (its flag 0x200 set)
    Scripts::CStruct* params = NewParams();
    params->PutNameKey(kChecksum, Prop<uint32_t>(msg, 0));
    Run(kLaunchFcfsNotification, params);
    DeleteParams(params);
    return kContinue;
}

// 0x00530CB0
int __cdecl HandleFlagReturned(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    uint8_t* player = FindPlayerSlotByObj(manager, Message(context));
    if (!player || !AsPlayer(player)->IsCarryingFlag()) // (carrying a flag: 0 / 1)
        return kContinue;
    int team = AsPlayer(player)->GetFlagTeam(); // the flag's team
    char name[64];
    TeamNameKey(name, team);
    Scripts::CStruct* params = NewParams();
    params->PutInt(kTeam, team);
    params->AppendWord(kString0, Scripts::QBVAL_STRING, Scripts::FetchText(name));
    Run(kFlagReturned, params);
    DeleteParams(params);
    if (IsOwnPlayer(player))
        Run(kDestroyCtfPanelMessage);
    uint8_t* local = LocalPlayer(manager);
    if (local && !IsObserver(local) && PlayerTeam(local) == team)
        Run(kHideCtfArrow);
    AsPlayer(player)->ClearFlags(); // carries no flag
    return kContinue;
}

// 0x00530E50
int __cdecl HandleRailsData(NetLink::InboundMsgCtx* context)
{
    uint8_t* skate = Skate();
    Hub* manager = ManagerOf(context);
    if (!(context->m_wireOptions & NetLink::kPacketChecksumMismatch)) {
        MemSys::Hub* memory = MemSys::Hub::Pull();
        memory->EnterHeapScope(Prop<MemSys::MemSource*>(memory, 0x114));
        Call(CodeAddress(&ParkEditing::CRailAuthoring::SetPacked), RailEditor(), context->m_body); // the rails read
        ParkEditing::g_railEditorFlag = false;
        Call(CodeAddress(&ParkEditing::CRailAuthoring::Unpack), RailEditor());
        ParkEditing::g_railEditorFlag = true;
        MemSys::Hub::Pull()->LeaveHeapScope();
        return kContinue;
    }
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    if (Prop<uint32_t>(skate, 0x378) == kLoadMainMenu) {
        CancelJoin(manager, true);
        return kKeep;
    }
    desc.m_ident = kMsgRequestRails; // again
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    SendToServer(context->m_receiverApp, &desc);
    return kDone;
}

// 0x00530FF0
int __cdecl OnGoalSyncMsg(NetLink::InboundMsgCtx* context)
{
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    Hub* manager = ManagerOf(context);
    if (!(context->m_wireOptions & NetLink::kPacketChecksumMismatch)) {
        const char* msg = context->m_body;
        Call(CodeAddress(&ParkEditing::CObjectiveAuthoring::ReadLevelGoalsFromBuffer), GoalEditor(), Prop<uint32_t>(msg, 0), msg + 4); // the level's goals read
        Scripts::CStruct* params = NewParams();
        params->PutNameKey(0u, kDoNotCreateGoalPeds);
        Run(kInitialiseCreatedGoals, params);
        DeleteParams(params);
        return kContinue;
    }
    if (Prop<uint32_t>(Skate(), 0x378) == kLoadMainMenu) {
        CancelJoin(manager, false);
        return kKeep;
    }
    desc.m_ident = kMsgRequestGoals; // again
    desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
    desc.m_groupId = kGroupGame;
    SendToServer(context->m_receiverApp, &desc);
    return kDone;
}

// 0x0052E760
int __cdecl OnCrownChangeMsg(NetLink::InboundMsgCtx* context)
{
    const uint8_t* msg = Message(context);
    Hub* manager = ManagerOf(context);
    NewPlayerSearch search;
    // The crown off the old king - or off a new player waiting to be added.
    if (uint8_t* king = IsCrownRuleActive(manager)) {
        AsPlayer(king)->GiveCrown(false);
    } else {
        for (uint8_t* added = FirstNewPlayer(manager, &search); added; added = NextNewPlayer(manager, &search)) {
            if (Prop<uint32_t>(added, 0x20) & 0x80) {
                Prop<uint32_t>(added, 0x20) &= ~0x80u;
                break;
            }
        }
    }
    // Onto the player of object id msg[0] - or the new player of that id.
    if (uint8_t* player = FindPlayerSlotByObj(manager, msg)) {
        AsPlayer(player)->GiveCrown(true);
        return kContinue;
    }
    for (uint8_t* added = FirstNewPlayer(manager, &search); added; added = NextNewPlayer(manager, &search)) {
        if (added[0x10] == msg[0]) {
            Prop<uint32_t>(added, 0x20) |= 0x80;
            break;
        }
    }
    return kContinue;
}

// 0x00531950
int __cdecl HandleSelectedGoals(NetLink::InboundMsgCtx* context)
{
    const uint8_t* msg = Message(context);
    uint8_t* goals = ObjectiveBoard();
    Call(CodeAddress(&GoalSystem::CGoalManager::UnpickEveryObjective), goals);
    uint8_t show = msg[0];
    const uint8_t* name = msg + 1;
    for (uint32_t goal = Prop<uint32_t>(name, 0); goal; goal = Prop<uint32_t>(name, 0)) {
        name += 4;
        uint8_t* entry = GetGoal(goals, goal);
        reinterpret_cast<void(__thiscall*)(uint8_t*)>(Prop<void**>(entry, 0)[0x74 / 4])(entry); // select it
        Call(CodeAddress(&GoalSystem::CObjective::ChooseOne), entry);
    }
    if (show == 1) {
        Scripts::CStruct* params = NewParams();
        params->PutNameKey(kGoalSummary, kGoalSummary);
        Scripts::SpawnScript(kViewSelectedGoalsMenu, params, 0, nullptr, 0, 0, false, false, false, false);
        DeleteParams(params);
    }
    return kContinue;
}

// 0x00531A80
int __cdecl HandleJoinedTeam(NetLink::InboundMsgCtx* context)
{
    PlayerSearch search;
    Hub* manager = ManagerOf(context);
    const uint8_t* msg = Message(context);
    uint8_t* player = FindPlayerSlotByObj(manager, msg);
    if (!player)
        return kContinue;
    Prop<int>(player, 0x30) = static_cast<int8_t>(msg[1]);
    char name[64];
    TeamNameKey(name, static_cast<int8_t>(msg[1]));
    Scripts::CStruct* params = NewParams();
    if (IsOwnPlayer(player)) {
        if (IsSquadMatch()) {
            params->AppendWord(kString0, Scripts::QBVAL_STRING, Scripts::FetchText(name));
            Run(kJoinedTeamYou, params);
        }
    } else if (IsSquadMatch()) {
        params->AppendWord(kString0, Scripts::QBVAL_STRING, PlayerName(player));
        params->AppendWord(kString1, Scripts::QBVAL_STRING, Scripts::FetchText(name));
        Run(kJoinedTeamOther, params);
    }
    DeleteParams(params);
    // Everyone else on the same team: a warning.
    int others = 0;
    bool sameTeam = true;
    for (uint8_t* other = StartRosterSweep(manager, &search, false); other; other = StepRosterSweep(manager, &search, false)) {
        if (other == player)
            continue;
        ++others;
        if (static_cast<int8_t>(msg[1]) != PlayerTeam(other))
            sameTeam = false;
    }
    if (sameTeam && others > 0 && Call<int>(CodeAddress(&GoalSystem::CMatchRules::NumTeams), GameMode()) > 0)
        Run(kWarnAllSameTeam);
    return kContinue;
}

// 0x00531C80
int __cdecl HandleGoalCompleted(NetLink::InboundMsgCtx* context)
{
    const uint8_t* msg = Message(context);
    Hub* manager = ManagerOf(context);
    uint8_t* goals = ObjectiveBoard();
    uint8_t* goal = GetGoal(goals, Prop<uint32_t>(msg, 4));
    uint8_t* current = Call<uint8_t*>(CodeAddress(&GoalSystem::CGoalManager::GetEngagedObjective), goals, 0);
    if (!goal)
        return kContinue;
    auto virtualCall = [](uint8_t* object, uint32_t offset) {
        return reinterpret_cast<void**>(Prop<void**>(object, 0))[offset / 4];
    };
    uint8_t* player = FindPlayerSlotByObj(manager, msg);
    uint8_t* local = LocalPlayer(manager);
    reinterpret_cast<void(__thiscall*)(uint8_t*, int)>(virtualCall(goal, 0x70))(goal, PlayerTeam(player));
    if (!PlayerSkater(player))
        return kContinue;
    reinterpret_cast<void(__thiscall*)(uint8_t*, uint32_t)>(virtualCall(goal, 0x6C))(goal, SkaterId(PlayerSkater(player)));
    if (local == player) {
        int total = Call<int>(CodeAddress(&GoalSystem::CGoalManager::CountPickedObjectives), goals);
        int done = Call<int>(CodeAddress(&GoalSystem::CGoalManager::CountObjectivesCleared), goals);
        if (total > done) {
            Scripts::CStruct* params = NewParams();
            params->PutInt(kNumGoalsLeft, total - done);
            Run(kCompletedGoal, params);
            DeleteParams(params);
        }
        Call(CodeAddress(&GoalSystem::CObjectiveNpc::RemoveObjectiveNpc), Prop<void*>(goal, 0x14));
        return kContinue;
    }
    if (current) {
        Call(CodeAddress(&GoalSystem::CObjective::GetTopObjectiveKey), current);
        Call(CodeAddress(&GoalSystem::CObjective::GetTopObjectiveKey), goal);
    }
    if (!IsSquadMatch() || PlayerTeam(local) != PlayerTeam(player))
        return kContinue;
    // A teammate's: the goal (or the last of its chain) ended here too.
    uint32_t* next = Prop<uint32_t*>(goal, 0x28);
    if (*next) {
        uint8_t* last = goal;
        while (next && *next) {
            last = GetGoal(goals, *next);
            next = Prop<uint32_t*>(last, 0x28);
        }
        reinterpret_cast<void(__thiscall*)(uint8_t*)>(virtualCall(last, 0x3C))(last);
        if (last != current)
            reinterpret_cast<void(__thiscall*)(uint8_t*, int, int)>(virtualCall(current, 0x34))(current, 0, 1);
    } else {
        reinterpret_cast<void(__thiscall*)(uint8_t*)>(virtualCall(goal, 0x3C))(goal);
    }
    Call(CodeAddress(&GoalSystem::CObjectiveNpc::RemoveObjectiveNpc), Prop<void*>(current, 0x14));
    reinterpret_cast<void(__thiscall*)(uint8_t*)>(virtualCall(goal, 0x68))(goal);
    int total = Call<int>(CodeAddress(&GoalSystem::CGoalManager::CountPickedObjectives), goals);
    int done = Call<int>(CodeAddress(&GoalSystem::CGoalManager::CountObjectivesCleared), goals);
    if (total > done) {
        const char* text = nullptr;
        Call(CodeAddress(&GoalSystem::CObjective::GetObjectiveListCaption), goal, &text);
        Scripts::CStruct* params = NewParams();
        params->PutInt(kNumGoalsLeft, total - done);
        params->AppendWord(kPlayerName, PlayerName(player));
        params->AppendWord(kGoalText, text);
        Run(kCompletedGoalSameTeam, params);
        DeleteParams(params);
    }
    return kContinue;
}

// 0x00531F10
int __cdecl HandleGoalStarted(NetLink::InboundMsgCtx* context)
{
    const uint8_t* msg = Message(context);
    Hub* manager = ManagerOf(context);
    uint8_t* goal = GetGoal(ObjectiveBoard(), Prop<uint32_t>(msg, 4));
    if (!goal)
        return kContinue;
    const char* text = nullptr;
    uint8_t* player = FindPlayerSlotByObj(manager, msg);
    if (PlayerTeam(LocalPlayer(manager)) != PlayerTeam(player))
        return kContinue;
    Call(CodeAddress(&GoalSystem::CObjective::GetObjectiveListCaption), goal, &text);
    Scripts::CStruct* params = NewParams();
    params->AppendWord(kPlayerName, PlayerName(player));
    params->AppendWord(kGoalText, text);
    Run(kStartedGoalSameTeam, params);
    DeleteParams(params);
    return kContinue;
}

// 0x00532280
int __cdecl HandleEliminated(NetLink::InboundMsgCtx* context)
{
    if (context->m_typeCode != kMsgEliminated)
        return kContinue;
    int id = static_cast<int>(MessageWord(context, 0));
    if (id < 0) {
        Run(kEliminatedNone);
        return kContinue;
    }
    uint8_t* player = FindPlayerSlotByObj(ManagerOf(context), static_cast<uint16_t>(id));
    if (!player)
        return kContinue;
    uint8_t* skate = Skate();
    AsPlayer(player)->MarkEliminated();
    Call(CodeAddress(&GameModules::Skate::ConcealSkater), skate, PlayerSkater(player), 1);
    if (IsOwnPlayer(player)) {
        Run(kEliminatedYou);
        return kContinue;
    }
    Scripts::CStruct* params = NewParams();
    params->AppendWord(kString0, PlayerName(player));
    Run(kEliminatedOther, params);
    DeleteParams(params);
    return kContinue;
}

// 0x00532390
int __cdecl HandleObserversFull(NetLink::InboundMsgCtx*)
{
    Scripts::CStruct* params = NewParams();
    params->PutNameKey(kReason, kFullObservers);
    params->PutNameKey(kJustDialog, 0);
    Run(kCreateJoinRefusedDialog, params);
    DeleteParams(params);
    return kContinue;
}

} // namespace OnlinePlay
