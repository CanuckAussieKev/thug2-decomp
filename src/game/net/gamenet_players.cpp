// The network manager's player queries and the observer's camera (GameNet::Manager, 0x0053A910-0x0053AF90), and the
// host mode (0x0053C320). See gamenet_manager.h.

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/net/gamespy.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/net/stats_man.h"
#include "game/obj/game_objects.h"
#include "game/obj/trick_objects.h"
#include "game/sio/inpman.h"
#include "game/skate/profile_manager.h"
#include "game/skater/skater.h"
#include "game/skater/walk_camera.h"
#include "game/sys/timer.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/gamenet_observer.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/mlp.h"
#include "game/skater/skater_camera_component.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/ass/assman.h"
#include "hook/game_code.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"

#pragma warning(disable : 4996) // (strcpy: the original's copies of a player's name)

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint8_t kMsgObserveRequest = 0x4A; // (the server's HandleObserveRequest)
constexpr uint8_t kMsgFcfsNotice = 0x5F;     // (the client's HandleFcfsNotice)

constexpr uint32_t kSkaterCamObject = 0x967C138C;     // the camera object (by id)
constexpr uint32_t kSkaterCamera = 0x5E43A604;        // SkaterCamera
constexpr uint32_t kWalkCamera = 0xC7D37EC2;          // WalkCamera
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl
constexpr uint32_t kFcfsPreference = 0x043EE978;      // the network preference naming the first-come notice's text

constexpr int kMaxObjectIds = 16;
constexpr uint32_t kNewPlayerObserver = 4; // NewPlayerInfo flags
constexpr uint32_t kNewPlayerPending = 8;

// PlayerInfo flags (+0x1D0).
constexpr uint32_t kPlayerFcfsCandidate = 0x21; // (0x00543380: local or 0x20)
constexpr uint32_t kPlayerPending = 0x08;       // joined during a game: added for the next one
constexpr uint32_t kPlayerGameEnded = 0x100;    // its game has ended (message 0x59)
constexpr uint32_t kPlayerInfoSize = 0x1F0;

constexpr uint8_t kMsgPlayerQuit = 0x2D;  // a player left (its object id, the reason)
constexpr uint8_t kMsgRemovedYou = 0x51;  // 'Q': kicked or banned (to its address)
constexpr uint8_t kMsgMovedOn = 0x56;     // 'V': the game went on without it
constexpr uint8_t kMsgGameOver = 0x5A;

constexpr int kGameSpyGameId = 1003; // THUG2 PC's (the CD-key SDK)
constexpr int kPanelTime = 2000;

// Why a player is dropped (DropPlayer's reason) and the panel message for each.
constexpr int kDropQuit = 0;
constexpr int kDropTimedOut = 1;
constexpr int kDropObserving = 2;
constexpr int kDropBanned = 3;
constexpr int kDropKicked = 4;
constexpr int kDropDropped = 5;
constexpr int kDropLeftOut = 6;
constexpr uint32_t kNetMessagePlayerTimedOut = 0x2FD5CBE0; // net_message_player_timed_out
constexpr uint32_t kDropTexts[] = {
    0xA609ACD7,                // net_message_player_quit
    kNetMessagePlayerTimedOut, //
    0xDF79BBF5,                // net_message_player_now_observing
    0xDF087FD9,                // net_message_player_banned
    0x3CD67FE6,                // net_message_player_kicked
    0xDDCDDB87,                // net_message_player_dropped
    0x2165392E,                // net_message_player_left_out
};

// Names.
constexpr uint32_t kNetLobby = 0x1C471C60;              // netlobby
constexpr uint32_t kHidePanelStuff = 0x47180228;        // hide_panel_stuff
constexpr uint32_t kShouldModulateColor = 0x11941568;   // should_modulate_color
constexpr uint32_t kNetworkEndGameSelected = 0x70E20955; // network_end_game_selected
constexpr uint32_t kTeam = 0x3B1F59E0;                  // Team
constexpr uint32_t kString0 = 0xA4B08520;               // String0
constexpr uint32_t kFlagReturned = 0x3B1F2478;          // flag_returned
constexpr uint32_t kHideCtfArrow = 0x1C318F8C;          // hide_ctf_arrow

// (The joining player's record: GameNet::NewPlayerInfo, player_info.h.)
constexpr uint32_t kNewPlayerRejoining = 0x10; // (a pending player added back)

Tsk::Stack* LogicTasks()
{
    return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager) + 4);
}

// The skater camera's / walk camera's calls not rewritten as members here.
void SetCameraSkater(Obj::CBaseComponent* camera, void* skater)
{
    static_cast<Obj::CSkaterCameraComponent*>(camera)->SetSkater(static_cast<Obj::CCompositeObject*>(skater));
}
void SetWalkCameraSkater(Obj::CBaseComponent* camera, void* skater)
{
    static_cast<Obj::CWalkCameraComponent*>(camera)->SetSkater(static_cast<Obj::CCompositeObject*>(skater));
}
void ResetWalkCamera(Obj::CBaseComponent* camera) { static_cast<Obj::CWalkCameraComponent*>(camera)->ResetNow(); }

} // namespace

// 0x0053A910
void Manager::ObservePlayer(uint8_t* player)
{
    auto* camObject = static_cast<Obj::CCompositeObject*>(
        reinterpret_cast<Obj::CBaseManager*>(reinterpret_cast<void*>(Obj::g_compositeObjectManager))->GetObjectByID(kSkaterCamObject));
    Obj::CBaseComponent* skaterCamera = camObject->GetComponent(kSkaterCamera);
    Obj::CBaseComponent* walkCamera = camObject->GetComponent(kWalkCamera);
    if (!player || !PlayerSkater(player)) {
        SetCameraSkater(skaterCamera, nullptr);
        SetWalkCameraSkater(walkCamera, nullptr);
        Field<int>(walkCamera, 0x18) = 3; // (frames that jump straight to the target)
        m_camPlayer = nullptr;
        return;
    }
    auto* skater = reinterpret_cast<Obj::CCompositeObject*>(PlayerSkater(player));
    if (player != LocalPlayer(g_gameNetManager)) {
        // Someone else's skater: the skater camera onto it.
        SetCameraSkater(skaterCamera, skater);
        SetWalkCameraSkater(walkCamera, nullptr);
        Field<int>(walkCamera, 0x18) = 3;
        skaterCamera->VirtualSuspend(false);
        walkCamera->VirtualSuspend(true);
        m_camPlayer = player;
        return;
    }
    // The local skater: the walk camera while it walks (its physics control's state says), else the skater camera.
    Obj::CBaseComponent* control = skater->GetComponent(kSkaterPhysicsControl);
    SetCameraSkater(skaterCamera, skater);
    SetWalkCameraSkater(walkCamera, skater);
    Field<int>(walkCamera, 0x18) = 3;
    uint8_t* state = Field<uint8_t*>(control, 0x2C);
    if (!Field<bool>(state, 0x118) && Field<int>(state, 0x1C) == 0) {
        skaterCamera->VirtualSuspend(false);
        walkCamera->VirtualSuspend(true);
    } else {
        skaterCamera->VirtualSuspend(true);
        walkCamera->VirtualSuspend(false);
        ResetWalkCamera(walkCamera);
    }
    m_camPlayer = player;
}

// 0x0053AA70
void Manager::SendObserveRequest()
{
    Net::MsgDesc desc;
    desc.Construct();
    if (IsObserver(LocalPlayer(this))) // (no local player: the original reads through null too)
        return;
    desc.m_id = kMsgObserveRequest;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToServer(m_client, &desc);
}

// 0x0053AAC0
void Manager::AssignFirstComeFirstServed()
{
    PlayerSearch search;
    Net::MsgDesc desc;
    desc.Construct();
    for (uint8_t* player = FirstPlayer(this, &search, false); player; player = NextPlayer(this, &search, false)) {
        if (IsLocalPlayer(player) || !(Field<uint32_t>(player, 0x1D0) & kPlayerFcfsCandidate))
            continue;
        AsPlayer(player)->MarkAsServerPlayer(); // (its flag 0x200: first come, first served)
        uint32_t text;
        reinterpret_cast<Prefs::Preferences*>(m_networkPreferences)
            ->GetPreference(kFcfsPreference)
            ->GetChecksum("Checksum", &text, 1);
        desc.m_data = &text;
        desc.m_length = sizeof(text);
        desc.m_id = kMsgFcfsNotice;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        m_server->EnqueueMessage(PlayerHandle(player), &desc);
        return;
    }
}

// 0x0053AC40
int Manager::NextPlayerObjectId()
{
    for (int id = 0; id < kMaxObjectIds; ++id) {
        bool used = false;
        PlayerSearch search;
        for (uint8_t* player = FirstPlayer(this, &search, false); player && !used;
             player = NextPlayer(this, &search, false))
            used = static_cast<int>(SkaterId(PlayerSkater(player))) == id; // (a player without a skater: read at 0x14)
        NewPlayerSearch newSearch;
        for (uint8_t* info = FirstNewPlayer(this, &newSearch); info && !used; info = NextNewPlayer(this, &newSearch)) {
            uint32_t flags = Field<uint32_t>(info, 0x20);
            if ((flags & kNewPlayerObserver) && !(flags & kNewPlayerPending))
                continue; // (an observer joining takes no id)
            used = Field<int8_t>(info, 0x10) == id;
        }
        if (!used)
            return id;
    }
    return kMaxObjectIds;
}

// 0x0053AE10
uint8_t* Manager::PlayerByConnection(Net::Conn* conn)
{
    PlayerSearch search;
    for (uint8_t* player = FirstPlayer(this, &search, true); player; player = NextPlayer(this, &search, true))
        if (Field<Net::Conn*>(player, 0x18) == conn)
            return player;
    return nullptr;
}

// 0x0053AEA0
uint8_t* Manager::PlayerByObjectId(uint16_t id)
{
    PlayerSearch search;
    for (uint8_t* player = FirstPlayer(this, &search, false); player; player = NextPlayer(this, &search, false)) {
        Obj::CObject* skater = PlayerSkater(player);
        if (skater && SkaterId(skater) == id)
            return player;
    }
    return nullptr;
}

// 0x0053C770
uint8_t* Manager::NewPlayer(Obj::CObject* skater, Net::Conn* conn, uint32_t flags)
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_networkHeap);
    void* block = Mem::Calloc(kPlayerInfoSize);
    auto* player = reinterpret_cast<uint8_t*>(block ? static_cast<PlayerInfo*>(block)->Construct(flags) : nullptr);
    Field<Obj::CObject*>(player, 0x14) = skater; // (out of memory: written through null, as the original)
    Field<Net::Conn*>(player, 0x18) = conn;
    if (IsLocalPlayer(player)) {
        if (IsObserver(player)) {
            uint8_t* input = reinterpret_cast<uint8_t*>(Inp::g_inputManager);
            Tsk::Stack* tasks = LogicTasks();
            Run(kHidePanelStuff);
            ObservePlayer(static_cast<uint8_t*>(NextCamPlayer())); // the first player to watch
            AddObserverControls(this, player, tasks, input);
        } else {
            m_camPlayer = nullptr;
        }
    } else if (IsObserver(LocalPlayer(this)) && !m_camPlayer) { // (no local player: read through null)
        ObservePlayer(player);
    }
    Mem::Manager::Get()->PopContext();
    return player;
}

GAME_GLOBAL(bool, g_netEnabled, 0x007CCA82);
GAME_GLOBAL(bool, g_openWaiting, 0x007CCE1D);

// 0x0053C950
void Manager::DestroyPlayer(uint8_t* player)
{
    if (player == m_camPlayer) { // the camera onto the next player (the local skater's if there is one)
        m_camPlayer = NextCamPlayer();
        if (player == m_camPlayer)
            m_camPlayer = nullptr;
        uint8_t* local = LocalPlayer(this);
        ObservePlayer(local && PlayerSkater(local) ? local : static_cast<uint8_t*>(m_camPlayer));
    }
    if (AsPlayer(player)->IsKing() && Field<void*>(this, 0x6C)) // it had the crown: taken off
        Field<Obj::CCrownObject*>(this, 0x6C)->TakeOff();
    if (AsPlayer(player)->IsCarryingFlag()) // it carried a flag: returned
        FlagReturned(player);
    if (player)
        reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(player))[0])(player, 1);
    UpdateLobbyServer();
    if (g_netEnabled && g_gameNetManager->InNetGame() &&
        g_queryReporting) // (reported to GameSpy's qr2: the player count changed)
        GameSpy::Qr2SendStateChanged(nullptr); // qr2_send_statechanged
}

// 0x0053CB70
void Manager::DropPlayer(uint8_t* player, int reason)
{
    char name[kNameSize];
    std::strcpy(name, PlayerName(player));
    if (!IsObserver(player) && Call<bool>(GAME_CODE(0x00553220, &Game::CGameMode::IsTrue), GameMode(), kShouldModulateColor))
        Obj::g_trickObjectManager->ReleasePlayerClusters(static_cast<int>(SkaterId(PlayerSkater(player)))); // its colours off the trick objects
    if (!OnServer()) {
        DestroyPlayer(player);
        return;
    }
    Net::Conn* conn = Field<Net::Conn*>(player, 0x18);
    GameSpy::CdKeyDisconnectUser(kGameSpyGameId, static_cast<int>(conn->m_id)); // gcd_disconnect_user
    if (!IsObserver(player)) {
        PlayerSearch search;
        for (uint8_t* other = FirstPlayer(this, &search, true); other; other = NextPlayer(this, &search, true)) {
            if (IsLocalPlayer(other))
                continue;
            Net::MsgDesc desc;
            desc.Construct();
            uint8_t msg[2] = {static_cast<uint8_t>(SkaterId(PlayerSkater(player))), static_cast<uint8_t>(reason)};
            desc.m_data = msg;
            desc.m_length = sizeof(msg);
            desc.m_id = kMsgPlayerQuit;
            desc.m_queue = Net::MsgDesc::kQueueSequenced;
            desc.m_groupId = kGroupGame;
            m_server->EnqueueMessage(PlayerHandle(other), &desc);
        }
        if ((reason == kDropQuit || reason == kDropTimedOut || reason == kDropDropped) &&
            static_cast<StatsMan*>(m_statsMan)->UpdateLoggedIn()) // (a rated game left early)
            static_cast<StatsMan*>(m_statsMan)->RemovePlayer(static_cast<int>(SkaterId(PlayerSkater(player))));
    }
    bool close = true;
    if (reason == kDropBanned || reason == kDropKicked) {
        conn = Field<Net::Conn*>(player, 0x18);
        m_server->SendMessageTo(kMsgRemovedYou, 0, nullptr, conn->m_ip, conn->m_port, 0);
        if (reason == kDropBanned)
            m_server->BanConnection(Field<Net::Conn*>(player, 0x18));
    } else if (reason == kDropLeftOut) {
        conn = Field<Net::Conn*>(player, 0x18);
        m_server->SendMessageTo(kMsgMovedOn, 0, nullptr, conn->m_ip, conn->m_port, 0);
    } else if (reason == kDropObserving) {
        close = false;
    }
    if (close && Field<Net::Conn*>(player, 0x18)) {
        Field<Net::Conn*>(player, 0x18)->Invalidate();
        Field<Net::Conn*>(player, 0x18)->DestroyMessageQueues();
    }
    uint32_t text = reason >= 0 && reason < static_cast<int>(sizeof(kDropTexts) / sizeof(kDropTexts[0]))
                        ? kDropTexts[reason]
                        : kNetMessagePlayerTimedOut;
    if (!IsLocalPlayer(player))
        CreateNetPanelMessage(true, text, name, nullptr, nullptr, nullptr, false, kPanelTime);
    if (reason == kDropObserving) {
        if (AsPlayer(player)->IsKing()) { // the crown taken off
            if (Field<void*>(this, 0x6C))
                Field<Obj::CCrownObject*>(this, 0x6C)->TakeOff();
            AsPlayer(player)->MarkAsKing(false);
        }
        if (AsPlayer(player)->IsCarryingFlag())
            FlagReturned(player);
    } else {
        bool fcfs = AsPlayer(player)->IsServerPlayer() != 0;
        DestroyPlayer(player);
        if (fcfs) { // it was to serve next: someone else
            AssignFirstComeFirstServed();
            if (!GetServerPlayer() && GameType() != kNetLobby)
                Run(kNetworkEndGameSelected);
        }
    }
    // Every player still playing has finished: game over (message 0x5A to every one).
    PlayerSearch search;
    for (uint8_t* p = FirstPlayer(this, &search, false); p; p = NextPlayer(this, &search, false))
        if (!(Field<uint32_t>(p, 0x1D0) & kPlayerGameEnded))
            return;
    Net::MsgDesc desc;
    desc.Construct();
    desc.m_id = kMsgGameOver;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    for (uint8_t* p = FirstPlayer(this, &search, true); p; p = NextPlayer(this, &search, true))
        m_server->EnqueueMessage(PlayerHandle(p), &desc);
}

// 0x0053D120
void Manager::ReAddPendingPlayers()
{
    UpdateLobbyServer();
    g_openWaiting = true;
    if (g_netEnabled && g_gameNetManager->InNetGame() &&
        g_queryReporting && g_openWaiting != 1) // (just set: never)
        GameSpy::Qr2SendStateChanged(nullptr);
    int frames = 10;
    PlayerSearch search;
    uint8_t* player = FirstPlayer(this, &search, true);
    while (player) {
        uint8_t* next = NextPlayer(this, &search, true); // (before the player is destroyed)
        if (Field<uint32_t>(player, 0x1D0) & kPlayerPending) {
            NewPlayerInfo info;
            info.Construct(); // (a skater profile)
            frames += 10;
            std::strcpy(info.m_name, PlayerName(player));
            info.m_objectId = static_cast<uint8_t>(NextPlayerObjectId());
            info.m_conn = Field<Net::Conn*>(player, 0x18);
            info.m_flags = kNewPlayerRejoining;
            info.m_profileId = Field<int32_t>(player, 0x28);
            info.m_rating = Field<int32_t>(player, 0x2C);
            info.m_unknown38 = 0;
            info.m_unknown3C = 0;
            info.m_unknown34 = 0;
            info.m_vehicleControlType = Field<int32_t>(player, 0x48);
            Mem::Manager* memory = Mem::Manager::Get();
            memory->PushContext(memory->m_skaterInfoHeap);
            info.m_profile->CopyFrom(Field<Obj::CSkaterProfile*>(player, 0x4C)); // its appearance
            Mem::Manager::Get()->PopContext();
            DestroyPlayer(player);
            AddJoiningPlayer(&info, frames); // (added back in `frames` frames, 10 apart)
            info.DestroyBody();
        }
        player = next;
    }
    for (uint8_t* p = FirstPlayer(this, &search, false); p; p = NextPlayer(this, &search, false)) {
        if (!IsLocalPlayer(p)) {
            static_cast<Obj::CSkater*>(PlayerSkater(p))->HideSkater(); // hidden
            static_cast<Obj::CSkater*>(PlayerSkater(p))->Reset27C();   // reset
        }
    }
}

// 0x0053D900
void Manager::DestroyAllPlayers()
{
    uint8_t* skate = Skate();
    PlayerSearch search;
    uint8_t* player = FirstPlayer(this, &search, true);
    while (player) {
        uint8_t* next = NextPlayer(this, &search, true);
        Obj::CObject* skater = PlayerSkater(player);
        if (skater && (static_cast<Obj::CSkater*>(skater)->GetSkaterNumber() != 0 || !IsLocalPlayer(player))) // (not the first local skater)
            Call(GAME_CODE(0x00565790, &Mdl::Skate::RemoveSkater), skate, skater);
        DestroyPlayer(player);
        player = next;
    }
}

// 0x0053E910
void Manager::RemovePlayers()
{
    DestroyAllPlayers();
    RemoveAllNewPlayers(); // the joining players too
}

// 0x0053E920
void Manager::DropPlayersNotReady()
{
    uint8_t* skate = Skate();
    PlayerSearch search;
    uint8_t* player = FirstPlayer(this, &search, false);
    while (player) {
        uint8_t* next = NextPlayer(this, &search, false);
        if (!(Field<uint32_t>(player, 0x1D0) & kPlayerFcfsCandidate)) {
            Obj::CObject* skater = PlayerSkater(player);
            DropPlayer(player, kDropLeftOut);
            Call(GAME_CODE(0x00565790, &Mdl::Skate::RemoveSkater), skate, skater);
        }
        player = next;
    }
}

// 0x0053EE70
void Manager::RemovePlayersAndApps()
{
    DestroyAllPlayers();
    RemoveAllNewPlayers();
    DisconnectClients(); // the apps shut down
}

// (inline in DestroyPlayer / ReAddPendingPlayers)
void Manager::UpdateLobbyServer()
{
    void* peer = Field<void*>(m_lobbyMan, 0x140);
    if (peer && GameSpy::PeerIsConnected(peer) && Field<bool>(m_lobbyMan, 0x139))
        GameSpy::PeerStateChanged(peer); // (the hosted game's details to GameSpy's peer SDK)
}

// (inline in DestroyPlayer / DropPlayer)
void Manager::FlagReturned(uint8_t* player)
{
    int team = AsPlayer(player)->GetFlagTeam(); // the flag's team
    char key[64];
    TeamNameKey(key, team);
    Script::CStruct* params = NewParams();
    params->AddInteger(kTeam, team);
    params->AddString(kString0, Script::ESYMBOLTYPE_STRING, Script::GetString(key));
    Run(kFlagReturned, params);
    DeleteParams(params);
    uint8_t* local = LocalPlayer(this);
    if (local && !IsObserver(local) && PlayerTeam(local) == team)
        Run(kHideCtfArrow);
    AsPlayer(player)->ClearFlags(); // carries no flag
}

// 0x0053E650
void Manager::EnterObserveMode()
{
    uint8_t* skate = Skate();
    Tsk::Stack* tasks = LogicTasks();
    uint8_t* input = reinterpret_cast<uint8_t*>(Inp::g_inputManager);
    uint8_t* local = LocalPlayer(this);
    if (IsObserver(local))
        return;
    if (OnServer())
        DropPlayer(local, kDropObserving);
    if (AsPlayer(local)->IsKing()) { // its crown given up
        AsPlayer(local)->MarkAsKing(false);
        if (Field<void*>(this, 0x6C))
            Field<Obj::CCrownObject*>(this, 0x6C)->TakeOff();
    }
    if (AsPlayer(local)->IsCarryingFlag()) { // its flag returned (hide_ctf_arrow whatever the team, unlike FlagReturned)
        int team = AsPlayer(local)->GetFlagTeam();
        char key[64];
        TeamNameKey(key, team);
        Script::CStruct* params = NewParams();
        params->AddInteger(kTeam, team);
        params->AddString(kString0, Script::ESYMBOLTYPE_STRING, Script::GetString(key));
        Run(kFlagReturned, params);
        DeleteParams(params);
        Run(kHideCtfArrow);
        AsPlayer(local)->ClearFlags();
    }
    Field<uint32_t>(local, 0x1D0) |= 4; // an observer
    Call(GAME_CODE(0x00565790, &Mdl::Skate::RemoveSkater), skate, PlayerSkater(local));
    Field<Obj::CObject*>(local, 0x14) = nullptr;
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_skaterHeaps[0]);
    ObservePlayer(static_cast<uint8_t*>(NextCamPlayer())); // the first player to watch
    AddObserverControls(this, local, tasks, input, -1);
    m_observerCommand = 0;
    Run(kHidePanelStuff);
    Mem::Manager::Get()->PopContext();
}

// 0x0053C320
void Manager::SetHostMode(int mode)
{
    if (mode == m_hostMode)
        return;
    m_hostMode = mode;
    if (mode == kHostModeAutoServe) {
        SendObserveRequest();
        g_autoServeStart = Tmr::GetTime(); // (the auto-serve task waits 2 s from here)
        LogicTasks()->AddTask(Field<Tsk::BaseTask*>(this, 0xF4));
    } else if (mode == kHostModeFcfs) {
        SendObserveRequest();
        AssignFirstComeFirstServed();
    }
}

} // namespace GameNet

REPLACE(0x0053A910, &GameNet::Manager::ObservePlayer);
REPLACE(0x0053AA70, &GameNet::Manager::SendObserveRequest);
REPLACE(0x0053AAC0, &GameNet::Manager::AssignFirstComeFirstServed);
REPLACE(0x0053AC40, &GameNet::Manager::NextPlayerObjectId);
REPLACE(0x0053AE10, &GameNet::Manager::PlayerByConnection);
REPLACE(0x0053AEA0, &GameNet::Manager::PlayerByObjectId);
REPLACE(0x0053C320, &GameNet::Manager::SetHostMode);
REPLACE(0x0053C770, &GameNet::Manager::NewPlayer);
REPLACE(0x0053C950, &GameNet::Manager::DestroyPlayer);
REPLACE(0x0053CB70, &GameNet::Manager::DropPlayer);
REPLACE(0x0053D120, &GameNet::Manager::ReAddPendingPlayers);
REPLACE(0x0053D900, &GameNet::Manager::DestroyAllPlayers);
REPLACE(0x0053E910, &GameNet::Manager::RemovePlayers);
REPLACE(0x0053E920, &GameNet::Manager::DropPlayersNotReady);
REPLACE(0x0053EE70, &GameNet::Manager::RemovePlayersAndApps);
REPLACE(0x0053E650, &GameNet::Manager::EnterObserveMode);
