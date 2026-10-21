# THPS3: Foundry's bucket collision, Rio / Suburbia carplant shadows, Tokyo's trains online - 2026-10-21

Branch `worktree-agent-a13d992e1fbb8ee3c`. The user's decisions A, B and C (relayed by the lead). Tools and data only:
**no C++ change** (C's evidence below shows none is needed).

## C first - Tokyo's bullet trains and planes online: THUGPRO does not create them (evidence)

The user: "Create them online if THUGPRO does", and "did THUGPRO have that engine fix?". The premise (from
docs/thugpro-hacks-review.json, tok-trains-forcecreate-online: "THUGPRO_ForceCreate, a DLL command that ignores
AbsentInNetGames") is wrong:

1. **THUGPRO_ForceCreate is a QB script, not a DLL command.** Morten1337's thugpro, `source/code/qb/thugpro/thugpro_utils.q`
   (commit 361b2c1): `script THUGPRO_ForceCreate` - `if NOT IsAlive name=<name>: Create name=<name>`, else `Kill` then
   `Create`. No THUGPRO binary carries the name: thugpro.dll, THUGPro.exe, ContentManager*.dll, updt.dll searched for
   the strings ForceCreate / SafeKill / TriggerCarPlant (ASCII and UTF-16) and the checksums 0x857EA1D0
   (THUGPRO_ForceCreate), 0xA340E9AF (AllocatePathManMemory), 0xA1DA6FE9 (InNetGame): none.
2. **THUG2's Create keeps AbsentInNetGames nodes out of a network game.** CFunc_Create 0x005A4FF0 -> 0x005A4EF0 ->
   0x005A4DA0: in net mode (0x0044E4E0) it asks 0x00565BB0 for the node, which is true for `AbsentInNetGames` (0x68910AC6)
   when InNetGame (0x0044E4F0) or two or more players (0x00553190) - and then nothing is created. ParseNodeArray's
   created-at-start uses the same gate.
3. **THUGPRO's trains and planes keep AbsentInNetGames** (its tok.q: TRG_shinkanseneast / west and TRG_Plane_NorthWest /
   SouthWest / East are `CreatedAtStart AbsentInNetGames`; only the 185 waypoints lost the flag - TP-14). So in a
   THUGPRO network game they are neither made at the start nor by ForceCreate: **THUGPRO has no trains or planes online.**
   Offline (InNetGame false, one player) both sides make them.
4. **Unmodified THUG2 really has no path manager in a network game**, and THUGPRO doesn't change that: THUG2's
   Levels.qb (`if ! InNetGame: AllocatePathManMemory`) and ReloadScene (skutils.qb) skip it online; CFunc_Cleanup
   (0x0059FE50 -> 0x00469820) frees it on every level change; the motion component's path start 0x00423FC0 calls
   AddToPath 0x00469620 with no check (and the car path 0x004F0740 AddToExtraGroup), so a path follower online reads
   through the null group array - our 0x004695D0 crash (docs/level-port-compromises.md section 3). THUGPRO's installed
   Levels.qb / skutils.qb (thugpro_qb.prx) have the same `if NOT InNetGame` guard (bytes checked); THUGPro.exe is
   byte-identical to THUG2PM.exe over 0x004694A0-0x00469B00, 0x00423C90-0x00424090, 0x00424180, 0x00425220,
   0x004F0740, 0x0059FE50, 0x0045D520, 0x004E1EF0 (its 64 differing .text bytes are elsewhere: 0x0046F559.., 0x004A0784..,
   0x004B6C63, 0x004D8834, 0x004E26A1, 0x005480A1.., 0x005BBCC2..); thugpro.dll contains none of the path manager's
   addresses (0x006F2264, 0x004694E0, 0x004695D0, 0x00469620, 0x00469670, 0x00469820, 0x00469960, 0x00469A80,
   0x00423FC0, 0x004F0740) nor CFunction-table entries of AllocatePathManMemory / InNetGame / Cleanup; its two
   references to 0x0044E4F0 are calls (`mov eax, 0x44E4F0; call eax`), not a hook. The thugpro_proxy source only
   defines PATH_MANAGER_INSTANCE_ADDRESS (unused) - and is older than the installed DLL (no ForceCreate etc.), but
   the installed binaries were checked directly.

**Conclusion:** both sides agree already (trains and planes offline only; no one is hit online by a train the others
don't see). Nothing to change: no engine fix (it would only matter if we made path followers exist online, which
neither THUG2 nor THUGPRO does), no register entry beyond a note on TP-14. Ours maps ForceCreate -> Create
(TPN-THPS3-4 `replace_calls`): the same, since each train is dead (its path's last waypoint runs the control script,
which `Die`s) when its spawned script re-creates it 4-10 s later. Each player's timer runs in his own (offline) game.

Recorded: docs/thugpro-hacks-review.json (correction on both entries), tools/compat/thugpro/README.md (TP-14 and
TPN-THPS3-4 notes), docs/level-port-compromises.md section 3 (the path-follower row: why THUG2 has no path manager
online), tools/thps3_levels/thugpro_new_extract.ts (`ADAPT` comment corrected).

## A - Foundry's bucket: THPS3's own collision shape

- Before: our LevelObject (PC-THPS3-15) took THUGPRO's collision of the name (TPN-THPS3-1: 134 faces, box -60..60,
  -60..60, -76..60) - smaller than the bucket drawn.
- Now: cmdConvert leaves TPN-THPS3-1's collision objects of the PC-THPS3-15 objects out ("PC-THPS3-15: 1 TPN-THPS3-1
  collision objects left out (THPS3's shape instead)"), so convertWorld's solidCollision makes it from THPS3's drawn
  model: Obj_Bucket 278 faces, box -111..110, -89..89, -76..78 (the drawn bucket's). Tokyo's trains already had theirs
  (no THUGPRO collision there).
- Kept: no kill (THPS3's TRGP_Kill_Bucket only starts a gap; THUGPRO's kill is in its model, not brought), THPS3's tip
  motion (thps3_rotation.ts).
- **Verified in C:\Games\THUG2-4** (under its lock; this branch's convert + make_menu_level; this branch's DLL with a
  temporary, uncommitted change making `teleport`'s ground probe include movable collision): probing down onto the
  bucket at its out position (x -4061, z -397) while it is upright: centre 45.9 (inside the bowl), x - 90: 42.0 (hit),
  z - 60: 92.0 (the rim); THUGPRO's shape ends at 60 from the centre, so the hit at 90 is THPS3's shape. While the
  bucket tips the same probes rise (46 -> 114 -> 94.8) - the collision moves and turns with it. Load checks: foun, tok,
  rio, sub reached free skate, no exception / run-time check / CRT report.

## B - Rio / Suburbia carplant_collision shadow vehicles left out

- What they are (THUGPRO's tok/rio/sub .q and model files): an invisible Vehicle (model CarPlantNode / CarPlantNode_Bus)
  locked onto each car by its own script (Obj_LockToObject at an offset, polling ObjectExists every 4 frames). The
  model's node array is one trigger node TRGP_CarPlant01 running `THUGPRO_TriggerCarPlant` - THUGPRO's global script
  (`LaunchEvent type=Carplant` on the skater, under CARPLANT_ENABLED). Its collision is 12 faces flagged 0x50
  (trigger + non-collidable): **not a second solid collision**, a car-plant trigger box.
- In ours that trigger script doesn't exist, so the boxes were inert; and our Rio cars are THUG2's own vehicles
  (PC-THPS3-10) whose models carry their own collision, car-hop triggers (veh_sedan: TRGP_Sedan01-04 -> Gap_CarHop)
  and THUG2's car plants. THUGPRO needed them because its Rio cars are THPS3 models. Suburbia's ice-cream truck is
  THUGPRO's icecreamtruck model in both (TPN-THPS3-3; THUGPRO has the shadow there too) - in ours the shadow was inert
  as well, so it is left out with the others (one rule): **if car plants on the ice-cream truck turn out to be missing,
  that's where to look** (bring THUGPRO_TriggerCarPlant, or give the truck's model a trigger node).
- How: tools/thps3_levels/thugpro_new_extract.ts `shadowVehicle` - an added Vehicle whose script locks it onto another
  object and whose model holds only trigger nodes running what THUG2 hasn't is listed in `_left_out` (with its trigger
  script); its model isn't taken. Regenerated tools/compat/thugpro/thps3_levels.json (`thugpro_compare.ts --thug2
  C:\Games\THUG2-4 --extract-thugpro`): exactly 21 entries gone (Rio 8 nodes, 8 scripts, 2 models; Sub 1 node, 1
  script, 1 model) and 18 `_left_out` lines added; nothing else changed. PC-THPS3-15's `kept_vehicles` and the
  README rows (PC-THPS3-15, TPN-THPS3-2 / -6, the left-out paragraph) updated. Converted rio.qb / sub.qb: no
  "carplant" left; staging models: Obj_Bucket, bullet_train, icecreamtruck, light_airplane01, plane_jet.

## Checks

typecheck, lint, npm test (197 pass), thps3_levels selfcheck (323 files, 0 failures); C++ unchanged but built
(out\build\verify): thug2_check on THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe (521 early self-tests passed each),
check_code_writes (no overlaps).

## What the user should check by hand

1. Foundry: ride / jump onto the bucket's rim and sides - it should block and carry you where the bucket is drawn
   (wider than before); it still doesn't kill.
2. Rio: cars still car-hop (gaps) and car-plant as THUG2's cars do; no invisible box over them.
3. Suburbia: the ice-cream truck - try a car plant on it (see B: the shadow that THUGPRO uses for it is gone).
4. Online (by hand only): Tokyo has no trains or planes in a network game - on both sides, as in THUGPRO.

## Reconvert

THPS3's **Foun**, **Rio** and **Sub** change (Tok unchanged): `node tools/thps3_levels/thps3_levels.ts convert` (all
levels) and make_menu_level for the main install.
