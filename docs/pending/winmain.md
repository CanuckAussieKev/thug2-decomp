- [x] Batch (winmain agent; NOT run in the game yet): WinMain 0x004E2680 and ParseNetworkParams 0x004E1220 -
      src/game/sys/winmain.cpp (`Sys::WinMain`, `Sys::ParseNetworkParams` / `Sys::ReadNetworkParams`).
      - WinMain: GameSpy's back-end check ("thps6pc", waits 5 ms at a time; "The backend is not available"), PartyMod's
        initPatch, ParseNetworkParams, an already running copy (FindWindowExA by the title "THUG2") brought to the
        front (SW_RESTORE, BringWindowToTop, SetForegroundWindow; returns 0), PartyMod's createSDLWindow, the window
        events, no window -> 0, srand(time) on the game's C runtime, the command line split at spaces into at most 20
        arguments (argv[0] is the first word, not the program name), the CD ("THUG CD2"), DirectSound, the 72 default
        bindings (k0_/gp0_/gp1_ 0..23 through Sys::WriteDefaultSetting), Platform::InitInput with the settings key,
        the 256 key names from MapVirtualKeyA (acute accent -> ', ; -> :) and F1-F15, Sys::Main (called directly),
        then the original's shutdown (DirectSound, 0x004908C0, Direct3D, InvalidateRect, minimise, _cexit,
        Mem::ShutDownManager, _exit(0)) - never reached with PartyMod (Main's fast exit ends the process).
      - PartyMod, built in (each with the original in a comment): the 0x004E26A0 call into its cave 0x005480A1
        (LoadLibraryA("partymod.dll"), then on to GSIStartAvailableCheck) loads nothing - PartyMod is in our DLL and
        attached in DllMain; 0x004E26DC networkParamsWrapper -> initPatch() then ParseNetworkParams; 0x004E2767
        createSDLWindow in place of 0x005F5860 (the game's window: class/title string 0x0064B190, 100,100 640x480,
        WindowProc 0x004E16B0, icon 0x66, full screen); 0x004E277D ShowWindow left out; 0x004E2C16 the DirectInput
        release 0x005F39E0 left out. The title string 0x0064B190 is 25 characters in the original ("...nd 2", most
        likely "Tony Hawk's Underground 2"; the retail exe isn't here to check) - PartyMod's exe patch leaves "THUG2"
        and puts "partymod.dll" in the rest.
      - ParseNetworkParams: runs RunStageOnce(TestStage::Late) first (the Late stage, where the runner's hook ran it
        before), then options found in an upper-case copy, values read from the command line itself: -HOST (needs
        the trailing space) -> 0x007CC260; -HOSTNAME -> 0x0067EFBC [15] (default "Beenox"); -EMAIL -> 0x007CCA90
        [255]; -PPASS -> 0x007CCB90 [15]; unless hosting, -CONNECT (value from 9 on: one character after the option
        skipped) -> 0x007CC980 [255], inet_addr or gethostbyname -> 0x0067EFB8, joining 0x007CCA80; -MAXPLAYERS
        2..8 -> 0x0067EFD0; -GROUP -> 0x007CC264; -NTEAMS (>4 -> 0) -> 0x007CCBE0; -SKILLLEVEL (>4 -> 4) ->
        0x0067EFD4 (all unsigned: negative is too big); -NAME -> 0x007CCBC4 [15]; -PASSWORD -> 0x007CCBD4 [9],
        moved to 0x007CCDFC when joining. Values: spaces skipped, up to a space or between quotes.
        Differences (documented in the source): ours looks at the first 1023 characters (the original's strcpy into
        1024 bytes overran its stack); our atol is the UCRT's (VC7.1's wraps round on overflow, the UCRT's may
        not - numbers that overflow aren't tested).
      - Uses our C runtime (strstr, _strupr_s, atol, snprintf, mbstowcs_s) except srand/time/_cexit/_exit (the game's:
        its rand state and clean-up); ws2_32 linked for inet_addr/gethostbyname (CMakeLists.txt).
      Tests (src/hook/selftests_winmain.cpp):
      - TestParseNetworkParams (Early): 41 command lines (one with no space after each option) x hosting/joining set beforehand or not, from patterned
        globals; the game's _strupr/atol stood in by VC7.1's "C"-locale behaviour (independent of our CRT),
        inet_addr/gethostbyname by recording stand-ins (found, not found, address bytes with top bits set); every
        written global compared.
      - TestParseNetworkParamsLate (Late, in the game only - not run yet): the same cases without name lookups,
        the original on the game's own C runtime.
      - TestWinMain (Early): 12 cases (back end up / down / waiting, running already, no window, the window changed
        by the events, empty/space-led/19/20/21/25-word command lines) with every callee and Win32 import stood in
        by recording code (StandIns), PartyMod's patch bytes written into the original, the original's LoadLibraryA
        counted (once per case, "partymod.dll"; ours never); _exit's stand-in throws back to the test. Covers the key
        name renames (also with bits above the character), mbstowcs widening (VC7.1's behaviour expected - the UCRT
        matches), the window read again at each use.
      63 deliberate bugs: 59 caught (2 after strengthening: the -GROUP / -NTEAMS value offsets, caught once a command
      line has no space after the option), 4 equivalent (the spaces skipped before atol, which skips them too;
      the max-players clamp at > 7 -> 8; the window read into a local right before InitDirectSound).
      Checks: thug2_check 0 MISMATCHED, 83 early tests pass; register scan: only the known 0x004EFF92 alarm, WinMain
      returns int; call audit: no MISMATCH (0x005F3BF0 "?" - it tail-jumps to wcscpy, cdecl checked by hand);
      check_partymod_overlap: no overlaps, every WinMain patch handled.
      Fallback fix (src/hook/hook.cpp): a game-source replacement left out for a wrong argument size no longer
      keeps the runner's fallback at its address out, so that stage (and StopIfReplacementsFailed's message) still
      comes. Not testable offline (InstallReplacements doesn't run in thug2_check).
      Note on disabling: Sys::WinMain calls our ParseNetworkParams and Sys::Main directly (the repo's convention),
      so disabling only 0x004E1220 in thug2-decomp.ini doesn't bring back the original's parse while WinMain is
      ours; disable winmain.cpp for both (then the runner's fallback runs the Late stage as before).
      Shared changes: StandIns and its helpers moved from selftests_misc.cpp into src/hook/standins.h (namespace
      TestStandIns, header-only) for reuse; tools/check_partymod_overlap.py HANDLED gets 0x004E26DC, 0x004E2767,
      0x004E277D, 0x004E2C16; CMakeLists.txt: winmain.cpp, selftests_winmain.cpp, ws2_32.
      Left: the window procedure 0x004E16B0 (not a function in Ghidra, so find_gaps doesn't list it; unused with
      PartyMod's SDL window), 0x004E2C90 (91 bytes, no callers), the SingletonPtr methods 0x004E1970-0x004E1EE0.
