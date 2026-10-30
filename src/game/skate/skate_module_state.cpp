// The skate module's state and its small commands (see skate_module.h): the singleton, the module callbacks, the
// game type, the level asked for / opened / changed, the preferences, the script commands that drive them.

#include "game/skate/skate_module.h"

#include <windows.h>

#include <cstdio>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/ed/park_editor.h"
#include "game/front/screen_element_manager.h"
#include "game/goals/goal_manager.h"
#include "game/gfx/camera.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/player_info.h"
#include "game/obj/emitter.h"
#include "game/obj/object.h"
#include "game/obj/proxim.h"
#include "game/obj/trick_objects.h"
#include "game/pcm/music_api.h"
#include "game/script/preferences.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/skate/career.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "game/obj/node_components.h"
#include "game/obj/rail_manager.h"
#include "game/skater/skater.h"
#include "game/sfx/terrain.h"
#include "game/sys/timer.h"
#include "game/ed/rail_editor.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/viewport.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mdl {

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
constexpr uint32_t kUsePreferences = 0x547CB588;         // use_preferences
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
constexpr uint8_t kMsgChangeLevel = 0x46;  // the level changes (to the level)
constexpr uint8_t kMsgCreatedGoals = 0x87; // the created goals ("goals data")
constexpr uint8_t kMsgReadyQuery = 0x25;   // are you ready (a time stamp)
constexpr uint8_t kGroupGame = 8;

const char* Text(uint32_t address) { return reinterpret_cast<const char*>(address); }
Game::CGameMode* GameMode() { return GameVariable<Game::CGameMode*>(0x007CE470); }
GameNet::Manager* NetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
Obj::CPlayerProfileManager* ProfileManager() { return GameVariable<Obj::CPlayerProfileManager*>(0x007CE4B4); }
int& Refs() { return GameVariable<int>(0x007CE47C); }
float& NextBeep() { return GameVariable<float>(0x007CE488); }

// A walk over the players (GameNet::Manager::FirstPlayerInfo / NextPlayerInfo).
struct PlayerSearch {
    void** m_vtable = reinterpret_cast<void**>(0x0064C1A8);
    void* m_node = nullptr;
};
// A walk over an app's connections (Net::App::FirstConnection / NextConnection).
struct ConnSearch {
    void** m_vtable = reinterpret_cast<void**>(0x0064759C);
    void* m_node = nullptr;
};
GameNet::PlayerInfo* AsPlayer(void* player) { return static_cast<GameNet::PlayerInfo*>(player); }

} // namespace

// 0x00564C10
void __cdecl Skate::DeleteInstance()
{
    if (static_cast<uint32_t>(Refs()) > 1) {
        --Refs();
        return;
    }
    if (Skate* skate = Instance())
        reinterpret_cast<Skate*(__thiscall*)(Skate*, int)>(skate->m_vtable[0])(skate, 1);
    GameVariable<Skate*>(0x007CE478) = nullptr;
    Refs() = 0;
}

// 0x005691A0
Skate* __cdecl Skate::CreateInstance(bool create)
{
    Skate* skate = Instance();
    if (!skate && create) {
        void* memory = Mem::Calloc(sizeof(Skate), nullptr, 1);
        if (memory) {
            skate = static_cast<Skate*>(memory)->Construct();
            int refs = Refs() + 1;
            GameVariable<Skate*>(0x007CE478) = skate;
            Refs() = refs;
            return skate;
        }
        GameVariable<Skate*>(0x007CE478) = nullptr;
    }
    ++Refs();
    return skate; // (the instance - null when none was made: main registers it as a module)
}

// 0x00564C50
void Skate::StopCallback() { m_logicTask->Remove(); }

// 0x00564C60
void Skate::HideSkater(Obj::CSkater* skater, bool hide)
{
    if (hide) {
        reinterpret_cast<void(__thiscall*)(Obj::CSkater*)>(skater->m_vtable[0x2C / 4])(skater);
        skater->Hide(hide);
    } else {
        reinterpret_cast<void(__thiscall*)(Obj::CSkater*)>(skater->m_vtable[0x30 / 4])(skater);
        skater->Hide(hide);
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
    Script::RunScript(script, nullptr, nullptr, false);
    double step = static_cast<double>(NextBeep()) / rate;
    if (step < static_cast<double>(0.2f))
        step = static_cast<double>(0.2f);
    NextBeep() = static_cast<float>(NextBeep() - step);
}

// 0x00564D50
void Skate::SetUnknown370(int value) { m_unknown370 = value; }

// 0x00564D60
void Skate::UpdateGameFlow()
{
    if (m_script)
        m_script->Update();
    m_gameFlow->Update();
}

// 0x00564D80
Prefs::Preferences* Skate::GetSplitScreenPreferences() { return m_splitscreenPreferences; }

// 0x00564D90
Game::CGoalManager* Skate::GetGoalManager() { return m_goalManager; }

// 0x00564DA0
void Skate::CheckControllers() { m_checkControllers = true; }

// 0x00564DB0 (Tsk::Task<Skate>'s vtable slot 0): its code run with it.
void __fastcall Skate::RunTask(Tsk::BaseTask* task)
{
    auto* code = *reinterpret_cast<void(__cdecl**)(Tsk::BaseTask*)>(reinterpret_cast<uint8_t*>(task) + 0x18);
    code(task);
}

// 0x00564DC0
void Skate::StartCallback()
{
    Terrain::LoadTerrainTypes();
    Script::RunScript(kInitLightManager, nullptr, nullptr, false, nullptr);
    Script::RunScript(kLoadPermanentPrefiles, nullptr, nullptr, false, nullptr);
    m_unknown3D0 = 0;
    auto* mainLoop = GameVariable<uint8_t*>(0x00696D68);
    Obj::Proxim_Init();
    Obj::Emitter_Init();
    m_logicTask->m_mask = 2;
    reinterpret_cast<Tsk::Stack*>(mainLoop + 4)->AddTask(m_logicTask); // the logic tasks
    GameMode()->Reset();
    Script::RunScript(kNetworkDriverStartup, nullptr, nullptr, false, nullptr);
    Script::RunScript(kInitLoadingBar, nullptr, nullptr, false, nullptr);
    Script::RunScript(kStartupLoadingScreen, nullptr, nullptr, false, nullptr);
    Sleep(2000);
    if (!Pcm::NoMusicPlease() || !Pcm::StreamsDisabled())
        Pcm::Init();
    Script::RunScript(kLoadPermSounds, nullptr, nullptr, false, nullptr);
    Mem::PushContext(Mem::BottomUpHeap());
    Mem::PushMemProfile(Text(0x0064E4B8)); // "Permanent Assets"
    Script::RunScript(kLoadPermanentAssets, nullptr, nullptr, false, nullptr);
    Mem::PopMemProfile();
    Mem::PopContext();
    ProfileManager()->Init();
    m_checksums->Fill();
    Script::RunScript(kInitEnvironment, nullptr, nullptr, false, nullptr);
    Script::RunScript(kDefaultSystemStartup, nullptr, nullptr, false, nullptr);
}

// 0x00564F20
void Skate::CleanupForParkEditor()
{
    GameVariable<Obj::CTrickObjectManager*>(0x007CE500)->RemoveAllClusters();
    Nx::Engine::KillAllTextureSplats();
    static_cast<Nx::CFlexibleParticleManager*>(GameVariable<void*>(0x006FA7C0))->DestroyNonPersistent();
    m_railManager->Cleanup();
    Obj::Proxim_Cleanup();
    Obj::Emitter_Cleanup();
}

// 0x00564F60
void Skate::Pause(bool paused)
{
    if (paused)
        GameVariable<Sfx::CSfxManager*>(0x006F85A8)->UpdateVoiceVolumes();
}

// 0x00564F80
Obj::CSkaterProfile* Skate::GetProfile(Script::CStruct* params)
{
    int skater;
    if (params && params->GetInteger(kSkater, &skater, 0))
        return ProfileManager()->GetProfile(skater);
    return ProfileManager()->GetCurrentProfile();
}

// 0x00564FC0
Obj::CSkaterProfile* Skate::GetProfile(int index) { return ProfileManager()->GetProfile(index); }

// 0x00564FD0
void Skate::SetCurrentGameType()
{
    Dbg::Printf(Text(0x0064E4D0)); // "<<<...Changing Game Mode\n"
    GameMode()->LoadGameType(m_gameType);
}

// 0x00565000
void Skate::OpenLevel(uint32_t level)
{
    GameNet::Manager* net = NetManager();
    Mem::PushMemProfile(Text(0x0064E51C)); // "LaunchLevel"
    if (net->InNetGame() && net->OnServer()) {
        PlayerSearch search;
        for (void* player = net->FirstPlayerInfo(&search, true); player; player = net->NextPlayerInfo(&search, true))
            AsPlayer(player)->AwaitReadyResponse(0);
    }
    if (level != kLoadNothing)
        Script::RunScript(level, nullptr, nullptr, false, nullptr);
    m_levelLaunched = true;
    m_level = level;
    if (net->InNetGame()) {
        for (int i = 0; i < 2; ++i) {
            ConnSearch search;
            if (Net::App* client = net->GetClient(i)) {
                if (Net::Conn* conn = client->FirstConnection(reinterpret_cast<Net::ConnSearch*>(&search)))
                    conn->UpdateCommTime(12000);
            }
        }
        if (net->OnServer()) {
            ConnSearch search;
            if (Net::App* server = net->m_server) { // (0x005480C0)
                for (Net::Conn* conn = server->FirstConnection(reinterpret_cast<Net::ConnSearch*>(&search)); conn;
                     conn = server->NextConnection(reinterpret_cast<Net::ConnSearch*>(&search)))
                    conn->UpdateCommTime(8000);
            }
        } else {
            struct NewPlayerSearch {
                void** m_vtable = reinterpret_cast<void**>(0x0064C82C);
                void* m_node = nullptr;
            } search;
            if (!net->FirstNewPlayerInfo(&search))
                net->SendReadyResponse();
        }
    }
    Mem::PopMemProfile();
}

// 0x005651D0
void __cdecl Skate::ObserveNextSkater()
{
    GameNet::Manager* net = NetManager();
    net->ObservePlayer(static_cast<uint8_t*>(net->NextCamPlayer()));
}

// 0x005651F0
int __cdecl Skate::IsMultiplayer()
{
    if (NetManager()->InNetGame())
        return 1;
    return GameMode()->GetInitialNumberOfPlayers() > 1 ? 1 : 0;
}

// 0x00565220
bool __cdecl Skate::CFunc_SetGameType(Script::CStruct* params, Script::CScript*)
{
    Skate* skate = Instance();
    uint32_t type;
    params->GetChecksum(0u, &type, true);
    skate->m_gameType = type;
    return true;
}

// 0x00565250
int __cdecl Skate::CFunc_InTeamGame(Script::CStruct*, Script::CScript*) { return GameMode()->IsTeamGame(); }

// 0x00565260
bool __cdecl Skate::CFunc_ChangeLevel(Script::CStruct* params, Script::CScript*)
{
    Dbg::Printf(Text(0x0064E528)); // "ScriptChangeLevel\n"
    GameNet::Manager* net = NetManager();
    uint32_t level;
    params->GetChecksum(kLevel, &level, 1);
    if (level == kUsePreferences)
        level = net->GetLevelPreference();
    net->SetLevel(level);
    bool showWarning = params->ContainsComponentNamed(kShowWarning);
    Pcm::StopMusic();
    PlayerSearch search;
    Net::App* server = net->m_server; // (0x005480C0)
    struct ChangeLevelMessage {
        uint32_t level;
        bool showWarning;
        uint8_t pad[3];
    } message;
    message.level = level;
    message.showWarning = showWarning;
    for (void* player = net->FirstPlayerInfo(&search, true); player; player = net->NextPlayerInfo(&search, true)) {
        GameNet::PlayerInfo* info = AsPlayer(player);
        Net::MsgDesc desc;
        desc.Construct();
        uint32_t now = Tmr::GetTime();
        if (!static_cast<uint8_t>(info->IsLocalPlayer()) && level == kLoadSk5EdGameplay) {
            void* map = GameVariable<Ed::CParkEditor*>(0x007CE5D8)->GetCompressedMapBuffer(false);
            int handle = info->GetConnHandle();
            server->StreamMessage(handle, kMsgParkMap, 15000, map, Text(0x0064C884), kGroupGame, false,
                                  true); // "level data"
            Ed::CRailEditorComponent::Instance(); // (the rail editor; this first answer unused, as the original)
            void* rails = Ed::CRailEditorComponent::Instance()->GetPacked();
            handle = info->GetConnHandle();
            server->StreamMessage(handle, kMsgParkRails, 0x1132, rails, Text(0x0064C878), kGroupGame, false,
                                  true); // "rail data"
        }
        desc.m_data = &message;
        desc.m_length = sizeof(message);
        desc.m_id = kMsgChangeLevel;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        server->EnqueueMessage(info->GetConnHandle(), &desc);
        if (!static_cast<uint8_t>(info->IsLocalPlayer()) && net->HasCreatedGoals()) {
            net->PrepareCreatedGoals(level);
            void* goals = net->GetCreatedGoalsMessage();
            uint32_t size = net->GetCreatedGoalsSize();
            server->StreamMessage(info->GetConnHandle(), kMsgCreatedGoals, static_cast<uint16_t>(size), goals,
                                  Text(0x0064C86C), kGroupGame, false, true); // "goals data"
        }
        desc.m_data = &now;
        desc.m_length = 4;
        desc.m_id = kMsgReadyQuery;
        server->EnqueueMessage(info->GetConnHandle(), &desc);
        info->SetReadyQueryOwed(now);
    }
    if (!net->InNetGame())
        server->SendData(false);
    return true;
}

// 0x005654B0
bool __cdecl Skate::CFunc_LaunchLevel(Script::CStruct*, Script::CScript*)
{
    Skate* skate = Instance();
    skate->OpenLevel(skate->m_requestedLevel);
    return true;
}

// 0x005654D0
bool __cdecl Skate::CFunc_RequestLevel(Script::CStruct* params, Script::CScript*)
{
    GameNet::Manager* net = NetManager();
    Skate* skate = Instance();
    uint32_t level;
    if (!params->GetChecksum(0u, &level, false)) {
        const char* name;
        if (params->GetString(0u, &name, 1)) {
            char script[0x80];
            std::snprintf(script, sizeof(script), Text(0x0064E53C), name); // "load_%s"
            level = Crc::GenerateCRCFromString(script);
        }
    }
    if (level == kUsePreferences) {
        level = net->GetLevelPreference();
        net->SetLevel(level);
    }
    skate->m_requestedLevel = level;
    return true;
}

// 0x005655B0
bool __cdecl Skate::CFunc_InitSkaterHeaps(Script::CStruct*, Script::CScript*)
{
    Mem::PushContext(Mem::BottomUpHeap());
    int skaters;
    if (Instance()->m_gameType == kFreeskate2p) {
        skaters = 2;
    } else {
        skaters = NetManager()->GetMaxPlayers();
        if (skaters <= 1) {
            Mem::PopContext();
            return true;
        }
    }
    Mem::Manager::Get()->DeleteExtraSkaterHeaps();
    Mem::Manager::Get()->InitExtraSkaterHeaps(skaters);
    Mem::PopContext();
    return true;
}

// 0x00565620
void Skate::PreCalculateObjectUpdateInfo()
{
    if (GameVariable<int>(0x00701378) <= 1 && Nx::GetActiveCamera(0)) {
        m_noCamera = false;
        m_cameraPos = *Nx::GetActiveCamera(0)->GetPos();
        return;
    }
    m_noCamera = true;
}

// 0x00565680
Prefs::Preferences* __cdecl Skate::GetPreferences(uint32_t name)
{
    if (name == kNetwork)
        return NetManager()->GetNetworkPreferences();
    if (name == kTaunt)
        return NetManager()->GetTauntPreferences();
    if (name == kSplitscreen)
        return Instance()->m_splitscreenPreferences;
    return nullptr;
}

// 0x00565D40
Prefs::Preferences* __cdecl Skate::GetPreferencesFromParams(Script::CStruct* params)
{
    uint32_t name;
    params->GetChecksum(kPrefs, &name, true);
    return GetPreferences(name); // (inlined there)
}

// 0x005656C0
void* Skate::GetGapChecklist() { return m_career->GetGapChecklist(-1); }

// 0x005656D0
int Skate::InNetGameLevel()
{
    if (NetManager()->InNetGame() && m_level != kLoadMainMenu)
        return 1;
    return 0;
}

// 0x00565700
bool Skate::InFrontEndOrNetMode()
{
    if (static_cast<uint8_t>(NetManager()->InNetMode()))
        return true;
    return m_requestedLevel == kLoadMainMenu || m_requestedLevel == kLoadCas;
}

// 0x00565BB0
int __stdcall Skate::ShouldBeAbsent(Script::CStruct* params)
{
    if (!params->ContainsFlag(kAbsentInNetGames))
        return 0;
    if (NetManager()->InNetGame())
        return 1;
    return GameMode()->GetInitialNumberOfPlayers() > 1 ? 1 : 0;
}

// 0x00564D60's neighbours: the skater list's node and head (Lst::Node<CSkater> 0x0064E544, Lst::Head<CSkater>
// 0x0064E5D4).
// 0x00565730: the head's destructor body (the node's).
void __fastcall SkaterHeadDestroyBody(Mem::ListNode* head) { head->UnlinkAs(reinterpret_cast<void**>(0x0064E544)); }

} // namespace Mdl

REPLACE(0x00564C10, Mdl::Skate::DeleteInstance);
REPLACE(0x005691A0, Mdl::Skate::CreateInstance);
REPLACE(0x00564C50, &Mdl::Skate::StopCallback);
REPLACE(0x00564C60, &Mdl::Skate::HideSkater);
REPLACE(0x00564CA0, &Mdl::Skate::TimerBeep);
REPLACE(0x00564D50, &Mdl::Skate::SetUnknown370);
REPLACE(0x00564D60, &Mdl::Skate::UpdateGameFlow);
REPLACE_SOURCE_ONLY(0x00564D80, &Mdl::Skate::GetSplitScreenPreferences); // 4 bytes
REPLACE(0x00564D90, &Mdl::Skate::GetGoalManager);
REPLACE(0x00564DA0, &Mdl::Skate::CheckControllers);
REPLACE(0x00564DB0, Mdl::Skate::RunTask);
REPLACE(0x00564DC0, &Mdl::Skate::StartCallback);
REPLACE(0x00564F20, &Mdl::Skate::CleanupForParkEditor);
REPLACE(0x00564F60, &Mdl::Skate::Pause);
REPLACE(0x00564F80, static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile));
REPLACE(0x00564FC0, static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(int)>(&Mdl::Skate::GetProfile));
REPLACE(0x00564FD0, &Mdl::Skate::SetCurrentGameType);
REPLACE(0x00565000, &Mdl::Skate::OpenLevel);
REPLACE(0x005651D0, Mdl::Skate::ObserveNextSkater);
REPLACE(0x005651F0, Mdl::Skate::IsMultiplayer);
REPLACE(0x00565220, Mdl::Skate::CFunc_SetGameType);
REPLACE(0x00565250, Mdl::Skate::CFunc_InTeamGame);
REPLACE(0x00565260, Mdl::Skate::CFunc_ChangeLevel);
REPLACE(0x005654B0, Mdl::Skate::CFunc_LaunchLevel);
REPLACE(0x005654D0, Mdl::Skate::CFunc_RequestLevel);
REPLACE(0x005655B0, Mdl::Skate::CFunc_InitSkaterHeaps);
REPLACE(0x00565620, &Mdl::Skate::PreCalculateObjectUpdateInfo);
REPLACE(0x00565680, Mdl::Skate::GetPreferences);
REPLACE(0x00565D40, Mdl::Skate::GetPreferencesFromParams);
REPLACE(0x005656C0, &Mdl::Skate::GetGapChecklist);
REPLACE(0x005656D0, &Mdl::Skate::InNetGameLevel);
REPLACE(0x00565700, &Mdl::Skate::InFrontEndOrNetMode);
REPLACE(0x00565BB0, Mdl::Skate::ShouldBeAbsent);
REPLACE(0x00565730, Mdl::SkaterHeadDestroyBody);
