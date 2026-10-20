# Level editor: a model viewer in the Add palette (B)

**NOT run in the game yet** (the user tests the editor's interactive behaviour himself; built and checked offline only).

The user's request: "In the Add menu, we need a model viewer to see what we are placing."

## The approach (and why)
The off-screen render of the game's own meshes, not the fallback (ghost + box thumbnail). Reading the renderer showed
that drawing one piece or one object is a few calls into code the game already uses for exactly that:
- **Scenery** (a scene sector): its geometry's meshes (CGeom +0x3C / +0x40, world coordinates) drawn as
  `Nx::RenderScene` (0x004DC700) draws them - shown (flag 0x40) and not hidden (0x210), opaque first then transparent
  (material +0x20 bit 0x40), `SetMaterial` 0x004D1F00 then `DrawMesh` 0x004D5290 per mesh.
- **Objects**: each geometry of the model through its instance (CGeom +0x4C) with the engine's single-instance render
  0x004D16B0 (cdecl, instance, flags; flag 8 = no culling) - the instance's world matrix, bones (peds' current pose)
  and lights, as the game draws it; meshes as above when a geometry has no instance.
- The camera is the renderer's own `Nx::SetCamera` (0x004DB200) at the orbit's eye (40 degrees, aspect 1.1 = square).
  The camera's globals it and the instance render change are saved and put back afterwards (0x00786B90-0x00786CF4:
  world / view / projection / heading matrices, viewport, camera; 0x00786E18 the view's shape; 0x007CA968 / 0x007CA980
  the culling matrix), the frustum (0x00775B80) recomputed from them (0x004CDAA0), 0x007825BC (the instance-lights
  cache) cleared so the next instance sets its own lights.
- Renderer independence: only level_editor_ui_backend.cpp touches Direct3D - `UiBackend::BeginOffscreen(size, argb)`
  switches to a square X8R8G8B8 render-target texture with its own depth surface (the game's depth format, no
  multisampling), saving the target, depth surface, viewport and world/view/projection transforms; `EndOffscreen` puts
  them back; `OffscreenTexture()` is the ImGui texture id; `FreeOffscreen()`. The picture is freed with the device
  objects (InvalidateDeviceObjects, called by Mlp::ResetDeviceIfLost before a reset / alt-tab), when the palette closes
  and when the editor stops; it is made again on the next draw.
- It draws in LevelEditor::Draw (after the game's 2D pass, the game's frame done), before Ui::Frame; only while the
  palette is open with an entry highlighted. Size: 256 px at 1280x720, scaled with the UI, at most 512.
- All the game's state changes go through its cached setters (SetState / SetMaterial / DrawMesh), so the cache and the
  device stay in agreement; ImGui's renderer already saves/restores the device state in a state block.

## What changed
- New `src/game/front/level_editor_preview.h/.cpp` (namespace `Front::LevelEditor::Preview`): the pure pieces (Orbit,
  Drag, Wheel, FitDistance, SphereOf, OrbitEye, the lens constants) and the viewer (Show, Input, Render, Hide, Frame).
- `level_editor_ui_backend.h/.cpp`: the off-screen target (above); InvalidateDeviceObjects frees it.
- `level_editor_ui.h/.cpp`: `Model::PreviewView` (index, texture, name, size, type), requests `kBrowserPreview`
  (the entry hovered in the list) and `kPreviewOrbit` (drag pixels, wheel steps), `Ui::PreviewPixels()`; the Add
  window grows to 760 px (scaled; capped by the view's height) with the picture (210 px, centred) above the list, the
  entry's name, size ("x by y by z") and type (scenery: "scene sector (scenery)"; objects: the node's class and model
  file). Drag in the picture orbits (the slow turn pauses 2.5 s after a drag), the wheel zooms (1.2x a step,
  0.35-4x); the preview follows the mouse over the list, or the keys when they moved the highlight since.
- `level_editor_objects.h/.cpp` (additive): `Objects::Geometry(object, index)`, `Objects::ModelFile(name)`.
- `level_editor.cpp` (small): the browser's `preview` entry, `model.preview.index`, the two request cases,
  `Preview::Frame(&model)` in Draw before Ui::Frame, `Preview::Hide()` when the editor stops.
- Self-tests `src/hook/selftests_editor_preview.cpp` (TestEditorPreviewFraming, TestEditorPreviewOrbit): the lens is
  square, a box's sphere, the fitted distance, the eye looks at the centre from above, every corner of five box shapes
  inside the picture from 24 orbits at zoom 1 and outside at zoom 3, drag / wheel signs and limits. 8 mutations of the
  preview code, all caught.
- CMakeLists.txt: the two new .cpp files.

Checks: thug2_check.exe (default THUG2PM.exe, C:\Games\THUG2-2\THUG2PM.exe, the stock C:\Games\THUG2\Game\THUG2.exe):
0 MISMATCHED, 362 early self-tests passed; `node tools/check_code_writes.ts`: No overlaps. No new REPLACE (nothing of
the game's replaced).

## In-game test checklist (NOT run in the game yet)
1. `-edit TR` (or any level). Open the Add palette with B. Above the list: a square picture of the highlighted piece,
   on a dark grey background, filling most of the square, turning slowly; below it its name, its size
   (`W x H x D  (x by y by z)`) and "scene sector (scenery)".
2. Up / Down (the keys): the picture changes to each new entry and starts from the default angle again.
3. Move the mouse over the list rows: the picture follows the row under the mouse; press Up/Down while still
   hovering - the keys' entry wins until the mouse moves onto another row.
4. Drag in the picture with the left button: it turns (left/right) and the eye goes over / under it (up/down, stops
   near straight above/below); release - it waits about 2.5 s, then turns slowly again. The editor's camera must not
   move or pick while you drag there.
5. Wheel over the picture: zooms in / out (limits 0.35x-4x); the list doesn't scroll from it. Wheel over the list
   still scrolls the list.
6. Objects tab: a LevelObject / GameObject shows its model with the game's lighting; the type line says the class and
   the model file. Check an animated thing too if the level lists one.
7. Transparent parts (fences, glass, foliage) look as in the world (drawn after the opaque parts).
8. The game's view behind is unaffected while the palette is open: no flicker, no wrong camera for a frame, the sky,
   fog and the skater's/peds' lighting as usual; the editor's gizmos and outlines line up with the world (mouse picking
   still hits what's under the cursor).
9. Enter / double click still places a copy where the cursor points; F still shows it; closing the palette (B / Esc /
   the window's x) stops the preview.
10. Alt-tab out and back (or change the window size) with the palette open: the picture comes back (made again);
    no crash, no "device lost" loop.
11. A very large piece and a very small one: both framed to fit (a tenth spare around).
12. With `[features] editor_ui=0` (old HUD) nothing changes - there's no preview there.

If the picture is blank or wrong:
- Empty dark-grey square with "(no preview)": the entry wasn't found (sector without geometry, object gone) or the
  target couldn't be made. The log then says once `level editor UI: no off-screen target of N pixels (HRESULT)` and
  `level editor: the Add palette's preview has no target - shown without one`.
- Dark grey square with nothing in it, no log line: the meshes were skipped - with distance clipping on (the game's
  setting), DrawMesh skips meshes beyond the far plane, and objects whose instance is inactive aren't drawn. Note the
  level, the entry and the settings.
- Picture black / garbage, or the world flickers / the camera jumps for a frame: the save/restore of the camera state
  or the device target missed something - a screenshot (Print Screen) with the palette open and the log's last lines
  help.
- To switch the preview off quickly for a bisect: there's no ini switch; `editor_ui=0` avoids the whole ImGui UI.

## Left undone
- No "ghost" of the entry at the placing spot in the world (the off-screen render made it unnecessary; the world
  already tints the highlighted entry where it stands).
- Lights are the game's own for objects (their level lighting), not a separate neutral rig; scenery is pre-lit.
- Not run in the game: whether the render is right is for the checklist above.
