# The end of the skater range (0x00529F62-0x00533F20) - skater-6 agent

Branch worktree-agent-a79a8b861713be8f4. The range is not skater code past 0x0052D6A0: it is the end of the walk
component (0x00529F80-0x0052D6A0: its update, jumps, hanging, landing; class `Skater::WalkComponent`) and then the
network game manager's message handlers and small members (GameNet::Manager, 0x0052D6A0-0x00533F20). 114 functions in
Ghidra's list plus 36 Ghidra never made (handlers and callbacks reached only through their registration - found by the
INT3 padding before them and by the code addresses pushed as immediates; created in this agent's snapshot copy
C:\Games\THUG2-ghidra-snapshot-sk6). 0x00532050 (the chat handler) and 0x005334D0 (the keyboard task) were done on main.
Shared files touched: CMakeLists.txt (new files listed, additive); src/game/net/gamenet_manager.h (member declarations
added; main's field names - from the THUGPRO join merge - used where they exist: m_joinMode, m_hostMode, m_gameOver,
m_latestReadyQuery, m_readyToPlay, m_lastScoreUpdate, m_joinState, m_joinIp, m_joinPrivateIp, m_joinPort,
m_camPlayer, m_matchClient, ClientSlot; the other fields reached by offset).
Switch off: `[replacements] disable=gamenet_state.cpp` (and the later batches' files, listed with them).

- [x] Batch SK6-1 (NOT run in the game yet; GameNet::Manager's small members, 58 functions 0x00532AA0-0x005333F0):
  src/game/net/gamenet_state.cpp (new).
  - Flags and saved values: join mode +0x3C (SetJoinMode clears the join-by-address flag 0x007CCA88), +0x40, the
    server's public address / ports +0x19C-+0x1A4, the clients +0x08 / +0x0C (GetClient(i): +0x08 + 4i), the LAN
    lobby client +0x2C, the start info's eight words +0x48, the game number +0x38 and the one ready was sent for
    +0x39, every-player-ready +0x3A, cheating noted +0x3B, final scores received +0x44, draw names +0x47 (toggled),
    the crown and its spawn point (+0x6C, +0x68 = a signed byte, sign-extended), the last leader / leading team
    +0x1D0 / +0x1D4, +0x190, the join state +0x198, +0x1CC, +0x9950, the buffer +0xFDF8 / size +0xFDFC.
  - Network preferences (+0x104; taunt preferences +0x110): rank_test, rank_limit, skill_level, num_players,
    team_mode, num_observers, level, fireball_difficulty mapped from num_N / teams_N / rank_test_* checksums as
    the original's compare chains do (defaults 0 / 9 / 2 / 2 / 4 / 2 / - / 3); show_names == boolean_true. Quirk
    kept: when the preference has no `checksum` the original reads its uninitialised local - the slot ECX was
    pushed into, i.e. the manager's address (ShouldDrawPlayerNames initialises it to 0).
  - 0x00532FE0 IsUntimedGame: goal attack / king / net king / (net) score challenge always; else in a network game
    netctf with ctf_game_type != timed_ctf, or stop_at_zero = boolean_true.
  - Messages: 0x00532ED0 FindServersOnLan (message 0x11, 12 bytes - the time, then 8 bytes the original leaves
    uninitialised, zeroed here - through the lobby client to the address asked for at 0x007CCA84 once, else broadcast
    to port 5153 and to the command line's address 0x0067EFB8 unless -1); 0x00533250 SendGameEnded (0x59, the game
    number, sequenced in group 8, then +0x39 = +0x38); 0x00533300 SendReadyResponse (0x26, +0x188, important, through
    each client there is). 0x00532E30 removes the task at +0xF4 (a tail jump to Tsk::BaseTask::Remove).
  - The goal editor's created goals: 0x00533380 (0x004E7E50(0) > 0, the manager unused), 0x005333A0 (the level at
    +0x1D8 and +0x194C, 0x004EBB70(level, +0x1DC, 6000), size + 4 at +0x1948), 0x005333E0 / 0x005333F0.
  - 4-byte functions source-only (0x00532AC0, 0x00532AD0, 0x00532EB0, 0x00533070, 0x005331E0, 0x00533210,
    0x005332B0, 0x005332E0). Several are linker-folded with other classes' identical getters (0x00532EB0 has 32
    callers, 0x00532EA0 49, 0x00532F70 34): the replacements do the same thing for all of them.
  - Test: TestGameNetState (selftests_skater_gamenet.cpp) - four scenarios on a stand-in manager in an arena (every
    word told apart), network preferences built by hand as real structures (each value of each preference between
    two others, and the checksum-less miss), every untimed mode x network game x ctf type x stop_at_zero, the
    messages with the apps' sends, the clock, the task list, InNetGame and the goal editor stood in for; calls,
    results and the arena compared. 38 deliberate bugs: 36 caught at once, 1 after strengthening (the crown spawn
    point's sign: the second call overwrote the first), 1 equivalent (ShouldDrawPlayerNames' assert flag only
    decides whether the empty debug-message function is called).
- [x] Batch SK6-2 (NOT run in the game yet; the smaller message handlers and helpers, 30 functions):
  src/game/net/gamenet_handlers.cpp / .h (new). The handlers' message ids come from their registration (client:
  0x00534480, server: 0x0053C380); names of scripts and preferences from the game's own .qb checksum tables.
  - Client: 0x0052D7B0 (0x25: the acknowledgement id, +0x188 - sent back by 0x00533300 as 0x26), 0x0052D7D0 (0x26,
    server's too: the packet acked; the sender's player 0x005446C0 - its connection ready again if the id is the one
    it owes), 0x0052D800 (0x4D: ExitSurveyorMode, then observe 0x0053E650), 0x0052D830 (0x79: a remote connection's
    important queues emptied), the CTF flag messages 0x0052D850 / 0x0052D880 / 0x0052D8B0 / 0x0052D8E0 (0x74 stole,
    0x71 took, 0x72 captured, 0x73 retrieved: the player of object id msg[0], team msg[1] - signed bytes, the id
    pushed as 16 bits), 0x0052D910 / 0x0052D930 (launch_auto_server_notification, Kill_Team_Flags), 0x0052D950
    (0x3F: a panel message through 0x005376F0), 0x0052D980 (0x23: the object update message's length from its bits -
    8 present bits, then per skater a 16-bit time, 9 flags, 6 signed 16-bit fields, 4+6+1+1, 8, a signed 32),
    0x0052DAC0 (0x70 and 0x46: nothing), 0x0052DAD0 (0x7C: the CD key challenge answered as 0x7D, 33 bytes, from
    profile_password through the online service's 0x0054CA10), 0x0052DCA0 (0x7B: the connection's last
    communication msg[0] seconds on), 0x0052DCC0 (0x53: join state 6, 0x54 to the server, the skate module's
    handlers on each local client unless on the server), 0x0052E1A0 (0x42: 0x00537140(true), the task at +0xD0 on
    the logic tasks unless the app is local), 0x0052EB70 (0x6C, both: the park map, its CRC computed and dropped),
    0x0052ED80 (0x5A: every player ready; an observer of a goal attack gets create_rankings), 0x0052EDD0 (0x6A: trick
    objects reset, create_game_ended_dialog, 0x00537620), 0x0052F2D0 (0x69: the team count; the first one outside a
    team game runs ChooseTeamMessage and PrepareSkaterForMove on the local skater), 0x0052F370 (0x80: the skate
    module's checksum written into the received message at +4, sent back as 0x81).
  - Server: 0x0052EBB0 (0x90: in a team game the object word noted for the sender's team - five slots at +0x70 +
    0x14 * team; 4 for an unknown sender or a repeat), 0x0052F3D0 (0x7D: the CD key answer checked by the online
    service's 0x0054CA30 with the sender's skater's id - no null check, the original's), 0x0052F400 (0x8B: the rail
    editor's rails streamed as 0x88, 0x1132 bytes; 0x8A: the created goals as 0x87 if there are any).
  - 0x0052D6A0 GetCdKey (HKCU then HKLM Software\Activision\Tony Hawk's Underground 2 "key", keys created; quirk
    kept: a HKCU key without the value is never closed; differs: a 100-byte value without a terminator - the
    original's strcpy ran on into its stack, ours stops). The list searches' destructors 0x0052DB60 / 0x0052DB70 /
    0x0052F730 / 0x0052F760 (vtables 0x0064C828 / 0x0064C82C).
  - Test: TestGameNetHandlersSmall (selftests_skater_gamenet2.cpp, kit src/hook/gamenet_testkit.h) - six scenarios:
    every handler on a stand-in arena (known / unknown players, signed ids and teams, remote / local connections and
    apps, server / client, team games, observers, filled team slots), object update messages of 15 bit layouts,
    GetCdKey through stood-in registry functions on 7 paths, the destructors with a stood-in Free. 46 deliberate bugs
    caught (one needed a rerun: a transient build failure).
- [x] Batch SK6-3 (NOT run in the game yet; the client's game handlers, 12 functions): src/game/net/gamenet_game_handlers.cpp
  (new; shared helpers in gamenet_handler_util.h, gamenet_handlers.cpp moved onto them).
  - 0x0052DB80 (0x22 new player: a NewPlayerInfo filled from the message - name strncpy'd 15, object id, team as a
    signed byte, flags, words; the appearance into its profile unless flags & 5; announced and added 10 frames on when
    m_readyToPlay, else at once), 0x0052E870 (0x58 crown: the king stops being one; the drop announced to him / to
    everyone in a network game / to the other local player - found by the id `(skater id == 0)`, i.e. the other of
    two; then the crown moved to the msg[0]'th crown node (type 0xAF86421B), counting on across up to 49 node walks,
    and taken off), 0x005300C0 (0x5F: local flag 0x200, launch_fcfs_notification), 0x00530CB0 (0x75 flag returned:
    flag_returned Team / String0 team_<n>_name, the CTF panel and arrow), 0x00530E50 / 0x00530FF0 (0x88 / 0x87: the
    created rails (in the heap at the memory manager's +0x114, 0x0067F000 cleared around 0x004F7DF0) / goals
    (InitialiseCreatedGoals DoNotCreateGoalPeds) arrived; a checksum mismatch asks again (4) or, leaving for the main
    menu, cancels the join with show_timeout (5) - the two differ in the order of the cancel's steps, kept),
    0x00531950 (0x6D: goals selected, the menu spawned when msg[0] = 1), 0x00531A80 (0x68: a player's team and
    the announcements; warn_all_same_team), 0x00531C80 / 0x00531F10 (0x6E / 0x6F goal attack completed / started:
    the goal's virtuals +0x70 / +0x6C / +0x3C / +0x34 / +0x68, chains through +0x28, the goals left; null current
    goal dereferenced as the original does), 0x00532280 (0x8D eliminated; 0x8E nothing), 0x00532390 (0x4E observers
    full: CreateJoinRefusedDialog).
  - Test: TestGameNetGameHandlers (selftests_skater_gamenet3.cpp; kit gained InstallCommon - scripts, structures,
    memory, sprintf) - five scenarios. 44 deliberate bugs caught, 10 after strengthening (new player flags 1 / 4, the
    crown worn before each case, the fcfs flag recorded, distinct heaps at the memory manager's +0x110 / +0x114,
    foreign-only packets, a list holding only the player, the current goal being the completed one, message 0x8C,
    elimination id 0).
- [x] Batch SK6-4 (NOT run in the game yet; the server's handlers, 9 functions): src/game/net/gamenet_server_handlers.cpp
  (new). 0x0052E060 (0x4A observe request: 4 unknown / observing; full (remote, the observers 0x00539C70 at the
  num_observers preference) 0x4E; else out of the game (0x0053CB70 2, skater removed 0x00565790, flag 4, the camera
  moved: 0x0053A580, null if that is the same player, 0x0053A910) and 0x4D), 0x0052E9E0 (0x6B: park map 0x6C 15000
  bytes, rails 0x88, created goals 0x87 - streamed in place; with 'F' at msg[4] the level change 0x46 (bytes 5-7 the
  original leaves uninitialised - zeroed) and a ready query 0x25 (the time; 0x00544670 notes what the player owes),
  sent at once), 0x0052EC40 (0x59 game ended for a player - flag 0x100, 4 if again or another game; game over 0x5A
  to all once everyone's has), 0x0052EE10 (0x67 team change relayed as 0x68 in the lobby or after the game),
  0x0052EF40 / 0x0052F1A0 (0x6E / 0x6F goal attack: completed - once per team (goal virtual +0x50 per teammate) /
  per player, marked done (+0x6C), passed on; started - to teammates; the goal message's bytes 1-3 the original leaves
  uninitialised, zeroed; the started one sets no queue - the original's), 0x0052FCC0 (0x0B disconnect: a remote
  player removed - 5; a waiting new player dropped with its connection, its node deleted through its vtable - 5;
  else 2), 0x0052FDA0 (0x57 drop what's carried: flag 0x8000 cleared; CTF flag returned locally and 0x75 to remote
  players; king: Rnd(10) into +0x68 and 0x58 to all), 0x00532170 (0x81 checksum answer: word0 ^ 0xDEADFACE == word1,
  else notify_client_cheating once).
  - Test: TestGameNetServerHandlers (selftests_skater_gamenet4.cpp) - four scenarios. 45 deliberate bugs caught, 1
    after strengthening (an observer local player on the returned flag's team).
- [x] Batch SK6-5 (NOT run in the game yet; the session handlers, 5 big functions): src/game/net/gamenet_session_handlers.cpp
  (new). 0x0052DD40 (0x11 LAN search, 12 bytes: the 0xE0-byte answer 0x10 to the asker's address - counts and limits
  (a limit below its count raised to it), password, in a game, host mode, logged in to GameSpy on the internet, server
  name, level, mode, skill, the asker's time, the players' names, then the waiting new players' not yet players -
  their names used as sprintf formats; the original's uninitialised bytes zeroed), 0x005301A0 (0x60-0x66 from the
  host's own player: the twelve options into the network preferences - fireball's 0x00534FC0 looked up for nothing;
  the target score falling back to capture then time options (x1000); kick/ban by index and name (_stricmp);
  change_level; 0x00558960; end game 0x6A to remote players; the team count's preference and SetNumTeams),
  0x005311F0 (0x46 level change, see the header), 0x005313C0 (0x34 start game: the type info's checksum is read into
  one slot across the loop - a type without one compares the previous type's, the first the array's address; the
  goal script defaults to the skate module's address as in skate_net's twin), 0x00532460 (0x54 the game's state for
  a joiner: the 0x50-byte game info - bytes 5-7 and 0x4D-0x4F uninitialised in the original -, the created goals,
  the spawned scripts' list at +0x150 as 0x49 (bytes 13-15 uninitialised), the skate module's 0x0056B990, an
  observer's graffiti trick objects (0x5C, copied), the chosen goals 0x6D, the king 0x55, then 0x42; the local
  player only gets ready to play).
  - Test: TestGameNetSessionHandlers (selftests_skater_gamenet5.cpp) - five scenarios (the original's uninitialised
    bytes masked; a score component is always there - the original doesn't check). 57 deliberate bugs caught, 8
    after strengthening (logged in without internet play, new players with flags 0x14 / 4, a target score of 0, the
    game-over flag set before a level change, the level and change time recorded after each, the created park to
    itself). (Two needed a rerun: the compiler's program database failing to open - out\scripts\mut_build.cmd now
    deletes it and builds again.)
- [x] Batch SK6-6 (NOT run in the game yet; tasks, GameSpy callbacks and a missed handler, 8 functions):
  src/game/net/gamenet_tasks.cpp (new), 0x0052E760 in gamenet_game_handlers.cpp. 0x0052F4A0 level change task
  (at 0x007CCDE8: 0x00565A00 with the level, the king's crown off, no flags carried, the task at +0xEC removed),
  0x00533810 modem state task (Net::Manager's state shown when it changes; redial pause = state 8 for 5 s then
  ConnectToInternet, leaving its parameters undeleted as the original does), 0x00533B50 net metrics task ("(%d) Lag:
  %d Resends: %d Bps: %d %d" per remote connection of the client (unless local) and the server - both lists twice,
  the original's outer loop), 0x00533ED0 join timeout task (unsigned 60000 ms), the GameSpy qr2 callbacks 0x00533400
  (every player/team key "") / 0x00533420 (the key lists) / 0x005334B0 (debug output), 0x0052E760 (0x55 new king:
  missed by the first survey - find_gaps found it).
  - Tests: TestGameNetTasks (selftests_skater_gamenet6.cpp; 28 deliberate bugs caught, 1 after strengthening - a join
    time difference past 2^31), TestGameNetGameHandlers gained HandleNewKing (6 caught).
  - Range check: find_gaps shows nothing left in 0x0052D6A0-0x00533F20 (the gaps it lists from 0x00533F20 on are
    GameNet::Manager's own range, not this one).
