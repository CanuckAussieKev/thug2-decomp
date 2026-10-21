# Level editor UX5: problem checker, budget meter, note pins

Branch `worktree-agent-a7a486affbe014d72`. **NOT run in the game yet** (the user tests interactive editor behaviour;
checked offline only: build, thug2_check on THUG2PM.exe and the stock THUG2.exe - 446 early self-tests pass -, 15 of 15
mutations caught, check_code_writes (no overlaps), npm typecheck / lint / test, apply_layout.test.ts).

The research's (docs/editor-ux-research.md) #1 problem checker (with its multiplayer-points check and the
layout-resilience "unresolved record" check), #14 budget meter and #17 note pins.

## New files (and the small hooks into shared ones)

- `src/game/front/level_editor_checks.{h,cpp}` - the problem checker and the Problems window (two tabs: Problems,
  Budget).
- `src/game/front/level_editor_budget.{h,cpp}` - the budget meter (toolbar bar + the Budget tab's table).
- `src/game/front/level_editor_notes.{h,cpp}` - note pins.
- `src/hook/selftests_editor_ux5.cpp` - 5 Early self-tests of the pure parts.
- Hooks (additive): `level_editor.cpp` (Budget::Observe before Layout::Tick; Checks / Notes Update; Alt+P; the notes'
  overlay and Checks::NoteModel in Draw; the screenshot sidecar's `problems` line; GoToNodeNamed / GoToScenery /
  GoToPoint for modules), `level_editor_modules.h` (those three declared), `level_editor_ui.cpp` (the toolbar's Budget bar
  and Problems button after the view buttons; the Outliner's Notes section after the families; the Problems and note
  windows; three help rows), `level_editor_layout.{h,cpp}` (File::notes and File::carried; the `note` record read and
  written; carried lines written at the end; Apply notes the layout to Checks; reset / new level forget), `level_editor.h`
  (the module list), CMakeLists.txt (4 sources), `tools/game/layout_file.ts` (`note` records: `readNoteRecord`,
  `decodeNoteText`, `layout.notes`), `tools/game/apply_layout.ts` (a comment: notes are passed over), its test.

## Keys and buttons

- **Alt+P** or the toolbar's **Problems** button (red with errors, amber with warnings, the count): the Problems window.
  Plain P still logs the camera pose (Alt+P no longer does).
- The toolbar's **Budget** bar (the worst meter's fill, green / amber / red): hover lists every meter; a click opens the
  Problems window's Budget tab.
- The Outliner's **Notes** section: **+ Note here** (a pin where the view looks); a click on a note opens its window,
  a double-click also takes the camera there.
- No command-search registry on main yet (UX3's) - the three actions are toolbar / Outliner buttons and Alt+P.

## The checks and their rules (level_editor_checks.h has them in full)

Re-run a moment (0.4 s) after the editor's change count, the node arrays or the level change; at once on opening the
window and on **Check now**. Each row: severity, rule, text; **Go to** (selects the node / scenery, or looks at the
place), **Fix** where safe, **Ignore** (this session - keyed by rule, subject and detail). Problems the level has as
shipped are tagged "(as shipped)" and hidden unless "The level's own" is ticked.

| Rule | What | Fix |
|---|---|---|
| Inside solid | a spawn / crown / flag (or the middle of an editor trigger volume, as an info) closed in by collision along one axis | - |
| Below the ground | the same under a surface (behind a face upwards) | onto the surface above (undoable) |
| Nothing below | no face within 5000 units below (falls out of the level) | - |
| Floating rail point | a rail point the editor moved / added with no face within 30 units (both ways, six directions) | - |
| Rail of one point | a rail node linked to nothing and by nothing | - |
| Collision left behind | (info) a moved piece with no collision of its own and none taken along | - |
| Hidden, collision on | a hidden piece whose taken-along collision (companion) is still on | switches it off |
| Limit | a count meter over its engine limit (error) or at 90 % (warning) | - |
| Trigger never fires | an editor trigger that does nothing yet (Triggers::Problem); a level's trigger faces with no node | - |
| Trigger script | a TriggerScript / enter / exit script that isn't a script (ScriptExists 0x00472DE0); a proximity node with none | - |
| Gap unpaired | an editor gap start with no end (the editor's or a level trigger's with its GapID) / end with no start | - |
| Online parity | (info) a spawn / crown / flag moved / added / hidden in a level shared with THUGPRO | - |
| Multiplayer | Player1 restart; Multiplayer restarts (one: info); a Horse restart; crowns; CTF Red and Blue each flag + base + team restart (TRG_CTF_<C>, _Base, TRG_CTF_Restart_<C>), Green / Yellow complete or absent | - |
| Layout record | a loaded record whose thing isn't in the level | Drop record (undoable) |

**Placement probes**: the game's collision ray meets only a face's front (RayHitsTriangle 0x0040B420 refuses a
determinant below 0.03). From the point (lifted 12 units) each of six directions is cast out (the nearest face facing the
point) and back in from its reach (the nearest face facing away - the cast repeated past each hit); a direction is
"behind a surface" when the face facing away is nearer. Inside = behind both ways along an axis; below = behind upwards.
Sides reach 150 units, up / down 2000. Results are cached per node while its place and a signature of the static
collision (changed sectors / walls / copies / shared collision generation / face edits) stay the same.

**Unresolved layout records (never silently dropped)**: after a layout goes in (Load, the edited copy's pick, autosave
restore, Reset level's undo), every record naming something - sector, rail, clone source, object, copy source, set,
hide, lock, proxim, wall, faces, cluster, chain links / drop / terrain / facing / flat grind / speed-up, path speed,
emitter, the level's start - is checked against the level (nodes by name, sectors by checksum, objects alive or by node).
Each that doesn't resolve is logged (`level editor: layout record not found in the level - kept ...`), listed as an
error, and its line kept: Capture writes it again at the file's end under a comment (`Layout::File::carried`) unless a
record of the same kind and key was captured anyway - so an update that brings the thing back finds its edit. **Fix**
(Drop record) drops it from the next save (undoable).

## The budget's limits (found in the engine)

| Meter | Limit | Where |
|---|---|---|
| Crowns | 10 | CFunc_SpawnCrown 0x00538170: two 10-entry stack arrays (unchecked) |
| Restarts | 256 | GetRestartNode 0x0046B790: a 256-name stack array for GetRestartNodes 0x0046B6C0 (unchecked) |
| Level lights | 150 | the light manager's level list (count 0x006FAEF4; AddLight 0x00490570) |
| Dynamic lights | 256 | its dynamic list (count 0x006FAEF0) |
| Nodes | 16384 | CNodeManager (0x006F0D84) name hash: 0x4000 slots (HashNode 0x0045CE60) |
| Node arrays | 20 | CNodeManager arrays (AddNodeArray 0x0045D520) |
| Script structures / components / vectors / arrays / symbols / running scripts | 19800 / 86000 / 9500 / 7000 / 12500 / 300 | Script::CreateScriptPools: pool pairs 0x006F3614, 0x006F2338, 0x006F3654, 0x006F22B4, 0x006F362C, 0x006F3548 (items in use: the current pool's m_used) |
| Script heap | 0x434770 bytes | Mem::Manager::InitStandardHeaps 0x005BBE80 (the script region: free = between the tops + the heap's free blocks) |
| Main memory | 64 MB | Mem::Manager::Construct 0x005BBC00 (the main region: top-down + bottom-up heaps) |
| Counts only | - | editor trigger volumes (movable collision list), collision faces taken along (2048 a piece: SharedCollision::kMostFaces), scenery copies, object copies |

"Edits" column: added crowns / restarts / lights / nodes counted against the level as shipped (hidden ones still
counted as the level's); pools and heaps: the change since the level came up (the baseline taken in the first frame the
level is up, before its edited copy's layout goes in - approximate, "about": the game allocates too). Memory meters
never raise Limit problems (only the counts and pools do).

## Note pins

- `note <name> pos=x,y,z color=RRGGBB text="..."` (version 2 still; older readers skip the kind); the text
  percent-encoded for % " # and control characters, at most 500 bytes, text= last. Names `ed_noteNN`.
- Drawn only while the editor runs (a stem, a head, the first line of text; the selected one white-ringed with its full
  first line). Not a node or object: nothing exists in play. Listed in the Outliner's Notes section (the search filters
  them); the note's window edits its text (one undo step a field, pushed when let go), colour (6), place (drag, or Move
  here), Go to, Delete. Undo / redo, Ctrl+S, the autosave and Reset level include them.
- The bake: layout_file.ts reads them into `layout.notes`; apply_layout.ts bakes nothing of them and reports nothing.

## In-game checklist (for the user)

1. Open the editor in TR (`-edit TR`). The toolbar shows **Budget** (a small green bar) and **Problems** after the view
   buttons; at 1280 x 720 check the right-hand buttons (Undo ... Place) still fit.
2. Hover the Budget bar: crowns, restarts, lights, nodes, node arrays, script pools, script heap, main memory listed with
   used / limit. Click it: the Problems window opens on Budget; the table's fills are coloured.
3. Press **Alt+P**: the Problems window opens on Problems (Alt+P again closes it; plain P still logs the camera pose).
   With an unedited TR, expect few or no rows; tick **The level's own** and look at what TR has as shipped - report
   anything that looks wrong (false "inside solid" / "below the ground").
4. Move a multiplayer spawn under the floor (drag it, Shift: down) - within about half a second a **Below the ground**
   error appears; **Go to** takes the camera to it; **Fix** puts it on the floor above; Ctrl+Z puts it back under.
5. Push a spawn into a wall or a crate: **Inside solid** (no Fix). Move it out: the row goes.
6. **Ignore** a row: it disappears and the toolbar count drops; tick **Ignored** to see it greyed (Unignore).
7. Hide (Delete) a CTF base `TRG_CTF_Red_Base` in a level that has CTF: a **Multiplayer** warning "team Red: missing its base";
   Home brings it back and the row goes.
8. Add crowns with N until there are 10 - the Budget bar turns amber near 8, the Crowns meter shows 10 / 10.
9. Move a rail point (L) off its ledge into the air: **Floating rail point** warning; put it back: gone.
10. Add a gap with the Triggers window (T) and delete its end: **Gap unpaired** ("no end anywhere").
11. Add a trigger "Run a script" without choosing a script: **Trigger never fires** ("does nothing yet").
12. In the Outliner, **+ Note here**: a yellow pin where the view looks, its window opens; type two lines, pick a colour,
    drag its place, **Move here**, **Go to**. The pin's label is the first line. Ctrl+Z undoes the text edit as one step.
13. Ctrl+S, leave the editor (play): no pin is drawn in play. Re-enter: the note is back. Load the (EDITED) entry from
    the level select: the note is there in the editor.
14. Unresolved records: with the game closed, add a line `hide 0BADF00D` and `set 0BADF00D pos=1,2,3` to the level's
    `thug2-decomp-layouts\<scene>.txt`, load the edited copy: the log has two `layout record not found in the level`
    lines; the Problems window lists them (Layout record, error). Ctrl+S: the file still has them at its end under
    `# not found in the level when loaded ...`. **Fix** (Drop record) one, Ctrl+S: it's gone from the file; Ctrl+Z before
    saving keeps it.
15. Reset level (Ctrl+Shift+Delete): the notes and the unresolved records go with the layout; Ctrl+Z brings both back.
16. `node tools/game/apply_layout.ts` on a layout with notes: no warning or report line about them.
17. Screenshot (Print Screen) in the editor: the sidecar's `problems` line has the counts.

## What's left

- Clicking a note pin in the view doesn't select it (the Outliner does); pins aren't drag-moved in the view (the
  window's fields / Move here do).
- Ignore lasts the session (until the level loads again); not saved.
- Gap pairing is checked for the editor's gaps only (a level's gaps may be started from rail / other scripts).
- "Collision left behind" can't tell a piece that never had collision from one whose collision stayed in a shared
  object the shared-collision pass didn't match - an info only.
- Placement probes trust collision orientation: one-sided collision facing up above a spawn (no underside) reads as
  "below the ground" - such rows on unedited nodes are "(as shipped)" and hidden by default.
- No command-search entries (UX3's registry wasn't on main): when it lands, register "Check for problems" (Alt+P),
  "Budget" and "Note here".
