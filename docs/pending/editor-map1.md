# Level editor feature map, rows 1-6: speed-ups, jump boosts, breakables, ledges, proximity nodes, horse spots

Branch `worktree-agent-aa584f878eae76f92` (main 5ee9e5f + the objects-and-life merge merged in). **NOT run in the game**
(editor UI work: the user tests it). Checked offline: clean build (own out\build\map1, own PDB endpoint, dependencies
from the lead's build), `thug2_check.exe` on THUG2PM.exe and on C:\Games\THUG2\Game\THUG2.exe (read only): 0 MISMATCHED,
all 434 early self-tests pass (3 new: `TestEditorMapRailSpeed`, `TestEditorMapTriggers`, `TestEditorMapProxim`);
`check_code_writes`: No overlaps; `npm run typecheck / lint / test` (137 pass, 4 new in tools/game/map_features.test.ts);
26 mutations (`node tools/mutate.ts out/scripts/mut_map1.ts`, the worktree's out\scripts): all caught (one, a box's offset
ignored by SameProxim, was missed at first - a check added).

## What each row does

1. **Rail speed-up** (THAW's RailAccelerate). A rail point's Inspector (Rail section): **Speed-up** checkbox, its rate
   (+% a frame while climbing, 0.5 - 50%, THAW's rails 2 - 10%), **Whole rail** / **Off on the whole rail**. Chevrons
   along each sped-up segment pointing uphill with "+x%"; the level's own RailAccelerate rails (the THAW ports) in grey,
   "level speed-up", read only. How: the point's TriggerScript (its own component, over any shared one) becomes
   `ed_railspeed_<node>` = `<its old script> <...>` (if it had one) + `ed_RailSpeed rate=<r>`; ed_RailSpeed /
   ed_RailSpeedLoop are THAW's two scripts compiled by NeverScript (embedded bytes, source in level_editor_chains.h),
   defined with Script::LoadQBData. The game honours it at once: CTriggerComponent::TriggerNode (0x00434A60, called
   from the skater's code 0x00509433 / 0x0050AAA3 / 0x0050AE59) reads the node's TriggerScript when it fires.
   Records: point `speed=<r>` (drawn rails), `railspeed <node> <r>` (level rails) - **not `speed`**: main's entities
   batch owns `speed` (path mph). Undo: the chains' step. Home on the point takes it off (own undo step); Reset level
   puts every TriggerScript back. Bake: the TriggerScript and the three scripts (fixed ones without their default
   rate - every caller passes it).
   *Found:* the THAW converter's RailAccelerate spawns RailAccelerate2 without passing `rate=`, so every ported rail
   uses 0.05 whatever its rate says (ours passes it).
2. **Jump boost** (T window, **Jump boost** button): a trigger kind `boost` - `trigger_jump_boost speed=<strength>`
   (THUG2's own skutils.qb script: the skater's upward velocity set to it; Berlin uses speed=500 / 900); Inspector
   Strength 100 - 5000 (one undo step a drag); an upward arrow as long as it's strong; the level's own boosts get their
   arrows too and are labelled "Jump boost (speed n)". Moves / turns / resizes / deletes as any editor trigger. Record
   `trigger <name> boost ... strength=n`; baked like any trigger.
3. **Breakable**: a scenery piece's Inspector **Breaks when hit** (one piece selected) + its **Sound** (HitGlassPane2x,
   HitMetalCan, HitTrafficBarrier, BonkBush, none, or typed). A trigger kind `break` over the piece's box (where the
   piece is now, grown 6): `PlaySound <s>`, `Shatter name=<piece> area=5000 variance=1 spread=1.5 life=3 bounce=0
   scale=0.4 vel_y=60` (BO_ShatteringGlass's), `Kill name=<piece>`; the piece's collision is off while it's breakable
   (`replaces`: the walls module's switch - THUG2's glass is 0x50 too: non-collidable trigger faces). Crack lines over the
   box's four sides, "breaks when hit"; the level's glass (Shatter / ShatterAndDie in a TriggerScript) is now listed as
   kind "Breakable". Broken in play: its box is taken out (no second shatter); opening the editor brings the piece back
   (CSector::SetActive). Unticking / deleting its trigger node / Reset level: the piece as the level has it, collision
   on. Bake: node, box, script, the piece's collision object left out.
4. **Ledges**: Place palette (N) **Ledge**: click points along an edge as a rail (edge snapping, near-end linking,
   Enter / right click finishes); ClimbingNode, Type Ledge, CreatedAtStart (THUG2's own form, ST's TRG_619...), drawn
   as climbing chains. On a ledge point: Insert / Delete / Extend / Link / Unlink (rail tools - ladders keep theirs);
   Inspector **Hang from**: as the game decides / left (HangLeft) / right (HangRight), whole ledge (a level's ledge: its
   fields). Records: point `type=Ledge hang=left|right`; bake: Type Ledge + the flag.
5. **Proximity nodes** (any ProximNode - the editor's or the level's): Inspector **On entering** / **On leaving** (the
   level's TriggerScripts list, as the trigger picker, or typed), **Shape** as the level has it / sphere / box,
   **Radius** or **Size**; resize handles (the trigger handles: drag a dot - sphere: radius, box: a face); outline drawn
   (rings / box) with "in: X out: Y". How: one script `ed_proxim_<node>` = `if Inside {enter} else {exit}` (a branch not
   chosen calls the node's own script with `<...>`), byte for byte NeverScript's form; put in the game's ProximNode
   (+0xA0 script, +0x6C shape, +0x70 radius, +0x7C box) every frame, editor on or off - the node's structure untouched;
   put back as it was when the edit goes, Home, Reset level, a network session. Record `proxim <node> [enter=] [exit=]
   [shape=sphere|box] [radius=] [size=] [offset=]`; undo: the triggers' step. Bake: TriggerScript + the script; a sphere's
   Shape / Radius; a **box isn't bakeable** (the game's BoundingBox shape is a sector's box): baked as a sphere around it,
   warned.
6. **Horse spots**: an orange **H** beside every restart whose restart_types has Horse; a spawn's Inspector **Horse
   spot** checkbox (adds / takes out Horse through the Inspector's own field edit: kFieldSet restart_types - live, one
   undo step, `set ... f.restart_types=`, Home / Reset level). A spawn the editor placed: disabled (its types come from
   its kind - node_create's).

## Files
- New: `src/game/front/level_editor_spots_ui.cpp` (horse checkbox), `src/hook/selftests_editor_map1.cpp`,
  `tools/game/map_features.ts` (+ `.test.ts`).
- Chains / triggers (this agent's area): `level_editor_chains.{h,cpp}`, `level_editor_chains_ui.cpp`,
  `level_editor_triggers.{h,cpp}`, `level_editor_triggers_ui.cpp`, `tools/game/triggers.ts` (+ test helper).
- Small hooks in shared files: `level_editor.cpp` (H badge, palette's 4th chain entry - kChainEntries 4, PlaceEntity's
  base follows it -, HasHandles, AddExtras call, Home: speed-up / proximity), `level_editor_ui.{h,cpp}` (ScenerySection,
  SpawnSection, help rows), `level_editor_layout.{h,cpp}` (`proxim` records), `level_editor_node_create.cpp` (a
  ClimbingNode's extra fields kept: one line), `tools/game/layout_file.ts`, `tools/game/apply_layout.ts`, `CMakeLists.txt`.
- Switch off: `[features] level_editor=0` (no REPLACE involved).

## In-game checklist (NOT run in the game)
Back up `thug2-decomp-layouts\`. Watch thug2-decomp.log (`level editor:` lines, EXCEPTION / RUN-TIME CHECK / DEBUG CRT;
`debug_editor=1` adds "rail speed-up scripts defined").
1. **Speed-up, level rail:** `edit_level.ts BO`, select a sloped rail's point, Rail > tick **Speed-up**: orange chevrons
   on its segment. Leave the editor, grind it uphill: speed grows. Back: rate 10% - stronger. Ctrl+Z / Ctrl+Y step it.
   **Whole rail**: chevrons on every segment. Home on the point: gone (Ctrl+Z back).
2. **Speed-up, a rail with a script** (a rail with a gap / sound TriggerScript): still gets its gap / sound AND the
   speed-up. Fields table shows TriggerScript `ed_railspeed_...`.
3. **Speed-up, drawn rail:** draw a rail up a slope (N > Rail), tick Speed-up, grind it. Ctrl+S, reload the EDITED copy:
   still sped up. Reset level: gone, the level's rail scripts as they were.
4. **THAW level** (a ported THAW level, e.g. the Mall): grey "level speed-up" chevrons on its RailAccelerate rails.
5. **Jump boost:** T > **Jump boost**: a blue box, arrow up. Skate through: thrown up. Strength 2000: higher. Move /
   resize / delete / undo as other triggers. Berlin (BE): its own fans show arrows, labelled "Jump boost (speed ...)".
6. **Breakable:** Boston: select a window / sign piece (one piece), Inspector **Breaks when hit**: cracks drawn,
   "breaks when hit HitGlassPane2x". Leave, skate into it: glass sound, shards, the piece gone; again - nothing. Reopen
   the editor: the piece is back (log "breakable ... piece back"). Untick: collision back (bump into it). Choose Sound
   (none): silent shatter. Delete the trigger node in the Outliner: as untick. Ctrl+Z each.
7. **Breakable baked / saved:** Ctrl+S, reload EDITED: still breaks; the level as shipped: solid, doesn't break.
8. **Ledge:** N > **Ledge**: click 3 points along a roof edge, Enter. Skate / jump to it and hang (grab the ledge): the
   skater hangs and shimmies. Hang from left / right: try both (which is right is for you to judge). Insert / Delete /
   Extend on it. Ctrl+S / reload: still there.
9. **Proximity node:** N > a proximity node (or a level one: BO's TRG_Proxim_In_Library). Inspector: On entering
   `BO_Library_On`, On leaving `BO_Library_Off` (or any visible scripts); skate in / out: they run. Shape box: drag a
   handle - the box grows; sphere: the radius. Put back as the level has it / Home: as it was. Ctrl+S / reload.
10. **Horse:** select a restart: **Horse spot** ticked shows an H; start a HORSE game (offline, 2 players) - that spot is
    used. Untick: H gone. Ctrl+Z.
11. **Bake:** `node tools/game/apply_layout.ts --level <lvl> --game C:\Games\THUG2-n --dry-run` with all of the above in
    a layout: report lines for the speed-ups, boost, breakable, proxim (box warned as a sphere), ledge points; no
    "unsupported record".
12. **Online:** in a network session none of it applies (speed-ups and proximity edits put back; trigger boxes taken out).

## Left / limits
- Not run in the game at all; the rail trigger firing on landing (TriggerType [3, 8, -1]) is THAW's port's assumption.
- Breakables need a piece the Kill command finds by name (a level LevelGeometry piece); an editor copy of scenery has no
  node - Shatter runs, Kill may not hide it.
- A level ledge's hang side and an added spawn's Horse type aren't switchable here (their fields / kind).
- Proximity box shapes are this DLL's (baked as spheres).
