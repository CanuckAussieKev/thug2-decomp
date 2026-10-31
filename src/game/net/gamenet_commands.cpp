// The network manager's script commands, first part (0x00535DA0-0x00536460): capture the flag's task, the game's
// state, the join mode, teams, the lobby's peer, the message of the day, and the first-come-first-served requests a
// client sends the server (start the game, change the level, toggle a pro set flag or a goal).

#include <cstdint>
#include "game/core/code_address.h"

#include "game/core/arena_alloc.h"
#include "game/net/gamespy.h"
#include "game/core/task_list.h"
#include "game/ed/park_editor.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/ed/rail_editor.h"
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
#include "game/net/stats_man.h"

namespace OnlinePlay {

using namespace Detail;
using Scripts::CScript;
using Scripts::CStruct;

uint32_t g_netGameStartTime;

namespace {

constexpr uint32_t kCtfTask = 0xF8;          // TaskList::SchedulerTask*: capture the flag's
constexpr uint32_t kChangeLevelTask = 0xEC;  // TaskList::SchedulerTask*: the level change's
constexpr uint32_t kMotdCancelled = 0xFE08;  // bool
constexpr uint32_t kMotdReceived = 0xFE09;   // bool
constexpr uint32_t kServicesState = 0xFE0A;  // uint8_t (1: available)

// Messages to the server.
constexpr uint8_t kMsgStartGameRequest = 0x60;
constexpr uint8_t kMsgChangeLevelRequest = 0x62;
constexpr uint8_t kMsgToggleProSetRequest = 0x63;
constexpr uint8_t kMsgToggleGoalRequest = 0x64;
constexpr uint8_t kMsgParkMap = 0x6C;
constexpr uint8_t kMsgRailsData = 0x88;
constexpr uint16_t kParkMapSize = 15000;
constexpr uint16_t kRailsDataSize = 4402;

// Checksums.
constexpr uint32_t kChecksum = 0x21902065;            // checksum
constexpr uint32_t kBooleanTrue = 0xF81BC89B;         // boolean_true
constexpr uint32_t kDisplayFlagBaseWarning = 0x55AF7C02; // display_flag_base_warning
constexpr uint32_t kGameType = 0x9834C4FA;            // game_type
constexpr uint32_t kSkillLevel = 0x403DA0D0;          // skill_level
constexpr uint32_t kFireballDifficulty = 0xCE238F5D;  // fireball_difficulty
constexpr uint32_t kTimeLimit = 0xC51BF6E3;           // time_limit
constexpr uint32_t kTargetScore = 0x71CD53A2;         // target_score
constexpr uint32_t kPlayerCollision = 0x043EE978;     // player_collision
constexpr uint32_t kFriendlyFire = 0xE959C43A;        // friendly_fire
constexpr uint32_t kStopAtZero = 0x48E748B5;          // stop_at_zero
constexpr uint32_t kScoreReset = 0x162E8C3A;          // score_reset
constexpr uint32_t kCtfGameType = 0x15421B73;         // ctf_game_type
constexpr uint32_t kGoals = 0x38DBE1D0;               // goals
constexpr uint32_t kTeamGraffitiMode = 0xA8774BF7;    // team_graffiti_mode
constexpr uint32_t kLevel = 0x651533EC;               // level
constexpr uint32_t kCreatedPark = 0xB664035D;         // load_sk5ed_gameplay
constexpr uint32_t kName = 0xA1DC81F9;                // name
constexpr uint32_t kFlag = 0x2E0B1465;                // flag
constexpr uint32_t kSetFlag = 0x1BC80657;             // SetFlag
constexpr uint32_t kUnSetFlag = 0x1FF8F994;           // UnSetFlag
constexpr uint32_t kSignedDisclaimer = 0x633A324E;    // signed_disclaimer

const char* const kNumTeams = "num_teams";     // "num_teams"
const char* const kParkMapDesc = "level data";  // "level data"
const char* const kRailsDesc = "rail data";    // "rail data"
const char* const kBit = "bit";          // "bit"
const char* const kParamId = "param_id";      // "param_id"
const char* const kFirstProSetFlag = "FLAG_PROSET1_GEO_ON"; // "FLAG_PROSET1_GEO_ON"

constexpr int kProSetCount = 7;
constexpr uint32_t kFlagBaseWarningInterval = 3000; // ms
constexpr uint32_t kJustStartedTime = 5000;         // ms

uint32_t g_lastFlagBaseWarning;

uint32_t& LastFlagBaseWarning() { return g_lastFlagBaseWarning; }
uint32_t& NetGameStartTime() { return g_netGameStartTime; }

TaskList::PileUp* LogicTasks() { return reinterpret_cast<TaskList::PileUp*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 4); }
Hub* NetLink() { return g_gameNetManager; }

// The lobby (OnlinePlay's GameSpy peer side): its peer, whether it's in a group room; the lobby's state (the manager's
// SetServiceState, 0x005453E0) and services state (SetNextServiceState, 0x005451B0).
void* LobbyPeer(Hub* manager) { return static_cast<RoomManager*>(manager->m_lobbyMan)->GetLobbyHandle(); }
bool InGroupRoom(Hub* manager) { return static_cast<RoomManager*>(manager->m_lobbyMan)->InGroupRoom(); }
void SetLobbyState(Hub* manager, int state) { manager->SetServiceState(state); }
void SetServicesState(Hub* manager, int state) { manager->SetNextServiceState(state); }

// A message for the server through client 0, sequenced in the game's group.
void SendRequest(Hub* manager, uint8_t id, void* data, uint16_t length)
{
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    desc.m_ident = id;
    desc.m_content = data;
    desc.m_byteCount = length;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToServer(manager->m_clientApp, &desc);
}

} // namespace

// 0x00535DA0: StartCTFGame - capture the flag's task into the logic tasks.
bool __cdecl CFunc_StartCTFGame(CStruct*, CScript*)
{
    LogicTasks()->QueueJob(Prop<TaskList::SchedulerTask*>(NetLink(), kCtfTask));
    return true;
}

// 0x00535DC0: EndCTFGame - its task removed.
bool __cdecl CFunc_EndCTFGame(CStruct*, CScript*)
{
    Prop<TaskList::SchedulerTask*>(NetLink(), kCtfTask)->DropOut();
    return true;
}

// 0x00535DE0: DisplayFlagBaseWarning - display_flag_base_warning, at most every 3 seconds.
bool __cdecl CFunc_DisplayFlagBaseWarning(CStruct*, CScript*)
{
    if (FrameClock::GetTime() - LastFlagBaseWarning() > kFlagBaseWarningInterval) {
        Run(kDisplayFlagBaseWarning);
        LastFlagBaseWarning() = FrameClock::GetTime();
    }
    return true;
}

// 0x00535E20: NetworkGamePending - a start request is waiting for the server.
bool __cdecl CFunc_NetworkGamePending(CStruct*, CScript*) { return NetLink()->m_gamePending; }

// 0x00535E30: MarkGameOver.
bool __cdecl CFunc_MarkGameOver(CStruct*, CScript*)
{
    NetLink()->m_gameOver = true;
    return true;
}

// 0x00535E40: SetJoinMode <n> (an unnamed integer: 0 to play, anything else to watch).
bool __cdecl CFunc_SetJoinMode(CStruct* params, CScript*)
{
    Hub* manager = NetLink();
    int mode = 0;
    params->FetchIntValue(0u, &mode, 0);
    manager->SetJoinMode(mode == 0 ? 0 : 1);
    return true;
}

// 0x00535E90: GetNumTeams - the game mode's team count into the script's num_teams.
bool __cdecl CFunc_GetNumTeams(CStruct*, CScript* script)
{
    script->m_args->PutInt(kNumTeams, Call<int>(CodeAddress(&GoalSystem::CMatchRules::NumTeams), GameMode()));
    return true;
}

// 0x00535EB0: ConnectedToPeer - the lobby has a GameSpy peer, connected (peerIsConnected).
bool __cdecl CFunc_ConnectedToPeer(CStruct*, CScript*)
{
    void* peer = LobbyPeer(NetLink());
    return peer && GameSpy::PeerIsConnected(peer);
}

// 0x00535EE0: InGroupRoom - the lobby's flag (+0x134).
bool __cdecl CFunc_InGroupRoom(CStruct*, CScript*) { return InGroupRoom(NetLink()); }

// 0x00535EF0: IsAutoServing - the server of a network game in host mode 1.
bool __cdecl CFunc_IsAutoServing(CStruct*, CScript*)
{
    Hub* manager = NetLink();
    if (!manager->InNetGame() || !(manager->m_flagBits & 1))
        return false;
    return manager->m_hostMode == 1;
}

// 0x00535F20: ChangeLevelPending - the level change's task is in a list.
bool __cdecl CFunc_ChangeLevelPending(CStruct*, CScript*)
{
    return Prop<TaskList::SchedulerTask*>(NetLink(), kChangeLevelTask)->IsOnTaskList();
}

// 0x00535F30: CancelMotdDownload.
bool __cdecl CFunc_CancelMotdDownload(CStruct*, CScript*)
{
    Prop<bool>(NetLink(), kMotdCancelled) = true;
    return true;
}

// 0x00535F40: DownloadMotd - the lobby's state 6.
bool __cdecl CFunc_DownloadMotd(CStruct*, CScript*)
{
    SetLobbyState(NetLink(), 6);
    return true;
}

// 0x00535F50: ServicesAreAvailable - with the services up (+0xFE0A = 1): true (if the message of the day hasn't come
// yet, services state 0 and lobby state 1 first); else services state 3, lobby state 1, false.
bool __cdecl CFunc_ServicesAreAvailable(CStruct*, CScript*)
{
    Hub* manager = NetLink();
    if (Prop<uint8_t>(manager, kServicesState) == 1) {
        if (!Prop<bool>(manager, kMotdReceived)) {
            SetServicesState(manager, 0);
            SetLobbyState(manager, 1);
        }
        return true;
    }
    SetServicesState(manager, 3);
    SetLobbyState(manager, 1);
    return false;
}

// 0x00535FA0: AlreadyGotMotd.
bool __cdecl CFunc_AlreadyGotMotd(CStruct*, CScript*) { return Prop<bool>(NetLink(), kMotdReceived); }

// 0x00535FB0: JoinServerComplete - ready to play.
bool __cdecl CFunc_JoinServerComplete(CStruct*, CScript*) { return NetLink()->m_readyToPlay; }

// 0x00535FC0: JustStartedNetGame - within 5 seconds of the network game's start (0x007CCDF4).
bool __cdecl CFunc_JustStartedNetGame(CStruct*, CScript*)
{
    return FrameClock::GetTime() - NetGameStartTime() < kJustStartedTime;
}

// 0x00535FE0: FCFSRequestStartGame - the game's options from the network preferences to the server (message 0x60);
// the game is then pending.
bool __cdecl CFunc_FCFSRequestStartGame(CStruct*, CScript*)
{
    StartGameRequest request; // (+0x18 is never set: the original's stack)
    Hub* manager = NetLink();
    OptionStore::Preferences* preferences = manager->GetOnlineSettings();
    request.unused18 = 0;
    request.skillLevel = preferences->GetPreferenceChecksum(kSkillLevel, kChecksum);
    request.fireballLevel = preferences->GetPreferenceChecksum(kFireballDifficulty, kChecksum);
    uint32_t gameType = 0;
    manager->GetGamePreferences()->FetchSetting(kGameType)->ReadHashValue(kChecksum, &gameType, 1);
    request.gameType = gameType;
    request.timeLimit = preferences->GetPreferenceChecksum(kTimeLimit, kChecksum);
    request.targetScore = preferences->GetPreferenceChecksum(kTargetScore, kChecksum);
    request.playerCollision = preferences->GetPreferenceChecksum(kPlayerCollision, kChecksum);
    request.friendlyFire = preferences->GetPreferenceChecksum(kFriendlyFire, kChecksum);
    request.stopAtZero = preferences->GetPreferenceChecksum(kStopAtZero, kChecksum);
    request.scoreReset = preferences->GetPreferenceChecksum(kScoreReset, kChecksum);
    request.ctfGameType = preferences->GetPreferenceChecksum(kCtfGameType, kChecksum);
    request.goals = preferences->GetPreferenceChecksum(kGoals, kChecksum);
    request.teamGraffiti = preferences->GetPreferenceChecksum(kTeamGraffitiMode, kChecksum);
    SendRequest(manager, kMsgStartGameRequest, &request, sizeof(request));
    manager->m_gamePending = true;
    return true;
}

// 0x00536170: FCFSRequestChangeLevel level=<load script> - for the created park, its map (0x6C, 15000 bytes) and the
// rail editor's rails (0x88, 4402 bytes) streamed to the server first; then the level (0x62).
bool __cdecl CFunc_FCFSRequestChangeLevel(CStruct* params, CScript*)
{
    Hub* manager = NetLink();
    uint32_t level = 0;
    params->ReadHashValue(kLevel, &level, 0);
    auto* client = static_cast<NetLink::Guest*>(manager->m_clientApp);
    if (level == kCreatedPark) {
        void* map = ParkEditing::CParkBuilderMode::Sole()->GetPackedParkData(false);
        client->SendBulkToHost(kMsgParkMap, kParkMapSize, map, kParkMapDesc, kGroupGame, true, false);
        ParkEditing::CRailAuthoring::Sole(); // (the rail editor looked up once for nothing)
        void* rails = Call<void*>(CodeAddress(&ParkEditing::CRailAuthoring::GetPacked), ParkEditing::CRailAuthoring::Sole());
        client->SendBulkToHost(kMsgRailsData, kRailsDataSize, rails, kRailsDesc, kGroupGame, true, false);
    }
    SendRequest(manager, kMsgChangeLevelRequest, &level, 4);
    return true;
}

// 0x00536240: ReattemptJoinServer - the connection request again on client 0.
bool __cdecl CFunc_ReattemptJoinServer(CStruct*, CScript*)
{
    Hub* manager = NetLink();
    manager->RetryEntry(manager->m_clientApp);
    return true;
}

// 0x00536260: FCFSRequestToggleProSet bit=<n> param_id=<checksum> - message 0x63: the id, then the bit's byte.
bool __cdecl CFunc_FCFSRequestToggleProSet(CStruct* params, CScript*)
{
    Hub* manager = NetLink();
    int bit;
    uint32_t id;
    params->FetchIntValue(kBit, &bit, 1);
    params->ReadHashValue(kParamId, &id, 1);
    struct {
        uint32_t id;
        uint8_t bit;
        uint8_t pad[3]; // (the original's stack)
    } request = {id, static_cast<uint8_t>(bit), {0, 0, 0}};
    SendRequest(manager, kMsgToggleProSetRequest, &request, sizeof(request));
    return true;
}

// 0x005362F0: FCFSRequestToggleGoalSelection name=<goal> - message 0x64.
bool __cdecl CFunc_FCFSRequestToggleGoalSelection(CStruct* params, CScript*)
{
    Hub* manager = NetLink();
    uint32_t goal; // (uninitialised in the original: a missing name sends what its slot held)
    params->ReadHashValue(kName, &goal, 1);
    uint32_t request = goal;
    SendRequest(manager, kMsgToggleGoalRequest, &request, 4);
    return true;
}

// 0x00536360: ResetProSetFlags - for each of the 7 pro set flags (FLAG_PROSET1_GEO_ON on), SetFlag or UnSetFlag
// {flag = <its number>} as m_proSetFlags has its bit.
bool __cdecl CFunc_ResetProSetFlags(CStruct*, CScript*)
{
    Hub* manager = NetLink();
    for (int bit = 0; bit < kProSetCount; ++bit) {
        CStruct* params = NewParams();
        params->PutInt(kFlag, Scripts::FetchIntValue(kFirstProSetFlag) + bit);
        Run((manager->m_proSetFlags & (1u << bit)) ? kSetFlag : kUnSetFlag, params);
        DeleteParams(params);
    }
    return true;
}

// 0x00536460: HasSignedDisclaimer - signed_disclaimer is boolean_true.
bool __cdecl CFunc_HasSignedDisclaimer(CStruct*, CScript*)
{
    return NetLink()->GetOnlineSettings()->GetPreferenceChecksum(kSignedDisclaimer, kChecksum) == kBooleanTrue;
}

// 0x00537100: FoundServers - the server list (+0x158) has an entry.
bool __cdecl CFunc_FoundServers(CStruct*, CScript*)
{
    const MemSys::ListNode* node = Prop<MemSys::ListNode*>(NetLink(), 0x164); // (the list's first node)
    int count = 0;
    // (the head's mark tested before a null node, as in the original)
    while (reinterpret_cast<intptr_t>(node->data) != -1 && node) {
        node = node->next;
        ++count;
    }
    return count != 0;
}

} // namespace OnlinePlay
