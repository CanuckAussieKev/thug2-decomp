# THUGPRO compatibility register

Everything this project does **only so that it agrees with THUGPRO** (Morten1337's THUG2 mod) for online play: choices
that are THUGPRO's, not the original games' data. Each entry has an id (TP-n), what it is, why, where it's applied and
how to take it out. Keep this list complete: a new THUGPRO-only choice gets an entry here, its data in this folder,
and a `TP-n` in the code comment where it's used.

Why separately: these are arbitrary agreements (names, offsets, identities), not improvements. Kept apart they can be
switched off as a whole (`--compat none` for the converters) or later replaced by a run-time translation of the
network messages, e.g. to play the same maps with players of another game or mod (THAW players: THAW's own names and
world frame) - see "Changing approach" below.

Data: `thaw_levels.json`, `thps3_levels.json`, `thps4_levels.json`, `thug1_levels.json` (read by `tools/compat/compat.ts`). Comparison
against THUGPRO's install: `tools/thaw_levels/thugpro_compare.ts`, `tools/thps3_levels/thugpro_compare.ts`, `tools/thps4_levels/thugpro_compare.ts`,
`tools/thug1_levels/thugpro_compare.ts`; THAW's hand edits (TP-5...TP-9): `tools/thaw_levels/thugpro_extract.ts` (extracting),
`thugpro_edits.ts` (applying), `thugpro_inventory.ts --ours` (what still differs). Background: `docs/pending/thaw.md`, `docs/pending/thps3-thugpro.md`, `docs/pending/thps4-thugpro.md`,
`docs/pending/thug1.md` ("THUGPRO"); the comparison reports (THAW / THPS3 / THPS4 / THUG1 ports vs THUGPRO). Rules: insights
from Morten1337's repositories are allowed (his permission, 2026-10-20), never copies of his
code or level data; the values here are identifiers and offsets, measured or read, with their source given.

## Build time (persisted in the files we generate)

| Id | What | Why (what travels online) | Applied in | Source |
|---|---|---|---|---|
| TP-1 | THAW level names: Chicago `skatepark`, Kyoto `kyoto`, Santa Cruz `sz`, Vans Park `vans` (the other 11 keep THAW's zone name, `z_ma`..., as THUGPRO's do) | A level is known online only by its load script's checksum (change level 0x46, start info 0x33): both sides must name it alike | `thaw_levels.json` `level_names` -> `thaw_levels.levelName` (folder, files, `Load_` / `Level_` scripts, node array name) | THUGPRO's installed `data\pre` file names |
| TP-2 | Vans Park left in THAW's world frame although it has a `Z_SV_Zone_Origin` | Positions travel as world coordinates (skater updates 0x23, int16): both sides must place the area alike. THUGPRO's toolchain never moved it (it looked the origin up under the new name `vans`) | `thaw_levels.json` `frame_kept` -> `thaw_levels.frameShift` | thugpro_toolchain `find_zone_origin.py`; confirmed by `thugpro_compare.ts` (2192/2192 nodes in place) |
| TP-3 | East LA and Santa Monica moved by THUGPRO's own offsets on top of the Zone_Origin frame: EL (-1319.656, -111.047, 269.924), SM (-5060.44, -580.625, -3203.87) | As TP-2. THUGPRO's "LA crash fix" (commit 361b2c1) moved them by hand-picked offsets that aren't in THAW's data | `thaw_levels.json` `frame_adjust` -> `thaw_levels.frameShift` | thugpro `source/levels/Z_EL/world_offset.txt`, `source/levels/Z_SM/node_position_patcher.py`; the values measured with `thugpro_compare.ts` (EL 3404/3404, SM 3644/3661 nodes in place - the rest moved by hand in THUGPRO) |
| TP-4 | THAW network spawns as THUGPRO hand-edited them (all 15 areas): every restart with Multiplayer / Horse / CTF / Team types THUGPRO moved, retyped, added or took out of the spawns - 18 edits in CH, SZ, BH, EL (CH's TrickAttack restart moved 434 units and no longer a spawn, CTF Green restart CTF only, an added half-pipe drop-in spawn; BH's Player_Start01 13.9 lower; PriorityRestart dropped on SZ / EL starts; EL's TransitionParkTeleport restarts no spawns; Santa Cruz's 10 restarts THAW types with the malformed string "Player 1" - restart01, derby01, fromalley01 / 02, fromboardwalk01, Killox01_restart..05 - made [Player1 Multiplayer Horse] as THUGPRO's, approved separately). `spawns_not_applied` is empty. Approved by the user 2026-10-20 | Competitive fairness: a player shouldn't start where the opponents never do. Spawns are picked locally, not sent: player n starts at the n-th Multiplayer restart of the node array, a respawn at one of the first 8 (0x00566100, 0x0046B790, 0x00580830) - so places, types and order (`order`: every spawn in THUGPRO's order, the slots refilled, Links renumbered) | `thaw_levels.json` `spawns` -> `thaw_levels.applySpawns` (positions in THAW's frame; added spawns in our form, Restart Generic, running the level's `<level>_net_dropin`: MakeSkaterGoto DropIn) | THUGPRO's installed `data\pre\<level>_scripts.prx` node arrays against THAW's own `_net` ones, extracted by `node tools/thaw_levels/thugpro_compare.ts --extract-thugpro` (never typed in); checked by `thugpro_compare.ts` (net spawns the same - places, types, order - in all 15 areas) |
| TP-5 | THAW node edits (all 15 areas, the user's decision 2026-10-20: everything THUGPRO changed is data here): 60 nodes THUGPRO moved / turned or relinked (rails - SM's 17 PacificParkStoresBuilding rails 94 units, DN's Player1_start02 rail 57, JA's TRG_783 11, SZ's levelevent99/100 16; MA's 6 lamps, DT's 99Cent_02 463, CH's Outby701/702 gap triggers; SM's 12 ferris cars) and 23 scene sectors / collision objects it moved rigidly (MA's 6 lamps with DLamp10 turned 6 degrees, CH's 2 gap-trigger polygons, DN's 5 lights / planter, DT's promo stickers, LV's backdrop - Bellagio, bridge, window glows - moved by thousands of units); 226 nodes renamed where no THAW script or node names the old name (EL 110 / SM 103 night lights and SZ's Pier124 light, the zone prefix taken off - probably THUGPRO's toolchain; CH's 11 goal letters; SZ's PBoatPostEvent_TRG_2214 rail -> bastardrail1). Applied (awaits a game run) | Positions travel as world coordinates: a rail or wall in another place means a remote skater grinds or stands in the air. Renames: names don't travel; applied so that the node arrays agree | `thaw_levels.json` `node_edits` (`moved`, `renamed`; `geometry`: translate / rotate_y / pivot, THAW's frame) -> `thugpro_edits.applyNodeEdits` (THAW's array before converting) and `applyGeometryEdits` (THAW's scene / collision before converting; a moved collision object gets float vertices, a new box, BSP tree and reaches) | THUGPRO's installed `data\pre\<level>_scripts.prx` / `scn.prx` / `col.prx` against THAW's `<zone>_net.pak.wpc` (and `<zone>.pak.wpc`), extracted by `node tools/thaw_levels/thugpro_compare.ts --extract-thugpro` (`thugpro_extract.ts`; never typed in). A sector / collision object counts as moved when the same points, in any order (THUGPRO converted THAW's Xbox files: they weld and order vertices otherwise), fit a rigid move to 0.1 units; otherwise it is reshaped and awaits the user. Checked: `thugpro_inventory.ts --ours` - 0 nodes elsewhere (44 before), `thugpro_compare.ts` every shared node at THUGPRO's place in all 15 |
| TP-6 | THAW node flags and keys as THUGPRO's (1,061 nodes): CreatedAtStart (taken off 216 - SV's elevator / bathroom doors, LV's 15 occluders, SZ's fromalley01 / fromboardwalk01 restarts and 6 rails, peds; added 113 - EL's 67 / SM's 44 night-off pieces), AbsentInNetGames (taken off 378 - JA's 168 light poles / bushes, SZ 93, EL's 44 metro-HQ rails and the limo, BH 23, SM's 12 ferris cars; added 110 - CH's 73 shadows, DN's 30 life peds), TrickObject (+24 / -13), CollisionMode (SZ 7, CH 2, LV 15), Class / Type (BH's 3 rails made game objects, CH's 17 / SZ's 13 life peds and points of interest made ghosts, DN 31 / JA 4 life nodes made peds, SV's 23 goal peds ped_from_profile, BH's cop cars cars), TerrainType (SZ 43 rails wire -> wood railing, SR 20 wood -> metal, DN 2, JA 1, EL 1), TriggerScript taken off (30) or set to a THAW / THUG2 script, restart_types / RestartName of restarts that aren't spawns (SV's TrickAttack restart); clusters THUGPRO renamed on all their nodes (DN 218, JA 288, SZ 13: the zone prefixed - probably THUGPRO's toolchain; harmless, and graffiti names a cluster) unless a THAW script names the old one (JA 34 kept); and the 485 scene sectors and collision objects of the 310 nodes whose AbsentInNetGames THUGPRO took off, from THAW's full zone archive (the net archive we convert hasn't them: JA 246, SZ 163, BH 39, EL 34, HO 2, SV 1). NetEnabled: THUGPRO changed none. Applied (awaits a game run) | What is there in a network game, what is solid, what grinds and how it sounds: a pole or rail present on one side only is a wall / rail the other side hasn't | `thaw_levels.json` `node_flags` (`nodes`: add / remove / set / unset; `clusters`: THAW's -> THUGPRO's; `imported`) -> `thugpro_edits.applyNodeEdits` / `applyGeometryEdits` (`thaw_levels.fullArchive`) | As TP-5. A TriggerScript naming THUGPRO's own script awaits the user (`awaiting`, kind script). Checked: `thugpro_inventory.ts --ours` - 0 flag / key differences on rails, geometry, restarts, objects (5,626 before) but our port's own (below) |
| TP-7 | Nodes, scene sectors and collision objects THUGPRO took out: CH's SkVans_Rail_Col01/02 (and the Sign03_LOD collision), SV's 7 meshes (Mesh11, 197, 198, 200, 201, SauceMachine_06, SnackMachine_04), DT's Mesh15, the collision of SZ's beach waves, booster streamers, hotel-roof pipe, KarnBox09 and FL_Barrier45, BH's gas-station ground piece, LV's elevator wall. Applied (awaits a game run) | A wall or floor only our side has: a THUGPRO skater passes through it on our screen, ours stops where theirs doesn't | `thaw_levels.json` `removed` (`nodes`, `sectors`, `collision`) -> `applyNodeEdits` (placeholders: the Links stay right) / `applyGeometryEdits` | As TP-5; checked: no collision or sector only ours |
| TP-8 | Nodes THUGPRO added that are made only of what our side has: SM's 24 static ferris-car rails, JA's 3 DDR rails (TRG_TP_DDR_RAIL_*), 211 waypoints (JA 97 and SZ 24 car paths, EL 88, SR 2 - their TriggerScripts THUG2's Car_* scripts). Not THUGPRO's meshes, models or scripts (they await the user). Applied (awaits a game run) | Rails: a remote skater grinding THUGPRO's ferris / DDR rails grinds them here too | `thaw_levels.json` `added` (`nodes`: their entries as JSON - Pos in THAW's frame -, Links by name) -> `applyNodeEdits` (appended to THAW's array, then converted as THAW's) | As TP-5: THUGPRO nodes whose name is in no THAW archive (not its _sfx sub-zones, not the full archive), whose class is a rail, waypoint, restart, generic node or object with a THUG2 model, and whose TriggerScript THAW or THUG2 defines |
| TP-9 | Per-face collision flags (THUG2's 0x1FFF) and terrain THUGPRO changed on THAW's own faces: 2,523 faces in 111 objects - wallride and vert (MA's fire-exit doors and sculpture +wallride, round store -skatable / -wallride), non-collidable (SV2's 3 hydrant waters, EL's park tree), camera-collidable (CH's SkVans_Rail01, SR's telescope), and terrain on DN's 61 objects (1,043 faces), SZ 7 (506), SR 10 (324), MA, SV, SV2, DT - the terrain maybe THAW's Xbox data, applied all the same (sound only). Applied (awaits a game run) | Wallride / skatable / collidable decide where a skater can go: on one side only, a THUGPRO skater wallrides where ours can't | `thaw_levels.json` `collision` (`faces`: per object its face count and [face, flags set, flags cleared, terrain]) -> `thugpro_edits.applyFaceEdits` (on the converted collision; the face count checked) | As TP-5: only objects whose faces are THAW's (same count, same vertices); THUGPRO's rebuilt faces await the user. Checked: `thugpro_inventory.ts --ours` - no face-flag or terrain difference on THAW's faces but our breakable glass (below) |
| TP-10 | THPS3 capture the flag: the Red and Blue flags and their bases where THUGPRO has them (place, angles and colour) in Rio, Suburbia, Skater Island, Tokyo, Cruise Ship and Warehouse - THUGPRO moved them from THPS3's own bases there (SI and Ware with the colours swapped against THPS3's ctf_1 = blue). Approved by the user 2026-10-20 | CTF pick-ups and captures are decided on the server against its own flags (SK4_Scripts Team_Flag -> CTF_Team_Flag_Trigger `if OnServer`); each side draws the flags from its own node array, so both must place them alike. The nodes' form stays ours (THUG2's Airport form, THUG2's flag models, Team_Flag); the team restarts stay THPS3's (the THPS3 CTF restart nearest to the flag) | `thps3_levels.json` `ctf_flags` -> `thps3_levels.levelCompat` / `ctfNodes` | THUGPRO's installed `datapre<level>_scripts.prx`, `levels<level><level>.qb`: TRG_CTF_Red / TRG_CTF_Red_Base / TRG_CTF_Blue / TRG_CTF_Blue_Base (Pos, or Position with z negated; Angles), read by `tools/thps3_levels/thugpro_compare.ts --extract-thugpro` (a level is listed when THUGPRO's flag isn't at THPS3's base of that colour) |
| TP-11 | THPS3 team nodes THPS3 has no counterpart for, in all 9 levels THUGPRO shares (foun, rio, sub, si, tok, shp, ware, burn, ros): TRG_CTF_Green / _Yellow (+ _Base), the lobby's team flags TRG_Flag_Red / Blue / Green / Yellow and TRG_Team_Restart. Approved by the user 2026-10-20 | THPS3 is a two-team game: without them a 3-4 team CTF has no flags for teams 3-4, the team lobby has no team-select flags and no Team restart. Places as THUGPRO's so that both sides agree; form ours (Airport form, Team_Flag scripts); the Green / Yellow team restarts are THPS3's multiplayer restarts nearest to their flags (TP-12 then places them as THUGPRO's) | `thps3_levels.json` `team_nodes` -> `thps3_levels.levelCompat` / `ctfNodes` | As TP-10: the same files, nodes TRG_CTF_Green(_Base), TRG_CTF_Yellow(_Base), TRG_Flag_<colour>, TRG_Team_Restart |
| TP-12 | THPS3 spawn points as THUGPRO's, in the 9 shared levels: every restart with a network type (MultiPlayer, Horse, Team, CTF*) that THUGPRO moved, retyped (restart_types / Type / RestartName / CreatedAtStart), renamed or added gets THUGPRO's place, angles and types (`changed`: the four TRG_CTF_Restart_<colour> everywhere, Foun's TRG_CTF1_Restart_1P/2P types, SI's TRG_Restart_Horse05, Tok's added TRG_Restart_1P, Ware's TRG_CTF2_Restart_* angles / types); THPS3 network spawns THUGPRO hasn't (`removed`: Ware's TRG_CTF1_Restart_1P/2P/4P, reused by THUGPRO as its team restarts) stop being spawns (Type / restart_types Generic, kept for kills that name them). TRG_Team_Restart stays TP-11's. And every network spawn in THUGPRO's node-array order (`order`: the spawns' slots refilled, Links renumbered; no Player1-first swap for these levels). Approved by the user 2026-10-20 | A player spawning elsewhere than the other side's players could be an advantage or a disadvantage: the user wants both sides' spawns unified. The order counts too: THUG2 starts player n at the n-th Multiplayer restart of the array and respawns at one of the first 8 (Skate 0x00566100 -> GetRestartNode 0x0046B790; CSkater::MoveToRandomRestart 0x00580830, Rnd(8)) | `thps3_levels.json` `spawns` -> `thps3_levels.levelCompat` / `applySpawns` (after `ctfNodes`) | THUGPRO's installed `data\pre\<level>_scripts.prx`, `levels\<level>\<level>.qb`, compared with THPS3's `Data\Levels\<Lvl>\<lvl>.qb` by `tools/thps3_levels/thugpro_compare.ts --extract-thugpro`; checked equal after conversion in all 9 (place, angles, Type, restart_types, RestartName, CreatedAtStart) |
| TP-13 | THPS3 nodes THUGPRO moved or turned (not spawns / CTF nodes, TP-10..12's): Foun 5 (3 peds up to 56 units, 2 particle boxes), Sub 4 (particles), SI 1 (TRG_Ped_PerchedPirate 59), Shp 6 (the landing path TRG_LandPoint01-04, TRG_Land02, sgo_Land: 300 units), Burn 9 peds (17-347 units) - the user's decision 2026-10-20: everything THUGPRO changed, as data under the flag | Positions of objects / peds aren't sent as such, but both sides should see the same level; moved objects draw (and, for game objects, place their models) where THUGPRO's do | `thps3_levels.json` `moved` -> `thps3_levels.applyNodeEdits` (on THPS3's own array before conversion: Position, z negated, and Angles; objectSectors places the moved game objects too) | THUGPRO's installed `data\pre\<level>_scripts.prx` node arrays against THPS3's own `<lvl>.qb`, extracted by `node tools/thps3_levels/thugpro_compare.ts --extract-thugpro tools/compat/thugpro/thps3_levels.json` (`nodeEdits`; never typed in); checked by `thugpro_inventory.ts --applied` |
| TP-14 | THPS3 node flags THUGPRO changed (CreatedAtStart, AbsentInNetGames, NetEnabled, TrickObject) on 331 nodes it kept of the same kind: Tok's 17 TRG_SRail_* / TRG_GS_SRail_00 rails and its shinkansen AbsentInNetGames, 185 path waypoints not, 22 not created at start; Shp's pool, ferry box, snap wires, track glass AbsentInNetGames, cakes / window reflections; SI's cannons trigger, door, lighting, sponsors AbsentInNetGames (NetEnabled off), debris; Foun's crane beam rails (CreatedAtStart, TrickObject off), presses created at start; Ros's autopsy / garage doors CreatedAtStart; Ware's secret-room glass without NetEnabled, hook / chains created at start, the dark set not created at start; Rio's broken fences kept online; Sub's invisible blockers (InvisoBox01, SUB_InvisoPlantsBlocker01, Inviso_PicnicCollide01, DISHSTART_*, InvisoStairsUnBonker) not created at start - its way of taking them out - and LOD jeeps; 2026-10-20 | Geometry and rails present on one side only: a rail one side grinds online and the other can't, a wall one side collides with | `flags` -> `applyNodeEdits` (as if THPS3 had them so: the converter's created-at-start and hidden analysis then follow them) | As TP-13. Not taken: flags of nodes THUGPRO made another class (its lights, particle boxes, static peds as GameObject / GenericNode...: its toolchain's form - `_notes`); NeverSuspend / Permanent (its toolchain adds Permanent to every NetEnabled) |
| TP-15 | THPS3 TerrainTypes THUGPRO changed: Rio 313 (297 wires TERRAIN_GRINDELECTRICWIRE, canal copings CONCSMOOTH), Tok 296 and Shp 125 (THPS3's TERRAIN_MEATALSMOOTH typo -> METALSMOOTH; Tok's 20 untyped -> CONCSMOOTH), Ware 46 (GRINDCONC), Sub 36 (wires), SI 18 (ropes GRINDWOOD), Ros 11 (WOOD -> METALSMOOTH / CONCSMOOTH); 2026-10-20 | Grind sound and sparks (local), kept the same on both sides | `terrain` -> `applyNodeEdits` (TerrainType; nodes.fixTerrains still checks every value against THUG2's table) | As TP-13 (the typo fix gives what our fixTerrains gave anyway: Tok / Shp unchanged in output) |
| TP-16 | THPS3 nodes THUGPRO took out, named by no THPS3 script or node: Shp `Box07` / `Box08`, SI's ocean sound objects `TRG_OceanNoise` / `01` (THUGPRO plays its own emitters there - included: TPN-THPS3-2 / 4 / 5), Ware's `TRG_CTF1_Restart_1P`..`4P` (TP-12 made them Generic; THUGPRO reused their places for its team restarts) and THPS3 genericnodes we drop anyway (Ware's zone nodes, Burn / Ros TRG_CTF_Team1 / 02); 2026-10-20 | A node on one side only | `removed` -> `applyNodeEdits` -> `convertNodeArray`'s hidden set (node and sector left out, as THPS3's hidden geometry) | As TP-13 (`nameUses`: 0x16 names in the level .qb and Data\Scripts; one still named is kept and noted) |
| TP-17 | THPS3 nodes THUGPRO renamed (a THUGPRO-only node at the place of a THPS3-only one, same class), the old name named by no script: none now - Tok's `TRG_Restart_Single` -> `TRG_Restart_1P` is kept (js_scripts.qb's CompWaypointStart names it; TP-12 adds THUGPRO's at the same place), Ware's `TRG_CTF1_Restart_3P` -> `TRG_CTF_Restart_Yellow` is TP-11 / TP-12's; 2026-10-20 | Names other scripts and the level editor look up | `renamed` -> `applyNodeEdits` (Name changed in place: array order kept) | As TP-13 |
| TP-18 | Rails THUGPRO added, as rail nodes from its node data (place, angles, TerrainType, flags, Cluster, Links by name): Foun's 8 wires `_TRG_Wire_Rail0`-`7` (TERRAIN_GRINDCABLE; their cluster `foun_wire` is THUGPRO's own mesh - not ours: with TPN-THPS3-1 the mesh is ours too and TrickObject / Cluster are kept; without it they are left out and the wires invisible), Rio's 23 (`QSilverBanner_01_Rail_0_*` on THPS3's banner, `rio_balcony*`, `rio_awning*`, `rio_roof_rail*`); 2026-10-20 | A rail one side can grind and the other can't | `rails` -> `thps3_levels.appendRails` (appended after the CTF nodes, before TP-12's ordering) | As TP-13. Not rails: Rio's `TRG_Node_2076`-`2083` are a ped's waypoint loop (THUGPRO's TRG_Ped_2084 - awaiting the user) |
| TP-19 | THPS3 collision THUGPRO edited: SI 8 objects (SI_Grating, _03, _05, _07, SI_ScaffBarrier2, SI_ShipCoping01, SI_VertRamp_01 / _05), Ros 4 (SkRos_Rail_Col06 / 07, SkRos_RailWall11, Radar02), Ware SkWare_Lamp01: the faces it took out - all duplicates (another face with the same corners) of faces it kept, 61 in all; SI's `Ramp_ReFlection`, `si_VRDirt01` / `02` drawn without collision (all their faces non-collidable anyway); 2026-10-20 | A wall or floor on one side only (a back face one side collides with) | `collision` -> `thps3_levels.collisionObjects` through `convertWorld`'s colEdits (a face matched by its three corners in turning order) | THUGPRO's installed `data\pre\<level>col.prx` / `scn.prx` against THPS3's own `<lvl>.bsp` (`collisionEdits`, as TP-13). Not edits: other face-count / bounding-box differences are its toolchain's (same faces, its face flag bits and bounding boxes over unused vertices) |
| TP-20 | THPS4's skate shop's load script `Load_practice` / `Level_practice` (its files stay `thps4_skateshop`) - approved by the user 2026-10-20 | As TP-1. THPS4's own name, `Load_SkateShop`, can't be used in THUG2 (THUG2's CheckForInvalidLevels turns it into the main menu; `levels\skateshop` is THUG2's shop); THUGPRO renamed it `practice` | `thps4_levels.json` `load_names` -> `thps4_levels.loadName` -> levels.json `load` -> `ported_levels` (Load_ / Level_ only) | THUGPRO's installed `data\pre\Practice_scripts.prx` and its Levels.qb `level_practice`; `tools/thps4_levels/thugpro_compare.ts` (436/436 nodes in place) |
| TP-21 | THPS4 skate shop's network-game nodes, THUGPRO's places: its King of the Hill crown (1) and its 8 added multiplayer spawns, plus THPS4's own TRG_SS_SkaterDropIn made a multiplayer spawn - approved by the user 2026-10-20 (an exception to the data rule: THPS4's shop, a single-player level, has none) | KOTH picks a crown by its index among the Crown nodes (CFunc_SpawnCrown 0x00538170): without one, a THUGPRO host's KOTH in the shop shows no crown here; the spawns: the same multiplayer start set (order and places) as THUGPRO's | `thps4_levels.json` `net_nodes` (places only: pos in THUG2's axes, angles) -> `thps4_levels.addNetNodes` (nodes in our form: Restart Type=Multiplayer TRG_Net_Restart01-08, GenericNode Crown TRG_KOTH01, all running the drop-in THPS4's practice start ran: `<level>_net_dropin`, MakeSkaterGoto DropIn). THPS4's menu restart TRG_SS_Skater01 stays a Player1 restart (not a ghost as THUGPRO's): with multiplayer restarts present it isn't made one, so the sets already match. Off: `--compat none` (the output then the same as before TP-21) | THUGPRO's installed `data\pre\Practice_scripts.prx` (`levels\practice\practice.qb`), extracted by `node tools/thps4_levels/thugpro_compare.ts --extract-thugpro` (never typed in); checked by `thugpro_compare.ts` (crowns same, mp restarts same) |
| TP-22 | THPS4 nodes THUGPRO moved or turned: College's rail nodes TRG_53 (131 units) and TRG_813 (59), its elevator doors LO_Elevator_Door_Floor1_01 / 02 (48, opened for THUGPRO's elevator teleport); Carnival's goal restarts TRG_G_HS_RestartNode (38.5, straightened) and TRG_G_PS_RestartNode (turned) and goats TRG_Goat / TRG_Goat01 (not in ours: animals aren't ported); Shipyard's kill-plane restart TRG_Re_Wtr_KillPlne_03 (turned). The user's decision 2026-10-20: everything THUGPRO changed in the register | A rail where one side grinds and the other doesn't (each side grinds its own rails while positions sync); the doors are collision | `thps4_levels.json` `node_moves` (pos in THUG2's Pos axes, angles) -> `thugpro_edits.applyEdits` (from `thps4_levels.cmdConvert`) | THUGPRO's installed `data\pre\<level>_scripts.prx` node arrays against THPS4's own `Game\data\levels\<Lvl>\<Lvl>.qb`, extracted by `node tools/thps4_levels/thugpro_compare.ts --extract-thugpro` (`thugpro_edits.extractEdits`; never typed in); checked by `thugpro_inventory.ts --thug2` ("ours with the profile") |
| TP-23 | THPS4 nodes THUGPRO renamed (same class, same place): Chicago's TRG_G_HighCombo_RestartNode -> TRG_G_PS_RestartNode (TRG_G_HighCombo_Pro -> TRG_G_PS_Pro: a goal ped, not in ours). Applied only where nothing of ours (node values, the level's scripts, kill scripts) still names the old one | Node names don't travel; a goal restart. For completeness (the user's decision, as TP-22) | `node_renames` -> `applyEdits` | As TP-22 |
| TP-24 | Present-at-the-start and network flags THUGPRO changed on restarts, rails, level geometry and objects (CreatedAtStart, AbsentInNetGames, NetEnabled, Permanent): Shipyard's 68 crane-track rails (TRG_Rl_Crne_B_Trk_*, Bucky_Rl_*) lose AbsentInNetGames and 6 sound / breakaway pieces lose NetEnabled / Permanent; CreatedAtStart off: Zoo 11 (hippo / giraffe / rhino collision, lion gate, tram sign, hippo bridge down - and on: hippo bridge up), Carnival 10 + 7 (goal triggers, shooting gallery, log stand...), London's 4 garage elevator doors and 2 fountain spits (on: the garage elevator), San Francisco's seal ladder (on: 7 gap triggers), Chicago's 5 hydrant rails and 2 trash cans, Alcatraz's 2 froth pieces, College's 14 rails (TRG_820-828...) and OffColl04 (and DT_tennisBall loses AbsentInNetGames, dt_blimp gains it), the skate shop's SSO_Fence02 / Cab_sign_1_College (off) and SSO_Fence05 / Cab_sign_11_Locked (on) | What is there at the start and online: geometry and rails present on one side only. Zoo's hippo bridge: THUGPRO's default state (its host option, not applied, moves it) | `node_flags` -> `applyEdits` | As TP-22 |
| TP-25 | TrickObject (with its Cluster) on College's 6 pieces (Sk3Ed_Gb_20x11 / 14 / 15 / 16, Box03, apm_bikerack_06) and its 6 stair rails TRG_Rail_Stair_142... (Cluster apm_bikerack_06) | Graffiti / trick attack: the tagged objects and their scoring agree | `trick_objects` -> `applyEdits` | As TP-22 |
| TP-26 | Rail grind terrains THUGPRO changed (TerrainType, and Type): Alcatraz 36, Chicago 11, Kona 41, London 164, San Francisco 54, College 54 rails (metal -> grind wire / electric wire / rope, concrete -> grind wire / metal / wood) - per rail, not a mapping (the other rails of the same terrain kept theirs) | Sounds and sparks only; for the user's decision as TP-22 | `rail_terrains` -> `applyEdits` | As TP-22 |
| TP-27 | Nodes THUGPRO took out (Zoo's meerkat TRG_meerkat_10: not in ours, animals aren't ported) and nodes it added that are node data only: rails - College 6 (TRG_TP_RailRoofLedge_01-06), skate shop 16 (roof_ledge_9-17, fence_rail_0_6-12), San Francisco 12 (ed_rail_*, back_qp_lips_*), Zoo 5 (TRG_NatasNode_01-05); generic restarts - San Francisco TRG_Elev_Warp2 (its elevator teleport's target), Zoo TRG_ParkingLotRestart01-03; Kona's 19 level lights TRG_Night_LevelLight_thugpro_* (Brightness 0 as THUGPRO's: lit only by its night lighting) | Rails only one side can grind; restarts and lights don't travel | `removed_nodes` / `added_nodes` -> `applyEdits` (in our node form: legacy Position, rails' zero Angles left off, Links by name, restarts `restart_types [Generic]`, after the converted nodes and before the peds and cars) | As TP-22; the added rails checked against our collision (0-18 units from a face but San Francisco's single-node ed_rail_4_0, 50) |
| TP-28 | College's collision: 607 duplicate faces (the same three corners twice) of 69 objects left out, as THUGPRO's (e.g. Box331 24 -> 12) | The same surfaces: nothing changes online; for completeness | `collision_duplicates` (object names) -> `thugpro_edits.removeDuplicateFaces` (THPS4's collision before converting) | THUGPRO's `data\pre\schcol.prx` against THPS4's `Schcol.dat` (an object is listed when its face count less the duplicates is THUGPRO's) - as TP-22; checked: the 69 objects' face counts equal THUGPRO's |
| TP-29 | **Off, the user's decision (2026-10-20): the door stays open** - the skate shop is our main menu's backdrop (`door_walls.enabled` false). THPS4's invisible skate shop door walls kept closed, as THUGPRO keeps them (InvisColl01's shop side, Object10's two invisible faces), only if switched on (the door open: our port change PC-THPS4-1 / PC-THPS4-2, 2026-10-19) | Online the door is open for ours, closed for THUGPRO's players (they run into a wall where ours ride through) | `door_walls` (`enabled`) -> `thps4_levels.openWalls` | THUGPRO's `practicecol.prx` (InvisColl01 10 faces, Object10's 2 - `thugpro_inventory.ts`) |
| TP-30 | THUG1's Hotter Than Hell named `hh` (Load_hh, levels\hh, hh_sky) - approved by the user 2026-10-20 | As TP-1: THUG1 names it SE (Load_SE), but THUG2 has a level SE of its own (Pro Skater), so the THUG1 port can't take THUG1's name; THUGPRO chose `hh` (its own identifier, in no THUG1 / THUG2 file). Without the entry it's `thug1_se` and doesn't load across | `thug1_levels.json` `level_names` -> `thug1_levels.levelName` | THUGPRO's installed `data\pre\HH_scripts.prx` and its Levels.qb (`Level_HH`, `load_script=Load_HH`, `name="Hotter Than Hell"`); checked by `tools/thug1_levels/thugpro_compare.ts` |
| TP-31 | THUG1 levels' network spawns as THUGPRO's: every restart with a Multiplayer / Horse / CTF / Team type in THUGPRO's places, types, names and order - VC TRG_restart_SCJ (moved 326 onto TRG_Restart_SJ's spot and made the first spawn), HN TRG_Restart01 (43), SC2 TRG_CTF_Restart_Yellow (165), SJ TRG_G_Collect2_RestartNode (Multiplayer / Horse taken off: 10 -> 9), HI TRG_Proset1_restartnode renamed `_trg_Proset1_restartnode`; all 12 shared levels in THUG1's own order (NJ: no Player1-first swap) - approved by the user 2026-10-20 | Competitive fairness: THUG2 starts player n at the n-th Multiplayer restart of the node array and respawns at one of the first 8 (Skate 0x00566100 -> GetRestartNode 0x0046B790; CSkater::MoveToRandomRestart 0x00580830, Rnd(8)) - a player shouldn't start where the opponents never do | `thug1_levels.json` `spawns` -> `thug1_levels.applySpawns` (in our node form: Pos / Angles / restart_types / Name set, the spawns' array slots refilled in `order`, Links following; the level's array keeps THUG1's order - `nodes.convertNodes` player1First false). Off: `--compat none` (the output then the same as before TP-31: checked, .qb code + sorted names) | THUGPRO's installed `data\pre\<level>_scripts.prx` node arrays against THUG1's own, extracted by `node tools/thug1_levels/thugpro_compare.ts --extract-thugpro` (`thugpro_edits.spawnEdits`; never typed in); checked by `thugpro_compare.ts` ("spawns same" in all 12) |
| TP-32 | THUG1 nodes THUGPRO moved (not the spawns - TP-31): NJ's El-train path waypoints TRG_TrainTest_00-03 (46-85) and LO_Fluff_Chopper (2000 down), HI Chopper_Hard_Rotors (16), RU tkr_cannonRail01 / 02 (1.3-1.8: the cannon grind line) - 8 nodes. The user's decision 2026-10-20 (TP-32 ... TP-39): everything THUGPRO changed in our ported levels is data here, under the compat flag | Positions are what both sides skate and grind on: a rail moved on one side only is a grind line the other side's player doesn't have | `thug1_levels.json` `moved_nodes` -> `hand_edits.applyNodeEdits` (Pos / Angles set on THUG1's node before conversion) | All of TP-32 ... TP-39: THUGPRO's installed `data\pre\<level>_scripts.prx` node arrays and `<level>col.prx` collision against THUG1's own `<LV>.pre` / `<LV>col.pre`, extracted by `node tools/thug1_levels/thugpro_compare.ts --extract-thugpro` (`thugpro_edits.handEdits`; never typed in; it prints what it leaves out and why); checked after conversion by `thugpro_compare.ts` ("hand edits as THUGPRO's: TP-n same/total" per level, `--details` the misses) |
| TP-33 | AbsentInNetGames as THUGPRO set it: taken off 1006 nodes - SD's demo skate area (67 geometry, 240 rails, 36 race markers, the intro kart), VC 350 (g_sj_detail_*, park trees), NJ 189 (182 geometry - the LO gates... - and 7 objects), RU 105, NY 12, HI 5 (the chopper, its rotors, 3 death planes), SC2 Gym_Doors01; put on 16 (HI's 15 goal pick-ups, NJ's TRGP_Vis_Playground_Medium) | Geometry and rails present in a network game on one side only: solid / grindable for one player, nothing for the other | `thug1_levels.json` `net_presence` (`present` = taken off, `absent` = put on) -> `hand_edits.applyNodeEdits`. Our converter's own network-game safety marks (`nodes.absentInNetGames`: peds, vehicles, path followers) still apply after it | As TP-32 |
| TP-34 | CreatedAtStart as THUGPRO set it: off on 64 nodes in 11 levels (SC2 13 objects - whirly vents, trash cans, cones, boxes, gym doors, bells; VC trp_death_SJ, a kill trigger; NJ NJ_Small_SouthEast07 / IC_StationBlock, 12 train doors, a rail, the skitch node, a vis proxim; HI GOLF_BALL_START / HI_ElevatorDoor / golfbldg2 / godrays; RU 4 occluders; HN mountains / trees / GeoSphere01 / the cursor killer; HH 4 objects and 3 vehicles; NY a rail and a sound box; every level's secret tape and Old Skool icon), on for 7 (RU's race4 ladders: 4 climbing nodes + 2 geometry; VC's leaf blower) | Created at start = there in free skate and net games: solid geometry and kill triggers on one side only differ in play | `thug1_levels.json` `created_at_start` (`on` / `off`) -> `hand_edits.applyNodeEdits` | As TP-32 |
| TP-35 | Other node values THUGPRO changed: classes (NJ's two dogs, the skitch node and a vis proxim made invisible GameObjects), rail and waypoint types / terrains (NY 359 rails, SD 17, SC2 25 rails + 59 waypoints, HI 20, NJ 5, VN 29 waypoints), clusters (SD's demo rails / geometry made trick objects with clusters: 123; NY 8 PROSET1 rails), other flags and values (NY IgnoreTOD 7, NJ NeverSuspend 2, RU occluders' Occluder off, RU godrays' times of day, HI tiki sound types 3, HI doors' CollisionMode 4, HI ped waypoint SkateAction) - 665 nodes in 7 levels (NJ 12, NY 366, SD 140, HI 28, RU 6, VN 29, SC2 84) | Rail types and terrains are the grind sound (local); trick objects / clusters count for trick attack and graffiti; the rest local. Taken whole so that both sides' levels are the same data | `thug1_levels.json` `node_values` ({node: {key: value or null, "+Flag" / "-Flag": true}}) -> `hand_edits.applyNodeEdits`. Not taken (`--extract-thugpro` lists them): peds' settings (THPS5 -> THPS6 skeletons as ours; profiles, AI), goal models set to "none", trigger scripts taken off (goals) or of THUGPRO's own, rigid-body settings dropped, SelectRenderOnly misspelt sRenderOnly, NY's 5 terrains written as flags, NY's 11 TERRAIN_GRINDRUSTYRAIL (not a THUG2 terrain - `nodes.fixTerrains`) | As TP-32 |
| TP-36 | Rails THUGPRO added, from their node data alone (positions, terrain, flags, cluster, links): NJ 20 ed_rail (+ THUG1's unlinked TRG_rail_e_midhouse4_12 reused on the clinic roof: moved 4468, retyped, relinked from TRG_rail_e_clinic_24), NY 11 ed_rail (+ 1 relink), RU 17 Natas_Alley_Barrier / natas_streetlamps, VN 3 and SC2 11 Natas spin points; HI TRG_Ped_SkaterNav127's link (a ped path) - 62 rails, 4 links | Grindable for both or neither: a THUGPRO-only rail makes its players grind on nothing in our game | `thug1_levels.json` `rails` (`added`: nodes in our JSON value form, appended to THUG1's array, Links by name; `reused`; `links`) -> `hand_edits.applyNodeEdits` | As TP-32 |
| TP-37 | Per-face collision values THUGPRO changed, matched to THUG1's faces by object and face (index where both have the same faces, else vertex places): wall-ride flag 0x4 on SD 90, VC 12, RU 238, HN 74 faces; non-collidable 0x10 on RU's 2; face terrain on NY 39 and SC2 108 faces | THUGPRO's players wallride walls ours can't; terrain is the skating sound / particles | `thug1_levels.json` `face_values` ({object: {faces, set / clear: {bit: [faces]}, terrain: [[face, terrain]]}}) -> `hand_edits.applyColEdits` (on the converted collision; the object's face count is checked) | As TP-32 |
| TP-38 | Collision THUGPRO took out: HH's 81 prop objects (1915 faces: sodas, napkins, trays, dt_choptail), SC2's 8 shadow objects (47 faces) and 4 faces of Qtr_Pipes29 (a quarterpipe's back) | Solid on one side only: ours hit what THUGPRO's pass through | `thug1_levels.json` `collision_removed` ({object: {faces, remove: "all" or [faces]}}) -> `hand_edits.applyColEdits` (objects dropped; faces dropped and the object's BSP tree and reaches rebuilt). Not taken: THUGPRO's exporter dropping empty objects (NY 639, RU 462, SC2 136, HN 109, HH 30) and zero-area / duplicate faces (HN 31) - no effect | As TP-32 |
| TP-39 | Collision THUGPRO reshaped (vertices moved): HN Inviso_SnowBlocker (17 vertices, up to 956 units - an invisible ceiling), HH SE_JO_CatWalk41 (1 vertex, 4 units) | As TP-38: where a skater collides | `thug1_levels.json` `collision_reshaped` ({object: {faces, verts: [[vertex, x, y, z]]}}) -> `hand_edits.applyColEdits` (tree and reaches rebuilt) | As TP-32 |


Not entries here, although THUGPRO does the same (they're right for THUG2 on their own):
- **THUG1's own level names** for the other THUG1 ports (nj, ny, fl, sd, hi, vc, sj, ru, vn, hn, sc2: Load_NJ ... Load_hn,
  Load_sc2): THUG1's Levels.qb names them so (`thug1_levels.ownName`); a THUG1 name THUG2 already uses keeps the `thug1_`
  prefix (SC, DJ, PH, TestLevel - THUGPRO plays THUG2's own SC / PH / DJ there), as does a level THUG1 doesn't load by
  its own name (NJ_Skateshop, the front end's skateshop - THUGPRO's Load_Mainmenu, which is THUG2's front end here).
- **The Zone_Origin frame** (each THAW area moved by minus its `<zone>_Zone_Origin` node): THAW's own data, keeps the
  areas near the origin (precision, the network's +-16,384 range). Our own change, PC-THAW-1 (port_changes, below); the idea came from THUGPRO's
  toolchain (credited in `thaw_levels.ts`).
- **Game-mode nodes without the zone prefix** (`TRG_CTF_*`, `TRG_Flag_*`, `TRG_KOTH*`, `TRG_Team_Restart`): THUG2's
  own scripts look them up by those names (`thaw_levels.multiplayerNames`).
- **THPS4's level names** (`alc`, `cnv`, `hof`, `jnk`, `kon`, `lon`, `sf2`, `sch`, `zoo`, `motox`): THPS4's own
  (its Levels.qb's `Load_Alc`..., checksums ignore case), `thps4_levels.levelName`; THUGPRO uses the same. THPS4's
  frame and its game-mode nodes (`TRG_CTF_*`, `TRG_Flag_*`, `TRG_KOTH*`) need nothing: THPS4 has them under THUG2's
  names already (`tools/thps4_levels/thugpro_compare.ts`: every shared node in place but THUGPRO's 1-4 hand moves).

## New in THUGPRO (not in any source game): `thugpro_new` - THAW

The user's decision (2026-10-20, Morten1337 agreeing): THUGPRO's own content goes into our THAW ports under the default
`--compat thugpro`, kept in ONE separate section, `thaw_levels.json` `thugpro_new`, apart from the hand edits to THAW's
data (TP-5 ... TP-9). **By reference only**: an entry names THUGPRO's item (archive entry, sector / collision object /
node / script name) and a CRC-32 of it; the converter reads the bytes from the user's THUGPRO install at convert time
(`--thugpro <dir>`, default `%LOCALAPPDATA%\THUG Pro`). A missing install, a missing item or a CRC that differs: a
warning and that item skipped - conversion never fails because of it. Nothing of THUGPRO's is in the repo. Written by
`node tools/thaw_levels/thugpro_compare.ts --extract-thugpro` (`thugpro_extract.ts`), applied by `thugpro_new.ts`.
Off: `--no-thugpro-new` (THUGPRO's hand edits only - the output as before this section) or `--compat none`.

The two newer sections (`thugpro_new`, `port_changes`) give every entry an `op`: **add** (something the source hasn't),
**set** (change fields of something that exists - only the given ones), **remove** (take out or suppress something that
exists); plus `id`, `target`, `why`. `set` / `remove` on something missing is an error the converter reports (and
skips), never an add. A rule for many things may be one entry (`target` describes it). The older TP-1 ... TP-39
sections keep their own form.

| Id | What | Why | Applied in | Source |
|---|---|---|---|---|
| TPN-THAW-1 | THUGPRO's new meshes and collision (`op` add): 31 sectors, 32 collision objects - Minneapolis' parking garage `z_dn_pg_*` (8 sectors, 12 collision objects, with `Z_DN_dn_tw_light_bldg05`'s collision), Kyoto's DDR machine `TP_DDR_Machine` (TP-8's 3 DDR rails now on it), `geo_CF_anotherbldf_enviro`, `ja_occluder30`; The Mall's 3 light glows and a shadow; Vans' 2 sauce machines; Casino's `LV_Elevator_2ndFloor`; Santa Monica's 13 shadows; Beverly Hills' 2 plants; Chicago's `SkVans_Sign08_LOD` collision | THUGPRO players skate and collide with them: without them they ride on air here and ours pass through theirs | `thugpro_new` `<zone>.entries` kind sector / collision -> `thaw_levels.newCollision` (into THAW's collision before converting, back in THAW's frame, a new BSP tree and reaches each) and `thugpro_new.mergeScene` (into the converted scene; materials ours where the checksum is ours, else THUGPRO's with its textures - a texture checksum ours uses for other pixels gets `thugpro_new_<hex>`) | THUGPRO's `data\pre\<level>scn.prx` / `col.prx` (only `levels\<level>\<level>.*`: the scn.prx also carries its sky) |
| TPN-THAW-2 | THAW meshes THUGPRO reshaped (`op` set: ours replaced by THUGPRO's; `remove` where it emptied one): 51 sectors, 46 collision objects, 1 removed (Hollywood's `Z_HO_Box04`) - e.g. Minneapolis' cars' collision, Vans' machines, Hollywood's signs and fire escape, Downtown's 99Cent_02 and Objects_Mesh23 (their nodes already moved by TP-5), the trigger planes of the teleports, Casino's elevator and backdrop, Santa Monica's carousel | Where a skater collides: the same shapes on both sides | as TPN-THAW-1 (a `set` only where the conversion has ours: a sector we leave out stays out) | as TPN-THAW-1 |
| TPN-THAW-3 | THUGPRO's content nodes (`op` add): 58 - the new meshes' nodes (garage, DDR machine, glows, shadows, plants, elevator floor), Kyoto's 4 cars and 10 peds, Santa Cruz's 3 cars, Kyoto's 3 sound proximity nodes | The nodes that make the meshes exist (CreatedAtStart, collision mode, trick objects) and the level's life | appended to THAW's array before converting (`thaw_levels.addNewNodes`: Pos back in THAW's frame, Links by name), then made as THAW's are - cars and peds our port's way (THUG2's ped AI, Car_Generic01; THUGPRO's car script read to tell a driving car: `PortedNodes.drivers`) | THUGPRO's `data\pre\<level>_scripts.prx` node array (compressed nodes expanded) |
| TPN-THAW-4 | THUGPRO's scripts (`op` add, or set: replacing our translation of the THAW script of that name) and the TriggerScripts naming them (`op` set): Chicago's 8 CTF / team-flag scripts (Team_Flag), Vans' 4 CTF bases (Team_Flag_Base), Santa Cruz's 8 parking meters (with its SZ_Natas_Meters_Check, SZ_BustedMeterEffect), East LA's coin08 (with its COIN, THUGPRO_SafeKill), The Mall's door sound (MA_Dooropening), East LA's glass that breaks the second pane silently (6 trigger scripts, EL_GoldlineGlass) - 37 scripts, 21 TriggerScripts | Behaviour as THUGPRO's (names don't travel; CTF bases and flags work the same) | appended to the level's .qb as THUGPRO's compiled bytes (`script_port.appendScripts`; ours of the same name taken out first, `thugpro_new.removeScripts`); never one THUG2 defines. A script naming a node ours hasn't (`name=X` or `X:` - on a missing object a statement runs on the skater) is left out at convert time and reported | THUGPRO's level `.qb` files and `thugpro_qb.prx`; each root's THUGPRO dependencies found by `--extract-thugpro` |
| TPN-THAW-5 | THUGPRO's host level options (`op` add, kind level_option): their toggle scripts `<level>_LEVEL_CHANGE_0n` (Minneapolis 2 - its garage on by default -, The Mall 1, Santa Cruz 1, East LA 6, Oil Rig 2) and the level's `<zone>_LoadingSounds` (11 levels) | THUGPRO's state after loading: its gameflow runs HandleLevelOptionObjects, which calls each toggle `on` / `off` by its SERVER_LEVEL_CHANGE_0n_ON flag (off unless the level's setup sets it); switching them is THUGPRO's host setting in its messages 155-159 - a later protocol layer | a setup script `<level>_thugpro_setup` (the level structure's `setup_script`, run by load_level and reset_level) calling the loading sounds and each toggle with its default | as TPN-THAW-4 |

Left out of `thugpro_new` (listed per area in `left_out`, with the reason): `Cylinder01` (THAW's own sky dome from
THUGPRO's bundled `thaw_sky.scn.xbx` - ours converts THAW's sky itself); the peds' scripts (Minneapolis' 30 and Kyoto's
4 life nodes' Ped_Stand_Script, Kyoto's peds' Ped_Outside_* - our port makes its peds its own way); East LA's cop car and
Oil Rig's ferry (not there at the start - our port makes only the cars that are; so their models `veh_bh_cop` /
`veh_ferry` aren't needed); Minneapolis' 21 cones / signs / chairs (LevelObjects without a scene sector in THUGPRO's
level or ours); the TriggerScripts of nodes our port leaves out (Downtown's dog, Santa Monica's 2 performers); THUGPRO's
banners (`<level>_LevelChanges` in `_thugpro.qb`: its own branding, placed by its run-time dynamic objects) and its
time-of-day scripts (compiled without names, driven by its TOD state); and every script that needs a command THUG2
hasn't (`_needs_cpp`): **TriggerType** (THUGPRO's C++: about 100 gap scripts in CH, DN, EL, HO, SV, DT, LV, SR, SV2, SZ,
JA gated by GetTriggerArray / TriggerType - e.g. Chicago's), and THAW commands THUGPRO implemented
(CinematicCamera_DoMorph / _Pan, PlayIGCCamScene, IGC_Camera_MoveToPlayer, SpawnScriptLater / SpawnScriptNow,
IsCreated, ChangeNodeFlag, DeleteGapFromScore, StickerOn, GoalManager_SetFloatTipVisibility). Proposed: our own
implementations in this project's C++ style (src/game/script), with a comment crediting THUGPRO's behaviour - never a
copy; TriggerType first (it gates gap scoring), reading the skater's last trigger kind as THUGPRO's GetTriggerArray
encodes it.

## Our own changes (port_changes) - THAW

`thaw_levels.json` `port_changes` `entries`: what this project changes in THAW's levels that is neither THAW's data
nor THUGPRO's. Applied whatever the profile (also `--compat none`); `--no-port-changes` leaves out all but the
`required` ones (the level needs them to load); each entry's `switch` is what `thaw_levels.ts` checks (`portChange`).
Read through `compat.portChanges`. (The Mall's `Z_MA_Mall_First_Rail_Blocker` was left out here until 2026-10-20: the
user restored it - THAW and THUGPRO keep it; there is no entry for it.)

| Id | op | Required | What | Why |
|---|---|---|---|---|
| PC-THAW-1 | set | | Every area with a `<zone>_Zone_Origin` node moved by minus its Pos (whole sixteenths): nodes, particles, scene, collision (`frameShift`; THUGPRO's frame is `thugproFrame`) | Areas near the origin: float precision, the network's +-16,384 range. Off: THAW's own world frame (then TP-2 / TP-3 change nothing) |
| PC-THAW-2 | remove | | Night-only sectors (`*NightOn*`, `*_tod_light*`) | THAW shows them only at night; the ports are lit as by day |
| PC-THAW-3 | remove | | LOD copies (the sectors a node names as LOD_Name) | THUG2 would draw both copies |
| PC-THAW-4 | set | | Breakables as THUG2 shatters: a geometry node whose TriggerScript shatters it gets PlaySound + Shatter + Kill, non-collidable trigger faces, no Type (`breakableScripts`) | Glass skated through as in THAW. **Under audit** (not changed here): it assumes ShatterAndDie is THAW-only, but THUG2 has ShatterAndDie (SK4_Scripts.qb) and BloodSplat (bails.qb); it also drops MA_KillStickers, so The Mall's "closed / cerrado" sticker floats after its glass breaks |
| PC-THAW-5 | remove | yes | TrickObject / Cluster off geometry nodes without a scene sector | ParseNodeArray 0x00588630 reads through the missing sector: a crash |
| PC-THAW-6 | set | | Restarts over nothing or a kill zone: no network spawn types, no kill targets (`nodes.groundRestarts`) | A spawn there falls for ever or dies at once |
| PC-THAW-7 | remove | yes | Collision trigger flags but on kill zones, breakables and geometry whose script we bring | THUG2 runs a trigger face's TriggerScript without checking it exists: a crash |
| PC-THAW-8 | remove | yes | A pass blending by the previous pass's alpha after another such pass (blend 12 / 13) | THUG2's pixel shader doesn't assemble: Santa Monica crashed loading |
| PC-THAW-9 | remove | yes | Billboard sectors | Their billboard data isn't converted: THUG2 can't draw them |
| PC-THAW-10 | remove | yes | Face flags 0x2000-0x8000, pass flags THUG2 hasn't the data for, blend modes past 13 (made 5) | Not THUG2's meanings |
| PC-THAW-11 | set | yes | Path followers, peds and cars AbsentInNetGames (`nodes.netGameNodes`) | No path manager / ped animations in network games |
| PC-THAW-12 | set | yes | Peds, cars, objects, particles, game-mode flags made THUG2's way; goal / story / animal peds, cars not there at the start, objects without a sector left out (`thaw_scripts.PortedNodes`) | THAW's components and scripts don't exist in THUG2 |

`--no-port-changes` switches off PC-THAW-1, -2, -3, -4 and -6; the required ones stay (their code isn't switched).

Left as our port makes it (not THUGPRO's choice, and matching it isn't safe or possible - `thugpro_inventory.ts --ours`
lists them under "our port's own"): TrickObject / Cluster off 365 geometry nodes without a scene sector (THUGPRO keeps
them; ParseNodeArray 0x00588630 reads through the missing sector - nodes.stripMissingTrickObjects; 10 of them TP-6's
Chicago additions), the breakable glass faces, CollisionMode off objects without collision; and values our port
doesn't carry because it makes those nodes its own way (lod_dist / SuspendDistance of peds and cars,
CreatedFromVariable, lightgroup, particle settings - listed in `awaiting` notes).

THPS3's THUGPRO-authored content (once awaiting the user) is included since 2026-10-20, by reference: "New in THUGPRO
(not in any source game)" below, rows TPN-THPS3-1 ... 6.

THPS4: THUGPRO's own content (teleports, host level options, kill scripts, models...) is now included, by reference:
`thugpro_new` (TPN-THPS4-1 ... TPN-THPS4-5 below, "New in THUGPRO"). Still left out: the skate shop's host option
(Open Fence: its default state force-creates the door walls - our main menu's backdrop keeps its door open, TP-29 off)
and its menu restarts made ghosts (TP-21 keeps TRG_SS_Skater01 our free-skate start); THUGPRO's level-options menu
data (`<LVL>_option_items`, the proset names and cameras).
Toolchain translation, not hand edits (not applied): peds / animals / particle emitters made game objects, trigger
scripts dropped, Pos for Position, physics keys, lod distances, face-flag bits.

THUG1: THUGPRO's own content (the El train, the boat, the arrows, the gulls and bums, its scripts and host-toggled
files) is now included by reference - "New in THUGPRO (not in any source game)" above (TPN-THUG1-1 ... 4).

## New in THUGPRO (not in any source game): `thugpro_new` - THUG1

Content THUGPRO made itself that is in no source game's level (THUG1's, nor THUG2's for the same place): its models,
nodes, level scripts and host-toggled level files. Morten agreed to the user that our ports may include it
(2026-10-20); the user's decision: all of it in one separate section, so it's easily identifiable. **By reference
only**: the section holds references (the file in THUGPRO's install, the node / script name) and crc32 checksums, never
THUGPRO's bytes; the converter reads each item from the user's THUGPRO install (`--thugpro`, default
`%LOCALAPPDATA%\THUG Pro`) when converting. No install, a missing file or a checksum that differs: a warning, the item
left out, never a failure. Each entry has `id`, `op` (`add` creates what the source hasn't, `set` changes the given
fields of what exists - on something missing an error the converter reports, never an add -, `remove` suppresses what
exists), `target`, `why`, `crc32`. (These two new sections use `op`; TP-1 ... TP-39 keep their own forms.) Off:
`--no-thugpro-new` (the hand edits TP-30 ... TP-39 stay) or `--compat none`. Extracted, never typed in:
`node tools/thug1_levels/thugpro_compare.ts --extract-thugpro --thug2 <workspace> --staging <converted>\levels`
(against a level converted with the section: the script edits of scripts ours leaves out aren't listed - it's a fixed
point, run once more to check). Form: compat.Entry ("The two sections with `op`" below) - one `entries` list, each
target {level, name}, plus `kind` (asset / node / script / file) and `crc32`; `levels` gives each level's THUGPRO key
and archive. Code: `tools/thug1_levels/thugpro_new.ts`.

| Id | What (THUG1 ports; counts from the data) | Applied | Check |
|---|---|---|---|
| TPN-THUG1-1 | Models THUG2 hasn't, THUGPRO ships (op add, 27 file entries): NJ veh_El_Train, VC / SJ transition_arrow, VC veh_LeafBlower (THUG1's leaf blower comes back: our converter left it out for its model), RU THPS2_Case (the Old Skool icon) and veh_tank_ru (THUG1's tanks come back), NY / VN Skater_OllieTheMagicBum (THUGPRO's loose `data\models\skater_secret`) | Written to `<staging>\models\`, installed into the overlay's models\ by make_menu_level (`ported_levels.installFiles`); the converter then keeps the THUG1 objects using them | `thugpro_compare.ts`: "new in THUGPRO as THUGPRO's" (file there, THUGPRO's bytes) |
| TPN-THUG1-2 | THUGPRO's nodes (op add: NJ's 2 El trains TRG_TrainA01 / 02, HI's boat TRG_HI_Boat01 - THUG2's own BA boat model -, VC TRG_SCJ_arrow_01 + its restart TRG_Restart_SJVC, SJ TRG_VC_arrow_01 + waypoint TRG_Restart_VCSJ, VN's 3 gulls - THUG2's AU seagull; op set: VN's 5 bums - TRG_546 renamed TRG_BUM_01 - and NY's 3 bum peds on the bum model, FL's gator (gorilla model) and goat) | Added to / set in THUG1's node array before conversion (after TP-32 ... TP-36), kept whole by the node pass (`thug1_port.NodePass` forced: THUG2 nodes already - the animal ones too) | Node there with THUGPRO's values |
| TPN-THUG1-3 | THUGPRO's level scripts (op add 299 - its own, incl. the trains', boat's, arrows', gulls', bums' scripts and `Sk3_Killskater_sand` from `qb\thugpro\thugpro_utils.qb`; op set 604 - changed scripts: VN's 58 sand kill zones now `Sk3_Killskater_sand`, rewritten gap checks with GetTriggerArray / TriggerType, ped / car / sound parameters; op remove 130 - emptied: NJ's 69 occlusion / vis / proximity triggers of its 85, RU's tank scripts...) | Added to / replaced / emptied in the compiled .qb, each only when it can run here (`thugpro_new.scriptCheck`: every command THUG2 or the level has, no THUG1 goal / flag command, every node it names in our level, no undrawn geometry); refused ones keep ours (2 at conversion: SJ_startup, RU_Cannon; ~30 at extraction - the VC <-> SJ `LoadZone` scripts among them) | Script bytes THUGPRO's / emptied |
| TPN-THUG1-4 | THUGPRO's host-toggled level files `levels\<level>\<level>_thugpro.qb` (op add: NJ, NY, FL, SD, HI, VC, SC2): option lists and toggle scripts behind its SERVER_LEVEL_CHANGE flags | Copied as they are; nothing runs them (THUGPRO's protocol messages 155-159 do, in its DLL) - so all off, THUGPRO's default. Switching needs a protocol layer of ours | File there |

Not taken (listed by the extraction): NJ / HI Old Skool icons and HI's 15 lei pick-ups - THUGPRO itself has
`model="none"` there and ships no model; NY's 11 TERRAIN_GRINDRUSTYRAIL rails - deliberately excluded, an unknown
terrain in THUG2 (reads through a null - the THPS3 Tokyo crash), ours keep THUG1's TERRAIN_GRINDMETAL; scripts calling
THUGPRO's own commands (LoadZone - the VC <-> SJ level change, so the arrows are decoration here -, its DLL's) or THUG1's
goals.

## Our own changes (port_changes) - THUG1

What our converters do to a source game's level that comes neither from the source game nor from THUGPRO - in each
game's file, section `port_changes` (THUG1: `thug1_levels.json`, ids PC-THUG1-n, read by
`tools/thug1_levels/port_changes.ts` through `compat.portChanges`). Applied whatever `--compat` says (they're ours, not
THUGPRO's); `--no-port-changes` switches off every entry that isn't `required` (required: needed for THUG2 to load the
level or to avoid a crash). Each entry: `id`, `op` (add / set / remove), `target` (a rule's description when it applies
to every node or script of a kind - here {levels, what}), `rule` (a short name), `why`, `required` - compat.Entry. Taking an entry out of the data switches it off; a required one
missing is an error.

| Id | Op | Target | Required |
|---|---|---|---|
| PC-THUG1-1 | add | kill scripts without nodename=: the restart nearest their first trigger node (THUG1 Hawaii's pool left the skater dead) | no |
| PC-THUG1-2 | set | levels without a Multiplayer restart (DJ, PH, SC...): Player1 restarts also Multiplayer / Horse | no |
| PC-THUG1-3 | set | the first Player1 restart moved to the array's start (not where TP-31 orders the spawns) | no |
| PC-THUG1-4 | set | DuplicateTrigger-only trigger scripts replaced by the linked node's | no |
| PC-THUG1-5 | set | restarts over nothing / over a kill zone: no network spawn; kills there sent to the nearest grounded restart | no |
| PC-THUG1-6 | remove | animal peds (crash 0x00482BA0) | yes |
| PC-THUG1-7 | remove | GenericNodes other than KOTH crowns (THUG1's goal nodes) | yes - **needs verification** (no crash proven) |
| PC-THUG1-8 | remove | vehicles / game objects whose model THUG2 hasn't or whose skeleton it doesn't load | yes |
| PC-THUG1-9 | remove | particles without a THUG2 texture, moving objects without a scene sector | yes |
| PC-THUG1-10 | set | peds: THPS6_human skeleton and animations, a THUG2 profile | yes |
| PC-THUG1-11 | remove | SoundType / AnimName / Extra_Anims THUG2 hasn't | yes |
| PC-THUG1-12 | remove | TrickObject / Cluster on climbing nodes and on geometry without a sector | yes |
| PC-THUG1-13 | set | rail terrains THUG2 hasn't | yes |
| PC-THUG1-14 | set | path followers, peds, cars AbsentInNetGames | yes |
| PC-THUG1-15 | remove | script statements THUG2 can't run, trigger scripts not brought | yes |
| PC-THUG1-16 | add | GetGap / Gap_CarHop gap scripts given; models' gap scripts defined empty | yes |
| PC-THUG1-17 | set | the node array's shared parts as global structures | yes |
| PC-THUG1-18 | add | restart_types from Type; kills as THUG2's SK3_KillSkater | yes |
| PC-THUG1-19 | set | thug1_<code> names where THUG2 has the level's name | yes |
| PC-THUG1-20 | remove | collision face flags 0x2000-0x8000, skinned scene sectors | yes |

## The two sections with `op`: `thugpro_new` and `port_changes`

Besides the TP-n sections above (grouped by kind, left as they are), a game's differences file can have two more
sections, both a list of entries (`entries`), each `{id, op, target, why, ...}`:
- `op` is one of `"add"` (something the source game hasn't: a node, sector, collision object, script), `"set"` (fields
  of something that exists change - merged, only the given fields; a `set` on something missing is an error the
  converter reports, never an add) or `"remove"` (something that exists left out or suppressed: faces, nodes, sectors,
  objects, statements). No other ops.
- `id`: the register row - `TPN-<GAME>-n` for `thugpro_new`, `PC-<GAME>-n` for `port_changes` (GAME: THPS3, THPS4,
  THUG1, THAW); `target`: what it acts on (its level: `target.level`, or `target.levels` - a list or `"*"`); `why`: one
  line. THPS4's entries also name the converter step that applies them (`rule`); THPS3's thugpro_new entries the item's `kind` (sector, collision, node, script, model) and `target.name`.
- `required` (port_changes): `true` for a change a level needs to load (or make_menu_level to install it) - the crash
  guards; `--no-port-changes` keeps those.
- Read through `tools/compat/compat.ts` (`thugproNew(profile, tool)`, `portChanges(tool)`).

| Section | What | Switch |
|---|---|---|
| `thugpro_new` | **New in THUGPRO (not in any source game)**: THUGPRO's own content - its scripts, nodes, teleports, host level options, models. By reference only - what is read from the user's THUGPRO install at convert time (`--thugpro`), never its bytes: THPS4 names file, entry, size and SHA-256; THPS3 archive, file, the item's name and a CRC-32 of the item | part of the THUGPRO profile: `--compat none` leaves it out, and `--no-thugpro-new` the profile without it |
| `port_changes` | **Our own changes**: this project's deliberate deviations from a source game's data - neither the source game's nor THUGPRO's (the skate shop's open door, crash guards, nodes and statements left out) | not THUGPRO data, so applied whatever `--compat` says (`--compat none` too); `--no-port-changes` leaves out all but the `required` ones: the source game's data as it is, but what a level needs to load |

## Our own changes (port_changes) - THPS4 and THPS3

Not THUGPRO entries: what this project changed in a source game's levels on purpose. THPS4
(`thps4_levels.json` `port_changes`, applied by `tools/thps4_levels/thps4_levels.ts` - `portChanges(rule, level)`):

| Id | Op | Required | Rule | Target | Why |
|---|---|---|---|---|---|
| PC-THPS4-1 | remove | no | `open_walls` | skate shop, collision InvisColl01, its z-min faces | The shop's door open (the user, 2026-10-19; our main menu's backdrop): the invisible box's shop-side face across the door |
| PC-THPS4-2 | remove | no | `open_walls` | skate shop, collision Object10, its invisible faces | The door open, as PC-THPS4-1: the shop's own two invisible faces across the doorway |
| PC-THPS4-3 | set | no | `created_in_play` | skate shop, node SSO_Fence05: CreatedAtStart | The fence piece at the end of the halfpipe there at the start (THPS4's menu killed it, its practice run made it again) |
| PC-THPS4-4 | remove | yes | `peds_cars` | College: the walkers and cars peds_cars.ts makes | Crash guard: College's node array came out null with them |
| PC-THPS4-5 | remove | yes | `zero_angles` | every level: Angles (0, 0, 0) on rails and level geometry | Crash guard: THUG2's fixed script vector pool (College's array came out null); zero is the default |
| PC-THPS4-6 | remove | yes | `never_created` | every level: made objects no brought script creates, waypoints nothing uses | Crash guard: THUG2's script heap (College) |
| PC-THPS4-7 | remove | yes | `trick_objects_without_sector` | every level: TrickObject / Cluster of geometry without a scene sector | Crash guard: ParseNodeArray 0x00588630 reads through the missing sector |
| PC-THPS4-8 | remove | no | `left_out_commands` | every level: 30 goal / career / camera / panel commands in brought scripts | Goals and THPS4's panels aren't ported (besides script_port's shared list) |
| PC-THPS4-9 | remove | yes | `empty_restart_scripts` | every level: restart trigger scripts left empty | THPS4's menu restart's script has nothing left; an empty script broke make_menu_level's parsing |
| PC-THPS4-10 | set | no | `ground_restarts` | every level: restarts over nothing / a kill zone | Not network spawns or kill targets (a player would fall forever) |
| PC-THPS4-11 | set | no | `net_game_nodes` | every level: path followers, peds, cars; single-player levels' Player1 restarts | Network games: no path manager or ped animations, and restarts for the players |

Checked: `--compat thugpro` and `--compat none` output byte-identical to before the move (75 files each; the .qb
files by code and sorted checksum names); `--no-port-changes` keeps PC-THPS4-4 / 5 / 6 / 7 / 9. Not entries (the converter's translation of THPS4's forms into THUG2's, not
a choice that could be switched back: the source has no THUG2 form to fall back on): THPS4's animals other than
pigeons, particle emitters, proximity nodes (their scripts run once at the start instead), goals and their pick-ups
and peds, trigger types as THUG2's arrays, MakeSkaterGoto kept for DropIn / StartSkating1 only, standing peds' THUG2
AI, vehicles as level objects of their models, the CTF / team flags' THUG2 form, the level names (the skate shop's
`thps4_` prefix), the shared script_port list of left-out commands; and THPS4's missing Foo scene.

### THPS3 (`thps3_levels.json` `port_changes`)

| Id | Op | What | Switchable |
|---|---|---|---|
| PC-THPS3-1 | remove | Warehouse's dark set (SkWare_Dark* sectors and nodes; `prefixes`, `target.levels`) | yes |
| PC-THPS3-2 | set | The start scripts run once from `<level>_setup` | required |
| PC-THPS3-3 | set | Kill zones as THUG2's SK3_KillSkater with a restart | required |
| PC-THPS3-4 | add | Multiplayer / Horse types on the Player1 restarts of single-player levels (`nodes.netRestarts`) | yes |
| PC-THPS3-5 | set | AbsentInNetGames on path followers, peds and cars | required |
| PC-THPS3-6 | set | TrickObject / Cluster off geometry without a scene sector | required |
| PC-THPS3-7 | set | Restarts over nothing or a kill zone: no spawn types, no kill target | yes |
| PC-THPS3-8 | add | Capture the flag made from THPS3's bases in THUG2's form | yes (kept while TP-10 ... TP-12 data is present) |
| PC-THPS3-9 | set | The Player1 restart first in the node array (levels without TP-12) | yes |
| PC-THPS3-10 | set | THPS3's traffic as THUG2's cars | required |
| PC-THPS3-11 | set | THPS3's peds and animals as THUG2's | required |
| PC-THPS3-12 | remove | Goal pickups, cars / birds not there at the start, objects without converted models, script-less proximity nodes | required |
| PC-THPS3-13 | remove | Script statements THUG2 can't run; trigger scripts not brought along | required |
| PC-THPS3-14 | remove | The Links of non-rail nodes | required |

Checked: the default output is unchanged by the move into data (every file); `--compat none --no-port-changes`
differs in the 13 levels' node arrays (and Warehouse's dark set back in its scene / collision).

## New in THUGPRO (not in any source game): `thugpro_new` - THPS4 and THPS3

THUGPRO's own content, in no source game - included in our levels since the user's decision of 2026-10-20 (Morten's
permission), **by reference only**: the section's `files` name THUGPRO's archive, the entry, its size and SHA-256; the
converter reads the bytes from the user's install (`--thugpro`, default %LOCALAPPDATA%\THUG Pro). No install or a
file that differs: a warning and that level's THUGPRO content left out. `--compat none` / `--no-thugpro-new` leave it
all out. Regenerate: `node tools/thps4_levels/thugpro_compare.ts --extract-thugpro`; check:
`node tools/thps4_levels/thugpro_inventory.ts --thug2 <folder>` ("ours with the profile").

THPS4 (`thps4_levels.json` `thugpro_new`, `tools/thps4_levels/thugpro_new.ts`, 70 entries, 32 files):

| Id | What | Entries (op / rule) | Online / notes |
|---|---|---|---|
| TPN-THPS4-1 | Teleports and elevators: College's elevator (SCH_teleport1 + its door open / close, 2 radius triggers), San Francisco's 2 elevators (SF2_teleport1 / 2, 2 radius triggers; TRG_Elev_Warp2 is TP-27's), London's 2 garage elevator door pairs (4 scripts, 2 radius triggers, the 4 doors made level objects, TRG_ParkingGarrage_Elevator moved by (0, 0, -25) and its televator with a jump boost to the added waypoint TRG_LON_Televator_Waypoint), Zoo's 4 televators (TRGP_Teleport01, TRGP_LeavingArea02, TRGP_TeleportZookeeper / 01); the sounds THUGPRO's level start-up loads for them (VC_ElevatorDing02, elevatording / dooropen / doorclose) | add script / proxim_object / node / sound; set trigger_script / level_object | Both sides teleport and open the same doors now. THUGPRO's helpers are ours: thps4_tp_televator (no THUGPRO_AutoKick: THUGPRO's own setting), thps4_tp_proxim_object (a ghost with Obj_SetInnerRadius / AnyObjectInRadius, as THUGPRO's dynamic level changes) |
| TPN-THPS4-2 | Host level options' scripts (`<LVL>_LEVEL_CHANGE_0n`): Chicago's bridge (5 + its 2 helpers), Shipyard's barge, London's race track and rooftop roll-in, San Francisco's rooftop objects, Zoo's hippo bridge - each run with `off` at the level's start: THUGPRO's default state (its HandleLevelOptionObjects with the host flags off) | add level_option, script | The default state matches. Another state needs THUGPRO's protocol (its messages 155-159 and the SERVER_LEVEL_CHANGE_0n_ON flags): C++ for later. The skate shop's option is left out (above) |
| TPN-THPS4-3 | London's kill zones: THUGPRO's TRGP_Lon_AdArchKill01 / WaterlooBridgeKill01 / Lon_AdArch_Roof / _Roof01 scripts calling its Lon_Killskater (ours had THPS4's Lon_LeavingMessage, emptied) | set trigger_script, add script | Both sides die there now |
| TPN-THPS4-4 | Models: Chicago's 5 and College's 13 banners (Big_Banner*), London's billboard (Lon_CanHouse_build_cw1) and gallery posters | add model (`shown_by_thugpro` false) | **THUGPRO doesn't show them**: its shipped scripts skip them (`thugpro_levelchanges_add_static_model` returns for replace_texture; the billboard branch is compiled out, GIVE_ME_BILLBOARDS). Placed only with `--thugpro-banners` (the files go to the staging's `models` folder) |
| TPN-THPS4-5 | The skate shop's ghost TRG_TP_SkaterKiller and its (empty) trigger script | add node, script | Inert in THUGPRO too (TP_SS_KillSkater is called by nothing: left out); not in the front end's backdrop (checked: 44 rails, 138 peds / cars, 3 trigger nodes + 6 scripts) |

### THPS3 (`thps3_levels.json` `thugpro_new`; `_left_out` lists what was found and not taken, with the reason)

| Id | What (op) | Applied in | Source |
|---|---|---|---|
| TPN-THPS3-1 | Meshes and collision (add): Foundry's `foun_wire` mesh (the 8 TP-18 wire rails' cluster: visible now, TrickObject / Cluster kept), its 3 `*_coll` quads and the bucket's `Obj_Bucket` collision; Rio's `ES_Building_collision`; Suburbia's 5 `*_collfix` objects (the picnic collider back) | `thugpro_new.loadNew` -> `applyGeometry` (in `convertWorld`: sectors with their materials - renumbered when ours uses the checksum - and textures; collision through `geometry.convertCol`, BSP trees and reaches made again: every face reachable) | THUGPRO's `data\pre\<level>scn.prx` / `col.prx`: sectors and collision objects that aren't THPS3's `.bsp` sectors |
| TPN-THPS3-2 | Nodes (add): those geometry nodes; Rio's ped `TRG_Ped_2084` and its waypoint loop `TRG_Node_2076`-`2083`, its 3 sound objects and the 8 cars' `*_carplant_collision` (CreatedAtStart added: THUGPRO creates them from its rewrites of the cars' scripts, which ours replaces with THUG2's cars); Suburbia's BBQ noise and the ice-cream truck's carplant; Skater Island's 2 ocean-wave emitters; Tokyo's `TRG_Restart_EventR` / `EventL`; Shipyard's waterslide sound; Warehouse's `Ware_RoomFlicker` | `applyNodes` (after our nodes, before TP-18's rails and TP-12's order; Links by name - a link to a node ours hasn't is dropped and reported) | THUGPRO's level `.qb` node array: names in no THPS3 node array and not made by TP-10 ... TP-18 |
| TPN-THPS3-3 | THPS3 objects made Vehicles with THUGPRO's model (set: Class, model, skeletonName; our drawn copy of THPS3's `.dff` left out): Foundry's `Obj_Bucket`; Suburbia's ice-cream truck and plane; Tokyo's 2 bullet trains, 3 planes, 18 cars (THUG2's own car models) | `applyNodes`; `cmdConvert` leaves the object's sector out | As TPN-THPS3-2: THUGPRO's Vehicles with a model whose THPS3 node isn't THPS3 traffic (ours makes those THUG2's cars - PC-THPS3-10) |
| TPN-THPS3-4 | Scripts (add): THUGPRO's own level scripts (sounds, carplants, ped, ocean waves, restarts, train / plane control, Shipyard's window counter...) that THUG2 can run - THUGPRO's DLL commands swapped where THUG2 has an equivalent (`replace_calls`: THUGPRO_ForceCreate -> Create, edmsg2 / Output -> printf) | `writeLevelQb` (appended to the compiled file; one whose name the file has already is left out and reported); the THPS3 scripts they call are brought along by the translator | THUGPRO's `levels\<level>\*.qb`: scripts in no THPS3 file, not THUG2's own, not names ours makes (`load_level_anims`, `<level>_setup`, CTF flags) |
| TPN-THPS3-5 | THPS3 scripts THUGPRO rewrote to use its new content (set: they replace ours): Skater Island's ocean proximity scripts, Tokyo's plane / train control, Shipyard's 64 glass scripts (its window counter) | `portScripts` (after the translator; a script ours doesn't bring is reported, not added - Tokyo's `JS_BulletTrainWest/EastControl`) | As TPN-THPS3-4 |
| TPN-THPS3-6 | Models (add): Obj_Bucket, CarPlantNode, CarPlantNode_Bus, icecreamtruck, light_airplane01, bullet_train, plane_jet (their `.mdl` / `.tex` / `.col.xbx`) | `cmdConvert` writes them to `<staging>\models\`; `ported_levels.installFiles` copies them into the overlay's `models` folder | THUGPRO's `<level>_scripts.prx` `models\<name>\` |

Left out (in `_left_out`): its particles (`THUGPRO_SpawnNodeParticles`), anything calling `THUGPRO_SafeKill`, `TP_DoFlash`,
`Editor_LoadCameraAnimParams`, `THUGPRO_Televator` or `Obj_PlayRandomStream` / `Obj_PlayRandomAnim` / `PlayRandomStream`
(its DLL's or global scripts' commands: SI's cannons proximity, Shipyard's setup, the cutscenes, its host level
changes); its sky sectors and its conversions of THPS3's `.dff` objects (THPS3's own data under other checksums: ours
converts THPS3's); Suburbia's 3 unnamed geometry nodes (no sector or collision anywhere: toolchain leftovers);
THUGPRO's models of THPS3's Rio / Burnside cars (ours uses THUG2's cars). Regenerate: `node
tools/thps3_levels/thugpro_compare.ts --thug2 <folder> --extract-thugpro tools/compat/thugpro/thps3_levels.json`;
check: `node tools/thps3_levels/thugpro_inventory.ts --thug2 <folder> --applied`.

## Run time (in the DLL)

| Id | What | Why | Where | Off by |
|---|---|---|---|---|
| TP-R1 | Online identity: GameSpy game name `thugpro` (key unchanged) | Shows THUGPRO's lobbies ("General", "Newbies") on OpenSpy instead of THUG2's | `src/game/net/online_identity.cpp` | `[Miscellaneous] OnlineGameName=` empty (the default: THUG2's own) |

Not done (would be run-time work): THUGPRO's own protocol - its extra messages (155-159), the scripts-hash check,
LZHAM compression, the level name it adds to 0x46. Mixed games with THUGPRO players need that whatever the files are.

## Changing approach (build time -> run time)

To drop an entry from the files and translate on the wire instead (per session, e.g. when the peer is THUGPRO):
- TP-1: map the four load-script checksums both ways in 0x46 / 0x33 (skate_net 0x00569560, HandleStartInfo).
- TP-20: the same for Load_practice <-> Load_thps4_skateshop; then
  `node tools/thps4_levels/thps4_levels.ts convert --compat none` (only the load script's name changes, not the files).
- TP-21: in the skate shop, map a received crown index past our crowns to a THPS4 restart's place (CFunc_SpawnCrown
  0x00538170) - spawns don't travel and need nothing; then `--compat none` (or drop `net_nodes` from `thps4_levels.json`).
- TP-22 ... TP-28 (THPS4: THUGPRO's hand edits): nothing of them travels on the wire, so there is nothing to translate
  - each side builds its rails, flags, restarts and collision from its own files. A run-time alternative: when the
  session's peer is THUGPRO, apply the profile's sections to the loaded node array (moves, flags, renames, terrains,
  added rails / restarts / lights) and collision before the level's objects are made, THPS4's own otherwise. Without
  them: `node tools/thps4_levels/thps4_levels.ts convert --compat none` (or drop the sections from
  `thps4_levels.json`); regenerate: `node tools/thps4_levels/thugpro_compare.ts --extract-thugpro`. Saved layouts
  aren't affected (no frame change).
- TP-29: off by default; to agree with THUGPRO's players set `door_walls.enabled` true and reconvert (the shop door
  closed for ours too). A run-time alternative: keep the door open and accept the difference (it decides only who can
  ride through the shop's door).
- TPN-THPS4-1 ... 5 (THUGPRO's own content): nothing of it travels as data - each side runs its own scripts; what does
  travel is the host level options' state (THUGPRO's messages 155-159, the SERVER_LEVEL_CHANGE_0n_ON flags): a
  run-time layer would receive them, set the flags and run `<LVL>_LEVEL_CHANGE_0n on / off` as THUGPRO's
  HandleLevelOptionObjects does (the scripts are in our levels already). Without the content: `--no-thugpro-new` (or
  `--compat none`); regenerate: `node tools/thps4_levels/thugpro_compare.ts --extract-thugpro`.
- TP-30: the same for Load_hh <-> Load_thug1_se; then `node tools/thug1_levels/thug1_levels.ts convert --compat none`
  (saved layouts: `tools/thug1_levels/migrate_layouts.ts` shows the renaming - THUG1's frame is the same on both sides).
- TP-31: spawns don't travel - each side picks its own from its node array, so nothing to translate on the wire. A
  run-time alternative: when the session's peer is THUGPRO (or the host asks for it), apply `spawns` to the loaded
  node array's restarts before the first spawn (positions / types / the order GetRestartNode walks). Without it:
  `--compat none` (THUG1's own spawns; regenerate the data with `node tools/thug1_levels/thugpro_compare.ts
  --extract-thugpro`).
- TP-32 ... TP-39: nothing of them travels - each side builds its level from its own files (node positions, flags,
  values, rails, collision); the network only carries the skaters. A run-time alternative: keep THUG1's data in the
  files and apply the sections when the session's peer is THUGPRO - the node edits to the loaded node array before
  its objects are created (positions, CreatedAtStart / AbsentInNetGames, values; rails appended and their links), the
  collision edits to the loaded collision (face flags / terrain; faces out, vertices moved - each needs the sector's
  tree rebuilt, so that one is easier as a second collision file to swap in). Without them: `--compat none` (THUG1's
  own data), or drop single sections (`moved_nodes`, `net_presence`, `created_at_start`, `node_values`, `rails`,
  `face_values`, `collision_removed`, `collision_reshaped`) from `thug1_levels.json`; regenerate with
  `node tools/thug1_levels/thugpro_compare.ts --extract-thugpro`.
- TP-10 / TP-11: nothing to translate on the wire - flags aren't sent, each side makes them from its node array. A
  run-time alternative: when the session's peer is THUGPRO, place the flag / team-node game objects from this data
  after loading (and THPS3's own otherwise); without the entries: `node tools/thps3_levels/thps3_levels.ts convert
  --compat none` (THPS3's two bases, two teams) - or remove `ctf_flags` / `team_nodes` from `thps3_levels.json`.
  Regenerate the data: `node tools/thps3_levels/thugpro_compare.ts --extract-thugpro tools/compat/thugpro/thps3_levels.json`.
- TP-4: nothing to translate on the wire - spawns are chosen locally (player n at the n-th Multiplayer restart, a
  respawn at one of the first 8); a run-time alternative is to switch the spawn set and order when the session's
  peer is THUGPRO (apply `spawns` to the loaded node array). Without the entry:
  `node tools/thaw_levels/thaw_levels.ts convert --compat none` (or drop `spawns` from `thaw_levels.json`);
  regenerate: `node tools/thaw_levels/thugpro_compare.ts --extract-thugpro`.
- TP-5 / TP-7 / TP-8 / TP-9: positions and collision don't travel as such, the skaters on them do: a run-time
  translation can't move a rail or wall. A run-time alternative is to apply the data when the session's peer is
  THUGPRO: TP-8's rails and TP-5's moves to the loaded node array before the rails are built (rails are made from the
  array at load), TP-7 / TP-9 to the loaded collision (hide the sectors, kill the collision objects, change face flags)
  - every edit is per object and by name. Without them: `node tools/thaw_levels/thaw_levels.ts convert --compat none`
  (THAW's own), or drop `node_edits` / `removed` / `added` / `collision` from `thaw_levels.json`; regenerate with
  `node tools/thaw_levels/thugpro_compare.ts --extract-thugpro`.
- TP-6: the flags (CreatedAtStart, AbsentInNetGames) could be set on the loaded nodes at run time when the peer is
  THUGPRO (CreatedAtStart decides what exists at load; AbsentInNetGames what exists in a network game); the
  imported pieces would have to be in the files whatever the peer (shown or hidden then). Without it: `--compat none`
  or drop `node_flags`.
- TP-12: spawns aren't sent either (each machine picks a player's restart by index; the position travels after).
  Run-time alternative: apply `spawns` (places, types and order) to the loaded node array only when the peer is
  THUGPRO; without it: `--compat none`
  (THPS3's own spawns, our team restarts at THPS3's nearest restarts) or remove `spawns` from `thps3_levels.json`.
- TP-13 .. TP-19 (THPS3's hand edits): nothing travels - each side builds the level from its own files; positions of
  moved objects, flags, rails and collision are local. A run-time alternative needs the two variants side by side: the
  edited node array and collision as a second build of the level (or the edits applied to the loaded node array and
  collision when the peer is THUGPRO - flags, places and terrains in place, rails appended, faces dropped from the
  collision objects before the BSP is made). Without them: `node tools/thps3_levels/thps3_levels.ts convert --compat
  none`, or drop a section (`moved`, `flags`, `terrain`, `removed`, `renamed`, `rails`, `collision`) from
  `thps3_levels.json` to leave out one entry. Regenerate: `node tools/thps3_levels/thugpro_compare.ts --extract-thugpro
  tools/compat/thugpro/thps3_levels.json`; check: `node tools/thps3_levels/thugpro_inventory.ts --thug2 <folder>
  --applied`.
- TPN-THAW-1 ... TPN-THAW-5 (thugpro_new): nothing of them travels but the host options' state (THUGPRO's messages
  155-159). A run-time alternative: keep THAW's level in the files and load THUGPRO's pieces only when the session's
  peer is THUGPRO (a second scene / collision / script set per level, read from its install as now). Without them:
  `node tools/thaw_levels/thaw_levels.ts convert --no-thugpro-new` (or `--compat none`); regenerate the references
  with `node tools/thaw_levels/thugpro_compare.ts --extract-thugpro`. The host options need a protocol layer for
  155-159 whatever the files are.
- port_changes (PC-THAW-n): not THUGPRO's - nothing to translate. `--no-port-changes` gives THAW's own data where a
  level can load without the change.
- TP-2 / TP-3: add / subtract the per-level offset to every world position sent / received (0x23 object updates,
  0x48 RunScript parameters, 0x8F / 0x95 scavenger drops, any positional KOTH / CTF event) before the int16 packing;
  watch the range at the edges.
- Then `node tools/thaw_levels/thaw_levels.ts convert --compat none` (or remove the entry from `thaw_levels.json`) and reconvert.
Saved level-editor layouts are in world coordinates: a frame change moves them (`tools/thaw_levels/migrate_layouts.ts`
has the shifting).
