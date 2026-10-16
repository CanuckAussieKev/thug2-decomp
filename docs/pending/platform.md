- [x] PartyMod round, platform (NOT run in the game yet; early tests pass offline): four functions PartyMod
      changes, with its changes built into the source (src/game/sys/platform.cpp, src/game/mc/memcard_cfuncs.cpp).
      - 0x5F3A50 Platform::SetActuators (source only; PartyMod jumps from its start): calls PartyMod's
        set_actuators(port, left, right) (SDL rumble on the controller playing as the port). The original's
        DirectInput force-feedback logic (ranges 0x7D42D0, one-motor kind 0x7D42C8, DIEFFECT parameters,
        magnitude * 10000) is kept in a comment. Sio::SetActuators (device.cpp) still calls 0x5F3A50 (which
        reaches PartyMod in the game): pointing it at Platform::SetActuators makes TestSio fail offline, since
        that test's original run uses the unpatched DirectInput version - TestSio would need PartyMod's jump
        written at 0x5F3A50 first.
      - 0x5F56E0 Platform::ProcessWindowEvents (source only; PartyMod jumps from its start): calls PartyMod's
        handleEvents (SDL event pump). The original's PeekMessage/Translate/Dispatch loop on the window
        0x7D6A28 is in a comment.
      - 0x5F44F0 Platform::InitInput(instance, window, config) (cdecl, bool): false without a window, true if
        DirectInput (0x7D69D0) is already set; else clears the ports' joystick/effect state, the key bindings
        (-1) and the graphics options, then PartyMod's writeConfigValues (in place of the settings loader
        0x5F34D0(config)), PartyMod's initManager (in place of DirectInput8Create + EnumDevices, which PartyMod
        NOPs), then the key-name table 0x5F3C10, and returns true.
      - 0x5A6E40 Mc::GetMemCardSpaceAvailable (CFunc): SpaceAvailable = 0 and FilesLeft = 1000000 into the
        calling script's params (CScript +0x14), then the card (0x7D1890's GetCard(0,0)); false without one;
        else SpaceAvailable = PartyMod's calcAvailableSpaceFudged(card) (clamped free blocks) in place of
        Card::GetFreeBlocks.
      Tests (src/hook/selftests_platform.cpp): each original runs with PartyMod's patch bytes written in for the
      test (jumps at 0x5F3A50 / 0x5F56E0, calls at 0x5F4591 / 0x5F45A2 with the 45-byte NOP, the call at
      0x5A6E7E), and PartyMod's functions, the struct adds, the card lookup and 0x5F3C10 are redirected (a jump
      over their first bytes, our own and the game's alike) to recording stand-ins. Rumble over 5 port/level
      cases (incl. values beyond 16 bits); input set-up with/without a window and DirectInput over dirty
      globals (the stand-ins record what's set up when they're called: order checked); the CFunc with and
      without a card. 17 deliberate bugs, all caught. Register scan, call audit and PartyMod overlap check
      clean. tools/check/main.cpp now defines an empty thug2_decomp_before_exit (main.c is linked into
      thug2_check for calcAvailableSpaceFudged).
      Left undone (stopped early on the coordinator's request - budget): 0x44FDC0 (Bink movie player, the
      movie-skip NOP at 0x450121), 0x452910 (music update; the shuffle PartyMod patches is really the code at
      0x452200, a separate function Ghidra merged into 0x452910's body - 0x452910 tail-jumps to it), 0x4E1EF0
      (the game's main: all singletons, MainLoop, teardown; fastExit at 0x4E2492), 0x5A8730
      (LoadFromMemoryCard; PartyMod NOPs the save checksum's JNZ at 0x5A8A01), 0x545880 (online/MOTD state
      machine, 18 states: wait 500 ms not 15000 at 0x545B4E, script 0x588ED87C not 0xD4E84C8D and
      [+0xFE09] = 1 in the state at 0x545CD0), 0x54DCB0 (RetrieveTopStats: sprintf "http://%s/thug2pc/%s"
      with PartyMod's "gamestats.<OnlineDomain>" in place of gamestats.gamespy.com - needs net.c to keep
      that string in a global; request struct 0x118 bytes; disassembly read, not written).
