# Skater subsystem, range 0x005197F2-0x00521B90 - skater-4 agent (tag SK4)

Branch worktree-agent-a58a600c70e03fa51, workspace C:\Games\THUG2-n (when the lock allows). The range's classes: the
skater's state history component and its network collisions, the vehicle sound component, Skitch, SpecialItem, the
stats manager, Tagging and TaggingControl, and the first part of the Trick component. Source in
src/game/skater/<class>.cpp + .h; tests in src/hook/selftests_skater_<class>.cpp. Shared files touched: CMakeLists.txt
(new files listed, additive).

- [x] Batch SK4-1 (NOT run in the game yet; the skater state history component and its network collisions, 15
  functions): src/game/skater/state_history.cpp (new).
  - SkaterStateHistory (type 0x18223FD6, vtable 0x0064C174, 0x1108 bytes): 0x0051AD30 Create, 0x0051ACB0 Construct
    (both 20-entry arrays cleared - the states at +0x1C as skate_net.cpp reads them, the other messages at +0xA70,
    0x54 bytes each, their time stamp at +4 / +6: 0x00519800 GetTime / 0x00519810 SetTime), 0x00519840 Destroy
    (the destructor body 0x005197C0 is the neighbouring range's).
  - Lst::Search<PlayerInfo>'s destructors (vtable 0x0064C1A8, 80 callers of the body): 0x00519830 / 0x0051A120.
  - The collision check 0x0051A1C0 (the skate module's 0x00566BE0 calls it for every pair of skaters in a network
    game): each skater's frame of movement (0x00519FA0: velocity times the frame length - w from the unrounded
    length) ahead of its position (0x00519FF0) as a segment of length 0x0051A020 (script float 0x25F78481, or
    0x0D24273B when the object's +0xBC is set; times 1 + 0x0A49FF23 while driving), a hit when the segments come
    within 0x0051A080's radius (one of four script floats by +0xBC and the network manager's flag 0x4, times 1 +
    0xEEF4AFB3 per driver) - the segments' closest points, else the first of the four pairs of ends. Players that
    may hit each other: not observing, not "it" (+0x1D0 bit 0x80), teams (bits 0x7800) and a team game's team
    field (+0x30). The faster skater hits (a driver beats one on foot); otherwise only "it" does. A hit: message 0x43
    (0x14 bytes: the other's id | driving << 7, the push) and 0x44 (1 byte) on the sequenced queue, group 8; a slap
    game adds a point to the other's score; a game of tag (0x6EF8FDA0 / 0x5D32129C) makes the other "it" and tells
    everyone (0x55); team tag (0x6C5FF266) tags or converts them (0x73 / 0x74, or 0x00544110 / 0x00543C70 for
    local players). The original looks the string "driving_radius_boost" up for each driver and drops the value.
  - 0x0051A150 IsNearLocalSkater (within 96 of the network manager's skater, +0x6C).
  - Message handlers on the skater's client app: 0x00519870 (0x84 hit by another: knocked back away from it,
    300 * 1.5 horizontally and 200 up, into a bail on foot or driving, the score lowered by the message's amount -
    to 0 at most, then unbind_fireball_tricks; 0x85 hit another; each an event 0x915E5E39 / 0xA1021AF0 with the
    other's name, "Someone" without one) and 0x00519C10 (0x43 / 0x44 the same for running into each other, the push
    as a vector in the event; bit 7 of the id byte - driving - is cleared in the message itself).
  - Quirks kept: no null check on the other players' PlayerInfo (the original would crash too); the hit name is a
    strncpy into 32 bytes - a 32-character name isn't terminated (the event reads on into the stack in both).
  - Test (src/hook/selftests_skater_state_history.cpp; every call recorded by stand-ins, hook/standins.h - the
    original's call to 0x00402F00 goes to our ClosestPointsBetweenLines, which works in floats and is tested on its
    own): the small methods over 300 random worlds (and the unrounded reach / radius), 4000 random collision
    worlds (teams, flags, game types, observers, slap, parallel and collinear movement for the end pairs; ~2500
    messages sent), 3000 random hit / collision messages. 64 deliberate bugs caught, 6 of them after strengthening
    the tests (w of the velocity and positions, collinear cases, a score loss equal to the score, 32-character
    names).
- [x] Batch SK4-2 (NOT run in the game yet; the skater vehicle sound component, 16 functions):
  src/game/skater/vehicle_sound.cpp (new).
  - SkaterVehicleSound (type 0x1859F772, vtable 0x0064C1E4, 0xC8 bytes): 0x0051B880 Create, 0x0051AD90
    RegisterMemberFunctions (not a Ghidra function: the table at 0x00684648), 0x0051ADD0 Construct, 0x0051ADB0 /
    0x0051AE30 the destructors, 0x0051AE60 InitFromStructure (the park editor's level: its +0x40 isn't "off"; the
    params aren't read), 0x0051B8E0 Finalize (core physics, sound, input), 0x0051B920 Update (while driving - the core
    physics' +0x2ED -, not silenced, with settings), 0x0051B670 ReadSettings (the structure named by +0xC0 in the
    global 0x3C2DA7F5: the engine and skid sounds, up to 6 gears of 4 floats, +0x34; the park editor's sounds from the
    global 0x38EC37AC).
  - 0x0051AE80 UpdateEngine: the speed in mph (|vel| * 3 * 1/17.6, times 0xEC67BC97 unless braking with +0x1C0
    set) eased by 0x02A7095B, each step limited by 0xCA5EFD91 / 0x77C8E7C8 (as the original compares them, the limit
    only applies when it points the wrong way - kept); the gear back to the first 0xC0F451D4 ms after driving stopped
    (the core physics' +0x380), else shifted at the gear's points; just after a shift the engine is quieter
    (0xBFA32092 of full, back to full over 0xFB6BD20E ms); the engine's speed between the gear's pitch range, eased
    by 0x3DD9C040; its pitch and volume from where it is between the settings' limits; played or updated.
  - 0x0051B3E0 UpdateSkid (the level follows |vel| * 3 while braking; a silent volume stops it), 0x0051B3B0 (reads
    0x71B4B9FE and the clock, nothing else).
  - Member functions: 0x0051B7C0 SetSkaterVehicleSounds, 0x0051B820 NoSkaterVehicleSounds, 0x0051B860
    StopSkaterVehicleSounds (not a Ghidra function).
  - 0x0051ADC0: the shared CallMemberFunction of components without their own (returns 2) - ComponentWithoutMemberFunctions.
  - Test (src/hook/selftests_skater_vehicle_sound.cpp): 6000 random components (the sound manager's and the script
    structures' calls stood in for, sVolume's methods real), each method; edge cases (a speed that rounds to 0 from
    just below, a shift point at exactly the speed, an engine just over its range). 46 deliberate bugs: 45 caught (4
    after adding the edge cases), 1 equivalent (the shift recovery capped at exactly 1 gives 1 either way).
- [x] Batch SK4-3 (NOT run in the game yet; the skitch and special item components, 15 functions):
  src/game/skater/skitch.cpp, special_item.cpp (new).
  - Skitch (type 0x3506CE64, vtable 0x0064C218, 0x20 bytes): 0x0051BA10 Create (allowed), 0x0051B960 / 0x0051B980
    destructors, 0x0051B9B0 InitFromStructure (the object hook manager, 0x27443708), 0x0051B9D0 CallMemberFunction
    (Obj_AllowSkitching 0xC38CCE3B: allowed unless "off"; Ghidra splits the function at 0x0051B9D8 - no separate
    entry), 0x0051BA70 GetNearestHookPoint (each hook node through the car's matrix and position, its height above
    the car dropped - projected onto the car's up row -, the nearest by squared distance; its index, -1 without
    nodes) and 0x0051BC70 GetHookPoint(index) (true also when there's no such point).
  - 0x0051B970: the shared Update of components with nothing to update (suspends itself) - ComponentWithoutUpdate.
  - SpecialItem (type 0xF27A294D, vtable 0x0064C250, 0x20 bytes, two item objects at +0x18): 0x0051BD80 Create,
    0x0051BF20 / 0x0051C130 destructors (both items destroyed first), 0x0051BE00 DestroyItem (the item's tag
    0x40764820 script run on it, then marked dead through its vtable), 0x0051BF00 DestroyItems, 0x0051BF80
    CreateItem (a game object from a global structure - its id, if any, offset by the object's id and WRITTEN BACK
    into that global structure, as the original does -, tagged with the owner, in the owner's model colour via
    0x00492EE0), 0x0051C160 CallMemberFunction (CreateSpecialItem: index, bone, params; its lock component given the
    params and the owner's id; message 0x77 to the server in a network game: params, bone, owner id, slot;
    DestroySpecialItem: message 0x78 - the original sends 8 uninitialised bytes before the id and slot -,
    DestroyAllSpecialItems, SpecialItemExists).
  - Fix outside the range: Obj::CreateGameObjectFromNode (0x00468EB0, node_objects.cpp) now returns the object it
    makes - the original returns it and CreateItem reads it (scan_register_assumptions had listed the site).
  - Test (src/hook/selftests_skater_skitch_items.cpp; stand-ins for the script structures, objects, network; fake
    vtables for the items, locks, hook manager): 5000 random cases over every function; exact whole-number hook
    cases for ties. 33 deliberate bugs: 32 caught (1 after adding the exact ties), 1 not observable (the order of
    the squared distance's sum: rounding only, against a float-rounded best).
- [x] Batch SK4-4 (NOT run in the game yet; the stats manager component - the story mode's stat challenges, 26
  functions): src/game/skater/stats_manager.cpp (new).
  - StatsManager (type 0x4FF8DAC6, vtable 0x0064C294, 0xAA0 bytes; 70 goals of 0x24 bytes from +0x1C): 0x0051CB90
    Create, 0x0051C440 RegisterMemberFunctions (not a Ghidra function: the table 0x00684180), 0x0051C5A0 Construct,
    0x0051C460 / 0x0051C640 destructors, 0x0051C470 / 0x0051C480 goal status get / set, 0x0051C670
    InitFromStructure (the skater by the object's id, the difficulty, the goals from the global array 0x4E46CC28 -
    a goal's status is only reset when its structure has 0xBD982FDC), 0x0051C950 IsActive (a skater, the game
    mode's flag 0x1DED1EA4, activated, not mode 0xE92ECAFE, the first profile named 0x0A7BE964 - without a name the
    original compares its own `this`, kept), 0x0051C9D0 / 0x0051CAC0 / 0x0051CAE0 setters and counters (the +0xA58
    value is a float: Update compares it as one), 0x0051CA00 Reset, 0x0051CBF0 AnnounceMetGoals, 0x0051CE80
    MeetGoal, 0x0051CF90 Update (the combo's score / multiplier truncated as _ftol does; take-offs (the skater
    state's +0x30) and jumps (physics state 1) followed: height, distance, the score made in the air; 13 goal kinds
    checked - rise, drop, air combo, height, +0xA58, score, multiplier, two counters, trick counts (manuals with
    0x89B7DBE4 too), jump / air distance (horizontal; the air distance zeroes the stored y's, as the original does),
    0x00562370 of the goal's +0x18), 0x0051D6A0 CheckBalanceTime (manual / grind / lip time goals).
  - Member functions: StatsManager_ReInit (vtable +0x14), _Reset, _ActivateGoals, _DeactivateGoals, _GetGoalStatus,
    _UnlockAmGoals, _UnlockProGoals, _GetLastAirComboScore, _ClearLastAirComboScore (0x0051C4A0-0x0051C580,
    0x0051CB20-0x0051CB70; most not Ghidra functions).
  - Test (src/hook/selftests_skater_stats.cpp): 3000 random components with runs of 1-6 frames, balance times,
    announcements, init, the setters and member functions; whole-feet cases for ties. 55 deliberate bugs: 54 caught
    (8 after strengthening: ties in rises / heights / air scores, the counters traced between calls), 1 equivalent
    (the points with a zero multiplier: the score is 0 then too).
  - Seen once in a full thug2_check run: "sfx: loading waves" (selftests_objsys9.cpp) failed, then passed alone and
    in the next full run - order-dependent or flaky, not this batch's code.
- [x] Batch SK4-5 (NOT run in the game yet; graffiti tags and stickers - the tagging and tagging control components, 18
  functions): src/game/skater/tagging.cpp (new).
  - Tagging (type 0x5B512EDC, vtable 0x0064C330, 0x24 bytes): 0x0051DA80 Create, 0x0051D760 / 0x0051D860 destructors
    (both textures given back), 0x0051D890 InitFromStructure (copies of tags\cas_01 and tags\sticker keyed by
    "_<skater number>" - without 0xA2B033FD the number is the params pointer, as in the original), 0x0051DB00
    CallMemberFunction (SetGraffitiTexture, SetStickerTexture, HasCustomTagTexture, ApplyCustomTagTexture),
    0x0051D830 ApplyCustomTagTexture (0xAA286543), 0x0051D850 HasCustomTagTexture (4 bytes: REPLACE_SOURCE_ONLY),
    0x0051D9C0 / 0x0051DA20 SetGraffitiTexture / SetStickerTexture, 0x0051DBC0 PlaceTag (the decal 0x004928A0; a
    sticker turned by Rnd(90) - 45 degrees; message 0x99 to the server, 0x28 bytes, group 10 - bytes 2-3 are stack
    garbage in the original).
  - TaggingControl (type 0x380D909A, vtable 0x0064C394, 0x38 bytes): 0x0051E280 Create, 0x0051DD50 / 0x0051DD60
    destructors, 0x0051DD90 Finalize, 0x0051E300 CallMemberFunction (TagNow, VertStickerSlap, TagClearFlags,
    BlockTaggingOn / Off - the names' checksums confirmed with out\scripts\qbkey.py), 0x0051DDF0 SlapSticker (a
    feeler along the facing: the velocity, the walk heading or the up row reversed; a sticker is marked first, placed
    the next time), 0x0051E390 Update (and 0x0051EA30, its jump: REPLACE_SOURCE_ONLY): the tag button tapped within
    100 ms while walking - six feelers from the tagging bone across the heading (x87 FSIN / FCOS, as the original),
    the nearest wall, the line stretched by 10 up to 5 times, the tag started (script 0x8C92DEA3 / 0x5213AFD6, 30
    frames) then placed (50 / 75 / 100 by the combo's score), the cooldown 0xD29DE3D7.
  - Test (src/hook/selftests_skater_tagging.cpp; the feeler's query scripted - its line recorded, hits made up):
    6000 random cases over every function. 57 deliberate bugs: 53 caught (6 after strengthening: random normals,
    ties between walls, the destroyed block traced, the custom-tag flag varied, the two float sources told apart), 4
    equivalent (VertStickerSlap's walking flag only matters for tags; a zero facing.y; reach * 0; a miss at 1000 vs
    999 against a 999 start).
- [x] Batch SK4-6 (NOT run in the game yet; the trick component's button events, their checks and the trick lists'
  queue, 31 functions): src/game/skater/trick_component.cpp (new; the component's other half - its Create 0x00523490,
  member functions... - is the next range's).
  - The event ring (100 TrickEvent of 0x18 bytes from +0x38: time, type 1 pressed / 2 released, button, used, flags;
    count +0x998, head +0x99A): 0x0051F570 AddEvent, 0x0051EB00 ClearEventBuffer, 0x0051EB80 ClearUsed, 0x0051EBD0
    MarkUsed (a naked stub keeping ECX / EDX: the original's callers use ECX after it), 0x0051F610 ExpireButton,
    0x0051F6B0 FindPress, 0x0051F7A0 FindPressBoth, 0x0051F9B0 CheckSpinSequence, 0x0051FB90 CheckReleasePress (with
    setMap the button map is set from its own three stack arguments, as the original does), 0x0051F8F0 UpdateButton
    (held-off buttons +0x9A8, ignored buttons +0xA1C), 0x0051F290 HoldOffButton, 0x0051F010 DumpEventBuffer
    (compiled-out debug output: the strings kept).
  - Triggers: 0x0051EA40 IsDirection (the button in EAX; a naked stub keeping ECX / EDX), 0x0051EC40 ReadTrigger
    (the generic button names 0x9A7DC229 / 0x03749393 / 0x7473A305 mapped through +0x99C; the time a number or a
    global integer), 0x0051ECC0 SetButtonMap, 0x0051ECF0 IsButtonDown.
  - Lists and the queue: 0x0051ED10 ClearQueue, 0x0051ED20 QueueTrick (100 of 0x14 bytes at +0xA34; dropped when
    full), 0x0051ED90 RemoveTrickList (not a Ghidra function; the list after a removed one is skipped, as in the
    original), 0x0051EE10 IsTrickEntry, 0x0051EF50 TrickNamed (_stricmp 0x00626EFB done in code: the C locale's),
    0x00521930 QueueDoneTricks, 0x00521A10 QueueAllDoneTricks, 0x00521A80 SetPendingTrick - these call the trigger
    check 0x0051FDA0 (next batch) by its address.
  - 0x0051EA90 RegisterMemberFunctions (not a Ghidra function: the table 0x00683148), 0x0051F2B0 Finalize, 0x0051F320
    Reset, 0x0051EAB0 / 0x0051EB30 / 0x0051EF00 the clears, 0x0051F260 SetCurrentBailEndpose (not a Ghidra function).
  - scan_register_assumptions lists 0x0051EA40 as a RISK (ECX / EDX read after the call in 0x0051FDA0): the stub keeps
    both; with 0x0051FDA0 rewritten (next batch) the original caller no longer runs.
  - Test (src/hook/selftests_skater_trick.cpp): 6000 random event rings over every function (the stubs called with
    registers set and checked), the trigger check stood in for. 50 deliberate bugs caught, 6 after strengthening
    (ties at exactly an event's age and exactly the hold-off time, the d-pad buttons, aliases that name nothing,
    queued lists that get removed).
- [x] Batch SK4-7 (NOT run in the game yet; the trick trigger check, 1 function of 7 KB):
  src/game/skater/trick_component.cpp 0x0051FDA0 CheckTrigger (thiscall, RET 0x10: the trigger, the flags its events
  get, the mask of flags that rule an event out, Press's stretch). The trigger's first component names its kind, the
  next ones its buttons and time (ReadTrigger). 19 kinds (names checked with the checksum): Press, Release, Tap,
  InOrder, PressAndRelease, TapTwiceRelease, TripleInOrderSloppy, TripleInOrderStrict (these as step lists),
  TripleInOrder, HoldTwoAndPress, ReleaseTwo, ReleaseTwoAndPress, PressTwoAnyOrder, AirTrickLogic, PressTwo,
  HoldThree, ReleaseAndTap and 0xB6258557 (name unknown: a press with no d-pad direction but the button map's).
  Each read from the disassembly case by case (the decompiler's output was unreliable): when an event is too old
  or rules the trigger out the used marks stay, when the events run out they're cleared (but not for
  PressTwoAnyOrder); Press / Release give the event the flags themselves; Tap leaves the button map; HoldThree reads
  its buttons unmapped. Press's stretch: with the flag and the core physics' rate (+0x28 -> +0x2E8) below 1, the
  time divided by it (_ftol: int64 cast). QueueDoneTricks / SetPendingTrick call it directly now.
  - IsTrickButton (0x0051EA40, SK4-6) renamed IsDirection: its eight buttons are the d-pad's directions (Up, Down,
    Left, Right, the diagonals); CheckReleasePress's three are Triangle, Circle and Square (comments fixed).
  - With 0x0051FDA0 rewritten, scan_register_assumptions' RISK on 0x0051EA40 (ECX / EDX read after the call in
    0x0051FDA0) is gone from the running code; the stub keeps both anyway.
  - Test (TestTrickTriggers in src/hook/selftests_skater_trick.cpp): 12000 random checks over every kind (half the
    event rings of just the trigger's buttons; ReleaseAndTap's pattern planted; times exactly an event's age; mapped
    button names; no kind, no trigger, input blocked); every kind found by the original (25-312 times each).
    41 deliberate bugs: 40 caught, 1 equivalent (rate <= 1 instead of < 1: dividing by 1 changes nothing).
  - find_gaps over 0x005197F2-0x00521B90: only 0x0051B9D8 (a Ghidra split inside 0x0051B9D0, SK4-3: no separate entry).
