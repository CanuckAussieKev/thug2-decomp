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
