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
- [x] Batch SK3-2 (NOT run in the game yet; SkaterFlipAndRotate and SkaterFloatingName, 15 functions):
  src/game/skater/flip_and_rotate.cpp, floating_name.cpp (new).
  - SkaterFlipAndRotate (0x488AF07A, vtable 0x0064BE20, 0x28 bytes): 0x00511470 Create, 0x00511300 / 0x00511320,
    0x00511350 Finalize (core physics, model, animation blender), 0x00511600 CallMemberFunction (BoardIsRotated,
    BoardRotate [`Normal` or the other way], BoardRotateAfter, flip, FlipAndRotate, RotateNoFlip, FlipAfter,
    IsFlipAfterSet, RotateAfter, 0x72289A28 - name unknown: flip only if the core physics' 0x004FC440 says so;
    PlayAnim does the "after" work and returns 2 so the animation components still play it), 0x00511390
    SetBoardRotated (returns the old value; with `send`, a local player's server gets message 0x2A {object id, value};
    its third argument isn't read), 0x00511310 ClearAfterFlags (not a Ghidra function; called by the skater),
    0x00511420 ApplyFlipToAnims (the state component's flip byte passed to the animation blender's 0x00419C10 as it
    is), 0x005114F0 Flip (+0x28 toggled - the original's "changed" test is always true - and +0x2C the game clock),
    0x00511570 DoAfterAnim (flip, turn around 0x004FE120, board; the model's +0x1F4 cleared). BoardRotate and the
    board's after-work don't check the player for null (as the original).
  - SkaterFloatingName (0x125044E2, vtable 0x0064BE54, 0x1C bytes): 0x00511810 Create (the label id not set),
    0x00511AA0 DestroyBody (destroy_object_label with id; the structure's allocation inlined in the original),
    0x00511B80 Destroy, 0x00511780 InitFromStructure (skater_name_0..7 by object id - a jump table; other ids leave
    it), 0x00511870 Update (no network game or show_names off: suspended; the label found through the screen
    element manager; a skater not shown (CSkater +0x2C4) or hidden: destroy_object_label if there is one; else
    create_object_label or the label's SetProperties with id, text "\c<n><name>" - n = team + 2 in a team game,
    else object id + 2 - and pos3D 96 (this machine's player) or 120 above the skater).
  - Test: TestSkaterFlipAndRotate / TestSkaterFloatingName (selftests_skater_flip_name.cpp): every function and
    branch original vs ours; the network message's bytes, the structures' allocations (the pool, stood in), the
    original's sprintf (the game's C runtime isn't started offline) done by a stand-in. 34 deliberate bugs caught,
    1 after strengthening (Create's cleared flag: the test now dirties the memory Create gets).
- [x] Batch SK3-3 (NOT run in the game yet; SkaterGap, 24 functions): src/game/skater/gap.cpp (new).
  - SkaterGap (0x11B4C67F, vtable 0x0064BE90, 0x58 bytes): 0x00512C60 Create, 0x005122A0 Construct (two list heads,
    Lst::Head<TrickListEntry> 0x0064BEC4 / <TrickNameEntry> 0x0064BEC8), 0x00511F10 / 0x005122F0 the destructors,
    0x00511BB0 InitFromStructure (frame 0), 0x00511BC0 Finalize (core physics, balance trick, physics control,
    walk), 0x00512320 Update (frame + 1, CheckGaps), 0x00512CC0 CallMemberFunction (StartGap, EndGap, StartGapTrick
    - 0xFC4F2009, name found by its checksum -, CheckGapTricks, ClearGapTricks, DuplicateTrigger), 0x00511C80
    GetStateFlags, 0x00511F60 CheckGaps, 0x00512030 ClearGaps, 0x00512050 StartGapTrick, 0x00512140 CheckGapTricks,
    0x005121A0 ClearGapTricks (NotInSameFrame), 0x00512360 StartGap, 0x00512770 EndGap, the list node / head
    destructors 0x00511E50 / 0x00511E90 / 0x00512240 / 0x00511E70 / 0x00511ED0 / 0x00512270, and the trick
    component's helpers 0x00511C10 ClearPendingGaps / 0x00511C30 AwardPendingGaps (ProcessAllGapsReward once every
    level's gaps are got). 0x00511C10, 0x00511C30, 0x00512140, 0x005121A0 aren't Ghidra functions.
  - Gap flags (gap.h): the low half cancels the gap in a state (0x1 ground, 0x2 air, 0x4 rail, 0x8 wall, 0x10 lip,
    0x20 wallplant, 0x40 manual, 0x80 / 0x100 walk states 2 / 3, 0x200 skating, 0x400 walking, 0x800 driving), the
    high half requires it (ticked off when seen; EndGap fails a gap with any left). CarGap adds 0x600 (a driving-only
    gap); otherwise 0x800 unless 0xE3ED4ADE (name unknown) is given. StartGap checks at once unless the settings'
    +0x40 (0x007CE5D8) is Off and driving doesn't matter.
  - The 0x64-byte gap entries and 0x34-byte gap tricks are trick_lists.h's TrickListEntry / TrickNameEntry (their
    fields named by accessors in gap.cpp; 0x00578670 SetFlag marks a gap trick done - the same +0x2C).
  - Quirks kept: EndGap leaks the DoPerfectCheck2 results structure when the landing isn't perfect; after each gap
    it ends the walk begins again from the first gap (a Continue that starts a gap of the same GapID makes EndGap
    loop for ever - as the original); BoardRotate / the board's after-work don't check the player for null.
  - Test: TestSkaterGap (selftests_skater_gap.cpp): the state flags in 40 states; four rounds of gaps started
    (every trick kind and flag kind, duplicates, skipped), checked by updates, ended (failed requirements,
    Require_Perfect 0/1/2, text or not, score with a local skater or not, GapScript permanent / NetEnabled,
    Continue, an event killing the other gap); the gap tricks (done, other node, duplicates), DuplicateTrigger, the
    destructors, the helpers. Script structures are stand-ins answering from a table (skater3_testkit.h; the
    bool-assert getters that search themselves - 0x00476CB0 etc. - stood in as their int twins). 61 deliberate bugs
    caught, 13 after strengthening (the test's own flag values, a Construct stand-in that clears what 0x00578740
    does, three more rounds).
- [x] Batch SK3-4 (NOT run in the game yet; the rest of SkaterLocalNetLogic and SkaterLoopingSound, 20 functions):
  src/game/skater/local_net_logic.cpp (added to), looping_sound.cpp (new).
  - SkaterLocalNetLogic: 0x00513630 Create (the state byte and +0x35 0xFF so the first update sends them),
    0x00512E30 / 0x00512E40 the destructors, 0x00512E70 Finalize (the state and core physics components), 0x005136C0
    Update (SendUpdate in a multiplayer game with a local player - the network manager's 0x00537320), 0x005136F0
    HandleGraffitiSteal (message 0x4B's client handler, not a Ghidra function: for the network manager's run count
    +0x38 only - else kHandlerMsgDone; the local skater as victim runs GraffitiStealYou, as thief
    GraffitiStealOther, String0 = the other's name; the original clears the new structure right after Init).
  - SkaterLoopingSound (0xB5F1E9D2, vtable 0x0064BF38, 0x110 bytes): 0x00513AD0 Create, 0x00513860
    RegisterMemberFunctions (not a Ghidra function), 0x005138E0 Construct, 0x00513B30 / 0x00513FA0, 0x00513880
    InitFromStructure (the air pitch floor 45 / fall 18.33 a second, volume_mult), 0x005138B0 SetSpeedFraction (at
    most 1), 0x00513970 Finalize, 0x00513F60 Suspend, 0x005139E0 CallMemberFunction (SkaterLoopingSound_TurnOn /
    _TurnOff; PlayAnim turns it on for the walk-to-skate animation - the object's tags' animTags), 0x00513BB0 Update
    (0x00513BED, a Ghidra function made of its body, registered as source only): the sound by state - ground
    TABLE_WHEELROLL, air the entry at 0x0067F018, rail TABLE_GRIND / TABLE_SLIDE (none when the state's +0x70);
    stopped when it changes or the skater hasn't moved (the original stops it every frame the position is the same);
    in the air the playing sound's pitch falls; otherwise volume and pitch by the speed (half while the core
    physics' +0x2EC); played, or updated when the first channel or the pitch moved by more than 0.5. 0x00513AB0
    StopSound (the skater's), 0x00513990 the sVolume assignment emitted here.
  - Test: TestSkaterLocalNetLogic / TestSkaterLoopingSound (selftests_skater_sound_net.cpp). 52 deliberate bugs:
    50 caught (6 after strengthening: the walking skater on PlayAnim, the air pitch fall, the update's fields traced
    each frame, playing sounds with small volume / pitch changes); 2 equivalent (`fraction >= 1` clamps 1 to 1;
    `!(pos == last)` equals `pos != last`, NaN included).
- [x] Batch SK3-5 (NOT run in the game yet; SkaterMatrixQueries and SkaterPhysicsControl, 27 functions):
  src/game/skater/matrix_queries.cpp, physics_control.cpp, skater_x87.h (new: the x87's FSIN / FCOS / FPTAN for the
  skater's code).
  - SkaterMatrixQueries (0xBF518E27, vtable 0x0064BF70, 0x60 bytes): 0x00514430 Create, 0x00513FD0 / 0x00513FE0,
    0x00514010 Finalize, 0x00514030 Update (the core physics' matrix copied), 0x00514040 CallMemberFunction (not a
    Ghidra function): GetHeading (heading / cosine / sine; FastACos of the at row's x, 360 - it for a negative z),
    GetSlope (Slope, ChangeInSlope; GetAngle of the at row and its flattened copy, negative pointing down),
    AbsolutePitchGreaterThan, PitchGreaterThan (against the core physics' +0x78), RollGreaterThan, YawingLeft,
    YawingRight, YawBetween (a pair: beyond the first, not beyond the second) - cosines by the x87's FCOS.
  - SkaterPhysicsControl (0xD9151D4D, vtable 0x0064BFE0, 0x30 bytes): 0x00517830 Create, 0x005176A0
    RegisterMemberFunctions (the table 0x006846F0), 0x005176C0 / 0x00517740, 0x005176D0 InitFromStructure,
    0x00517770 Finalize (the state's physics mode -1), 0x00517AC0 Update, 0x00517C80 CallMemberFunction
    (SkaterPhysicsControl_SwitchWalkingToSkating / _SwitchSkatingToWalking), 0x005178B0 SetPaused, 0x00517940
    SwitchSkatingToWalking, 0x00517B20 SwitchWalkingToSkating, and the script member functions PausePhysics
    0x00517A80, UnPausePhysics 0x00517AA0, GetTimeSincePhysicsSwitch 0x005177A0 / GetPreviousPhysicsStateDuration
    0x005177F0 (ms as unsigned, times 0.001), SetBoardMissing 0x005176E0 / UnsetBoardMissing 0x005176F0 /
    IsBoardMissing 0x00517700, SetForceWalk 0x00517710 / UnSetForceWalk 0x00517720 (with the board missing flag) /
    IsForceWalkOn 0x00517730 (the table's names matched to the functions in table order). Calls into the walk,
    camera and core physics code stay GameFunction (0x004FDD50, 0x0050C670, 0x00519680, 0x0052AF40, 0x005260C0,
    0x004FA420 / 0x00524FF0, 0x00525060 / 0x004F9A40).
  - Tests: TestSkaterMatrixQueries (selftests_skater_queries.cpp), TestSkaterPhysicsControl
    (selftests_skater_physics_control.cpp; the original passes bools with stale upper bytes - the stand-ins
    record the low byte). 22 + 27 deliberate bugs caught, 2 after strengthening (an exact cosine for
    AbsolutePitchGreaterThan; InitFromStructure's flag traced).
