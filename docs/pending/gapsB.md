- [x] Batch gapsB (gaps next to Main / MainLoop / WinMain; NOT run in the game yet; branch wt/gapsB): 63 REPLACE lines
      (2 source only).
      - SingletonPtr (src/game/core/singleton.h / .cpp, namespace Spt): Neversoft's `Spt::SingletonPtr<T>` - a vtable and
        the instance; constructed with T's Create(create) (a reference, the instance made with `create`), destroyed
        with T's Release. The game's are locals (Sys::Main, the movie player, the network code) with inline
        constructors; out of line are, per instance, the 11-byte destructor (`MOV [ECX],vtable; JMP Release`, called
        from the exception-unwind funclets) and the 33-byte scalar deleting destructor (the vtable's only entry).
        All 24 instances in the exe (found by scanning for that destructor shape against the known Release functions -
        no others): 0x004E1980-0x004E1AF0 / 0x004E1B00-0x004E1EC0 (vtables 0x0064AFB0-0x0064B000, 21 instances: Pre,
        0x00463730, Mdl, Mc, 0x0044E7F0, the async loader, the script cache (Script::CreateScriptCache / ReleaseScriptCache from main's cache batch),
        and 14 more of Main's game singletons), 0x0044FD40/0x0044FD60
        (the sound manager 0x0047BA60, vtable 0x006472B4), 0x0044FD50/0x0044FD90 (Sio, 0x006472B8), and
        0x004B2230/0x004B2240 (Mlp, 0x0064A2EC). No identical-code folding among them (each has its own vtable);
        callers are only the unwind funclets and each scalar destructor. The one out-of-line constructor 0x004E1A90
        (not a function in Ghidra; 0x004E1A90-0x004E1AB4, called by 0x004F75F0) is SingletonPtr for 0x00590160's
        singleton; it builds the instance pointer through 0x004E1970 - a store-and-return-this constructor the linker
        folded with other one-pointer classes' (17 calls in the network code 0x00597740-0x0059D410): written as
        `Spt::PointerMember::Construct`. Each instance is a Singleton type (Type, kVtable, Create, Release); those
        without source are `GameSingleton<create, release, vtable>`, named by the Create address (Main's naming)
        except Mdl::Manager and the sound manager.
      - Mlp::Manager (src/game/sys/mlp.*): Construct 0x0044EDC0 (the three task lists 0x00406E80, +0xB8/+0xBC and m_done
        cleared; the EH frame only unwinds a half-built manager), DestroyBody 0x0044EE30 (task lists 0x004070E0, the
        last first), ResetDeviceIfLost 0x0044EE90 (static: uses no manager; the device-lost reset that MainLoop has
        inline - MainLoop now calls it; also source only at its jump 0x0044F250, which PlayMovieFile called - it now
        calls ours directly), ProfileFrames 0x0044F220 (CFunc ProfileTasks: +0xC4 frames, +0xC8 cleared; EAX left as
        the argument, nobody reads it), StopLoop 0x0044F240 (CFunc ResetEngine: m_done = true; 4 bytes, source only),
        Mlp::CreateManager 0x0044F260 / ReleaseManager 0x0044F2B0 (the singleton 0x00696D68, references 0x00696D6C,
        unsigned; Calloc(0xCC, current heap, 1)). Sys::Main calls CreateManager/ReleaseManager directly now.
      - Sys::WindowProc 0x004E16B0 (src/game/sys/winmain.*): bounds 0x004E16B0-0x004E180A (code to 0x004E17EB, then its
        jump table 0x004E17EC (4 targets) and index table 0x004E17FC (15 bytes, messages 2..0x10)). WM_DESTROY/WM_CLOSE:
        PostQuitMessage(0) + quit flag; WM_ACTIVATE (LOWORD 1/2): ShowWindow(game window 0x007D6A28, SW_MAXIMIZE),
        active; else SW_MINIMIZE, inactive, device lost, InvalidateRect(0, 0, TRUE); WM_PAINT: ValidateRect;
        WM_KEYDOWN -> Sio::SpecialKeyInput, WM_CHAR -> Sio::KeyInput; WM_SYSKEYDOWN F10 and WM_SYSCOMMAND SC_KEYMENU
        (whole wParam compared) and WM_POWERBROADCAST swallowed (0); the rest DefWindowProcA (a tail jump). Unused with
        PartyMod's SDL window; hooked anyway (harmless).
      - 0x004E2C90 turned out to be a component's InitFromStructure: Obj::CFlipRotateComponent (src/game/obj/flip_rotate.*),
        the factory 0x00481FB0's type 0xDF5C091A = "FlipRotate" (checksums matched by CRC: 0x0C7A712C "flipped",
        0x6EA3704A "rotated"). Written with its small neighbours: Construct 0x004E2C50, DestroyBody 0x004E2C80, Destroy
        0x004E2CF0, InitFromStructure 0x004E2C90 (base component 0x00481710/0x00481730/0x00481740 via GameFunction).
      Tests (src/hook/selftests_gapsB.cpp, Early): TestSingletonPtrs (all 24 destructors and scalar destructors with
      flags 0/1/2/3/0xFFFFFFFE, the constructor 0x004E1A90 with create 0/1/0x100/0xFF, 0x004E1970; Release, Create
      0x00590160 and Mem::Manager::Free stood in, ours found through the replacement table), TestMlpManager (Construct
      and DestroyBody on dirty memory, ProfileTasks, ResetEngine, the device reset in 6 cases through 0x0044EE90 and
      0x0044F250 with a stand-in device; Create/Release through 15 steps: none, out of memory, made, again, the last
      reference, no instance, a count over 2^31, the count wrapping), TestWindowProc (27 messages x 11 wParams, every
      Win32 call stood in through the import slots and the API), TestFlipRotateComponent (constructor, 75 parameter
      combinations, destructors).
      Mutations: 70 deliberate bugs, all caught at first (each by the test for its function; the device-reset ones also by
      TestMainLoop, which calls ResetDeviceIfLost now; movie_reset by TestPlayMovieFile hanging). None equivalent.
      Checks: thug2_check 0 MISMATCHED, early 91/91 (after merging main); register scan only the known 0x004EFF92 alarm (FlipRotate's
      Construct returns this); call audit no MISMATCH (singleton.cpp's GameFunction calls take template addresses the
      audit can't read: all 38 Create/Release functions checked by hand - plain RET, cdecl); check_partymod_overlap: no
      overlaps, nothing to handle.
      Shared files changed: src/game/sys/main.cpp (Mlp::CreateManager/ReleaseManager called directly; merged with main's script-cache calls),
      src/game/gfx/movie.cpp (calls Mlp::Manager::ResetDeviceIfLost in place of GameFunction 0x0044F250),
      src/hook/selftests_misc.cpp (TestMain stands in for our Mlp create/release too), src/hook/selftests_partymod.cpp
      (TestPlayMovieFile stands in for our ResetDeviceIfLost too), src/game/sys/mlp.cpp (MainLoop calls
      ResetDeviceIfLost), CMakeLists.txt (singleton.cpp, flip_rotate.cpp, selftests_gapsB.cpp).
      Left (find_gaps, new neighbours): Mdl::Manager 0x0044F300-0x0044F7E0 (17 functions, the module manager, next to
      0x0044F2B0); 0x004B2270-0x004B2320 (next to the Mlp SingletonPtr); FlipRotate's Update 0x004E2D20 (1343 bytes).
