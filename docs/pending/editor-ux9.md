# editor-ux9 (agent record): gap wizard, skateability overlay, raised / lowered ground - NOT run in the game yet

NOT ORIGINAL (the level editor is ours). Built and checked offline only; the user tests the interactive behaviour.
Branch `worktree-agent-a4b02271225e701fa` (main with UX3 / UX4 / UX5 / UX6 / UX7 / goals merged in).

## Files
New: `src/game/front/level_editor_gap_wizard.h/.cpp`, `level_editor_skate_view.h/.cpp`, `level_editor_ground.h/.cpp`
(pure generator, records), `level_editor_ground_game.cpp` (run-time pieces), `level_editor_ux9.h/.cpp` (glue, the
Ground tool's form, the actions, the layout extension), `src/hook/selftests_editor_ux9.cpp` (6 early tests);
`tools/game/ground_pieces.ts` (+ `ground_pieces.test.ts`).

Small hooks in shared files (additive):
- `level_editor.cpp`: include; `Ux9::Init()` after `Ux3::Init()`; `Ux9::Start()` / `Stop()` beside Overlays'; `Ux9::Update()`
  after `Notes::Update()`; Escape: `Ux9::Busy()` makes it a cancel, `Ux9::Cancel()` first in the cancel branch;
  `Ux9::Hover(...)` after the ruler's hover; a click branch (`Ux9::Busy() && Ux9::Click(...)`) before the ruler's;
  `Ux9::AddOverlay(painter)` after `Ruler::AddOverlay`; the screenshot sidecar's `ux9` line.
- `level_editor_ui.cpp`: `Ux9::Windows(...)` after `Notes::Window`.
- `level_editor_overlays.h/.cpp`: bit `kSkateability` (1 << 9, off by default; kAllBits now 10 bits) and its menu row.
  UX3's registry turns it into an "Overlay: Skateability" toggle in the command search automatically.
- `level_editor_scenery.cpp`: `WhyNotMovable` says "a generated ground piece - edit it with the Ground tool".
- `level_editor_triggers.h/.cpp`: `Triggers::AddGapPair(start, end)` (the wizard's two ends, names / GapID fresh,
  one undo step, the start selected).
- `level_editor.h`: the module list's comment. `CMakeLists.txt`: 6 lines.
- (after merging UX8) `level_editor_hints.h/.cpp` + the context's fill in `level_editor.cpp`: `ux9Tool` - the status
  bar's hints while the gap wizard picks (LMB pick an end, Alt+J off, Esc) or the ground tool marks (LMB a corner / edit a
  piece, Alt+U off, Esc). The pies (fixed built-in slices) aren't given the tools - the command search and keys are.
  Escape order: UX8's pie first, then the UX9 tools.
- `tools/game/layout_file.ts`: `grounds` (the `ground` record; an unreadable one goes to `unknown`, as the DLL skips it);
  `tools/game/apply_layout.ts`: the bake of `ground` records (after the clones), its header comment.

Keys (the action registry, rebindable; checked against the built-ins by a self-test): **Alt+J** gap wizard (`gap_wizard`),
**Alt+K** skateability overlay (`skateability`), **Alt+U** raise / lower ground (`ground_tool`). Escape cancels a tool. All
three are in the command search (F1 lists the registry).

## 1. Gap wizard (Alt+J)
Click where the gap starts, then where it ends; each click is classified: a **rail** (the rail manager's active segment
nearest the cursor within 14 px, not behind the first surface hit), else the collision face hit - floor (normal y >= 0.7,
not vert) an **area**, near-vertical (|y| < 0.2, not vert) a **wall**, otherwise (slopes, vert faces) a **lip**. A hover
dot / label shows what a click would take; after the first pick a line follows the cursor. The form (name, score
50 - 5000, type) then makes the gap with phase 5's code: `Triggers::AddGapPair` - a GapStart volume (StartGap GapID= flags=)
and a GapEnd volume (EndGap GapID= score= text=), overlay nodes, `trigger` records, baked like any trigger, in the gap list
(and UX6's landed / untested list, which reads the gap checklist). Gates face the other end: rail 120 x 120 x 60 across
the rail (thin along it), lip 300 wide x 400 high above it, wall 300 x 300 80 units off it, area 300 x 300 on the ground.
Types -> StartGap flags: Air [CANCEL_GROUND] (the editor's existing default), Grind [REQUIRE_RAIL, CANCEL_GROUND], Lip
[REQUIRE_LIP], Manual [PURE_MANUAL], Wallride [REQUIRE_WALLRIDE, CANCEL_GROUND]; suggested from the spots (two rails:
grind, a wall: wallride, else air). **Skitch: THUG2's gap flags have none - shown disabled with that tooltip.** Flags can
be changed afterwards in the trigger Inspector (both ends follow), as can size / place (handles, drag).

## 2. Skateability overlay (Alt+K / Overlays menu)
Classes, in the skater's own order (read in 0x005003A0 for this): 0x01 skatable whatever the slope; else 0x02 not
skatable; else 0x08 vert (skatable); else 0x04 wallride; else skatable when normal y >= sin(script float 0x3EEDE4D3 x
the float at 0x00645464) - read from the game, the legend prints the value. Skatable split into Floor (normal y >= 0.97)
and Bank / ramp. 0x10 faces not drawn, except the level's kill triggers (kill red, water blue - Triggers::LevelTriggers'
kinds); the editor's own kill / water triggers outlined. Grind rails: the rail manager's active segments, thick cyan
(THUG2 grinds only rail nodes - no collision edge is grindable by itself). Legend bottom right with face counts.
Cached per sector (made again when its collision's box / first / last vertex changes); nearest sectors within 3000
units, at most 12000 faces, painted far to near under the lines.

## 3. Raised / lowered ground (Alt+U) - generated at run time, and baked
Two clicks (snapped to the form's grid, 50 by default) mark a rectangle; the ground height is cast at its centre and
the material chosen (the piece the first corner was clicked on, if it can lend it; else the nearest floor-like piece);
the form: Raise / Lower, height / depth, ramps per side (none = wall, kicker, quarter pipe), ramp length (auto: kicker
2 x height, QP radius = height; in a pit opposite ramps share the width), the rectangle and ground height as numbers,
"Material from the selected scenery", Make / Apply / Remove / New rectangle / Close, a list of the level's pieces (a
click goes there and edits it). The form's piece is previewed as a wireframe. Make / Apply / Remove are one undo step each
(a module step - Ctrl+Z / Ctrl+Y). A click on a piece with the tool armed opens it; a plain click selects it as scenery
but it can't be moved ("edit it with the Ground tool").

**Generation approach (run time):** `CScene::CloneSector` (0x0049DF30) copies the material piece (the "donor": its
material, vertex format, collision object), named `Crc(name_material)`. The copy's first mesh then gets **new** vertex
buffers (0x004D5760, filled in the donor's FVF layout: position, normal, size, colour - the donor's average -, every UV
set planar by the face's main axis at the donor's measured texel scale) and new index buffers (malloc + 0x004D4270,
uploaded through IDirect3DIndexBuffer9::Lock) holding one strip that draws every face both ways (degenerate joins,
each face at an even position - culling can't hide it); its vertex count, index counts (LOD 0 and any LOD the donor
had), sphere and the geometry's box set; per-vertex bytes made again, the copied position / triangle lists dropped
(count 0); the copy's other meshes' strips zeroed (degenerate). The collision geometry (the copy owns it) gets new
arrays from `Mem::New` (what `CCollObjTriData::Destruct` frees): float vertices, 10-byte faces (flags, terrain, u16
indices), intensities 0x80, a one-leaf BSP tree (new[] layout, every face), rejection scale 0; the CCollStatic's
face-rejection bytes re-made on the bottom-up heap (as its Clone). Then `SetActive(true)`, `Scenery::NoteCollisionMoved`
+ `Commit` (super sectors). Removing hides the sector (kept in a pool for reuse by undo / redo / re-apply). Face flags:
decks / ramps 0x01, quarter-pipe faces steeper than 45 degrees 0x09 (skatable + vert), walls / cheeks 0x04; terrain the
donor's commonest. Faces wound so (b - a) x (c - a) points out of the solid (the static line test is one-sided).
Limits: 2048 faces (the tree-less face table), donors must be a plain PC geometry, not a billboard / shared mesh, a plain
XYZ FVF with texture coordinates whose size is the stride, no per-vertex data, not see-through, with collision.

**Pits:** the floor can't be cut, so lower = the sectors with ground faces (normal y >= 0.7, within 30 of the ground
height, overlapping the rectangle; collision-only ones included) are hidden **if each lies within the rectangle + 25**
(tiles); one reaching out refuses with its name and size ("THUG2's floors can't be cut: make the rectangle cover that
whole piece, or raise around it"). The pit: floor `depth` below, walls facing in (wall-ridable), ramps inside the walls
(no cheeks). The tiles hidden are the record's `hides=` and come back with the piece's removal / Reset level.

**Record** (a layout extension record, version 2 kept; an older DLL keeps it as an unknown record):
`ground <name> raise|lower rect=x0,z0,x1,z1 base=y height=h ramps=<-x>,<+x>,<-z>,<+z> length=l material=<sector> uv=u
color=AARRGGBB terrain=t [hides=0x...,...]` (ramps `-` / `k` / `q`; uv, colour, terrain measured on the donor when made,
so the bake gives the same piece). Applied after everything else (Layout's extensions); a piece that can't be made is
logged and its record kept.

**Bake** (`tools/game/apply_layout.ts`, `tools/game/ground_pieces.ts` = the DLL's generator ported): a new scene sector
`ed_groundNN` (the donor's first mesh copied for its material / format / shaders, the generated vertex buffer, strip and
lods = the two-sided strip, one vertex buffer) and a collision object of that name (large faces, fixed-point while it
fits, tree and reaches built); hides= tiles left out of scene and collision; report line; THUGPRO parity warning.
Warned and skipped: a donor not in the scene / of another format, a name already there.

## Checks (offline)
- Built (own dir out\build\ux9, PDB endpoint thug2ux9); `thug2_check.exe` on THUG2PM.exe and on
  C:\Games\THUG2\Game\THUG2.exe (read only): 0 MISMATCHED, all early self-tests pass (503 after merging UX8; 6 new: TestEditorGroundShapes,
  TestEditorGroundStripAndVertices, TestEditorGroundRecords, TestEditorGapWizard, TestEditorSkateability,
  TestEditorUx9Keys).
- `node tools/check_code_writes.ts`: No overlaps (no REPLACE added).
- Mutations (`node tools/mutate.ts out/scripts/mut_ux9.ts`, 24): all caught (one, the deck's uv axis, after a stronger
  test) - winding, vert faces, the QP extension, kicker length, pit width sharing, pit cheeks, sink, strip parity /
  two-sidedness, FVF specular offset / stride check, record key twice / material missing, tile margin, ground tolerance,
  wall-vs-vert spots, rail gate direction, grind flags, score limit, the skateability order (0x02/0x08, 0x08/0x04), the
  floor threshold, pass-through faces.
- `npm run typecheck`, `lint`, `test` (149 pass; new: ground_pieces.test.ts - the same shape numbers as the C++ tests -
  and a bake test in apply_layout.test.ts: platform with QP, pit with a hidden tile, a missing material warned).

## In-game checklist (NOT run in the game)
Use a workspace with this branch's DLL; watch `thug2-decomp.log` (`level editor:` lines; EXCEPTION / RUN-TIME CHECK /
DEBUG CRT). `node tools/game/edit_level.ts TR` (Training) unless said otherwise.
1. **Keys:** the status bar's hints show the tools' clicks / keys while one is armed. F1 / the command search (Ctrl+Space) list Gap wizard (Alt+J), Skateability overlay (Alt+K), Raise / lower
   ground (Alt+U). Plain J / K / U still do chain link / Faces mode / chain unlink.
2. **Skateability:** Alt+K (or Overlays > Skateability): floors green, slopes teal, quarter pipes orange, wallride walls
   yellow, other walls red, legend bottom right with counts and the slope limit (expect about 0.5 - 0.8). Rails thick
   cyan. Alt+K again: gone; the Overlays menu's checkbox follows; restart: remembered (`[editor] overlays=`).
3. Boston (`edit_level.ts BO`) or a level with water: kill planes red, water blue; add a kill zone (T window): outlined red.
   Frame rate with the overlay on near the busiest spot: should stay playable (note it if not).
4. Compare: a face the overlay calls "Not skatable" (red, plain steep) - skate onto it: the skater can't ride it; a teal
   bank: rideable.
5. **Gap wizard:** Alt+J: the hint; hover a rail - "a rail" label; click a rail, then click a second rail across a gap:
   the form, type Grind preselected, flags shown; Skitch greyed (tooltip). Name it, score 500, Make: two boxes (green
   start, yellow end) appear, the start selected, its Inspector shows "Gap start" with REQUIRE_RAIL, CANCEL_GROUND;
   log `gap wizard - "..." (500, Grind) from a rail to a rail` and `added trigger ...`.
6. Play (F5): grind the first rail, transfer to the second: the gap is awarded; pause > gap list has it; UX6's play-test
   list shows it landed.
7. Wizard with floor -> lip of a kicker (Air), and wall -> floor (Wallride suggested): gates look sized sensibly (a lip's
   rising 400 above it). Esc during picking: "Gap wizard cancelled". "Pick again" restarts the picks.
8. Ctrl+Z after Make removes both ends; Ctrl+S saves them as `trigger` records; reload the (EDITED) entry: they're back.
9. **Raise:** Alt+U, click two corners on Training's floor (the rectangle follows the cursor, snapped to 50): the form
   and a wireframe preview. Height 150, +X side Quarter pipe, -Z Kicker; Make: a solid platform textured like the floor
   it stands on (lit about as the floor), log `ground piece ed_ground01 (platform, ...) in place - material <piece>`.
   Look from all sides (both faces drawn: no holes), look at it with Alt+4 (collision only): its collision present.
10. Skate it: deck rideable, the kicker launches, the quarter pipe goes vert (straight up and back down) at its top,
    the walls wallride; landing on the deck works; no falling through anywhere (edges, cheeks).
11. Skateability overlay on the piece: deck green, kicker teal, QP lower half teal / upper half orange, walls yellow.
12. Edit: Alt+U then click the piece (or the form's "Pieces in this level"): change height / ramps, Apply: the piece
    changes in place; Ctrl+Z / Ctrl+Y step through; Remove: gone, Ctrl+Z back. A plain click on it says it can't be moved.
13. "Material from the selected scenery": select a brick / metal piece, press it, Apply: the piece takes that texture
    (a refusal names why when the piece can't lend it, e.g. "a see-through material").
14. **Lower:** on a floor made of tiles (a THPS port's park floor, or any level whose floor is cut into pieces): a
    rectangle covering whole tiles, Lower, depth 150, quarter pipes on two opposite sides: the tiles vanish, a pit with
    walls and a bowl appears; skate into it. On Training's one-piece floor: refused - the message names the floor piece
    and its size.
15. Save (Ctrl+S): the layout file has `ground ed_ground01 raise rect=...` lines; leave and load the (EDITED) entry: the
    pieces are back (`the layout's N of N ground pieces put in place`); Reset level: gone (tiles shown); Ctrl+Z: back.
16. Bake: `node tools/game/apply_layout.ts --level TR --game C:\Games\THUG2-n --dry-run`: `ground ed_ground01: a platform
    ...` report lines, no "unsupported record"; install and load Training as shipped (layout moved away): the piece is
    there, skateable as in the editor. (Whether THUG2's own levels read the overlay's loose .scn/.col over their pre
    archives is the open question of editor-bake2 - try a ported level if Training shows nothing.)
17. Leave the editor and load another level, then come back: no crash, no stale pieces in the other level.
18. Screenshot in the editor: the sidecar has a `ux9` line (pieces, overlay, wizard).

If something is wrong:
- Piece invisible but solid: the mesh's index / vertex buffers didn't draw - log the donor (`material ...`) and try
  another material piece (a single-mesh one).
- Piece visible but you fall through: the collision rewrite (super sectors) - Alt+4 shows whether its faces are there.
- Crash on leaving the level: the freed arrays - note the donor piece; the generated arrays come from Mem::New as the
  game's own copies.

## Left / limits
- Corners between two ramps on adjacent sides are left open (each ramp's cheek closes its own end).
- Copings aren't grindable: no rail is made along a deck's or QP's edge - use UX7's auto-rail along an edge (Alt+R) or
  draw one with the chains.
- Each Make / Apply orphans the copy's replaced vertex / index buffers (the engine's buffer destructors aren't decompiled
  yet): a few KB of memory and card memory per edit, until the game exits.
- The generated piece takes one material (the donor's first mesh); multi-pass materials draw every UV set the same.
- Pits need floor tiles inside the rectangle; on one-piece floors only raising works.
- The skateability threshold assumes the plain-face test is the only slope rule (vert / skatable flags win, as in
  0x005003A0); faces of movable objects (cars, moving platforms) aren't classified (static collision only).
- The wizard's gates are boxes facing along the start -> end line; a curved transfer may want them turned by hand.
