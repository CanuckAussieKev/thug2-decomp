// The game's main.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/sys/startup.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game/ass/resource_registry.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/memory_sizes.h"
#include "game/core/slab_bank.h"
#include "game/core/stubs.h"
#include "game/core/task_list.h"
#include "game/ed/park_editor.h"
#include "game/file/async.h"
#include "game/file/archive.h"
#include "game/front/career_complete.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/front/new_frontend.h"
#include "game/front/screen_element_manager.h"
#include "game/front/test_view.h"
#include "game/gfx/engine.h"
#include "game/gfx/tod_manager.h"
#include "game/mc/memcard.h"
#include "game/mth/maths.h"
#include "game/mth/random.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "game/obj/component_types.h"
#include "game/obj/compositemgr.h"
#include "game/obj/entity_hub.h"
#include "game/obj/path_manager.h"
#include "game/obj/tracker.h"
#include "game/obj/trick_objects.h"
#include "game/script/cache.h"
#include "game/script/script_startup.h"
#include "game/script/param_struct.h"
#include "game/sfx/sfx.h"
#include "game/sio/device.h"
#include "game/sio/input_hub.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "game/skate/highscores.h"
#include "game/skate/skate_module.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/key_names.h"
#include "game/sys/mlp.h"
#include "game/sys/game_module.h"
#include "game/sys/pc_setup.h"
#include "game/sys/stopwatch.h"
#include "game/script/node_manager.h"
#include "game/sys/game_config.h"
#include "game/gfx/movie.h"
#include "game/obj/collide_and_die.h"

namespace SystemCore {

namespace {

// Compiled-out debug code at the start of main (a call to the empty 0x005480A0 with 0).
inline void DebugSetUp(int /*unused*/) {}

// The main loop's task manager (FrameLoop::Hub): three task lists, at +0x04, +0x40 and +0x7C.
void QueueJob(TaskList::PileUp& list, void* task) { list.QueueJob(static_cast<TaskList::SchedulerTask*>(task)); }
void ClearJobList(TaskList::PileUp& list) { list.ClearJobList(); }
void MainLoop(FrameLoop::Hub* mlp) { mlp->MainLoop(); }

// The module manager (GameModules::Hub, 0x0044F6F0). (The modules are passed as they were made: not all of their types
// derive from GameModules::PartUnit in our source yet.)
void EnrolUnit(GameModules::Hub* mdl, void* module) { mdl->EnrolUnit(static_cast<GameModules::PartUnit*>(module)); }
void LaunchModule(GameModules::Hub* mdl, void* module) { mdl->LaunchModule(static_cast<GameModules::PartUnit*>(module)); }
void HaltEveryModule(GameModules::Hub* mdl) { mdl->HaltEveryModule(); }
void RemoveModule(GameModules::Hub* mdl, void* module) { mdl->RemoveModule(static_cast<GameModules::PartUnit*>(module)); }

// The bottom-up heap's virtual EnterHeapScope / LeaveHeapScope (slots 0 and 1).
void PushBottomUpHeap()
{
    MemSys::MemSource* heap = MemSys::Hub::Pull()->m_bottomUpHeap;
    reinterpret_cast<void(__thiscall*)(MemSys::MemSource*)>(heap->m_vtable[0])(heap);
}
void PopBottomUpHeap()
{
    MemSys::MemSource* heap = MemSys::Hub::Pull()->m_bottomUpHeap;
    reinterpret_cast<void(__thiscall*)(MemSys::MemSource*)>(heap->m_vtable[1])(heap);
}

bool QuitRequested() { return Engine3D::g_quitRequested; }

} // namespace

bool g_demoMode;

// 0x004E1EF0
int __cdecl Main(int argc, char** argv)
{
    DebugSetUp(0);
    GameConfig::PrepareAll(argc, argv); // the command line options (Config)
    MemSys::Hub::Pull()->InitStandardHeaps();
    switch (GameConfig::g_language) { // the language's start-up (German has its own)
    case 3: // French
        SetKeyNames();
        break;
    case 5: // German
        SetGermanKeyNames();
        break;
    default:
        SetKeyNames();
        break;
    }
    DebugOut::Printf("\n Start of main()\n");
    DebugOut::Printf("argc = %d\n", argc);
    for (int i = 0; i < argc; ++i)
        DebugOut::Printf("%2d: %s\n", i, argv[i]);
    DebugOut::Printf("\n");
    MemSys::PushMemProfile("Everything");
    MemSys::PushMemProfile("Initialization");
    if (argc == 2 && std::strcmp(argv[1], "demo") == 0)
        g_demoMode = true; // demo mode
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    Maths::SeedRandom(0x06680BF3);
    Maths::FillSineTable();

    while (!QuitRequested()) {
        PushBottomUpHeap();
        Stubs::IgnoreArgument(0x10); // (a memory manager method, compiled down to the shared stub)

        MemSys::PushMemProfile("Symbol Hash Table");
        Scripts::CreateSymbolTable();
        MemSys::PopMemProfile();
        MemSys::PushMemProfile("Hash Item Pool Manager");
        // Not original - the HashItem pool's size is ours (game/core/memory_sizes.h: pool sizes as THUGPRO's;
        // the main block grows to match). Original behaviour: 10000 items.
        MemSys::SlotPoolSetup::SetUp(0, MemSys::Sizes::AtHand().hashItems);
        MemSys::PopMemProfile();
        Pre::CreateManager(true);
        FrameClock::PrepareAll();
        MemSys::PushMemProfile("File System");
        FileLoad::AsyncInit(); // (passed "DATA", unused)
        MemSys::PopMemProfile();

        MemSys::PushMemProfile("System Singletons");
        GameObjects::CreateTracker(true);
        FrameLoop::Hub* mlp = FrameLoop::CreateManager(true);
        GameModules::Hub* mdl = GameModules::CreateManager(true);
        Sio::Hub* sio = Sio::CreateManager(true);
        MemCard::CreateManager(true);
        PadInput::Hub* input = PadInput::CreateManager(true);
        FileLoad::AsyncLoaderModule* asyncLoader = FileLoad::CreateAsyncLoaderModule(true);
        MemSys::PopMemProfile();
        PadInput::g_inputManager->SetOverridePadWithStick(true); // (+0x10C)
        Scripts::AllocatePools();
        // Not original - the level editor (game/front/level_editor.h) and CREATE 100% CAS
        // (game/front/career_complete.h): their script commands (LevelEditor_Start..., Career100_Apply...)
        // registered after the game's own.
        // Original behaviour: only the game's table (0x0067F3A0) and its member functions are registered.
        MenuSystem::LevelEditor::RegisterScriptFunctions();
        MenuSystem::CareerComplete::RegisterScriptFunctions();
        MenuSystem::TestView::RegisterScriptFunctions(); // (TeleportSkaterStanding - game/front/test_view.h)
        Skater::ThawBmx::RegisterScriptFunctions(); // (ThawBmx_Available, the bike's members - game/skater/thaw_bmx.h)
        MenuSystem::NewFrontend::RegisterScriptFunctions(); // (the overlay's pause items: NewFrontend_PauseItem / _Open)
        Scripts::LoadBootScriptFiles();

        MemSys::PushMemProfile("Game Singletons");
        GameObjects::CPathManager::Sole();
        Scripts::CScriptStore* scriptCache = Scripts::CreateScriptCache(true);
        AssetStore::CResourceRegistry* assets = AssetStore::CreateAssetManager(true);
        SoundFx::CreateManager(true);
        NetLink::CreateManager(true);
        GameObjects::CreateCompositeObjectManager(true);
        GameObjects::RegisterComponentTypes();
        GameObjects::AcquireTrickObjectManager(true);
        GoalSystem::CMatchRules::Sole(true);
        GameObjects::CProfileRoster::Fabricate(true);
        Highscores::CBestResults::Fabricate(true);
        uint8_t* module5691A0 = reinterpret_cast<uint8_t*>(GameModules::Skate::SpawnOccurrence(true));
        auto* frontEnd = reinterpret_cast<uint8_t*>(GameModules::CreateFrontEnd(true));
        OnlinePlay::Hub::Fabricate(true);
        reinterpret_cast<void*&>(GameObjects::g_gameNetManager) = OnlinePlay::g_gameNetManager; // (the network manager)
        auto* parkEditor = reinterpret_cast<uint8_t*>(ParkEditing::CParkBuilderMode::Fabricate(true));
        MenuSystem::CreateScreenElementManager(true);
        LevelNodes::CreateNodeManager(true);
        Gfx2D::CreateTODManager(true);
        MemSys::PopMemProfile();

        MemSys::PushMemProfile("Resgistering and starting modules");
        QueueJob(mlp->m_systemTasks, mdl->m_task);
        QueueJob(mlp->m_systemTasks, sio->m_task);
        QueueJob(mlp->m_logicTasks, input->m_task);
        EnrolUnit(mdl, scriptCache);
        EnrolUnit(mdl, parkEditor);
        EnrolUnit(mdl, frontEnd);
        EnrolUnit(mdl, module5691A0);
        EnrolUnit(mdl, asyncLoader);
        EnrolUnit(mdl, assets);
        LaunchModule(mdl, scriptCache);
        LaunchModule(mdl, frontEnd);
        LaunchModule(mdl, module5691A0);
        LaunchModule(mdl, parkEditor);
        LaunchModule(mdl, asyncLoader);
        LaunchModule(mdl, assets);
        MemSys::PopMemProfile();
        MemSys::PushMemProfile("sStartEngine");
        Engine3D::Engine::PrepareAll(350); // start the engine
        MemSys::PopMemProfile();
        MemSys::PopMemProfile(); // (Initialization)

        MemSys::PushMemProfile("MainLoop");
        MainLoop(mlp);
        // Originally from PartyMod Fixes: the process ends here (FastExit, in place of the original's PopMemProfile() - PartyMod's call
        // at 0x004E2492). The
        // original went on to tear everything down and, unless quitting, start again from the top.
        FastExit();

        HaltEveryModule(mdl);
        RemoveModule(mdl, module5691A0); // (the script cache isn't unregistered)
        RemoveModule(mdl, frontEnd);
        RemoveModule(mdl, parkEditor);
        RemoveModule(mdl, asyncLoader);
        RemoveModule(mdl, assets);
        ClearJobList(mlp->m_displayTasks);
        ClearJobList(mlp->m_logicTasks);
        ClearJobList(mlp->m_systemTasks);
        Gfx2D::ReleaseTODManager();
        LevelNodes::ReleaseNodeManager();
        MenuSystem::DeleteScreenElementManager();
        ParkEditing::CParkBuilderMode::Release();
        OnlinePlay::ReleaseManager();
        GameModules::ReleaseFrontEnd();
        GameModules::Skate::DiscardOccurrence();
        Highscores::CBestResults::Release();
        GameObjects::CProfileRoster::Release();
        GoalSystem::CMatchRules::Release();
        GameObjects::ReleaseTrickObjectManager();
        GameObjects::ReleaseCompositeObjectManager();
        NetLink::ReleaseManager();
        SoundFx::ReleaseManager();
        AssetStore::ReleaseAssetManager();
        Scripts::ReleaseScriptCache();
        FileLoad::DestroyAsyncLoaderModule();
        PadInput::ReleaseManager();
        MemCard::ReleaseManager();
        Sio::ReleaseManager();
        GameModules::ReleaseManager();
        FrameLoop::ReleaseManager();
        GameObjects::ReleaseTracker();
        Pre::DestroyManager();
        MemSys::PopMemProfile(); // (Everything)
        Stubs::IgnoreArgument(0x10);
        PopBottomUpHeap();
    }
    return 0;
}

} // namespace Sys
