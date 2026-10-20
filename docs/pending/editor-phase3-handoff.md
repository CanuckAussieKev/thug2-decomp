# Level editor phase 3 (create and delete point nodes): handoff

Branch `worktree-agent-ac73f9426e90a064c`. Stopped at the lead's request (account switch) **before any code was
written**: this note holds the research and the design. Nothing is half-done in the source; the tree builds as main
does (a baseline verify build passed, `out/build/verify`, PDB endpoint `editorcreate`).

## Done
- Read CLAUDE.md, docs/agent-rules.md, docs/HANDOFF.md, docs/pending/editor-ui.md (phases 0-2 and its "Notes for
  phase 3"), the plan page (scratchpad editor-plan/level-editor-plan.html: phase 3, "How to build it", the v2 format
  sketch, the mockup's "Place: what to add" palette and the confirm dialog).
- Read the editor sources: level_editor.cpp (selection, undo steps, Delete / Home / Duplicate, gizmos, Update),
  level_editor_ui.{h,cpp} (Model, Request, Inspector, Outliner, windows), level_editor_nodes.{h,cpp},
  level_editor_node_edit.{h,cpp}, level_editor_layout.{h,cpp}.
- Decompiled (own snapshot copy in the session scratchpad, `ghidra-editorcreate`; helper scripts gh.sh / xr.sh /
  xrefs.py there) and checked the findings below.

## Findings (the engine)
- **Registering a node array:** `CNodeManager::AddNodeArray(name, flag)` (0x0045D520, ours in
  src/game/script/node_manager.cpp) takes the global array symbol `name` (`Script::GetArray`: a symbol of type
  ESYMBOLTYPE_ARRAY whose `raw` is the CArray*, as `CNodeArrayAsset::CreateNodeArray` 0x004090D0 makes one with
  `CreateSymbol`), puts it in `m_arrays[m_count++]`, hashes its nodes (slots hold array index + node index), adds its
  name prefixes, turns position= into pos= (z negated), resolves links= from indices to names within the array. `flag`
  (the level's owned array only) runs 0x004696C0 on 0x004694E0's object (walks every node, numbers chains with
  component 0x9C91C3CA) - pass false for the overlay. `RemoveNodeArray` (0x0045D1C0) unhashes and moves the LAST array
  into the removed slot.
- **ParseNodeArray (0x005A34E0) doesn't parse the last array:** it parses the node manager OWNER's array
  (0x0045C220 = `GetOwnerValue58`: the owner is the level's `CNodeArrayAsset`, `MakeNodeArrayOwner` 0x00408BF0, +0x58 =
  m_nodes). So an overlay array added later is never parsed by the game - the editor must do per class what
  ParseNodeArray does: ProximNode -> `Proxim_AddNode(name, CreatedAtStart)` (0x0046B1F0), EmitterObject ->
  `Emitter_AddNode` (0x00468450; needs the table 0x006F2260), LevelLight -> 0x00490570(struct) (second loop),
  CreatedAtStart objects -> 0x005A2E50 (not needed for the kinds planned). At the next level load ParseNodeArray clears
  proxim / emitter / light lists (0x0046AB30, 0x004683B0, 0x0048EF00, 0x0048FDA0).
- **GetLastNodeArray (0x0045D120) / GetLastNodeArrayName (0x0045D150) callers:** 0x00594FD0 (park editor),
  CFunc_ReLoadNodeArray 0x005A39F0, CFunc_LoadNodeArray, and 0x004E8F50 (park editor: appends nodes to the owner's
  array but hashes them with `HashNodeIn(GetLastNodeArrayName(), ...)`). With an overlay array last, that would hash
  park nodes under the overlay's index. **Plan:** a NOT ORIGINAL tweak in node_manager.cpp - GetLastNodeArray / Name
  skip the editor's overlay array (a name set by the editor; nothing changes while it's unset, so the CNodeManager
  self-test in selftests_cobj.cpp still compares equal).
- **The overlay must leave with its level:** the level's array is removed by `CNodeArrayAsset::Unload` (0x00408F00 ->
  RemoveNodeArray). If the overlay stayed, the next level's array would be added AFTER it and every walk (restarts,
  crowns) would meet the stale overlay nodes first. **Plan:** a NOT ORIGINAL hook in `CNodeManager::RemoveNodeArray`
  (a function pointer, null by default) the editor sets; when the level array it was attached after goes, the overlay
  is removed (RemoveNodeArray of its own name), its structures freed, the added / hidden lists cleared.
- **Walks:** `CFunc_SpawnCrown` (0x00538170, not rewritten) walks every array (StartWalk / NextNode) and counts nodes
  whose **type** is Crown (any class) into two unchecked 10-entry stack arrays; crown = `net+0x68 % count` (count 0 =
  division by zero). `Obj::GetRestartNodes` (0x0046B6C0, ours, proxim.cpp) walks every array, class Restart; with a
  type it adds a name per matching restart_types element; callers use a 256-name buffer. Walk order = array order then
  index, so an overlay after the level's keeps the level's crown and restart numbers.
- **The kill path 0x005A32C0 by class:** ProximNode -> Proxim_SetActive(name, 0); EmitterObject ->
  Emitter_SetActive(name, 0); LevelLight -> 0x0048FCA0(name) (removes from both light lists); RailNode / ClimbingNode ->
  rail SetActive; LevelGeometry -> sector off; LevelObject / GameObject / Pedestrian / Vehicle / ParticleObject ->
  objects of that name killed (needs a script). **No case for Restart or GenericNode** (crowns): nothing to hide them.
- **CreateFromStructure 0x005A2E50 returns 0 for an unknown class** (nothing made). So "hidden" can be: the node's
  class and type replaced by a marker checksum (e.g. CRC "ed_hidden"), the originals kept in components
  `ed_hidden_class` / `ed_hidden_type` on the node itself. Restart walks skip it (class), SpawnCrown skips it (type),
  Create / Kill do nothing; for proxim / emitter also SetActive(0) (keep the previous m_active to restore), for lights
  also 0x0048FCA0 (unhide: 0x00490570(struct) again - the cutscenes' pair, cutscene_data.cpp). A live flag object:
  MarkAsDead. Unhide restores class / type.
- **Consequence for crown order (the mockup's dialog):** a hidden crown is no longer counted by the game, so the crowns
  after it each move up one number (their relative order is kept). Keeping "#2 stays #2" while not spawning there
  would need SpawnCrown rewritten. Suggested dialog text: "It will be hidden, not removed, so the crowns after it keep
  their order (each moves up one number while it's hidden). Home brings it back." Hidden restarts likewise leave the
  restart order. Guard: refuse hiding the last counted crown (SpawnCrown's % 0) and the last Player1 / Multiplayer
  restart.
- **Limits:** crowns 10 (count type=Crown, not hidden, all arrays); restarts 256 (class Restart, not hidden); lights:
  0x00490570 adds a dynamic light unless `ExcludeSkater` (0xE1509E4F) via 0x0048FB80 (max 0x100, count 0x006FAEF0) and
  a level light unless `ExcludeLevel` (0x6F050E18) via 0x00490400 (max 0x96, count 0x006FAEF4) - both silently refuse
  when full (no corruption); the editor should refuse at 256 / 150 with a message. Defaults read by 0x00490570:
  Brightness 100, InnerRadius 300, OuterRadius 300, Color [r g b] (default 0xFFFFFF), ExcludeGeometry (0xAF3F7288).
- **Node forms (THUG2's, as the converters make them):** multiplayer restart: Class Restart, Type MultiPlayer,
  CreatedAtStart, RestartName "Multi: Restart", restart_types [MultiPlayer Horse] (thps4_levels.ts 200-206,
  thps3 compat). Team / CTF restart: name TRG_CTF_Restart_<Colour> (found BY NAME, 0x00566100), Type CTF, CreatedAtStart,
  RestartName "Team: CTF <Colour>", restart_types [CTF MultiPlayer Horse]. Crown: Class GenericNode, Type Crown,
  CreatedAtStart (thps4_levels.ts 210-219). CTF flag set (Airport form, thps3_levels.ts ctfNodes): TRG_CTF_<Colour>
  GameObject Type Flag_<Colour>, NeverSuspend, Model "gameobjects\flags\flag_<colour>\flag_<colour>.mdl", lod_dist1 800,
  lod_dist2 801, TriggerScript TRG_CTF_<Colour>Script (a level script `Team_Flag <colour>` - the level may not have it;
  flags are only made in net games, where layouts never apply, so this matters for the bake); its base
  TRG_CTF_<Colour>_Base, Type Flag_<Colour>_Base, model flag_<colour>_base, no script. Flags and team restarts are
  name-driven: they must use these exact names (refuse when the level already has that name); other kinds get fresh
  `ed_<kind>NN` names. LevelLight: Pos, Brightness, InnerRadius, OuterRadius, Color. ProximNode / EmitterObject:
  Shape Sphere (0xAA069978), Radius, CreatedAtStart. A sound emitter can be made at run time (Emitter_AddNode), but it
  is silent until a script plays a sound on it - list "a sound to play" as later.
- Proxim / emitter nodes can't be freed singly (no remove function): deleting an added one sets it inactive; making it
  again (undo) reuses it (update m_pos / radius, SetActive) instead of adding a second.
- Memory: Proxim_AddNode / Emitter_AddNode use Mem::Calloc on the current heap; Manager::Free works from the block
  header, so their later cleanup is safe. Node structures / arrays: Script::NewStruct / NewArray pools; free added
  structures with Clear + DeleteStruct only after the array is re-synced (nothing in the game keeps node pointers for
  these kinds).
- Names for labels: `Script::NoteQbName(checksum, text)` (qb.h, ours) makes Objects::NameOf know an added name.

## Design (what to build next, in order)
1. `src/game/script/node_manager.{h,cpp}`: the overlay-skip in GetLastNodeArray / GetLastNodeArrayName and the
   RemoveNodeArray hook (both NOT ORIGINAL, null / 0 by default). Check `node tools/check_code_writes.ts` and that
   TestCompositeObjects still passes.
2. New `src/game/front/level_editor_node_create.{h,cpp}` (namespace `NodeCreate`), in CMakeLists next to
   level_editor_node_edit.cpp:
   - pure: `Kind` {Spawn, TeamSpawn, Crown, FlagSet, Light, Sound, Proxim}, `Team` {Red, Blue, Green, Yellow};
     `Spec` {name, cls, type, pos, angles, restartTypes, colour[3], brightness, inner, outer, radius};
     `Default(kind, team, pos, yaw)`; `Fields(spec)` (the full node: derived RestartName, CreatedAtStart, Model...);
     `FreshName(stem, taken)` ("ed_restart01".."99", then 100+); `Counts` + `LimitMessage(kind, counts)`;
     `HideLeavesNone(...)`; list ops `Add / Take / PutBack` and crown reorder `MoveCrown(list, name, delta)`,
     `CrownOrder`, `ApplyCrownOrder(list, names)` (crowns fill the crown slots in the given order, unlisted after).
   - game: one global array symbol (e.g. "thug2_decomp_editor_nodes", made once with CreateSymbol and reused), a
     `Sync()` that RemoveNodeArray's it, SetSizeAndType + SetElement with the structures, AddNodeArray(name, false)
     after the level's (remember the level array name for the hook); `Create(spec, index)` (structure from Fields, sync,
     live: proxim / emitter / light), `Remove(name)` (spec with its current place returned, live undo, NodeEdit forgets
     it, sync, free), `Hide(name, on)` (marker as above), `MoveCrown`, `Order / SetOrder`, `CountsNow`, `ResetAll`,
     `ForgetLevel`, `IsAdded`, `Hidden`, `HiddenNames`.
3. `level_editor_nodes`: Node gets `added` (array is the overlay) and `hidden` (class marker; cls / type read from
   ed_hidden_*); crown / spawn order skips hidden; `Walked(family)` counts; a generation counter (the UI's caches key on
   it, not on data()/size()); `Invalidate()` after hide.
4. `level_editor.cpp`: undo kinds kAdded (specs + indices + exist), kHidden (names + state), kOrder (overlay order);
   N toggles the Place palette; an armed entry places at the click (CursorPoint, then the ground below as G does;
   lights raised ~150), angles (0, camera yaw, 0), selected after; Shift+click keeps placing; Esc disarms. Delete: an
   added node removed; an original asks (UI confirm) then hides; Home on a hidden node shows it again. Online warning
   (NoteOnlineEdit) on add / hide / reorder of spawns, crowns, flags in shared levels, same once-a-run flag. Limits
   refused with a Hud message.
5. `level_editor_ui`: toolbar "Place (N)"; the palette window (search, entries greyed with the reason when a limit or a
   taken name stops them); Inspector: "Added" badge + Delete, "Hide" for originals, hidden note + "Show again (Home)",
   crowns "#n of N" with Move up / down for added crowns only (the level's crowns keep the level's order - the choice
   to document); the confirm window; hidden gizmos greyed; Outliner tags "added" / "hidden". Only ImGui's API there.
6. `level_editor_layout`: File gets `added` (Specs), `hidden` (checksums), `crownOrder` (names); records
   `node <name> <Class> type=<T> pos=x,y,z angles=x,y,z [types=A,B] [color=r,g,b] [brightness=] [inner=] [outer=]
   [radius=]`, `hide <hex>  # name`, `order crown <name> <name>...`; unknown keys skipped; version stays 2 (phase-2
   readers skip the new records). Capture writes added nodes as node records (not as set records), Apply: node records
   (one sync), order, hide, then set. ResetToLevel: NodeEdit::ResetAll then NodeCreate::ResetAll. Never in net games
   (Tick already checks).
7. Self-tests `src/hook/selftests_editor_create.cpp` (in CMakeLists): FreshName, Fields per kind, the record round trip
   (node / hide / order, old v2 and v1 files still read), limits, HideLeavesNone, MoveCrown / ApplyCrownOrder, undo of
   create / delete through Add / Take / PutBack; optionally a node-manager test of the skip and the hook on a manager in
   test memory (0-node arrays; check RemoveNodeArrayPrefixes copes offline - NodePrefixes() may be null early). Prove
   with `node tools/mutate.ts`.
8. Docs: level_editor.h header comment; a phase 3 section in docs/pending/editor-ui.md (overlay array, live vs reload,
   limits, format, the in-game checklist the lead listed).

## Files touched
- Only this note. No source changes yet.

## Commands to check
- Build: `C:\Users\kevin\AppData\Local\Temp\claude\C--Dev-thug2-decomp\c861af32-ae53-4deb-9c82-1d00b291c5a6\scratchpad\build.cmd`
  (VsDevCmd x86, `_MSPDBSRV_ENDPOINT_=editorcreate`, `cmake --build out\build\verify -j 2`), or `tools\verify_build.cmd`.
- `out\build\verify\thug2_check.exe` (THUG2PM.exe default) and `out\build\verify\thug2_check.exe C:\Games\THUG2\Game\THUG2.exe`.
- `node tools/check_code_writes.ts`; `node tools/mutate.ts` for the new tests.
- Don't launch the game (the lead's instruction for this task).
