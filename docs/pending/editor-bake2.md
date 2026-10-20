# apply_layout.ts: triggers and level properties baked (the editor plan's bake, part 2)

Branch `worktree-agent-a07f0217faa1c8730` (off main b853d0d). TypeScript only - no DLL change. **NOT run in the game
yet.** Checked offline: `npm run typecheck / lint / test` (118 tests pass, 9 new), and
`out\scripts\verify_bake2.ts` in the worktree on the game's own files (read only; output under the worktree's `out\`).

## What bakes now (tools/game/apply_layout.ts)

Everything phase 6 baked, plus the two missing record kinds:

### `trigger` records (phase 5) - tools/game/triggers.ts
The DLL makes an editor trigger at run time out of three things; the bake writes the same three into the level's files:
- **The node** - appended to the node array, as the DLL's overlay node (Triggers::NodeSpec -> NodeCreate::Fields):
  `Pos`, `Angles (0, yaw, 0)`, `Name`, `Class=GenericNode`, `Type=ed_trigger`, `TriggerScript=<name>Script`. The
  level's nodes keep their indexes and bytes (the phase 6 splice).
- **The box** - a collision object named after the node in the `.col.xbx` (tools/level_port/thug2_col.ts's writer, as
  the converters use): 8 corners, 12 triangles, centred on pos, turned by yaw, the size clamped 10..50000 as the DLL.
  Corner order, face order and face flags are **THUG2's own trigger boxes'** - checked against Boston's
  `TRGP_ShoppingCart` byte for byte (a test, when the game is installed): every face `0x1150` = trigger 0x40 +
  non-collidable 0x10 (what the trigger component's feeler asks for: SetIgnore(0, 0x50)) + no skater shadow 0x100 +
  invisible 0x1000; terrain 0; intensities 128; fixed-point corners while the box fits 16-bit sixteenths (as the
  shipped boxes), floats when bigger.
- **The script** - `script <name>Script ... endscript`, the **DLL's own bytes**: `Triggers::Body` / `QbFile` ported
  (kill: SK3_KillSkater / SK3_KillSkater_Water nodename=; teleport: TeleportSkaterToNode nodename=; script: a call of the
  chosen script; gap start: StartGap GapID= flags=[...]; gap end: EndGap GapID= score= text=). Tests compare with the
  DLL self-test's expected bytes and with Boston's own `BO_Gap_StateHouseSpine1` (StartGap + EndGap: identical). A
  trigger that lacks something (no restart, no script, a gap end without name / score) gets an empty script, as in the
  DLL, and a warning. Written into **the level's scripts .qb** (`<level>_scripts.qb` - THUG2's own levels, Levels.qb's
  `level_qb`; found in the folder the level is read from, else Data\levels\<level>), else (the ported levels, whose
  scripts are in their node array's file) into `<level>.qb`. New scripts go before the file's name table; a script of
  that name already there (a layout baked twice) gets the new body in place; names added to the name table (the
  scripts', targets', gap ids, gap flags).
- **Gaps:** nothing else needed - the gap checklist is built at the end of CFunc_ParseNodeArray from every EndGap a
  node's TriggerScript reaches; the baked gap end's node / script are what it reads (exactly the level's own gaps'
  form). A gap end with no start (or the reverse) is warned about.
- **Level triggers switched off / replaced:** a trigger's `replaces=<checksum>` now leaves that level trigger's
  collision object out (the DLL switches it off while the copy is there); a `wall` record's off does too (phase 6
  already). The phase 6 "kept on - not baked" exception is gone.
- Record reading: `tools/game/layout_file.ts` now reads trigger records with the DLL's `Triggers::ReadRecord`
  (ported): a record the DLL refuses (kind's case, `other`, a repeated key, a bad value, text= not last, no pos) makes
  the layout refused, as the DLL refuses the file. A trigger named like a level node / collision object (e.g. baking
  onto an already-baked level) isn't added (the DLL skips "there already" too).

### `level` records (level properties) - Levels.qb
Written into the level's structure in **the overlay's `scripts\game\Levels.qb`** (`Level_<level>`, else the
structure(s) whose `level=` is the level's name), only with `--install` (the workspace overlay's own copy, changed in
place - and `anteara\scripts\game\Levels.qb` too when it exists) or into the bake output
(`out\baked\<level>\scripts\game\Levels.qb`, made from the overlay's copy, else the game's loose
Data\scripts\game\Levels.qb). Every other byte of Levels.qb kept: an existing key's **last** occurrence (the one the
game's Search finds - Levels.qb repeats some, e.g. Level_alc's ambient_red) gets the new value in place, a missing key
is added at the structure's end; new names into the name table.

| layout `level` key | Levels.qb key(s) | as the DLL |
|---|---|---|
| `tod=` | `tod_state` = morning / afternoon / evening / night (a name) | LevelProps::StructureValues |
| `time=` | `tod_manager_active` = 0 / 1 | same |
| `ambient=r,g,b` | `ambient_red`, `ambient_green`, `ambient_blue` (0..255) | same |
| `fog=r,g,b,a,d` | `fog_red`, `fog_green`, `fog_blue`, `fog_alpha` (0..255), `fog_dist` (0..100000) | same |
| `name=` | `name` (a string) | the DLL only uses it for the EDITED LEVELS entry's text; the level select's own sign text is gamemenu_levelselect.qb's and is not changed |
| `start=` | **not baked** - no Levels.qb key (the DLL puts the skater there with ResetSkaters once the edited copy is up); reported | - |

### Byte-for-byte safety (verify_bake2.ts on C:\Games\THUG2\Game, read only)
- All 77 scripts files (THUG2's `<level>_scripts.qb` and the installed ported levels' `.qb`): adding nothing gives the
  same bytes; adding two trigger scripts keeps every script the file had byte for byte, the globals unchanged; ns.exe
  decompiles 73 of them - the other 4 (CA_Scripts, LA_Scripts, thps3_skateshop, z_el) ns.exe can't decompile as
  shipped either.
- The overlay's Levels.qb: all 74 level structures given every property in turn - the values read back, every global
  still there, the bytes before the structure unchanged; ns.exe decompiles the result.
- End to end: `apply_layout.ts` on Training (THUG2's own: scripts into TR_Scripts.qb) and Alcatraz (ported: scripts into
  alc.qb) with four triggers and every level property: the .qb / scripts .qb / Levels.qb decompile with ns.exe (the
  scripts read `sk3_killskater nodename=TRG_Ware_Restart01`, `StartGap GapID=ed_gap01 flags=[CANCEL_GROUND]`,
  `EndGap GapID=ed_gap01 score=150 text="Baked gap"`; the nodes `Class=GenericNode Type=ed_trigger
  TriggerScript=ed_trigger01Script`), the .col reads back.
- Node array: unchanged nodes keep their bytes (phase 6's round trip, unchanged).

## Tests
- `tools/game/triggers.test.ts` (new): scripts = the DLL self-test's bytes (Boston's StateHouseSpine1 / 2, HighSlap,
  bare StartGap, kill / water / teleport / a hex-named script); empty scripts for what Problem names; the DLL's .qb
  image; records read / refused as ReadRecord; the node's fields; the box (12 faces 0x1150, a quarter turn, fixed /
  float corners through writeCol / readCol); qb_write's putScripts (added / replaced, bytes kept) and setStructEntries
  (last of a repeated key, added keys, other structures' bytes); with the game installed: Boston's own gap script and
  TRGP_ShoppingCart's box identical.
- `tools/game/apply_layout.test.ts`: the trigger records now lowercase (as the DLL writes and requires); a new bake test
  (6 triggers + one named like a level node: nodes, boxes, the replaced trigger out, scripts in the scripts .qb with its
  own bytes kept, empty script + warnings, lone gap warning, Levels.qb values clamped as the DLL, other structures'
  bytes kept, start reported, baked twice over its own output: nothing doubled, Levels.qb the same bytes).

## Shared files touched (additive)
- `tools/level_port/qb_write.ts`: `addNames` (factored out of replaceGlobalArray - same behaviour), `globalStructSpan`,
  `setStructEntries`, `scriptSpans`, `putScripts`.
- `tools/game/layout_file.ts`: trigger records carry `spec` (read by triggers.ts), refused as the DLL refuses them.

## Still not bakeable
- **The level's start restart** (`level start=`): no key in the level structure; the DLL moves the skater after the
  load. Reported, not baked.
- **The DLL's box fires on the way in only** (a movable box is hit where a line enters it); a baked box is static level
  collision like THUG2's own boxes, whose faces are met on the way in **and on the way out**: a kill zone / teleport /
  gap end doesn't care (the skater is gone, the gap ended), but a "Run a script" trigger runs its script again on the
  way out, and a gap start restarts its gap when left.
- The node is the DLL's (`GenericNode`, `ed_trigger`), not a level geometry node like THUG2's own trigger sectors; the
  editor shows a baked one as a level trigger (its Triggers window / Inspector read the level's 0x40 faces). Moving the
  baked node in the editor moves the node, not its collision (move it as a level trigger / invisible wall instead).
- The career save: baked gap ends are the level's own gaps now - they go into that level's career gap list like any.
- Whether THUG2's own levels read the overlay's loose `<level>.qb` / `<level>_scripts.qb` / `.col.xbx` over their pre
  archives (TR_scripts.pre / TRcol.pre) isn't known (phase 6 said so too); a ported level surely does. Levels.qb in the
  overlay is read (make_menu_level.ts's own edits rely on it).
- `make_menu_level.ts` empties the overlay and makes it again: an installed bake (level files and the Levels.qb
  values) is gone after it - bake again.
- After installing a bake, the level's saved layout still exists: its (EDITED) entry applies the layout on top of the
  baked level (the DLL skips triggers whose nodes are there already, but moves / copies would apply twice) - move the
  layout file away once it's baked.

## Checklist for the user (NOT run in the game)
Use a workspace (C:\Games\THUG2-n) with the overlay made (`node tools/menu_level/make_menu_level.ts --game <folder>`)
and a saved layout with triggers and level properties (e.g. Training: add a kill zone, a teleport, a gap, a Run-a-script
copy of a level trigger; set Time of day night, Time passes no, Ambient and Fog; Ctrl+S).
1. `node tools/game/apply_layout.ts --level TR --game C:\Games\THUG2-n --dry-run`: the report lists each trigger
   ("trigger ed_trigger01: kill zone -> ... at ..."), "Levels.qb: Level_TR tod_state=night ...", the start warning if
   a start was set, the THUGPRO parity lines; no "unsupported record \"trigger\"".
2. Without `--dry-run`: `out\baked\TR\levels\TR\` has TR.qb, TR_Scripts.qb, TR.col.xbx, TR.scn.xbx and
   `out\baked\TR\scripts\game\Levels.qb`; `out\neverscript\ns.exe -d` on TR_Scripts.qb shows `script ed_trigger01Script
   { sk3_killskater nodename=... }` etc. at its end.
3. Move the layout file (`thug2-decomp-layouts\4B7E41E2.txt`) out of the workspace's layouts folder, then
   `--game C:\Games\THUG2-n --install`: "written: ...\thug2-decomp-data\scripts\game\Levels.qb" and the level folder.
4. Load Training **as shipped** (the THUG2 section): it's night, time stands still, ambient / fog as set (judge by eye)
   - the level's structure now has them for every load.
5. Skate into the baked kill zone: the bail / "out of bounds" message and back at its restart; the teleport: "On the
   Move!" and at its target; the gap: ollie through the start, land past the end - "Editor Gap 1" awarded.
6. Pause > gap list (VIEW GAPS): "Editor Gap 1" (100) is listed with the level's own gaps. (Don't save the career if
   you don't want it kept - it's a level gap now.)
7. The level trigger the Run-a-script copy replaced: nothing fires at its old place; the copy fires where it stands
   (and again when you leave its box - see "Still not bakeable").
8. Log: no `EXCEPTION`, `RUN-TIME CHECK`, `DEBUG CRT` while loading or firing them. If nothing of the triggers works
   but the time of day does, the game didn't read the overlay's TR files over its pre archives - try a ported level
   (e.g. Alcatraz `alc`) the same way.
9. LEVEL EDITOR in the baked level, T: the baked triggers are drawn as the level's own (kill red, teleport purple, gap
   green / yellow) with their labels; Switch off works on them.
10. `make_menu_level.ts --game <folder>` again: the baked files and Levels.qb values are gone (the level as shipped).
