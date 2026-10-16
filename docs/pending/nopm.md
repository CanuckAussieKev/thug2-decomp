# nopm: PartyMod moved into the game's source (src/partymod deleted)

- [x] PartyMod gone as a separate thing (2026-10-17, at the user's request; **NOT run in the game yet**; builds
      without warnings, thug2_check 0 MISMATCHED - 826 replacements, 15 not checked, none new -, early 98/98).
      Everything of PartyMod v1.0.4 the game still used is C++ in the game's source, by subsystem; each file with
      moved code says "Based on PartyMod (c) 2024 PARTYMANX, MIT - see LICENSES/partymod.txt" (the licence moved
      there from src/partymod/LICENSE; THIRD_PARTY_NOTICES.md and README updated). Same settings file
      (partymod.ini next to the exe), same sections, keys and defaults; same SDL2 2.30.9 (now linked to
      thug2_code; thug2_check still delay-loads it).
      Where it went:
      - src/game/sys/config.* (Config::): partymod.ini - Init, ExecutableDirectory, GetBool/GetInt/GetString.
      - src/game/sys/debug_log.* (DebugLog::): PartyMod's [Logging] log (Level, LogToFile, LogFile=partymod.log;
        console with [Miscellaneous] Debug=1). Kept separate from thug2-decomp.log so the settings work as before.
      - src/game/sys/sdl_events.* (Platform::InitEvents / RegisterEventHandler / PumpSdlEvents): the event pump
        (handlers in the same order: quit, window focus, input); std::vector instead of the stretchy buffer.
      - src/game/sys/sdl_window.* (Sys::CreateSdlWindow, Windowed(), WindowWidth(), WindowHeight()): the window
        (ResolutionX/Y, Windowed, Borderless; the max-resolution check; focus events -> 0x007CCBE4 / 0x00786AB4).
      - src/game/sys/pc_setup.* (Sys::InitPcSetup = initPatch, Sys::FastExit = fastExit): the start-up in the
        same order - config, log/console, script patches, events (+quit), srand(time) on our C runtime for the
        music shuffle, the online hosts, the glyph settings, the graphics settings. WinMain calls InitPcSetup and
        CreateSdlWindow as before; Platform::InitInput calls Nx::WriteGraphicsSettings and Sio::InitSdlInput.
      - src/game/sio/sdl_input.* (Sio::InitSdlInput = initManager, ProcessSdlDevice = processController,
        SetSdlActuators = set_actuators, UsingKeyboard(), TheInputSettings()): SDL controllers, players, the
        keyboard and its menu keys, typed text (Sio::KeyInput called directly), the DirectInput key table
        0x007D6770, the bindings. Sio::Device fields instead of PartyMod's struct; its four 32-bit stores
        (+0x14, +0xE0, +0xEC and +0xF0 - the last one 4 bytes past the end of the 0xF0-byte device) kept as
        32-bit stores (Store32), so the bytes written are the same.
      - src/game/sys/platform_cfuncs.* (Sys::CFunc_IsPS2, Sys::InstallIsPS2): see IsPS2 below.
      - src/game/gfx/settings.* (Nx::LoadGraphicsSettings, WriteGraphicsSettings, BlurDisabled()).
      - src/game/gfx/screen.cpp: Nx::CurrentLevel, DesiredAspectRatio; SetScreenAspectRatio /
        GetScreenAngleFactor / SetScreenBlur / SetLetterbox now contain PartyMod's code (they called it).
        Mlp's own CurrentLevel copy dropped (uses Nx::CurrentLevel); Mlp::RenderFrameLetterboxed is public (the
        main loop test writes a call to it into the original, as PartyMod's renderWorldWrapper).
      - src/game/gfx/render.cpp: Nx::SetDisplayRegion, Nx::Draw2DWithBlackBars (draw2DWrapper).
      - src/game/gfx/movie.cpp: Nx::SetMovieTexture, Nx::DrawMovieQuad (movie_setTexture / movie_drawPrim).
      - src/game/gfx/glyphs.* (Nx::LoadGlyphSettings, PreferGlyphs(), GlyphStyle(), ButtonForTag =
        dehexifyDigitWrapper, ShouldUseGlyph); font.cpp returns the style's names itself.
      - src/game/gfx/loading_bar.cpp: the 100000-byte gradient buffer (resbuffer), LoadingBarGradientBuffer().
      - src/game/mc/memcard_cfuncs.cpp: Mc::FreeDiskBlocks (calcAvailableSpaceFudged, fastcall as before).
      - src/game/net/online_hosts.* (Net::SetUpOnlineHosts, GamestatsHost()): see GameSpy below.
      - src/game/script/patched.* (Script::InitScriptPatches, LoadPatchedScript = pipLoadWrapper) and
        src/game/util/bps.* (Bps::Apply, PatchChecksum): the keyboard.qb BPS patch (its 380 bytes now an array
        in patched.cpp), with PartyMod's reuse of the patched copy on later loads.
      Deleted: src/partymod whole - patch.c/h (patch primitives), patches.c (incbin output, now the array),
      incbin, util/hash (map, only for the script patch list - now a vector), util/sb (event list - now a
      vector), cfunc.c (wrap_cfunc), networkParamsWrapper, partymod_attach (DllMain no longer calls anything),
      getModuleInfo / base_addr / mod_size (only for findPattern), every #ifndef THUG2_DECOMP block, UPSTREAM.md.
      Kept: src/proxy/dinput8.cpp still points THUG2PM.exe's "partymod.dll" name (0x0064B198) at dinput8.dll -
      the exe's own patch makes the *original* WinMain load partymod.dll, which still runs when WinMain's
      replacement is disabled in the ini; without the redirect the partymod.dll in the game folder would then
      patch the game. Comments reworded. thug2_decomp_before_exit stays (Sys::FastExit calls it).
      IsPS2: the CFunction table's IsPS2 entry (0x00681468: name 0x006524FC, function 0x0068146C) points at the
      shared 3-byte "return false" stub 0x005B9E50, which 12 other CFunctions share (IsNGC too) - it can't be
      REPLACEd for IsPS2 alone (and is already Stubs::ReturnFalse, source only). So the game's own
      Sys::CFunc_IsPS2 (false; with PS2 controls true for the vehicle grind script 0x47F15883 - CScript +0xC8 -,
      as PartyMod's isPs2Wrapper_DD) is put into the table entry by Sys::InstallIsPS2, from Sio::InitSdlInput
      under PS2 controls only (where wrap_cfunc did it; it checks the entry's name is "IsPS2" first). No REPLACE
      line (nothing to count). Early test TestIsPS2 (selftests_platform.cpp): through the table entry, original
      vs ours with PS2 controls off; on, against the wrapper's answers; the entry's neighbours unchanged. 12 of 12
      deliberate bugs caught (setting ignored/inverted, wrong checksum/field/null handling, not installed, wrong
      entry...). When the CFunction table becomes source, the entry simply names CFunc_IsPS2.
      GameSpy host names: src/game/net/online_hosts.cpp writes the same 14 addresses (12 code operands, the 2
      natneg data words) with hook's new WriteGameCode (hook.h) - commented "until the GameSpy SDK is linked,
      then this becomes its configuration". tools/check_code_writes.py (replaces check_partymod_overlap.py)
      checks those 14 writes and the IsPS2 entry stay outside every REPLACE'd function's body and first 16
      bytes: "No overlaps". tools/ghidra/partymod_patches.py retired (it listed src/partymod's patches).
      Tests: pointed at the moved functions (the original runs still get PartyMod's jumps/calls written in, now
      to our functions - so for those patched parts the comparison is our code against itself; the unpatched
      parts are still original vs ours). Two reference copies live in the tests instead, since the game has no
      separate function for them: PartyMod's reorder_flash_vertices (selftests_pc.cpp, the flash test's
      original run) and networkParamsWrapper (selftests_winmain.cpp: Sys::InitPcSetup then 0x004E1220).
      selftests_partymod.cpp renamed selftests_pc.cpp.
      Behaviour changes (all meant to be none in practice):
      - The "Input text '%s' > 1 byte!!" debug log line now gets its argument (PartyMod passed none: it printed
        stack garbage, at Level 3 only).
      - The window's `resolution_setting = 0` (PartyMod set its own pointer variable, not the setting) dropped.
      - Two calls in the park editor check are typed thiscall instead of PartyMod's fastcall with a
        null EDX (0x004AAE20 ignores ECX/EDX; 0x004637E0 doesn't read EDX) - same code.
      - Calls PartyMod made through game addresses that are now our own functions call ours directly: Pip::Load,
        Pip::GetFileSize, Crc::GenerateCRCFromString, Str::DehexifyDigit (REPLACE_SHADOW: its shadow check no
        longer sees these calls), Sio::KeyInput, Mth::Rnd (already so in ShuffleRandom). Same unless one of them
        is disabled in the ini.
      - The CRC-32 table for the BPS check is computed instead of listed (the standard one).
      - Kept as they were, deliberately: the window title "THUG2 - PARTYMOD", the log's "PARTYMOD for THUG2
        1.0.4" line, partymod.ini / partymod.log names, the out-of-range read in RemoveController.
      Checks: register scan - only the known 0x4EFF92 false alarm; call audit no MISMATCH; check_code_writes "No
      overlaps"; dinput8.dll imports SDL2.dll as before.
      **Check in the game** (the user):
      - [ ] Starts; thug2-decomp.log has no failures; with [Logging] LogToFile=1 partymod.log is written as before
            (with [Miscellaneous] Debug=1 a console opens).
      - [ ] Window: ResolutionX/Y honoured (and 0 = desktop size), Windowed=1, Borderless=1, full screen; alt-tab
            away and back (focus: the device comes back).
      - [ ] Keyboard: default binds and a custom [Keybinds] one; menus with Enter/Escape/arrows/E/R/1/2/-; typing a
            name on the on-screen keyboard (Enter, Backspace, Escape); chat F1.
      - [ ] Controller: joins as player 1 on a button press; a second controller rumbles and becomes player 2
            (split screen); unplug/replug; [Gamepad] binds and MovementStick/CameraStick.
      - [ ] PS2 controls (UsePS2Controls=1, the default): L1/R1 spin without delay, L2/R2 nollie/switch, acid
            drops, break vert, a vehicle grinds (IsPS2); and with UsePS2Controls=0 the Xbox layout.
      - [ ] Rumble (bails, the menu option).
      - [ ] Glyphs: keyboard -> key names; controller -> button glyphs; PreferGlyphs=1; GlyphStyle 0/1/2.
      - [ ] Letterbox/widescreen: front end 4:3 with black bars, levels full width; split screen not broken; the
            loading bar at high resolution; screen flashes; DisableBlur=0 blur.
      - [ ] Movies: intro/credits fitted 4:3 with bars; skipping with start/X.
      - [ ] Music: shuffle order differs between launches.
      - [ ] Online: the menu, the MOTD/news download, top stats (OpenSpy / OnlineDomain).
      - [ ] Memory card: free space shown, saving/loading.
      - [ ] Quitting: the process ends at once; thug2-decomp.log ends with the shadow summary and "quitting".
