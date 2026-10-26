// The network game's course (GameNet::Manager): back to the lobby, a game started (message 0x34), a join cancelled,
// and the manager's tasks for the observer, auto-serve, the join's state and the server's start of a game
// (0x0053AF90, 0x0053B0D0, 0x0053BED0-0x0053C320, 0x0053C6E0). See gamenet_manager.h.

#include <cstdint>
#include <cstring>

#include "game/core/stubs.h"
#include "game/core/task.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/mth/random.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_commands.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/gamenet_observer.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "game/net/stats_man.h"
#include "game/obj/smtptr.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint8_t kMsgStartGame = 0x34;
constexpr uint32_t kRootWindow = 0x56A1EAE3;   // root_window (a screen element)
constexpr uint32_t kMenuState = 0xCDC00E63;    // menu_state (a tag)
constexpr uint32_t kOn = 0xF649D637;           // on

// The root window (its smart pointer let go at once: the raw pointer kept, as the original).
Obj::CObject* FindRootWindow()
{
    Front::ElementPtr found;
    return Front::CScreenElementManager::Instance()->GetElement(&found, kRootWindow, 0)->mp_object;
}
constexpr uint8_t kMsgDisconnect = 0x0B; // (the client leaves the server)

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;     // netlobby
constexpr uint32_t kLevel = 0x651533EC;        // level (the network preference)
constexpr uint32_t kGameType = 0x9834C4FA;     // game_type
constexpr uint32_t kChecksum = 0x21902065;     // checksum
constexpr uint32_t kTime = 0x906B67BA;           // time
constexpr uint32_t kHorseTimeLimit = 0xC146142E; // horse_time_limit
constexpr uint32_t kTimeLimit = 0xC51BF6E3;      // time_limit
constexpr uint32_t kTargetScore = 0x71CD53A2;    // target_score
constexpr uint32_t kScore = 0xCD66C8AE;          // score
constexpr uint32_t kCtfGameType = 0x15421B73;    // ctf_game_type
constexpr uint32_t kTimedCtf = 0x938AB24B;       // timed_ctf
constexpr uint32_t kScoreReset = 0x162E8C3A;     // score_reset
constexpr uint32_t kBooleanTrue = 0xF81BC89B;    // boolean_true
constexpr uint32_t kGoals = 0x38DBE1D0;          // goals (the created goals chosen)
constexpr uint32_t kHorse = 0x9D65D0E7;          // horse
constexpr uint32_t kNetCtf = 0x6C5FF266;         // netctf
// Game types without a time limit (0 sent): netgoalattack, king, netking, netscorechallenge, scorechallenge, firefight,
// netfirefight.
constexpr uint32_t kUntimedTypes[] = {0xEC200EAA, 0x5D32129C, 0x6EF8FDA0, 0x1498240A, 0xF135ECB6, 0x3D6D444F, 0xBFF33600};

// Scripts.
constexpr uint32_t kLoadPendingPlayers = 0x895819F7;       // LoadPendingPlayers
constexpr uint32_t kStartNetworkGame = 0x6CD351C5;         // StartNetworkGame
constexpr uint32_t kCreateConnectingDialog = 0xA107181A;   // CreateConnectingDialog
constexpr uint32_t kCreateJoiningDialog = 0xB0637172;      // CreateJoiningDialog
constexpr uint32_t kCreateTryingPasswordDialog = 0x4E2ED110; // CreateTryingPasswordDialog
constexpr uint32_t kEnteredNetworkGame = 0xFF898BE6;       // entered_network_game
constexpr uint32_t kDialogBoxExit = 0x666B6A9D;            // dialog_box_exit
constexpr uint32_t kCreateJoinRefusedDialog = 0x92528E98;  // CreateJoinRefusedDialog
constexpr uint32_t kReason = 0xC44777F3;                   // reason
constexpr uint32_t kCurrentMenuAnchor = 0xF53D1D83;        // current_menu_anchor
constexpr uint32_t kCreateWaitForPlayersDialog = 0xEF32B8DE; // CreateWaitForPlayersDialog
constexpr uint32_t kGameWillStart = 0x9DB2FD76;            // net_message_game_will_start
constexpr uint32_t kHomieStatusChatting = 4; // (the presence status while in the lobby)

constexpr uint32_t kLinkLostDialogAnchor = 0xE8E406BC;      // link_lost_dialog_anchor
constexpr uint32_t kDialogBoxAnchor = 0x3B56E746;           // dialog_box_anchor
constexpr uint32_t kCreateLinkUnpluggedDialog = 0x751DE012; // create_link_unplugged_dialog
constexpr uint32_t kCreateLinkUnpluggedFrontEndDialog = 0xCFA1CF26; // create_link_unplugged_front_end_dialog
constexpr uint32_t kShowJoinTimeoutNotice = 0x3F7009B4;     // ShowJoinTimeoutNotice
constexpr uint32_t kCreateLostConnectionDialog = 0xDA9AFFBF; // CreateLostConnectionDialog
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;              // (the skate module's next level: the main menu)
constexpr uint32_t kLinkCheckInterval = 1000;
constexpr uint32_t kSilentTimeout = 20000;  // ms without a word from a connection in the game
constexpr uint32_t kJoiningTimeout = 60000; // ... from one still joining
constexpr int kMaxResends = 20;
constexpr int kDropTimedOut = 1;
constexpr int kDropDropped = 5;

constexpr uint32_t kAutoServeWait = 2000;    // ms after the host mode is set
constexpr uint32_t kAutoServeIdle = 5000;    // ms in the lobby
constexpr int kPanelTime = 2000;

// PlayerInfo flags (+0x1D0).
constexpr uint32_t kPlayerCaptureBlocked = 0x8000;
// NewPlayerInfo flags (+0x20).
constexpr uint32_t kNewPlayerLocal = 1;
constexpr uint32_t kNewPlayerObserver = 4;

// The start of a game (message 0x34, 0x48 bytes; read by the client's HandleStartGame 0x005313C0).
struct StartGameInfo {
    uint32_t gameType;      // +0x00
    uint32_t timeLimit;     // +0x04 (0: none)
    uint32_t targetScore;   // +0x08
    uint32_t fireballLevel; // +0x0C
    uint32_t unused10;      // +0x10 (never written: the stack's)
    bool stopAtZero;        // +0x14 IsUntimedGame
    bool resetScores;       // +0x15
    uint8_t gameId;         // +0x16 (1-255, not the last game's)
    uint8_t unused17;
    uint32_t teams;         // +0x18
    uint32_t goals;         // +0x1C the goals preference's checksum
    uint32_t value20;       // +0x20 (0x00535AD0)
    int8_t crownSpawnPoint; // +0x24
    uint8_t unused25[3];
    uint32_t startInfo[8];  // +0x28 the manager's start info words (+0x48)
};
static_assert(sizeof(StartGameInfo) == 0x48 && offsetof(StartGameInfo, stopAtZero) == 0x14 &&
              offsetof(StartGameInfo, teams) == 0x18 && offsetof(StartGameInfo, startInfo) == 0x28);

Tsk::Stack* LogicTasks()
{
    return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(GameVariable<void*>(0x00696D68)) + 4);
}
Prefs::Preferences* NetworkPreferences(Manager* m) { return reinterpret_cast<Prefs::Preferences*>(m->m_networkPreferences); }
// The preferences of the game being set up: the network game's, else the skate module's (split screen, 0x00564D80).
Prefs::Preferences* GamePreferences(Manager* m)
{
    return m->InNetGame() ? NetworkPreferences(m) : Call<Prefs::Preferences*>(0x00564D80, Skate());
}
uint32_t PreferenceChecksum(Prefs::Preferences* prefs, uint32_t name, uint32_t field)
{
    uint32_t value;
    prefs->GetPreference(name)->GetChecksum(field, &value, 1);
    return value;
}
Manager* TaskManager(void* task) { return Field<Manager*>(task, 0x1C); }

} // namespace

// 0x0053AF90
void Manager::ResetToLobby()
{
    int teams;
    if (OnServer()) {
        m_level = PreferenceChecksum(NetworkPreferences(this), kLevel, kChecksum);
        teams = GetNumTeams();
    } else {
        teams = Call<int>(0x00553130, GameMode());
    }
    Field<uint32_t>(this, 0x30) = kNetLobby;
    Call(0x00553AD0, GameMode(), kNetLobby); // the game mode
    Call(0x005535A0, GameMode(), teams);
    m_gameOver = false;
    ResetPlayers();
    if (uint8_t* king = KingOfTheHill(this))
        AsPlayer(king)->MarkAsKing(false);
    PlayerSearch search;
    for (uint8_t* p = FirstPlayer(this, &search, true); p; p = NextPlayer(this, &search, true))
        AsPlayer(p)->ClearFlags(); // carries no flag
    ClearQueuedScripts();
    Field<uint32_t>(this, 0x1B0) = Tmr::GetTime(); // (back in the lobby: auto-serve waits from here)
}

// 0x0053B0D0
void Manager::StartNetworkGame()
{
    PlayerSearch search;
    Net::MsgDesc desc;
    desc.Construct();
    Prefs::Preferences* prefs = GamePreferences(this);
    Net::App* server = m_server;
    RemoveAllNewPlayers();
    Field<uint32_t>(this, 0x30) = PreferenceChecksum(GamePreferences(this), kGameType, kChecksum);
    ChooseStartPoints(); // the skaters' starting points shuffled
    std::memset(&Field<uint8_t>(this, 0x70), 0, 0x50);
    uint32_t gameType = Field<uint32_t>(this, 0x30);
    uint32_t timeLimit =
        prefs->GetPreferenceValue(gameType == kHorse ? kHorseTimeLimit : kTimeLimit, kTime);
    m_level = PreferenceChecksum(NetworkPreferences(this), kLevel, kChecksum);
    StartGameInfo info;
    info.gameType = Field<uint32_t>(this, 0x30);
    info.fireballLevel = static_cast<uint32_t>(GetFireballLevel());
    bool untimed = false;
    for (uint32_t type : kUntimedTypes)
        untimed = untimed || info.gameType == type;
    if (untimed)
        info.timeLimit = 0;
    else if (info.gameType == kNetCtf) // (capture the flag: only the timed kind)
        info.timeLimit = prefs->GetPreferenceChecksum(kCtfGameType, kChecksum) == kTimedCtf ? timeLimit : 0;
    else
        info.timeLimit = timeLimit;
    info.resetScores = prefs->GetPreferenceChecksum(kScoreReset, kChecksum) == kBooleanTrue;
    info.stopAtZero = IsUntimedGame();
    info.targetScore = prefs->GetPreferenceValue(kTargetScore, kScore);
    info.teams = static_cast<uint32_t>(GetNumTeams());
    uint32_t value = 0;
    GamePreferences(this)->GetPreference(kGoals)->GetChecksum("checksum", &value, 1);
    info.goals = value;
    info.value20 = GetTeamGraffitiMode();
    do
        info.gameId = static_cast<uint8_t>(Mth::Rnd(0xFF) + 1);
    while (info.gameId == Field<uint8_t>(this, 0x39));
    info.crownSpawnPoint = Field<int8_t>(this, 0x68);
    std::memcpy(info.startInfo, &Field<uint32_t>(this, 0x48), sizeof(info.startInfo));
    desc.m_data = &info;
    desc.m_length = sizeof(info);
    desc.m_id = kMsgStartGame;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    for (uint8_t* p = FirstPlayer(this, &search, true); p; p = NextPlayer(this, &search, true)) {
        Field<uint32_t>(p, 0x24) = 0;
        AsPlayer(p)->ClearFlags(); // carries no flag
        Field<uint32_t>(p, 0x1D0) &= ~kPlayerCaptureBlocked;
        server->EnqueueMessage(PlayerHandle(p), &desc);
        if (!InNetGame())
            break; // (split screen: the first player only)
    }
    if (uint8_t* king = KingOfTheHill(this))
        AsPlayer(king)->MarkAsKing(false);
    Field<void*>(this, 0x1D0) = nullptr;   // the last leader
    Field<int>(this, 0x1D4) = -1;          // the last leading team
    ClearQueuedScripts();
    ResetPlayers();
    GameVariable<bool>(0x007CE4FD) = false;
    if (static_cast<StatsMan*>(m_statsMan)->UpdateLoggedIn())
        static_cast<StatsMan*>(m_statsMan)->StartGame();
}

// 0x0053C6E0
void Manager::CancelJoin()
{
    Net::MsgDesc desc;
    desc.Construct();
    if (m_joinState == 10 || m_joinState == 8 || m_joinState == kJoinStateRefused)
        return;
    Net::App* client = m_client;
    desc.m_id = kMsgDisconnect;
    SendToServer(client, &desc);
    client->SendData(false);
    DisconnectClients();
    m_joinTimeoutTask->Remove();
    RemoveAllNewPlayers();
    if (Field<bool>(m_buddyMan, 0x10)) // (logged in to the presence)
        static_cast<BuddyMan*>(m_buddyMan)->SetStatus(kHomieStatusChatting, Script::GetString("homie_status_chatting"),
                                                      static_cast<char*>(m_lobbyMan) + 0x34);
}

// 0x0053BED0
void __cdecl Manager::ObserverTask(void*)
{
    Manager* manager = GameVariable<Manager*>(0x007CCE14);
    if (manager->m_observerCommand == 1) { // (the next player asked for)
        manager->ObservePlayer(static_cast<uint8_t*>(manager->NextCamPlayer()));
        manager->m_observerCommand = 0;
    }
}

// 0x00536890
void __cdecl Manager::ObserverInput(void* handler)
{
    auto* input = static_cast<ObserverInputHandler*>(handler);
    Manager* manager = input->m_data;
    Obj::CObject* root = FindRootWindow();
    if (root) {
        // Not while a menu is up: the root window's menu_state tag on.
        Script::CStruct* tags = NewParams();
        root->CopyTagsTo(tags);
        uint32_t menuState = 0; // (left as it was on the stack when the root window has no menu_state: the original)
        tags->GetChecksum(kMenuState, &menuState, 0);
        DeleteParams(tags);
        if (menuState == kOn)
            return;
    }
    if (input->m_controlData[0x20] & 0x40) // (the controller's button: the next player)
        manager->m_observerCommand = 1;
}

// 0x0053BF00
void __cdecl Manager::AutoServeTask(void* task)
{
    Manager* manager = TaskManager(task);
    if (Tmr::GetTime() - GameVariable<uint32_t>(0x007CCDEC) < kAutoServeWait || GameType() != kNetLobby ||
        manager->CountPlayers() < 2 || manager->m_waitingForGameToStart ||
        Tmr::GetTime() - Field<uint32_t>(manager, 0x1B0) <= kAutoServeIdle)
        return;
    Run(kLoadPendingPlayers);
    Run(kStartNetworkGame);
    manager->m_waitingForGameToStart = true;
}

// 0x0053BF90
void __cdecl Manager::JoinStateTask(void* task)
{
    Manager* manager = TaskManager(task);
    switch (manager->m_joinState) {
    case kJoinStateConnecting: // 1
        Run(kCreateConnectingDialog);
        manager->m_joinState = 0;
        break;
    case kJoinStateJoining: // 2 (none for the host's own client)
        if (!manager->m_client->IsLocal())
            Run(kCreateJoiningDialog);
        manager->m_joinState = 0;
        break;
    case kJoinStateJoiningWithPassword: // 3
        Run(kCreateTryingPasswordDialog);
        manager->m_joinState = kJoinStateTryingPassword;
        break;
    case 6: { // joined
        NewPlayerSearch search;
        uint8_t* info = FirstNewPlayer(manager, &search);
        for (; info && !(Field<uint32_t>(info, 0x20) & kNewPlayerLocal); info = NextNewPlayer(manager, &search)) {
        }
        if (info && (Field<uint32_t>(info, 0x20) & kNewPlayerObserver)) { // the local player joined to observe
            Call(0x00565790, Skate(), Call<void*>(0x00566520, Skate())); // the local skater removed
            manager->ObservePlayer(nullptr);
        }
        Run(kEnteredNetworkGame);
        manager->m_joinState = 7;
        break;
    }
    case 8: // the join over
        Run(kDialogBoxExit);
        manager->m_joinTimeoutTask->Remove();
        manager->m_joinStateTask->Remove();
        break;
    case kJoinStateRefused: { // 9
        Script::CStruct* params = NewParams();
        params->AddChecksum(kReason, manager->m_refusedReason);
        manager->m_joinStateTask->Remove();
        Run(kCreateJoinRefusedDialog, params);
        DeleteParams(params);
        break;
    }
    case 10: // cancelled
        manager->m_joinStateTask->Remove();
        break;
    default:
        break;
    }
}

// 0x0053C1F0
void __cdecl Manager::StartGameTask(void* task)
{
    Manager* manager = TaskManager(task);
    bool& waitingShown = GameVariable<bool>(0x007CCE2C);
    if (manager->CountOtherPlayers() > 0) { // players still loading
        if (manager->OnServer()) {
            if (!waitingShown) {
                Front::ElementPtr menu;
                Front::CScreenElementManager::Instance()->GetElement(&menu, kCurrentMenuAnchor, 0);
                if (!menu.Get()) { // (no menu up)
                    Run(kCreateWaitForPlayersDialog);
                    waitingShown = true;
                }
            }
        } else if (!waitingShown) {
            manager->CreateNetPanelMessage(true, kGameWillStart, nullptr, nullptr, nullptr, "netexceptionprops", false,
                                           kPanelTime);
            waitingShown = true;
        }
        return;
    }
    if (CFunc_AllPlayersAreReady(nullptr, nullptr) && // (the script command, without parameters)
        Tmr::GetTime() >= GameVariable<uint32_t>(0x007CCDF4)) { // every one ready, and the time has come
        waitingShown = false;
        manager->StartNetworkGame();
        Field<Tsk::BaseTask*>(manager, 0xE8)->Remove();
    }
}

// 0x0053EB70
void __cdecl Manager::ConnectionTask(void* task)
{
    Manager* manager = TaskManager(task);
    uint8_t* skate = Skate();
    uint32_t& checked = GameVariable<uint32_t>(0x007CCE30); // the link checked last
    if (manager->InNetMode() && Tmr::GetTime() - checked > kLinkCheckInterval) {
        bool netGame = manager->InNetGame();
        checked = Tmr::GetTime();
        // the network cable unplugged: a method of the network object (0x006EFDD8) - on the PC the shared
        // return-false stub 0x005B9E50
        if (Stubs::ReturnFalse()) {
            Front::ElementPtr dialog;
            Front::CScreenElementManager::Instance()->GetElement(&dialog, netGame ? kLinkLostDialogAnchor : kDialogBoxAnchor, 0);
            if (!dialog.Get()) {
                Run(netGame ? kCreateLinkUnpluggedDialog : kCreateLinkUnpluggedFrontEndDialog);
                return;
            }
        }
    }
    if (manager->OnServer()) { // the players' connections watched
        PlayerSearch search;
        uint8_t* player = FirstPlayer(manager, &search, true);
        while (player) {
            uint8_t* next = NextPlayer(manager, &search, true);
            Net::Conn* conn = Field<Net::Conn*>(player, 0x18);
            if (!conn->IsLocal()) {
                int reason = -1;
                if (conn->m_status & 1) { // (in the game)
                    if (conn->GetTimeElapsedSinceCommunication() > kSilentTimeout)
                        reason = kDropTimedOut;
                    else if (conn->GetHighestResendCount() > kMaxResends)
                        reason = kDropDropped;
                } else if (conn->GetTimeElapsedSinceCommunication() > kJoiningTimeout) {
                    reason = kDropTimedOut;
                }
                if (reason >= 0) {
                    Obj::CObject* skater = PlayerSkater(player);
                    bool observer = IsObserver(player);
                    manager->DropPlayer(player, reason);
                    if (!observer)
                        Call(0x00565790, skate, skater);
                }
            }
            player = next;
        }
        return;
    }
    // A client: the server silent too long while joining or joined.
    Net::ConnSearch search = {reinterpret_cast<void**>(0x0064759C), nullptr};
    Net::App* client = manager->m_client;
    if (!client)
        return;
    Net::Conn* conn = client->FirstConnection(&search);
    if (!conn || !(manager->m_joinState == 8 || manager->m_joinState == 7 || manager->m_joinState == 6) ||
        conn->GetTimeElapsedSinceCommunication() <= kSilentTimeout)
        return;
    if (Field<uint32_t>(skate, 0x378) == kLoadMainMenu) {
        Run(kShowJoinTimeoutNotice);
        manager->m_joinStateTask->Remove();
        manager->m_joinState = 10;
    } else {
        Run(kCreateLostConnectionDialog);
        manager->DestroyAllPlayers();
        manager->RemoveAllNewPlayers(); // the joining players gone
        manager->DisconnectClients();   // the apps shut down
    }
}

} // namespace GameNet

REPLACE(0x0053EB70, GameNet::Manager::ConnectionTask);
REPLACE(0x0053AF90, &GameNet::Manager::ResetToLobby);
REPLACE(0x0053B0D0, &GameNet::Manager::StartNetworkGame);
REPLACE(0x0053C6E0, &GameNet::Manager::CancelJoin);
REPLACE(0x0053BED0, GameNet::Manager::ObserverTask);
REPLACE(0x00536890, GameNet::Manager::ObserverInput);
REPLACE(0x0053BF00, GameNet::Manager::AutoServeTask);
REPLACE(0x0053BF90, GameNet::Manager::JoinStateTask);
REPLACE(0x0053C1F0, GameNet::Manager::StartGameTask);
