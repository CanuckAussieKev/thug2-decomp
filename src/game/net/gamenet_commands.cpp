// The network manager's script commands, first part (0x00535DA0-0x00536460): capture the flag's task, the game's
// state, the join mode, teams, the lobby's peer, the message of the day, and the first-come-first-served requests a
// client sends the server (start the game, change the level, toggle a pro set flag or a goal).

#include <cstdint>

#include "game/core/alloc.h"
#include "game/core/task.h"
#include "game/ed/park_editor.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/ed/rail_editor.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace GameNet {

using namespace Detail;
using Script::CScript;
using Script::CStruct;

namespace {

constexpr uint32_t kCtfTask = 0xF8;          // Tsk::BaseTask*: capture the flag's
constexpr uint32_t kChangeLevelTask = 0xEC;  // Tsk::BaseTask*: the level change's
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

const char* const kNumTeams = reinterpret_cast<const char*>(0x0064C9B4);     // "num_teams"
const char* const kParkMapDesc = reinterpret_cast<const char*>(0x0064C884);  // "level data"
const char* const kRailsDesc = reinterpret_cast<const char*>(0x0064C878);    // "rail data"
const char* const kBit = reinterpret_cast<const char*>(0x0064C9CC);          // "bit"
const char* const kParamId = reinterpret_cast<const char*>(0x0064C9C0);      // "param_id"
const char* const kFirstProSetFlag = reinterpret_cast<const char*>(0x0064C9D0); // "FLAG_PROSET1_GEO_ON"

constexpr int kProSetCount = 7;
constexpr uint32_t kFlagBaseWarningInterval = 3000; // ms
constexpr uint32_t kJustStartedTime = 5000;         // ms

uint32_t& LastFlagBaseWarning() { return GameVariable<uint32_t>(0x007CCE24); }
uint32_t& NetGameStartTime() { return GameVariable<uint32_t>(0x007CCDF4); }

Tsk::Stack* LogicTasks() { return reinterpret_cast<Tsk::Stack*>(GameVariable<uint8_t*>(0x00696D68) + 4); }
Manager* Net() { return GameVariable<Manager*>(0x007CCE14); }

// The lobby (GameNet's GameSpy peer side): its peer, whether it's in a group room; the lobby's state (the manager's
// SetServiceState, 0x005453E0) and services state (SetNextServiceState, 0x005451B0).
void* LobbyPeer(Manager* manager) { return static_cast<LobbyMan*>(manager->m_lobbyMan)->GetPeer(); }
bool InGroupRoom(Manager* manager) { return static_cast<LobbyMan*>(manager->m_lobbyMan)->InGroupRoom(); }
void SetLobbyState(Manager* manager, int state) { manager->SetServiceState(state); }
void SetServicesState(Manager* manager, int state) { manager->SetNextServiceState(state); }

// A message for the server through client 0, sequenced in the game's group.
void SendRequest(Manager* manager, uint8_t id, void* data, uint16_t length)
{
    Net::MsgDesc desc;
    desc.Construct();
    desc.m_id = id;
    desc.m_data = data;
    desc.m_length = length;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToServer(manager->m_client, &desc);
}

} // namespace

// 0x00535DA0: StartCTFGame - capture the flag's task into the logic tasks.
bool __cdecl CFunc_StartCTFGame(CStruct*, CScript*)
{
    LogicTasks()->AddTask(Field<Tsk::BaseTask*>(Net(), kCtfTask));
    return true;
}

// 0x00535DC0: EndCTFGame - its task removed.
bool __cdecl CFunc_EndCTFGame(CStruct*, CScript*)
{
    Field<Tsk::BaseTask*>(Net(), kCtfTask)->Remove();
    return true;
}

// 0x00535DE0: DisplayFlagBaseWarning - display_flag_base_warning, at most every 3 seconds.
bool __cdecl CFunc_DisplayFlagBaseWarning(CStruct*, CScript*)
{
    if (Tmr::GetTime() - LastFlagBaseWarning() > kFlagBaseWarningInterval) {
        Run(kDisplayFlagBaseWarning);
        LastFlagBaseWarning() = Tmr::GetTime();
    }
    return true;
}

// 0x00535E20: NetworkGamePending - a start request is waiting for the server.
bool __cdecl CFunc_NetworkGamePending(CStruct*, CScript*) { return Net()->m_gamePending; }

// 0x00535E30: MarkGameOver.
bool __cdecl CFunc_MarkGameOver(CStruct*, CScript*)
{
    Net()->m_gameOver = true;
    return true;
}

// 0x00535E40: SetJoinMode <n> (an unnamed integer: 0 to play, anything else to watch).
bool __cdecl CFunc_SetJoinMode(CStruct* params, CScript*)
{
    Manager* manager = Net();
    int mode = 0;
    params->GetInteger(0u, &mode, 0);
    manager->SetJoinMode(mode == 0 ? 0 : 1);
    return true;
}

// 0x00535E90: GetNumTeams - the game mode's team count into the script's num_teams.
bool __cdecl CFunc_GetNumTeams(CStruct*, CScript* script)
{
    script->m_params->AddInteger(kNumTeams, Call<int>(0x00553130, GameMode()));
    return true;
}

// 0x00535EB0: ConnectedToPeer - the lobby has a GameSpy peer, connected (peerIsConnected).
bool __cdecl CFunc_ConnectedToPeer(CStruct*, CScript*)
{
    void* peer = LobbyPeer(Net());
    return peer && GameFunction<int(__cdecl*)(void*)>(0x005FC730)(peer);
}

// 0x00535EE0: InGroupRoom - the lobby's flag (+0x134).
bool __cdecl CFunc_InGroupRoom(CStruct*, CScript*) { return InGroupRoom(Net()); }

// 0x00535EF0: IsAutoServing - the server of a network game in host mode 1.
bool __cdecl CFunc_IsAutoServing(CStruct*, CScript*)
{
    Manager* manager = Net();
    if (!manager->InNetGame() || !(manager->m_flags & 1))
        return false;
    return manager->m_hostMode == 1;
}

// 0x00535F20: ChangeLevelPending - the level change's task is in a list.
bool __cdecl CFunc_ChangeLevelPending(CStruct*, CScript*)
{
    return Field<Tsk::BaseTask*>(Net(), kChangeLevelTask)->InList();
}

// 0x00535F30: CancelMotdDownload.
bool __cdecl CFunc_CancelMotdDownload(CStruct*, CScript*)
{
    Field<bool>(Net(), kMotdCancelled) = true;
    return true;
}

// 0x00535F40: DownloadMotd - the lobby's state 6.
bool __cdecl CFunc_DownloadMotd(CStruct*, CScript*)
{
    SetLobbyState(Net(), 6);
    return true;
}

// 0x00535F50: ServicesAreAvailable - with the services up (+0xFE0A = 1): true (if the message of the day hasn't come
// yet, services state 0 and lobby state 1 first); else services state 3, lobby state 1, false.
bool __cdecl CFunc_ServicesAreAvailable(CStruct*, CScript*)
{
    Manager* manager = Net();
    if (Field<uint8_t>(manager, kServicesState) == 1) {
        if (!Field<bool>(manager, kMotdReceived)) {
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
bool __cdecl CFunc_AlreadyGotMotd(CStruct*, CScript*) { return Field<bool>(Net(), kMotdReceived); }

// 0x00535FB0: JoinServerComplete - ready to play.
bool __cdecl CFunc_JoinServerComplete(CStruct*, CScript*) { return Net()->m_readyToPlay; }

// 0x00535FC0: JustStartedNetGame - within 5 seconds of the network game's start (0x007CCDF4).
bool __cdecl CFunc_JustStartedNetGame(CStruct*, CScript*)
{
    return Tmr::GetTime() - NetGameStartTime() < kJustStartedTime;
}

// 0x00535FE0: FCFSRequestStartGame - the game's options from the network preferences to the server (message 0x60);
// the game is then pending.
bool __cdecl CFunc_FCFSRequestStartGame(CStruct*, CScript*)
{
    StartGameRequest request; // (+0x18 is never set: the original's stack)
    Manager* manager = Net();
    Prefs::Preferences* preferences = manager->GetNetworkPreferences();
    request.unused18 = 0;
    request.skillLevel = preferences->GetPreferenceChecksum(kSkillLevel, kChecksum);
    request.fireballLevel = preferences->GetPreferenceChecksum(kFireballDifficulty, kChecksum);
    uint32_t gameType = 0;
    manager->GetGamePreferences()->GetPreference(kGameType)->GetChecksum(kChecksum, &gameType, 1);
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
    Manager* manager = Net();
    uint32_t level = 0;
    params->GetChecksum(kLevel, &level, 0);
    auto* client = static_cast<Net::Client*>(manager->m_client);
    if (level == kCreatedPark) {
        void* map = Ed::CParkEditor::Instance()->GetCompressedMapBuffer(false);
        client->StreamMessageToServer(kMsgParkMap, kParkMapSize, map, kParkMapDesc, kGroupGame, true, false);
        Ed::CRailEditorComponent::Instance(); // (the rail editor looked up once for nothing)
        void* rails = Call<void*>(0x004F45B0, Ed::CRailEditorComponent::Instance());
        client->StreamMessageToServer(kMsgRailsData, kRailsDataSize, rails, kRailsDesc, kGroupGame, true, false);
    }
    SendRequest(manager, kMsgChangeLevelRequest, &level, 4);
    return true;
}

// 0x00536240: ReattemptJoinServer - the connection request again on client 0.
bool __cdecl CFunc_ReattemptJoinServer(CStruct*, CScript*)
{
    Manager* manager = Net();
    manager->ReattemptJoin(manager->m_client);
    return true;
}

// 0x00536260: FCFSRequestToggleProSet bit=<n> param_id=<checksum> - message 0x63: the id, then the bit's byte.
bool __cdecl CFunc_FCFSRequestToggleProSet(CStruct* params, CScript*)
{
    Manager* manager = Net();
    int bit;
    uint32_t id;
    params->GetInteger(kBit, &bit, 1);
    params->GetChecksum(kParamId, &id, 1);
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
    Manager* manager = Net();
    uint32_t goal; // (uninitialised in the original: a missing name sends what its slot held)
    params->GetChecksum(kName, &goal, 1);
    uint32_t request = goal;
    SendRequest(manager, kMsgToggleGoalRequest, &request, 4);
    return true;
}

// 0x00536360: ResetProSetFlags - for each of the 7 pro set flags (FLAG_PROSET1_GEO_ON on), SetFlag or UnSetFlag
// {flag = <its number>} as m_proSetFlags has its bit.
bool __cdecl CFunc_ResetProSetFlags(CStruct*, CScript*)
{
    Manager* manager = Net();
    for (int bit = 0; bit < kProSetCount; ++bit) {
        CStruct* params = NewParams();
        params->AddInteger(kFlag, Script::GetInteger(kFirstProSetFlag) + bit);
        Run((manager->m_proSetFlags & (1u << bit)) ? kSetFlag : kUnSetFlag, params);
        DeleteParams(params);
    }
    return true;
}

// 0x00536460: HasSignedDisclaimer - signed_disclaimer is boolean_true.
bool __cdecl CFunc_HasSignedDisclaimer(CStruct*, CScript*)
{
    return Net()->GetNetworkPreferences()->GetPreferenceChecksum(kSignedDisclaimer, kChecksum) == kBooleanTrue;
}

// 0x00537100: FoundServers - the server list (+0x158) has an entry.
bool __cdecl CFunc_FoundServers(CStruct*, CScript*)
{
    const Mem::ListNode* node = Field<Mem::ListNode*>(Net(), 0x164); // (the list's first node)
    int count = 0;
    // (the head's mark tested before a null node, as in the original)
    while (reinterpret_cast<intptr_t>(node->data) != -1 && node) {
        node = node->next;
        ++count;
    }
    return count != 0;
}

REPLACE(0x00537100, CFunc_FoundServers);
REPLACE(0x00535DA0, CFunc_StartCTFGame);
REPLACE(0x00535DC0, CFunc_EndCTFGame);
REPLACE(0x00535DE0, CFunc_DisplayFlagBaseWarning);
REPLACE(0x00535E20, CFunc_NetworkGamePending);
REPLACE(0x00535E30, CFunc_MarkGameOver);
REPLACE(0x00535E40, CFunc_SetJoinMode);
REPLACE(0x00535E90, CFunc_GetNumTeams);
REPLACE(0x00535EB0, CFunc_ConnectedToPeer);
REPLACE(0x00535EE0, CFunc_InGroupRoom);
REPLACE(0x00535EF0, CFunc_IsAutoServing);
REPLACE(0x00535F20, CFunc_ChangeLevelPending);
REPLACE(0x00535F30, CFunc_CancelMotdDownload);
REPLACE(0x00535F40, CFunc_DownloadMotd);
REPLACE(0x00535F50, CFunc_ServicesAreAvailable);
REPLACE(0x00535FA0, CFunc_AlreadyGotMotd);
REPLACE(0x00535FB0, CFunc_JoinServerComplete);
REPLACE(0x00535FC0, CFunc_JustStartedNetGame);
REPLACE(0x00535FE0, CFunc_FCFSRequestStartGame);
REPLACE(0x00536170, CFunc_FCFSRequestChangeLevel);
REPLACE(0x00536240, CFunc_ReattemptJoinServer);
REPLACE(0x00536260, CFunc_FCFSRequestToggleProSet);
REPLACE(0x005362F0, CFunc_FCFSRequestToggleGoalSelection);
REPLACE(0x00536360, CFunc_ResetProSetFlags);
REPLACE(0x00536460, CFunc_HasSignedDisclaimer);

} // namespace GameNet
