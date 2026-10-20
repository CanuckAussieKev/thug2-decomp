# Level editor phase 6: full Inspector, level properties, apply_layout.ts bake

Branch `worktree-agent-ad02377db8580f8b0` (merged main at addd72c: walls, phase 5 triggers, shadows, shared collision). **NOT run in the
game yet** (editor UI work: the user tests it). Checked offline: build (out\build\verify), thug2_check on THUG2PM.exe
and the stock C:\Games\THUG2\Game\THUG2.exe (406 early self-tests passed on both), `check_code_writes` "No
overlaps", `npm run typecheck / lint / test` (109 tests pass), 10 + 2 mutations of the new self-tests all caught
(out\scripts\mut_phase6.ts in the worktree), apply_layout dry runs and a real bake of Training (TR) into out\ (its qb
decompiles with ns.exe; its scn / col read back).

## What it does

### Full Inspector (src/game/front/level_editor_fields.{h,cpp}, level_editor_fields_ui.cpp)
- **All fields** of a selected node (a node by itself, or an object's / scenery's own node): a table of every field
  - its own and its class's shared structure's (ncomp_..., greyed) - each with a typed editor: integer, float, name
  (checksum names shown by the game's name table, `#XXXXXXXX` when unknown, a note when a typed name isn't one the
  game knows), string, local string, vector, pair, flag (checkbox), arrays of names / integers / floats. Typed text
  is checked as it's typed (red: why not), Enter or leaving the box sets it. Links: names of existing nodes only, not
  itself, none twice.
- Each row says **when the game sees it**: `live` (restart_types of a restart - read at every respawn; Links - through
  the chains' `links` records, a rail / path / ped / car linked again at once), `on reload` (everything else: written
  into the node now, saved, and written into the node again **as the edited copy's node array loads** - a new
  `SkateScript::SetNodeArrayAddedHook` at the end of CNodeManager::AddNodeArray, before ParseNodeArray makes the
  level's objects), `above` (pos / angles: the Position / Heading fields; a rail's TerrainType: the chain section's
  Terrain), `read-only` (name / Class / Type - identity; structures and scripts; every field of a node the editor
  added - its fields come from its kind).
- **Shared structure fields**: CStruct::Search / FindLastComponent take the last one (0x004764B0 / 0x00476430), so a
  value typed over a shared field becomes the node's **own override** (a component after the include) - the shared
  structure isn't touched. A shared structure's **flag** can't be switched off for one node: read-only, said so.
- Per field: **Home** (back as the level has it), **x** (take an own field out; an override out = the shared value
  counts again), **Put back every field**; **Add a field** (name, type, value) at the foot. Each change is one undo
  step (UndoStep::kFields: the node's own component - present or not, value, its place among the components - put back
  exactly). Reset level puts every field back.
- Saved on the node's `set` record: `set <node> [pos=] [angles=] f.<field>=<value>...` (format in
  level_editor_fields.h: `i: f: n: s: l: v: p: flag: an: ai: af:`, `del`; names as text, else 0x+hex - '#' is the
  layout's comment). Older DLLs skip f. keys. Version stays 2.

### Level properties (src/game/front/level_editor_level_props.{h,cpp}; the Inspector's foot, "Level properties")
- **Name** (the EDITED LEVELS entry reads "<name> (EDITED)"; the toolbar shows it), **Start** (one of its restarts:
  the skater is put there - ResetSkaters node_name=, as F8 - once the edited copy is up), **Time of day** (shown at
  once via Gfx::CTODManager::SetTODInstant; load_level reads tod_state), **Time passes** (tod_manager_active),
  **Ambient** (ambient_red/_green/_blue) and **Fog** (fog_red/_green/_blue/_alpha/_dist) overrides. One undo step a
  change (UndoStep::kProps).
- Applied only to the edited copy: LevelEditor_PickLevel (the level select chose "(EDITED)") writes them into the
  level's structure (Level_<x>, included by load_level) before it loads; once the level is up (Layout::Tick) the
  structure is put back - any other load of the level never sees them; never in a network session. Opened with -edit
  or the pause menu, only the time of day shows at once; the rest on the next load from the level select.
- Saved as a `level` record: `level [name=] [start=] [tod=] [time=] [ambient=r,g,b] [fog=r,g,b,a,d]`.
- **Not verified in code**: which engine code reads the level structure's ambient_* / fog_* keys (Levels.qb sets them
  for every level and the converters write them; the readers weren't traced) - the user's test decides.

### tools/game/apply_layout.ts (+ layout_file.ts, tools/level_port/qb_write.ts)
- Reads a layout and the level's loose files (overlay first, then Data\levels), writes a baked copy (out\baked\<level>
  by default; `--install` into a workspace's overlay; the main install only with `--allow-main`; the backups and
  THUGPRO folders never), `--dry-run` reports only. The node array is spliced back into the .qb byte for byte for every
  node not changed (all 93 installed levels' node arrays round-trip byte-identical); new names go into its name table.
- Every record kind: sector (move / turn / scale, hidden = left out of scn + col), clone (a new sector + collision),
  rail (pos; hidden = marker + links cut), object / copy (pos, heading), set (pos / angles / fields, overrides as the
  DLL), node / point / order (appended - indexes kept, crowns in order, a point copies its `like` node as the level
  has it), hide / drop (the DLL's ed_hidden marker), links (names -> indexes), terrain, wall (collision moved / off),
  lock (ignored), level (listed: Levels.qb, not the level's files), trigger (phase 5: read, **not baked yet** - listed
  with what each does; a level trigger it replaces is kept on), anything else listed as unsupported.
- **Shared collision** (main's level_editor_shared_coll.h, ported): a moved / hidden / copied piece takes the faces of
  collision-only objects that belong to it (same rules: collidable non-trigger faces in its box grown 6, named
  `<name>_col / _ic / _col01 / _ic01` or within 6 of its triangles and strictly the nearest solid piece; see-through =
  every mesh's material's first pass transparent - an approximation of the engine's material flag 0x40): the originals
  get face flag 0x10, a companion object `ed_coll_<piece>_<n>` holds them placed with the piece. TR's SkWare_Rail01
  moved: 16 faces taken from SkWare_Main_1, as the DLL's header describes.
- **THUGPRO parity**: a level in tools/compat/thugpro's register, or THUG2's own, gets "this breaks online parity" for
  every edit of spawns, crowns, flags / CTF / team nodes, gaps / triggers, geometry (scenery, collision, rails).
- Tests: tools/game/apply_layout.test.ts (a small level made in the test; every record kind).

## Shared files touched
src/game/script/node_manager.{h,cpp} (the added-array hook, additive), level_editor_layout.{h,cpp} (set fields, level
record, the hook, PickLevel / Tick / EditedLevels / LevelTitle), level_editor.cpp (undo kinds, requests, model),
level_editor_ui.{h,cpp} (the old read-only field table replaced by FieldsSection; LevelSection), CMakeLists.txt.
Switch off: `[replacements]` doesn't apply (not a REPLACE); the editor as a whole: `[features] level_editor=0`.

## In-game checklist (NOT run)
1. Training (or any level): LEVEL EDITOR, select a restart in the Outliner. The Inspector's **All fields** lists its
   fields; RestartName / CreatedAtStart from `ncomp_...` are greyed; the column says live / on reload / above /
   read-only (hover: why).
2. Type a new RestartName (Enter): the row turns orange (edited), the status line says "on reload". Ctrl+Z puts it
   back, Ctrl+Y again; its **Home** button puts it back.
3. Edit **restart_types** to `Player1, Multiplayer` (live): leave the editor, press F8 until that restart - it works
   for both. Type `Nonsense9!` - the red error shows and nothing is set.
4. Select a rail point: edit **Links** to the name of another rail point (live) - the rail is drawn relinked at once;
   type a name that isn't a node - refused in red. Home puts the links back.
5. On a light / proximity node, change a float (OuterRadius / Radius) and **Add a field** (e.g. `MyFlag`, flag, on).
   Ctrl+S. Leave the level, load "<LEVEL> (EDITED)" from the level select: the fields are there (the log says
   "N field edits written into its nodes before they're made"); the level as shipped doesn't have them.
6. A greyed shared flag (CreatedAtStart on a ncomp node) is read-only with the explanation; a shared value typed in
   shows non-grey (override); **x** removes the override and the shared value comes back.
7. **Level properties** (Inspector foot): set Name "My TR", Start a restart, Time of day night (the sky changes at
   once), Time passes no, Ambient and Fog overrides. Ctrl+S. Ctrl+Z / Ctrl+Y step through the changes.
8. Main menu -> level select -> EDITED LEVELS: the entry reads "My TR (EDITED)". Load it: the skater starts at the
   chosen restart, it's night, ambient / fog as set (judge visually). Then load the level as shipped: its own time of
   day, ambient, fog, start.
9. Reset level (Ctrl+Shift+Delete): every field and property back; Ctrl+Z brings them back.
10. A network game / online: no EDITED entry, nothing applied (as before).
11. `node tools/game/apply_layout.ts --level TR --dry-run` (with a saved TR layout): the report lists the edits, the
    level properties not baked and the parity warnings ("THUG2's own level"). Without --dry-run it writes
    out\baked\TR\levels\TR\ - to try one in a workspace: `--game C:\Games\THUG2-n --install` (whether THUG2's own
    levels read the overlay's loose files over their pre archives isn't known - a ported level surely does).
