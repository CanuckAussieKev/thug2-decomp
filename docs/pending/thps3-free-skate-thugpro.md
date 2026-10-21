# thps3-free-skate-thugpro - THPS3 free skate's world as THUGPRO's offline free skate (TP-41 rewritten, PC-THPS3-17)

Branch worktree-agent-a208e0d7a765867b6 (continues a stopped agent's uncommitted work: start_state.ts, editScripts,
the compare's --start-state). User's decisions 2026-10-21: (1) match THUGPRO's free skate world in our offline free
skate, every THPS3 level; (2) keep the life (Foundry's bucket, peds, vehicles, moving objects...) even where THUGPRO
lacks it; (3) the premise "THUGPRO plays free skate as a network game" is wrong (the user sees peds there) - find out
from THUGPRO's files why each state is what it is.

## Why THUGPRO's free skate looks as it does (evidence)

Sources, read only: THUGPRO's installed `%LOCALAPPDATA%\THUG Pro\data\pre\<LEVEL>_scripts.prx` (levels\<l>\<l>.qb and
<l>_scripts.qb, decompiled with ns.exe -lenient), `thugpro_qb.prx` (qb\game\Levels.qb), its scene / collision prx;
Morten1337's thugpro source (skateshop.q, gameflow.q, thugpro_utils.q); THPS3's Data\Scripts and level .qb; THUG2PM.exe
in Ghidra (snapshot copy C:\Games\THUG2-ghidra-snapshot-freeskate).

- **Not a network game.** THUGPRO's main menu "Freeskate" is `change_gamemode_singlesession` (skateshop.q) - THUG2's
  offline single session: InNetGame / InMultiplayerGame false. Every ped in THUGPRO's Foundry is AbsentInNetGames
  (23 of 23), so the peds the user sees prove it. THUGPRO's net branches (e.g. `CJR_Foun_LoadingScript`'s
  `if InNetGame`, which opens the doors and kills the pour) don't run offline.
- **The Player1 restart's script runs at the start, in both games.** `ChangeLevelGameFlow_THUGPRO` (gameflow.q) calls
  `ResetSkaters` after loading; CFunc_ResetSkaters 0x005AC3B0 -> 0x00566980 skips each local skater to the restart
  0x00566100 finds (offline: the Player1 one) through SkipToRestart 0x00583DA0, which runs the node's TriggerScript
  (src/game/skater/skater.cpp). Ours runs that script from `<level>_setup` (PC-THPS3-2). (The stopped agent had assumed
  THUGPRO doesn't run it; corrected in start_state.ts.)
- **Foundry's generator door (GENERATOR 1 CORE): open because THUGPRO rewrote `CJR_Foun_Valve_Check`.** THPS3's
  non-career branch opens the doors only `if GetFlag flag=CJR_FLAG_GEN_OPEN` (set only by the career's five-valve goal)
  and otherwise makes the closed door; THUGPRO's non-career branch opens them unconditionally (kills GenDoor_Closed*,
  ForceCreates GenDoor01-03_Open, frees the valve peds). It runs from `FounTRG_Restart_1PScript` (above). Node arrays
  agree: neither the closed nor the open pieces are CreatedAtStart in either game; the generator room (Secret_Gen*,
  TRG_Ledge_SecretGen*) is CreatedAtStart in both.
- **Foundry's valve booths:** THUGPRO's `CJR_Foun_Valve_A..E_Booth` are `GoTo ..._Clear` (the cleared state), THPS3's
  blink red until the career goal.
- **Foundry's bucket: THUGPRO's files make it.** `FounTRG_Restart_1PScript` -> `CJR_Foun_Create_Bucket_Real`
  (ForceCreate Obj_Bucket, _Brace, _Hook, _Controller, _Booth_Controller, the director ped); offline,
  `CJR_Foun_Bucket_Controller` sends it out every cycle. THUGPRO's Obj_Bucket is a `Class=Vehicle` with its own model
  (models\Obj_Bucket, in FOUN_scripts.prx) - THPS3's is a GameObject; the scene has the Obj_Bucket* sectors. So its
  absence in the user's THUGPRO game is not in the data (not removed, not left un-CreatedAtStart for good, not killed,
  not flagged against offline): it happens at run time - most likely the Vehicle object (THUG2's car components on a
  bucket) failing to show. Not checked in THUGPRO's game (we don't run it). Ours keeps THPS3's bucket (decision 2).
- **Skater Island's outer door is there** in THUGPRO's offline free skate: its `ALF_SI_Init` ForceCreates
  SI_OuterDoor; only `if InMultiplayerGame` / the career / the door-blowing gap kill it. TP-41's old kill was wrong
  and is gone.
- **Subway:** THUGPRO's `ALF_SUB_Status` (SUB_Status, CreatedAtStart, not AbsentInNetGames) runs both of THPS3's
  goal-done branches unconditionally and without THPS3's `if InMultiplayerGame Die`: houses unboarded (BOARDED_* killed,
  UNBOARDED_* made), SUB_Blocker* gone, open rails, dish turned (DISHEND_*), curtain lit - offline and online. It also
  kills the Thin Man ped.
- **Skater Island's cannons:** THUGPRO's `ALF_SI_Init` doesn't make TRGP_FireCannons (THPS3's does): skating into the
  trigger doesn't fire the cannons / blow the interior (DEST_ -> CREAT_). THUGPRO makes its own proximity trigger
  (TRG_FireCannonsProximity01, a node of its own) once the outer door is blown.
- **Shipyard:** THUGPRO's `SHP_Setup` makes the helicopter (sfo_Chopper, sfo_ChopperBlades) in every offline game (THPS3:
  career only) - it sits on the helipad, spins up and flies off; the ship's world state is THPS3's free-skate state A
  (THUGPRO's BDJ_SHP_LoadingScript unsets FERRY / NETS / POOL offline as THPS3's free skate does; its RestartSetup kills
  the B pieces, collision-only ones included). Its ambient boats (TRG_LeftAmb_* / RightAmb_*) are never made
  (Show/HideLeftAmb / RightAmb emptied).
- **Culling emptied:** THUGPRO emptied THPS3's visibility culling scripts (Rio's CJR_Rio_Opti_* and ReSpawn_Multi,
  Skater Island's ALF_SI_hide_Inside and the visibility half of ALF_SI_Unhide_Inside, Shipyard's BDJ_SHP_Show* /
  Hide*, Tokyo's JS_Vis_* / JS_InVis_* / JS_Prox_*, Foundry's CJR_Foun_Inv_InGenerator / Vis_OutGenerator): everything
  is drawn all the time. Collision is the same either way.
- **Life THUGPRO removed** (emptied scripts, inventory: scratch emptied.ts over all 9 levels): crowds / judges /
  comp peds (Rio, SI, Tok), Subway's Thin Man / inhaler / farter / pumpkin scenes, Foundry's soaker crane sequence and
  welder sparks, Skater Island's parrots and sharks, Tokyo's camera / mid peds on-off, Shipyard's ambient boats. Kept in
  ours (decision 2).

## Changes

- **TP-41 rewritten** (`tools/compat/thugpro/thps3_levels.json` `free_skate`; README row): per level, THPS3 scripts
  edited the way THUGPRO rewrote them - `conditions` (every `if` of that command in the script decided) and `drop`
  (statements left out). Written into THPS3's script bytes before anything reads them (`editScripts`: script_port's
  `thps3_port_always` / `_never` / `_dropped` markers; the translator and `createdAtStart` take them as decided):
  - foun `CJR_Foun_Valve_Check` {GetFlag: true}; `CJR_Foun_Valve_A..E_Booth` {IsCareerMode, GetGoal: true}
  - sub `ALF_SUB_Status` {InMultiPlayerGame: false, GetGoal: true}, drop `Kill Name=TRG_Ped_ThinMan` (life kept)
  - si `ALF_SI_Init` drop `Create Name=TRGP_FireCannons`
  - shp `BDJ_SHP_LoadingScript` {IsCareerMode: true} (the helicopter; the goal flags stay clear)
  Gone from TP-41: Foundry's `SetFlag CJR_FLAG_GEN_OPEN` and Skater Island's outer-door kill (`si_free_skate`).
  `--compat none`: THPS3's own free skate.
- **PC-THPS3-17** (port change, switchable): Create / Kill of collision-only geometry kept in the brought scripts
  (script_port `TranslatorOptions.collisionOnly`, thps3_levels passes the kept nodes with collision and no drawn
  sector). THUG2's LevelGeometry Create 0x005A2E50 / Kill 0x005A32C0 touch the geometry only through CSector
  0x0049E680 / 0x0049E9B0 / 0x0049EA30, which skip a null one (disassembly checked); Visible / Invisible / Shatter stay
  left out. Needed for TP-41 (Subway's BOARDED_02 / SUB_Blocker02 / DISHSTART_Inviso are collision only - they stayed
  solid behind the unboarded house) and fixes THPS3's own free skate: Shipyard's sfc_RampBreakB*, sfc_RampVerts*,
  sft_EndAnchorChainAir*, sft_FerryCollisionB (killed by THPS3's RestartSetup) were solid in ours. Other levels: the
  scripts that already ran now also switch their collision-only pieces (Foundry's press collision and Opti floors,
  Rio's spectator-box collapse, Tokyo / Rio glass and trigger planes killed after use, LA's tremor / car-fall
  triggers, Airport's areas, the tutorial's `Create prefix="C"`).
- **start_state.ts / thugpro_compare.ts --start-state**: Player1 restart's script counted on THUGPRO's side; geometry
  nodes count when either side draws or collides with them (trigger planes were skipped).
- Tests: thps3_doors.test.ts (TP-41 data), script_port.test.ts (withoutUndrawn with collisionOnly).
- Docs: tools/compat/thugpro/README.md (TP-41, PC-THPS3-17), docs/level-port-compromises.md section 3 / 4,
  docs/pending/thps3-doors.md corrected (premise struck through), tools/thps3_levels/README.md (--start-state).

## Verified

- `npm run typecheck` / `lint` / `test` (200 pass); `thps3_levels.ts selfcheck --thug2 C:\Games\THUG2-1` (323 files, 0
  failures).
- Converted before (main) / after into scratch: only .qb files differ (scenes, collision, textures byte-identical);
  burn, ros, ware unchanged; can / ap / la / tut: PC-THPS3-17 statements only.
- `thugpro_compare.ts --start-state` for the 9 shared levels, node by node (THUGPRO's Levels.qb setup / startup
  scripts + node flags + object and Player1 restart scripts, vs ours): **no world node differs** except
  - Foundry `Press_Down`: the press cycles in both (a static-analysis artefact: both branches of its loop);
  - visibility culling (hidden, not removed): Rio 17 Sul_Bowl* (CJR_Rio_ReSpawn_Multi), Skater Island 9
    SI_Treasure* / SI_ShipBackside* (ALF_SI_Unhide_Inside), Shipyard 362 (BDJ_SHP_HideLeft / Right / Back) - THPS3's
    culling, which THUGPRO emptied: kept (drawing only; see below);
  - life, kept on purpose: Subway's Thin Man, Skater Island's resting parrot, Shipyard's 4 ambient boats; Tokyo's
    Obj_Flag_Set_Medals (an invisible flag holder).
  Before this work: Foundry's closed doors / booths, Subway's houses / blockers / dish (7 more world nodes incl. the
  collision-only ones) and Shipyard's 7 collision-only B pieces differed.
- Game, C:\Games\THUG2-1 under the lock (main install's dinput8.dll copied in for the automation commands; DLL / ini
  put back on release): Foundry - GENERATOR 1 CORE open, generator room in view (Screenshots\fsk_foun_door2); Subway -
  the house passage open, Thin Man there (fsk_sub_house*); Shipyard - the helicopter on the pad, rotor spinning
  (fsk_shp_c0 / c1; main's conversion: empty pad, fsk_shpb_c0); the start drop-in identical to main's (skater
  positions equal). Load checks, 15 s in free skate each, no errors in the log: foun, sub, shp, si, rio, tok,
  thps3_la, thps3_ap, thps3_tut. Never online.
  **Not checked in the game:** Foundry's bucket from close by (its scripts are unchanged), walking through the
  unboarded house / the open generator door, Shipyard's B-piece collision gone, Skater Island's cannons not firing.

## Remaining differences from THUGPRO's free skate (with reasons)

- Visibility culling (Rio, Skater Island, Shipyard, Tokyo, Foundry's generator): ours hides areas THPS3 hides from where
  they can't be seen; THUGPRO draws everything. Not layout (collision the same). If the user sees holes / pop-in at
  some vantage point, a TP edit emptying those scripts (as THUGPRO) would match - it would also keep Tokyo's / Rio's
  culled peds and cars alive.
- Life THUGPRO lacks, kept (decision 2): Foundry's bucket, Subway's Thin Man and scenes, Skater Island's parrots /
  sharks, Shipyard's ambient boats, crowds / judges, Foundry's soaker sequence and welder sparks.
- Skater Island's cannons: THUGPRO can fire them after the outer door is blown (its own TRG_FireCannonsProximity01);
  ours can't in free skate (THUGPRO-only node, not brought).
- Shipyard online: ours makes the helicopter in network games too (the edit decides `IsCareerMode` for the whole
  script; THUGPRO's net branch doesn't call it) - a local object, flies off.
- Dynamic edits THUGPRO made to scripts that run later (not the start state): Rio's spectator box (THUGPRO's collapse
  doesn't make SpecBox_Broken; its net-only option), Subway's BBQ / fence debris / bats, Tokyo's smasher cutscene,
  Warehouse's glass (THUGPRO's light set - PC-THPS3-1 already keeps the lit set). Not matched.
- Levels THUGPRO doesn't ship as THPS3 ports (can, ap, la - THUG2's own; tut, skateshop): no TP-41 change;
  PC-THPS3-17 only.

## For the lead

- Reconvert THPS3 (`node tools/thps3_levels/thps3_levels.ts convert`) and `node tools/menu_level/make_menu_level.ts`:
  every level's .qb changes (TP-41: foun, sub, si, shp; PC-THPS3-17: most); scenes / collision / textures don't.
- `--no-port-changes` switches PC-THPS3-17 off; `--compat none` TP-41.
- For the user: Foundry - generator door open, valve booths blue, bucket still running; Subway - houses open, walk
  through where the boards were (no invisible wall), dish turned, curtain lit; Shipyard - helicopter takes off at the
  start, the ramp / anchor area without invisible collision; Skater Island - outer door there, cannons don't fire.
