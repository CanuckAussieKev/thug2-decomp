# thugpro-node-flags - THAW node flags as THUGPRO's (TP-40)

Agent "thugpro-node-flags", branch worktree-agent-ab8ff607ce079e26e (on main 4d49723). Follows
docs/pending/thaw-missing-geometry.md (THAW's non-story node flags, `nonStoryNodeFlags` / `applyNodeFlags`).

User's decision (2026-10-21): "we should match THUGPRO for online compat. But it should be documented as such that it
differs from real THAW."

## Survey (all 15 areas)

THUGPRO keeps `CreatedFromVariable` and each converted area's level qb (`data\pre\<level>_scripts.prx`,
`levels\<level>\<level>.qb`) has a global table `NODEFLAG_... = 0 / 1`; its `thugpro_dynamic.q` adds CreatedAtStart
to (and creates) a node whose flag is true - the same rule as ours. Its toolchain writes every flag as 0 and the values
were then set by hand (thugpro_toolchain `thaw_node_array_converter.py`, read for insight only). No THUGPRO flagged
node has CreatedAtStart itself, so the table decides alone.

Against THAW's non-story values, 30 flags differ (effective on / off; "0 in THUGPRO, never set in THAW" is off on
both sides and not counted - EL's 2ndSprinkler04-06, GoalUnderwear01, SPEAR_AFTER6, TACO; SR's FINALPROS_IGC; SV's
CAT_NODES is in neither table):

| Area | Flag | THAW | THUGPRO | Nodes |
|---|---|---|---|---|
| HO | Z_HO_CUTSCENELIGHTS | 1 | 0 | 3 street-light pieces |
| HO | Z_HO_ElCAP_CLOSEDDOORS | 1 | 0 | El Capitan's closed doors |
| HO | Z_HO_ElCAP_OPENDOORS | 0 | 1 | El Capitan's open doors |
| HO | Z_HO_DT_EVENT | 1 | 0 | the metro work site: scissor lift (base, platform, collision), barriers / qp, 17 rails, invisible barrier, a kill piece (26) |
| HO | Z_HO_DINO_BOARDS, _DINO_BOARDS02 | 1 | 0 | the roof boards where the dino head stood |
| BH | Z_BH_AWNINGPRE | 1 | 0 | Rodeo's awning: 13 rails, 3 pieces |
| BH | Z_BH_BHSRGATEPRE | 0 | 1 | the exit to Skate Ranch: fences 01 / 02 and g_bhsr_blocker |
| BH | Z_BH_BHSRGATEPOST | 1 | 0 | the opened exit's fences 03 / 04 |
| DT | Z_DT_GREEN_BEFORE | 0 | 1 | DT_Green_Shatter03 (unbroken) |
| DT | Z_DT_GREEN_ROOF_BEFORE | 0 | 1 | DT_Green_Shatter01 and its collision |
| DT | Z_DT_GREEN_AFTER | 1 | 0 | 24 Chinatown "after" rails |
| DT | Z_DT_INITIALIZEGRAFFITIAREA_BEFORE | 0 | 1 | graffiti set-up piece + game object |
| EL | Z_EL_COUCH_DEF | 0 | 1 | roof couch + 8 rails |
| EL | Z_EL_SHACK_DEF | 0 | 1 | hood park shack + 8 rails |
| EL | Z_EL_SPEAR_BEFORE | 0 | 1 | 3 "before" statues, a grind piece, 6 metro HQ rails |
| EL | Z_EL_SPEAR_AFTER | 1 | 0 | 2 "after" statues, a spring |
| SM | NODEFLAG_PERMANENT_PEDS | 0 | 1 | skate / tattoo shop peds (made by our port anyway; absent in net games) |
| SM | Z_SM_INITIALIZEALLGOALS_BEFORE | 1 | 0 | a goal set-up piece |
| SM | Z_SM_TELESCOPE_BEFORE | 1 | 0 | the pier telescope (3 pieces) + 5 goal surfer peds |
| SR | Z_SR_HO_DINO | 0 | 1 | the dino head (15 pieces) |
| SR | Z_SR_HO_DINO_DEF | 1 | 0 | its stand-in mound cap |
| SR | Z_SR_SM_SCOPE_OBJ | 0 | 1 | the telescope + its talk object |
| SR | Z_SR_CRANE_TOP | 0 | 1 | the crane's top and cargo box (vehicles: parked) |
| SR | Z_SR_CRANE_SNAKE | 0 | 1 | a crane shadow |
| SR | Z_SR_EL_MATTRESS | 0 | 1 | the camper corner's 7 props |
| OI | OI_Derrick_Before | 0 | 1 | drill, unbroken scaffold, blockers, 16 rails (23) |
| OI | OI_Derrick_After | 1 | 0 | wrecked derrick, kicker, 144 rails (146) |
| OI | OI_Grate_before | 0 | 1 | spheres, pipes, metal grate, decals (14) |
| OI | OI_Grate_after | 1 | 0 | 16 grate grind rails |

CH, MA, DN, JA, SZ, SV2 have no flags; SV, LV: none differ. THUGPRO's choice is consistent with "the pieces taken to
Skate Ranch are at Skate Ranch" for SR / HO / SM, but the opposite for OI and EL (the before state there).

## Change

- `tools/compat/thugpro/thaw_levels.json` `node_flag_values` (TP-40): {zone: {flag: {thaw, thugpro, what}}}.
- `thaw_levels.ts`: `COMPAT.nodeFlagValues` (setCompat), `zoneNodeFlags(zone, flags, report)` - THAW's non-story map
  with the zone's THUGPRO values on top (per area: NODEFLAG_PERMANENT_PEDS is set only in SM); a THAW value other
  than the recorded one is reported; `convertLevelNodes` applies it before `applyNodeFlags`. `--compat none`: THAW's.
- `thugpro_compare.ts`: new "node flags" column - THUGPRO's flagged nodes made (CreatedAtStart or a true table
  value) against ours (CreatedAtStart); nodes AbsentInNetGames on both sides are skipped (HO / BH / EL's shop peds,
  which our port makes from NODEFLAG_PERMANENT_PEDS itself - thaw_scripts.wantedPed).
- Register: tools/compat/thugpro/README.md TP-40 (TP-40 was free: the older TP-40..43 in HANDOFF.md became
  TPN-THUG1-*). docs/level-port-compromises.md section 7: a line saying these areas differ from the real THAW.
- Test: thaw_levels.test.ts "TP-40".

## Verification

- npm typecheck / lint / test pass (189 tests); `thaw_levels.ts selfcheck`: 735 files, 0 failures.
- Converted all 15 areas into the worktree (`out\thaw_staging`, reading C:\Games\THUG2-2 / C:\Games\THAW only): no
  "THAW value differs" reports; `thugpro_compare.ts --staging out\thaw_staging\levels`: node flags same in all 15
  (HO 157, BH 418, DT 193, EL 117, SM 1006, SR 2459, LV 365, OI 564 flagged nodes), everything else as before (nodes
  in place, crowns, mp restarts, net spawns). Without TP-40 (same convert, section renamed): HO 32, BH 21, DT 27,
  EL 29, SM 3, SR 25, OI 199 nodes differed.
- Nodes made from flags, before -> after: HO 110 -> 79, BH 205 -> 190, DT 151 -> 132, EL 52 -> 80, SM 506 -> 501,
  SR 2208 -> 2236, OI 474 -> 349. The geometry pieces all have scene sectors (the "without a scene sector" counts
  didn't change).
- **NOT run in the game** (no game runs). For the user to look at after the lead reconverts THAW: Skate Ranch's dino
  head (instead of the bare mound), the telescope, the crane top; Oil Rig's derrick standing (not wrecked); BH's
  fenced exit to SR; HO's El Capitan doors open and no metro work site.

## For the lead

- Reconvert THAW (`node tools/thaw_levels/thaw_levels.ts convert`, make_menu_level) - HO, BH, DT, EL, SM, SR, OI change.
