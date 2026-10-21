# shared-fields - every node kind's common fields shared, last, in all four converters - 2026-10-21

Follows docs/pending/kyoto-crash.md (PC-THAW-13, rails only) and docs/pending/shared-fields-audit.md (the
recommendation: share last, every node kind, all four games). Two agents' work (the first one stopped; continued and
finished here).

## The change
- `tools/level_port/nodes.ts`: `shareNodeFields(data, arrayName, prefix)` - a converter's LAST step on a level's
  compiled .qb (scripts appended): the node array's common fields (everything but `OWN_KEYS`: Pos / Position / Angles /
  Name / Links / TriggerScript) of every node kind as `<level>_inc_<n>` global structures the nodes name (one per set of
  identical fields at least two nodes have; `shareParts`, THUG1's old compressIncludes grouping), defined just before the
  node array; every other byte kept. As THUG2's own levels (`ncomp_*`) and THUGPRO's compiler do it.
  - Runs once (a node array naming a global structure already is an error) and checks itself: `sharedDifferences`
    (every node resolves - its includes put in place - to the same fields as before; every other global the same) or
    it throws.
  - `assertUnshared`: the text passes that read nodes' own fields (stripMissingTrickObjects, absentInNetGames,
    netRestarts, groundRestarts) refuse a node array that is shared already.
  - `unshareNodeFields`: make_menu_level's front end reads the converted THPS4 skate shop's nodes by their Class -
    it gets them expanded.
- Converters (each shares in its .qb write, so the TrickObject strip / groundRestarts rewrite go through it too):
  - THPS3 `writeLevelQb`: **PC-THPS3-18** (required; new - main's PC-THPS3-17 is the collision-only Create / Kill).
  - THPS4 `writeQb`: **PC-THPS4-14** `shared_fields` (required; new).
  - THUG1 `writeQb`: **PC-THUG1-17** now this (thug1_port.compressIncludes removed - it shared before the text passes,
    so groundRestartsAt never saw restarts whose Class went into an include: the gap the audit found).
  - THAW `writeLevelQb`: **PC-THAW-13** `shared_parts` now every node kind, last (was rails only, before the text
    passes).
  - Register entries: tools/compat/thugpro/README.md and the four `<game>_levels.json`.
- THUG1's THUGPRO script check (`thugpro_new.scriptCheck`, TPN-THUG1-3): with the nodes no longer shared when it runs,
  it saw geometry without a scene sector as such and refused 8 THUGPRO "set" scripts main's output had (NJ's
  TRGP_LO_forleasedown03-08Script, SD_Race_SwitchGate2, HI_Initialize_Objects, HN2_Heli_Move) - all only `Kill` that
  collision-only / trigger geometry. Before, compressIncludes' sharing had hidden the nodes' Class and they passed by
  accident. Now a name of geometry kept without a sector passes when the script only uses it as `Create / Kill /
  IsAlive Name=` (THUG2's Create 0x005A2E50 / Kill 0x005A32C0 skip a missing sector - PC-THPS3-17's finding); any other
  use is still refused. Output: the same scripts as main.
- Tools: `tools/level_port/components.ts` (a level's script-component count, the audit's method),
  `tools/level_port/compare_nodes.ts` (two conversions compared as THUG2 resolves them: every node with its includes
  in place, every other global, every script's bytes, and the component counts).
- DLL (NOT ORIGINAL, log only): `Script::LogScriptPoolUse` (src/game/script/qb.cpp) - once a `-level` level is up
  and settled, start_level.cpp logs `script pools in use in <level>: CComponent <used> (peak <peak>) of <count>,
  CStruct ...`; tools/game/load_check.ps1 prints the CComponent / CStruct part and any `pool ... full` line.

## Verified
POOLS_AND_CHECKS

## The lead must
- Reconvert all four games (every level's .qb changes; scenes, collision, textures don't):
  `node tools/thps3_levels/thps3_levels.ts convert`, the same for thps4_levels, thug1_levels, thaw_levels, then
  `node tools/menu_level/make_menu_level.ts` for the main install.
- Install the DLL only for the pool log (not needed for the data change).
