// The network manager's message handlers (OnlinePlay::Hub) and their helpers. See gamenet_handlers.h.

#include "game/net/gamenet_handlers.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/task_list.h"
#include "game/ed/park_editor.h"
#include "game/net/gamenet_game_cfuncs.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/stats_man.h"
#include "game/obj/entity.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/skate/skate_net.h"
#include "game/util/bitstream.h"
#include "game/script/game_cfuncs.h"
#include "game/ed/rail_editor.h"
#include "game/core/code_address.h"
#include "game/ass/resource_registry.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/rig.h"
#include "game/ed/goal_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"

namespace OnlinePlay {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kLaunchAutoServerNotification = 0x6553898E; // launch_auto_server_notification
constexpr uint32_t kKillTeamFlags = 0xEBB3642E;                 // Kill_Team_Flags
constexpr uint32_t kProfilePassword = 0x3357D89B;               // profile_password
constexpr uint32_t kUiString = 0x96875C0F;                      // ui_string
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;                 // netgoalattack
constexpr uint32_t kCreateRankings = 0xFCE4F72D;                // create_rankings
constexpr uint32_t kCreateGameEndedDialog = 0x21ECD114;         // create_game_ended_dialog
constexpr uint32_t kChooseTeamMessage = 0x751F873A;             // ChooseTeamMessage
constexpr uint32_t kPrepareSkaterForMove = 0xF1B110ED;          // PrepareSkaterForMove

// Messages.
constexpr uint8_t kMsgReadyToReceive = 0x54;
constexpr uint8_t kMsgCdKeyAnswer = 0x7D;
constexpr uint8_t kMsgChecksum = 0x81;
constexpr uint8_t kMsgRequestRails = 0x8B;
constexpr uint8_t kMsgRequestGoals = 0x8A;
constexpr uint8_t kMsgGoalsData = 0x87;
constexpr uint8_t kMsgRailsData = 0x88;

constexpr int kCdKeyAnswerSize = 0x21;
constexpr uint16_t kRailDataSize = 0x1132;
constexpr int kParkMapSize = 15000;

} // namespace

// 0x0052D6A0
void __cdecl GetCdKey(char* key)
{
    static const char kPath[] = "Software\\Activision\\Tony Hawk's Underground 2";
    char value[100] = {};
    DWORD size = sizeof(value);
    DWORD type;
    HKEY handle;
    auto query = [&] {
        RegQueryValueExA(handle, "key", nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
    };
    bool opened;
    if (RegCreateKeyA(HKEY_CURRENT_USER, kPath, &handle) == ERROR_SUCCESS &&
        RegQueryValueExA(handle, "key", nullptr, &type, reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS) {
        opened = true;
    } else {
        // (an opened HKEY_CURRENT_USER key without the value is left open: the original's)
        opened = RegCreateKeyA(HKEY_LOCAL_MACHINE, kPath, &handle) == ERROR_SUCCESS;
        if (opened)
            query();
    }
    if (opened)
        RegCloseKey(handle);
    // (a value filling all 100 bytes has no terminator: the original's strcpy runs on into its stack - here it stops)
    size_t length = strnlen(value, sizeof(value));
    std::memcpy(key, value, length);
    key[length] = '\0';
}

static VtableSlot kPlayerSearchVtable_slots[] = {&OnlinePlay::PlayerSearchDestructors::Teardown};
void** const kPlayerSearchVtable = reinterpret_cast<void**>(kPlayerSearchVtable_slots);
static VtableSlot kNewPlayerSearchVtable_slots[] = {&OnlinePlay::NewPlayerSearchDestructors::Teardown};
void** const kNewPlayerSearchVtable = reinterpret_cast<void**>(kNewPlayerSearchVtable_slots);

// 0x0052DB60 / 0x0052DB70
void PlayerSearchDestructors::DestroyBody() { m_vtable = kPlayerSearchVtable; }
void NewPlayerSearchDestructors::DestroyBody() { m_vtable = kNewPlayerSearchVtable; }

// 0x0052F730
PlayerSearchDestructors* PlayerSearchDestructors::Teardown(unsigned int flags)
{
    m_vtable = kPlayerSearchVtable;
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0052F760
NewPlayerSearchDestructors* NewPlayerSearchDestructors::Teardown(unsigned int flags)
{
    m_vtable = kNewPlayerSearchVtable;
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0052D7B0
int __cdecl HandleReadyQuery(NetLink::InboundMsgCtx* context)
{
    ManagerOf(context)->m_latestReadyQuery = MessageWord(context, 0);
    return kContinue;
}

// 0x0052D7D0
int __cdecl HandleReadyResponse(NetLink::InboundMsgCtx* context)
{
    context->m_Acknowledge = true;
    if (uint8_t* player = PlayerByConnection(ManagerOf(context), context->m_senderConn))
        AsPlayer(player)->ReceiveReadyResponse(MessageWord(context, 0));
    return kContinue;
}

// 0x0052D800
int __cdecl HandleObserve(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    CFunc_ExitSurveyorMode(nullptr, nullptr); // (the script command, without parameters)
    manager->EnterObserveMode();
    return kContinue;
}

// 0x0052D830
int __cdecl HandleFlushImportant(NetLink::InboundMsgCtx* context)
{
    if (context->m_senderConn->IsOffMachine())
        context->m_senderConn->DestroyImportantMessages();
    return kContinue;
}

namespace {
// msg[0] the player's object id, msg[1] the team (both signed bytes).
int FlagHandler(NetLink::InboundMsgCtx* context, void (PlayerRecord::*method)(int))
{
    const uint8_t* msg = Message(context);
    if (uint8_t* player = PlayerByObjectId(ManagerOf(context), msg))
        (AsPlayer(player)->*method)(static_cast<int>(static_cast<int8_t>(msg[1])));
    return kContinue;
}
} // namespace

// 0x0052D850 / 0x0052D880 / 0x0052D8B0
int __cdecl HandleStoleFlag(NetLink::InboundMsgCtx* context) { return FlagHandler(context, &PlayerRecord::NoteFlagSteal); }
int __cdecl HandleTookFlag(NetLink::InboundMsgCtx* context) { return FlagHandler(context, &PlayerRecord::NoteFlagPickup); }
int __cdecl HandleCapturedFlag(NetLink::InboundMsgCtx* context) { return FlagHandler(context, &PlayerRecord::ScoreFlagCapture); }

// 0x0052D8E0
int __cdecl HandleRetrievedFlag(NetLink::InboundMsgCtx* context)
{
    if (uint8_t* player = PlayerByObjectId(ManagerOf(context), Message(context)))
        AsPlayer(player)->NoteFlagReturned();
    return kContinue;
}

// 0x0052D910 / 0x0052D930
int __cdecl HandleAutoServerNotice(NetLink::InboundMsgCtx*)
{
    Run(kLaunchAutoServerNotification);
    return kContinue;
}
int __cdecl HandleKillTeamFlags(NetLink::InboundMsgCtx*)
{
    Run(kKillTeamFlags);
    return kContinue;
}

// 0x0052D950
int __cdecl HandlePanelMessage(NetLink::InboundMsgCtx* context)
{
    const char* msg = context->m_body;
    // 0x005376F0: (broadcast, text, string 1, string 2, object, properties, important, time)
    ManagerOf(context)->BuildNetNotice(false, MessageWord(context, 4), msg + 0x0C, msg + 0x1C, nullptr, nullptr,
                                              false, static_cast<int>(MessageWord(context, 8)));
    return kContinue;
}

// 0x0052D980
int __cdecl HandleObjectUpdateLength(NetLink::InboundMsgCtx* context)
{
    Util::PackedBitBuffer stream;
    stream.ChangeIntakeContents(context->m_body, 0x400);
    uint32_t present = stream.ReadBits(8) & 0xFF;
    for (int skater = 0; skater < 8; ++skater) {
        if (!(present & (1u << skater)))
            continue;
        stream.ReadBits(16); // the time
        uint32_t flags = stream.ReadBits(9);
        for (int field = 0; field < 6; ++field) { // the position and the rotation
            if (flags & (1u << field))
                stream.ReadSignedBits(16);
        }
        if (flags & 0x40) {
            stream.ReadBits(4);
            stream.ReadBits(6);
            stream.ReadBits(1);
            stream.ReadBits(1);
        }
        if (flags & 0x80)
            stream.ReadBits(8);
        if (flags & 0x100)
            stream.ReadSignedBits(32);
    }
    context->m_bodySize = stream.GetByteCount();
    return kContinue;
}

// 0x0052DAC0
int __cdecl HandleNothing(NetLink::InboundMsgCtx*) { return kContinue; }

// 0x0052DAD0
int __cdecl HandleCdKeyChallenge(NetLink::InboundMsgCtx* context)
{
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    Hub* manager = ManagerOf(context);
    const char* password = manager->GetOnlineSettings()->GetPreferenceString(kProfilePassword, kUiString);
    char answer[36];
    // 0x0054CA10: the answer to the challenge (the message itself). (Static: the original passes the online service,
    // +0x1C, in ECX, unused.)
    RankingsManager::GenerateAuth(context->m_body, password, answer);
    desc.m_ident = kMsgCdKeyAnswer;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    desc.m_content = answer;
    desc.m_byteCount = kCdKeyAnswerSize;
    SendToServer(context->m_receiverApp, &desc);
    return kContinue;
}

// 0x0052DCA0
int __cdecl HandleKeepAlive(NetLink::InboundMsgCtx* context)
{
    context->m_senderConn->StampLastHeard(static_cast<int8_t>(Message(context)[0]) * 1000);
    return kContinue;
}

// 0x0052DCC0
int __cdecl HandleJoinAccepted(NetLink::InboundMsgCtx* context)
{
    uint8_t* skate = Skate();
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    Hub* manager = ManagerOf(context);
    manager->SetEntryPhase(6);
    desc.m_ident = kMsgReadyToReceive;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToServer(context->m_receiverApp, &desc);
    if (!manager->OnServer()) {
        for (int i = 0; i < 2; ++i) {
            if (NetLink::AppHost* client = manager->FetchPatron(i))
                reinterpret_cast<GameModules::SkateNet*>(skate)->AddClientHandlers(client, i); // the skate module's handlers for that client
        }
    }
    return kContinue;
}

// 0x0052E1A0
int __cdecl HandleReadyToPlay(NetLink::InboundMsgCtx* context)
{
    auto* logicTasks = reinterpret_cast<TaskList::PileUp*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 0x04);
    Hub* manager = ManagerOf(context);
    manager->SetReadyState(true); // (m_readyToPlay set, the skaters told)
    if (!context->m_receiverApp->IsHostLocal())
        logicTasks->QueueJob(manager->m_clientAddNewPlayersTask);
    return kContinue;
}

// 0x0052EB70
int __cdecl HandleParkMap(NetLink::InboundMsgCtx* context)
{
    ParkEditing::CParkBuilderMode::Sole()->SetPackedParkData(reinterpret_cast<const ParkEditing::CompressedMap*>(context->m_body), true);
    void* map = ParkEditing::CParkBuilderMode::Sole()->GetPackedParkData(false);
    Checksums::GenerateCRCFromBuffer(map, kParkMapSize); // (not used)
    return kContinue;
}

// 0x0052ED80
int __cdecl HandleGameOver(NetLink::InboundMsgCtx* context)
{
    ManagerOf(context)->SetGameOver();
    if (ScriptCommands::CFunc_IsObserving(nullptr, nullptr) && GameType() == kNetGoalAttack)
        Run(kCreateRankings); // (IsObserving)
    return kContinue;
}

// 0x0052EDD0
int __cdecl HandleGameEnded(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    GameObjects::g_trickObjectManager->RemoveAllClusters();
    Run(kCreateGameEndedDialog);
    manager->ClearRoster();
    return kContinue;
}

// 0x0052F2D0
int __cdecl HandleChooseTeam(NetLink::InboundMsgCtx* context)
{
    uint8_t* skate = Skate();
    int teams = static_cast<int8_t>(Message(context)[0]);
    if (teams <= 0 || IsSquadMatch()) {
        Call(CodeAddress(&GoalSystem::CMatchRules::SetNumTeams), GameMode(), teams);
        return kContinue;
    }
    Call(CodeAddress(&GoalSystem::CMatchRules::SetNumTeams), GameMode(), teams);
    Run(kChooseTeamMessage);
    if (Call<GameObjects::CEntity*>(CodeAddress(GameModules::Skate::GetLocalSkaterStub), skate)) { // the local skater
        Run(kPrepareSkaterForMove, nullptr, Call<GameObjects::CEntity*>(CodeAddress(GameModules::Skate::GetLocalSkaterStub), skate));
        Call(CodeAddress(&GameModules::Skate::TeleportToSpawn), skate, Call<GameObjects::CEntity*>(CodeAddress(GameModules::Skate::GetLocalSkaterStub), skate), 0, 0);
    }
    return kContinue;
}

// 0x0052F370
int __cdecl HandleChecksumRequest(NetLink::InboundMsgCtx* context)
{
    uint8_t* skate = Skate();
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    char* msg = context->m_body;
    *reinterpret_cast<uint32_t*>(msg + 4) = Call<uint32_t>(CodeAddress(&GameModules::Skate::GetCheats), skate);
    desc.m_groupId = kGroupGame;
    desc.m_byteCount = 8;
    desc.m_ident = kMsgChecksum;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_content = msg;
    SendToServer(context->m_receiverApp, &desc);
    return kContinue;
}

// 0x0052EBB0
int __cdecl HandleTeamObject(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    if (!IsSquadMatch())
        return kContinue;
    uint8_t* player = PlayerByConnection(manager, context->m_senderConn);
    if (!player)
        return kDone;
    uint32_t object = MessageWord(context, 0);
    auto* objects = &Prop<uint32_t>(manager, 0x70 + 0x14 * Prop<int>(player, 0x30)); // five per team
    for (int i = 0; i < 5; ++i) {
        if (!objects[i]) {
            objects[i] = object;
            return kContinue;
        }
        if (objects[i] == object)
            return kDone;
    }
    return kContinue;
}

// 0x0052F3D0 (no check that the sender is a player: the original's)
int __cdecl HandleCdKeyAnswer(NetLink::InboundMsgCtx* context)
{
    Hub* manager = ManagerOf(context);
    uint8_t* player = PlayerByConnection(manager, context->m_senderConn);
    uint32_t id = Prop<uint32_t>(Prop<void*>(player, 0x14), 0x14); // its skater's id
    Prop<RankingsManager*>(manager, 0x1C)->SetPlayerAuth(static_cast<int>(id), context->m_body);
    return kContinue;
}

// 0x0052F400
int __cdecl HandleCreatedDataRequest(NetLink::InboundMsgCtx* context)
{
    uint8_t id = context->m_typeCode;
    NetLink::AppHost* app = context->m_receiverApp;
    Hub* manager = ManagerOf(context);
    if (id == kMsgRequestRails) {
        int handle = context->m_senderConn->m_hdl;
        auto railEditor = [] { return ParkEditing::CRailAuthoring::Sole(); };
        railEditor(); // (looked up once for nothing)
        void* rails = Call<void*>(CodeAddress(&ParkEditing::CRailAuthoring::GetPacked), railEditor()); // the rails, packed
        app->SendBulkMessage(handle, kMsgRailsData, kRailDataSize, rails, "rail data", kGroupGame, false, true);
    } else if (id == kMsgRequestGoals && manager->HasCreatedGoals()) {
        int handle = context->m_senderConn->m_hdl;
        void* goals = manager->GetCreatedGoalsMessage();
        auto size = static_cast<uint16_t>(manager->GetCreatedGoalsSize());
        app->SendBulkMessage(handle, kMsgGoalsData, size, goals, "goals data", kGroupGame, false, true);
    }
    return kContinue;
}

} // namespace OnlinePlay
