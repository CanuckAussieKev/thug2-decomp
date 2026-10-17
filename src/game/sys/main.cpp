// The game's main.
// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/sys/main.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/stubs.h"
#include "game/file/async.h"
#include "game/file/pre.h"
#include "game/front/front_end.h"
#include "game/mc/memcard.h"
#include "game/mth/math.h"
#include "game/mth/random.h"
#include "game/net/net_manager_view.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/obj/tracker.h"
#include "game/script/cache.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/sys/mlp.h"
#include "game/sys/pc_setup.h"
#include "game/sys/timer.h"
#include "game/script/node_manager.h"
#include "hook/game.h"
#include "hook/hook.h"


namespace Sys {

namespace {

// Compiled-out debug code at the start of main (a call to the empty 0x005480A0 with 0).
inline void DebugSetUp(int /*unused*/) {}

// The singletons without source yet: Create(true) makes the instance (or takes another reference) and returns
// it; Release drops a reference.
uint8_t* Create(uint32_t create) { return GameFunction<uint8_t*(__cdecl*)(bool)>(create)(true); }
void Release(uint32_t release) { GameFunction<void(__cdecl*)()>(release)(); }

// The main loop's task manager (Mlp::Manager): three task lists, at +0x04, +0x40 and +0x7C.
void AddTask(uint8_t* mlp, uint32_t list, void* task)
{
    GameFunction<void(__thiscall*)(uint8_t*, void*)>(0x00406C90)(mlp + list, task);
}
void RemoveAllTasks(uint8_t* mlp, uint32_t list) { GameFunction<void(__thiscall*)(uint8_t*)>(0x00406CB0)(mlp + list); }
void MainLoop(uint8_t* mlp) { reinterpret_cast<Mlp::Manager*>(mlp)->MainLoop(); }

// The module manager (Mdl::Manager, 0x0044F6F0).
void RegisterModule(uint8_t* mdl, void* module) { GameFunction<void(__thiscall*)(uint8_t*, void*)>(0x0044F3C0)(mdl, module); }
void StartModule(uint8_t* mdl, void* module) { GameFunction<void(__thiscall*)(uint8_t*, void*)>(0x0044F310)(mdl, module); }
void StopAllModules(uint8_t* mdl) { GameFunction<void(__thiscall*)(uint8_t*)>(0x0044F530)(mdl); }
void UnregisterModule(uint8_t* mdl, void* module) { GameFunction<void(__thiscall*)(uint8_t*, void*)>(0x0044F3E0)(mdl, module); }

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

bool QuitRequested() { return GameVariable<bool>(0x007D6A2C); }

} // namespace

// 0x004E1EF0
int __cdecl Main(int argc, char** argv)
{
    DebugSetUp(0);
    GameFunction<void(__cdecl*)(int, char**)>(0x005B60B0)(argc, argv); // the command line options (Config)
    Mem::Manager::Get()->InitStandardHeaps();
    switch (GameVariable<int>(0x007CEFA0)) { // the language's start-up (German has its own)
    case 3: // French
        GameFunction<void(__cdecl*)()>(0x004E0DA0)();
        break;
    case 5: // German
        GameFunction<void(__cdecl*)()>(0x004E0FE0)();
        break;
    default:
        GameFunction<void(__cdecl*)()>(0x004E0DA0)();
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
        GameVariable<bool>(0x007CC978) = true; // demo mode
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
        Mem::PoolManager::SetUp(0, 10000);
        Mem::PopMemProfile();
        Pre::CreateManager(true);
        Tmr::Init();
        Mem::PushMemProfile("File System");
        File::AsyncInit(); // (passed "DATA", unused)
        Mem::PopMemProfile();

        Mem::PushMemProfile("System Singletons");
        Obj::CreateTracker(true);
        auto* mlp = reinterpret_cast<uint8_t*>(Mlp::CreateManager(true));
        uint8_t* mdl = Create(0x0044F6F0);
        auto* sio = reinterpret_cast<uint8_t*>(Sio::CreateManager(true));
        Mc::CreateManager(true);
        uint8_t* singleton44E7F0 = Create(0x0044E7F0);
        File::AsyncLoaderModule* asyncLoader = File::CreateAsyncLoaderModule(true);
        Mem::PopMemProfile();
        GameFunction<void(__thiscall*)(void*, bool)>(0x0044E610)(GameVariable<void*>(0x00696D40), true); // (+0x10C)
        GameFunction<void(__cdecl*)()>(0x005A5E70)();
        GameFunction<void(__cdecl*)()>(0x005A5B20)();

        Mem::PushMemProfile("Game Singletons");
        GameFunction<void(__cdecl*)()>(0x004694E0)();
        Script::CScriptCache* scriptCache = Script::CreateScriptCache(true);
        uint8_t* module4088E0 = Create(0x004088E0);
        Create(0x0047BA60);
        Net::CreateManager(true);
        Obj::CreateCompositeObjectManager(true);
        GameFunction<void(__cdecl*)()>(0x004E4860)();
        Create(0x00589180);
        Create(0x00553DB0);
        Create(0x0057C0D0);
        Create(0x0057EED0);
        uint8_t* module5691A0 = Create(0x005691A0);
        auto* frontEnd = reinterpret_cast<uint8_t*>(Mdl::CreateFrontEnd(true));
        Create(0x0053F520);
        GameVariable<void*>(0x006FA7D0) = GameVariable<void*>(0x007CCE14); // (the network manager)
        uint8_t* module590160 = Create(0x00590160);
        Create(0x004ACAF0);
        SkateScript::CreateNodeManager(true);
        Create(0x00574540);
        Mem::PopMemProfile();

        Mem::PushMemProfile("Resgistering and starting modules");
        AddTask(mlp, 0x7C, *reinterpret_cast<void**>(mdl));
        AddTask(mlp, 0x7C, *reinterpret_cast<void**>(sio + 0x18));
        AddTask(mlp, 0x04, *reinterpret_cast<void**>(singleton44E7F0));
        RegisterModule(mdl, scriptCache);
        RegisterModule(mdl, module590160);
        RegisterModule(mdl, frontEnd);
        RegisterModule(mdl, module5691A0);
        RegisterModule(mdl, asyncLoader);
        RegisterModule(mdl, module4088E0);
        StartModule(mdl, scriptCache);
        StartModule(mdl, frontEnd);
        StartModule(mdl, module5691A0);
        StartModule(mdl, module590160);
        StartModule(mdl, asyncLoader);
        StartModule(mdl, module4088E0);
        Mem::PopMemProfile();
        Mem::PushMemProfile("sStartEngine");
        GameFunction<void(__cdecl*)(int)>(0x0048C1D0)(350); // start the engine
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
        UnregisterModule(mdl, module590160);
        UnregisterModule(mdl, asyncLoader);
        UnregisterModule(mdl, module4088E0);
        RemoveAllTasks(mlp, 0x40);
        RemoveAllTasks(mlp, 0x04);
        RemoveAllTasks(mlp, 0x7C);
        Release(0x00574910);
        SkateScript::ReleaseNodeManager();
        Release(0x004AACF0);
        Release(0x0058D290);
        Release(0x005395C0);
        Mdl::ReleaseFrontEnd();
        Release(0x00564C10);
        Release(0x0057FDB0);
        Release(0x0057C120);
        Release(0x00552F70);
        Release(0x005880E0);
        Obj::ReleaseCompositeObjectManager();
        Net::ReleaseManager();
        Release(0x0047C6C0);
        Release(0x00407980);
        Script::ReleaseScriptCache();
        File::DestroyAsyncLoaderModule();
        Release(0x0044E6B0);
        Mc::ReleaseManager();
        Sio::ReleaseManager();
        Release(0x0044F740);
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
