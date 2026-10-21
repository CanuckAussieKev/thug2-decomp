# Level-editor UX research for the THUG2 editor

Scope: ideas that are not already in the lead's plan (play from here, W/E/R modes, changes panel, command search, box select /
select similar, surface / vertex snap, view modes / overlays, camera focus / orbit / speed / bookmarks / ortho, jump-arc and
grind previews, ruler, Outliner toggles / groups / rename / filters, Inspector multi-edit / copy-paste / folding, duplicate
with repeat, cross-level copy-paste, preferences / keymap, asset grid with recents / favourites, layout snapshots,
first-run tips). Also checked against what the editor already has (level_editor.h, docs/pending/editor-*.md): G drop to
ground, click-again cycling, L lock, H / I hide / isolate, gaps / triggers, ladders, model swap, the B-palette viewer, F8
restart tester, autosave.

Sources are linked where a page confirmed the behaviour. Several sites (Epic docs, GDC Vault, StrategyWiki, Valve's wiki)
refused automated fetches, so a few details come from search snippets. **[mem]** marks behaviour I know from the games
but could not confirm on a fetched page. Sizes: S = a day or two, M = a week, L = more.

## 1. Additional ideas, ranked

| # | Idea | Who does it | Why it helps a THUG2 editor | Size |
|---|------|-------------|-----------------------------|------|
| 1 | **Problem checker with Go to / Fix**: a live list of problems in the edited copy, each one clickable (the camera goes there and selects it) and, where it's safe, fixable. | Hammer's Check for Problems (Alt+P: Go to error / Fix / Fix all of type) [VDC](https://developer.valvesoftware.com/wiki/Hammer_Check_For_Problems_Dialog); TrenchBroom's live Issue Browser [manual](https://trenchbroom.github.io/manual/latest/) | THUG2 has many silent failure modes that only show up in play: spawns or crowns under the floor or inside walls, rail points floating away from their scenery, moved scenery that leaves collision behind (the TR rail bug), going over the crown / restart / light limits, gaps whose triggers never line up, empty trigger volumes, edits to nodes shared with THUGPRO, layout records that no longer resolve after a level update. One list beats one-off warnings. | M |
| 2 | **Adjust Last Operation panel**: after an action (duplicate-with-repeat, scale, turn, drop, place), a small panel lets you change its parameters afterwards (count, offset, angle, snap) and re-applies it as the same undo step. | Blender, F9 / the panel at the bottom left [manual](https://docs.blender.org/manual/en/2.93/interface/undo_redo.html) | Duplicate-with-repeat is planned. This turns "Ctrl+D, nudge, Ctrl+D..." into "Ctrl+D, set count 8, offset 120" - a row of ledges or kickers in seconds. It also covers typed values for every key action without a dialog before each one. | M |
| 3 | **Skater line trail from play tests**: while testing, record the skater's path and draw it in the editor afterwards as a polyline, coloured by state (air / grind / manual / wallride / bail), with markers where gaps fired. Keep the last few runs. | Trackmania's ghosts and its "build, drive, look" loop (the editor is "so easy to use and to test that people create a piece of track, press forward and see where their car goes") [Game Developer](https://gamedeveloper.com/design/acknowledging-trackmania-s-non-racing-mode) | Shows where a line actually flows, where skaters bail, which kicker overshoots - better data for tuning than a predicted jump arc, and it uses the real physics. | M |
| 4 | **Placement ghost that follows the cursor and aligns to surfaces**: while placing (B / N), the piece or node shows as a translucent preview on the surface under the cursor before the click, optionally turned to the surface's normal (a wall for a ladder or wallride sign, a slope for a kicker). | Unreal: dragging from the Content Browser places onto the surface under the cursor (a [placing doc](https://dev.epicgames.com/documentation/en-us/unreal-engine/placing-actors-in-unreal-engine)); TrenchBroom: drag from the entity browser into the view [manual](https://trenchbroom.github.io/manual/latest/) | Placing now drops to the ground on the click. A ghost first removes guess-and-undo, and Shift+click "place more" becomes painting. | S |
| 5 | **Live measurements while dragging**: show the size and height above the ground next to the piece, plus how far it moved and its distance to the nearest rail / ledge / coping while it's held. | Hammer shows geometry, its size and its position together in the view (Robin-Yann Storm's GDC 2016 "Keeping Level Designers in the Zone" and his write-up) [Game Developer](https://gamedeveloper.com/design/don-t-fear-the-hammer-the-importance-of-ux-in-editors-tools), [GDC Vault](https://gdcvault.com/play/1022954/Keeping-Level-Designers-in-the) | Skate lines depend on heights and gaps (can you ollie onto that ledge, does the transfer reach). The planned ruler is a separate tool; this is the always-on readout while moving. | S |
| 6 | **Status bar with context-sensitive mouse and key hints**: the bottom bar shows what LMB / RMB / MMB / wheel and the modifiers do now (hovering, dragging, placing, a rail point selected). | Blender's status bar (mouse button shortcuts and the active tool's keymap; tap Alt for tool keys) [manual](https://docs.blender.org/manual/en/latest/interface/window_system/status_bar.html); TrenchBroom's info bar changes with the tool [manual](https://trenchbroom.github.io/manual/latest/) | The editor's keys depend on context (W A S D fly or nudge, L locks a piece or splits a rail point, Escape cancels / closes / deselects). Hints in place beat F1 and first-run tips, and they stay correct as features are added. | S |
| 7 | **Linked duplicates (instances)**: a copy that stays linked to its source; changing one (moving its parts, its model, its fields) changes them all until it is unlinked. | TrenchBroom's linked groups [features](https://trenchbroom.github.io/manual/latest/); Unreal's Packed Level Actors / prefabs, Unity prefabs | Skateparks repeat modules (a ledge plus a rail, a kicker pair, a bench row). Fix the rail height once instead of eight times. Fits the layout format as a `link` record over cloned nodes. | L |
| 8 | **Selection sets**: save the current selection under a name, re-select it from a list, add to it or take from it. | Source 2 Hammer's Selection Sets (a section of its [Hammer Overview](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Hammer_Overview)) | THUG2 levels have scattered pieces that belong to one idea (a line's rails, a gap's two triggers and its ledges). Cheaper than groups (no change to the level) and saved with the layout. | S |
| 9 | **Align / distribute tool**: line the selection up by its min / centre / max on X / Y / Z (world or local), relative to the selection's bounds or the last-selected piece; spread pieces evenly. | Roblox Studio's Align tool [docs](https://create.roblox.com/docs/studio/align-tool) | Rows of ledges, benches and rails, kicker pairs facing each other, crowns on a line. Snap-to-25 doesn't line up pieces that weren't on the grid to begin with (most of a shipped level). | S |
| 10 | **Follow selection camera**: the view stays locked on the selection while it moves (dragged, nudged, or a moving object playing its path). | Unity, Shift+F "lock the view to the selected GameObject while it moves" [manual](https://docs.unity3d.com/Manual/SceneViewNavigation.html) | Nudging a piece across a big level (W A S D nudge, which also flies) loses it off screen; also useful to watch the planned moving-object preview. | S |
| 11 | **Click-to-teleport and skater-height walk mode**: a key moves the camera to the point under the crosshair; a walk mode keeps the camera at a skater's eye height with gravity, so you see the level as a player does. | Blender's walk navigation (Space teleports to the crosshair; Tab toggles gravity; V jumps) [manual](https://docs.blender.org/manual/en/latest/editors/3dview/navigate/walk_fly.html) | THUG2 levels are big. Teleport makes crossing them quick, and skater height shows whether a ledge reads at the right scale without starting a test. | S |
| 12 | **Usage-aware asset browser**: sort the B palette by how often a piece is used in the level, filter to "used in this level", group by name prefix. | TrenchBroom's entity browser (sort by name or usage, group by prefix, "only used") [manual](https://trenchbroom.github.io/manual/latest/) | Shipped THUG2 levels have hundreds of pieces with Neversoft prefixes (g_WareNew_...); usage and prefix grouping find the level's own kit fast. Complements the planned thumbnails / favourites. | S |
| 13 | **Grid projected on the surface under a drag**: draw the snap grid on the face the piece is sliding over (or the ground under it), not only in space. | TrenchBroom (the grid is projected onto brush faces in 3D) [manual](https://trenchbroom.github.io/manual/latest/); Eskil Steenberg's GDC microtalk on how much simple snapping helps [GDC Vault](https://gdcvault.com/play/1025288/Tools-Tutorial-Day-UX-Microtalks) | Snap 25 is invisible right now; seeing the grid on the floor makes snapping predictable and shows alignment with neighbours. | S |
| 14 | **Budget meter**: one always-visible bar for the level's limits - crowns (10), restarts (256), lights (256 / 150), triggers, added clones / memory - turning amber near a limit. | The Tony Hawk park editors' "park memory" meter, which fills as you place pieces **[mem]**; constraint as a design aid in the THPS park editors [jordanm.co.uk](https://jordanm.co.uk/2024/06/25/the-park-editor-made-me-do-it.html) | The engine's limits are hard and silent. A meter tells you before the problem checker would. | S |
| 15 | **Pie / radial menus with a controller mode**: hold a key (or a pad button) for a radial menu of the common actions (transform mode, snap, view mode, place). | Blender's pie menus ("Pie Menu on Drag": tap does one thing, hold-and-drag opens the pie) [manual](https://docs.blender.org/manual/en/latest/editors/preferences/keymap.html); the THPS park editors are pad-only **[mem]** | THUG2 is played on a pad and the editor already takes Start to leave. Radials make a pad editor possible (and keep keys free for the mouse). | M |
| 16 | **Wheel drill-through on the same spot**: with a modifier, the wheel steps through what's under the cursor (front to back), highlighting each, before you click. | TrenchBroom ("the scroll wheel drills through occluded objects") [manual](https://trenchbroom.github.io/manual/latest/) | The editor already cycles by clicking again; the wheel previews the next candidate without changing the selection, which is gentler in TR's stacked geometry. | S |
| 17 | **Note pins in the world**: put a short text note at a point (to-dos, "raise this ledge"), listed in the Outliner, shown as a billboard, saved in the layout, never baked. | Unreal's Note actor **[mem]**; review-comment pins in Roblox Team Create **[mem]** | Long edits over many sessions, plus play-test findings: the note sits where the problem is. Cheap, given the layout records. | S |
| 18 | **Replace selection with...**: swap every selected piece / object for the chosen asset, keeping position, rotation, scale and links. | Unreal's Replace Selected Actors (keeps location, rotation, scale, tags) [API](https://docs.unrealengine.com/4.27/en-US/API/Editor/UnrealEd/Editor/UEditorEngine/ReplaceSelectedActors/index.html) | The editor has a per-object model swap; a batch version lets you try "all these benches as ledges" in one step. | S |
| 19 | **Validate a level by skating it**: a test run can "validate" a goal or gap by actually landing it; the editor marks gaps / goals that no play test has hit yet. | Trackmania requires a map to be driven to its finish before it can be published (validation) **[mem]** | For gaps and created goals: proves each one is reachable, and the marker becomes a to-do list. Uses the trail recording (#3). | M |
| 20 | **Simulate without the skater**: run the level's moving objects, cars, peds and particles while still in editor camera, then stop and return to the edit state. | Unreal's Simulate in Editor (with "Keep Simulation Changes", K) [docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/playing-and-simulating-in-unreal-engine) | Car paths, moving platforms and ped routes that the editor now moves can be watched in place without a full play test. | M |

## 2. Better variants of planned items

**W/E/R vs. fly with WASD.** The two big editors avoid the conflict the same way: **flying only works while the right
mouse button is held**. Unity: "Hold the right mouse button... W/S/A/D... Q/E down/up"; the movement keys work whatever
tool is active, and without RMB, Q/W/E/R pick tools ([Unity](https://docs.unity3d.com/Manual/SceneViewNavigation.html)).
Unreal has a preference for WASD camera control: always, only while RMB is held (the default), or never
([UDN prefs](https://docs.unrealengine.com/udk/Three/UnrealEditorPreferences.html)); Godot uses W/E/R for tools and WASD for
its freelook while RMB is held, plus Shift+F for a toggled freelook without holding the button
([Godot shortcuts](https://docs.godotengine.org/en/4.6/tutorials/editor/default_key_mapping.html)). Unreal also cycles
translate / rotate / scale with the **Space bar** ([Epic](https://dev.epicgames.com/documentation/en-us/unreal-engine/transforming-actors-in-unreal-engine)).
Recommendation: make RMB-held flying the default (the editor already looks with RMB, so the change is small), keep
"WASD always flies" as a preference for people used to it, and in that mode put the transform modes on Space (cycle) and
1 / 2 / 3. This also clears our other WASD overload (W A S D / Q E nudge a selection *and* fly): without RMB they nudge;
with RMB they fly; the arrow keys could nudge too, as in Hammer.

**Camera speed.** Unity: the **wheel while flying changes the speed**, not the position
([Unity](https://docs.unity3d.com/Manual/SceneViewNavigation.html)); Unreal: RMB + wheel goes past the speed slider's
range ([forum](https://answers.unrealengine.com/questions/404631/view.html)) and a preference scales speed by the selected
object's size ([versluis.com](https://versluis.com/2023/12/changing-camera-speed-depending-on-object-size-in-unreal-engine)).
Our wheel moves forward / back; with RMB-held flying, wheel-while-RMB = speed and wheel alone = forward / back keeps both.

**Focus.** Unity's F a second time zooms to the pivot; Shift+F follows (idea #10). TrenchBroom's orbit: the
**wheel changes the orbit radius** and the orbit centre is the point clicked on a surface, not only the selection
([manual](https://trenchbroom.github.io/manual/latest/)) - worth copying for Alt+drag orbit.

**Play from here.** Unreal: "Play From Here" in the view's right-click menu, and **Possess / Eject** to switch between
play and simulate in one session; **Keep Simulation Changes (K)** keeps what changed during play
([Epic](https://dev.epicgames.com/documentation/en-us/unreal-engine/playing-and-simulating-in-unreal-engine)). Trackmania DS
let you put the vehicle anywhere to test one part ([Siliconera](https://siliconera.com/?p=29185)). Variants for us: start at
the cursor point *facing the camera's heading with the camera's speed carried over* (test a kicker at speed without a
run-up), one key to go back to the editor with the camera exactly where it was, and record the trail (#3) every time.

**Changes panel / undo history.** Blender's Adjust Last Operation (#2) is the cheapest high-value piece of it. For the
changes list, Hammer's "Fix / Fix all of type" pattern (#1) suggests group actions: "revert all changes of this kind".

**Box select.** TrenchBroom's paint selection (drag across objects to add them; a drag that starts on a selected object
duplicates it instead) ([manual](https://trenchbroom.github.io/manual/latest/)) is a useful second mode for scattered
small pieces where a box catches the floor.

**Overlays / view modes.** Add skate-specific overlays (section 3) beside wireframe / collision; TrenchBroom puts its view
options and filters in one dropdown on the info bar - one place for both.

**Snapping.** TrenchBroom snaps vertex moves *relatively* by default (keeps the off-grid offset) with a modifier for
absolute ([manual](https://trenchbroom.github.io/manual/latest/)). Shipped THUG2 pieces are rarely on our 25 grid, so
relative snapping (move by 25 from where it is) is usually what's wanted; offer absolute behind a modifier.

**Asset grid.** Add TrenchBroom-style usage sort and "used in level" (#12), and a cursor-following placement ghost (#4).

## 3. Skate-game-specific ideas (Tony Hawk park editors and similar)

Sources for the park editors are thin online (StrategyWiki's THPS2 page blocks fetches; most hits are gap guides). What
was confirmed is cited; the rest is **[mem]**.

1. **Two-pick gap wizard** - THPS2's gap creation: pick two pieces as markers, the skater must pass both in one combo, then
   name it, give it 50-5000 points and a type (air / rail / manual / wallride), adjustable afterwards
   ([StrategyWiki](https://strategywiki.org/wiki/Tony_Hawk%27s_Pro_Skater_2/Park_Editor) via search snippet). Our gaps
   are trigger volumes; a wizard that makes the two triggers from two clicked pieces (sized to them) and asks for name /
   points / type in one panel would hide the volume work. THUG2 also let players make custom gaps in its park editor
   ([Steam thread](https://steamcommunity.com/app/2395210/discussions/0/3886101523499937836/)). **M**
2. **Rail tool: draw a new rail point by point** - THUG's rail tool lays rails "on almost any item in the field", up / down
   and sideways ([Neoseeker review](https://www.neoseeker.com/tonyhawk-underground/user_reviews/21705/), search snippet).
   We move existing rail points; making new rails, with each click snapping to the nearest geometry edge (a ledge's lip,
   a wall top) and an "auto-rail along this edge" for a selected piece, would be the biggest gain for line building. **L**
3. **Create-a-goal** - THUG / THUG2 park editors let players make goals (trick challenges, SKATE / COMBO letters)
   ([Neoseeker review](https://www.neoseeker.com/tonyhawk-underground/user_reviews/21705/);
   [Wikipedia](https://en.wikipedia.org/wiki/Tony_Hawk%27s_Underground_2)). For us: place the letters as nodes along a
   recorded trail (#3), pick a goal type and a ped to give it, and test it from the editor. **L**
4. **Pieces that connect** - THPS1+2's Create-a-Park draws complaints that its pieces don't connect cleanly
   ([Steam](https://steamcommunity.com/app/2395210/discussions/0/3883850599455450186/)). Lesson: when you snap a
   quarter-pipe or ledge next to another, snap edge-to-edge (matching coping heights, no gap), and flag seams in the
   problem checker. **M**
5. **Skateability overlay** - colour surfaces by what the skater can do on them: vert (quarter-pipe / launch), wallride-
   able, skatable floor, non-skatable (bail), grindable edges highlighted, ladders / hang points. THUG2 stores this in its
   collision face flags (the editor already shows collision and per-face flags). Shows at a glance why a line fails. The
   park editors show pieces' rails as part of the cursor preview **[mem]**. **M**
6. **Restart and start points tied to testing** - the park editors put the P1 / P2 / horse / CTF / KOTH points as pieces
   **[mem]**; we have the F8 restart tester and spawn nodes. Add "test from this restart" on a spawn's right-click and a
   check that every multiplayer mode the level claims has its points (feeds the problem checker). **S**
7. **Raise / lower ground with auto-ramps** - the THPS3/4 / THUG park editors raise blocks of ground and edges become
   quarter-pipes or walls automatically (pools) **[mem]**. Hard on fixed shipped geometry; feasible only for our own
   added "kit" pieces. Note it, don't plan it yet. **L**
8. **Session / Skater XL** - Session's built-in **object dropper** places a fixed set of skateable objects into a map at
   run time and rides them straight away ([NACON](https://www.nacongaming.com/en/session-skate-sim)); its placements were
   lost after a game update ([Steam](https://steamcommunity.com/app/861650/discussions/0/3874844466814297153)) - a
   reminder that our layouts must survive level / DLL updates (the problem checker's "unresolved record" check, #1).
   Their community map tools live in Discords and found nothing citable beyond that.
