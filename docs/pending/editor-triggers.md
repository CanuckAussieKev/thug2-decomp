# Level editor phase 5: trigger volumes, kill zones, teleports, gaps (NOT ORIGINAL - the user's request)

Branch `worktree-agent-a76a139e4b17c7051` (off main b1c7534). **NOT run in the game yet** (the user tests the editor by
hand): built and checked offline only. Edited levels only, offline: nothing here is put in place in a network session
(the edited-levels guard stays, and a second guard drops the volumes if a network session starts).

## How the game's triggers work (found in THUG2PM.exe)

- **Trigger volumes are collision faces with 0x40, the skater passing through them because of 0x10.** The trigger
  component's update **0x00434D90** (an object that moved since its last frame) casts a feeler (0x00417760) from where
  it was to where it is with `SetIgnore(0, 0x50)` - only faces with both flags - over static **and movable** collision
  (`GetCollision(1, 0)`); its callback **0x00434D70** hands each accepted face to **0x00434BF0**: the hit's collision
  object's checksum (CollisionHit +0x34 = feeler +0x58) and the face's 0x40 (feeler +0x6D). When no movable's owner is
  being tested and no object was hit, that calls `CTriggerComponent::TriggerNode` (0x00434A60) with the checksum alone:
  `GetNode(checksum)` - so **a trigger's collision object is named like the node whose TriggerScript runs** (a level's
  trigger sectors are LevelGeometry nodes, usually collision only). The skater's walk component casts the same feeler
  (0x0043B4C4).
- The world's movable collision list (0x00691978 / RegisterMovable 0x004177B0) is tested without a cache when no cache
  is current - the skater's physics sets 0x00691974 only around its own feelers (0x0052CEF0 / 0x0052D300 clear it), so
  the trigger component's feeler goes through the list. Through a cache, movable boxes would be skipped (their cache
  geometry, slot 20 0x005BA1D0, is null) - not the case here.
- **Kill zones**: TriggerScripts calling `SK3_KillSkater` / `SK3_KillSkater_Water` / `SK3_KillSkater_Pungee`
  `nodename=<restart>` (sk4_scripts.qb: the panel message, then SK3_Killskater_Finish -> KillSkater node=; levels wrap
  it, e.g. BO_KillskaterOutOfBounds). **Teleports**: `TeleportSkaterToNode nodename=<node>` (Obj_MoveToNode Orient
  NoReset). **Gaps**: `StartGap GapID=<id> flags=[...]` on one trigger, `EndGap GapID=<id> score=<n> text="..."` on
  another (BO_scripts.qb). The flags in THUG2's levels: PURE_AIR / _GRIND / _RAIL / _MANUAL, CANCEL_GROUND / _AIR /
  _RAIL / _GRIND / _MANUAL / _WALL / _WALLRIDE / _LIP / _HANG / _WALK, REQUIRE_AIR / _RAIL / _GRIND / _MANUAL / _LIP /
  _WALL / _WALLRIDE / _WALK.
- **The gap checklist** is built at the end of `CFunc_ParseNodeArray` (0x005A34E0, at 0x005A3961) unless the script
  integer 0xF7ECD937 is set: the skate module's checklist (0x005656C0 -> the career's for the current level,
  0x00584110(-1)) `Build` (0x00578AC0): every EndGap any node's TriggerScript reaches (ForEachNodeScriptCall walks every
  node array - the editor's overlay too) with a score and a non-empty text; one already there (name and score) is kept.
- `TriggerNode` skips a node the skate module leaves out (0x00565BB0: flag 0x68910AC6 and not a network game / too few
  players) - an added node has no such flag.

## What it does

- **T** (or the toolbar-less key; `[editor] triggers=0/1` remembers it): the trigger volumes drawn and the **Triggers
  window** (top right of the view, movable). Old HUD (`editor_ui=0`): T says it needs the UI.
- **The level's volumes** (found when the editor starts: every collision sector's faces with 0x40, the node of its name,
  its TriggerScript walked with `SkateScript::ForEachScriptCall` for the calls above, down 5 levels as the gap list's
  build does): drawn within 8000 units (300 nearest) as their trigger faces' outlines (64 faces or fewer; a bigger one -
  water - as its box), coloured by kind - **kill red, teleport purple (an arrow to its target), gap start green, gap end
  yellow, a script cyan, no script grey** - with a label within 3000 units ("Kill zone (water) -> TRG_x  TRG_Water01",
  "Gap end: Statehouse Stair Spine (250)"). Listed in the Triggers window (click: inspected; with a node, selected).
  A click on a volume in the view selects it (its node when it has one; Alt passes volumes; gizmos and arrows win).
- Inspected / selected level volume: kind, faces, TriggerScript, box; **Turn off** (its collision object's flag 4 -
  the world queries skip it - layout `trigger_off`; grey "(off)" outline) / **Turn on**; **Make an editable copy** (an
  editor volume of the Run-a-script kind over its faces' box, running the same TriggerScript, the original off while
  the copy is there - `replaces=`). Moving a level's own volume in place isn't offered: it is collision without
  geometry, which the scenery code can't move (level_editor_view.h); the copy is how it moves / resizes.
- **Adding** (the Triggers window's buttons; placed on the ground where the view looks, facing the camera's heading,
  selected, one undo step): **Kill zone** (400 x 200 x 400, `SK3_KillSkater nodename=<the nearest Player1 /
  Multiplayer restart>`), **Water** (800 x 100 x 800, `SK3_KillSkater_Water`), **Teleport** (`TeleportSkaterToNode
  nodename=<the nearest restart>`), **Script** (runs a script chosen in the Inspector), **Gap** (two volumes: the start
  where the view looks - `StartGap GapID=ed_gapNN flags=[CANCEL_GROUND]` - and the end 600 units further on -
  `EndGap GapID=ed_gapNN score=100 text="Editor Gap N"` - an arrow from start to end).
- **The Inspector** (an added volume selected): Kind; Size (x, y, z - one undo step a drag); Kill: Restart (the level's
  spawns) and Water; Teleport: To (spawns) or any node by name; Script: the scripts the level's nodes' TriggerScripts
  name, or any by name (and what it calls); Gap: its name (the gap list's text), score, flags (StartGap's), "Select the
  other end" (both ends share name, score, flags); "Not active: why" (no restart, no script, a name that isn't there...)
  or "Active". It moves (drag, arrows, W A S D / Q E), turns (Z / X, the heading), drops (G), goes Home, is deleted
  (Delete; Ctrl+Z brings it back) as any added point node: it **is** one (the Triggers family in the Outliner).
- **Undo / redo**: adding, the Inspector's changes, Turn off / on, editable copies are one step each (a new step kind:
  the whole trigger state - specs, their nodes, the volumes turned off); moves / turns are the node steps; Delete is the
  added-node step. Reset level removes them all and turns the level's back on.
- F1 (the UI's help): a Triggers section; the old HUD's: a line. Screenshot sidecar: a `triggers` line.

## How run-time trigger collision is added

- Each added volume is a node in the editor's overlay node array (phase 3's mechanism, level_editor_node_create.h):
  `GenericNode` type `ed_trigger` (`Nodes::Classify` puts it in the Triggers family), `TriggerScript=<name>Script`.
  NodeCreate's `Spec` got two additive fields: `extra` (fields after the kind's own) and `owned` (another module writes
  its records - not a `node` record).
- Its collision is an **`Nx::CCollMovBBox`** (our decompiled movable box, vtable 0x00645BC0: one set of face flags for
  every face) made by the editor: checksum = the node's name, face flags **0x50**, its box / position / orientation
  (SetOrientation 0x00412070) from the node's pos and heading and the size, registered with `Nx::RegisterMovable` - the
  trigger component's feeler meets it exactly as it meets a level's trigger faces, and calls the node's TriggerScript.
  Its owner (slot 10) is 0, so 0x00434BF0 takes the plain GetNode path. A box is hit where a line **enters** it
  (Mth::BBox::LineIntersect: a line starting inside, or inside throughout, doesn't hit), so a volume fires once on the
  way in. Feelers that ignore 0x10 (all but the graffiti ones, as for the level's trigger faces) never meet it; the
  editor's ground casts don't either (static only).
- Kept in step every frame while editing (the node moved / turned, the size changed: placed again); unregistered and
  freed when its node goes (Delete, undo, Reset level), when the level's node array is taken out (NodeCreate's new
  `SetArrayRemovedListener` - the volumes never outlive their level), and at once in a network session (`Guard`, every
  frame, editor on or off).
- **Its script** is compiled by the editor (`Triggers::Body`: tokens byte for byte as the game's own compiled level
  scripts - the self-test compares with Boston's BO_Gap_StateHouseSpine1 / 2 and BO_GapStart_HighSlap) and defined
  through the game's own loader, `Script::LoadQBData("editor\thug2_decomp_triggers.qb", ...)` - one `.qb` image with
  every volume's script (`script <name>Script ... endscript`, then the names' checksum-name tokens): an unchanged script
  is left as it is, a changed one replaced (running ones restarted, as for any .qb reload). A volume that lacks
  something gets an empty script. "Run a script" is a call of the chosen script (so a node's TriggerScript never
  changes). The scripts stay defined after the level goes (harmless; redefined when needed).

## Gap checklist rebuild

- After every change that touches gaps (a script redefined, an editor gap end appearing / going / renamed / rescored),
  the skate module's checklist (0x005656C0) is **built again with the game's own `CGapChecklist::Build`** - what the
  level's load does - so the editor's EndGaps are added like the level's (sorted by score then name). The script integer
  0xF7ECD937 is honoured as ParseNodeArray does.
- Build only adds: the editor's gaps no longer wanted (deleted, renamed, rescored) are taken out of the list (the
  `CGapCheck` destroyed: 0x005794E0), unless the level has its own gap of that name (its EndGaps walked once).
- When the level's node array goes, the editor's gaps are taken out of that level's checklist (the career keeps one per
  level), so the level as shipped never lists them. **Caveat:** saving the game while an edited level with new gaps is
  loaded writes those gaps into that level's career gap list (they'd show in its list after loading that save) - avoid
  saving the career during editor tests.

## Layout records (version 2 kept; older readers skip them)

```
trigger ed_trigger01 kill pos=-120.5,8,330.125 yaw=1.57079637 size=400,200,400 target=TRG_Restart01
trigger ed_trigger02 kill pos=... yaw=0 size=800,100,800 target=TRG_Restart01 water=1
trigger ed_trigger03 teleport pos=... target=TRG_Restart_StateHouse
trigger ed_trigger04 script pos=... script=TRG_SFX_TrigBox_CrowdScript replaces=8E1A2B3C
trigger ed_trigger05 gapstart pos=... gap=ed_gap01 flags=CANCEL_GROUND score=100 text="Editor Gap 1"
trigger ed_trigger06 gapend pos=... gap=ed_gap01 flags=CANCEL_GROUND score=100 text="Editor Gap 1"
trigger_off 1A2B3C4D  # TRG_Water01
```

- `pos` required; unknown keys skipped; a repeated key or bad value refuses the file (as `node` records do). Names that
  aren't known are written `0x` + 8 hex digits. The gap's text loses quotes ('), '#' (the layout's comment mark) and
  control characters, at most 60 characters. The trigger nodes aren't `node` records (Capture leaves `owned` specs out).
- Applied after the node / hide / lock records; the message ends "..., N triggers". Saved: "..., N triggers" (added
  and turned off).

## Files

- New: `src/game/front/level_editor_triggers.{h,cpp}` (pure parts + game side), `src/game/front/level_editor_triggers_ui.cpp`
  (ImGui: the Triggers window, the Inspector section), `src/hook/selftests_editor_triggers.cpp` (5 early tests).
- Small hooks (shared files):
  - `level_editor.cpp`: include; Triggers::Start / Stop; `Triggers::Guard()` after Layout::Tick; T key; Tick + undo
    step + selection after View::Tick; the click branch (a volume before scenery / objects); the overlay in Draw; undo
    kind `kTriggers` (Restore / Current cases); the old HUD's help line; the screenshot line.
  - `level_editor_ui.cpp`: include; `Triggers::InspectorSection` after NodeSection (node selected by itself);
    `Triggers::Window` in Frame; three help rows.
  - `level_editor_layout.{h,cpp}`: File's `triggers` / `triggersOff`; Write / Read of `trigger` / `trigger_off`;
    Capture (owned nodes left out; triggers captured); Apply; ResetToLevel; the saved message.
  - `level_editor_node_create.{h,cpp}`: `Field` moved before `Spec`; `Spec::extra` / `owned` (Fields appends `extra`,
    SameSpec compares them); `SetArrayRemovedListener`.
  - `level_editor_nodes.cpp`: GenericNode type `ed_trigger` -> Family::Trigger.
  - `CMakeLists.txt`: three lines.

## Checks (offline)

- Builds clean (own dir out/build/trig, own PDB endpoint; dependencies from the lead's build), no warnings.
- `thug2_check.exe` on THUG2PM.exe and on C:\Games\THUG2\Game\THUG2.exe (read only): 0 MISMATCHED, all 379 early tests
  pass, 5 new: `TestEditorTriggersKinds`, `TestEditorTriggersScripts` (byte-exact with Boston's compiled gap scripts,
  kill / water / teleport / script bodies, empty bodies for incomplete triggers, the .qb image walked token by token to
  its end), `TestEditorTriggersRecords` (round trips, refusals, defaults, the layout's lines), `TestEditorTriggersPieces`
  (text cleaning, sizes, boxes turned, ray / box, names / checksums, gap partners), `TestEditorTriggersNode` (the
  overlay node's fields, SameSpec, the Triggers family).
- Mutations (`node tools/mutate.ts out/scripts/mut_triggers.ts` - the spec is in the worktree's out/scripts, building its
  own out/build/verify), 20: **all caught** - kind order (kill / teleport, gap end / start), the flags' commas, flags=
  always written, the string's length, water ignored, the gap score unchecked, '#' kept, a repeated pos=, pos= optional,
  the size minimum, the box's turn sign, gap partners' case, NameFor's name check, endscript missing, kind `other`
  accepted, text after the closing quote (caught once a test was added for it), extra fields dropped, SameSpec ignoring
  owned, the Triggers family.
- `node tools/check_code_writes.ts`: No overlaps. No TypeScript changed (npm checks not needed).

## In-game test checklist (for the user) - NOT run in the game yet

Use this branch's DLL. Watch `thug2-decomp.log` (`level editor:` lines; `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT`).
Don't save the career while testing gaps (see the caveat above).

1. **See the level's triggers:** `node tools/game/edit_level.ts BO` (Boston). Log: `level editor: N trigger volumes in
   the level`. Press **T**: the Triggers window (top right) and coloured outlines with labels near the camera. Fly to the
   State House: purple teleport volumes with an arrow to TRG_Restart_StateHouse; gap volumes green / yellow labelled
   with their gap's name and score; kill zones red (out of bounds). T again hides them; restart the game - remembered.
2. **Water:** Barcelona (`edit_level.ts BA`) or any level with water: its water volumes red, labelled "(water)" and
   the restart they send to. A big water plane is drawn as its box.
3. **Inspect / turn off:** click a level volume in the view (or its row in the window): the window shows its kind,
   faces, TriggerScript, box. **Turn off**: it turns grey "(off)". Esc, skate through it: nothing happens. Re-enter,
   Ctrl+Z (or Turn on): it works again.
4. **Kill zone:** in Training (`edit_level.ts TR`), T, look at the floor, click **Kill zone**: a red box on the floor,
   selected; the Inspector's "Trigger volume": Kind Kill zone, Restart (the nearest), "Active". Log `added trigger
   ed_trigger01 (Kill zone)` and `trigger ed_trigger01 (Kill zone) in place`. Esc, skate into it: a bail / death message
   and back at that restart. Choose another restart in the Inspector: the skater goes there next time.
5. **Water:** click **Water**: skate in: a splash, the skater hidden, back at the restart.
6. **Teleport:** click **Teleport**, pick a far restart in "To" (or type a node name in "Or a node"): an arrow points
   there; skate in: "On the Move!" and the skater is there, facing its heading.
7. **Run a script:** click **Script**: "Not active: Choose the script it runs"; pick one of the level's scripts (e.g.
   a sound trigger's): Active; skate in: it runs. Type a name that isn't a script: "There is no script of that name".
8. **Gap:** click **Gap**: a green start box where you look, a yellow end box 600 units further, an arrow between.
   Pause > the gap list (VIEW GAPS / the gaps menu): "Editor Gap 1" (100) is in it; log `gap list built again - 1 of the
   editor's gaps`. Skate: ollie through the start and land after passing through the end without touching the ground
   between (CANCEL_GROUND): "Editor Gap 1" is awarded; the gap list shows it landed.
9. **Gap settings:** select either end: rename it (Name), change the score and flags (e.g. PURE_AIR): both ends follow
   (Select the other end); the gap list shows the new name / score only (the old entry gone).
10. **Move / turn / resize:** drag a volume, use the arrows, W A S D / Q E, Z / X (turns 15 degrees - the box turns), G
    (drops it); drag Size in the Inspector (one Ctrl+Z undoes the whole drag). Skate: it fires where it is now.
11. **Delete / undo:** Delete an added volume: gone (and its gap from the gap list); Ctrl+Z: back, working, its gap
    listed again; Ctrl+Y deletes again.
12. **Editable copy:** select a level volume (e.g. a Boston gap or kill zone), **Make an editable copy**: a cyan box
    over its faces, selected; the original turns grey "(off)". Move the copy away and skate it: the original's script
    runs at the new place, not the old one. Delete the copy: the original is on again.
13. **Save / reload:** Ctrl+S: "Saved: ..., N triggers"; `thug2-decomp-layouts\<scene>.txt` has `trigger ...` and
    `trigger_off ...` lines. Quit to the menu, load the level's (EDITED) entry: after a second `Layout loaded: ...,
    N triggers`; the volumes are back and work; the editor's gaps are in the gap list. Load the level as shipped: none of
    them, its gap list without the editor's gaps.
14. **Undo chain:** add a kill zone, change its kind to Teleport, drag its size, turn off a level volume, make a copy -
    then Ctrl+Z through all of them and Ctrl+Y forward again: everything comes back as it was at each step.
15. **Reset level** (Ctrl+Shift+Delete): every added volume gone, the level's turned on, the editor's gaps out of the
    gap list; Ctrl+Z brings them back.
16. **Leaving the level:** with added volumes, load another level: log `the level's node array left - its N triggers go
    with it`; nothing fires at those places in the new level; the old level's gap list (load it as shipped) has no
    editor gaps.
17. **Old HUD** (`editor_ui=0`): T says the trigger volumes need the UI; a layout's volumes still work in play.
18. **F1:** the Triggers rows. **Screenshot** in the editor: the sidecar's `triggers` line.
19. **Online (only if you test online at all):** in a network game the edited copy never loads; if a volume was added
    and a network session starts in that level, the log says `a network session - the editor's N trigger volumes taken
    out`.

## Left / limits

- Nothing run in the game. The parts most worth watching: the first trigger firing (a movable box on the movable list
  with no owner), the LoadQBData redefinition while scripts run, the gap list rebuild.
- Volumes fire on the way **in** only (a box is hit where a line enters it); a skater starting inside one won't fire
  it until it leaves and comes back. The level's trigger planes fire whichever way they're crossed.
- A level's own volume moves / resizes only through its editable copy, which is its faces' axis-aligned box (a
  diagonal plane becomes a box around it; a copy's script that checks its node's own name would see the copy's).
- Gaps on rails / lips (a rail node's TriggerScript with StartGap / EndGap, TriggerType checks) aren't made: gaps are
  two volumes (the REQUIRE_RAIL / REQUIRE_LIP flags are offered); rails are phase 4's (chains).
- Sizes are set in the Inspector (no drag handles on the box faces yet); boxes turn about the up axis only.
- Volumes fire for any object with a trigger component, as the level's do.
- The career-save caveat above (editor gaps saved into a level's gap list if the career is saved meanwhile).
- T is a new key: if the chains / scaling work takes T too, one of them must move.
