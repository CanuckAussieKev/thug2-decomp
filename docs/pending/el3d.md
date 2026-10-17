# wt/el3d: the Element3d screen element, and the widescreen front end's backdrop

**NOT run in the game yet.** Disable with `[replacements] disable=element3d.cpp` (the class and the rule); the
matrix helpers are in matrix.cpp, the aspect / angle getters in screen.cpp, the layout getters in
screen_element.cpp. The sprite change (below) is in sprite.cpp. The rule alone is off with
`[features] widescreen_menus=0` (which also turns off the rest of the widescreen front end).

## Batch el3d (24 functions)
- **Front::CElement3d** (src/game/front/element3d.*; vtable 0x00649FF4, 0x510 bytes): constructor 0x4A6060,
  destructor body 0x4A6240, scalar deleting destructor 0x4A6D00 (slot 0), SetProperties 0x4A6630 (slot 1),
  SetMorph 0x4A6BA0 (slot 9), Update 0x4A6D30 (slot 10), ClearModels 0x4A5E30, ApplyViewport 0x4A5E80, AddModel
  0x4A6540 (`RET 0x14`: params + a Vector by value), ScaleToScreenDims 0x4A62B0, ApplyAngles 0x4A6BD0 (cdecl,
  switch on the angle order), **ScreenToWorld 0x4A5EE0** (EAX = centre vector, ESI = out matrix, then cdecl
  viewport, x, y, z: naked stub, REPLACE_UNCHECKED; only caller is Update).
- CScreenElement (screen_element.*): GetLayoutWidth 0x4A87A0 / GetLayoutHeight 0x4A87B0 (return the x87 product
  unrounded: `double`); the layout now names +0x114 (alpha), +0x134/+0x138 (x, y), +0x13C/+0x140 (scale) and
  sizeof 0x178; +0x144/+0x148 and +0x160/+0x164 are the two factors of the layout size (which is dims and which a
  scale isn't known).
- Mth::Matrix (matrix.*): Ident 0x41A640, Scale(Vector) 0x41BA30 (each row times the vector component-wise),
  Translate 0x48A230 (row 3 += v), RotateX/Y/Z 0x41A6A0/0x41A6D0/0x41A700 (this = this * rotation; the
  originals use D3DXMatrixMultiply, ours Mth::Multiply as RotateXLocal does), `Vector * Matrix` 0x41F470 (the
  original's x87 summation order).
  (Next to Ident, 0x41A680 isn't Matrix code: see Left.)
- Nx (screen.*): GetScreenAspect 0x4A0790, SetScreenAngle 0x4A07A0 / GetScreenAngle 0x4A07B0 (0x701348, the
  cameras' default angle from the script global camera_fov).

### How an Element3d is drawn (the conventions)
- Its models are ordinary composite objects (up to 25, `m_models` at +0x178: smart pointer + offset), made by
  the composite object manager and set up by the scripts element3d_add_components / element3d_init_model; the
  3D pass draws them with the world (their model's viewport mask = 1 << active_viewport). Nothing about them is 2D
  and they are **not** clipped by the 2D pass's 4:3 viewport - in the widescreen front end they are drawn with the
  full-window 3D view (the goals menu's sign overhangs the 4:3 area).
- Update places them each frame. On the layout (no ParentParams): ScreenToWorld maps the element's centre (its
  top left + half its layout size) to the camera's frame: x' = (x - 320) * aspect * 0.75 * z / -448,
  y' = (y - 224) * z / 448 (both * 0.9 on back buffers wider than 640), z' = CameraZ (-500 for the backdrops), plus
  the scale_to_screen_dims centre; the element's matrix is the camera's orientation translated there. With
  ParentParams it follows an object (by id, through the composite object manager; when it's gone the script
  KillElement3d is spawned on the element and it stops) or a node, with the identity orientation.
  NodeToPointTo / ObjectToPointTo turn it to face the target (row 0 w = 1, sic) and tilt it.
- The models' own matrix: identity about the pivot, turned by angleX/Y/Z (advanced by AngVelX/Y/Z each frame,
  wrapped within a turn with truncation) in the AngleOrder (default Y X Z), then the columns scaled by
  (scaleX, scaleY, scaleX) * the fit scale (* the clamped distance factor with ScaleWithDistance), then times the
  element's matrix. Hover adds sin(2 pi t / period) * Amp along the matrix's row 1. Hidden while the alpha is
  below 0.0001.
- So the screen x of a layout position is spread by aspect * 0.75 with the 3D view's aspect - in the widescreen
  front end, 3D elements away from x 320 sit further out than the 2D layout's 4:3 positions would put them (the
  goals menu's sign). Not changed here (it's the original's own widescreen behaviour).
- Quirks kept: ScaleToScreenDims' vertical fit uses the width in its perspective factor; its bounds are
  uninitialised if the first slot is empty (zeros here); the look-at target is uninitialised when the object isn't
  found (zeros here); NaN comparisons follow the original's x87 flag tests.

### The widescreen front end's rule (widescreen_menus, front end only)
The backdrop panel (UI_bg.mdl, made by make_mainmenu_3d_plane in skateshop.qb and used by the network, goals,
Create-A-Graphic, high scores, options, dialog box and keyboard menus via thememenu.qb etc.; also catmenu's
cat_bg.mdl) is a flat model with hard edges sized for 4:3: the 3D view is already the window's, so widening the
projection can't show more of it. It is **stretched horizontally**, in the camera's frame, about the element's
place: Update scales the models' x column by k (WidescreenStretch) when the element is on the layout and facing the
camera (no ParentParams, not pointing at anything), every model corner (bounding box + offset, through the
element's own matrix and scale) is in front of the camera, and its projected leftmost and rightmost corners reach
within 5% of the 4:3 area's edges or past them (on 16:9 the 4:3 area is 0.75 of the half-width; the panel reaches
0.752 / 0.768 in the user's screenshot). k is the smallest stretch that brings a corner to each window edge (the
larger of the two sides) plus 1%, never below 1. Signs, icons, boards and decks (small or off-centre) are left
alone. The panel's picture (the city on the globe) is stretched by about 1.33 on 16:9 - the cost of filling the
width with a model that ends.

### Tests
- TestElement3dMath: the matrix helpers, ApplyAngles (every order and two invalid ones), the layout getters,
  GetScreenAspect, ScreenToWorld through its stub (with and without a camera, 1920 / 640 back buffers, three
  aspects, four places) - original vs ours, exact.
- TestElement3dClass: original vs ours on stand-ins (every outside call recorded; D3DX and the Mth rotation /
  normalise helpers redirected to the same code), widescreen_menus off: the constructor on dirty memory,
  SetProperties (a model with most properties, two Sectors with scale_to_screen_dims, a Sector with ParentParams,
  CloneModel), SetMorph with / without scale_to_screen_dims, Update in 7 layout cases (pointing at nothing / a
  node / an object, hover, distance scaling clamped both ways, no camera, 640 back buffer, alpha at / below the
  threshold, an empty slot, every angle order and an invalid one, angles past a turn and negative), following a
  node, an object, the object gone (KillElement3d), dead; ApplyViewport, ClearModels, the destructors (flags 2, 3).
  The element is compared word by word after each step, then the whole arena.
- TestElement3dWidescreenRule: worked-out k on 1920x1080 (tan(half angle) 1.2): a panel reaching -460 / 470 at
  500 (k = 600 / 460 * 1.01), off-centre by 10, corners at two depths (471 just widened, 470 just not), and
  unchanged for: setting off, in a level, small, one side short, already past both edges, behind the camera.
- TestElement3dWidescreenUpdate: through Update with a 90-degree camera: the model's x row is stretched by
  497.9 / 420 * 1.01 relative to the setting off, its y row unchanged; in a level identical to off.
- Mutations: 73 deliberate bugs, 69 caught (9 after strengthening the tests: summation order with cancelling
  terms, ScaleToScreenDims' NaN / depth / vertical fits, tilt, pivot w, hover w (the objects' positions are now
  compared after every step), a corner behind the camera, a second model without geometry); 4 equivalent:
  `down * 0.9f` in float (one rounding either way), the point's w in ScreenToWorld (unused), the order the
  destructor releases the slots (each object has one pointer), row 0's w before OrthoNormalizeAbout(2) (rebuilt).
- Rule detail: the rule asks each model for its bounding box every frame (0x493CD0(0) / 0x48DE80); a model without
  geometry yet leaves the element unchanged that frame (no null dereference).

### What to look at in the game (a 16:9 window, widescreen_menus=1)
- Network ACTIONS menu, Select Goals, Create-A-Graphic, High Scores (and other menus with the dark purple panel):
  the panel should now reach both screen edges (no skate shop showing at the sides between the top and bottom
  bands); its contents (the city / globe picture) are horizontally stretched by about a third. The menu boxes,
  text and the yellow signs stay where they were. The skater-select / board / deck models and the signs unchanged.
- widescreen_menus=0: everything exactly as before (PartyMod's pillarbox); in a level nothing changes.
- If the panel's edge shows a seam or doesn't quite reach: thug2-decomp.log has nothing for this yet - say which
  menu and the window size.

## Sprite edge rule (sprite.cpp, also on this branch)
The edge rule now extends a side only for sprites that overlap the 4:3 layout (left only if the layout right edge
is > 0, right only if the layout left edge is < 640), upright and quarter-turned alike; a sprite wholly outside
(parked off-screen, a band's end strip) is drawn where it is. TestSpriteWidescreenEdges gained 8 such cases
(edges exactly at 0 / 640, upright and turned 180); 8 deliberate bugs caught. About the bottom-left shade: the
bottom band is 10 abutting 76.8-wide strips (thememenu.qb, rot_angle 180) spanning about layout -87..684, so its
end strip on the left (about -87..-10) lies wholly outside: before, it was stretched to screen 0..217 and drawn
over by / over the next strip's stretched part (a different strip texture there - the "shade"); now it keeps its
own place (screen ~44..217) and only the strip crossing x 0 reaches the screen's edge. If the band is
semi-transparent the overlap 44..217 is still drawn twice, as the band's own layout does past the layout edge;
if that still shows, the remaining option is not drawing sprites wholly outside the layout in the widescreen
front end (they were never visible in the original's 4:3 screen).

## Left
- 0x41A680 (a 30-byte Vector getter of some other class: +0x1C..+0x28, one caller 0x421AE0) - between Matrix::Ident
  and RotateX, not part of Matrix.
- 0x4A57D0 / 0x4A5D30 (+0x440..+0x474 of another element class, before Element3d's code), the rest of the screen
  element code 0x4A74E0-0x4A8790 and CScreenElement itself (constructor 0x4A8F90, destructor 0x4AAB30,
  SetProperties 0x4A96B0, SetMorph 0x4A9AA0).
- The viewport camera lookup 0x4A09C0 / 0x4A06D0, the camera getters 0x485A00 / 0x485A10 / 0x485B10, the model's
  bounding box 0x493CD0 / 0x48DE80 and viewport mask 0x494970, the node lookups 0x45C1B0 / 0x45C050 (GameFunction).

## For the merger
- New files: src/game/front/element3d.{h,cpp}, src/hook/selftests_el3d.cpp (both in CMakeLists.txt).
- CScreenElement (screen_element.h) gained fields up to 0x178 and static_asserts its size; Mth::Matrix gained
  Ident / Scale / Translate / RotateX/Y/Z and `Vector * Matrix`; screen.h gained Get/SetScreenAngle and
  GetScreenAspect.
- docs/PROGRESS.md not regenerated here (run tools/progress_report.py after merging); no decomp-progress.md entry
  yet - this file is the record.
- Seen once during mutation runs: "PC async handle (synchronous I/O)" failed on its own (an unrelated, apparently
  flaky test).
