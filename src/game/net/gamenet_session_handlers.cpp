// The network manager's session handlers (GameNet::Manager): the server's answer to a LAN search, the host's options,
// the game's state for a joining player, the level change and the start of a game. See gamenet_handlers.h.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/task.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/net_manager.h"
#include "game/obj/object.h"
#include "game/obj/trick_objects.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "game/ed/rail_editor.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace GameNet {

using namespace Detail;

namespace {

// Names.
constexpr uint32_t kUiString = 0x96875C0F;            // ui_string
constexpr uint32_t kServerName = 0x2A5CF019;          // server_name
constexpr uint32_t kChecksum = 0x21902065;            // checksum
constexpr uint32_t kNetLobby = 0x1C471C60;            // netlobby
constexpr uint32_t kNetScavengerHunt = 0xFC13B531;    // netscavengerhunt
constexpr uint32_t kNetElimiskate = 0x10F84276;       // netelimiskate
constexpr uint32_t kNetGraffiti = 0x5E2EA50C;         // netgraffiti
constexpr uint32_t kNetGoalAttack = 0xEC200EAA;       // netgoalattack
constexpr uint32_t kNetCtf = 0x6C5FF266;              // netctf
constexpr uint32_t kFirefight = 0x3D6D444F;           // firefight
constexpr uint32_t kNetFirefight = 0xBFF33600;        // netfirefight
constexpr uint32_t kScavenger = 0x1F3430CE;           // scavenger
constexpr uint32_t kScavengerInterval = 0x8A6B04AD;   // scavenger_interval
constexpr uint32_t kScavengerCollect = 0xD4F04E7F;    // scavenger_collect_period
constexpr uint32_t kElimiskate = 0xF6198005;          // elimiskate
constexpr uint32_t kElimiskateInterval = 0xF92E5D89;  // elimiskate_interval
constexpr uint32_t kElimiskateBreak = 0xA96CE18F;     // elimiskate_break_period
constexpr uint32_t kTargetScore = 0x71CD53A2;         // target_score
constexpr uint32_t kScore = 0xCD66C8AE;               // score
constexpr uint32_t kTime = 0x906B67BA;                // time
constexpr uint32_t kLevel = 0x651533EC;               // level
constexpr uint32_t kLoadSk5edGameplay = 0xB664035D;   // load_sk5ed_gameplay (the created park)
constexpr uint32_t kChangingLevels = 0xD39572F8;      // net_message_changing_levels
constexpr uint32_t kHideConsoleWindow = 0x53610AA4;   // hide_console_window
constexpr uint32_t kChangeLevel = 0x39C58EA1;         // change_level
constexpr uint32_t kChangeLevelFlag = 0x055ACC01;     // (a flag change_level is given)
constexpr uint32_t kShowWarning = 0xD8E34EDA;         // show_warning
constexpr uint32_t kChosenStartGame = 0x1A64CB61;     // chosen_start_game
constexpr uint32_t kEndGameSelected = 0x65A12F7B;     // fcfc_end_game_selected
constexpr uint32_t kTeamMode = 0xB9D44C2F;            // team_mode
constexpr uint32_t kTeamsNone = 0x6A220326;           // teams_none
constexpr uint32_t kTeamsTwo = 0x768E5A1D;            // teams_two
constexpr uint32_t kTeamsThree = 0x4D04DFA7;          // teams_three
constexpr uint32_t kTeamsFour = 0x85736594;           // teams_four
constexpr uint32_t kDefaultTimeLimit = 0x8D38A280;    // default_time_limit
constexpr uint32_t kTargetScoreArray = 0x50E63314;    // targetScoreArray
constexpr uint32_t kHighestScoreArray = 0x1A2C22F9;   // highestScoreArray
constexpr uint32_t kVictoryConditions = 0x94A24C5E;   // victory_conditions
constexpr uint32_t kFireballLevel = 0xCE87E4E3;       // fireball_level
constexpr uint32_t kStopAtZero = 0x48E748B5;          // stop_at_zero
constexpr uint32_t kShouldResetScores = 0x0B803C5E;   // should_reset_scores
constexpr uint32_t kStartingNewNetGame = 0xA5AE5E25;  // StartingNewNetGame
constexpr uint32_t kInitSelectedGoals = 0x26D9EDC1;   // InitSelectedGoals
constexpr uint32_t kSkaterScore = 0xD262802F;         // SkaterScore (a component)
constexpr uint32_t kUnlimitedTime = 0xF0E712D2;       // unlimited_time
constexpr uint32_t kGoalScript = 0x79F90D81;          // goal_script
constexpr uint32_t kNetGameTypeInfo = 0x2F94C542;     // net_game_type_info
constexpr uint32_t kMpGameTypeInfo = 0x4DA72272;      // mp_game_type_info

// Messages.
constexpr uint8_t kMsgServerInfo = 0x10;
constexpr uint8_t kMsgGameInfo = 0x33;
constexpr uint8_t kMsgGameStateSent = 0x42;
constexpr uint8_t kMsgSpawnedScript = 0x49;
constexpr uint8_t kMsgKing = 0x55;
constexpr uint8_t kMsgTrickObjects = 0x5C;
constexpr uint8_t kMsgGameEnded = 0x6A;
constexpr uint8_t kMsgSelectedGoals = 0x6D;
constexpr uint8_t kMsgGoalsData = 0x87;


// The server's answer to a LAN search (message 0x10, 0xE0 bytes).
#pragma pack(push, 1)
struct ServerInfo {
    int8_t numPlayers;          // +0x00
    int8_t maxPlayers;          // +0x01
    int8_t numObservers;        // +0x02
    int8_t maxObservers;        // +0x03
    bool password;              // +0x04
    bool inGame;                // +0x05 (not in the lobby)
    uint8_t hostMode;           // +0x06
    bool loggedIn;              // +0x07 internet play, logged in to GameSpy
    char name[16];              // +0x08
    char level[32];             // +0x18
    char mode[32];              // +0x38
    uint8_t skillLevel;         // +0x58
    uint8_t pad59[3];
    uint32_t time;              // +0x5C the asker's
    char players[8][16];        // +0x60
};
#pragma pack(pop)
static_assert(sizeof(ServerInfo) == 0xE0);

// A joining player's game information (message 0x33, 0x50 bytes).
struct GameInfo {
    uint8_t crownSpawnPoint;    // +0x00
    bool notLan;                // +0x01 (the connection type isn't 1)
    uint8_t gameId;             // +0x02
    uint8_t proSetFlags;        // +0x03
    uint8_t maxPlayers;         // +0x04
    uint8_t pad05[3];
    uint32_t timeLimit;         // +0x08 seconds
    uint32_t gameType;          // +0x0C
    uint32_t level;             // +0x10
    uint32_t teams;             // +0x14
    uint32_t value18;           // +0x18 (0x00535B20)
    uint32_t value1C;           // +0x1C (0x00535AD0)
    uint32_t value20;           // +0x20 (the game mode's 0x00553170)
    uint32_t targetScore;       // +0x24
    uint32_t startInfo[8];      // +0x28 the manager's start info words
    uint32_t value48;           // +0x48 (the game mode's 0x00553270)
    uint8_t variant;            // +0x4C scavenger hunt / elimiskate: which of its three goals is running
    uint8_t pad4D[3];
};
static_assert(sizeof(GameInfo) == 0x50);

// strncpy(dest, source, 15) and the 16th byte zeroed.
void CopyName(char* dest, const char* source)
{
    size_t length = strnlen(source, kNameSize - 1);
    std::memcpy(dest, source, length);
    std::memset(dest + length, 0, kNameSize - length);
}

Prefs::Preferences* NetworkPreferences(Manager* manager) { return manager->GetNetworkPreferences(); }

// A goal's virtual +0xC: running.
bool GoalRunning(uint8_t* goal) { return reinterpret_cast<bool(__thiscall*)(uint8_t*)>(Field<void**>(goal, 0)[3])(goal); }

} // namespace

// 0x0052DD40
int __cdecl HandleFindServer(Net::MsgHandlerContext* context)
{
    PlayerSearch players;
    NewPlayerSearch newPlayers;
    if (context->m_MsgLength != 0xC)
        return kDone;
    Manager* manager = ManagerOf(context);
    const char* serverName;
    manager->GetNetworkPreferences()->GetPreference(kServerName)->GetString(kUiString, &serverName, 1);
    const uint8_t* msg = Message(context);
    ServerInfo info;
    std::memset(&info, 0, sizeof(info));
    char name[kNameSize];
    CopyName(name, serverName);
    Net::App* server = manager->m_server;
    info.skillLevel = static_cast<uint8_t>(manager->GetSkillLevel());
    info.numPlayers = static_cast<int8_t>(manager->CountPlayers());
    info.maxPlayers = static_cast<int8_t>(manager->GetMaxPlayers());
    info.numObservers = static_cast<int8_t>(manager->CountObservers());
    info.maxObservers = static_cast<int8_t>(manager->GetMaxObservers());
    if (info.maxPlayers < info.numPlayers)
        info.maxPlayers = info.numPlayers;
    if (info.maxObservers < info.numObservers)
        info.maxObservers = info.numObservers;
    info.password = *manager->GetPassword() != '\0';
    info.inGame = GameType() != kNetLobby;
    info.hostMode = static_cast<uint8_t>(manager->GetHostMode());
    info.loggedIn = manager->InInternetMode() && Field<bool>(manager->m_buddyMan, 0x10);
    CopyName(info.name, name);
    std::sprintf(info.level, "%s", manager->GetLevelName(true));
    std::sprintf(info.mode, "%s", manager->GetGameTypeName());
    // The players' names (not observers, unless 0x005434C0), then the new players' not among them yet.
    uint8_t ids[8] = {};
    int idCount = 0;
    info.time = Field<uint32_t>(msg, 0);
    int named = 0;
    for (uint8_t* player = FirstPlayer(manager, &players, true); player; player = NextPlayer(manager, &players, true)) {
        if (IsObserver(player) && !AsPlayer(player)->IsPendingObserver())
            continue;
        if (PlayerSkater(player))
            ids[idCount++] = static_cast<uint8_t>(SkaterId(PlayerSkater(player)));
        CopyName(info.players[named], PlayerName(player));
        ++named;
    }
    char* slot = info.players[named];
    for (uint8_t* added = FirstNewPlayer(manager, &newPlayers); added; added = NextNewPlayer(manager, &newPlayers)) {
        uint32_t flags = Field<uint32_t>(added, 0x20);
        if (!(flags & 0x18) && (flags & 4))
            continue;
        bool known = false;
        for (int i = 0; i < idCount; ++i) {
            if (ids[i] == added[0x10])
                known = true;
        }
        if (known)
            continue;
        std::sprintf(slot, reinterpret_cast<const char*>(added)); // (the name as the format, the original's)
        slot += kNameSize;
    }
    server->SendMessageTo(kMsgServerInfo, sizeof(info), &info, context->m_Conn->m_ip, context->m_Conn->m_port, 0);
    return kContinue;
}

// 0x005301A0
int __cdecl HandleHostOptions(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    auto* host = static_cast<uint8_t*>(manager->GetServerPlayer()); // (the host's own player)
    if (!host || Field<Net::Conn*>(host, 0x18) != context->m_Conn)
        return kDone;
    const uint8_t* msg = Message(context);
    auto word = [msg](uint32_t offset) { return Field<uint32_t>(msg, offset); };
    // (the text / a field of the entry with that checksum in a global list)
    auto optionText = [manager](uint32_t list, uint32_t value) { return manager->GetChoiceName(list, value); };
    auto optionField = [manager](uint32_t list, uint32_t value, uint32_t field) {
        return manager->GetChoiceInteger(list, value, field);
    };
    switch (context->m_MsgId) {
    case 0x60: {
        // The game's options, each a preference: its text and checksum.
        Prefs::Preferences* prefs = NetworkPreferences(manager);
        auto set = [&](uint32_t preference, uint32_t list, uint32_t value, bool fireball = false) {
            Script::CStruct* params = NewParams();
            const char* text = optionText(list, value);
            if (fireball)
                optionField(list, value, kFireballLevel); // (looked up for nothing)
            params->AddString(kUiString, Script::ESYMBOLTYPE_STRING, text);
            params->AddComponentOfType(kChecksum, Script::ESYMBOLTYPE_NAME, value);
            prefs->SetPreference(preference, params);
            DeleteParams(params);
        };
        set(0x9834C4FA, 0x2F94C542, word(0x00));        // game_type: net_game_type_info
        set(0x403DA0D0, 0x48DAE9CB, word(0x08));        // skill_level: skill_level_info
        set(0xCE238F5D, 0xD7E42AF4, word(0x0C), true);  // fireball_difficulty: fireball_level_info
        set(0x043EE978, 0x8BA3F08B, word(0x10));        // player_collision: on_off_types
        set(0xE959C43A, 0x8BA3F08B, word(0x14));        // friendly_fire: on_off_types
        set(0x48E748B5, 0x7FE33468, word(0x20));        // stop_at_zero: boolean_types
        set(0x162E8C3A, 0x7FE33468, word(0x24));        // score_reset: boolean_types
        set(0x15421B73, 0x15EA4F9E, word(0x28));        // ctf_game_type: ctf_type
        set(0x38DBE1D0, 0x38DBE1D0, word(0x2C));        // goals: goals
        set(0xA8774BF7, 0xA8774BF7, word(0x30));        // team_graffiti_mode
        {   // time_limit: time_limit_options, with its time
            Script::CStruct* params = NewParams();
            const char* text = optionText(0x834A7C01, word(0x04));
            int time = optionField(0x834A7C01, word(0x04), kTime);
            params->AddString(kUiString, Script::ESYMBOLTYPE_STRING, text);
            params->AddComponentOfType(kChecksum, Script::ESYMBOLTYPE_NAME, word(0x04));
            params->AddComponentOfType(kTime, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(time));
            prefs->SetPreference(0xC51BF6E3, params);
            DeleteParams(params);
        }
        {   // target_score: target_score_options, else capture_options, else time_limit_options (its time, in ms)
            Script::CStruct* params = NewParams();
            const char* text = optionText(0x52CE135D, word(0x1C));
            int score = optionField(0x52CE135D, word(0x1C), kScore);
            if (!text) {
                text = optionText(0x5D67D0BE, word(0x1C));
                score = optionField(0x5D67D0BE, word(0x1C), kScore);
                if (!text) {
                    text = optionText(0x834A7C01, word(0x1C));
                    score = optionField(0x834A7C01, word(0x1C), kTime) * 1000;
                }
            }
            params->AddString(kUiString, Script::ESYMBOLTYPE_STRING, text);
            params->AddComponentOfType(kChecksum, Script::ESYMBOLTYPE_NAME, word(0x1C));
            if (score > 0)
                params->AddComponentOfType(kScore, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(score));
            prefs->SetPreference(kTargetScore, params);
            DeleteParams(params);
        }
        Run(kChosenStartGame);
        break;
    }
    case 0x61: {
        // Kick (msg[0] = 1) or ban the msg[1]'th player, whose name must be msg + 2.
        PlayerSearch search;
        int index = 0;
        uint8_t* player = FirstPlayer(manager, &search, true);
        for (; player; player = NextPlayer(manager, &search, true), ++index) {
            if (static_cast<int8_t>(msg[1]) == index)
                break;
        }
        if (!player)
            return kDone;
        if (_stricmp(reinterpret_cast<const char*>(msg + 2), PlayerName(player)) != 0)
            return kDone;
        Obj::CObject* skater = PlayerSkater(player);
        bool observer = IsObserver(player);
        manager->DropPlayer(player, msg[0] == 1 ? 3 : 4);
        if (!observer)
            Call(0x00565790, Skate(), skater);
        break;
    }
    case 0x62: {
        Script::CStruct* params = NewParams();
        params->AddComponentOfType(kLevel, Script::ESYMBOLTYPE_NAME, word(0));
        params->AddChecksum(kChangeLevelFlag, kChangeLevelFlag);
        params->AddChecksum(kShowWarning, kShowWarning);
        Run(kChangeLevel, params);
        DeleteParams(params);
        break;
    }
    case 0x64:
        Call(0x00558960, GoalManager(), word(0));
        break;
    case 0x65: {
        // End the game: everyone else told.
        PlayerSearch search;
        Net::MsgDesc desc;
        desc.Construct();
        Net::App* server = manager->m_server;
        Run(kEndGameSelected);
        desc.m_id = kMsgGameEnded;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        for (uint8_t* p = FirstPlayer(manager, &search, true); p; p = NextPlayer(manager, &search, true)) {
            if (!IsLocalPlayer(p))
                server->EnqueueMessage(PlayerHandle(p), &desc);
        }
        break;
    }
    case 0x66: {
        // The number of teams.
        int teams = static_cast<int8_t>(msg[0]);
        Script::CStruct* count = NewParams();
        count->AddInteger(0u, teams);
        Script::CStruct* params = NewParams();
        static const char* const kTexts[] = {"None", nullptr, "2", "3", "4"};
        static const uint32_t kTeams[] = {kTeamsNone, 0, kTeamsTwo, kTeamsThree, kTeamsFour};
        if (teams >= 0 && teams <= 4 && teams != 1) {
            params->AddString(kUiString, kTexts[teams]);
            params->AddChecksum(kChecksum, kTeams[teams]);
        }
        NetworkPreferences(manager)->SetPreference(kTeamMode, params);
        DeleteParams(params);
        CFunc_SetNumTeams(count, nullptr);
        DeleteParams(count);
        break;
    }
    default: // (0x63: nothing)
        break;
    }
    return kContinue;
}

// 0x0052F4A0 is the level change's wait task (gamenet_tasks.cpp).

GAME_GLOBAL(uint32_t, g_levelChangeTime, 0x007CCDE8);

// 0x005311F0
int __cdecl HandleChangeLevel(Net::MsgHandlerContext* context)
{
    Manager* manager = ManagerOf(context);
    const uint8_t* msg = Message(context);
    manager->ClearQueuedScripts();
    manager->m_gameOver = false;
    if (!msg[4])
        return kContinue;
    auto* logicTasks = reinterpret_cast<Tsk::Stack*>(GameVariable<uint8_t*>(0x00696D68) + 0x04);
    auto* changeTask = Field<Tsk::BaseTask*>(manager, 0xEC);
    if (changeTask->InList()) {
        if (!manager->OnServer())
            manager->m_level = Field<uint32_t>(msg, 0);
        return kDone;
    }
    uint32_t oldLevel = manager->m_level;
    manager->m_level = Field<uint32_t>(msg, 0);
    g_levelChangeTime = Tmr::GetTime() + 3000; // when to change
    if (manager->OnServer()) {
        Script::CStruct* params = NewParams();
        params->AddString(kUiString, Script::ESYMBOLTYPE_STRING, manager->GetLevelName(false));
        params->AddComponentOfType(kChecksum, Script::ESYMBOLTYPE_NAME, manager->m_level);
        manager->GetNetworkPreferences()->SetPreference(kLevel, params);
        DeleteParams(params);
        if (manager->m_level == kLoadSk5edGameplay)
            g_levelChangeTime += 1000;
    } else if (oldLevel == kLoadSk5edGameplay && manager->m_level != oldLevel) {
        Ed::CRailEditorComponent::Instance()->Clear(); // the created park's rails gone
    }
    logicTasks->AddTask(changeTask);
    manager->CreateNetPanelMessage(false, kChangingLevels, manager->GetLevelName(true), nullptr, nullptr, nullptr, false,
                                   5000);
    Run(kHideConsoleWindow);
    return kDone;
}

// 0x005313C0
int __cdecl HandleStartGame(Net::MsgHandlerContext* context)
{
    PlayerSearch search;
    Mem::Manager* memory = Mem::Manager::Get();
    uint8_t* skate = Skate();
    memory->PushContext(Field<Mem::Allocator*>(memory, 0x5D4));
    const uint8_t* msg = Message(context);
    Manager* manager = ManagerOf(context);
    auto word = [msg](uint32_t offset) { return Field<uint32_t>(msg, offset); };
    manager->SetStartInfoWords(reinterpret_cast<const uint32_t*>(msg + 0x28));
    Field<int>(manager, 0x68) = static_cast<int8_t>(msg[0x24]); // the crown's spawn point
    Field<bool>(manager, 0x1B5) = false;
    for (uint8_t* p = FirstPlayer(manager, &search, false); p; p = NextPlayer(manager, &search, false)) {
        if (!IsLocalPlayer(p)) {
            static_cast<Obj::CSkater*>(PlayerSkater(p))->HideSkater();
            static_cast<Obj::CSkater*>(PlayerSkater(p))->Reset27C();
        }
    }
    Call(0x00553AD0, GameMode(), word(0));
    Call(0x00558250, Call<void*>(0x00564D90, skate, 0));
    Script::CArray* types = Script::GetArray(manager->InNetGame() ? kNetGameTypeInfo : kMpGameTypeInfo);
    // The victory conditions.
    Script::CStruct* victory = NewParams();
    if (word(4) == 0 && word(0) != kNetGoalAttack) {
        victory->AddComponentOfType(kDefaultTimeLimit, Script::ESYMBOLTYPE_INTEGER, 0);
        if (word(0) != kFirefight && word(0) != kNetFirefight) {
            Script::CArray* conditions = Script::NewArray();
            conditions = conditions ? conditions->Init() : nullptr;
            Script::CopyArray(conditions, Script::GetArray(kTargetScoreArray));
            auto* condition = reinterpret_cast<Script::CStruct*>(conditions->GetElement(0));
            condition->AddComponentOfType(kScore, Script::ESYMBOLTYPE_INTEGER, word(8));
            victory->AddArray(kVictoryConditions, conditions);
        }
    } else {
        victory->AddComponentOfType(kDefaultTimeLimit, Script::ESYMBOLTYPE_INTEGER, word(4));
        if (word(0) == kNetCtf) {
            Script::CArray* conditions = Script::NewArray();
            conditions = conditions ? conditions->Init() : nullptr;
            Script::CopyArray(conditions, Script::GetArray(kHighestScoreArray));
            victory->AddArray(kVictoryConditions, conditions);
        }
    }
    victory->AddComponentOfType(kFireballLevel, Script::ESYMBOLTYPE_INTEGER, word(0xC));
    victory->AddComponentOfType(kStopAtZero, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(static_cast<int8_t>(msg[0x14])));
    victory->AddComponentOfType(kShouldResetScores, Script::ESYMBOLTYPE_INTEGER,
                                static_cast<uint32_t>(static_cast<int8_t>(msg[0x15])));
    Call(0x00564D50, skate, word(4));
    Call(0x00553BE0, GameMode(), victory);
    Call(0x005535A0, GameMode(), word(0x18));
    Call(0x005530A0, GameMode(), word(0x1C));
    Call(0x00553080, GameMode(), word(0x20));
    DeleteParams(victory);
    Run(kStartingNewNetGame);
    Call(0x00566A60, skate);
    manager->ResetPlayers();
    Field<bool>(manager, 0x1B4) = false;
    Field<bool>(manager, 0x3B) = false;
    manager->SetFinalScoresReceived(false);
    manager->SetLastLeader(nullptr);
    // The last leading team (+0x1D4) none: the original calls the setter folded at 0x005430D0 (PlayerInfo's
    // SetReadyQueryOwed, the same offset) on the manager.
    Field<int>(manager, 0x1D4) = -1;
    manager->SetGameId(msg[0x16]);
    if (word(0) == kNetGoalAttack)
        Run(kInitSelectedGoals);
    manager->m_gameOver = false;
    // Every player's flags and score reset.
    for (uint8_t* p = FirstPlayer(manager, &search, true); p; p = NextPlayer(manager, &search, true)) {
        AsPlayer(p)->ClearFlags();
        AsPlayer(p)->ClearLastHitTime();
        if (Obj::CObject* skater = PlayerSkater(p)) {
            auto* score = reinterpret_cast<uint8_t*>(static_cast<Obj::CCompositeObject*>(skater)->GetComponent(kSkaterScore)); // its score component
            if (Field<void*>(score, 0x18))
                Call(0x00564A10, Field<void*>(score, 0x18));
        }
    }
    if (uint8_t* king = KingOfTheHill(manager))
        AsPlayer(king)->MarkAsKing(false);
    // The mode's goal script. (The original reads each type's checksum into its argument's slot, holding the type
    // info array at first: a type without one compares that - or the previous type's checksum.)
    auto checksum = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(types));
    for (int i = 0; i < static_cast<int>(types->m_size); ++i) {
        auto* type = reinterpret_cast<Script::CStruct*>(types->GetElement(static_cast<uint32_t>(i)));
        type->GetChecksum(kChecksum, &checksum, 1);
        if (checksum != word(0))
            continue;
        Script::CStruct* params = NewParams();
        params->AddInteger(kTime, static_cast<int>(word(4)));
        params->AddInteger(kScore, static_cast<int>(word(8)));
        params->AddInteger(kUnlimitedTime, word(4) == 0 ? 1 : 0);
        // (with no goal_script the original runs whatever its local held: the skate module's address)
        auto script = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(skate));
        type->GetChecksum(kGoalScript, &script, 1);
        Run(script, params);
        DeleteParams(params);
        break;
    }
    Mem::Manager::Get()->PopContext();
    return kContinue;
}

// 0x00532460
int __cdecl HandleSendGameState(Net::MsgHandlerContext* context)
{
    Net::Manager* net = Net::Manager::Instance();
    uint8_t* skate = Skate();
    Manager* manager = ManagerOf(context);
    uint8_t* player = PlayerByConnection(manager, context->m_Conn);
    if (!player || (Field<uint32_t>(player, 0x1D0) & 0x40))
        return kContinue;
    Net::MsgDesc desc;
    desc.Construct();
    Tmr::GetTime(); // (not used)
    Field<uint32_t>(player, 0x1D0) |= 0x40; // told
    GameInfo info;
    std::memset(&info, 0, sizeof(info));
    info.maxPlayers = static_cast<uint8_t>(manager->GetMaxPlayers());
    info.value48 = Call<uint32_t>(0x00553270, GameMode());
    info.variant = 0;
    Net::App* server = manager->m_server;
    if (IsLocalPlayer(player)) {
        manager->SetReadyToPlay(true); // (ready to play)
    } else {
        if (manager->IsGameOver()) {
            info.gameType = kNetLobby;
            info.timeLimit = 0;
            info.value20 = 0;
            info.targetScore = 0;
        } else {
            info.gameType = GameType();
            info.timeLimit = Call<uint32_t>(0x005533B0, GameMode()) / 1000;
            info.value20 = Call<uint32_t>(0x00553170, GameMode());
            info.targetScore = static_cast<uint32_t>(manager->GetNetworkPreferences()->GetPreferenceValue(kTargetScore, kScore));
            if (GameType() == kNetScavengerHunt || GameType() == kNetElimiskate) {
                bool scavenger = GameType() == kNetScavengerHunt;
                uint8_t* goals = GoalManager();
                if (GoalRunning(GetGoal(goals, scavenger ? kScavenger : kElimiskate)))
                    info.variant = 0;
                else if (GoalRunning(GetGoal(goals, scavenger ? kScavengerInterval : kElimiskateInterval)))
                    info.variant = 1;
                else if (GoalRunning(GetGoal(goals, scavenger ? kScavengerCollect : kElimiskateBreak)))
                    info.variant = 2;
            }
        }
        info.gameId = manager->GetGameId();
        info.level = manager->GetLevel();
        info.teams = Call<uint32_t>(0x00553130, GameMode());
        info.value18 = manager->GetGoalsPreference();
        info.value1C = manager->GetTeamGraffitiMode();
        info.proSetFlags = Field<uint8_t>(manager, 0x194);
        info.crownSpawnPoint = Field<uint8_t>(manager, 0x68);
        std::memcpy(info.startInfo, &Field<uint32_t>(manager, 0x48), sizeof(info.startInfo));
        info.notLan = net->GetConnectionType() != 1;
        desc.m_data = &info;
        desc.m_length = sizeof(info);
        desc.m_id = kMsgGameInfo;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        server->EnqueueMessage(PlayerHandle(player), &desc);
        if (manager->HasCreatedGoals()) {
            manager->PrepareCreatedGoals(manager->GetLevel());
            void* goals = manager->GetCreatedGoalsMessage();
            auto size = static_cast<uint16_t>(manager->GetCreatedGoalsSize());
            server->StreamMessage(PlayerHandle(player), kMsgGoalsData, size, goals, "goals data", kGroupGame, false,
                                  true);
        }
        // The scripts spawned so far (the list at +0x150): 0x49 each.
        auto* node = Field<Mem::ListNode*>(manager, 0x150);
        node = node->data == reinterpret_cast<void*>(-1) ? nullptr : node;
        for (auto* item = node ? static_cast<uint8_t*>(node->data) : nullptr; item;) {
            desc.Construct();
            uint32_t spawned[4] = {Field<uint32_t>(item, 0x1C), Field<uint32_t>(item, 0x18), Field<uint32_t>(item, 0x14),
                                   0};
            desc.m_data = spawned;
            desc.m_length = sizeof(spawned);
            desc.m_id = kMsgSpawnedScript;
            desc.m_queue = Net::MsgDesc::kQueueSequenced;
            desc.m_groupId = kGroupGame;
            server->EnqueueMessage(PlayerHandle(player), &desc);
            node = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            if (!node)
                break;
            item = static_cast<uint8_t*>(node->data);
        }
        if (!IsLocalPlayer(player))
            Call(0x0056B990, skate, player);
        if (IsObserver(player) && GameType() == kNetGraffiti) {
            // An observer of graffiti: the trick objects' state.
            desc.Construct();
            static uint8_t buffer[0x1100]; // (the original's is on its stack)
            int size = GameVariable<Obj::CTrickObjectManager*>(0x007CE500)->WriteGraffitiState(buffer);
            server->StreamMessage(PlayerHandle(player), kMsgTrickObjects, static_cast<uint16_t>(size), buffer,
                                  "TrickObj Buffer", kGroupGame, true, false);
        }
        // The goals chosen (goal attack): a byte, then the selected goals' names, then 0.
        desc.Construct();
        uint8_t* goals = GoalManager();
        static uint8_t selected[0x1000];
        selected[0] = 0;
        uint8_t* out = selected + 1;
        int count = Field<int>(goals, 0); // (its first word, read by the getter folded at 0x00469490)
        for (int i = 0; i < count; ++i) {
            uint8_t* goal = Call<uint8_t*>(0x00557FB0, goals, i);
            if (Call<bool>(0x005542F0, goal)) {
                uint32_t name = Call<uint32_t>(0x00554280, goal);
                std::memcpy(out, &name, 4);
                out += 4;
            }
        }
        std::memset(out, 0, 4);
        desc.m_data = selected;
        desc.m_id = kMsgSelectedGoals;
        desc.m_length = static_cast<uint16_t>(out - selected + 4);
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        server->EnqueueMessage(PlayerHandle(player), &desc);
        if (uint8_t* king = KingOfTheHill(manager)) {
            desc.Construct();
            uint8_t id = static_cast<uint8_t>(SkaterId(PlayerSkater(king)));
            desc.m_data = &id;
            desc.m_length = 1;
            desc.m_id = kMsgKing;
            desc.m_queue = Net::MsgDesc::kQueueSequenced;
            desc.m_groupId = kGroupGame;
            server->EnqueueMessage(PlayerHandle(player), &desc);
        }
    }
    // The end of the game's state (0x42).
    Net::MsgDesc done;
    done.Construct();
    done.m_id = kMsgGameStateSent;
    done.m_queue = Net::MsgDesc::kQueueSequenced;
    done.m_groupId = kGroupGame;
    server->EnqueueMessage(PlayerHandle(player), &done);
    if (manager->InNetGame()) {
        manager->SendFacesTo(player);
        manager->SendTagTexturesTo(player);
    }
    return kContinue;
}

} // namespace GameNet

REPLACE(0x0052DD40, GameNet::HandleFindServer);
REPLACE(0x005301A0, GameNet::HandleHostOptions);
REPLACE(0x005311F0, GameNet::HandleChangeLevel);
REPLACE(0x005313C0, GameNet::HandleStartGame);
REPLACE(0x00532460, GameNet::HandleSendGameState);
