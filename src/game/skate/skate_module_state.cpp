// The skate module's state and its small commands (see skate_module.h): the singleton, the module callbacks, the
// game type, the level asked for / opened / changed, the preferences, the script commands that drive them.

#include "game/skate/skate_module.h"
#include "game/net/net_lists.h"
#include "game/skater/state_history.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task_list.h"
#include "game/park_editing/park_editor.h"
#include "game/front/screen_element_manager.h"
#include "game/goals/goal_manager.h"
#include "game/gfx/view_cam.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/player_info.h"
#include "game/obj/emitter_object.h"
#include "game/obj/entity.h"
#include "game/obj/proximity.h"
#include "game/obj/trick_objects.h"
#include "game/audio/music_api.h"
#include "game/script/preferences.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/param_struct.h"
#include "game/sound/sfx.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "game/obj/node_components.h"
#include "game/obj/rail_manager.h"
#include "game/skater/rider.h"
#include "game/sound/surface_kind.h"
#include "game/sys/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/viewport.h"
#include "game/assets/resource_registry.h"
#include "game/core/code_address.h"
#include "game/gfx/engine.h"

namespace GameModules {

int g_skateModuleReferences;
float g_nextBeep;

namespace {

constexpr uint32_t kInitLightManager = 0x7004771D;       // init_light_manager
constexpr uint32_t kLoadPermanentPrefiles = 0x7690764F;  // load_permanent_prefiles
constexpr uint32_t kNetworkDriverStartup = 0x6B1D08C3;   // network_driver_startup
constexpr uint32_t kInitLoadingBar = 0x36932FCA;         // init_loading_bar
constexpr uint32_t kStartupLoadingScreen = 0xAA37AEAE;   // startup_loading_screen
constexpr uint32_t kLoadPermSounds = 0x565D0F63;         // LoadPermSounds
constexpr uint32_t kLoadPermanentAssets = 0xAE754239;    // load_permanent_assets
constexpr uint32_t kInitEnvironment = 0x63BBA6AD;        // init_environment
constexpr uint32_t kDefaultSystemStartup = 0x738AB3D8;   // default_system_startup
constexpr uint32_t kSkater = 0x5B8AB877;                 // skater
constexpr uint32_t kLoadNothing = 0x3A8E4A31;            // (name unknown: the level that loads nothing)
constexpr uint32_t kLevel = 0x651533EC;                  // level
constexpr uint32_t kFromSettingsMarker = 0x547CB588;         // use_preferences
constexpr uint32_t kShowWarning = 0xD8E34EDA;            // show_warning
constexpr uint32_t kLoadSk5EdGameplay = 0xB664035D;      // load_sk5ed_gameplay
constexpr uint32_t kFreeskate2p = 0x5C8F5D66;            // freeskate2p
constexpr uint32_t kNetwork = 0x9F7B7843;                // network
constexpr uint32_t kTaunt = 0x4839873C;                  // taunt
constexpr uint32_t kSplitscreen = 0x06AB02F2;            // splitscreen
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;           // Load_MainMenu
constexpr uint32_t kLoadCas = 0xA368B4F1;                // load_cas
constexpr uint32_t kPrefs = 0xB58E6556;                  // prefs
constexpr uint32_t kAbsentInNetGames = 0x68910AC6;       // AbsentInNetGames

constexpr uint8_t kMsgParkMap = 0x6C;      // the park editor's map ("level data")
constexpr uint8_t kMsgParkRails = 0x88;    // its rails ("rail data")
constexpr uint8_t kMsgLevelSwitch = 0x46;  // the level changes (to the level)
constexpr uint8_t kMsgCreatedGoals = 0x87; // the created goals ("goals data")
constexpr uint8_t kMsgReadyQuery = 0x25;   // are you ready (a time stamp)
constexpr uint8_t kGroupGame = 8;

GoalSystem::CMatchRules* GameMode() { return GoalSystem::g_gameMode; }
OnlinePlay::Hub* NetSessionSlot() { return OnlinePlay::g_onlineHub; }
GameObjects::CProfileRoster* RosterHandle() { return GameObjects::g_profileRoster; }
int& Refs() { return g_skateModuleReferences; }
float& NextBeep() { return g_nextBeep; }

// A walk over the players (OnlinePlay::Hub::BeginPlayerWalk / StepPlayerWalk).
struct PlayerSearch {
    void** m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};
// A walk over an app's connections (NetLink::AppHost::BeginConnWalk / StepConnWalk).
struct ConnSearch {
    void** m_vtable = NetLink::kConnSearchVtable;
    void* m_listLink = nullptr;
};
OnlinePlay::PlayerRecord* AsPlayer(void* player) { return static_cast<OnlinePlay::PlayerRecord*>(player); }

} // namespace

// 0x00564C10
void __cdecl Skate::DiscardOccurrence()
{
    if (static_cast<uint32_t>(Refs()) > 1) {
        --Refs();
        return;
    }
    if (Skate* skate = Sole())
        reinterpret_cast<Skate*(__thiscall*)(Skate*, int)>(skate->m_vtable[0])(skate, 1);
    g_skateModule = nullptr;
    Refs() = 0;
}

// 0x005691A0
Skate* __cdecl Skate::SpawnOccurrence(bool create)
{
    Skate* skate = Sole();
    if (!skate && create) {
        void* memory = MemSys::AllocZeroed(sizeof(Skate), nullptr, 1);
        if (memory) {
            skate = static_cast<Skate*>(memory)->MakeNew();
            int refs = Refs() + 1;
            g_skateModule = skate;
            Refs() = refs;
            return skate;
        }
        g_skateModule = nullptr;
    }
    ++Refs();
    return skate; // (the instance - null when none was made: main registers it as a module)
}

// 0x00564C50
void Skate::StopCallback() { m_updateJob->DropOut(); }

// 0x00564C60
void Skate::ConcealSkater(GameObjects::CRider* skater, bool hide)
{
    if (hide) {
        reinterpret_cast<void(__thiscall*)(GameObjects::CRider*)>(skater->m_vtable[0x2C / 4])(skater);
        skater->Conceal(hide);
    } else {
        reinterpret_cast<void(__thiscall*)(GameObjects::CRider*)>(skater->m_vtable[0x30 / 4])(skater);
        skater->Conceal(hide);
    }
}

// 0x00564CA0
void Skate::TimerBeep(float timeLeft, float startTime, float rate, const char* script)
{
    if (!(timeLeft > 0.01f) || !(timeLeft <= startTime)) {
        NextBeep() = startTime;
        return;
    }
    if (!(timeLeft < NextBeep()))
        return;
    Scripts::LaunchScript(script, nullptr, nullptr, false);
    double step = static_cast<double>(NextBeep()) / rate;
    if (step < static_cast<double>(0.2f))
        step = static_cast<double>(0.2f);
    NextBeep() = static_cast<float>(NextBeep() - step);
}

// 0x00564D50
void Skate::SetUnknown370(int value) { m_unknown370 = value; }

// 0x00564D60
void Skate::AdvanceMatchFlow()
{
    if (m_ownScript)
        m_ownScript->TickFrame();
    m_gameFlow->TickFrame();
}

// 0x00564D80
OptionStore::Preferences* Skate::GetSplitViewSettings() { return m_splitscreenPreferences; }

// 0x00564D90
GoalSystem::CGoalManager* Skate::GetObjectiveBoard() { return m_goalManager; }

// 0x00564DA0
void Skate::ScanPadConnections() { m_watchForUnplug = true; }

// 0x00564DB0 (TaskList::Task<Skate>'s vtable slot 0): its code run with it.
void __fastcall Skate::RunTask(TaskList::SchedulerTask* task)
{
    auto* code = *reinterpret_cast<void(__cdecl**)(TaskList::SchedulerTask*)>(reinterpret_cast<uint8_t*>(task) + 0x18);
    code(task);
}

// 0x00564DC0
void Skate::StartCallback()
{
    SurfaceKind::LoadTerrainTypes();
    Scripts::LaunchScript(kInitLightManager, nullptr, nullptr, false, nullptr);
    Scripts::LaunchScript(kLoadPermanentPrefiles, nullptr, nullptr, false, nullptr);
    m_unknown3D0 = 0;
    auto* mainLoop = reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager);
    GameObjects::NearbyZone_Startup();
    GameObjects::Emitter_Init();
    m_updateJob->m_skipBits = 2;
    reinterpret_cast<TaskList::PileUp*>(mainLoop + 4)->QueueJob(m_updateJob); // the logic tasks
    GameMode()->Reset();
    Scripts::LaunchScript(kNetworkDriverStartup, nullptr, nullptr, false, nullptr);
    Scripts::LaunchScript(kInitLoadingBar, nullptr, nullptr, false, nullptr);
    Scripts::LaunchScript(kStartupLoadingScreen, nullptr, nullptr, false, nullptr);
    Sleep(2000);
    if (!AudioStream::MusicOffRequested() || !AudioStream::StreamsOff())
        AudioStream::PrepareAll();
    Scripts::LaunchScript(kLoadPermSounds, nullptr, nullptr, false, nullptr);
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    MemSys::PushMemProfile("Permanent Assets"); // "Permanent Assets"
    Scripts::LaunchScript(kLoadPermanentAssets, nullptr, nullptr, false, nullptr);
    MemSys::PopMemProfile();
    MemSys::LeaveHeapScope();
    RosterHandle()->PrepareAll();
    m_checksums->Fill();
    Scripts::LaunchScript(kInitEnvironment, nullptr, nullptr, false, nullptr);
    Scripts::LaunchScript(kDefaultSystemStartup, nullptr, nullptr, false, nullptr);
}

// 0x00564F20
void Skate::TidyBeforeParkBuild()
{
    GameObjects::g_trickSpotHub->RemoveAllClusters();
    Engine3D::Engine::KillAllTextureSplats();
    static_cast<Engine3D::CFlexibleParticleManager*>(Engine3D::g_flexibleParticleManager)->DestroyNonPersistent();
    m_railManager->TidyUp();
    GameObjects::NearbyZone_Shutdown();
    GameObjects::Emitter_Cleanup();
}

// 0x00564F60
void Skate::Freeze(bool paused)
{
    if (paused)
        SoundFx::g_audioMixer->UpdateVoiceVolumes();
}

// 0x00564F80
GameObjects::CRiderIdentity* Skate::FetchAccount(Scripts::CStruct* params)
{
    int skater;
    if (params && params->FetchIntValue(kSkater, &skater, 0))
        return RosterHandle()->FetchAccount(skater);
    return RosterHandle()->FetchActiveAccount();
}

// 0x00564FC0
GameObjects::CRiderIdentity* Skate::FetchAccount(int index) { return RosterHandle()->FetchAccount(index); }

// 0x00564FD0
void Skate::SetCurrentGameType()
{
    DebugOut::Printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<Changing Game Mode\n"); // "<<<...Changing Game Mode\n"
    GameMode()->ApplyMatchStyle(m_matchStyle);
}

// 0x00565000
void Skate::EnterStage(uint32_t level)
{
    OnlinePlay::Hub* net = NetSessionSlot();
    MemSys::PushMemProfile("LaunchLevel"); // "LaunchLevel"
    if (net->InNetGame() && net->OnServer()) {
        PlayerSearch search;
        for (void* player = net->BeginPlayerWalk(&search, true); player; player = net->StepPlayerWalk(&search, true))
            AsPlayer(player)->AwaitReadyResponse(0);
    }
    if (level != kLoadNothing)
        Scripts::LaunchScript(level, nullptr, nullptr, false, nullptr);
    m_levelLaunched = true;
    m_level = level;
    if (net->InNetGame()) {
        for (int i = 0; i < 2; ++i) {
            ConnSearch search;
            if (NetLink::AppHost* client = net->FetchPatron(i)) {
                if (NetLink::Conn* conn = client->BeginConnWalk(reinterpret_cast<NetLink::ConnSearch*>(&search)))
                    conn->StampLastHeard(12000);
            }
        }
        if (net->OnServer()) {
            ConnSearch search;
            if (NetLink::AppHost* server = net->m_host) { // (0x005480C0)
                for (NetLink::Conn* conn = server->BeginConnWalk(reinterpret_cast<NetLink::ConnSearch*>(&search)); conn;
                     conn = server->StepConnWalk(reinterpret_cast<NetLink::ConnSearch*>(&search)))
                    conn->StampLastHeard(8000);
            }
        } else {
            struct NewPlayerSearch {
                void** m_vtable = OnlinePlay::kNewPlayerSearchVtable;
                void* m_listLink = nullptr;
            } search;
            if (!net->BeginJoinerWalk(&search))
                net->SendReadyResponse();
        }
    }
    MemSys::PopMemProfile();
}

// 0x005651D0
void __cdecl Skate::ObserveNextSkater()
{
    OnlinePlay::Hub* net = NetSessionSlot();
    net->SpectatePlayer(static_cast<uint8_t*>(net->NextCamPlayer()));
}

// 0x005651F0
int __cdecl Skate::IsMultiplayer()
{
    if (NetSessionSlot()->InNetGame())
        return 1;
    return GameMode()->GetStartingRiderCount() > 1 ? 1 : 0;
}

// 0x00565220
bool __cdecl Skate::CFunc_SetGameType(Scripts::CStruct* params, Scripts::CScript*)
{
    Skate* skate = Sole();
    uint32_t type;
    params->ReadHashValue(0u, &type, true);
    skate->m_matchStyle = type;
    return true;
}

// 0x00565250
int __cdecl Skate::CFunc_InTeamGame(Scripts::CStruct*, Scripts::CScript*) { return GameMode()->IsSquadMatch(); }

// 0x00565260
bool __cdecl Skate::CFunc_ChangeLevel(Scripts::CStruct* params, Scripts::CScript*)
{
    DebugOut::Printf("ScriptChangeLevel\n"); // "ScriptChangeLevel\n"
    OnlinePlay::Hub* net = NetSessionSlot();
    uint32_t level;
    params->ReadHashValue(kLevel, &level, 1);
    if (level == kFromSettingsMarker)
        level = net->GetLevelPreference();
    net->ChangeArena(level);
    bool showWarning = params->HasEntryNamed(kShowWarning);
    AudioStream::StopMusic();
    PlayerSearch search;
    NetLink::AppHost* server = net->m_host; // (0x005480C0)
    struct ChangeLevelMessage {
        uint32_t level;
        bool showWarning;
        uint8_t pad[3];
    } message;
    message.level = level;
    message.showWarning = showWarning;
    for (void* player = net->BeginPlayerWalk(&search, true); player; player = net->StepPlayerWalk(&search, true)) {
        OnlinePlay::PlayerRecord* info = AsPlayer(player);
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        uint32_t now = FrameClock::GetTime();
        if (!static_cast<uint8_t>(info->IsOwnPlayer()) && level == kLoadSk5EdGameplay) {
            void* map = ParkEditing::g_parkEditor->GetPackedParkData(false);
            int handle = info->GetLinkHandle();
            server->SendBulkMessage(handle, kMsgParkMap, 15000, map, "level data", kGroupGame, false,
                                  true); // "level data"
            ParkEditing::CRailAuthoring::Sole(); // (the rail editor; this first answer unused, as the original)
            void* rails = ParkEditing::CRailAuthoring::Sole()->GetPacked();
            handle = info->GetLinkHandle();
            server->SendBulkMessage(handle, kMsgParkRails, 0x1132, rails, "rail data", kGroupGame, false,
                                  true); // "rail data"
        }
        desc.m_content = &message;
        desc.m_byteCount = sizeof(message);
        desc.m_ident = kMsgLevelSwitch;
        desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
        desc.m_groupId = kGroupGame;
        server->PutOnOutbox(info->GetLinkHandle(), &desc);
        if (!static_cast<uint8_t>(info->IsOwnPlayer()) && net->HasCreatedGoals()) {
            net->PrepareCreatedGoals(level);
            void* goals = net->GetCreatedGoalsMessage();
            uint32_t size = net->GetCreatedGoalsSize();
            server->SendBulkMessage(info->GetLinkHandle(), kMsgCreatedGoals, static_cast<uint16_t>(size), goals,
                                  "goals data", kGroupGame, false, true); // "goals data"
        }
        desc.m_content = &now;
        desc.m_byteCount = 4;
        desc.m_ident = kMsgReadyQuery;
        server->PutOnOutbox(info->GetLinkHandle(), &desc);
        info->SetReadyQueryOwed(now);
    }
    if (!net->InNetGame())
        server->Transmit(false);
    return true;
}

// 0x005654B0
bool __cdecl Skate::CFunc_LaunchLevel(Scripts::CStruct*, Scripts::CScript*)
{
    Skate* skate = Sole();
    skate->EnterStage(skate->m_requestedLevel);
    return true;
}

// 0x005654D0
bool __cdecl Skate::CFunc_RequestLevel(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* net = NetSessionSlot();
    Skate* skate = Sole();
    uint32_t level;
    if (!params->ReadHashValue(0u, &level, false)) {
        const char* name;
        if (params->FetchText(0u, &name, 1)) {
            char script[0x80];
            std::snprintf(script, sizeof(script), "load_%s", name); // "load_%s"
            level = Checksums::HashFromText(script);
        }
    }
    if (level == kFromSettingsMarker) {
        level = net->GetLevelPreference();
        net->ChangeArena(level);
    }
    skate->m_requestedLevel = level;
    return true;
}

// 0x005655B0
bool __cdecl Skate::CFunc_InitSkaterHeaps(Scripts::CStruct*, Scripts::CScript*)
{
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    int skaters;
    if (Sole()->m_matchStyle == kFreeskate2p) {
        skaters = 2;
    } else {
        skaters = NetSessionSlot()->GetPlayerLimit();
        if (skaters <= 1) {
            MemSys::LeaveHeapScope();
            return true;
        }
    }
    MemSys::Hub::Pull()->DeleteExtraSkaterHeaps();
    MemSys::Hub::Pull()->InitExtraSkaterHeaps(skaters);
    MemSys::LeaveHeapScope();
    return true;
}

// 0x00565620
void Skate::PrepareEntityUpdateData()
{
    if (g_viewportCount <= 1 && Engine3D::FetchLiveCamera(0)) {
        m_noCamera = false;
        m_cameraPos = *Engine3D::FetchLiveCamera(0)->FetchLocation();
        return;
    }
    m_noCamera = true;
}

// 0x00565680
OptionStore::Preferences* __cdecl Skate::GetPlayerOptions(uint32_t name)
{
    if (name == kNetwork)
        return NetSessionSlot()->GetOnlineSettings();
    if (name == kTaunt)
        return NetSessionSlot()->GetTauntSettings();
    if (name == kSplitscreen)
        return Sole()->m_splitscreenPreferences;
    return nullptr;
}

// 0x00565D40
OptionStore::Preferences* __cdecl Skate::GetPreferencesFromParams(Scripts::CStruct* params)
{
    uint32_t name;
    params->ReadHashValue(kPrefs, &name, true);
    return GetPlayerOptions(name); // (inlined there)
}

// 0x005656C0
void* Skate::FetchGapList() { return m_career->FetchGapList(-1); }

// 0x005656D0
int Skate::InNetGameLevel()
{
    if (NetSessionSlot()->InNetGame() && m_level != kLoadMainMenu)
        return 1;
    return 0;
}

// 0x00565700
bool Skate::InFrontEndOrNetMode()
{
    if (static_cast<uint8_t>(NetSessionSlot()->IsNetworkGame()))
        return true;
    return m_requestedLevel == kLoadMainMenu || m_requestedLevel == kLoadCas;
}

// 0x00565BB0
int __stdcall Skate::ShouldBeAbsent(Scripts::CStruct* params)
{
    if (!params->HasBareFlag(kAbsentInNetGames))
        return 0;
    if (NetSessionSlot()->InNetGame())
        return 1;
    return GameMode()->GetStartingRiderCount() > 1 ? 1 : 0;
}

// 0x00564D60's neighbours: the skater list's node and head (ListKit::Elem<CRider> 0x0064E544, ListKit::Foremost<CRider>
// 0x0064E5D4).
// 0x00565730: the head's destructor body (the node's).
void __fastcall SkaterHeadDestroyBody(MemSys::ListNode* head) { head->UnlinkAs(kSkaterNodeVtable); }

static VtableSlot kPlayerSearchVtable_slots[] = {&GameObjects::PlayerSearchDestructors::Teardown};
void** const kPlayerSearchVtable = reinterpret_cast<void**>(kPlayerSearchVtable_slots);
static VtableSlot kSkateVtable_slots[] = {&Skate::Teardown, &Skate::StartCallback, &Skate::StopCallback};
void** const kSkateVtable = reinterpret_cast<void**>(kSkateVtable_slots);
static VtableSlot kSkaterNodeVtable_slots[] = {Skate::DestroySkaterNode};
void** const kSkaterNodeVtable = reinterpret_cast<void**>(kSkaterNodeVtable_slots);
static VtableSlot kSkaterHeadVtable_slots[] = {Skate::DestroySkaterHead};
void** const kSkaterHeadVtable = reinterpret_cast<void**>(kSkaterHeadVtable_slots);

} // namespace Mdl
