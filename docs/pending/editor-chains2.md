# Level editor phase 4b: chains finished - ladders, trick-object rails, flat ground (NOT ORIGINAL - the user's plan)

Branch `worktree-agent-a3cbcba52d9735182` (off main b853d0d). **NOT run in the game yet** (the user tests the editor by
hand) - built and checked offline only.

## What

1. **Ladders made, deleted, reshaped, turned.**
   - The Place palette (N) has a third chain entry, **Ladder**: click a **wall** where the ladder's foot goes (the
     foot is put 4 units out from the wall and dropped onto the ground below it, within 64), then click where its
     **top** goes (the point straight above the foot nearest the cursor's ray; at least 24 above the foot). It faces the
     wall the first click hit. A click on a floor / ceiling is refused ("click on a wall"). One undo step.
   - **Delete** (key or the Inspector's *Delete ladder*) deletes the whole ladder: an added one is removed, a level's is
     dropped (both points hidden with the editor's marker, their rail manager nodes switched off - `drop` records).
   - **Reshape**: L off, move the foot or the top (phase 4's point moves; an added ladder's node moves too).
   - **Z / X** turn the whole ladder 15 degrees (the Inspector: *Turn left / right*, *Facing back*); **Home** puts a level
     ladder's facing back (and its points, as before). An added ladder's facing is its nodes' Angles; a level ladder's is
     the rail manager's orientation (`facing` records), its node untouched.
2. **Drawn rails are trick objects.** Each drawn rail point gets a cluster - the scenery piece the first click hit (as
   the levels name their rails' clusters: AP's `Cluster=AP_treetrunk03`), else the rail's first point; points drawn on
   from a rail copy its cluster (a level rail's only when it's a TrickObject). When the rail manager is built again they
   are registered with the trick object manager exactly as AddFromNodeArray registers the level's TrickObject rails.
   Inspector **Trick object** (drawn rails only) switches the whole rail; off takes its points out of the manager's name
   map again.
3. **Grindable on flat ground.** Inspector checkbox **Grindable on flat ground** (any rail - drawn or the level's, whole
   rail): RailClear passes its segments. The game has no per-node flag or field for this (see below), so this is ours:
   the log says so when it's switched on, and the tooltip explains it.
4. **apply_layout.ts** bakes the new records (`facing=`, `cluster=`, `grindflat=` on points; `facing` and `grindflat`
   records): a ladder point is `ClimbingNode`, `Type=Ladder`, `CreatedAtStart`, `Angles=(0, facing, 0)` (THUG2's own
   form); a cluster becomes `TrickObject` + `Cluster=`; flat ground the node flag `ed_grind_flat` (a warning: only this
   DLL reads it) and a THUGPRO-parity note.

## What THUG2's ladders are (studied on the shipped levels)

`out/scripts/dump_ladders.ts` (scratch, not committed) listed every ClimbingNode of the shipped levels' node arrays
(C:\Games\THUG2\Game\Data\levels\*\*.qb, read only; shared `ncomp_` structures expanded): about 500 Ladder points - pairs (TestLevel's, AP's palms, PH's / NO's / BA's buildings,
SC's diving boards, AU's...) plus Type Ledge chains (hang / shimmy). A ladder: two ClimbingNodes, `Type=Ladder`,
`CreatedAtStart`, the same `Angles` on both - `(0, a, 0)` or `(pi, a', pi)` - the foot linked to the top (some levels the
other way: FindLadderOnLine handles both, it compares heights); some palms also `TrickObject Cluster=<trunk>`.

How it's found and climbed (our rail manager source, 0x0057C170-0x0057EE65, and the checker it calls):
- AddNode (0x0057D090): a ClimbingNode gets RAIL_FLAG_CLIMBING, Type Ladder RAIL_FLAG_LADDER, CreatedAtStart
  RAIL_FLAG_ACTIVE, and its orientation - its Angles' matrix (0x0041A730) as a quaternion (0x0042E1F0) in m_climb (+0x30).
- FindLadderOnLine (0x0057DA20): an active LADDER node with a next, within Climb_Max_Snap of the skater's line; the lower
  node is climbed from, its orientation passed to 0x004413E0, which puts the skater at the point on the ladder minus the
  orientation's **at row** times a script distance (`fld d; fmul [edi+0x20..0x2C]; fsubr` - capstone, read only).
  FindLadder (0x0057D880) is the same near a point (getting on at the top / bottom).
- So the at row - `(sin a, 0, cos a)` for `(0, a, 0)` - points **into the wall**. Measured: `out/scripts/ladder_facing.ts`
  (scratch) cast rays from each level ladder's middle along +/- at against the level's collision (front / back faces):
  the nearest *front* face is along +at for the buildings (every PH ladder, all but one of NO's, BO's, BA's, TR's, SC's
  diving boards: 88 of 128 segments in those 8 levels; the rest are trees / palms whose nodes sit inside the trunk's
  skin, and AP's "Climberthing" ring, a Ledge-like loop typed Ladder). The editor's ladders use `facing = atan2(-n.x, -n.z)` of the wall's normal n.

## Trick objects (how they register, and at run time)

- At load, AddFromNodeArray (0x0057E960) registers each RailNode / ClimbingNode with a `TrickObject` flag (or every one,
  when the script integer 0xB675F8F6 is set) with the trick object manager (0x007CE500): 0x005896D0 AddCluster(cluster)
  (refuses one there already; in the graffiti mode 0xB664035D also tells the mode, 0x004F4970 / 0x004F3D10; a 0x58-byte
  cluster put in its table at +0x08) and 0x00589080 AddObject(name, cluster) (the name -> cluster put in the table at
  +0x20 - a Lst::HashTable). The cluster is `Cluster=` or the node's own name.
- Both are plain run-time calls (ParseNodeArray 0x005A37B4 calls AddCluster for other objects too); the park editor gets
  its rails through ParseNodeArray again. A skater grinding: the trick component's list 0x00588A20 looks the rail's name
  up (0x00588380, the +0x20 map) and adds the cluster to the combo's trick objects (graffiti tagging / trick-object
  scoring); 0x00588FF0 (from 0x00555377, 0x0056204E, 0x0056ACEE) tricks a cluster by an object's name the same way.
- **Gaps are not trick objects**: a level's gaps are its nodes' scripts (StartGap / EndGap in TriggerScripts); a drawn
  rail has none, so drawing a rail adds no gap. (Phase 5's trigger volumes are the editor's way to add gaps.)
- Ours: `Chains::RegisterTrickObjects` after each rebuild - the drawn points with a cluster (`g_extras`) added
  (AddCluster + AddObject), those gone or changed taken out of the +0x20 map (`Lst::HashTable::FlushItem`, our source of
  0x005B80D0). Forgotten with the level (the manager is the level's).

## Flat ground: the answer

RailClear (0x0057C600) is the only test: across the rail a quarter of the way along, 6 up, 7 out each way - anything
there: not clear; then 12 down from each side - ground on both sides at the rail's height (within 1) or not 2 below:
not clear. Its only exception is the park editor's level (0xD443A2BC): every rail passes. It takes two **positions**
(no node, no flags), and is called by the rail manager (FindRail 0x0057DDA0, FindJoiningRail 0x0057E170, FindGrindRail
0x0057E2D0) and by the skater's code (0x0043E29F / 0x0043E32B, 0x0050E5FB / 0x0050E68D, 0x00516ABF / 0x00516B2F -
`tools/` scan of E8 calls). Nothing on a node (LipOverride is lip tricks; NoClimbing, HangLeft / HangRight climbing)
reaches it: **there is no flag or field the game honours to skip the test.**

So the option is ours: `Obj::SetRailClearOverride` (rail_manager.{h,cpp}, NOT ORIGINAL, null by default - the original's
behaviour) is asked first, after the park editor's test; the editor sets it once (Chains::Tick) to `FlatGrindable`,
which passes a segment whose ends are (within 0.01, either way round) a marked rail node and its next. Marked: a drawn
point's `grindflat=1`, a level point's `grindflat` record, or any node with the flag `ed_grind_flat` (a baked level).
The marked nodes are found again when the rail manager's array, its count or the marks change. In the game, other
players (THUGPRO, a stock game) can't grind such a rail - the bake warns.

## Layout records (still `thug2-decomp layout 2`; an older reader skips unknown keys / records)

```
point ed_ladder01 ClimbingNode pos=10,0,-5.5 links=<ed_ladder02> facing=-1.57079637
point ed_ladder02 ClimbingNode pos=10,250.25,-5.5 facing=-1.57079637
point ed_rail02 RailNode pos=... terrain=TERRAIN_GRINDWOOD cluster=5EC70001 grindflat=1
facing 00000004 3.14159274       a level ladder point turned (its heading, radians)
grindflat 00000006               a level rail point grindable on flat ground
```

Malformed refuse the file: `grindflat=` other than 0 / 1, a bad `facing=` / `cluster=`, a `facing` record without its
heading or twice for a node, a `grindflat` record without a checksum.

## Files

- `src/game/front/level_editor_chains.{h,cpp}`: the model (Point facing / cluster / grindFlat, State facings /
  grindFlat), records, ladder helpers (FacingIntoWall, FacingDirection, LadderFoot, LadderTop, SameSegment), LadderClick,
  LadderOp (delete / turn / facing back), kOpTrickObject / kOpGrindFlat, RebuildRails (ladder orientations, trick
  objects), FlatGrindable + its cache, the view.
- `src/game/front/level_editor_chains_ui.cpp`: the Inspector's ladder section (facing, turn, facing back, delete) and
  the rail's Trick object / Grindable on flat ground checkboxes.
- Small hooks: `level_editor.cpp` (the palette's Ladder entry, ladder clicks, Z / X on a ladder, Delete on a ladder, Home
  puts the facing back, the drawing keys line), `level_editor_ui.cpp` (two help rows), `level_editor_node_create.{h,cpp}`
  (a ClimbingNode's fields: CreatedAtStart; a copied `like` node's ed_grind_flat taken off), `src/game/obj/rail_manager.
  {h,cpp}` (SetRailClearOverride - 8 lines, NOT ORIGINAL).
- `tools/game/layout_file.ts`, `tools/game/apply_layout.ts`, `tools/game/apply_layout.test.ts` (the bake; one new test).
- `src/hook/selftests_editor_chains.cpp` (2 new tests, 2 extended).

## Checks (offline)

- Clean build in the worktree's own `out/build/chains2` (own PDB endpoint), no warnings.
- `thug2_check.exe` on THUG2PM.exe and on C:\Games\THUG2\Game\THUG2.exe (stock, read only): 0 MISMATCHED, all 409 early
  tests pass. New: `TestEditorChainsLadder` (facing into +x / -z / diagonal walls, the foot out from the wall and on the
  ground - none below, too far below, floors refused - the top from a flat and a rising ray, too low, looking down the
  line), `TestEditorChainsFlat` (segments either way, tolerance; RailClear passes what its override accepts, before any
  feeler, the positions passed through). Extended: `TestEditorChainsRecords` (ladder points, clusters, grindflat, facing
  / grindflat records round trip; 6 malformed forms; sorting; states differ by facing / cluster / flat), 
  `TestEditorChainsNodes` (ClimbingNode is a chain class; a ladder point's fields).
- Mutations (`node tools/mutate.ts out/scripts/mut_chains2.ts`, tests EditorChains), **all 18 caught**: a cluster not
  written, grindflat= any value, a facing record twice accepted, grindflat records unsorted, facing / cluster / flat
  marks not compared, Empty ignoring flat marks, the ladder facing away from the wall, the foot pushed into the wall,
  ground any depth below taken, a foot on floors, no minimum height, the top's formula, segments matched one way only,
  ClimbingNode not a chain class, RailClear's override ignored (caught as a crash: the feelers then run offline),
  a ladder point not CreatedAtStart.
- `node tools/check_code_writes.ts`: No overlaps. `npm run typecheck`, `lint`, `test` (110 pass).

## In-game test checklist (for the user) - NOT run in the game yet

Use this branch's DLL. Watch `thug2-decomp.log` (`level editor:` lines; `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`);
`[features] debug_editor=1` also logs "rail manager built again", "n drawn rail points are trick objects" and "n rail
segments grindable on flat ground". Back up `thug2-decomp-layouts\` first.

1. **Draw a ladder:** `node tools/game/edit_level.ts` a level with plain walls (Boston, Berlin). N > *Ladder*. Click a
   floor: refused ("click on a wall"). Click a wall near its foot, then higher up the wall: a two-point purple ladder
   appears (log `added ed_ladder01 / ed_ladder02 (a ladder, facing N degrees)`). Leave the editor, skate to it, press up
   at its foot: the skater climbs it facing the wall, and gets off at the top (if there is a ledge to get onto).
2. **Facing:** select a ladder point: Inspector *Ladder* - "Facing N°". Z / X (or Turn left / right) 15° steps: climb it
   again - the skater faces the new way. Turn it 180° (12 presses): the skater climbs with its back to the wall (wrong:
   that's the check that the sign is right - it should face the wall at 0 turns). Ctrl+Z puts each step back.
3. **A level ladder:** Boston's TRG_Ladder01 (or New Orleans'): Z twice, *Facing back* (or Home): as the level had it.
   Delete: both points grey; can't climb it; Ctrl+Z: back and climbable.
4. **Reshape:** L off (Inspector "Whole ladder" unticked), drag the top point up / down: climb it - longer / shorter.
   Drag the foot. L on: the whole ladder moves.
5. **Delete a drawn ladder:** Del - both points go; Ctrl+Z - both back.
6. **Trick object:** draw a rail on a bench / ledge edge (N > Rail, phase 4). Inspector: *Trick object* ticked, "cluster
   <the bench's sector>". Grind it in a combo; in a Graffiti game (offline, 2 players if possible) or with a trick-object
   goal the rail's piece counts like the level's rails (graffiti tags the bench). Untick: no longer counted. A level
   rail's checkbox is greyed (its own TrickObject setting).
7. **Flat ground:** draw a rail on flat open ground (Alt+click so it isn't snapped). Leave the editor: it can't be
   ground (the game's own rule). Back in the editor, tick *Grindable on flat ground* (log: "...grindable on flat ground -
   RailClear ... passes it"): grind it now, including landing on it from an ollie and joining onto it from a neighbour.
   Untick: not grindable again. Also try a level rail you moved down to the floor.
8. **Save / reload:** Ctrl+S: the layout has `point ed_ladder.. ClimbingNode ... facing=`, `cluster=`, `grindflat=1`,
   `facing` / `grindflat` lines. Load the level's EDITED entry: ladders where they were, climbable, facings kept; the
   flat rail grindable; trick objects counted.
9. **Undo / Reset level:** Ctrl+Z steps back through each ladder / facing / checkbox change; Ctrl+Shift+Delete > Reset
   level: drawn ladders gone, level ladders' facings back, flat marks off, trick objects of drawn rails gone with them.
10. **Autosave:** draw a ladder and turn it, wait for `autosaved`, kill the game, reopen the editor there: Restore brings
    it back turned.
11. **Leaving the level:** with drawn ladders / flat rails, load another level: nothing carries over (no flat grinding on
    the new level's rails); the log shows no errors.
12. **Bake:** `node tools/game/apply_layout.ts --level <lvl> --game C:\Games\THUG2-n --dry-run`: the report lists the
    ladder points added and warns "n rail point(s) grindable on flat ground: the node flag ed_grind_flat - only this
    project's DLL reads it". Installed into a workspace: the baked ladders are climbable without the layout (the game's
    own load), the flat rail grindable with this DLL.
13. **F1:** help lists "N > Ladder" and the rail checkboxes.

## Known limits

- The ladder's foot is 4 units off the wall (THUG2's sit 1-16 off); the skater's distance from it is the game's script
  value. A ladder with nothing to get onto at its top just ends (the game's behaviour).
- No preview of the foot between the two clicks (the HUD says what's next).
- A level ladder's facing is changed in the rail manager only (its node's Angles stay - nothing else reads them); the
  bake writes them.
- A level rail's trick-object setting isn't switchable (its TrickObject / Cluster fields: the Inspector's fields, phase 6,
  on reload). Turning a drawn rail's trick object off leaves its (empty) cluster in the manager until the level goes.
- A baked level's `ed_grind_flat` flag can't be switched off in the editor (the checkbox shows it; unticking removes only
  a layout mark) - re-bake without it.
- Flat-ground rails are this DLL's: online, other players can't grind them (layouts aren't applied online anyway; baked
  levels warn).
- Ledges (ClimbingNode Type Ledge) count as "ladders" in the editor: they turn and delete whole the same way; drawing makes
  only ladders.
