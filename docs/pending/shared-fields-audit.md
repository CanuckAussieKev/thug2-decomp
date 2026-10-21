# Shared node fields: THUGPRO's Kyoto, and every ported level's script components - 2026-10-21

Research only (no converter or game change). Follows `docs/pending/kyoto-crash.md` (PC-THAW-13 `shared_parts`).

**How counted:** every structure entry in a .qb's globals (nested structures included; script bodies not - they are
bytecode until run) = one CComponent, as the kyoto agent counted. Read with `tools/level_port/qb.ts`; THUGPRO's files
read-only from `%LOCALAPPDATA%\THUG Pro\data\pre\<level>_scripts.prx`, ours from the main install's overlay
(`C:\Games\THUG2\Game\thug2-decomp-data\levels\<level>\<level>.qb`, already the reconverted THAW). Calibration: THUG2's
SE counts 28,672 this way (the kyoto agent: 29,053), Kyoto 26,056 (agent 26,341; in the game it added ~23,400 -
the count is on the safe side by ~3k, probably the front end's level being unloaded first).
**Budget:** 86,000 - 51,250 in use when a level's .qb loads = **34,750** for the level's qb (conservative).

## 1. How THUGPRO's Kyoto avoids the crash

| | nodes | nodes naming a shared structure | shared structures | node array | whole qb |
|---|---|---|---|---|---|
| THAW's Kyoto expanded (ours before PC-THAW-13) | 4,298 | 0 | 0 | 38,677 | ~40k |
| ours now (rails shared) | 4,298 | 2,872 (rails) | 284 `kyoto_inc_<n>` | 24,360 | 26,056 |
| THUGPRO's Kyoto | 4,338 | 3,970 (all kinds) | 428 `Z_JA_NodeArray_compressed_node_<n>` + 3 `ncomp_*` | 21,966 | 24,470 |

- **Same nodes, shared fields:** THUGPRO keeps all of Kyoto's nodes (40 more than ours - its spawns / CTF); its
  compiler shares every node kind's common parts (rails, LevelGeometry `Class CreatedAtStart TrickObject Cluster
  CollisionMode`, waypoints, proximity nodes, peds) - nodes keep Name / Pos / Angles / Links. No split node array.
- **And a much bigger pool:** THUGPRO's `THUGPro.exe` has the CComponent pool push at 0x0046F577 patched on disk to
  92,000 (THUG2PM: 86,000; CStruct 24,800 vs 19,800, CArray 8,000 vs 7,000, CScript / cache 600 vs 300), and its
  shipped `thugpro.dll` at start-up writes larger values again (`mov eax, addr; mov [eax], imm` sequence at file
  offset 0x14ad0): **CComponent 184,000**, CStruct 49,600, CVector 19,000, CPair 1,600, CArray 16,000, 0x46F610
  45,000, CScript / cache 1,200, plus 8-32 MB heaps at 0x5BBxxx. Morten's thugpro_proxy source
  (`Code/SkPro/Miscellaneous.cpp`) has the same patch commented out with older values (CComponent 100,000) - the
  shipped DLL is newer than the repository.
- So THUGPRO's Kyoto would fit even in 86,000, but THUGPRO doesn't rely on that: its own THPS4 ports are **not**
  shared at all and are bigger than ours (SCH 33,449, ALC 30,095, SF2 28,137 - over our budget's comfort) - they work
  only because of the 184,000 pool.

## 2. Every ported level (main install's overlay)

Who shares today: **THUG1** all node kinds (compressIncludes -> shareIncludes, PC-THUG1-17, when ported); **THAW**
rail nodes only (PC-THAW-13); **THPS3 / THPS4 none**. "All kinds" = shareIncludes with every node taking part
(simulated on the shipped qb; THUG1 levels are already there).

| level | game | shared now | count now | headroom | rails shared | all kinds |
|---|---|---|---|---|---|---|
| ny | THUG1 | all | 30,343 | **4,407** | - | (30,343) |
| jnk | THPS4 | none | 28,041 | **6,709** | 19,219 | 15,746 |
| sd | THUG1 | all | 26,827 | 7,923 | - | - |
| nj | THUG1 | all | 26,692 | 8,058 | - | - |
| shp | THPS3 | none | 26,510 | 8,240 | 20,395 | 16,608 |
| kyoto | THAW | rails | 26,056 | 8,694 | (26,056) | 23,555 |
| ru | THUG1 | all | 26,046 | 8,704 | - | - |
| sch | THPS4 | none | 25,946 | 8,804 | 17,310 | 14,864 |
| thps3_ap | THPS3 | none | 25,098 | 9,652 | 19,404 | 15,974 |
| alc | THPS4 | none | 24,229 | 10,521 | 15,780 | 13,684 |
| vc / hi / fl | THUG1 | all | 24,111 / 23,837 / 23,822 | ~10.7-10.9k | - | - |
| sf2 | THPS4 | none | 23,220 | 11,530 | 16,065 | 13,405 |
| thps3_la | THPS3 | none | 22,914 | 11,836 | 18,578 | 15,135 |
| tok / cnv | THPS3 / THPS4 | none | 21,849 / 21,377 | ~13k | 16,750 / 15,472 | 14,304 / 13,332 |
| z_sm, z_el, z_dt, lon, kon, z_bh, z_sr, sz | THAW / THPS4 | rails / none | 20,200-21,800 | 13-14.5k | | z_el 17,820, lon 12,701 |
| the rest (39 levels) | | | under 20,000 | over 14.7k | | |

(THUG2's own largest for comparison: SE 28,672, BA 26,229, NO 23,906 - with THUG2's ncomp sharing.)

- **Nearest the limit:** NY (THUG1, already shared - only 4.4k left; sharing more needs something else, e.g. fewer
  nodes), then Junkyard (THPS4, 6.7k), then SD / NJ / Cruise Ship / Kyoto / RU / College (~8-9k). None over today.
- **Gains:** rails only already takes THPS3 / THPS4 levels down by 25-35% (JNK -8.8k, SCH -8.6k, ALC -8.4k); all kinds
  another 5-15% (JNK 15.7k: -12.3k overall). For THAW, all kinds saves 1.7-3.1k per level (Kyoto -2.5k).
  Each component is 16 bytes, so the memory itself is small (10k = 160 KB); what it buys is pool headroom for
  level-editor additions (each added node ~5-10 components, its own fields unshared), online play, and scripts'
  run-time structures. Load time: fewer components to parse and a smaller qb (marginal). Nothing visible in play.

### Is sharing every node kind safe?

Not as the converters are ordered now: several **text passes run after `nodeArrayNs` and read a node's own text**
(regex over each `{...}`), so a field moved into an include is invisible to them:

| pass (tools/level_port/nodes.ts) | reads | with the field shared |
|---|---|---|
| stripMissingTrickObjects (THPS3, THPS4, THAW, THUG1) | Class=LevelGeometry, TrickObject, Cluster | geometry without a sector keeps TrickObject in the include: **ParseNodeArray crash** (THPS3 AP / THAW Minneapolis). THUG1 is safe: compressIncludes strips on the structures before sharing. |
| groundRestarts / groundRestartsAt (THPS3, THPS4, THAW, THUG1) | Class=Restart, restart_types, Name | restarts sharing Class are skipped silently (the include has no Name) |
| absentInNetGames (via netGameNodes; THUG1 direct) | CreatedAtStart, TriggerScript, Class=Pedestrian/Vehicle | peds / vehicles: the include itself gets marked (works); path followers: CreatedAtStart in the include, TriggerScript in the node - **missed** (network-game crash class) |
| netRestarts | Class=Restart, restart_types | the include is edited for all its sharers (works) |

**Existing latent gap (THUG1, today):** groundRestartsAt (PC-THUG1-5) never sees restarts whose Class went into an
include - NY: 56 of 67 restarts (e.g. `ny_inc_0 = {Class=Restart Type=Player1 CreatedAtStart RestartName="P1: Restart"
restart_types=[...]}`), also SD 36, VC 39, RU 38. A restart over nothing / over a kill zone there isn't caught. My
approximate check (scripts calling Obj_FollowPath* directly) found no path follower currently missed by
absentInNetGames, but the pass can't see it if one appears.

## Recommendation

1. **Share last, not first:** make sharing a final step that runs on the finished node array - after
   stripMissingTrickObjects, netGameNodes and groundRestarts(At) - e.g. in each converter's qb write (compile the text,
   then shareIncludes on the compiled qb's node array and write it back with the include structures; or parse the
   final text). Then every pass keeps reading nodes' own entries and sharing can cover every node kind safely.
2. Then turn it on for **all four games, all node kinds**: THPS3 / THPS4 first (biggest gain, nothing shared today -
   JNK 28.0k -> 15.7k, SHP 26.5k -> 16.6k), THAW all kinds (-2-3k each), THUG1 moved to the same final step (fixes the
   groundRestarts gap above with no output change otherwise). Each as a port change entry (PC-*), reconvert, and run
   the parity / thugpro_compare tools (nodeArray already merges includes, so comparisons are unaffected).
3. NY stays the tightest (30.3k shared). If the level editor's additions or online play need more, the alternative is
   THUGPRO's: a larger CComponent pool in our CreateScriptPools (NOT ORIGINAL; cheap: 16 bytes an item), which the
   pool-full log would justify. Not needed for any level today.

Tools used (scratch, not committed): component counter, all-kinds simulation and include-kind check over
`tools/level_port/qb.ts`; THUGPRO's exe / dll read for immediates. NOT run in the game.
