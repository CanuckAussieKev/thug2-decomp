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
