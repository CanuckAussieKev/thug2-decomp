// The network manager's small members (GameNet::Manager, 0x00532AA0-0x005333F0): its flags and saved values, the
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
#include "hook/game.h"
#include "hook/hook.h"

namespace GameNet {

static_assert(offsetof(Manager, m_client2) == 0x0C && offsetof(Manager, m_matchClient) == 0x2C &&
                  offsetof(Manager, m_gameMode) == 0x30,
              "layout must match the original");

namespace {

// The manager's fields past those declared (reached by offset until the class is laid out).
template <typename T>
T& Field(Manager* manager, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(manager) + offset);
}

constexpr uint32_t kGameId = 0x38;           // uint8_t: the current game's number (the start info's)
constexpr uint32_t kReadyGameId = 0x39;      // uint8_t: the game ready was last sent for
constexpr uint32_t kAllReady = 0x3A;         // bool
constexpr uint32_t kCheatingNoted = 0x3B;    // bool
constexpr uint32_t kJoinMode = 0x3C;
constexpr uint32_t kHostMode = 0x40;
constexpr uint32_t kFinalScores = 0x44;      // bool
constexpr uint32_t kDrawNames = 0x47;        // bool
constexpr uint32_t kStartInfoWords = 0x48;   // 8 words
constexpr uint32_t kCrownSpawnPoint = 0x68;
constexpr uint32_t kCrown = 0x6C;
constexpr uint32_t kHostTask = 0xF4;         // Tsk::BaseTask*
constexpr uint32_t kNetworkPrefs = 0x104;    // Prefs::Preferences
constexpr uint32_t kTauntPrefs = 0x110;      // Prefs::Preferences
constexpr uint32_t kObserverCount = 0x188;
constexpr uint32_t kServerListState = 0x190;
constexpr uint32_t kJoinState = 0x198;
constexpr uint32_t kPublicIp = 0x19C;
constexpr uint32_t kPublicPort = 0x1A0;
constexpr uint32_t kPrivatePort = 0x1A4;
constexpr uint32_t kPlayerInfoFlags = 0x1CC;
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
constexpr uint8_t kMsgObserverCount = 0x26;
constexpr uint8_t kMsgReady = 0x59;
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
    Script::CStruct* preference = manager->GetNetworkPreferences()->GetPreference(name);
    auto value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(manager));
    preference->GetChecksum(kChecksum, &value, 1);
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
void* GoalEditor() { return GameFunction<void*(__cdecl*)()>(0x004E8470)(); }

} // namespace

// 0x00532AA0
void Manager::SetJoinMode(int mode)
{
    Field<int>(this, kJoinMode) = mode;
    GameVariable<bool>(0x007CCA88) = false;
}

// 0x00532AC0
int Manager::GetJoinMode() { return Field<int>(this, kJoinMode); }

// 0x00532AD0
int Manager::GetHostMode() { return Field<int>(this, kHostMode); }

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
int Manager::GetSkillLevel() { return NumberOf(PreferenceChecksum(this, kSkillLevel), 1, 5, -1, 2); }

// 0x00532C50
int Manager::GetMaxPlayers() { return NumberOf(PreferenceChecksum(this, kNumPlayers), 2, 8, 0, 2); }

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
int Manager::GetMaxObservers() { return NumberOf(PreferenceChecksum(this, kNumObservers), 0, 8, 0, 2); }

// 0x00532E30
void Manager::RemoveHostTask() { Field<Tsk::BaseTask*>(this, kHostTask)->Remove(); }

// 0x00532E40 / 0x00532E50 / 0x00532E60 / 0x00532E70 / 0x00532E80 / 0x00532E90
uint32_t Manager::GetPublicIp() { return Field<uint32_t>(this, kPublicIp); }
uint32_t Manager::GetPublicPort() { return Field<uint32_t>(this, kPublicPort); }
uint32_t Manager::GetPrivatePort() { return Field<uint32_t>(this, kPrivatePort); }
void Manager::SetPublicIp(uint32_t value) { Field<uint32_t>(this, kPublicIp) = value; }
void Manager::SetPublicPort(uint32_t value) { Field<uint32_t>(this, kPublicPort) = value; }
void Manager::SetPrivatePort(uint32_t value) { Field<uint32_t>(this, kPrivatePort) = value; }

// 0x00532EA0 (+0x08 + index * 4: the two clients lie side by side)
Net::App* Manager::GetClient(int index) { return (&m_client)[index]; }

// 0x00532EB0
Net::App* Manager::GetMatchClient() { return m_matchClient; }

// 0x00532EC0
uint32_t Manager::GetPlayerInfoFlags() { return Field<uint32_t>(this, kPlayerInfoFlags); }

// 0x00532ED0
void Manager::FindServersOnLan()
{
    if (!m_matchClient)
        return;
    // 12 bytes: the time, then 8 bytes the original leaves uninitialised (zeroed here).
    uint32_t message[3] = {Tmr::GetTime(), 0, 0};
    uint32_t& askedFor = GameVariable<uint32_t>(0x007CCA84);
    if (askedFor) {
        m_matchClient->SendMessageTo(kMsgFindServer, sizeof(message), message, askedFor, kLanPort, 0);
        askedFor = 0;
        return;
    }
    m_matchClient->SendMessageTo(kMsgFindServer, sizeof(message), message, 0xFFFFFFFF, kLanPort, 0);
    uint32_t commandLine = GameVariable<uint32_t>(0x0067EFB8); // (ParseNetworkParams)
    if (commandLine != 0xFFFFFFFF)
        m_matchClient->SendMessageTo(kMsgFindServer, sizeof(message), message, commandLine, kLanPort, 0);
}

// 0x00532F70 / 0x00532F80
Prefs::Preferences* Manager::GetNetworkPreferences() { return &Field<Prefs::Preferences>(this, kNetworkPrefs); }
Prefs::Preferences* Manager::GetTauntPreferences() { return &Field<Prefs::Preferences>(this, kTauntPrefs); }

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
    if (m_gameMode == kNetGoalAttack || m_gameMode == kKing || m_gameMode == kNetKing ||
        m_gameMode == kNetScoreChallenge || m_gameMode == kScoreChallenge)
        return true;
    if (!InNetGame())
        return false;
    Prefs::Preferences* prefs = GetNetworkPreferences();
    if (m_gameMode == kNetCtf && prefs->GetPreferenceChecksum(kCtfGameType, kChecksum) != kTimedCtf)
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
bool Manager::ShouldDrawPlayerNames()
{
    uint32_t value = 0;
    GetNetworkPreferences()->GetPreference(kShowNames)->GetChecksum(kChecksum, &value, 0);
    return value == kBooleanTrue;
}

// 0x00533180 / 0x00533190 / 0x005331A0
void Manager::SetLastLeader(void* player) { Field<void*>(this, kLastLeader) = player; }
void* Manager::GetLastLeader() { return Field<void*>(this, kLastLeader); }
int Manager::GetLastLeadingTeam() { return Field<int>(this, kLastLeadingTeam); }

// 0x005331B0 / 0x005331C0
void Manager::SetServerListState(uint32_t value) { Field<uint32_t>(this, kServerListState) = value; }
uint32_t Manager::GetServerListState() { return Field<uint32_t>(this, kServerListState); }

// 0x005331D0 / 0x005331E0
void Manager::SetFinalScoresReceived(bool received) { Field<bool>(this, kFinalScores) = received; }
bool Manager::FinalScoresReceived() { return Field<bool>(this, kFinalScores); }

// 0x005331F0 / 0x00533200
void Manager::SetJoinState(int state) { Field<int>(this, kJoinState) = state; }
int Manager::GetJoinState() { return Field<int>(this, kJoinState); }

// 0x00533210 / 0x00533220
void* Manager::GetCrown() { return Field<void*>(this, kCrown); }
void Manager::ClearCrown() { Field<void*>(this, kCrown) = nullptr; }

// 0x00533230
void Manager::SetCrownSpawnPoint(char point) { Field<int>(this, kCrownSpawnPoint) = point; }

// 0x00533240
void Manager::SetLobbyId(uint32_t id) { Field<uint32_t>(this, kLobbyId) = id; }

// 0x00533250
void Manager::SendReady()
{
    Net::MsgDesc desc;
    desc.Construct();
    desc.m_data = &Field<uint8_t>(this, kGameId);
    desc.m_length = 1;
    desc.m_id = kMsgReady;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    static_cast<Net::Client*>(m_client)->EnqueueMessageToServer(&desc);
    Field<uint8_t>(this, kReadyGameId) = Field<uint8_t>(this, kGameId);
}

// 0x005332A0
bool Manager::ReadySent() { return Field<uint8_t>(this, kReadyGameId) == Field<uint8_t>(this, kGameId); }

// 0x005332B0 / 0x005332C0 / 0x005332D0
bool Manager::AllPlayersReady() { return Field<bool>(this, kAllReady); }
void Manager::SetAllPlayersReady() { Field<bool>(this, kAllReady) = true; }
void Manager::ClearAllPlayersReady() { Field<bool>(this, kAllReady) = false; }

// 0x005332E0 / 0x005332F0
bool Manager::CheatingNoted() { return Field<bool>(this, kCheatingNoted); }
void Manager::NoteCheating() { Field<bool>(this, kCheatingNoted) = true; }

// 0x00533300
void Manager::SendObserverCount()
{
    Net::MsgDesc desc;
    desc.Construct();
    uint32_t count = Field<uint32_t>(this, kObserverCount);
    desc.m_data = &count;
    desc.m_length = sizeof(count);
    desc.m_id = kMsgObserverCount;
    desc.m_queue = Net::MsgDesc::kQueueImportant;
    for (int i = 0; i < 2; ++i) {
        if (Net::App* client = GetClient(i))
            static_cast<Net::Client*>(client)->EnqueueMessageToServer(&desc);
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
    void* editor = GoalEditor();
    return GameFunction<int(__thiscall*)(void*, uint32_t)>(0x004E7E50)(editor, 0) > 0;
}

// 0x005333A0
void Manager::PrepareCreatedGoals(uint32_t level)
{
    Field<uint32_t>(this, kGoalsLevel) = level;
    Field<uint32_t>(this, kGoalsMessage) = level;
    void* editor = GoalEditor();
    int size = GameFunction<int(__thiscall*)(void*, uint32_t, void*, int)>(0x004EBB70)(
        editor, level, &Field<uint8_t>(this, kGoalsMessage + 4), kGoalsBufferSize);
    Field<int>(this, kGoalsSize) = size + 4;
}

// 0x005333E0 / 0x005333F0
void* Manager::GetCreatedGoalsMessage() { return &Field<uint8_t>(this, kGoalsMessage); }
uint32_t Manager::GetCreatedGoalsSize() { return Field<uint32_t>(this, kGoalsSize); }

} // namespace GameNet

using GameNet::Manager;
REPLACE(0x00532AA0, &Manager::SetJoinMode);
REPLACE_SOURCE_ONLY(0x00532AC0, &Manager::GetJoinMode);
REPLACE_SOURCE_ONLY(0x00532AD0, &Manager::GetHostMode);
REPLACE(0x00532AE0, &Manager::GetRankTest);
REPLACE(0x00532B30, &Manager::GetRankLimit);
REPLACE(0x00532BE0, &Manager::GetSkillLevel);
REPLACE(0x00532C50, &Manager::GetMaxPlayers);
REPLACE(0x00532CF0, &Manager::GetNumTeams);
REPLACE(0x00532D60, &Manager::GetMaxObservers);
REPLACE(0x00532E30, &Manager::RemoveHostTask);
REPLACE(0x00532E40, &Manager::GetPublicIp);
REPLACE(0x00532E50, &Manager::GetPublicPort);
REPLACE(0x00532E60, &Manager::GetPrivatePort);
REPLACE(0x00532E70, &Manager::SetPublicIp);
REPLACE(0x00532E80, &Manager::SetPublicPort);
REPLACE(0x00532E90, &Manager::SetPrivatePort);
REPLACE(0x00532EA0, &Manager::GetClient);
REPLACE_SOURCE_ONLY(0x00532EB0, &Manager::GetMatchClient);
REPLACE(0x00532EC0, &Manager::GetPlayerInfoFlags);
REPLACE(0x00532ED0, &Manager::FindServersOnLan);
REPLACE(0x00532F70, &Manager::GetNetworkPreferences);
REPLACE(0x00532F80, &Manager::GetTauntPreferences);
REPLACE(0x00532F90, &Manager::SetStartInfoWords);
REPLACE(0x00532FD0, &Manager::GetStartInfoWord);
REPLACE(0x00532FE0, &Manager::IsUntimedGame);
REPLACE(0x00533060, &Manager::SetGameId);
REPLACE_SOURCE_ONLY(0x00533070, &Manager::GetGameId);
REPLACE(0x00533080, &Manager::GetLevelPreference);
REPLACE(0x005330B0, &Manager::GetFireballLevel);
REPLACE(0x00533130, &Manager::TogglePlayerNames);
REPLACE(0x00533140, &Manager::ShouldDrawPlayerNames);
REPLACE(0x00533180, &Manager::SetLastLeader);
REPLACE(0x00533190, &Manager::GetLastLeader);
REPLACE(0x005331A0, &Manager::GetLastLeadingTeam);
REPLACE(0x005331B0, &Manager::SetServerListState);
REPLACE(0x005331C0, &Manager::GetServerListState);
REPLACE(0x005331D0, &Manager::SetFinalScoresReceived);
REPLACE_SOURCE_ONLY(0x005331E0, &Manager::FinalScoresReceived);
REPLACE(0x005331F0, &Manager::SetJoinState);
REPLACE(0x00533200, &Manager::GetJoinState);
REPLACE_SOURCE_ONLY(0x00533210, &Manager::GetCrown);
REPLACE(0x00533220, &Manager::ClearCrown);
REPLACE(0x00533230, &Manager::SetCrownSpawnPoint);
REPLACE(0x00533240, &Manager::SetLobbyId);
REPLACE(0x00533250, &Manager::SendReady);
REPLACE(0x005332A0, &Manager::ReadySent);
REPLACE_SOURCE_ONLY(0x005332B0, &Manager::AllPlayersReady);
REPLACE(0x005332C0, &Manager::SetAllPlayersReady);
REPLACE(0x005332D0, &Manager::ClearAllPlayersReady);
REPLACE_SOURCE_ONLY(0x005332E0, &Manager::CheatingNoted);
REPLACE(0x005332F0, &Manager::NoteCheating);
REPLACE(0x00533300, &Manager::SendObserverCount);
REPLACE(0x00533360, &Manager::GetPendingData);
REPLACE(0x00533380, &Manager::HasCreatedGoals);
REPLACE(0x005333A0, &Manager::PrepareCreatedGoals);
REPLACE(0x005333E0, &Manager::GetCreatedGoalsMessage);
REPLACE(0x005333F0, &Manager::GetCreatedGoalsSize);
