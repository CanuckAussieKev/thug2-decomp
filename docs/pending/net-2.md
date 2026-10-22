# Online & network game modes, second part (0x0053A910-0x00541870) - NET2 agent

Branch worktree-agent-a25cc356b174d99a0, workspace C:\Games\THUG2-2. Source in src/game/net/gamenet_players.cpp,
gamenet_game_cfuncs.cpp (+ .h); tests in src/hook/selftests_net_*.cpp (kit: src/hook/net2_testkit.h on
gamenet_testkit.h - the manager's real player lists in the arena, hand-built script structures, recording vtables).
Shared files touched (small, additive): CMakeLists.txt (new files listed), src/game/net/gamenet_manager.h (member
declarations added before "Fields"), src/game/net/gamenet_join.cpp (its NextPlayerObjectId helper replaced by the
member).
Switch off: `[replacements] disable=gamenet_players.cpp` (and gamenet_game_cfuncs.cpp).

- [x] Batch NET2-1 (NOT run in the game yet; the players, the observer's camera, the host mode and the network game's
  script commands, 20 functions): src/game/net/gamenet_players.cpp, gamenet_game_cfuncs.cpp (+ .h) (new).
  - GameNet::Manager: 0x0053A910 ObservePlayer (the camera object 0x967C138C's SkaterCamera / WalkCamera: someone
    else's skater - skater camera on, walk camera suspended; the local skater - both pointed at it, the walk camera
    while the physics control's state says walking (+0x118 / +0x1C), else the skater camera and the walk camera
    reset; none or no skater - both cleared; m_camPlayer), 0x0053AA70 SendObserveRequest (message 0x4A unless the
    local player observes; no null check on the local player, as the original), 0x0053AAC0
    AssignFirstComeFirstServed (the first remote non-observer with flag 0x20: flag 0x200, message 0x5F with the
    "Checksum" of network preference 0x043EE978), 0x0053AC40 NextPlayerObjectId (lowest id 0-15 not used by a playing
    player's skater nor a joining player - a joining observer counts only while pending; 16), 0x0053AE10
    PlayerByConnection (observers too), 0x0053AEA0 PlayerByObjectId (16-bit id, observers and skater-less players
    skipped), 0x0053C320 SetHostMode (1 auto-serve: observe request, time 0x007CCDEC, the task +0xF4 started; 2 first
    come first served: observe request + AssignFirstComeFirstServed).
  - Script commands: 0x0053B450 PlayerTookFlag (message 0x71 to every remote player incl. observers, 0x00543F00 for
    the local one), 0x0053B5A0 PlayerCapturedFlag (skipped with flag 0x8000; 0x72; the local machine bumps the
    capturer's score counter 0x00561B40/0x00561A80 and calls 0x00543A90), 0x0053B710 PlayerRetrievedFlag - QUIRK KEPT:
    sent as a capture (0x72 / 0x00543A90, not 0x73 / 0x00544110), 0x0053B860 PlayerHasFlag (true for an unknown
    player), 0x0053B8A0 GetNumPlayersOnTeam, 0x0053B8F0 GetPlayersTeam (objId), 0x0053B940 ExitSurveyorMode,
    0x0053D400 SetHostMode, 0x0053D440 EnteredNetworkGame (qr2 set up when 0x007CCA82; tasks +0xD4, +0xC8; flags
    0x007CCDF8, +0x46), 0x0053D490 KickPlayer / 0x0053D5E0 BanPlayer (the player with flag 0x400: the server drops it
    (0x0053CB70 reason 4 / 3) and removes a non-observer's skater; a client sends 0x61 {0 kick / 1 ban, index in the
    list, name} - ban inside the network heap), 0x0053D760 CancelJoinServer, 0x0053D780 EnterSurveyorMode (the
    surveyor task 0x0053BED0 and an input handler 0x00536890 on controller 0, flag 0x10000).
  - Test (selftests_net_players.cpp TestNetPlayers): 8 cases, original vs ours on the same arena with real player /
    joining-player lists (the originals' inlined list walks run on them); every branch above, the camera's six
    situations, host mode sequences, kick / ban on server and client with none / two different marked players.
    44 deliberate bugs: 43 caught (5 after strengthening: an observer with 0x20, a pending joining observer, a missing
    "team", the 0x007CCDF8 order around task adds, the input device); 1 equivalent (GetInteger's assert flag - asserts
    are compiled out).
- [ ] Next: the network game's start / end (0x0053AF90, 0x0053B0D0), qr2 reporting (0x0053B9C0-0x0053BEC5), the
  join-state / auto-serve tasks, player removal (0x0053C6E0-0x0053D120, 0x0053D900).
