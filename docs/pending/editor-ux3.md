# Level editor UX3: history, changes, command search / keymap, box select, organising, clipboards, snapshots (NOT ORIGINAL)

Branch `worktree-agent-a539bcfcc17eed16b` (main merged with UX1 and UX2 in). **NOT run in the game yet** (no game
runs for editor work): built and checked offline only - thug2_check on THUG2PM.exe and the stock THUG2.exe (0
MISMATCHED, all early self-tests pass), check_code_writes (no overlaps), mutations below.

## What was built

1. **Changes panel** (Ctrl+Shift+H, toolbar "Changes N" - the count is the badge): every record of the edited copy's
   layout as it is now (`Layout::CaptureText`), grouped (Level properties, Scenery, Scenery copies, Rail points,
   Objects, Object copies, Nodes moved / fields, Nodes added, Nodes hidden, Triggers, Chains, Peds / cars / sounds,
   Collision and walls, Trick objects, Cameras, Locks), a one-line summary each (names from the name table or the
   record's comment), a filter, **Go to** (selects it - node / object / scenery - and takes the camera there) and
   **Revert** = the layout without that record put in place (`ResetToLevel` + `Apply`, as Reset level's undo does) as
   ONE undo step (a `kLevel` step: Ctrl+Z puts it back). Refreshed when the undo history moves, when opened, and once
   a second. `src/game/front/level_editor_changes.*` (pure list / take-out), `level_editor_ux3.cpp` (refresh, revert).
2. **Undo history** (Ctrl+H, toolbar "History"): every undo step has a short label where it is made (`PushUndo(step,
   "Turn")` at each of level_editor.cpp's ~30 sites; modules' steps "Faces / clusters / cameras" unless
   `PushModuleUndoLabelled`; the selection then as a tooltip). Click a step: undo / redo to it (`JumpHistory`).
3. **Command search** (Ctrl+Space or F3 - no clash: Space only flies with no Ctrl; F3 unused): fuzzy search over every
   action and setting of the **action registry** (`level_editor_actions.*`), its keys at the right, toggles' state
   ([x]), disabled ones greyed; Up / Down / Enter / Esc. The registry IS the editor's keymap: level_editor.cpp and
   level_editor_camera.cpp read `Actions::Pressed(id)` / `Actions::Held(keys, id)` instead of scan codes; rebindable in
   the **Keys** section (its own window - the search's "Keys (rebind)", F1's "Keys..." - and hosted by UX1's
   Preferences window, "Keys" header), stored in `thug2-decomp.ini [editor_keys]` as `<id>=Ctrl+Y, Ctrl+Shift+Z` (only
   keys that differ from the defaults; `none` unbinds). F1's help lists from the registry first (keys as bound now),
   the old prose table under "Details".
4. **Box select**: a drag from empty space in the view (nothing under the cursor), or Ctrl+B then any drag. Dragged to
   the right: what's wholly inside; to the left: what the box crosses (dashed outline). Shift adds, Ctrl removes.
   Objects with a model (and copies) first, else movable scenery pieces (and clones); point nodes select one at a time
   (the nearest the box's middle). Respects the toolbar's Pick filter, picking locks, hidden pieces.
   **Select similar** (Shift+G menu): same mesh (objects: model file, any case; scenery: a copy of the same piece or
   the same box size within 0.5), same class, same node family, same type; All of this family; Invert (Ctrl+I);
   Select all (Ctrl+A). With a point node selected: the Outliner's search is set to its family (nodes select one at a
   time).
5. **Outliner**: per-row toggles at the row's right - `•` eye (hidden in the editor only: objects / scenery through
   level_editor_view.h's Hide + new `View::Show`, gizmos through a set; never saved), `L` lock (locked against changes:
   nudge, drag, turn, scale, G, Delete, Home and Paste transform refused with a message - the Inspector's fields still
   edit; saved), `S` selectable (objects / scenery: the existing L pick lock; nodes: their gizmo isn't picked; saved).
   **Groups** (Ctrl+G or a row's right-click "Make a group of the selection..."): a "Groups" tree at the Outliner's top,
   click selects (objects, else scenery, else the node), right-click add the selection / rename / delete.
   **Labels**: right-click "Label..." - shown after the name ("Sk_01 — big ramp"), searched too. **Type filters** in the
   search: any family word singular / plural (`rail:`, `spawn:`, `object:`, `scenery:`... plus restart / ped / car /
   proxim / piece), `group:<name>`, `label:`, `locked:`, `hidden:`, then words.
6. **Inspector**: a "Copy / paste" section (Copy transform + fields, Paste transform, Paste fields, what's copied) and,
   with 2+ selected objects / scenery pieces, "Shared fields (N nodes)": every editable field all have, the value or
   *mixed*; Enter sets it on all of them as one undo step (`SetFieldsOnNodes`). Ctrl+Shift+C copies the transform
   (position, heading, scale) and the inspected node's own editable fields; Ctrl+Shift+V pastes the transform (one
   undo step); Paste fields has no default key (rebindable, in the search). **Section folding remembered** in
   `[editor_sections]` (fields, levelprops, camera, cameras, clusters, faceflags, locks, ux3clip, ux3multi) through the
   Inspector window's ImGui state storage (imgui_internal.h) - nothing changed in the other modules' headers.
7. **Copy / paste between levels** (Ctrl+C / Ctrl+V; C and V alone stay collision / camera node): the clipboard's text
   is a layout fragment (`# thug2-decomp clipboard ...` then a layout): scenery as `clone` records (a copy's own source
   and its placement relative to it), objects as `copy` records, added nodes as `node` records, added trigger volumes
   as `trigger` records, a level's spawn / crown / light / sound / proximity node as a new node of its kind. Paste merges
   it into this level's layout (`Clip::Merge`: sources that don't exist here reported and left out - message + log;
   fresh names for copies / nodes / triggers whose names are taken; CTF / lobby flags whose fixed name is taken left
   out) and puts it in place as one undo step; pasted object copies are selected. In the same level: in place.
8. **Layout snapshots** (toolbar "Snapshots"): `thug2-decomp-layouts\<scene>.<name>.txt` (names: letters, digits, _,
   -, 1-32, not "autosave"; `SavedLayoutScene` doesn't take them, so the level select's EDITED list ignores them). Save
   as snapshot (it becomes the active one: a `snapshot <name>` record in the layout); Switch to: the layout now is kept
   in the active snapshot (else "previous"), the chosen one put in place (one undo step) and saved as the edited copy's
   layout (Ctrl+S triggered) - the one the (EDITED) entry loads; Shift+click Delete.

## The registry's shape (for the lead and the other agents)

`Front::LevelEditor::Actions` (level_editor_actions.h): `struct Action { id (ini key), name, category, tip, Kind
{Press, Hold, Toggle, Info}, defaults, keys (vector<Chord {scancode, mods Ctrl|Shift|Alt}>), keysText (Info), rebindable,
run, enabled, checked }`. Built-ins are a table in Id order (`enum Id`: kHelp ... kChainFinish, then UX1's kPlay,
kPreferences, kFrameSelection, kBookmark1..9, kBookmarkStore1..9, kViewTop..kViewOrtho, kBuiltinCount), then the Info
hints, then `Register()`ed ones (the search's settings: snap25, carry_objects, pick_nodes / objects / scenery). **To add
a key** (UX2 / UX4 at merge): append a `Make(id, name, category, kind, {chords}, tip)` row before the hints and its Id
before kBuiltinCount (same order), then read it with `Actions::Pressed(Actions::kX)` / `Held`. A key press resolves
(`Resolve`): bound to the key, Ctrl exactly as bound, Shift / Alt held when bound; the most specific wins (so Shift / Alt
not bound are qualifiers: Shift+[ fine scale, Alt+[ stretch). `Trigger(id)` = as if pressed next frame (the search's
default run). `SeedKeys` at editor start (replaces UX1's g_playKey latches). The registry self-test fails if two
default Press keys clash.

## Keys bound (all rebindable; defaults)

Panels: F1 help, **Ctrl+Space / F3 command search**, B browser, N place, **Ctrl+H undo history**, **Ctrl+Shift+H
changes**, Keys (none), Snapshots (none), Ctrl+, preferences (UX1). Edit: Ctrl+Z undo, Ctrl+Y / **Ctrl+Shift+Z** redo
(new: was undo before), Ctrl+S, Ctrl+D, Ctrl+R / Ctrl+Shift+R autosave, Delete, Home, Ctrl+Shift+Delete reset level,
**Ctrl+C / Ctrl+V copy / paste between levels**, **Ctrl+Shift+C copy transform + fields**, **Ctrl+Shift+V paste
transform**, paste fields (none). Select: Backspace deselect, L lock, **Ctrl+B box select**, **Shift+G select
similar**, select family (none), **Ctrl+I invert**, **Ctrl+A select all**, **Ctrl+G group**. Transform: Z / X, [ / ], G
(Shift+G is now select similar), UX2's tools W move / E rotate / R scale (no right button held), Space next tool, `
world / local, Alt+G modal transform, Ctrl+Shift+D repeat duplicate, nudge with the arrows and PageUp / PageDown
(wasd_flies=1: the fly keys nudge too, Space aside). Camera: fly W A S D / E Space / Q (right button held; wasd_flies=1
always), - / =, F frame, 1..9 / Ctrl+1..9 bookmarks, numpad 7 / 1 / 3 (Ctrl: opposite) / 5, F5 / Ctrl+P play.
Gestures listed (fixed): UX2's snap modifiers while dragging (Ctrl, Ctrl+Shift, Alt+Shift, Ctrl+Alt), typed X / Y / Z
and digits during a drag (read from SDL events, not the keymap).
View: C, K, T, O, H, I and /, Alt+H, V, P. Chains: Insert, End, J, U, Enter. Gestures (listed, fixed): drag from empty
space = box, right mouse, wheel, Shift, Alt+drag orbit, RMB+wheel speed, Esc, browser keys.

Behaviour changes from the strict Ctrl matching: Ctrl+B no longer opens the browser, Ctrl+Delete no longer deletes,
Ctrl+Shift+Z redoes, Shift+G selects similar (G alone drops), Ctrl+click on empty space no longer deselects (it starts
a removing box).

## Shared files touched (small, additive)

- level_editor.cpp: includes; `UndoStep::label / detail`, `PushUndo(step, label)` + labels at its sites, `KindLabel`;
  the keys read from the registry (`Actions::BeginFrame` before UX1's play block, Pressed / Held); `Ux3::Update()`;
  the Outliner lock gating (nudge, turn, scale, drop, drag start, Home, Delete); box-select hooks in the click code;
  gizmo hide / pickable filter and the box outline in AddNodeGizmos; Escape ignored while a key is captured; the
  services at the end (UndoHistory, JumpHistory, ApplyLayoutStep, CurrentSelection / SetSelection, GoToThing,
  ProjectToBack, SetFieldsOnNodes, SelectionTransform / SetSelectionTransform, ShowMessage...).
- level_editor_modules.h: those services' declarations. level_editor_layout.h/.cpp: `File::extra` (unknown records
  kept and written back) and `AddExtension` (capture / apply / reset hooks; reset also on a new level).
- level_editor_ui.h/.cpp: `PushRequest`; toolbar / Outliner / Inspector / help / frame hooks (one line each mostly; the
  Outliner's Selectable gets AllowOverlap for the row toggles). level_editor_view.h/.cpp: `View::Show`.
- level_editor_camera.cpp (UX1): F / 1..9 / Ctrl+1..9 / numpad from the registry; level_editor.cpp's TransformKeys (UX2) and its nudge / RMB-fly block likewise. My `TransformValues` (modules.h) is the Inspector clipboard's pose (UX2 owns the `Transform` namespace). level_editor_prefs.cpp (UX1): the
  Keys header. CMakeLists.txt: 6 sources, 1 self-test file.
- New: level_editor_actions.*, level_editor_changes.*, level_editor_organize.*, level_editor_clip.*,
  level_editor_ux3.h/.cpp, level_editor_ux3_ui.cpp, src/hook/selftests_editor_ux3.cpp (10 tests).

## Layout records (editor-only, an older reader skips them; the Changes panel doesn't list them)

`group <name> o:<id>,s:<checksum>,n:<node>` · `label <checksum> <text>` · `edlock <checksum>` · `nopick <node>` ·
`snapshot <name>` (names / text %-escaped as Fields::Escape).

## Tests

`src/hook/selftests_editor_ux3.cpp` (Early): chords / key lists (incl. "Ctrl+," and "Keypad+"), resolution (Z vs Ctrl+Z vs
Ctrl+Shift+Z, H / Alt+H, G / Shift+G, Delete / Ctrl+Shift+Delete), held keys, fuzzy ranking, the Changes list and
Without, Outliner records round trip, a layout keeping unknown records, type filters and row matching, box rectangles /
inside / crossing / combine, similar criteria, snapshot names / files (not a saved layout), the paste merge (missing
sources, fresh names, fixed names refused), the registry's order / unique ids / no clashing defaults.
Mutation-proved with `out/scripts/mut_ux3.ts` (`node tools/mutate.ts out/scripts/mut_ux3.ts`): see the result below.

16 of 16 mutations caught (Ctrl matched exactly, the most specific chord, the fuzzy order, the comma key, the registry order, Without, the editor records left out of Changes, inside vs crossing, Ctrl removing, singular family words, escaped labels, the autosave name, model case, missing paste sources, taken names, the layout writing records back).

## Not done / limits

- Nodes still select one at a time (a box or Select similar over point nodes takes one / filters the Outliner).
- The lock doesn't stop the Inspector's typed fields (position / scale / fields) - only the view's moves and keys.
- Revert re-applies the whole layout (as Reset level's undo): a moment's pause in big layouts; reverting one point of an
  added rail whose others link to it may leave a broken link (Ctrl+Z puts it back).
- Copy of a level's own node copies its kind, place and radius only (not its other fields); walls, faces, cameras and
  chains aren't copied between levels.
- level_editor_play.cpp's `KeyDown` (F5 / Ctrl+P while playing, outside the editor) still reads the keys directly.

## In-game checklist (for the user - NOT run)

1. Enter the editor: the toolbar shows Search, History, Changes N, Snapshots after the view options; F1 lists every
   action by category with keys, "Details" folds the old table.
2. Ctrl+Space (and F3): the search opens focused; type "undo", "box", "snap"; Up / Down / Enter runs; Esc closes;
   settings show [x] / [ ] and switch the toolbar's checkboxes.
3. Every key works as on main: fly W A S D Q E Space with the right button held (and, wasd_flies=1, always - then
   they nudge a selection too); without it W / E / R pick the tool, Space cycles, ` world / local, Alt+G modal,
   Ctrl+Shift+D repeats; arrows / PageUp / PageDown nudge; Z / X, [ / ] (Shift fine, Alt stretch), G, Delete, Home,
   Ctrl+Z / Y / S / D, L, C, H, Alt+H, I, /, T, K, O, V, P, Insert / End / J / U / Enter on chains, F1, B, N; UX1's F,
   1..9 / Ctrl+1..9, numpad, F5 / Ctrl+P, Ctrl+,.
4. Keys window (search "Keys", or F1 > Keys..., or Preferences > Keys): click a key, press another - it changes, the
   ini gets `[editor_keys]`; right-click removes; + adds; Reset; a clash shows "also: ..."; Esc while capturing cancels
   and does NOT leave the editor or deselect.
5. Move / turn / scale / copy a few things: Ctrl+H lists steps by name ("Move", "Turn", "Scale", "Duplicate"...);
   click an older step - the level goes back to it; click a greyed one - forward again.
6. Ctrl+Shift+H: changes grouped with summaries; Go to selects and frames; Revert puts one back (the count drops),
   Ctrl+Z restores it; the count badge follows edits.
7. Drag from empty sky / ground with nothing under the cursor: a box; left-to-right takes what's inside, right-to-left
   (dashed) what it crosses; Shift adds, Ctrl removes; Ctrl+B then a drag starting on an object also boxes.
8. Select an object, Shift+G: Same mesh / class / family / type select the matching ones; Ctrl+I inverts; Ctrl+A all.
   With a spawn selected Shift+G > Same family puts "spawns:" in the Outliner search.
9. Outliner: the `• L S` toggles at a row's right - eye hides it (object / scenery / gizmo), L refuses moves (message),
   S makes clicks pass it. Right-click a row: Label... (shown after the name, searchable), Make a group... (Groups tree
   at the top; click selects). Save, reload the EDITED level: groups, labels, locks come back.
10. Search filters: `rail:`, `rail: ware`, `object: car`, `group:Mine`, `label:`, `locked:`, `hidden:`.
11. Select 2+ objects: Inspector "Shared fields": equal values shown, differing ones "mixed"; type + Enter sets all
    (one Ctrl+Z undoes all). Ctrl+Shift+C on one, select another, Ctrl+Shift+V: same place / heading / scale.
    Fold Inspector sections, leave and re-enter: they stay folded.
12. Ctrl+C a few pieces and objects in one level, open another edited level, Ctrl+V: what exists there appears, the
    message names what didn't (log lists all); Ctrl+Z removes the paste.
13. Snapshots: save "a", change things, save "b"; Switch to "a": the level goes back, the (EDITED) entry loads "a";
    "b" still there; Ctrl+Z undoes the switch.
