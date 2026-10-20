# Level editor: picking that doesn't grab the floor (structure, decals, cycle, filter, locks)

Branch `worktree-agent-ac9d09b082263491b`. **NOT run in the game yet** (the user tests interactive editor behaviour
himself; checked offline only: build, thug2_check on both exes, mutations, check_code_writes).

## The complaint

"No matter where I press, I always select something. Usually it's the ceiling or the floor." Two causes:
1. Every click took the first scenery the ray met - TR's floor (2087 x 0 x 3105), walls, roof - and SelectGroup took it.
2. Sectors without collision were picked by their **boxes**: TR's `g_wareNew_spiderweb_01` (a see-through ceiling decal,
   box 3412 x 160 x 2601) swallowed every click below it (the user's screenshot thug2_2026-10-20_23-34-35).

## What changed

- **New `src/game/front/level_editor_picking.{h,cpp}`** (pure, self-tested): the structure rule, the click choice under
  the filter / Alt, click-to-cycle's order, ray / triangle and ray / strip tests, the lock list.
- **Structure** (`IsStructure(min, max, camera)` on the piece's world bounds) - passed by a plain click and the hover:
  - the camera is inside the box (the room you're in), or
  - the second-largest extent >= **1500** (wide in two dimensions: floors, roofs, streets, the ground), or
  - a slab: second-largest >= **400** and the thinnest <= **0.1** x that (wall panels, floor tiles).
  - Measured offline from the .scn.xbx sector boxes (tools/level_port/thug2_scene.ts; in-game census for TR agreed):
    TR 82 / 790 pieces structure, BO 114 / 1481, THPS3 Foundry 48 / 488, THAW z_ma 255 / 1397. Kept pickable on
    purpose: TR's bowl 720 x 147 x 807, BO buildings ~1477 x 1080 x 1036, ramps, rails, funboxes. Caught: TR's floor
    / walls / roof, floor tiles 800 x 51 x 699, BO's 2655 x 91 x 2368 and Foundry's 1536 x 0 x 856 slabs.
- **See-through pieces** (every mesh's material alpha-blended: +0x20 flag 0x40, the scene's own sort test) are passed
  too: webs, shadows, grime, glows. (Alpha-tested fences that the game sorts as transparent are passed as well - Alt
  takes them.)
- **No-collision sectors are hit by their triangles** (each mesh's index buffer 0 as a triangle strip over its
  vertex copy - where the sector is now; billboards by their sphere), not their boxes; the box only skips the rest.
- `Scenery::Pick` goes on through passed and locked pieces (up to 32 collision hits) and reports the nearest passed
  one (`Hit::passed`) for Alt; with `layers` it returns everything along the ray (the cycle). A click that meets only
  structure / decals deselects, as a click on nothing.
- **Alt+click / middle click** (already "one piece") takes the piece under the cursor whatever it is, structure and
  decals included; holding Alt shows it in the hover. Chosen over double-click: Alt already meant "this exact piece",
  and a double-click's first click would deselect / start a drag. The Outliner and the browser still select anything.
- **Click-to-cycle:** a plain click within 4 px (at 720 lines) of the last one, the camera not moved (the click's turn
  towards the selection doesn't count), nothing changed / dragged and the filter the same, takes the next entry after
  the last click's: the gizmo, the objects (nearest first), the scenery, then the passed pieces; wraps. The status bar
  says "n of m under the cursor - click again for the next"; the hover line shows "(n of m under the cursor: click)".
  A first click on only structure takes nothing (and starts no cycle).
- **Hover outline:** with the UI, what a click would take also gets its box edges drawn (yellow, the ImGui draw list):
  the object, or the boxes of the scenery group (one piece with Alt) - besides the tint. Alt tints one piece too.
  "Under the cursor" says "- (passing scenery X ... - Alt+click takes it)" when only a passed piece is there.
- **Pick filter:** toolbar `Pick: Nodes Objects Scenery` toggles (after Snap 25); `[editor] pick=<bits>` (1 nodes,
  2 objects, 4 scenery; default 7) in thug2-decomp.ini, like families=.
- **Locks:** L (or the Inspector's Lock / Unlock) locks the selected objects / scenery pieces and deselects; locked
  ones are passed by every click (Alt too) and by `Objects::Pick` / `Scenery::Pick`. The Inspector lists them
  ("Locked (n)", Unlock each); selecting a locked one from the Outliner and pressing L unlocks it; L with nothing
  selected unlocks all (the old HUD's way). Not undoable (marks unsaved). Layout record `lock sector <checksum>` /
  `lock object <id>` (version 2 kept; older readers skip unknown records); cleared when a level loads, put back with
  its layout.
- F1 help (UI table and old HUD text) lists the new rules; the old HUD (`editor_ui=0`) follows the same rules (no
  toolbar: the filter from the ini; no outline - it has the tint / box as before).
- `debug_editor=1`: the scene census line now counts see-through pieces; each click logs "click - selected: ...".

Shared files touched (small, additive): CMakeLists.txt (2 lines), level_editor.h (controls comment),
level_editor_layout.{h,cpp} (lock records), level_editor_objects.{h,cpp} (lock skip, PickAll), level_editor_ui.{h,cpp}
(toolbar toggles, Inspector lock button + list, help rows, 3 request kinds, 4 model fields). level_editor.cpp: the
hover block and the click branch (2 inserted calls), the release, new helpers (CycleHolds, NoteClickForCycle,
SelectionLocks, ToggleLock, HoverText), the L key, 3 request cases.

## Checks (offline)

- Build (own dir out/build/agent3): clean.
- thug2_check.exe on THUG2PM.exe and on C:\Games\THUG2-3\THUG2.exe (stock): all 365 early tests pass, five new:
  `TestEditorPickStructure` (real piece sizes, thresholds' edges, inside), `TestEditorPickChoose` (filter, Alt, ties),
  `TestEditorPickCycle` (order, IndexOf, same spot), `TestEditorPickTriangles` (triangle both sides / miss / parallel,
  a strip with a degenerate join, limits, vertex count), `TestEditorPickLocks` (locks, layout round-trip, unknown kind).
- Mutations (out/agent3/mutate.ps1): 6 of 7 caught (slab `<`, inside off, tie to object, Alt ignored, passed first in
  the cycle, triangle v bound); not caught: dropping the strip's explicit degenerate skip - equivalent (a degenerate
  triangle has zero area and never hits), it's only a shortcut.
- `node tools/check_code_writes.ts`: no overlaps.

## In-game test checklist (for the user) - NOT run in the game yet

Use this branch's DLL; `debug_editor=1` helps (log lines "click - selected" and the census).
1. `-edit TR`. Fly above the warehouse looking down (the user's view: camera (29.6, 1258.2, -1825.5), yaw -145.8, pitch
   -43.8). Hover the floor and the ramps: the web decal must no longer be "under the cursor"; the floor isn't
   highlighted; the ramps / boxes are (tint + yellow box edges).
2. Click bare floor: nothing selected (a selection is dropped). "Under the cursor" says "- (passing scenery
   g_WareNew_Main5 ... - Alt+click takes it)".
3. Inside the warehouse at skater height: click a wall, the ceiling, the floor - nothing selected; click a ramp,
   a box, a rail piece - it is (with its group).
4. Hold Alt over the floor: it tints / outlines; Alt+click selects the floor piece alone; middle click the same.
5. Click-to-cycle: click a ramp standing on the floor; click again without moving the mouse: the next thing under the
   cursor (status bar "2 of n under the cursor"), again ... the floor last, then back to the first. Move the mouse or
   the camera, or drag: the next click starts afresh.
6. Toolbar `Pick:` - turn off Scenery: clicks only take gizmos and objects; turn off Objects and Nodes too: clicks
   select nothing. Leave and re-enter the editor: the toggles are remembered (`[editor] pick=` in the ini).
7. Lock: select a ramp, press L - it deselects and the next click passes through it (to what's behind); the Inspector
   shows "Locked (1)" with Unlock. Unlock: clicks take it again. Lock an object (a car / prop) the same way.
8. Ctrl+S with a lock, reload the level (or leave and `-edit TR` again): the lock is back (the layout file has
   `lock sector ...`). An older DLL reading the file still loads the rest.
9. L with nothing selected: "Unlocked all n".
10. Another level: BO (streets passed, buildings and props selectable), foun (THPS3, floors passed), z_ma (THAW).
    Check nothing selectable you'd expect (a big funbox, a ramp) is now treated as structure - report its size from
    "Under the cursor" with Alt held so the thresholds can be tuned.
11. `editor_ui=0` (old HUD): the same click rules; F1 lists them; L locks / unlocks all.
12. Watch the frame rate while hovering in big levels (the hover now tests decal / no-collision pieces' triangles).

## Left / notes

- Thresholds (1500 / 400 / 0.1) are first guesses from measured sizes; tune from item 10.
- Locks aren't in undo; nodes can't be locked (the filter's Nodes toggle hides gizmo picks instead).
- The Outliner doesn't mark locked rows (the Inspector's list is where locks are seen) - kept the Outliner untouched
  for the parallel UI work.
