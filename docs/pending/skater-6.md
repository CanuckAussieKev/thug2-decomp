# The end of the skater range (0x00529F62-0x00533F20) - skater-6 agent

Branch worktree-agent-a79a8b861713be8f4. The range is not skater code past 0x0052D6A0: it is the end of the walk
component (0x00529F80-0x0052D6A0: its update, jumps, hanging, landing; class `Skater::WalkComponent`) and then the
network game manager's message handlers and small members (GameNet::Manager, 0x0052D6A0-0x00533F20). 114 functions in
Ghidra's list plus 36 Ghidra never made (handlers and callbacks reached only through their registration - found by the
INT3 padding before them and by the code addresses pushed as immediates; created in this agent's snapshot copy
C:\Games\THUG2-ghidra-snapshot-sk6). 0x00532050 (the chat handler) and 0x005334D0 (the keyboard task) were done on main.
Shared files touched: CMakeLists.txt (new files listed, additive); src/game/net/gamenet_manager.h (member declarations
added; the placeholder m_unknownC split into m_client2 +0x0C, m_unknown10, m_matchClient +0x2C, m_gameMode +0x30 -
nothing used m_unknownC).
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
    to port 5153 and to the command line's address 0x0067EFB8 unless -1); 0x00533250 SendReady (0x59, the game
    number, sequenced in group 8, then +0x39 = +0x38); 0x00533300 SendAck (0x26, +0x188, important, through
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
