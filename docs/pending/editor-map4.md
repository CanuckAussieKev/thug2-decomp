# editor-map4 (agent record): the feature map's last partial rows - particles turned, cameras playable, the EDITED entry's name on its sign, ambient light and fog - NOT run in the game yet

NOT ORIGINAL (the level editor is ours). Branch `worktree-agent-ac73cb9bf218fc276` (from main 00b1e49). Level editor / UI
work: **NOT run in the game** - the user tests. Checked offline: own build (out\build\map4, PDB endpoint thug2map4; no
warnings), thug2_check on THUG2PM.exe and on the stock C:\Games\THUG2\Game\THUG2.exe (read only): argument sizes 3300
replacements, 0 MISMATCHED; **437 early self-tests all passed** on both; `node tools/check_code_writes.ts`: No overlaps;
`npm run typecheck` / `lint` / `test`: 139 pass; mutations below. **The overlay changed (tools/menu_level/level_select.ts):
the lead must rerun make_menu_level.ts** for the sign labels (the DLL side works without it - the entry's title shows the
name either way).

| Row | What works | Live / reload | Left |
|---|---|---|---|
| 1 Particles turned | Z / X (or the Inspector's heading) on a particle node turns its emitter with it: MidPosition / EndPosition turned about the node's place (as TurnAboutUp turns a heading) and moved with it, the BoxDimsStart / Mid / End boxes turned (the box along the world's axes holding the turned box: a quarter turn swaps x and z) - always from the emitter as first seen (no drift over many notches) | live (the node's fields at once, the system made again 0.3 s after the turn rests - shown at the latest on leaving the editor, as moves); reload: the `set ... angles=` record turns it again; baked (apply_layout.ts placeParticle) | the engine also builds a rotation matrix from a particle node's Angles (0x0042A090 reads Angles into D3DXMatrixRotationX/Y/Z) whose consumer wasn't traced - the bounding box (0x00495260) treats the boxes as world-axis extents, so turning the dims is our reading; check 2 below decides |
| 2 Cameras playable | Triggers window (T) > **Camera**: a trigger volume that plays an editor camera (the nearest; Inspector: Camera combo / a node by name, Seconds 0.5-60) - its script `LevelEditor_PlayCamera camera=<node> time=<s>`. The script command (ours, registered with the editor's): plays the node as the game plays goal / view-goals cameras - `PlaySkaterCamAnim {name=ed_camera_shot virtual_cam targetid=world targetOffset positionOffset frames=<s*60> skippable=1}` (CFunc 0x005AD830, view_goals_menu.qb's play_single_view_goals_cam's call) without play_hold, so the animation ends after its frames and the skater's camera is back (a button skips it); a shot still playing is killed first (KillSkaterCamAnim name=ed_camera_shot, 0x005ADD70). Offsets: an editor camera's from its place now; else the node's targetOffset / positionOffset; else its Pos / Angles looking 1000 ahead. A level's own trigger calling it shows as a Camera trigger (orange, an arrow to the camera) | live in play (refused while the editor runs - Look through is the preview there); triggers saved as `trigger ... camera ... target= seconds=` records; baked as the other triggers (node, box, script - the baked script needs this project's DLL) | no "play now" button in the editor (the game is paused there); a skater-relative shot (targetid=skater) not offered |
| 3 EDITED entry's name | LevelEditor_EditedLevels: the entry's title (text=, shown above the signs) is the edited copy's name, else "<LEVEL> (EDITED)"; new sign_label= (the name cut to 14 characters, else "EDITED") which the overlay's level select draws on the sign (a small TextElement child of the sign: grows / fades with it on focus) | next time the level select opens (after a save) | the label sits over the level's own sign art (position (34, 64) in the sign's 68 x 72 space, scale 0.45 - SIGN_LABEL_POS / SCALE in level_select.ts); needs the overlay rebuilt |
| 4 Ambient / fog | **Studied:** nothing reads a level structure's ambient_* / fog_*. Their checksums (0xED664897... / 0xCE0BD6E5...) appear in the exe only in the time-of-day manager's state parser 0x00571A00 (TODManager_SetParams' state structures - tod_manager.qb's tod_manager_default_*), CFunc_TODManager_GetCurrentTODParams 0x00572C00 and CFunc_GetLightCurrentColor 0x00447130 (outputs); no level structure in Levels.qb has fog_* and its ambient_* do nothing. The engine's ambient light and fog come from the manager's ApplyTODParams (0x00572A10: SetAmbientColor 0x0048E8F0, fog 0x00490DE0 / 0x00490E20 / 0x00490E00 - what SetFogColor / SetFogDistance / EnableFog call). **Done:** a NOT ORIGINAL filter in our tod_manager.cpp (Gfx::SetTODParamsFilter - ApplyTODParams and SetFogActive pass the parameters through it): the edited copy's ambient / fog put over the state's (fog shown even where the manager's is off), sent at once on a change (Reapply: ApplyTODParams with the current parameters). Kept in the level structure as **ed_ambient=[r g b] / ed_fog=[r g b a d]** (our keys; StructureValues writes them for the edited copy's load, apply_layout.ts bakes the same) - read once the level is up (Layout::Tick -> LevelProps::OnLevelUp, offline only); the panel's defaults are the manager's current state | **live** in the editor (the Ambient / Fog rows now say "live"); on load of the edited copy (its record) and of a baked level (its ed_ keys) | lights a script set from a named state (TODManager_ResetLevelLights state_name=) aren't overridden until the next state change; with tod_manager.cpp's replacements disabled the game's own ApplyTODParams runs without the filter (only the editor's own Reapply applies it) |

## Files
- `src/game/front/level_editor_entities.{h,cpp}` (ParticleEmitter, TurnParticle, TurnBoxExtents; NodeMoved takes the angles,
  the origin kept per node), `level_editor_node_edit.cpp` (passes the angles).
- `src/game/front/level_editor_triggers.{h,cpp}`, `level_editor_triggers_ui.cpp` (Kind::Camera - after Break, record word
  `camera`, `seconds=`; Calls.camera; AddCamera; the Inspector's Camera rows; arrows to the camera).
- `src/game/front/level_editor_cameras.{h,cpp}` (ShotFrames, CFunc_PlayCamera), `level_editor.cpp` (registers
  LevelEditor_PlayCamera).
- `src/game/front/level_editor_level_props.{h,cpp}` (Override, ReadOverrideKeys, OnLevelUp, Reapply; ed_ keys),
  `level_editor_layout.{h,cpp}` (OnLevelUp in Tick; EditedTitle / SignLabel; sign_label=), `level_editor_fields_ui.cpp`
  (the rows' "live" notes).
- `src/game/gfx/tod_manager.{h,cpp}` - **shared, small and additive**: SetTODParamsFilter (null by default: the original
  behaviour), called at the top of ApplyTODParams and in SetFogActive's fog-on branch.
- `src/hook/selftests_editor_map4.cpp` (new; CMakeLists.txt one line), `selftests_editor_fields.cpp` (StructureValues'
  ed_ keys), `selftests_editor_levels.cpp` (title / label).
- Tools: `tools/game/triggers.ts` (camera kind, seconds, body, names), `tools/game/apply_layout.ts` (placeParticle /
  turnBoxExtents for `set` records with angles; ed_ambient / ed_fog in Levels.qb; describe), `tools/menu_level/level_select.ts`
  (the sign label), tests in `apply_layout.test.ts`, `map_features.test.ts`, `triggers.test.ts`.

## Self-tests and mutations
Early: TestEditorParticleTurn, TestEditorCameraTrigger (the body's bytes - the same bytes are in map_features.test.ts),
TestEditorLightingOverride, plus TestEditorLevelProps / TestEditorLevelsFiles extended. Mutations: `node tools/mutate.ts
out/scripts/mut_map4.ts` in the worktree (its own verify build, build_verify_map4.cmd): **22 / 22 caught** - box
turn (swapped / no exact quarter / two boxes), Mid / End turn sign, about the old place, End not turned, the camera
body (integer time, unclamped, no Problem), the record (max, seconds unwritten), a calling script's kind, shot frames
(30 fps, no minimum), the override (level first, fog always, distance as a channel), the ed_fog key / distance, the
title with "(EDITED)" kept, the label length and empty label.

## In-game checklist (NOT run in the game yet)
Use a workspace with this branch's DLL **and the overlay rebuilt** (`make_menu_level.ts --game <workspace>`); `[features]
debug_editor=1` helps. Watch thug2-decomp.log for `level editor:` lines and EXCEPTION / RUN-TIME CHECK / DEBUG CRT.

Particles
1. `-edit bo`, select Bldg_smoke_01 (Outliner, Particles). Press Z three times (45 degrees), then three more (90): after
   each pause the smoke is made again; at 90 degrees its drift (towards Mid / End) points a quarter round from where it
   did. All fields (Inspector): MidPosition / EndPosition changed, BoxDimsStart's x and z swapped at 90.
2. Watch the smoke at 45 degrees: the particles start in a box that's turned / grown - report whether the emitter looks
   turned twice (if the engine also turns the boxes by the node's Angles, the boxes should be left alone - then say so).
3. Turn it back (X six times): the fields are exactly the level's again (no drift). Move + turn, Ctrl+S, reload BOSTON
   (EDITED): same place and direction. Ctrl+Z over a turn puts the emitter back.

Cameras
4. Make a camera (V) looking at a ledge. T > **Camera**: a trigger box in front of the view, orange, an arrow to
   ed_camera01; the Inspector: Camera ed_camera01, Seconds 3.
5. Leave the editor and skate through the box: the view cuts to the camera for 3 seconds (the skater keeps skating), then
   back to the skater's camera. Press a button during the shot: it skips back. Skate through twice quickly: one shot,
   restarted.
6. Seconds 10, a node by name (a restart's name): the shot looks from the restart, 1000 ahead along its heading.
7. Ctrl+S; the layout has `trigger ... camera ... target=ed_camera01 seconds=3`; reload the EDITED entry: the trigger and
   camera are back and work. Reset level: gone.
8. A level script calling `LevelEditor_PlayCamera camera=ed_camera01 time=2` plays it too (e.g. a baked level's script,
   or one added to the level's scripts by hand); inside the editor nothing happens (refused).
9. Bake: `node tools/game/apply_layout.ts --level BO --game C:\Games\THUG2-n --dry-run`: "trigger ed_triggerNN: camera
   ed_camera01 for 3 s (LevelEditor_PlayCamera - needs this project's DLL)".

EDITED entry
10. Level properties > Name "My Boston", Ctrl+S, leave. Level select > EDITED LEVELS: the sign shows "My Boston" in small
    letters near its foot (growing with the sign when focused), the title above reads "My Boston". Another edited level
    without a name: its sign says "EDITED", its title "<LEVEL> (EDITED)". A 20-character name is cut to 13 + ".".
11. Without the rebuilt overlay: the title shows the name; no label (the old script ignores sign_label).

Ambient / fog
12. Level properties > tick **Ambient**, drag the colour to red: the skater / objects' ambient light turns red **at once**
    (no reload). Untick: back to the level's.
13. Tick **Fog**: colour grey, alpha 128, distance 1500: fog closes in at once; alpha 0 / untick: back to the level's own
    fog (none where the level has none).
14. Change the Time of day with fog set: the fog stays the edited one in every state.
15. Ctrl+S, leave, load the EDITED entry: ambient and fog as set from the start (log: "the level structure's own lighting
    (ed_ambient / ed_fog) put in place"). The level as shipped: its own lighting.
16. Bake with `--install` into a workspace overlay and load the level as shipped from it: Levels.qb's Level_<x> has
    ed_ambient / ed_fog and the lighting shows (offline only; a network game ignores them).

## Switching off
`[features] level_editor=0` (the editor; LevelEditor_PlayCamera stays registered - a baked camera trigger still plays).
Nothing here is a REPLACE; the TOD filter is null unless the editor's level properties or a level's ed_ keys set it.
