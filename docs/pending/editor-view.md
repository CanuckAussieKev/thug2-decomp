# editor-view (agent record): level editor step 3d, view options - NOT run in the game yet

NOT ORIGINAL (the level editor is ours). Source: `src/game/front/level_editor_view.h/.cpp` (new), small hooks in
level_editor.cpp (keys, click, Draw, the model, Start / Stop, the old HUD's help), level_editor_ui.cpp / .h (toolbar,
legend, Inspector section, help rows, `Overlay::under`, five requests), level_editor_scenery.cpp / .h (`SectorOf`,
`ForEachLevelSector`; hidden sectors skipped by Pick's `note`), level_editor_objects.cpp (hidden objects skipped by the
pick walk). Self-tests: `src/hook/selftests_editor_view.cpp` (3 early tests). CMakeLists.txt: two lines.

## What it does
- **Show collision** (C, the toolbar's "Collision (C)", remembered as `[editor] collision=0/1`): the collision near the
  camera drawn as see-through, shaded triangles through ImGui's background draw list (under the lines and gizmos),
  clipped to the near plane, painted far to near. Sectors within 3000 units (collision-only ones within 6000, and
  always first), nearest first, at most 12000 faces (the legend says "the nearest only" when capped). Cached per
  sector, made again when its collision's box or first / last vertex changes (the editor moved or turned it). Read from
  CSector +0x18 (CCollStatic, vtable 0x00645C18; flag 4 = off: not drawn) -> +0x0C CCollObjTriData (our
  `Nx::CCollObjTriData`, `GetRawVertexPos` / `FaceVertex`; its vertices are where the sector is now).
  A legend (bottom left of the view) lists the colours and the counts.
- **Invisible walls**: sectors with collision and a null geometry, magenta with their edges. With the collision shown,
  **Shift+click** on a magenta face (the nearest face drawn under the cursor) selects that wall: the Inspector shows its
  name, checksum, faces, vertices, box, size and its face-flag values with counts; it is drawn lighter with white
  edges. **Not movable / hideable**: the editor's moves, undo and layouts all go by a sector's geometry
  (level_editor_scenery.cpp's tracks, `WhyNotMovable` "collision only") - making collision-only moves would mean a
  geometry-less track kind, its undo state and a layout record kind. Left for later; the Inspector says so.
- **Hide (H) / Isolate (I or /) / Show all (Alt+H)**, also the Inspector's buttons and the toolbar's "Show all (n
  hidden)": the selection (scenery pieces / objects) hidden from the view and from clicks, or everything else hidden.
  While editing only: `View::Stop` (LevelEditor_Stop) shows everything again; nothing goes in a layout.
  - Scenery: the sector's **visibility mask** - CSector::SetVisibility 0x0049E680 / GetVisibility 0x0049E690 (geometry
    +0x44 and each mesh's viewport bits +0x1B, which the scene render tests - scene_render.cpp) - what the LevelGeometry
    Visible / Invisible node commands use (0x005A3B80: Invisible clears bit 1). Not CSector::SetActive (0x0049E9B0: also
    sets the collision's flag 4 - the editor's Delete and the layouts' `hidden` use that, read back through IsActive), so
    collision and the layout's ChangedSectors are untouched.
  - Objects: each model geometry's SetActive(false) (geometry vfunc +0x34; IsActive +0x38), as the model's suspend pass
    0x00493C60 does - which turns them on again when its distance / frustum decision changes, so `View::Tick` turns
    hidden ones off every frame. Collision and logic untouched; restored to their state when hidden.
  - Hidden pieces are passed by clicks (Scenery::Pick's note, Objects' pick walk) and their collision isn't drawn.
- Old HUD (`editor_ui=0`): H / I / / / Alt+H work (its F1 panel lists them); C says the collision view needs the UI.
- F1 help: a "View" section. Screenshot sidecar: a `view` line (collision counts, hidden count) and the wall.

## Face flags (u16 at collision face +0) - what is confirmed
Confirmed in THUG2PM.exe's code (Ghidra snapshot, this batch):
- **0x01 skatable** (any slope), **0x02 not skatable**, **0x04 wallride**, **0x08 vert**: the skater's feeler check
  0x005003A0 reads the hit face's flags (feeler +0xF8 relative to the skater): 0x01 -> skatable (+0x228 = 1); else
  0x02 -> not skatable; else 0x08 -> skatable, and vert (+0x229 = flags >> 3 & 1); else 0x04 -> not skatable,
  wallride (+0x22A = 1); else skatable by slope against a physics angle.
- **0x10 not collidable**: every feeler's default ignore mask (0x00417760 / 0x00417250: ignore 0x10); the face test
  0x004131F0 skips `flags & ignore` and `~flags & require` (ignore at feeler +0x4C, require +0x4E: SetIgnore 0x004170F0
  writes them in that order).
- **0x40 trigger**: noted in the hit (+0x49) by 0x004131F0; 0x004E5390 / 0x0043B4C4 / 0x00434E83 cast feelers that
  only hit faces with both 0x40 and 0x10 (SetIgnore(0, 0x50)) - trigger volumes the skater passes through (water,
  kill zones by data: THPS3's water is 0x50).
- **0x80 the camera passes through**: the camera's collision feeler (0x004E4425 in 0x004E4360) ignores 0x90.
  (Our docs called 0x80 "camera collides" - the code says the camera's feeler ignores those faces. Settled in
  docs/pending/face-flag-0x80.md: THUG2's 0x80 = camera passes through; THPS3 / THPS4 meant the reverse.)
- **0x1000 / 0x2000 not taggable** (graffiti feelers 0x0051DF71 / 0x0051E74B ignore 0x3000).
- 0x800: ignored by one skater feeler (0x004F8698, ignore 0x810) - meaning not known. 0x20: three feelers hit only 0x20
  faces (0x00422F51, 0x004F8341, 0x0052716D) - meaning not known.
- Not confirmed (data only): 0x100, 0x200, 0x400 (THPS3 invisible planes 0x100 / 0x180, no-shadow walls 0x480).
- Kill zones aren't a face flag: they are trigger faces whose node's TriggerScript kills (coloured as triggers).

## Checks (offline)
- Built (verify build, own PDB endpoint); thug2_check on THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe (read only):
  all 373 early tests pass.
- Mutations (tools/mutate.ts, 10): all caught (surface order x4, clip sign / interpolation / keep-out, box distance,
  isolate duplicates, hidden-set duplicates).
- `node tools/check_code_writes.ts`: No overlaps.

## In-game checklist (NOT run in the game yet)
1. Open the editor in a level (e.g. `-edit` a level), press **C**: see-through coloured faces near the camera, a legend
   at the bottom left of the view with counts; the toolbar's Collision (C) is ticked. Press C again: gone.
2. Close the game and start again: the collision view comes back on (C remembered in `[editor] collision=`).
3. Fly around with it on: no stutter worth noting; faces don't stick to the screen behind the camera; faces cut cleanly
   at the near plane when flying through a wall.
4. Quarter pipes orange (vert), wallride walls yellow, floors pale or green, railings / decorations grey, water and
   kill zones blue (THPS3 Foundry / a level with water).
5. Find an invisible wall (THAW The Mall's entrance blocker, a level's out-of-bounds blockers): magenta with edges.
6. **Shift+click** a magenta face: the Inspector shows "invisible wall", its name, faces, box and face flags; it turns
   lighter with white edges. Shift+click elsewhere (not magenta): the old Shift+click (add / remove) works as before.
   Esc / Backspace drops the wall.
7. Select a piece of scenery (a click), press **H**: it vanishes; clicks go through to what's behind it; its collision
   isn't drawn. The toolbar shows "Show all (1 hidden)".
8. Skate test: leave the editor (Esc) - the hidden piece is back, and was never non-collidable (play the level: it
   collides as before). Re-enter: nothing hidden.
9. Select an object (a car, a level object), H: it vanishes and stays hidden while flying far and back (the suspend
   pass turns models on and off with distance). Alt+H: it's back.
10. Select something, press **I** (or **/**): everything else (scenery and objects) vanishes; the selection stays
    selected and movable. Alt+H: everything back. Nodes' gizmos stay.
11. Hide something, Ctrl+S, leave, load the level's EDITED entry: nothing is hidden (never saved).
12. Hide a piece, then Ctrl+D a different piece, undo / redo, Reset level (Ctrl+Shift+Delete): no hidden piece comes
    back wrongly and nothing else goes invisible.
13. Old HUD (`editor_ui=0`): H / I / Alt+H work; F1 lists them; C shows "needs the editor's UI".
14. Screenshot (Print Screen) in the editor: the sidecar has a `view` line.

## Left / ideas
- Moving / hiding invisible walls (needs geometry-less tracks, undo state and a layout record kind).
- A hover highlight for walls under the cursor while Shift is held; colouring trigger faces by their node's script
  (kill vs other); the Outliner marking hidden rows; the collision view in the old HUD (it would need the renderer's
  own triangle draw).
- A sector duplicated while hidden in the editor (picked in the Outliner) copies its visibility: the copy stays
  invisible until Show all / leaving the editor, which shows such copies as their source is (layouts re-make copies
  from the shown source, so nothing is saved hidden).
