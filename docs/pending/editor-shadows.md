# Level editor: shadows follow their piece; removing parts of a selection

Branch `worktree-agent-a5e73190d4dc99883`. **NOT run in the game yet** (the user tests interactive editor behaviour
himself; checked offline only: build, thug2_check on THUG2PM.exe and the stock THUG2.exe, mutations, check_code_writes).

## The requests

1. In TR a rail's shadow is a separate see-through decal sector on the floor; moving `SkWare_Rail01` left its shadow
   (`g_big_rail_shadow`) behind. A selected group should take its shadows.
2. Deselect parts of a larger selection: Ctrl+click, an Inspector list with x / Keep only this, Alt+click still
   toggling a selected piece out.

## What changed

- **New `src/game/front/level_editor_shadows.{h,cpp}`** (pure, self-tested): the shadow-name parser (`OwnerNames`),
  the place rule (`FallsUnder`, `NearestToGroup`, `StandsBy`, `Beside`) and Ctrl+click's choice (`Members::DropTarget`).
- **`level_editor_scenery.cpp` `GroupOf`** (so SelectGroup, AddGroup / Shift+click and the hover's outline): after the
  cluster and the built-on pieces, the group's shadows - among the scene's shown, movable pieces with the same
  placement as the clicked one (as the rest of GroupOf):
  - **(a) by name, always first.** A name whose last "shad" is followed only by letters, then optionally `_` and a
    number, then optionally one letter (`_Shadow`, `Shadow`, `_shadow`, `_SHADOW`, `_Shado_01`, `shad`, `_Shadows_554b`,
    `Shadow31`) belongs to what's before it (one trailing `_` dropped). Candidates, most specific first: prefix `_`
    number, prefix + number, prefix (BO's `BO_Classic_Barrel_Shadow_01` -> `BO_Classic_Barrel_01`; NJ's
    `hj_nj_PinkhouseWiresShadow03` -> `hj_nj_PinkhouseWires03`). The owner is the first candidate whose checksum is a
    sector of the scene; when the owner is a group member, the shadow joins - whatever its shape (TR's
    `TB_Beam_Upper_01_Shadow` and the ladder's shadow stand upright on walls).
  - **(b) by place** - only for pieces whose name has "shad" in it and that have **no owner by name in the scene**, and
    only when the click wasn't on structure: a see-through piece (GeomSeeThrough), flat (no taller than **16**), its
    top within **10** of a member's bottom, at least half of its footprint within that member's footprint grown by
    **10 + the member's height**, at least **a quarter** of the member's footprint in area, at most **1500** wide; the
    member taller than 16 (a flat piece casts none; it isn't a shadow taken this round); and the shadow's middle no
    further from that member's footprint than from any other piece standing on that floor (not in the group, not
    see-through, not structure; bottom within 10 of the shadow's top) - inside both: the nearer footprint middle wins.
    Unnamed decals, graffiti, puddles never join.
  - A copy's shadow (Ctrl+D) is named for its source: it joins by name when a copy of its owner is in the group and
    the shadow is beside that copy (`Beside`: the footprints within 10 + its height).
  - `debug_editor=1` logs each join: `level editor:   shadow by name: SkWare_Rail02_Shadow (27 x 1 x 546) of
    SkWare_Rail02` / `shadow by place: g_big_rail_shadow (...) of SkWare_Rail01`; the selected list marks `(shadow)`.
  - The shadows are ordinary members: moves, quarter turns, scale, Home, Delete / H / I, Ctrl+D, undo and the layout
    carry them as they carry the other members. The pivot / bottom (arrows, G, scaling's anchor) and the rails found by
    box leave the shadows out while the selection has anything else.
- **Ctrl+click** (Ctrl wasn't used for clicks - only for snapping while dragging): on a part of the scenery selection
  removes just that piece - the nearest selected piece no further than what a plain click takes, so a selected shadow
  or floor in front counts and a selected piece behind something unselected doesn't. On a carried rail point's gizmo
  (UI on): that point is no longer carried (until another group is selected; `Scenery::DropRail`). No drag. A
  Ctrl+click on anything else is a plain click (so Ctrl held from the start of a snapped drag on an unselected thing
  still works). Note: Ctrl+click on a *selected* piece now removes it instead of starting a snapped drag - drag first,
  then hold Ctrl.
- **Alt+click on a selected piece** still takes it out (unchanged path: `one` -> `Scenery::Toggle`; Alt's hover takes
  even passed pieces, so a selected shadow can be Alt+clicked out too).
- **Inspector**: with more than one piece or any rail points carried, "Parts": each piece (name, size, "shadow"
  marked) and each carried rail point, each with **x** (removed / no longer carried) and **Keep only this** (a piece:
  that piece alone with its rails; a rail point: that node selected by itself). Requests `kMemberRemove` /
  `kMemberKeep` (level_editor_ui.h).
- Help: F1 rows (Ctrl+click, Shadows, Alt+click on a selected piece) in the UI; the old HUD's F1 panel and its key
  line (`Ctrl+click remove one`) and the status bar's keys for a scenery selection.
- Shared files touched (small, additive): CMakeLists.txt (two files), level_editor_ui.h (Model::Selection::members,
  two Request kinds), level_editor.cpp (Ctrl+click branch, the Inspector requests, help / key rows).

## What joins in TR (offline survey of TR.scn.xbx + TR.qb; out/scripts/shadow_survey.ts, each shadow vs single pieces)

By name (18): SkWare_Rail02_Shadow -> SkWare_Rail02, SkWare_Kicker_Wood02b_Shadow -> SkWare_Kicker_Wood02b,
WH_TB_Foldingchair_01..04_Shadow -> the chairs, WH_TB_Sk8brd_02_shadow / WH_TB_Ska8brd_01(b)_shadow -> the boards,
Wh_TB_Boombox_01_shadow, TrashCan_Placeholder(01)_shadow, JG_ArcadeGame_shadow, TB_Beam_Upper_01_Shadow,
g_WareNew_MainLadder_shadow, LO_YardGates01/02Shadow, LO_forlease03shad.
By place (3): **g_big_rail_shadow -> SkWare_Rail01** (the user's rail), wh_tb_launchramp_shado01 ->
SkWare_Kicker_Block01, wh_tb_shadows_554 -> JG_3B_YellowTube.
Not taken (15): SKWare_Cab_Qp_Shadow (684 x 381 by the 2880-wide quarter pipe: under a quarter of its footprint),
Car_Shadow (a car's - an object), g_warenew_rail_shadow_56 (29 high), TB_WH_Room2_Rail02_Shadow31 (1288 x 100 x 2049,
a room's), wh_tb_shadows_554b (2048 x 751 x 2456), the NJ pieces TR carries (teleshadow*, dt_shad*, NJ_hj_FenceShadow),
BlackBoard_Placeholder_shadow (upright, no owner).

Other levels' naming (offline): **BO** 125 shadow names - `_Shadow`, `Shadow01`, `_Shadow_01`, `_Shado_01`,
`Shadows`, `shadow_02d`, NightOn_/NightOff_ day / night copies; 40 by name (barrels, trash cans, BO_tz_boat), 18 by
place (street lights, hydrant, bus stop, tables, the boat-mast shadow next to a barrel - the one doubtful one); many
draped over the ground (24-165 high) aren't flat and stay. A looser flatness (32) took JG_CO_StatueShadow under a tree -
kept at 16. **THUG1's NJ port**: `_Shadow` (graves), `Shadow03`, `shad01`, `teleshadow07`; 19 by name, 5 by place.
**THAW's z_dt**: `_shadow_01`, `_shadow01`, `_SHADOW`, `shad23`, `selfShadow` (no owner: by place or not at all).

## Checks (offline)

- `out\scripts\build.cmd` (own build dir, PDB endpoint `thug2shadows`): no errors, no warnings.
- thug2_check on THUG2PM.exe and C:\Games\THUG2\Game\THUG2.exe: 0 MISMATCHED, all early self-tests passed (387).
- `src/hook/selftests_editor_shadows.cpp`: TestEditorShadowNames (TR / BO / NJ / z_dt names), TestEditorShadowPlace
  (TR's boxes: g_big_rail_shadow under SkWare_Rail01, launch ramp, Car_Shadow refused, the garage floor casts none; every
  threshold at its edge), TestEditorDropTarget.
- Mutations (`node tools/mutate.ts out/scripts/mut_shadows.ts`): 27 / 27 caught.
- `node tools/check_code_writes.ts`: No overlaps.

## In-game test checklist (for the user) - NOT run in the game yet

Turn on `debug_editor=1` for the log lines.
1. TR, level editor: click SkWare_Rail01 (the long rail near the launch ramp). The hover outline and the selection
   include `g_big_rail_shadow`; the log has `shadow by place: g_big_rail_shadow ... of SkWare_Rail01`. Drag it: the
   shadow moves under it. Z / X turn, [ / ] scale, Home: the shadow follows each time. Ctrl+Z: back together.
2. TR: click SkWare_Rail02 - `SkWare_Rail02_Shadow` joins by name; move it, the shadow follows.
3. TR: a folding chair (WH_TB_Foldingchair_01) and a skateboard: their shadows come along.
4. The arrows / pivot sit on the rail, not shifted toward the shadow; G drops the rail onto the floor (not by the
   shadow's height).
5. Save (Ctrl+S), reload the edited level: the rail and its shadow are where you left them.
6. Ctrl+D a rail with its shadow: the copy has a shadow copy; clicking the copy later selects its shadow too.
7. A group of several pieces: Ctrl+click one piece - only it leaves the selection (message "Removed ... (n left)"),
   no drag starts. Ctrl+click the selected shadow on the floor - it leaves. Ctrl+click an unselected piece - a plain
   click (selects it).
8. With the UI, Ctrl+click a carried rail point's gizmo: "Rail point ... no longer moves with the selection"; moving
   the group leaves that point behind.
9. Inspector "Parts": every piece with its size, shadows marked "shadow", the rail points; x removes one, Keep only
   this leaves just that piece (or selects the rail point by itself).
10. Alt+click (and middle click) on a selected piece: it leaves the selection; on an unselected one: it's added.
11. F1: the new rows (Ctrl+click, Shadows) read well; the old HUD (`editor_ui=0`): its key line shows
    "Ctrl+click remove one" with scenery selected, and its F1 panel's new line fits.
12. BO: a street light or a trash can - its shadow comes along; a building's or a tree's big shadow doesn't jump to
    something nearby.

## Left / notes

- Shadows draped over uneven ground (taller than 16) or with names that name no sector join only if named for a
  member; widen kFlat / the rules if the user finds misses (BO's tree shadows, NJ's telephone-pole shadows).
- A piece once taken as a shadow stays marked as one for the session (pivot / rails leave it out when selected with
  other pieces).
- Removing parts isn't an undo step (selection changes aren't); Inspector's list shows the scenery selection only
  (objects already have Shift+click).
- Switch-off: the work is editor-only (no REPLACE lines); reverting the branch removes it.
