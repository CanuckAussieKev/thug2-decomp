// The network manager's small members (OnlinePlay::Manager, 0x00532AA0-0x005333F0): its flags and saved values, the
// network preferences it reads, and the few messages it sends on its own.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/task.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "game/ed/goal_editor.h"

namespace OnlinePlay {

namespace {

// The manager's fields not declared yet (reached by offset until the class is laid out).
template <typename T>
T& Field(Manager* manager, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(manager) + offset);
}

constexpr uint32_t kGameMode = 0x30;
constexpr uint32_t kGameId = 0x38;           // uint8_t: the current game's number (the start info's)
constexpr uint32_t kEndedGameId = 0x39;      // uint8_t: the game whose end was last reported
constexpr uint32_t kCheatingNoted = 0x3B;    // bool
constexpr uint32_t kFinalScores = 0x44;      // bool
constexpr uint32_t kDrawNames = 0x47;        // bool
constexpr uint32_t kStartInfoWords = 0x48;   // 8 words
constexpr uint32_t kCrownSpawnPoint = 0x68;
constexpr uint32_t kCrown = 0x6C;
constexpr uint32_t kHostTask = 0xF4;         // TaskList::SchedulerTask*
constexpr uint32_t kNetworkPrefs = 0x104;    // OptionStore::Preferences
constexpr uint32_t kTauntPrefs = 0x110;      // OptionStore::Preferences
constexpr uint32_t kLastLeader = 0x1D0;
constexpr uint32_t kLastLeadingTeam = 0x1D4;
constexpr uint32_t kGoalsMessage = 0x1D8;    // the level, then the goals (6000 bytes)
constexpr uint32_t kGoalsSize = 0x1948;
constexpr uint32_t kGoalsLevel = 0x194C;
constexpr uint32_t kLobbyId = 0x9950;
constexpr uint32_t kPendingData = 0xFDF8;
constexpr uint32_t kPendingDataSize = 0xFDFC;

constexpr int kGoalsBufferSize = 6000;
constexpr uint16_t kLanPort = 5153;          // the LAN lobby's port

// Messages.
constexpr uint8_t kMsgFindServer = 0x11;
constexpr uint8_t kMsgReadyResponse = 0x26;
constexpr uint8_t kMsgGameEnded = 0x59;
constexpr uint8_t kGroupGame = 8;            // the sequenced group of the game's messages

// Checksums.
constexpr uint32_t kChecksum = 0x21902065;        // checksum
constexpr uint32_t kRankTest = 0x2120910C;        // rank_test
constexpr uint32_t kRankTestLess = 0x3C6F2B1A;    // rank_test_less
constexpr uint32_t kRankTestGreater = 0x46D173AB; // rank_test_greater
constexpr uint32_t kRankLimit = 0xAD6D5802;       // rank_limit
constexpr uint32_t kSkillLevel = 0x403DA0D0;      // skill_level
constexpr uint32_t kNumPlayers = 0x1CB2A6B0;      // num_players
constexpr uint32_t kTeamMode = 0xB9D44C2F;        // team_mode
constexpr uint32_t kNumObservers = 0xA8DA5CB7;    // num_observers
constexpr uint32_t kLevel = 0x651533EC;           // level
constexpr uint32_t kFireballDifficulty = 0xCE238F5D; // fireball_difficulty
constexpr uint32_t kShowNames = 0x0F7DD263;       // show_names
constexpr uint32_t kBooleanTrue = 0xF81BC89B;     // boolean_true
constexpr uint32_t kTeamsNone = 0x6A220326;       // teams_none
constexpr uint32_t kTeamsTwo = 0x768E5A1D;        // teams_two
constexpr uint32_t kTeamsThree = 0x4D04DFA7;      // teams_three
constexpr uint32_t kCtfGameType = 0x15421B73;     // ctf_game_type
constexpr uint32_t kTimedCtf = 0x938AB24B;        // timed_ctf
constexpr uint32_t kStopAtZero = 0x48E748B5;      // stop_at_zero
// num_0 ... num_9
constexpr uint32_t kNum[] = {0x18CBB824, 0x6FCC88B2, 0xF6C5D908, 0x81C2E99E, 0x1FA67C3D,
                             0x68A14CAB, 0xF1A81D11, 0x86AF2D87, 0x16103016, 0x61170080};
// The game modes.
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;      // netgoalattack
constexpr uint32_t kKing = 0x5D32129C;               // king
constexpr uint32_t kNetKing = 0x6EF8FDA0;            // netking
constexpr uint32_t kNetScoreChallenge = 0x1498240A;  // netscorechallenge
constexpr uint32_t kScoreChallenge = 0xF135ECB6;     // scorechallenge
constexpr uint32_t kNetCtf = 0x6C5FF266;             // netctf

// The checksum the network preference `name` is set to. On a miss (asserted, so the debug message's empty
// function is called) the original reads its uninitialised local: the slot ECX was pushed into - the manager.
uint32_t PreferenceChecksum(Manager* manager, uint32_t name)
{
    Scripts::CStruct* preference = manager->GetOnlineSettings()->GetPreference(name);
    auto value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(manager));
    preference->ReadHashValue(kChecksum, &value, 1);
    return value;
}

// num_<first> ... num_<last> -> first + bias ...; anything else -> otherwise.
int NumberOf(uint32_t value, int first, int last, int bias, int otherwise)
{
    for (int n = first; n <= last; ++n) {
        if (value == kNum[n])
            return n + bias;
    }
    return otherwise;
}

// 0x004E8470: the goal editor module.
ParkEditing::CObjectiveAuthoring* GoalEditor() { return ParkEditing::CObjectiveAuthoring::Get(); }

} // namespace

// 0x00532AA0
void Manager::SetJoinMode(int mode)
{
    m_joinMode = mode;
    g_joinServerIPSucceeded = false;
}

// 0x00532AC0
int Manager::GetEntryMode() { return m_joinMode; }

// 0x00532AD0
int Manager::GetHostingRole() { return m_hostMode; }

// 0x00532AE0
int Manager::GetRankTest()
{
    uint32_t value = PreferenceChecksum(this, kRankTest);
    if (value == kRankTestLess)
        return 1;
    return value == kRankTestGreater ? 2 : 0;
}

// 0x00532B30
int Manager::GetRankLimit() { return NumberOf(PreferenceChecksum(this, kRankLimit), 1, 9, -1, 9); }

// 0x00532BE0
int Manager::GetDifficultyTier() { return NumberOf(PreferenceChecksum(this, kSkillLevel), 1, 5, -1, 2); }

// 0x00532C50
int Manager::GetPlayerLimit() { return NumberOf(PreferenceChecksum(this, kNumPlayers), 2, 8, 0, 2); }

// 0x00532CF0
int Manager::GetNumTeams()
{
    uint32_t value = PreferenceChecksum(this, kTeamMode);
    if (value == kTeamsNone)
        return 0;
    if (value == kTeamsTwo)
        return 2;
    if (value == kTeamsThree)
        return 3;
    return 4; // teams_four, or anything else
}

// 0x00532D60
int Manager::GetSpectatorLimit() { return NumberOf(PreferenceChecksum(this, kNumObservers), 0, 8, 0, 2); }

// 0x00532E30
void Manager::RemoveHostTask() { Field<TaskList::SchedulerTask*>(this, kHostTask)->Remove(); }

// 0x00532E40 / 0x00532E50 / 0x00532E60 / 0x00532E70 / 0x00532E80 / 0x00532E90
uint32_t Manager::GetJoinIp() { return m_joinIp; }
uint32_t Manager::GetJoinPrivateIp() { return m_joinPrivateIp; }
int Manager::GetEntryPort() { return m_joinPort; }
void Manager::SetJoinIp(uint32_t ip) { m_joinIp = ip; }
void Manager::SetJoinPrivateIp(uint32_t ip) { m_joinPrivateIp = ip; }
void Manager::SetEntryPort(int port) { m_joinPort = port; }

// 0x00532EA0 (+0x08 + index * 4: the two clients lie side by side)
NetLink::App* Manager::GetClient(int index) { return ClientSlot(index); }

// 0x00532EB0
NetLink::App* Manager::GetMatchmakingClient() { return m_matchClient; }

// 0x00532EC0
void* Manager::GetCamPlayer() { return m_camPlayer; }

uint32_t g_commandLineServer{0xFFFFFFFF};

// 0x00532ED0
void Manager::FindServersOnLan()
{
    if (!m_matchClient)
        return;
    // 12 bytes: the time, then 8 bytes the original leaves uninitialised (zeroed here).
    uint32_t message[3] = {FrameClock::GetTime(), 0, 0};
    uint32_t& askedFor = g_askedFor;
    if (askedFor) {
        m_matchClient->DeliverTo(kMsgFindServer, sizeof(message), message, askedFor, kLanPort, 0);
        askedFor = 0;
        return;
    }
    m_matchClient->DeliverTo(kMsgFindServer, sizeof(message), message, 0xFFFFFFFF, kLanPort, 0);
    uint32_t commandLine = g_commandLineServer; // (ParseNetworkParams)
    if (commandLine != 0xFFFFFFFF)
        m_matchClient->DeliverTo(kMsgFindServer, sizeof(message), message, commandLine, kLanPort, 0);
}

// 0x00532F70 / 0x00532F80
OptionStore::Preferences* Manager::GetOnlineSettings() { return &Field<OptionStore::Preferences>(this, kNetworkPrefs); }
OptionStore::Preferences* Manager::GetTauntSettings() { return &Field<OptionStore::Preferences>(this, kTauntPrefs); }

// 0x00532F90
void Manager::SetStartInfoWords(const uint32_t* words)
{
    std::memcpy(&Field<uint32_t>(this, kStartInfoWords), words, 8 * sizeof(uint32_t));
}

// 0x00532FD0
uint32_t Manager::GetStartInfoWord(int index) { return (&Field<uint32_t>(this, kStartInfoWords))[index]; }

// 0x00532FE0
bool Manager::IsUntimedGame()
{
    uint32_t mode = Field<uint32_t>(this, kGameMode);
    if (mode == kNetGoalAttack || mode == kKing || mode == kNetKing || mode == kNetScoreChallenge ||
        mode == kScoreChallenge)
        return true;
    if (!InNetGame())
        return false;
    OptionStore::Preferences* prefs = GetOnlineSettings();
    if (mode == kNetCtf && prefs->GetPreferenceChecksum(kCtfGameType, kChecksum) != kTimedCtf)
        return true;
    return prefs->GetPreferenceChecksum(kStopAtZero, kChecksum) == kBooleanTrue;
}

// 0x00533060 / 0x00533070
void Manager::SetGameId(uint8_t id) { Field<uint8_t>(this, kGameId) = id; }
uint8_t Manager::GetGameId() { return Field<uint8_t>(this, kGameId); }

// 0x00533080 (on a miss: the manager's address, the uninitialised local's slot)
uint32_t Manager::GetLevelPreference() { return PreferenceChecksum(this, kLevel); }

// 0x005330B0
int Manager::GetFireballLevel() { return NumberOf(PreferenceChecksum(this, kFireballDifficulty), 1, 5, 0, 3); }

// 0x00533130
void Manager::TogglePlayerNames() { Field<uint8_t>(this, kDrawNames) ^= 1; }

// 0x00533140
bool Manager::WantsNameTags()
{
    uint32_t value = 0;
    GetOnlineSettings()->GetPreference(kShowNames)->ReadHashValue(kChecksum, &value, 0);
    return value == kBooleanTrue;
}

// 0x00533180 / 0x00533190 / 0x005331A0
void Manager::SetLastLeader(void* player) { Field<void*>(this, kLastLeader) = player; }
void* Manager::GetLastLeader() { return Field<void*>(this, kLastLeader); }
int Manager::GetLastLeadingTeam() { return Field<int>(this, kLastLeadingTeam); }

// 0x005331B0 / 0x005331C0
void Manager::SetLastScoreUpdate(uint32_t time) { m_lastScoreUpdate = time; }
uint32_t Manager::GetLastScoreUpdate() { return m_lastScoreUpdate; }

// 0x005331D0 / 0x005331E0
void Manager::SetFinalScoresReceived(bool received) { Field<bool>(this, kFinalScores) = received; }
bool Manager::FinalScoresReceived() { return Field<bool>(this, kFinalScores); }

// 0x005331F0 / 0x00533200
void Manager::SetEntryPhase(int state) { m_joinState = state; }
int Manager::GetEntryPhase() { return m_joinState; }

// 0x00533210 / 0x00533220
void* Manager::GetCrownHolder() { return Field<void*>(this, kCrown); }
void Manager::ClearCrown() { Field<void*>(this, kCrown) = nullptr; }

// 0x00533230
void Manager::SetCrownStartSpot(char point) { Field<int>(this, kCrownSpawnPoint) = point; }

// 0x00533240
void Manager::SetRoomId(uint32_t id) { Field<uint32_t>(this, kLobbyId) = id; }

// 0x00533250
void Manager::SendGameEnded()
{
    NetLink::NetMessageInfo desc;
    desc.Construct();
    desc.m_data = &Field<uint8_t>(this, kGameId);
    desc.m_byteCount = 1;
    desc.m_id = kMsgGameEnded;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    static_cast<NetLink::Client*>(m_clientApp)->SendToHost(&desc);
    Field<uint8_t>(this, kEndedGameId) = Field<uint8_t>(this, kGameId);
}

// 0x005332A0
bool Manager::GameEndedSent() { return Field<uint8_t>(this, kEndedGameId) == Field<uint8_t>(this, kGameId); }

// 0x005332B0 / 0x005332C0 / 0x005332D0
bool Manager::IsGameOver() { return m_gameOver; }
void Manager::SetGameOver() { m_gameOver = true; }
void Manager::ResetMatchEnded() { m_gameOver = false; }

// 0x005332E0 / 0x005332F0
bool Manager::CheatingNoted() { return Field<bool>(this, kCheatingNoted); }
void Manager::NoteCheating() { Field<bool>(this, kCheatingNoted) = true; }

// 0x00533300
void Manager::SendReadyResponse()
{
    NetLink::NetMessageInfo desc;
    desc.Construct();
    uint32_t id = m_latestReadyQuery;
    desc.m_data = &id;
    desc.m_byteCount = sizeof(id);
    desc.m_id = kMsgReadyResponse;
    desc.m_queue = NetLink::NetMessageInfo::kQueueImportant;
    for (int i = 0; i < 2; ++i) {
        if (NetLink::App* client = GetClient(i))
            static_cast<NetLink::Client*>(client)->SendToHost(&desc);
    }
}

// 0x00533360
void* Manager::GetPendingData(uint32_t* size)
{
    *size = Field<uint32_t>(this, kPendingDataSize);
    return Field<void*>(this, kPendingData);
}

// 0x00533380
bool Manager::HasCreatedGoals()
{
    return GoalEditor()->CountCompleteGoals(0) > 0;
}

// 0x005333A0
void Manager::PrepareCreatedGoals(uint32_t level)
{
    Field<uint32_t>(this, kGoalsLevel) = level;
    Field<uint32_t>(this, kGoalsMessage) = level;
    ParkEditing::CObjectiveAuthoring* editor = GoalEditor();
    int size = static_cast<int>(editor->WriteLevelGoalsToBuffer(
        level, &Field<uint8_t>(this, kGoalsMessage + 4), static_cast<uint32_t>(kGoalsBufferSize)));
    Field<int>(this, kGoalsSize) = size + 4;
}

// 0x005333E0 / 0x005333F0
void* Manager::GetCreatedGoalsMessage() { return &Field<uint8_t>(this, kGoalsMessage); }
uint32_t Manager::GetCreatedGoalsSize() { return Field<uint32_t>(this, kGoalsSize); }

} // namespace OnlinePlay

using OnlinePlay::Manager;
