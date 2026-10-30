// The game's main.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/sys/main.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game/ass/assman.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/memory_sizes.h"
#include "game/core/pool.h"
#include "game/core/stubs.h"
#include "game/core/task.h"
#include "game/ed/park_editor.h"
#include "game/file/async.h"
#include "game/file/pre.h"
#include "game/front/career_complete.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/front/new_frontend.h"
#include "game/front/screen_element_manager.h"
#include "game/front/test_view.h"
#include "game/gfx/engine.h"
#include "game/gfx/tod_manager.h"
#include "game/mc/memcard.h"
#include "game/mth/math.h"
#include "game/mth/random.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "game/obj/component_types.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/obj/path_manager.h"
#include "game/obj/tracker.h"
#include "game/obj/trick_objects.h"
#include "game/script/cache.h"
#include "game/script/script_startup.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sio/device.h"
#include "game/sio/inpman.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "game/skate/records.h"
#include "game/skate/skate_module.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/key_names.h"
#include "game/sys/mlp.h"
#include "game/sys/module.h"
#include "game/sys/pc_setup.h"
#include "game/sys/timer.h"
#include "game/script/node_manager.h"
#include "game/sys/game_config.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/movie.h"
#include "game/obj/collide_and_die.h"


namespace Sys {

namespace {

// Compiled-out debug code at the start of main (a call to the empty 0x005480A0 with 0).
inline void DebugSetUp(int /*unused*/) {}

// The main loop's task manager (Mlp::Manager): three task lists, at +0x04, +0x40 and +0x7C.
void AddTask(Tsk::Stack& list, void* task) { list.AddTask(static_cast<Tsk::BaseTask*>(task)); }
void RemoveAllTasks(Tsk::Stack& list) { list.RemoveAllTasks(); }
void MainLoop(Mlp::Manager* mlp) { mlp->MainLoop(); }

// The module manager (Mdl::Manager, 0x0044F6F0). (The modules are passed as they were made: not all of their types
// derive from Mdl::Module in our source yet.)
void RegisterModule(Mdl::Manager* mdl, void* module) { mdl->RegisterModule(static_cast<Mdl::Module*>(module)); }
void StartModule(Mdl::Manager* mdl, void* module) { mdl->StartModule(static_cast<Mdl::Module*>(module)); }
void StopAllModules(Mdl::Manager* mdl) { mdl->StopAllModules(); }
void UnregisterModule(Mdl::Manager* mdl, void* module) { mdl->UnregisterModule(static_cast<Mdl::Module*>(module)); }

// The bottom-up heap's virtual PushContext / PopContext (slots 0 and 1).
void PushBottomUpHeap()
{
    Mem::Allocator* heap = Mem::Manager::Get()->m_bottomUpHeap;
    reinterpret_cast<void(__thiscall*)(Mem::Allocator*)>(heap->m_vtable[0])(heap);
}
void PopBottomUpHeap()
{
    Mem::Allocator* heap = Mem::Manager::Get()->m_bottomUpHeap;
    reinterpret_cast<void(__thiscall*)(Mem::Allocator*)>(heap->m_vtable[1])(heap);
}

bool QuitRequested() { return Nx::g_quitRequested; }

} // namespace

GAME_GLOBAL(bool, g_demoMode, 0x007CC978);

// 0x004E1EF0
int __cdecl Main(int argc, char** argv)
{
    DebugSetUp(0);
    GameConfig::Init(argc, argv); // the command line options (Config)
    Mem::Manager::Get()->InitStandardHeaps();
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
    Dbg::Printf("\n Start of main()\n");
    Dbg::Printf("argc = %d\n", argc);
    for (int i = 0; i < argc; ++i)
        Dbg::Printf("%2d: %s\n", i, argv[i]);
    Dbg::Printf("\n");
    Mem::PushMemProfile("Everything");
    Mem::PushMemProfile("Initialization");
    if (argc == 2 && std::strcmp(argv[1], "demo") == 0)
        g_demoMode = true; // demo mode
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    Mth::InitialRand(0x06680BF3);
    Mth::InitSinLookupTable();

    while (!QuitRequested()) {
        PushBottomUpHeap();
        Stubs::IgnoreArgument(0x10); // (a memory manager method, compiled down to the shared stub)

        Mem::PushMemProfile("Symbol Hash Table");
        Script::CreateSymbolTable();
        Mem::PopMemProfile();
        Mem::PushMemProfile("Hash Item Pool Manager");
        // Not original - the HashItem pool's size is ours (game/core/memory_sizes.h: pool sizes as THUGPRO's;
        // the main block grows to match). Original behaviour: 10000 items.
        Mem::PoolManager::SetUp(0, Mem::Sizes::Current().hashItems);
        Mem::PopMemProfile();
        Pre::CreateManager(true);
        Tmr::Init();
        Mem::PushMemProfile("File System");
        File::AsyncInit(); // (passed "DATA", unused)
        Mem::PopMemProfile();

        Mem::PushMemProfile("System Singletons");
        Obj::CreateTracker(true);
        Mlp::Manager* mlp = Mlp::CreateManager(true);
        Mdl::Manager* mdl = Mdl::CreateManager(true);
        Sio::Manager* sio = Sio::CreateManager(true);
        Mc::CreateManager(true);
        Inp::Manager* input = Inp::CreateManager(true);
        File::AsyncLoaderModule* asyncLoader = File::CreateAsyncLoaderModule(true);
        Mem::PopMemProfile();
        Inp::g_inputManager->SetOverridePadWithStick(true); // (+0x10C)
        Script::AllocatePools();
        // Not original - the level editor (game/front/level_editor.h) and CREATE 100% CAS
        // (game/front/career_complete.h): their script commands (LevelEditor_Start..., Career100_Apply...)
        // registered after the game's own.
        // Original behaviour: only the game's table (0x0067F3A0) and its member functions are registered.
        Front::LevelEditor::RegisterScriptFunctions();
        Front::CareerComplete::RegisterScriptFunctions();
        Front::TestView::RegisterScriptFunctions(); // (TeleportSkaterStanding - game/front/test_view.h)
        Skater::ThawBmx::RegisterScriptFunctions(); // (ThawBmx_Available, the bike's members - game/skater/thaw_bmx.h)
        Front::NewFrontend::RegisterScriptFunctions(); // (the overlay's pause items: NewFrontend_PauseItem / _Open)
        Script::LoadAllStartupQBFiles();

        Mem::PushMemProfile("Game Singletons");
        Obj::CPathManager::Instance();
        Script::CScriptCache* scriptCache = Script::CreateScriptCache(true);
        Ass::CAssMan* assets = Ass::CreateAssetManager(true);
        Sfx::CreateManager(true);
        Net::CreateManager(true);
        Obj::CreateCompositeObjectManager(true);
        Obj::RegisterComponentTypes();
        Obj::AcquireTrickObjectManager(true);
        Game::CGameMode::Instance(true);
        Obj::CPlayerProfileManager::Create(true);
        Records::CGameRecords::Create(true);
        uint8_t* module5691A0 = reinterpret_cast<uint8_t*>(Mdl::Skate::CreateInstance(true));
        auto* frontEnd = reinterpret_cast<uint8_t*>(Mdl::CreateFrontEnd(true));
        GameNet::Manager::Create(true);
        reinterpret_cast<void*&>(Obj::g_gameNetManager) = GameNet::g_gameNetManager; // (the network manager)
        auto* parkEditor = reinterpret_cast<uint8_t*>(Ed::CParkEditor::Create(true));
        Front::CreateScreenElementManager(true);
        SkateScript::CreateNodeManager(true);
        Gfx::CreateTODManager(true);
        Mem::PopMemProfile();

        Mem::PushMemProfile("Resgistering and starting modules");
        AddTask(mlp->m_systemTasks, mdl->m_task);
        AddTask(mlp->m_systemTasks, sio->m_task);
        AddTask(mlp->m_logicTasks, input->m_task);
        RegisterModule(mdl, scriptCache);
        RegisterModule(mdl, parkEditor);
        RegisterModule(mdl, frontEnd);
        RegisterModule(mdl, module5691A0);
        RegisterModule(mdl, asyncLoader);
        RegisterModule(mdl, assets);
        StartModule(mdl, scriptCache);
        StartModule(mdl, frontEnd);
        StartModule(mdl, module5691A0);
        StartModule(mdl, parkEditor);
        StartModule(mdl, asyncLoader);
        StartModule(mdl, assets);
        Mem::PopMemProfile();
        Mem::PushMemProfile("sStartEngine");
        Nx::Engine::Init(350); // start the engine
        Mem::PopMemProfile();
        Mem::PopMemProfile(); // (Initialization)

        Mem::PushMemProfile("MainLoop");
        MainLoop(mlp);
        // Originally from PartyMod Fixes: the process ends here (FastExit, in place of the original's PopMemProfile() - PartyMod's call
        // at 0x004E2492). The
        // original went on to tear everything down and, unless quitting, start again from the top.
        FastExit();

        StopAllModules(mdl);
        UnregisterModule(mdl, module5691A0); // (the script cache isn't unregistered)
        UnregisterModule(mdl, frontEnd);
        UnregisterModule(mdl, parkEditor);
        UnregisterModule(mdl, asyncLoader);
        UnregisterModule(mdl, assets);
        RemoveAllTasks(mlp->m_displayTasks);
        RemoveAllTasks(mlp->m_logicTasks);
        RemoveAllTasks(mlp->m_systemTasks);
        Gfx::ReleaseTODManager();
        SkateScript::ReleaseNodeManager();
        Front::DeleteScreenElementManager();
        Ed::CParkEditor::Release();
        GameNet::ReleaseManager();
        Mdl::ReleaseFrontEnd();
        Mdl::Skate::DeleteInstance();
        Records::CGameRecords::Release();
        Obj::CPlayerProfileManager::Release();
        Game::CGameMode::Release();
        Obj::ReleaseTrickObjectManager();
        Obj::ReleaseCompositeObjectManager();
        Net::ReleaseManager();
        Sfx::ReleaseManager();
        Ass::ReleaseAssetManager();
        Script::ReleaseScriptCache();
        File::DestroyAsyncLoaderModule();
        Inp::ReleaseManager();
        Mc::ReleaseManager();
        Sio::ReleaseManager();
        Mdl::ReleaseManager();
        Mlp::ReleaseManager();
        Obj::ReleaseTracker();
        Pre::DestroyManager();
        Mem::PopMemProfile(); // (Everything)
        Stubs::IgnoreArgument(0x10);
        PopBottomUpHeap();
    }
    return 0;
}

} // namespace Sys

REPLACE(0x004E1EF0, Sys::Main);
