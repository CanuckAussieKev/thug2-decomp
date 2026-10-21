# thps3-doors - why Foundry's generator door is closed in ours and open in THUGPRO (TP-41, PC-THPS3-16)

**Corrected 2026-10-21 (docs/pending/thps3-free-skate-thugpro.md):** the premise below that THUGPRO plays free
skate as a network game is wrong - the user sees peds in THUGPRO's free skate (all AbsentInNetGames) and THUGPRO's
Freeskate is THUG2's offline single session. THUGPRO's door is open because its `CJR_Foun_Valve_Check` opens it in
any non-career game without THPS3's `GetFlag CJR_FLAG_GEN_OPEN`; Skater Island's outer door is there in its offline
free skate. TP-41 was rewritten accordingly (script edits; the Skater Island kill and the flag are gone); the
corrected statements are marked below.

Agent "thps3-doors", branch worktree-agent-ac6b0dc54c19f1571. User's report 2026-10-21: THPS3 Foundry, GENERATOR 1 CORE
doorway: THUGPRO shows the door open (generator room visible), ours the big double door closed.

## Why

- The door is a set of EnvironmentObjects: `GenDoor_Closed01/02` (the double door), `GenDoor_Closed03` (+ `GenDoor_Closed_03_Sign`),
  their open pieces `GenDoor01/02/03_Open`, and the sliding GameObjects `Obj_GenDoor01/02/03` (+ waypoints
  `TRG_Waypoint_GenDoor*`) that THPS3's `CJR_Foun_Secret_Gen_Doors` makes for the career's door-opening cutscene.
  None has CreatedAtStart in THPS3; `cjr_scripts.qb` decides:
  - `CJR_Foun_LoadingScript`: `if InMultiPlayerGame` -> Create GenDoor01-03_Open, Kill prefix GenDoor_Closed /
    GenDoor03_Closed, Create the secret generator room (TRG_Ledge_SecretGen*, Secret_Gen*, Collision_Secret_Gen*,
    Air_Polys_Secret_Gen*). Not multiplayer: nothing.
  - `CJR_Foun_Valve_Check` (from the Player1 restart's trigger script): multiplayer -> `GoTo CJR_Foun_Secret_Gen_Multi_Off`
    (empty); career or free skate: `GetFlag CJR_FLAG_GEN_OPEN` -> open (kill closed, create open), else closed (create
    GenDoor_Closed, kill GenDoor03_Closed and the secret room).
  - `CJR_FLAG_GEN_OPEN` is set only by `CJR_Foun_Valve_Counter_Hit` (career, the five-valve goal, then
    `CJR_Foun_Secret_Gen_Doors`: a camera cut and the doors slide).
  So THPS3's offline free skate has the door closed (ours = THPS3's, correct); its multiplayer has it open.
- THUGPRO (`data\pre\FOUN_scripts.prx`, foun_scripts.qb) wrote `InMultiPlayerGame` as `InNetGame` in these scripts.
  ~~It plays every game, free skate too, as a network game: it always takes the multiplayer branch -> open.~~
  **Corrected:** its offline free skate is not a network game; its `CJR_Foun_Valve_Check` (from the Player1
  restart's trigger script, which THUG2's ResetSkaters runs) opens the doors in every non-career game - THPS3's
  `if GetFlag flag=CJR_FLAG_GEN_OPEN` taken out. (Foundry's
  node array differs from THPS3's only in classes / flags: GenDoor pieces LevelGeometry, still no CreatedAtStart.)
- Ours before: the converter made GenDoor_Closed01/02/03 and GenDoor01/02/03_Open all CreatedAtStart (its static
  follow of the start scripts took both branches of every `if`), the closed branch made the door closed. In a network
  game ours already took the multiplayer branch (THUG2's `InMultiplayerGame`, 0x5A11B0 -> 0x5651F0, is
  `InNetGame || more than one skater`), so online ours = THUGPRO; only offline free skate differed.

## The table (nodes named gate / door / blocker / secret / open / closed / board / fence / lock etc. in the 9 levels THUGPRO shares, THPS3 vs THUGPRO vs ours by node flags - `out/s/cmp.ts` scratch - plus the scripts' multiplayer branches)

| Level | Piece | THPS3 free skate | THUGPRO | Ours before | Ours now |
|---|---|---|---|---|---|
| Foundry | GenDoor01/02/03 double door + generator room | closed (career valve goal / multiplayer open it) | open (~~always the multiplayer branch~~ its Valve_Check opens it in any non-career game) | closed (offline), open in network games | **open** (TP-41, now: `CJR_Foun_Valve_Check`'s GetFlag decided true; `--compat none`: closed) |
| Skater Island | SI_OuterDoor (+ SI_Sponsors06, si_lighting02) | present (cannon goal blows it; multiplayer kills it in ALF_SI_Unhide_Inside) | CreatedAtStart + AbsentInNetGames (TP-14) and made by its ALF_SI_Init: ~~gone in its (network) free skate~~ **there in its offline free skate** | present offline, gone in network games | ~~gone (TP-41: killed a second after the start scripts)~~ **there** (the kill taken out of TP-41) |
| Tokyo | Tok_Secret_Building00_1/2 (+ _WallEast, _LedgeEast) and Tok_Mid_Sign_Destroyed(_A) | not there (JS_Mid_Smasher_CareerRestart: career + TOK_SECRET_AREA_OPEN, or the sign-smasher goal) | not CreatedAtStart | **there, drawn and solid beside the building / sign they replace** (bug: static follow took the career branch) | not there (PC-THPS3-16) |
| Airport | Tickets, ScrollSign_Done | not there (GOAL_SCRIPTED branches) | n/a (THUGPRO doesn't share AP) | CreatedAtStart (same bug) | not there (PC-THPS3-16) |
| LA | TRGP_CarFall_W / _E | not there | n/a | CreatedAtStart | not there |
| Skater Island | CREAT_* (cannon damage, 36 sectors) | not there (ALF_SI_Init kills the prefix; career flag / FireCannons make it) | not CreatedAtStart | CreatedAtStart, killed by ALF_SI_Init | not CreatedAtStart (same end state) |
| Shipyard | sf_AnchorDoor*, sf_*Break*, ferry, net rails: the A / B world states (BDJ_SHP_RestartSetup: GetFlag BDJ_SHP_NETS; multiplayer: BDJ_SHP_RestartSetupNet) | state A (unbroken) | not CreatedAtStart; free skate = THPS3's multiplayer branch | A / B pieces both CreatedAtStart, the script picks (unchanged) | unchanged. ~~Not looked into~~ **Checked**: THUGPRO's offline free skate shows state A as THPS3's (not a network game); its RestartSetup kills the same collision-only B pieces (ours since PC-THPS3-17) |
| Rio | ES_Sul_Fence_Broken* | BouncyObject, AbsentInNetGames | LevelObject (TP-14/35: present in net) | same as THUGPRO | unchanged |
| Rosewood | Obj_SkRos_AutopsyDoor_Right*, GarageDoor_* | not CreatedAtStart | CreatedAtStart (TP-14) | as THUGPRO | unchanged |
| Suburbia | SUB_InvisoPlantsBlocker | CreatedAtStart | not (TP-14) | as THUGPRO | unchanged |
| Suburbia | ALF_SUB_Status (boarded / unboarded houses, DOOR_BLOCKER, branches): multiplayer `Die`s | career flags | as THPS3 multiplayer | scripts as THPS3 | unchanged (not a gate ours misses) |
| Canada, Burnside, Warehouse | no gate / door pieces differ (`can`, `burn`, `ware` node diffs: only CTF restarts and THUGPRO's added pieces) | | | | |
| THPS4, THUG1 | not surveyed (~~the same pattern, THUGPRO's net-mode free skate reading as a multiplayer branch~~ - no such pattern: THUGPRO's free skate is offline; script rewrites like TP-41's may exist there) | | | | |

Counted: THPS3 scripts with `InMultiPlayerGame`: 142 (most are peds / particles / visibility); only Foundry's doors and
Skater Island's door are gate-like.

## Changes

- (Superseded by TP-41's rewrite, docs/pending/thps3-free-skate-thugpro.md.) `tools/compat/thugpro/thps3_levels.json` `free_skate` (TP-41: `{foun: {set_flags}, si: {kill}}`),
  `levelCompat().freeSkate`, `portScripts` (`SetFlag` lines first in `<level>_setup`; `<level>_free_skate` kills after
  `Wait 1 Second`). `thugpro_compare.ts --extract-thugpro` keeps the section.
- PC-THPS3-16 (`port_changes`, not required, `--no-port-changes` turns it off): `createdAtStart` follows an
  `if IsCareerMode / GetGoal / InMultiPlayerGame / InNetGame / InSplitScreenGame` only where it is false (offline
  free skate; `not` inverted; anything with other tokens or a flag test stays "both branches"). Nodes made only there
  lose CreatedAtStart but stay in the array (scripts still make them): Tok 6, SI 36, AP 2, LA 2.
- Register: tools/compat/thugpro/README.md TP-41 and PC-THPS3-16 rows; docs/level-port-compromises.md section 4.
- Tests: tools/thps3_levels/thps3_doors.test.ts (3).
- Collision: no invisible collision left: the open state kills the closed door sectors (a Kill removes sector and
  collision, as in THPS3); the generator room's sectors / collision are CreatedAtStart in THPS3 already.

## Verified

- `npm run typecheck` / `lint` / `test` (192 + 3), `thps3_levels.ts selfcheck` (323 files, 0 failures).
- `thugpro_compare.ts` for the 9 shared levels: identical output before / after (node counts, flags, spawns, triggers).
- Before / after conversion (out\thps3_before / thps3_after, scratch): node arrays differ only in the CreatedAtStart
  flags listed above; foun_setup now `SetFlag flag=CJR_FLAG_GEN_OPEN` first, si_setup spawns si_free_skate.
- Game load check in C:\Games\THUG2-1 (lock held; main's installed dinput8.dll copied in for `teleport` / `camera`,
  restored on release): Foundry at the report's spot (-4065.5, 132, 4310.5, heading -9.2): the double door is open and the
  generator room is in view (same view as THUGPRO's screenshot); Skater Island loads, no log errors.
  **NOT checked in the game**: Skater Island's door gone / skater can leave the ship, Tokyo without the secret buildings.

## For the lead

- Reconvert THPS3: `node tools/thps3_levels/thps3_levels.ts convert` and `node tools/menu_level/make_menu_level.ts`
  (Foundry, Skater Island, Tokyo, Airport, LA change; the others byte-identical).
- For the user to look at: Foundry's generator room (walk in, rails / walls inside; no stuck collision at the door), Skater Island's
  front (the outer door gone, outside area reachable), Tokyo's mid area (no extra buildings next to the sign).
