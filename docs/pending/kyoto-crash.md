# THAW Kyoto (kyoto) load crash - 2026-10-21

**Symptom:** the main install (DLL main 20ebef7) crashed loading THAW's Kyoto from the level select: 0xC0000005 writing
00000004, EAX 0, ECX = checksum "Cluster", EDX = 0x006F2338 (the CComponent pool pair). Symbolized with a rebuild of
20ebef7: `NewTypedComponent` (struct.cpp:905) <- `CStruct::AddChecksum` <- `AddParameter` <- `ParseStructure` <-
`ParseArray` <- `ParseSymbolDefinition` <- `ParseQB` <- `LoadQBData` <- 0x005A5990 (parse a .qb file) <-
`CNodeArrayAsset::FinishLoading` <- `CAssMan::LoadAsset` <- the load script. Reproduced in C:\Games\THUG2-2 from a fresh
start (`-level kyoto`), the same stack; the log's new line said `pool CComponent full: all 86000 items`.

**Cause: the converted data - Kyoto's node array is bigger than the original's script component pool allows.**
- Every structure entry in a loaded .qb is a script component from the CComponent pool, 86000 x 16 bytes, made by
  CreateScriptPools (0x0046F520; ours is equivalent). `Mem::Pool::Allocate` (0x005BA3D0) returns null when it's empty
  and the parser writes into it unchecked - the original and ours alike.
- Measured in the game: 51,247 components in use when a level's .qb starts loading (THUG2's scripts and ours).
  Kyoto's node array, expanded by the converter (`nodes.expandIncludes`), needed 38,677 (4,298 nodes, 2,900 of them
  rails with Class / Type / CreatedAtStart / TrickObject / Cluster / TerrainType each): 89,900 > 86,000.
- THUG2's own levels: the largest is SE with 29,053 (BA 26,562, NO 24,232, BE 23,750...). They share each node's
  common parts as global structures the node names (BE: `ncomp_RailNode_1 = {Class=RailNode Type=Concrete
  CreatedAtStart TrickObject Cluster=g_fountain_ledge_01 TerrainType=TERRAIN_GRINDCONC}`), which the THAW converter
  had undone. The THUG1 converter already redoes it (`compressIncludes`, PC-THUG1-17, for the same crash in HI / NJ).

**Fix (converter):**
- `tools/level_port/nodes.ts`: `shareIncludes(converted, names, prefix, shared?)` (shared, new: THUG1's
  `compressIncludes` grouping moved here; `OWN_KEYS` too) - one `<level>_inc_<n>` structure per set of identical
  common parts at least two nodes have, the nodes keeping Pos / Angles / Name / Links / TriggerScript. THUG1's
  `compressIncludes` now calls it (same output: same grouping, order and names).
- `tools/thaw_levels/thaw_levels.ts` convertNodeArray: PC-THAW-13 `shared_parts` (required; register entry in
  tools/compat/thugpro/thaw_levels.json and README.md): the rail nodes' common parts shared (rails only - the text
  passes after it, stripMissingTrickObjects and groundRestarts, read geometry / restart nodes' own entries). The
  report says `<n> include structures for the rail nodes' common parts`.
- Kyoto now: 26,341 components (below SE); in the game 74,621 in use after loading it (peak 74,708 of 86,000).
  z_dn 19,682 -> 15,687.
- `src/game/core/pool.cpp` (NOT ORIGINAL, log only): a pool that runs dry is logged once (`pool <name> full: all
  <count> items (<size> bytes each) in use`) - the next such crash names its pool. Pools without a name (self-tests'
  fakes) aren't logged.

**The lead must:** reconvert THAW (`node tools/thaw_levels/thaw_levels.ts convert` - all 15, every level's .qb
changes) and run `node tools/menu_level/make_menu_level.ts` for the main install; rebuild / install the DLL for the
pool log (not needed for the fix itself).

**Verified:** typecheck, lint, npm test (153 pass); verify build; thug2_check: 507 early self-tests pass;
check_code_writes: no overlaps. In C:\Games\THUG2-2 under its lock (all of THAW reconverted from this branch +
make_menu_level, this branch's DLL): `-level kyoto` reached free skate (picture: the skater in Kyoto's street), ran
150 s and again 90 s with the final DLL, responding, no EXCEPTION, no pool full; z_ma, z_dn, skatepark 75 s each:
loaded, responding, no errors. Not tried: the other 11 THAW levels in the game, THUG1 levels (its converter's output
should be unchanged - not reconverted here).
