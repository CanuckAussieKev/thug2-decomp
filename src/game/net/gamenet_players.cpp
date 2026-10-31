// The network manager's player queries and the observer's camera (OnlinePlay::Hub, 0x0053A910-0x0053AF90), and the
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
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"

#pragma warning(disable : 4996) // (strcpy: the original's copies of a player's name)

namespace OnlinePlay {

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
constexpr uint32_t kNewPlayerObserver = 4; // JoinerRecord flags
constexpr uint32_t kNewPlayerPending = 8;

// PlayerRecord flags (+0x1D0).
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

// Why a player is dropped (EvictPlayer's reason) and the panel message for each.
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

// (The joining player's record: OnlinePlay::JoinerRecord, player_info.h.)
constexpr uint32_t kNewPlayerRejoining = 0x10; // (a pending player added back)

TaskList::PileUp* LogicTasks()
{
    return reinterpret_cast<TaskList::PileUp*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 4);
}

// The skater camera's / walk camera's calls not rewritten as members here.
void SetCameraSkater(GameObjects::CObjectModule* camera, void* skater)
{
    static_cast<GameObjects::CRiderChaseView*>(camera)->BindRider(static_cast<GameObjects::CGameEntity*>(skater));
}
void SetWalkCameraSkater(GameObjects::CObjectModule* camera, void* skater)
{
    static_cast<GameObjects::CFootView*>(camera)->BindRider(static_cast<GameObjects::CGameEntity*>(skater));
}
void ResetWalkCamera(GameObjects::CObjectModule* camera) { static_cast<GameObjects::CFootView*>(camera)->ResetNow(); }

} // namespace

// 0x0053A910
void Hub::SpectatePlayer(uint8_t* player)
{
    auto* camObject = static_cast<GameObjects::CGameEntity*>(
        reinterpret_cast<GameObjects::CObjectHubBase*>(reinterpret_cast<void*>(GameObjects::g_compositeObjectManager))->LookUpEntity(kSkaterCamObject));
    GameObjects::CObjectModule* skaterCamera = camObject->FetchModule(kSkaterCamera);
    GameObjects::CObjectModule* walkCamera = camObject->FetchModule(kWalkCamera);
    if (!player || !PlayerSkater(player)) {
        SetCameraSkater(skaterCamera, nullptr);
        SetWalkCameraSkater(walkCamera, nullptr);
        Prop<int>(walkCamera, 0x18) = 3; // (frames that jump straight to the target)
        m_camPlayer = nullptr;
        return;
    }
    auto* skater = reinterpret_cast<GameObjects::CGameEntity*>(PlayerSkater(player));
    if (player != LocalPlayer(g_gameNetManager)) {
        // Someone else's skater: the skater camera onto it.
        SetCameraSkater(skaterCamera, skater);
        SetWalkCameraSkater(walkCamera, nullptr);
        Prop<int>(walkCamera, 0x18) = 3;
        skaterCamera->VirtualSuspend(false);
        walkCamera->VirtualSuspend(true);
        m_camPlayer = player;
        return;
    }
    // The local skater: the walk camera while it walks (its physics control's state says), else the skater camera.
    GameObjects::CObjectModule* control = skater->FetchModule(kSkaterPhysicsControl);
    SetCameraSkater(skaterCamera, skater);
    SetWalkCameraSkater(walkCamera, skater);
    Prop<int>(walkCamera, 0x18) = 3;
    uint8_t* state = Prop<uint8_t*>(control, 0x2C);
    if (!Prop<bool>(state, 0x118) && Prop<int>(state, 0x1C) == 0) {
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
void Hub::SendObserveRequest()
{
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    if (IsObserver(LocalPlayer(this))) // (no local player: the original reads through null too)
        return;
    desc.m_ident = kMsgObserveRequest;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToServer(m_clientApp, &desc);
}

// 0x0053AAC0
void Hub::AssignFirstComeFirstServed()
{
    PlayerSearch search;
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    for (uint8_t* player = FirstPlayer(this, &search, false); player; player = NextPlayer(this, &search, false)) {
        if (IsOwnPlayer(player) || !(Prop<uint32_t>(player, 0x1D0) & kPlayerFcfsCandidate))
            continue;
        AsPlayer(player)->SetHostPlayer(); // (its flag 0x200: first come, first served)
        uint32_t text;
        reinterpret_cast<OptionStore::Preferences*>(m_networkPreferences)
            ->FetchSetting(kFcfsPreference)
            ->ReadHashValue("Checksum", &text, 1);
        desc.m_content = &text;
        desc.m_byteCount = sizeof(text);
        desc.m_ident = kMsgFcfsNotice;
        desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        m_host->PutOnOutbox(PlayerHandle(player), &desc);
        return;
    }
}

// 0x0053AC40
int Hub::NextPlayerObjectId()
{
    for (int id = 0; id < kMaxObjectIds; ++id) {
        bool used = false;
        PlayerSearch search;
        for (uint8_t* player = FirstPlayer(this, &search, false); player && !used;
             player = NextPlayer(this, &search, false))
            used = static_cast<int>(SkaterId(PlayerSkater(player))) == id; // (a player without a skater: read at 0x14)
        NewPlayerSearch newSearch;
        for (uint8_t* info = FirstNewPlayer(this, &newSearch); info && !used; info = NextNewPlayer(this, &newSearch)) {
            uint32_t flags = Prop<uint32_t>(info, 0x20);
            if ((flags & kNewPlayerObserver) && !(flags & kNewPlayerPending))
                continue; // (an observer joining takes no id)
            used = Prop<int8_t>(info, 0x10) == id;
        }
        if (!used)
            return id;
    }
    return kMaxObjectIds;
}

// 0x0053AE10
uint8_t* Hub::PlayerByConnection(NetLink::Conn* conn)
{
    PlayerSearch search;
    for (uint8_t* player = FirstPlayer(this, &search, true); player; player = NextPlayer(this, &search, true))
        if (Prop<NetLink::Conn*>(player, 0x18) == conn)
            return player;
    return nullptr;
}

// 0x0053AEA0
uint8_t* Hub::PlayerByObjectId(uint16_t id)
{
    PlayerSearch search;
    for (uint8_t* player = FirstPlayer(this, &search, false); player; player = NextPlayer(this, &search, false)) {
        GameObjects::CEntity* skater = PlayerSkater(player);
        if (skater && SkaterId(skater) == id)
            return player;
    }
    return nullptr;
}

// 0x0053C770
uint8_t* Hub::NewPlayer(GameObjects::CEntity* skater, NetLink::Conn* conn, uint32_t flags)
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_networkHeap);
    void* block = MemSys::AllocZeroed(kPlayerInfoSize);
    auto* player = reinterpret_cast<uint8_t*>(block ? static_cast<PlayerRecord*>(block)->MakeNew(flags) : nullptr);
    Prop<GameObjects::CEntity*>(player, 0x14) = skater; // (out of memory: written through null, as the original)
    Prop<NetLink::Conn*>(player, 0x18) = conn;
    if (IsOwnPlayer(player)) {
        if (IsObserver(player)) {
            uint8_t* input = reinterpret_cast<uint8_t*>(PadInput::g_inputManager);
            TaskList::PileUp* tasks = LogicTasks();
            Run(kHidePanelStuff);
            SpectatePlayer(static_cast<uint8_t*>(NextCamPlayer())); // the first player to watch
            AddObserverControls(this, player, tasks, input);
        } else {
            m_camPlayer = nullptr;
        }
    } else if (IsObserver(LocalPlayer(this)) && !m_camPlayer) { // (no local player: read through null)
        SpectatePlayer(player);
    }
    MemSys::Hub::Pull()->LeaveHeapScope();
    return player;
}

bool g_netEnabled;
bool g_openWaiting;

// 0x0053C950
void Hub::RemovePlayerRecord(uint8_t* player)
{
    if (player == m_camPlayer) { // the camera onto the next player (the local skater's if there is one)
        m_camPlayer = NextCamPlayer();
        if (player == m_camPlayer)
            m_camPlayer = nullptr;
        uint8_t* local = LocalPlayer(this);
        SpectatePlayer(local && PlayerSkater(local) ? local : static_cast<uint8_t*>(m_camPlayer));
    }
    if (AsPlayer(player)->HoldsCrown() && Prop<void*>(this, 0x6C)) // it had the crown: taken off
        Prop<GameObjects::CCrownObject*>(this, 0x6C)->TakeOff();
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
void Hub::EvictPlayer(uint8_t* player, int reason)
{
    char name[kNameSize];
    std::strcpy(name, PlayerName(player));
    if (!IsObserver(player) && Call<bool>(CodeAddress(&GoalSystem::CMatchRules::IsTrue), GameMode(), kShouldModulateColor))
        GameObjects::g_trickObjectManager->ReleasePlayerClusters(static_cast<int>(SkaterId(PlayerSkater(player)))); // its colours off the trick objects
    if (!OnServer()) {
        RemovePlayerRecord(player);
        return;
    }
    NetLink::Conn* conn = Prop<NetLink::Conn*>(player, 0x18);
    GameSpy::CdKeyDisconnectUser(kGameSpyGameId, static_cast<int>(conn->m_ident)); // gcd_disconnect_user
    if (!IsObserver(player)) {
        PlayerSearch search;
        for (uint8_t* other = FirstPlayer(this, &search, true); other; other = NextPlayer(this, &search, true)) {
            if (IsOwnPlayer(other))
                continue;
            NetLink::NetMessageInfo desc;
            desc.MakeNew();
            uint8_t msg[2] = {static_cast<uint8_t>(SkaterId(PlayerSkater(player))), static_cast<uint8_t>(reason)};
            desc.m_content = msg;
            desc.m_byteCount = sizeof(msg);
            desc.m_ident = kMsgPlayerQuit;
            desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
            desc.m_groupId = kGroupGame;
            m_host->PutOnOutbox(PlayerHandle(other), &desc);
        }
        if ((reason == kDropQuit || reason == kDropTimedOut || reason == kDropDropped) &&
            static_cast<RankingsManager*>(m_statsMan)->UpdateLoggedIn()) // (a rated game left early)
            static_cast<RankingsManager*>(m_statsMan)->RemovePlayer(static_cast<int>(SkaterId(PlayerSkater(player))));
    }
    bool close = true;
    if (reason == kDropBanned || reason == kDropKicked) {
        conn = Prop<NetLink::Conn*>(player, 0x18);
        m_host->DeliverTo(kMsgRemovedYou, 0, nullptr, conn->m_hostAddr, conn->m_portNo, 0);
        if (reason == kDropBanned)
            m_host->BlockConnection(Prop<NetLink::Conn*>(player, 0x18));
    } else if (reason == kDropLeftOut) {
        conn = Prop<NetLink::Conn*>(player, 0x18);
        m_host->DeliverTo(kMsgMovedOn, 0, nullptr, conn->m_hostAddr, conn->m_portNo, 0);
    } else if (reason == kDropObserving) {
        close = false;
    }
    if (close && Prop<NetLink::Conn*>(player, 0x18)) {
        Prop<NetLink::Conn*>(player, 0x18)->MarkStale();
        Prop<NetLink::Conn*>(player, 0x18)->DropQueuedTraffic();
    }
    uint32_t text = reason >= 0 && reason < static_cast<int>(sizeof(kDropTexts) / sizeof(kDropTexts[0]))
                        ? kDropTexts[reason]
                        : kNetMessagePlayerTimedOut;
    if (!IsOwnPlayer(player))
        BuildNetNotice(true, text, name, nullptr, nullptr, nullptr, false, kPanelTime);
    if (reason == kDropObserving) {
        if (AsPlayer(player)->HoldsCrown()) { // the crown taken off
            if (Prop<void*>(this, 0x6C))
                Prop<GameObjects::CCrownObject*>(this, 0x6C)->TakeOff();
            AsPlayer(player)->GiveCrown(false);
        }
        if (AsPlayer(player)->IsCarryingFlag())
            FlagReturned(player);
    } else {
        bool fcfs = AsPlayer(player)->IsHostPlayer() != 0;
        RemovePlayerRecord(player);
        if (fcfs) { // it was to serve next: someone else
            AssignFirstComeFirstServed();
            if (!GetHostPlayer() && GameType() != kNetLobby)
                Run(kNetworkEndGameSelected);
        }
    }
    // Every player still playing has finished: game over (message 0x5A to every one).
    PlayerSearch search;
    for (uint8_t* p = FirstPlayer(this, &search, false); p; p = NextPlayer(this, &search, false))
        if (!(Prop<uint32_t>(p, 0x1D0) & kPlayerGameEnded))
            return;
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    desc.m_ident = kMsgGameOver;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    for (uint8_t* p = FirstPlayer(this, &search, true); p; p = NextPlayer(this, &search, true))
        m_host->PutOnOutbox(PlayerHandle(p), &desc);
}

// 0x0053D120
void Hub::ReAddPendingPlayers()
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
        if (Prop<uint32_t>(player, 0x1D0) & kPlayerPending) {
            JoinerRecord info;
            info.MakeNew(); // (a skater profile)
            frames += 10;
            std::strcpy(info.m_label, PlayerName(player));
            info.m_entityKey = static_cast<uint8_t>(NextPlayerObjectId());
            info.m_conn = Prop<NetLink::Conn*>(player, 0x18);
            info.m_flagBits = kNewPlayerRejoining;
            info.m_profileId = Prop<int32_t>(player, 0x28);
            info.m_skillPoints = Prop<int32_t>(player, 0x2C);
            info.m_unknown38 = 0;
            info.m_unknown3C = 0;
            info.m_unknown34 = 0;
            info.m_vehicleControlType = Prop<int32_t>(player, 0x48);
            MemSys::Hub* memory = MemSys::Hub::Pull();
            memory->EnterHeapScope(memory->m_skaterInfoHeap);
            info.m_account->CopyFrom(Prop<GameObjects::CRiderIdentity*>(player, 0x4C)); // its appearance
            MemSys::Hub::Pull()->LeaveHeapScope();
            RemovePlayerRecord(player);
            AddJoiningPlayer(&info, frames); // (added back in `frames` frames, 10 apart)
            info.DestroyBody();
        }
        player = next;
    }
    for (uint8_t* p = FirstPlayer(this, &search, false); p; p = NextPlayer(this, &search, false)) {
        if (!IsOwnPlayer(p)) {
            static_cast<GameObjects::CRider*>(PlayerSkater(p))->ConcealSkater(); // hidden
            static_cast<GameObjects::CRider*>(PlayerSkater(p))->Reset27C();   // reset
        }
    }
}

// 0x0053D900
void Hub::DestroyAllPlayers()
{
    uint8_t* skate = Skate();
    PlayerSearch search;
    uint8_t* player = FirstPlayer(this, &search, true);
    while (player) {
        uint8_t* next = NextPlayer(this, &search, true);
        GameObjects::CEntity* skater = PlayerSkater(player);
        if (skater && (static_cast<GameObjects::CRider*>(skater)->GetSkaterNumber() != 0 || !IsOwnPlayer(player))) // (not the first local skater)
            Call(CodeAddress(&GameModules::Skate::RemoveSkater), skate, skater);
        RemovePlayerRecord(player);
        player = next;
    }
}

// 0x0053E910
void Hub::RemovePlayers()
{
    DestroyAllPlayers();
    RemoveAllNewPlayers(); // the joining players too
}

// 0x0053E920
void Hub::DropPlayersNotReady()
{
    uint8_t* skate = Skate();
    PlayerSearch search;
    uint8_t* player = FirstPlayer(this, &search, false);
    while (player) {
        uint8_t* next = NextPlayer(this, &search, false);
        if (!(Prop<uint32_t>(player, 0x1D0) & kPlayerFcfsCandidate)) {
            GameObjects::CEntity* skater = PlayerSkater(player);
            EvictPlayer(player, kDropLeftOut);
            Call(CodeAddress(&GameModules::Skate::RemoveSkater), skate, skater);
        }
        player = next;
    }
}

// 0x0053EE70
void Hub::RemovePlayersAndApps()
{
    DestroyAllPlayers();
    RemoveAllNewPlayers();
    DisconnectClients(); // the apps shut down
}

// (inline in RemovePlayerRecord / ReAddPendingPlayers)
void Hub::UpdateLobbyServer()
{
    void* peer = Prop<void*>(m_lobbyMan, 0x140);
    if (peer && GameSpy::PeerIsConnected(peer) && Prop<bool>(m_lobbyMan, 0x139))
        GameSpy::PeerStateChanged(peer); // (the hosted game's details to GameSpy's peer SDK)
}

// (inline in RemovePlayerRecord / EvictPlayer)
void Hub::FlagReturned(uint8_t* player)
{
    int team = AsPlayer(player)->GetFlagTeam(); // the flag's team
    char key[64];
    TeamNameKey(key, team);
    Scripts::CStruct* params = NewParams();
    params->PutInt(kTeam, team);
    params->AppendWord(kString0, Scripts::QBVAL_STRING, Scripts::FetchText(key));
    Run(kFlagReturned, params);
    DeleteParams(params);
    uint8_t* local = LocalPlayer(this);
    if (local && !IsObserver(local) && PlayerTeam(local) == team)
        Run(kHideCtfArrow);
    AsPlayer(player)->ClearFlags(); // carries no flag
}

// 0x0053E650
void Hub::EnterObserveMode()
{
    uint8_t* skate = Skate();
    TaskList::PileUp* tasks = LogicTasks();
    uint8_t* input = reinterpret_cast<uint8_t*>(PadInput::g_inputManager);
    uint8_t* local = LocalPlayer(this);
    if (IsObserver(local))
        return;
    if (OnServer())
        EvictPlayer(local, kDropObserving);
    if (AsPlayer(local)->HoldsCrown()) { // its crown given up
        AsPlayer(local)->GiveCrown(false);
        if (Prop<void*>(this, 0x6C))
            Prop<GameObjects::CCrownObject*>(this, 0x6C)->TakeOff();
    }
    if (AsPlayer(local)->IsCarryingFlag()) { // its flag returned (hide_ctf_arrow whatever the team, unlike FlagReturned)
        int team = AsPlayer(local)->GetFlagTeam();
        char key[64];
        TeamNameKey(key, team);
        Scripts::CStruct* params = NewParams();
        params->PutInt(kTeam, team);
        params->AppendWord(kString0, Scripts::QBVAL_STRING, Scripts::FetchText(key));
        Run(kFlagReturned, params);
        DeleteParams(params);
        Run(kHideCtfArrow);
        AsPlayer(local)->ClearFlags();
    }
    Prop<uint32_t>(local, 0x1D0) |= 4; // an observer
    Call(CodeAddress(&GameModules::Skate::RemoveSkater), skate, PlayerSkater(local));
    Prop<GameObjects::CEntity*>(local, 0x14) = nullptr;
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_skaterHeaps[0]);
    SpectatePlayer(static_cast<uint8_t*>(NextCamPlayer())); // the first player to watch
    AddObserverControls(this, local, tasks, input, -1);
    m_observerCommand = 0;
    Run(kHidePanelStuff);
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x0053C320
void Hub::SetHostMode(int mode)
{
    if (mode == m_hostMode)
        return;
    m_hostMode = mode;
    if (mode == kHostModeAutoServe) {
        SendObserveRequest();
        g_autoServeStart = FrameClock::GetTime(); // (the auto-serve task waits 2 s from here)
        LogicTasks()->QueueJob(Prop<TaskList::SchedulerTask*>(this, 0xF4));
    } else if (mode == kHostModeFcfs) {
        SendObserveRequest();
        AssignFirstComeFirstServed();
    }
}

} // namespace OnlinePlay
