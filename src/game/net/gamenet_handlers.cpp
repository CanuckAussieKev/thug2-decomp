// The network manager's message handlers (GameNet::Manager) and their helpers. See gamenet_handlers.h.

#include "game/net/gamenet_handlers.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/task.h"
#include "game/ed/park_editor.h"
#include "game/net/gamenet_game_cfuncs.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/stats_man.h"
#include "game/obj/object.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/skate/skate_net.h"
#include "game/util/bitstream.h"
#include "game/script/game_cfuncs.h"
#include "game/ed/rail_editor.h"
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
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"

namespace GameNet {

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

static VtableSlot kPlayerSearchVtable_slots[] = {&GameNet::PlayerSearchDestructors::Destroy};
void** const kPlayerSearchVtable = reinterpret_cast<void**>(kPlayerSearchVtable_slots);
static VtableSlot kNewPlayerSearchVtable_slots[] = {&GameNet::NewPlayerSearchDestructors::Destroy};
void** const kNewPlayerSearchVtable = reinterpret_cast<void**>(kNewPlayerSearchVtable_slots);

// 0x0052DB60 / 0x0052DB70
void PlayerSearchDestructors::DestroyBody() { m_vtable = kPlayerSearchVtable; }
void NewPlayerSearchDestructors::DestroyBody() { m_vtable = kNewPlayerSearchVtable; }

// 0x0052F730
PlayerSearchDestructors* PlayerSearchDestructors::Destroy(unsigned int flags)
{
    m_vtable = kPlayerSearchVtable;
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0052F760
NewPlayerSearchDestructors* NewPlayerSearchDestructors::Destroy(unsigned int flags)
{
    m_vtable = kNewPlayerSearchVtable;
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0052D7B0
int __cdecl HandleReadyQuery(Net::MsgHandlerContext* context)
{
    ManagerOf(context)->m_latestReadyQuery = MessageWord(context, 0);
    return kContinue;
}

// 0x0052D7D0
int __cdecl HandleReadyResponse(Net::MsgHandlerContext* context)
{
    context->m_Acknowledge = true;
    if (uint8_t* player = PlayerByConnection(ManagerOf(context), context->m_Conn))
        AsPlayer(player)->ReceiveReadyResponse(MessageWord(context, 0));
    return kContinue;
}

// 0x0052D800
int __cdecl HandleObserve(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    CFunc_ExitSurveyorMode(nullptr, nullptr); // (the script command, without parameters)
    manager->EnterObserveMode();
    return kContinue;
}

// 0x0052D830
int __cdecl HandleFlushImportant(Net::MsgHandlerContext* context)
{
    if (context->m_Conn->IsRemote())
        context->m_Conn->DestroyImportantMessages();
    return kContinue;
}

namespace {
// msg[0] the player's object id, msg[1] the team (both signed bytes).
int FlagHandler(Net::MsgHandlerContext* context, void (PlayerInfo::*method)(int))
{
    const uint8_t* msg = Message(context);
    if (uint8_t* player = PlayerByObjectId(ManagerOf(context), msg))
        (AsPlayer(player)->*method)(static_cast<int>(static_cast<int8_t>(msg[1])));
    return kContinue;
}
} // namespace

// 0x0052D850 / 0x0052D880 / 0x0052D8B0
int __cdecl HandleStoleFlag(Net::MsgHandlerContext* context) { return FlagHandler(context, &PlayerInfo::StoleFlag); }
int __cdecl HandleTookFlag(Net::MsgHandlerContext* context) { return FlagHandler(context, &PlayerInfo::TookFlag); }
int __cdecl HandleCapturedFlag(Net::MsgHandlerContext* context) { return FlagHandler(context, &PlayerInfo::CapturedFlag); }

// 0x0052D8E0
int __cdecl HandleRetrievedFlag(Net::MsgHandlerContext* context)
{
    if (uint8_t* player = PlayerByObjectId(ManagerOf(context), Message(context)))
        AsPlayer(player)->RetrievedFlag();
    return kContinue;
}

// 0x0052D910 / 0x0052D930
int __cdecl HandleAutoServerNotice(Net::MsgHandlerContext*)
{
    Run(kLaunchAutoServerNotification);
    return kContinue;
}
int __cdecl HandleKillTeamFlags(Net::MsgHandlerContext*)
{
    Run(kKillTeamFlags);
    return kContinue;
}

// 0x0052D950
int __cdecl HandlePanelMessage(Net::MsgHandlerContext* context)
{
    const char* msg = context->m_Msg;
    // 0x005376F0: (broadcast, text, string 1, string 2, object, properties, important, time)
    ManagerOf(context)->CreateNetPanelMessage(false, MessageWord(context, 4), msg + 0x0C, msg + 0x1C, nullptr, nullptr,
                                              false, static_cast<int>(MessageWord(context, 8)));
    return kContinue;
}

// 0x0052D980
int __cdecl HandleObjectUpdateLength(Net::MsgHandlerContext* context)
{
    Util::BitStream stream;
    stream.SetInputData(context->m_Msg, 0x400);
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
    context->m_MsgLength = stream.GetByteCount();
    return kContinue;
}

// 0x0052DAC0
int __cdecl HandleNothing(Net::MsgHandlerContext*) { return kContinue; }

// 0x0052DAD0
int __cdecl HandleCdKeyChallenge(Net::MsgHandlerContext* context)
{
    Net::MsgDesc desc;
    desc.Construct();
    Manager* manager = ManagerOf(context);
    const char* password = manager->GetNetworkPreferences()->GetPreferenceString(kProfilePassword, kUiString);
    char answer[36];
    // 0x0054CA10: the answer to the challenge (the message itself). (Static: the original passes the online service,
    // +0x1C, in ECX, unused.)
    StatsMan::GenerateAuth(context->m_Msg, password, answer);
    desc.m_id = kMsgCdKeyAnswer;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    desc.m_data = answer;
    desc.m_length = kCdKeyAnswerSize;
    SendToServer(context->m_App, &desc);
    return kContinue;
}

// 0x0052DCA0
int __cdecl HandleKeepAlive(Net::MsgHandlerContext* context)
{
    context->m_Conn->UpdateCommTime(static_cast<int8_t>(Message(context)[0]) * 1000);
    return kContinue;
}

// 0x0052DCC0
int __cdecl HandleJoinAccepted(Net::MsgHandlerContext* context)
{
    uint8_t* skate = Skate();
    Net::MsgDesc desc;
    desc.Construct();
    Manager* manager = ManagerOf(context);
    manager->SetJoinState(6);
    desc.m_id = kMsgReadyToReceive;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToServer(context->m_App, &desc);
    if (!manager->OnServer()) {
        for (int i = 0; i < 2; ++i) {
            if (Net::App* client = manager->GetClient(i))
                reinterpret_cast<Mdl::SkateNet*>(skate)->AddClientHandlers(client, i); // the skate module's handlers for that client
        }
    }
    return kContinue;
}

// 0x0052E1A0
int __cdecl HandleReadyToPlay(Net::MsgHandlerContext* context)
{
    auto* logicTasks = reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager) + 0x04);
    Manager* manager = ManagerOf(context);
    manager->SetReadyToPlay(true); // (m_readyToPlay set, the skaters told)
    if (!context->m_App->IsLocal())
        logicTasks->AddTask(manager->m_clientAddNewPlayersTask);
    return kContinue;
}

// 0x0052EB70
int __cdecl HandleParkMap(Net::MsgHandlerContext* context)
{
    Ed::CParkEditor::Instance()->SetCompressedMapBuffer(reinterpret_cast<const Ed::CompressedMap*>(context->m_Msg), true);
    void* map = Ed::CParkEditor::Instance()->GetCompressedMapBuffer(false);
    Crc::GenerateCRCFromBuffer(map, kParkMapSize); // (not used)
    return kContinue;
}

// 0x0052ED80
int __cdecl HandleGameOver(Net::MsgHandlerContext* context)
{
    ManagerOf(context)->SetGameOver();
    if (CFuncs::CFunc_IsObserving(nullptr, nullptr) && GameType() == kNetGoalAttack)
        Run(kCreateRankings); // (IsObserving)
    return kContinue;
}

// 0x0052EDD0
int __cdecl HandleGameEnded(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    Obj::g_trickObjectManager->RemoveAllClusters();
    Run(kCreateGameEndedDialog);
    manager->ResetPlayers();
    return kContinue;
}

// 0x0052F2D0
int __cdecl HandleChooseTeam(Net::MsgHandlerContext* context)
{
    uint8_t* skate = Skate();
    int teams = static_cast<int8_t>(Message(context)[0]);
    if (teams <= 0 || IsTeamGame()) {
        Call(CodeAddress(&Game::CGameMode::SetNumTeams), GameMode(), teams);
        return kContinue;
    }
    Call(CodeAddress(&Game::CGameMode::SetNumTeams), GameMode(), teams);
    Run(kChooseTeamMessage);
    if (Call<Obj::CObject*>(CodeAddress(Mdl::Skate::GetLocalSkaterStub), skate)) { // the local skater
        Run(kPrepareSkaterForMove, nullptr, Call<Obj::CObject*>(CodeAddress(Mdl::Skate::GetLocalSkaterStub), skate));
        Call(CodeAddress(&Mdl::Skate::MoveToRestart), skate, Call<Obj::CObject*>(CodeAddress(Mdl::Skate::GetLocalSkaterStub), skate), 0, 0);
    }
    return kContinue;
}

// 0x0052F370
int __cdecl HandleChecksumRequest(Net::MsgHandlerContext* context)
{
    uint8_t* skate = Skate();
    Net::MsgDesc desc;
    desc.Construct();
    char* msg = context->m_Msg;
    *reinterpret_cast<uint32_t*>(msg + 4) = Call<uint32_t>(CodeAddress(&Mdl::Skate::GetCheats), skate);
    desc.m_groupId = kGroupGame;
    desc.m_length = 8;
    desc.m_id = kMsgChecksum;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_data = msg;
    SendToServer(context->m_App, &desc);
    return kContinue;
}

// 0x0052EBB0
int __cdecl HandleTeamObject(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    if (!IsTeamGame())
        return kContinue;
    uint8_t* player = PlayerByConnection(manager, context->m_Conn);
    if (!player)
        return kDone;
    uint32_t object = MessageWord(context, 0);
    auto* objects = &Field<uint32_t>(manager, 0x70 + 0x14 * Field<int>(player, 0x30)); // five per team
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
int __cdecl HandleCdKeyAnswer(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    uint8_t* player = PlayerByConnection(manager, context->m_Conn);
    uint32_t id = Field<uint32_t>(Field<void*>(player, 0x14), 0x14); // its skater's id
    Field<StatsMan*>(manager, 0x1C)->SetPlayerAuth(static_cast<int>(id), context->m_Msg);
    return kContinue;
}

// 0x0052F400
int __cdecl HandleCreatedDataRequest(Net::MsgHandlerContext* context)
{
    uint8_t id = context->m_MsgId;
    Net::App* app = context->m_App;
    Manager* manager = ManagerOf(context);
    if (id == kMsgRequestRails) {
        int handle = context->m_Conn->m_handle;
        auto railEditor = [] { return Ed::CRailEditorComponent::Instance(); };
        railEditor(); // (looked up once for nothing)
        void* rails = Call<void*>(CodeAddress(&Ed::CRailEditorComponent::GetPacked), railEditor()); // the rails, packed
        app->StreamMessage(handle, kMsgRailsData, kRailDataSize, rails, "rail data", kGroupGame, false, true);
    } else if (id == kMsgRequestGoals && manager->HasCreatedGoals()) {
        int handle = context->m_Conn->m_handle;
        void* goals = manager->GetCreatedGoalsMessage();
        auto size = static_cast<uint16_t>(manager->GetCreatedGoalsSize());
        app->StreamMessage(handle, kMsgGoalsData, size, goals, "goals data", kGroupGame, false, true);
    }
    return kContinue;
}

} // namespace GameNet
