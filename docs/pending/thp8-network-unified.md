# thp8-network-unified - THP8's network levels and the whole town as THUG2 levels (2026-10-24)

Agent "thp8-net" (worktree branch), workspace C:\Games\THUG2-2. The user's request: "look into the Project 8 levels -
they have certain network play levels, whereas for us, it seems like we have just chunked the levels ... make a
unified level that contains everything, and make each network level". Data only from `C:\Games\THP8\game\DATA`
(never the recompilation's programs). No leaked source; THUGPRO and Morten1337's repositories not used.

## THP8's network levels (read from its data)

qb.pak's `online_level_select_menu_level_info` lists them, each entry naming a `multiplayer_level_<x>` structure:
`text`, `checksum` (the id), `spawn_in` (the zones whose network spawns it uses - the zones it is made of), `area_pak`
(the barrier archive, ZONES\z_online_<x>.pak.xen), `area_pak_zone` (the zone it belongs to), `ui_struct` (the level
select's description). In THP8's order:

| Text | Id | Zones (spawn_in) | Barriers (area_pak) | Ours |
|---|---|---|---|---|
| BEAVER VALLEY | BeaverValley | houses, cretepark, shops, center, citypark, ghetto, funpark, cartoon, factory, parking, inschool, outschool | none | thp8_net_beavervalley (the unified town) |
| SKATE PARK | Skatepark | cretepark | z_online_skatepark | thp8_cretepark (zone level, barriers added) |
| FACTORY | Factory | factory | z_online_factory | thp8_factory |
| SLUMS | Slums | ghetto | z_online_ghetto | thp8_ghetto |
| SUBURBIA | Suburbia | houses | z_online_suburbs | thp8_houses |
| MAIN STREET | MainStreet | shops | z_online_shops | thp8_shops |
| FUN PARK | funpark | funpark, cartoon | z_online_funpark | thp8_net_funpark |
| SCHOOL | School | inschool, outschool | z_online_school | thp8_net_school |
| INDUSTRIAL ZONE | Industrial | ghetto, factory | z_online_industrial | thp8_net_industrial |
| CAPITOL BUILDING | Capitol | center, parking, citypark | z_online_capitol | thp8_net_capitol |
| SKATE & FUN PARKS | SkateFunParks | cretepark, funpark, cartoon | z_online_funskatepark | thp8_net_skatefunparks |
| SKATE CENTRAL | SkateCentral | houses, shops, center, citypark, parking | z_online_central | thp8_net_skatecentral |

- **Barriers**: each z_online archive is a small scene of barrier walls (`Z_Online_<x>_g_barrier_NN` sectors, 9 in
  central's) with Havok collision bodies, a node array of their LevelGeometry nodes (a few with a trigger script) and
  a texture (THP8 draws the walls as a shimmer near the skater). No spawns in them.
- **Spawns**: in the zones themselves - every city zone has ~9-11 `Multiplayer` restarts (plus Player1 / Horse ones),
  and four CTF flags (`Z_<zone>_TRG_Flag_<colour>` GameObjects). A network level's spawns are its zones'.
- **Game modes**: not per level. THP8's network menu (`net_game_type_info`) offers TRICK ATTACK, GRAFFITI, COMBO MAMBO,
  WALLS, SCORE CHALLENGE, COMBO CHALLENGE on every level (mode_info also defines netctf, netking, netslap, netfirefight,
  netelimiskate, netgoalattack). In THUG2 the modes are THUG2's own, offered for every level; ours have the
  Multiplayer / Horse restarts its modes use. CTF: THP8's flags are kept as before (zone-prefixed names - THUG2's
  ctf.qb looks for TRG_CTF_<colour>, so THUG2's CTF has no flag bases in THP8 levels: not done here, as before).
- **2P / split screen**: `twoplayer_level_select_menu_level_info` names load_Z_2P_* scripts (THP8's own two-player
  versions) - not converted.
- Before this, the zone levels outschool / center / funpark had the barriers of the multi-zone areas they belong to
  (school / capitol / funpark) - walls far outside them and open where the zone was cut. Those barriers are now only
  on the network levels; the single-zone ones keep theirs.

## What was made (tools/thp8_levels, tools/menu_level)

- `networkAreas` / `readNetworkAreas` read the list; `netSpecs` makes a level of each one of several zones
  (`thp8_net_<id>`, level select 125-131, the sign THP8's text, " AREA" where a zone level has it: FUN PARK AREA,
  SCHOOL AREA). `convertLevel` converts any level spec - one zone or several: the zones' scenes as parts of one
  scene (`convertScenes`: a material of one checksum defined differently in two zones - other light maps, 53 of the
  276 shared across the town - gets a checksum of its own; each zone's light maps lit by its own sun), collision
  (each zone's bodies, props' boxes, barriers), node arrays one after the other (`mergeNodeArrays`: Links moved up -
  no node name repeats across the twelve zones, checked), scripts from all its zones, restarts (soloRestarts /
  groundRestarts / netGameNodes as before), the light refining budget per zone. Moved as one (the middle of all the
  collision at the origin).
- The whole town (BEAVER VALLEY) reaches 21,015 units from its middle - past the +-16,384 THUG2's half-unit network
  positions reach: its entry gets `IsBigLevel` (manifest `flags`, passed on by level_select.manifestLevels); the DLL
  now reads that flag from the THPS2 / THP8 lists too (src/game/net/thugpro_compat.cpp kLevelLists). The other network
  levels reach 5,525-13,275 (school: the inside zone stretches far south).
- Level select: seven new pictograms (level_select_icons.ts: citymap, balloon, books, chimneys, pediment, ticket,
  compass - inside the diamond, unique), THP8_LEVELS entries `net_<id>`; the --sheet now shows THP8's too.
- Zone levels kept (the user's complaint - levels cut at zone edges - is answered by the network levels, which are
  THP8's own playable areas; the zones stay for single areas). `list` shows all 22.
- Tests (thp8_levels.test.ts, synthetic): the list read (text / id / zones / barriers / main zone, one-zone levels
  left to the zones, " AREA" signs, the main zone first), node arrays merged (Links moved up), material keys,
  the refine budget per zone, the reach. Mutations: Links not moved / the main zone not first - both caught.
  `npm test` 331 pass, typecheck, lint, selfcheck (1132 files, 0 failures).

## Budgets, measured (game runs: workspace THUG2-2, Debug DLL, `memory in <level>` log line - new, see C++ below)

| Level | Files (scn / tex / col / qb MB) | Drawn tris | Nodes | Main block free (of) | BottomUp / Script heap | Process private | Address space free (largest) |
|---|---|---|---|---|---|---|---|
| thp8_bedroom (before) | 0.7 / 1.1 / 0.03 / 0.01 | 11,014 | ~100 | 38,575 KB (69,824) | 24.4 / 7.1 MB | 171 MB | 1,455 MB (517) |
| thp8_center (before) | 11.5 / 7.2 / 0.5 / 0.6 | 180,530 | 4,325 | 37,098 KB (69,824) | 25.9 / 7.3 MB | 206 MB | 1,432 MB (517) |
| thp8_net_school | 9.4 / 6.9 / 0.6 / 0.9 | 167,470 | 5,657 | 36,355 KB (69,824) | 26.7 / 7.4 MB | 219 MB | 1,431 MB (517) |
| thp8_net_capitol | | 474,707 | 8,757 | 35,019 KB (89,856) | 48.0 / 10.9 MB | 299 MB | 1,363 MB (517) |
| thp8_net_skatecentral | | 625,222 | 13,957 | 32,698 KB (89,856) | 50.4 / 11.1 MB | 375 MB | 1,299 MB (517) |
| thp8_net_beavervalley | 81.5 / 64.1 / 3.5 / 4.8 | 1,261,786 | ~34,000 | 24,415 KB (89,664) | 58.4 / 12.0 MB (peak 14.0) | 608 MB | 1,058 MB (517) |

Also loaded: thp8_net_skatefunparks (main block free 35,383 KB, private 289 MB), thp8_net_funpark (36,192 KB, 259 MB),
thp8_net_industrial (35,665 KB, 262 MB), thp8_net_school with the new DLL (36,382 KB, 240 MB); and the zone level
thp8_funpark (its barriers now gone) as a regression check. The level's scene and textures aren't in the main block (Direct3D's
vertex buffers / textures and the file buffers are outside it): what grows is the process (171 -> 608 MB private) and
what's left of the 2 GB address space (the exe isn't large-address-aware; never modified) - the whole town leaves
~1 GB free, its largest free block 517 MB. That is the remaining hard limit: a level ~2.5 times the town's size would
run out of address space. Nothing was cut from any level.

### Script pools (the whole town at its peak, defaults before -> after)

CComponent 284,043 (184,000 -> 360,000), CStruct 43,362 (49,600 -> 64,000), CVector 37,294 (19,000 -> 48,000; ran dry
first: "pool CVector full" then a crash), CArray 19,691 (16,000 -> 26,000), HashItem 39,923 (40,000 -> 56,000),
CSymbolTableEntry 14,514 (45,000, unchanged). Main block: 71.5 MB -> 89.7 MB (the bigger pools + the script heap).

## C++ changes (own commits; all NOT ORIGINAL, original_sizes=1 gives the original's)

1. **Memory log** (6abadbe): `Mem::LogMemoryUse` (src/game/core/memory_report.cpp) logs once a level is up (next to
   the script pools' line, -level / start_level runs): the main block, free now / least, every heap over 64 KB used /
   peak, process private bytes, the address space free and its largest block. The pools' line gains HashItem.
2. **Limits** (1b35dd9), each found by loading the whole town (hang dumps with MiniDumpWriteDump + read_dump.ts +
   symbolize.ts):
   - **The node manager's name hash** (SkateScript::CNodeManager, 0x006F0D84): 0x4000 slots of 16-bit indexes; the
     town's ~34,000 named nodes filled it and HashNode (0x0045CE60) went round it for ever - the load hung. Ours:
     `Mem::Sizes::NodeHashSlots()` slots (default 0x10000, `[memory] node_hash_slots=`, a power of two 0x4000-0x100000)
     kept beside the manager (its layout - static_assert - and m_slots / m_walk are left: the original's functions
     reading the manager, 0x004E8F50 and two CFuncs, only pass it on as `this`), node indexes and the walk in 32 bits
     (a node array may hold more than 32,767; the town's does). Only the game's own manager (CreateNodeManager) gets
     it (`SkateScript::UseBigNameHash`); any other uses its m_slots as the original. A full hash is logged instead of
     looping; GetNode stops after one round. Why it's safe: every function touching the slots is ours (0x0045CE60-
     0x0045D6E0 all REPLACEd; the only original references to 0x006F0D84 load the pointer and call our methods);
     levels within the original's size find the same nodes (self-test below).
   - **The rail manager's name table** (CRailManager::AddFromNodeArray 0x0057E960): another 0x4000-slot 16-bit table,
     ~18,000 rail nodes in the town: the same endless loop. Ours: with 0x2000 rail nodes or more a table of 32-bit
     indexes at most a quarter full (smaller levels keep the original's exactly - its self-tests compare heaps).
   - **The script heap**: AddNodeArrayPrefixes (the node name prefixes, on the script heap) ran it dry (crash in
     PrefixNodes::Add on a failed Malloc) - the original left ~2 MB beside the pools. Ours: 16 MB more
     (`[memory] script_heap_extra_mb=`, 0-256), part of the script region / main block. The town used 12.0 MB of the
     script heap (peak 14.0).
   - **Pool defaults** (memory_sizes.cpp kDefaults): above. ~3.7 MB more of the main block.
   - Self-tests (selftests_nodes.cpp): the bigger hash (0x10000 slots) against the original's code and table - the
     same nodes found for names that share a slot only in the original's, the absent ones none, the walk, after an
     array is unhashed; ours only: a full 4-slot hash leaves the 5th node out and GetNode returns null (no endless
     search). Mutations (tools/mutate.ts): the walk's step, unhash keeping nodes, GetNode without its guard - caught;
     node indexes cut to 16 bits in the big table - missed (the test's arrays are small; the town's load is the test).
     selftests_memory_sizes: our counts, THUGPRO's kept as a table, the original's hash / no extra heap under
     UseOriginal. thug2_check: 771 early self-tests pass; check_code_writes: no overlaps.
   - The level editor's budget shows the hash's size from NodeHashSlots.
3. **IsBigLevel lists** (4b8105b): src/game/net/thugpro_compat.cpp also reads the THPS2 and THP8 level select lists.

## Tested in the game (THUG2-2, under its lock, this branch's Debug DLL; load checks only - nobody played)

- All seven new levels load into free skate (`load_check.ps1` / `run_game.ps1 -Level`), no exception / run-time check /
  CRT report / pool full in the log: thp8_net_school, _capitol, _skatecentral (see the table), _skatefunparks,
  _funpark, _industrial (below), and **thp8_net_beavervalley - the whole town - loads**: ~7 minutes in the Debug
  build (the node array and the rail / trick object set-up), then runs; script pools / memory as above.
- Before the C++ changes the town: crashed (pool CVector full) / with bigger pools from the ini: hung (node hash),
  then crashed (script heap), then hung (rail table) - each fixed as above.
- **Not tested: hosting a LAN game.** The test-automation channel refuses every network / LAN / host item and script
  (by design: agents never go online, and getting round the refusal is not allowed), so whether the levels appear in
  the host's level select and load in a network game wasn't run. Why they should: the network level select is the
  same create_level_select_menu (in_server_options - docs/pending/levelselect.md), whose THP8 section lists all 22
  levels (checked in the installed gamemenu_levelselect.qb: entries 125-131, BEAVER VALLEY with IsBigLevel); the
  levels have Multiplayer / Horse restarts.

## For the user to try

1. Free skate: the PROJECT 8 section's new signs after BEDROOM - BEAVER VALLEY (wait ~7 min in a Debug build), FUN
   PARK AREA, SCHOOL AREA, INDUSTRIAL ZONE, CAPITOL BUILDING, SKATE & FUN PARKS, SKATE CENTRAL. Skate across the zone
   seams (houses -> shops -> capitol, skate park -> fun park): no gaps, walls or doubled geometry? Lighting the same on
   both sides of a seam?
2. The barrier walls of the network levels (invisible collision) at the right places; none left in SCHOOL /
   THE CAPITOL / FUN PARK zone levels.
3. Host a LAN game (NETWORK PLAY -> host) and pick a THP8 network level: listed, loads, spawns spread over its zones;
   a second copy joining (allow_multiple_instances=1) sees the host's position right in BEAVER VALLEY (IsBigLevel).
4. Frame rate in BEAVER VALLEY / SKATE CENTRAL (1.26 M / 0.63 M triangles; THUG2 draws what's in view).

## Notes / not done

- THP8's lod stand-ins (`z_<zone>_lod`), lower-resolution textures: not used - nothing had to be cut.
- CTF flag names, THP8's 2P levels, the per-level game mode descriptions (ui_struct description) - not ported.
- Load time: the town's ~7 min is the Debug build's (the user's x86-debug too); a Release build should be far faster.

# Follow-up (2026-10-24, branch thp8-net-2 from main f95c862; workspace THUG2-2)

The user played main: "In each of these levels, there is still the bugs. Also, looks like 'Beaver Valley' level now
loads the entire map, but there are invisible collision walls preventing us from skating it. For the levels with the
screenshots, are these just our interpretation of levels? if so, they don't need to exist - we only need to have the
main open world level, and the network levels (all playable in free skate & network)". The screenshots were of the
twelve per-zone levels, each ending in void at the zone's edge.

## 1. The set: THP8's own levels only

- Yes, the per-zone levels were our own split: in THP8 the city's zones are one streamed open world (its
  Level_Z_<zone> entries only pick where the world starts). Removed (`RETIRED`: thp8_houses, _outschool, _inschool,
  _shops, _ghetto, _factory, _center, _citypark, _cretepark, _funpark, _parking, _cartoon). Every convert now deletes
  the staging folders and levels.json entries of levels it no longer makes (`pruneStaging`); make_menu_level makes the
  overlay again from the staging, so they leave the overlay too (checked in THUG2-2: no old thp8_<zone> folder left).
- Now: the 12 network levels + BEAVER VALLEY (the open world) = 13, all `thp8_net_<id>` (the five one-zone ones too:
  thp8_net_skatepark, _factory, _slums, _suburbia, _mainstreet - their z_online barriers and the zone's network
  spawns), in THP8's order, level select 113-124; signs reuse the zones' pictograms for the five (bowl, gear, bricks,
  mailbox, trafficlight). All in the PROJECT 8 section, which free skate and the network level select share.
- Kept, standalone in THP8 too: TRAINING (z_training), SKATE SHOP (z_skateshop), BEDROOM (z_bedroom) - THP8 has a
  level structure and load script of their own for each (Level_Z_Training / _Skateshop / _Bedroom), they are in no
  network level's spawn_in and lie outside the city. The cartoon world is part of the city (the fun park's and the
  whole town's spawn_in): no level of its own now.
- The void at the edges: every level of the city now also draws the city's other zones' `_lod` stand-ins (THP8's
  far-away versions, ~0.3 MB scene + 1 MB textures each; drawn only, no collision) - what THP8 shows beyond a network
  level's barriers.

## 2. BEAVER VALLEY's invisible walls

- Offline survey (out of the converted collision vs the drawn sectors, and of THP8's Havok bodies): 676 collision
  objects in the town have no drawn sector; the ones that matter are the **seam walls**: each zone's collision has
  invisible walls (all faces 0x1000 invisible, most 0x1080 - the camera passes) along its edge where the next zone
  begins: z_houses' 115 m x 70 m plane at x = 0 (z_shops starts there), z_parking's at x = -50 (the fun park), z_ghetto's
  at x = 277 (the factory), z_shops' / z_inschool's at x = 1, z_outschool's at x = -125 (the skate park)... No node and
  no sector owns them (no Name on the Havok bodies, no LevelGeometry node near them; their bodies are in the zone's
  main collision like any other) - THP8 must switch them off at run time when the neighbouring zone is streamed in;
  nothing in its data says so. Not the z_online barriers (the town has none) nor the overlay.
- Fix (tools/thp8_levels seamWalls): in a level of several zones, a body whose collidable faces are all invisible and
  mostly steep, with another of the level's zones' ground 4 m beyond its faces at half or more of the (up to 9) places
  looked at, is left out. 43 in the town (houses 3, cretepark 4, shops 5, citypark 1, ghetto 2, funpark 3, cartoon 7,
  factory 1, parking 12, inschool 3, outschool 2); 1-8 in each other multi-zone level; none in one-zone levels (their
  edges stay walls, with the barriers). Kept: invisible walls with only their own zone about (roof blockers, the
  houses' Grecian roof wall...) and the town's outer edge.
- Not confirmed: whether these were the walls the user hit (no spot given). Invisible walls that stay and could still
  block (largest; THP8 coordinates): z_shops' plane x 106..141 z -148..16 (own ground both sides - the slums side?),
  z_houses' x -59..14 z 2..21 (y -8..38), z_cartoon's x -196..-170, z_factory's x 278..337 z -109. If the user hits a
  wall, a screenshot (its sidecar has the position) pins it.
- The one diagnosis run (SKATE CENTRAL, teleport onto the houses side of the x = 0 seam, push towards the shops with a
  trace): inconclusive - the skater stayed hovering at the teleport spot (state "air", y fixed, 600 frames), so the
  crossing wasn't seen. Load fine (memory line as before, private 403 MB).

## Checks

Tests (seam walls: an invisible wall with the other zone's ground beyond left out, one with only its own zone about
kept, a seen wall kept, one zone none - mutations caught; the set: standalone zones, RETIRED; network specs with a
one-zone level); npm test 333 pass, typecheck, lint, selfcheck (1132 files). Converted and installed in THUG2-2 (15
levels).

## For the user

1. BEAVER VALLEY: skate across the zone seams (houses -> main street at the big gate, the hill top -> fun park, slums
   -> factory, school grounds -> skate park): no invisible wall? If one is still there, a screenshot there.
2. The one-zone network levels (SKATE PARK ... MAIN STREET): the zone's barriers hold you in, the other zones visible
   beyond (low detail) instead of void.
3. TRAINING, SKATE SHOP, BEDROOM still there; no old per-zone signs.
