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
    number, sequenced in group 8, then +0x39 = +0x38); 0x00533300 SendObserverCount (0x26, +0x188, important, through
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
