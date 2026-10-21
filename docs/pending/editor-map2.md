# editor-map2 (agent record): the feature map's living and moving things - peds and cars, model swap, uncreated ghosts, bouncy sets and physics, moving objects (speed, preview), sound emitters with sounds, particle systems, team lobby flags - NOT run in the game yet

Branch `worktree-agent-ae3e302d5d2c230d8`. Level editor / UI work: no game runs (the user tests). Verified by building
(out\build\verify in the worktree), thug2_check.exe on THUG2PM.exe and on the stock C:\Games\THUG2\Game\THUG2.exe (both
pass: 426 early self-tests), `node tools/check_code_writes.ts` (No overlaps), `npm run typecheck` / `lint` / `test`
(129 pass), and mutations (below).

## What was built

New module **src/game/front/level_editor_entities.{h,cpp}** (+ `level_editor_entities_ui.cpp`, the Inspector's
sections). Small hooks: level_editor_objects.cpp (copyable classes, a particle copy's places), level_editor_node_edit.cpp
(Movable / WhenApplied defer to Entities; NodeEdit::Set tells Entities::NodeMoved), level_editor_node_create.{h,cpp}
(team lobby flags), level_editor_layout.{h,cpp} (`speed` / `emitter` records, Capture holds the preview, Apply / reset /
forget), level_editor.cpp (palette entries and PlaceEntity, undo kind kEntities, Tick, overlay, Delete of a copy selected
by its node, preview stopped before undo), level_editor_ui.cpp (Inspector hooks, View checkbox, the Place list scrolls),
CMakeLists.txt (3 files), tools/game/layout_file.ts + apply_layout.ts (+ tests).

| Row | What works | Live / on reload | Left |
|---|---|---|---|
| 1 Peds & vehicles | Place palette (N): one entry per Pedestrian / Vehicle type the level has made (class + Type + profile + model; "Ped: ... like TRG_Ped08"), placed at the click facing the camera; also the browser (B > Objects) and Ctrl+D now copy peds and cars. A copy keeps its source's node (skeleton, model, anim set, profile, AI type, TriggerScript, links = its path). Path: the existing Inspector > Path (Assign a path... / No path) works on the copy's node (chains' `links` record; the copy is made again). Delete deletes it; Ctrl+Z | live (CreateFromStructure, as the browser's copies); `copy` records on reload (made before the `links` records apply) | A copy runs its source's TriggerScript (a goal ped's script runs twice - pick generic peds); a copy starts at its node's place when remade for a path change (existing chains limit) |
| 2 Model swap | Inspector > Model (an object with model=): a combo of the models the level's live objects use (so they're loaded). A field edit (`set ... f.model=`; undo, Home, layout) | on reload: the edited copy's early field pass makes it with the model. Live: killed and made again from its node where it stands (place, turn, scale kept) once the game removes the old one - at the latest on leaving the editor. Undo / Home remake it back the same way. A copy with a swapped model is remade after its layout loads | Peds / cars use profiles (no model=): not swappable |
| 3 Show uncreated | Inspector > View "Show uncreated objects": object nodes (LevelObject, GameObject, Pedestrian, Vehicle) with no live object drawn as ghosts (a 40 x 40 x 40 box facing their heading + "name (uncreated)", nearest 200 within 4000). Their diamond selects the node; they move / turn as point nodes (`set` records); Home puts them back | live in the node: a script's `create` makes it where its node is now. Deleted copies' leftover nodes are not ghosts | not toggled from the toolbar (the Inspector's View section); not saved in the ini |
| 4 Bouncy | Inspector > "Physics (bouncy object)" for nodes with `bouncy`: coeff_restitution, coeff_friction, center_of_mass, skater_collision_* (impulse / rotation / assent / radius), SoundType editable (Enter; field edits, undo), contacts counted. Place palette "Bouncy set: <stem> (n)": every live bouncy object whose name shares the stem (BO_Bouncy_Ball01..04, up to 24) copied as the level placed them, about the click (lowest on the ground) - one undo step | physics fields on reload (the rigid body reads them at creation; a remake isn't forced); sets live (`copy` records) | contacts edited only in the fields table (an array of vectors - read-only there) |
| 5 Moving objects | Inspector > "Moving along its path" for a live object whose node's first link is a Waypoint: Path speed (checkbox + mph, 0.1-200; undo step per drag); Play / Pause / Step (half a second) / Stop - the object slides along its waypoints (first link each, loops when the path closes, wraps at the end) at the speed (set, else 25 mph cars / 3 peds / 10 others), the path drawn green; no script runs; Stop, another selection, a drag, an undo, leaving the editor put it back; a save reads where it stood | speed: live and again whenever the object is made (our script LevelEditor_PathSpeed = `Obj_SetPathVelocity <speed> mph`, defined by the game's loader from QB made in the DLL, run on the object - the game's own member function); `speed` records; a level script that sets the speed later wins | peds' animations don't play in the preview (the game is paused); scripts that set their own speed override it |
| 6 Sound emitters | Place palette "Sound emitter: <sound> (loop)" for every sound the level's wave table holds (0x006F5E68): an `ed_soundNN` EmitterObject (sphere, radius 300) with that sound. Inspector > Sound on any emitter (level, added): the sound (or none), Volume % (0-400), Radius (sphere emitters: its node's Radius and the emitter manager's copy). Played by the DLL as THUG2's emitter sounds are heard: from the shape's nearest point to the listener (EmitterNode::GetClosestPoint), the sound's own dropoff (CSfxManager::GetVolume), restarted when it ends, stopped out of range | live outside the editor (silent while paused, i.e. in the editor); `emitter` records; baked: ed_sound / ed_volume / Radius node fields, which the DLL reads when a level loads | box-shaped level emitters keep their sector's area; the level's own scripts may also play on its emitters (both heard) |
| 7 Particles | Particle nodes move as point nodes (drag, keys, G; `set` records): MidPosition / EndPosition (world places) shifted with them, the system made again 0.3 s after the move rests. Palette "Particles: <Texture> like <node>" copies a particle node (one per Type + Texture), its Mid / End moved with it (`copy` record); Delete on a copy selected by its node deletes it | node now; the visible system once the game removes the old one (at the latest on leaving the editor); reload exact | rotation doesn't turn the emitter box; a particle system the game doesn't register under its node's name isn't remade (check 7 below) |
| 8 Team lobby flags | Palette "Team lobby flag (Red / Blue / Green / Yellow)": `TRG_Flag_<Colour>` GameObject Type Flag_<Colour>, flag model, NeverSuspend, lod 800/801, TriggerScript `TRG_Flag_<Colour>Script` - the exact names SK4_Scripts' Create_Team_Flags creates in a lobby (NodeExists / create name=); no base (BO's / TR's have none); refused when the level has one | node now; the flag object when a lobby makes it (network only) | the level must define TRG_Flag_<Colour>Script (THUG2's levels do; a ported level may not) |

Offline only: layouts apply only offline (unchanged guard); the baked fields ed_sound / ed_path_speed are ignored while a
network session is on; the editor never starts online.

## Layout records (version 2 still; an older reader skips them)
```
speed 0000ABCD 27.125                                   a moving object's path speed, mph (0.1-200)
emitter 1234ABCD sound=DEADBEEF vol=75.5 radius=320     an emitter's sound (0: none), volume % (0-400), radius (0: own)
```
A value out of range, a node twice, a key twice refuse the file (as every record); unknown keys skipped. A lobby flag
is a `node TRG_Flag_<Colour> GameObject type=Flag_<Colour> ...` record (the name decides its script). apply_layout.ts:
`speed` -> ed_path_speed, `emitter` -> Radius / ed_sound / ed_volume (warned: only this DLL reads them); particle copies
and particle `set pos=` move MidPosition / EndPosition; lobby flags get TRG_Flag_<Colour>Script.

## Self-tests and mutations
src/hook/selftests_editor_entities.cpp (Early): TestEditorEntitySets, TestEditorEntityPaths, TestEditorEntityTypeKeys,
TestEditorEntityState, TestEditorEntityRecords, TestEditorLobbyFlag. Mutations: out\scripts\mut_map2.ts in the worktree
(tools/mutate.ts, TESTS EditorEntity / EditorLobby): **26 mutations, 26 caught** (sample-zero-length after a test was added). TS: apply_layout.test.ts "layout_file:
speed and emitter records..." and "bake: particle systems moved and copied...".

## In-game checklist (NOT run in the game yet)
Use `-edit bo` (Boston) unless said; `debug_editor=1` logs remakes and speeds.
1. **Ped:** N > search "Ped:" - one entry per type. Click one, click the ground: a ped appears facing away from the
   camera and walks its source's path (or stands). Inspector > Path > Assign a path... > a waypoint: it is made again
   and walks there (after leaving the editor at the latest). Delete removes it; Ctrl+Z / Ctrl+Y. Report any crash
   (copying peds is new ground; a goal ped's copy runs its goal script - avoid TRG_Ped_Hawk).
2. **Car:** N > "Car: Duck ..." - place on a street; it follows its source's path after leaving the editor.
3. **Save / reload:** Ctrl+S, load BOSTON (EDITED): the ped / car copies are there, with their assigned paths.
4. **Model swap:** select a trash can (LevelObject with a model). Inspector > Model > pick a cone / chair. On leaving the
   editor it becomes that model where it stood; Ctrl+Z (in the editor) and leaving: back. Save, reload: the new model.
5. **Uncreated:** Inspector > View > Show uncreated objects: grey boxes with "(uncreated)" labels (e.g. TRG_SKATE_S
   letters, goal peds). Drag one; save; play the goal / mode that creates it: it appears at the new place.
6. **Bouncy:** select BO_Bouncy_Ball01: Inspector "Physics (bouncy object)"; set coeff_restitution 0.9, save, reload,
   kick it: bouncier. N > "Bouncy set: BO_Bouncy_Ball (n)": the balls appear spaced as in the level, on the ground.
7. **Particles:** select a smoke particle node (spark gizmo, e.g. Bldg_smoke_01) - Inspector "Particle system"; drag it
   100 units; leave the editor: the smoke is at the new place (not doubled - if two show, the remake hit a system the
   game doesn't keep under its node's name: report). N > "Particles: ..." places a copy; Delete removes it.
8. **Moving:** select a car or a walking ped: "Moving along its path" - Play: it slides along its path (green line),
   Pause / Step / Stop (back where it was). Path speed on, 60 mph: leave the editor: it drives faster (unless its script
   resets the speed). Ctrl+Z turns the speed off again. Save, reload: still 60.
9. **Sound emitter:** N > "Sound emitter: SK6_BO_FireLoop (loop)" (search "fire"), click near a skater spot, leave the
   editor: the fire crackles near it, fading with distance, silent while paused. Inspector > Sound: Volume 300 louder,
   Radius 1000 (inside the sphere: full volume). Pick another sound / (none). Save, reload: same.
10. **Lobby flag:** N > "Team lobby flag (Red)" in a level without one (TR has Red: refused with the message; try Green).
    Pennant gizmo appears; save. (Only a network lobby makes the flag object - check by hand online if wanted.)
11. **Reset level** (Ctrl+Shift+Delete): copies, speeds, emitter sounds gone; Ctrl+Z brings them back.
12. **Place palette:** the list scrolls (long in Boston); the search box filters it.

## Notes for the lead
- Shared files touched (small, additive): level_editor.cpp, level_editor_ui.cpp, level_editor_layout.{h,cpp},
  level_editor_node_edit.cpp, level_editor_node_create.{h,cpp}, level_editor_objects.{h,cpp}, CMakeLists.txt,
  tools/game/layout_file.ts, apply_layout.ts (+ test). Palette indexes: the entities' entries come after the chains'
  three (PlaceEntity: PlaceEntries().size() + kChainEntries) - if another branch appends palette entries, keep the
  entities last or adjust PlaceEntity's base.
- No decompilation; no game-code addresses newly replaced. Calls into game code: 0x005A2E50 (as before), 0x004A09E0 /
  0x00485A00 (nearest camera, its position - as sfx_voices.cpp), the Sfx manager 0x006F85A8 and level wave table
  0x006F5E68 / count 0x006F85B8.
- Switch off: `[features] level_editor=0` (the whole editor); nothing else runs outside it except the emitter sounds and
  path speeds a layout / baked level names.
