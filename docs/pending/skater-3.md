# Skater, third range (0x00510823-0x005197F2) - skater-3 agent (tag SK3)

The skater's small components after the core physics: SkaterEndRun, SkaterFinalizePhysics, SkaterFlipAndRotate,
SkaterFloatingName, SkaterGap, the rest of SkaterLocalNetLogic, SkaterLoopingSound, SkaterMatrixQueries,
SkaterNonLocalNetLogic, SkaterPhysicsControl, SkaterRotate, SkaterRunTimer, SkaterScore, SkaterSound,
SkaterStancePanel, SkaterState - plus the core physics' update (0x00510830). Source in src/game/skater/<component>.cpp
(one file per component); tests in src/hook/selftests_skater_<topic>.cpp (kit: src/hook/skater3_testkit.h - one
arena, recording stand-ins, ST0-returning stand-ins). Shared files touched: CMakeLists.txt (new files listed,
additive). Member function and checksum names come from the game's own .qb files (their checksum name tables).
Switch off: `[replacements] disable=end_run.cpp` (and the other files named below).

- [x] Batch SK3-1 (NOT run in the game yet; SkaterEndRun and SkaterFinalizePhysics, 19 functions):
  src/game/skater/end_run.cpp, finalize_physics.cpp (new).
  - SkaterEndRun (0xB1D5A3E1, vtable 0x0064BDB8, 0x1C bytes): 0x00510F90 Create, 0x00510D20 / 0x00510D40 the
    destructors, 0x00510D70 Update (outside a multiplayer game - Mdl::Skate 0x005651F0 - it suspends itself; a local
    skater whose run is over has the network manager tell the server once: 0x005332A0 / 0x00533250, message 0x59),
    0x00510DC0 CallMemberFunction (IsInEndOfRun, EndOfRunStarted, EndofRunDone, ClearEndofRunStarted,
    Goal_EndOfRunStarted, Goal_EndOfRunDone, RunStarted), 0x00510D30 GetFlags (into a pointer, returned),
    0x00510EA0 EndRun(force) (unforced, a game mode that doesn't end runs here - 0x00553550 - only sends RunHasEnded;
    otherwise once: the forced flag and ForceEndOfRun for a local skater), 0x00510F10 EndGoalRun (GoalHasEnded; its
    one argument isn't read), 0x00510F30-0x00510F80 the flag getters / clearers. Flag 0x20 is cleared by
    Goal_EndOfRunDone and RunStarted but never set in this component.
  - SkaterFinalizePhysics (0x9B373E58, vtable 0x0064BDEC, 0x20 bytes): 0x00511280 Create, 0x00511010 / 0x00511020,
    0x00511050 Finalize (core physics, state), 0x00511080 Update: the state's +0xF4 (out of a lip straight up: the
    core physics' +0x256 says the frame began in a lip, the skater is now in the air, |vx| and |vz| below 1), two
    physics vectors to the state, both matrices orthonormal again about up (the physics' replaced by its object's
    when 0x00405480 had to repair the object's), the display matrix, the sound components' terrain / state and the
    looping sound's speed fraction (|v| in x-z over Skater_Max_Max_Speed_Stat, computed in double, rounded once).
  - Skater states (the core physics' dispatch at 0x00510830): 0 ground, 1 air, 3 lip; 2, 4, 5 not named yet.
  - Test: TestSkaterEndRun / TestSkaterFinalizePhysics (selftests_skater_end_run.cpp): every function original vs
    ours on one arena, each callee stood in for and recorded (Suspend through a recording vtable, the matrix
    functions change the matrix so their order shows, 0x00580790 answers in ST0); 34 deliberate bugs caught, 1
    after strengthening (only the looping sound's state differing).
