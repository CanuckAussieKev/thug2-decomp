# thug2-decomp

Readable C++ source for Tony Hawk's Underground 2 (PC), reconstructed by decompiling the game. You build it and play
it with the data files from your own copy of the game. No game files, executables or data taken from them are in
this repository.

> **Not affiliated with or endorsed by Activision, Neversoft or any other rights holder.** A non-commercial fan project
> for preservation, interoperability and education. The reconstructed game code belongs to the game's owners and no
> licence is granted for it; the project's own original work is MIT. See [NOTICE.md](NOTICE.md) and [LICENSE](LICENSE).
> Rights holders: open an issue and it will be dealt with promptly.

## The goal

A game that builds from readable source and keeps getting better, not a byte-for-byte copy of the original program.
The code is rewritten to do what the original does, in C++ you can read, change and debug; how it compiles doesn't
matter. While the decompilation is under way, each rewritten function is checked against the original's behaviour,
so the source stays a trustworthy base. On top of that base:

- **Bugs get fixed.** Crashes, hangs, memory limits and other faults found in the original are fixed in the source.
- **The game gets better:** PartyMod's fixes are part of it, plus a level editor, a new front end, graphics options,
  levels from the other Tony Hawk games and more (below) - with the original behaviour kept wherever it's what you
  play against: physics, scoring, scripts, online play with other players.
- **Changes are marked.** Code that isn't the original's behaviour carries a `NOT ORIGINAL` comment saying what it
  does and why, so the original stays readable underneath, and most additions can be turned off in
  `thug2-decomp.ini`.

## What's in it

- **The game's code** as readable C++, by subsystem (`src/game/`): engine, renderer, script runtime, skater physics,
  goals, park editor, networking, front end.
- **PartyMod's fixes** ([PartyMod](https://github.com/PARTYMANX/partymod-thug2) v1.0.4 by PARTYMANX, MIT) as part of the
  source: an SDL2 window and input, controller and keyboard bindings, widescreen, the OpenSpy online service.
- **Additions:** a level editor, a new front end, graphics options, a loading console, a screenshot key with the
  game's state alongside, debugging aids.
- **Level converters** (in the [thug2-content-pipeline](https://github.com/CanuckAussieKev/thug2-content-pipeline)
  submodule): THPS2, THPS3, THPS4, THUG, THAW and Project 8 levels, generated from your own copies of those games -
  plus THAW's BMX, rideable in any level.

## Status

The decompilation isn't finished: about 91% of the game's code (by bytes) is rewritten - 7,444 of 8,482 functions
(`docs/PROGRESS.md` has the figure by subsystem). Until it is, development builds run as a DLL inside the game process: each rewritten function
replaces the original and is checked against it by self-tests, and everything not yet rewritten still runs the
original code, so the game stays playable at every step. Those builds are tied to the one build of `THUG2.exe` the
code's addresses come from (the "reference build"); the retail CD build isn't supported.

When the decompilation is complete, the game builds as a standalone program from this source and needs only your
copy's data files. The libraries (D3DX9, GameSpy, the C runtime) will be linked rather than decompiled, and the QB
script interpreter is planned to be replaced by JavaScript (QuickJS) with the scripts decompiled to readable JS.

## Optional QB to JavaScript prototype

This fork's [`scripting/`](scripting/README.md) target starts the planned QB to
JavaScript path: an offline TypeScript translator emits generators executed in
embedded QuickJS, with typed C++ host calls and frame waits. Synthetic tests
need no game files. This is an optional standalone prototype; the existing DLL
build is unchanged. See [the implementation plan](docs/qb-js-plan.md) for the
supported first milestone and remaining work.

## What you need

**Games** (your own copies; nothing from them is ever committed):

| Game | Needed for | Default folder | What the tools read |
|---|---|---|---|
| Tony Hawk's Underground 2 (PC) | Everything | `C:\Games\THUG2` (the game in its `Game` subfolder) | `Data\...` |
| THPS2 (PC) | Porting THPS2's levels | `C:\Games\THPS2` | `ALL.PKR` (the PC version keeps every data file in it) |
| THPS3 (PC) | Porting THPS3's levels | `C:\Games\THPS3` | `Data\Levels\<Lvl>\` (`.bsp`, `.tdx`, `.qb`), `Data\Scripts\` |
| THPS4 (PC) | Porting THPS4's levels, the playable skate shop | `C:\Games\THPS4` | `data\levels\`, `data\scripts\`, `data\models\` |
| THUG (PC) | Porting THUG's levels | `C:\Games\THUG1` | `Data\pre\` (`<LV>scn.pre`, `<LV>col.pre`, `<LV>.pre`, `qb.pre`) |
| THAW (PC, version 1.01) | Porting THAW's levels and its BMX | `C:\Games\THAW` | `data\worlds\worldzones\`, `data\pak\`, `data\skies\` |
| Tony Hawk's Project 8 (Xbox 360 data) | Porting Project 8's levels | `C:\Games\THP8` | `game\DATA\` (`ZONES\`, `PAK\`) |
| THUGPRO (optional) | THUGPRO's additions to the ported levels, and online parity with its players | `%LOCALAPPDATA%\THUG Pro` | Read only, by reference (`tools/pipeline/compat/thugpro`) |

Every folder you give a tool (`--thug2`, `--thps3`, `--thps4`, `--thug1`, `--thaw`, `--game`, CMake's `THUG2_GAME_DIR`)
may be the install root, with the game in a `Game` subfolder, or the game folder itself; the tools find either
(`tools/pipeline/lib/install.ts`).

**Software:**

| Tool | Version | Used for |
|---|---|---|
| Visual Studio | 2026 (MSVC 14.51) with "Desktop development with C++", x86 tools, CMake and Ninja | The game code and `thug2_check.exe` |
| Node.js | 24 or later | Every tool in `tools/` (TypeScript, run directly: `node tools/<x>.ts`) |
| Git | any | Clone with `--recurse-submodules` (`tools/pipeline`, `tools/anteara`) |
| NeverScript | downloaded and built by `tools\build_neverscript.ps1` (docs/neverscript.md) | Compiling / decompiling QB scripts for the level converters |
| Ghidra | 12.1.4 | Decompiling |
| Python + uv | Python 3.13 via uv | The Ghidra scripts in `tools/ghidra` (pyghidra) and `tools/capstone_disasm.py` |

The build downloads its libraries itself at configure time, each pinned and checked against a SHA-256: SDL2 2.30.9,
MinHook 1.3.3, Dear ImGui 1.91.9b, stb, and the Inter 4.1 and JetBrains Mono 2.304 fonts (the level editor's UI).

**Antivirus:** the self-test runner loads the game's code into its own process and patches it in memory, which
behaviour-based antivirus can flag. Add exceptions for the repo folder and the game folder (keep build scripts inside
the repo, e.g. `out\scripts`, so the repo's exception covers them).

## Build

1. In Visual Studio: **File > Open > Folder...** and pick this folder.
2. Choose the **x86 Debug** configuration in the toolbar.
3. **Build > Build All**. The output (with `SDL2.dll` and `gamecontrollerdb.txt`) is copied to `C:\Games\THUG2\Game`;
   change `THUG2_GAME_DIR` in `CMakeLists.txt` or the CMake cache if your game is elsewhere.
4. Run the game and read `thug2-decomp.log` in the game folder.

From the command line:

```
npm install                          (once: the TypeScript tools' dependencies)
tools\build_neverscript.ps1          (once: out\neverscript\ns.exe)
tools\verify_build.cmd               (builds out\build\verify; nothing copied)
out\build\verify\thug2_check.exe     (argument-size checks + the early self-tests, offline)
node tools\check_code_writes.ts      (must say "No overlaps")
npm run typecheck && npm run lint && npm test
```

Prefer a solution file? `cmake -S . -B out/vs -G "Visual Studio 18 2026" -A Win32`, then open `out\vs\thug2_decomp.slnx`
and build **Debug | Win32**. Builds are Debug: debug-CRT reports and run-time check failures go to `thug2-decomp.log`
with a call stack.

## Playing

Useful start-up switches:

| How | What it does |
|---|---|
| `-level <name>` or `[features] start_level=<name>` | Skips the menus and starts that level in free skate. `<name>` is the level's folder name (`TR`, `BO`, `foun`, `z_ma`...). |
| `-edit <name>` or `[features] edit_level=<name>` | The same, skips the intro movies and opens the level editor there. |
| `-level <name> -at x,y,z[,heading]` or `[features] start_at=` | Once the level is up, the skater stands at that spot, facing `heading` (degrees, 0 = +z, 90 = +x). |
| `node tools/pipeline/game/edit_level.ts` | Lists every installed level by game; `edit_level.ts <name>` starts one in the editor. |

### The level editor

Pause menu > **LEVEL EDITOR**, or start with `-edit <name>`. It's offered in free skate and single session only (never
in career / story, classic, the park editor or network games).

- **Learning it:** F1 lists every key; **Ctrl+Space** (or F3) searches every action; the status bar shows what the
  mouse and keys do at that moment. Every key is rebindable (Preferences > Keys, saved in `[editor_keys]` of the ini).
  The full list of controls and the editor's modules is at the top of `src/game/front/level_editor.h`.
- **Edited copies:** edits never change a shipped level. Saving (Ctrl+S) writes the level's layout,
  `Game\thug2-decomp-layouts\<scene>.txt` (plain text, one record per line), and the level select then has a separate
  entry, **EDITED LEVELS (OFFLINE)** > "<LEVEL> (EDITED)". Layouts are never applied online. Unsaved work is autosaved
  every minute and offered back after a crash; Reset level puts a level back as shipped.
- **Nodes and scripts it makes:** nodes the editor adds (spawns, crowns, flags, lights, rails, ladders, paths,
  triggers, cameras, goals...) go into an extra node array; trigger volumes, rail speed-ups, jump boosts, breakables
  and proximity nodes get small QB scripts generated at run time. Goals use THUG2's own Create-a-Goal records.
- **Baking a layout into a level's files:** `node tools/pipeline/game/apply_layout.ts --level <name> [--dry-run]` writes a
  copy of the level's files with the layout built in, to `out\baked\<name>`; `--install --game <folder>` puts it in
  that folder's overlay. It warns about every edit that would break online parity with THUGPRO.

### Other games' levels (optional)

Each converter reads your install of that game and writes a staging folder next to THUG2; the menu tool then installs
everything into the data overlay and the level select:

```
node tools\pipeline\thps2_levels\thps2_levels.ts convert --thug2 C:\Games\THUG2 --thps2 C:\Games\THPS2
node tools\pipeline\thps3_levels\thps3_levels.ts convert --thug2 C:\Games\THUG2 --thps3 C:\Games\THPS3
node tools\pipeline\thps4_levels\thps4_levels.ts convert --thug2 C:\Games\THUG2 --thps4 C:\Games\THPS4
node tools\pipeline\thug1_levels\thug1_levels.ts --thug2 C:\Games\THUG2 --thug1 C:\Games\THUG1 convert
node tools\pipeline\thaw_levels\thaw_levels.ts   convert --thug2 C:\Games\THUG2 --thaw  C:\Games\THAW
node tools\pipeline\thp8_levels\thp8_levels.ts   convert --thug2 C:\Games\THUG2 --thp8  C:\Games\THP8
node tools\pipeline\thaw_bmx\convert.ts         convert --thug2 C:\Games\THUG2 --ns out\neverscript\ns.exe
node tools\pipeline\menu_level\make_menu_level.ts --game C:\Games\THUG2
```

THPS4 must be converted before `make_menu_level.ts` for the playable skate shop. Each
converter's README (`tools/pipeline/<game>_levels/README.md`) lists its switches; the THUGPRO compatibility register is
explained in `tools/pipeline/compat/thugpro/README.md`. `make_menu_level.ts` empties and rebuilds the overlay each run.
`docs/level-port-compromises.md` lists what the ports do differently from the originals. Optional: Anteara Mod
(`tools/anteara/README.md`).

## Settings (`thug2-decomp.ini`)

The ini sits in the game folder. Every key is optional; the default applies when it's missing. 1 = on, 0 = off.

**`[features]`** - our additions

| Key | Default | Effect |
|---|---|---|
| `skip_intro_movies` | 0 | Skips the logo and intro movies at start-up |
| `mute_audio` | 0 | Silences all sound from the first frame |
| `widescreen_menus` | 1 | 0 brings back PartyMod's pillarboxed 4:3 front end |
| `new_frontend` | 0 | The new front end: the game's own menus, restyled (0 = the classic menus) |
| `mouse_menus` | 1 | Mouse control of the menus (`debug_mouse=1` logs each click) |
| `hd_ui` | 0 | The menus' 4x-upscaled images and fonts (when the overlay was made with `make_menu_level.ts --hd-ui 4`) |
| `level_editor` | 1 | The pause menu's LEVEL EDITOR (`editor_ui=0`: the old on-screen HUD instead of the Dear ImGui UI; `debug_editor=1` logs its picks and moves) |
| `start_level` / `edit_level` | (none) | Start straight in a level / in the level editor (as `-level` / `-edit`) |
| `start_at` | (none) | With a start level: the skater put standing at `x,y,z[,heading]` once it's up (as `-at`) |
| `screenshots` | 1 | The screenshot key (Print Screen, or F12 if Windows' Snipping Tool takes it): a PNG and a `.txt` of the game's state; `screenshot_dir=` changes the folder |
| `restart_tester` | 1 | F8 puts the skater at the level's next unvisited restart |
| `level_select_loading_screens` | 1 | A level's loading screen is its level select picture with a clean bar over it (when the overlay was made with `make_menu_level.ts`; 0 = the original loading pictures) |
| `loading_console` | 1 | A panel on the loading screen: what is loading, the time since the load began and "last activity N s ago" (orange after 10 s with nothing new: probably a hang); 0 = the original loading screen |
| `debug_mode` | 0 | Neversoft's DEBUG OPTIONS in the pause menu, DEVKIT_ONLY levels, the viewer's free camera (F9) |
| `thaw_bmx` | 0 | Rides THAW's BMX (F7, or the pause menu's RIDE BMX; needs `tools/pipeline/thaw_bmx/convert.ts` and `make_menu_level.ts`) |
| `thaw_bmx_wheels` | 1 | The bike finds the ground under both wheels, as THAW does (0 = the skater's own ground check) |
| `thaw_bmx_shadow` | 1 | The bike drawn into the rider's shadow |
| `anteara_mod` | 0 | Plays Anteara Mod from the overlay (`tools/anteara/README.md`) |
| `create_100_cas` | 1 | GAME OPTIONS > CREATE 100% CAS |
| `thugpro_parts` | 1 | THUGPRO's extra CAS parts and boards, read from your THUGPRO install by reference (2 also shows them in CAS, 0 off; `thugpro_folder=` another install) |
| `allow_multiple_instances` | 0 | Lets a second copy run (its log is `thug2-decomp-2.log`) |
| `automation` | 0 | The test-automation channel (`tools/pipeline/game/README.md`); never online |
| `debug_rail_speed`, `debug_collision`, `debug_sprites` | 0 | Extra log lines for those systems |

**`[debug]`**

| Key | Default | Effect |
|---|---|---|
| `collision_events` | 0 | Logs a `collision-event:` line each time the skater's velocity turns round on the ground (the physics path that did it, the face, probes) and shows it on screen for 2 s - for finding broken collision in converted levels; `node tools/pipeline/game/collision_events.ts <log> [--col <level>]` groups them by spot |
| `collision_event_screenshots` | 1 | With `collision_events`: a screenshot of each event's frame too |
| `thaw_bmx_steer` / `thaw_bmx_wheels` | 0 | A log line each frame for the BMX's steering / its wheels |

**`[Graphics]`** - from PartyMod, plus ours

| Key | Default | Effect |
|---|---|---|
| `ResolutionX` / `ResolutionY` | 0 | The game's resolution; 0 = the desktop's |
| `Windowed` / `Borderless` | 0 | Runs in a window / a borderless window |
| `AntiAliasing` | 0 | Multisample anti-aliasing |
| `HQShadows` | 0 | High-quality shadows |
| `DistanceClipping` / `ClippingDistance` | 0 / 100 | Stops drawing beyond a distance (1-100) |
| `Fog` | 0 | Distance fog |
| `DisableBlur` | 1 | Keeps the motion blur off |
| `OcclusionCulling` | 1 | The original's occlusion culling; 0 draws everything in view |
| `anisotropy` | 0 | 2-16: anisotropic filtering on every sampler |
| `msaa` | 0 | 2 / 4 / 8 samples (the best the device has) |
| `draw_distance_scale` / `lod_distance_scale` | 1 | Multiply the far plane / objects' level-of-detail distances |

**`[Miscellaneous]`** - from PartyMod

| Key | Default | Effect |
|---|---|---|
| `UsePS2Controls` | 1 | PS2 controls (spine transfer, acid drop, break vert as on PS2); 0 = the Xbox layout |
| `UIControls` | 1 | PartyMod's menu-controls option |
| `PreferGlyphs` | 0 | Shows controller button glyphs even when playing with the keyboard |
| `GlyphStyle` | 1 | The button glyphs shown: 1 = PlayStation, other values = Xbox |
| `OnlineDomain` | `openspy.net` | The online service's domain (see Online play) |
| `OnlineGameName` / `OnlineGameKey` | (empty) | The online identity: empty = THUG2's own; `thugpro` = THUGPRO's lobbies (see Online play) |
| `Debug` | 0 | Opens a console window with the game's debug output |
| `[online] thugpro_peer` | 0 | Chat and server info with THUGPRO players (on by itself with `OnlineGameName=thugpro`) |

**`[Keybinds]`**, **`[Gamepad]`**, **`[Logging]`**

| Key | Default | Effect |
|---|---|---|
| `[Keybinds]` (PartyMod's keys) | PartyMod's | Keyboard bindings; plus `Screenshot=` and `ThawBmx=` (an SDL key name or scan code) |
| `[Gamepad] MovementStick` / `CameraStick` | left / right | Which stick moves and which turns the camera |
| `[Logging] Level` | 2 | How much goes into the log |

**`[editor]`** - remembered by the level editor; set from its toolbar, menus and Preferences window (Ctrl+,)

| Key | Default | Effect |
|---|---|---|
| `families` | all | The Outliner's family chips (which node kinds show) |
| `pick` | 7 (all) | The toolbar's Pick filter: 1 nodes, 2 objects, 4 scenery |
| `overlays` | all | The Overlays menu's choices |
| `collision` | 0 | The collision view (C) |
| `triggers` | 1 | Trigger volumes shown (T) |
| `carry` | 1 | Objects and nodes standing on scenery move with it |
| `shared_collision` | 1 | Moved pieces take their faces from shared collision objects with them |
| `palette_grid` | 1 | The Add palette as a thumbnail grid (0 = a list) |
| `trail_record` | 1 | Records play-test trails |
| `fly_speed` | 800 | Camera fly speed |
| `fly_accel_ms` | 0 | Time to reach full fly speed |
| `look_pct` | 100 | Mouse-look sensitivity (%) |
| `invert_y` | 0 | Inverts mouse-look up / down |
| `gizmo_pct` | 100 | Gizmo size (%) |
| `ui_scale_pct` | 0 | UI scale (%); 0 = automatic for the window size |
| `autosave_s` | 60 | Seconds between autosaves |
| `snap_move` | 25 | Grid size for snapped moves |
| `snap_turn` | 15 | Angle step for snapped turns |
| `accent` | 0 | The UI's accent colour: 0 orange, 1 blue, 2 green, 3 purple, 4 red, 5 teal |
| `wasd_flies` | 0 | 1 = WASD always flies; 0 = fly while the right mouse button is held |
| `speed_by_selection` | 0 | Fly speed scaled to the selection's size |
| `pad` | 0 | Controller mode |
| `tips_seen` | 0 | The first-run tips have been shown |

**`[memory]`** - the script pools' sizes (bigger than THUG2's so big converted levels fit - `src/game/core/memory_sizes.h`)

| Key | Default | THUG2's |
|---|---|---|
| `component_pool` | 184000 | 86000 |
| `struct_pool` | 49600 | 19800 |
| `vector_pool` | 19000 | 9500 |
| `pair_pool` | 1600 | 800 |
| `array_pool` | 16000 | 7000 |
| `symbol_pool` | 45000 | 12500 |
| `script_pool` | 1200 | 300 |
| `script_cache_pool` | 1200 | 300 |
| `string_heap` / `string_count` | 1 MB / 28000 | 190000 bytes / 7000 |
| `hash_item_pool` | 40000 | 10000 |
| `script_heap_extra_mb` | 16 | Extra megabytes for the script heap (0-256) |
| `node_hash_slots` | 65536 | Slots in the node-name hash table |
| `original_sizes` | 0 | 1 puts every pool back to THUG2's size |

**`[replacements]`** and **`[selftests]`** - development builds

| Key | Default | Effect |
|---|---|---|
| `[replacements] disable=` | (empty) | Leaves these as the original code: parts of source file names (`matrix.cpp`), function names (`Mth::FastSin`) or exact addresses (`0x00402640`), comma-separated; `*` disables everything |
| `[selftests] enabled` | 1 | 0 skips every self-test and shadow check |
| `[selftests] early` | 1 | 0 skips the early self-tests in the game (`thug2_check.exe` runs them offline) |
| `[selftests] shadow_calls` | 100 | For each REPLACE_SHADOW function, how many of the game's own calls also run the original to compare |
| `[selftests] filter=` | (empty) | Runs only the self-tests whose names contain one of these words |
| `[selftests] trace` | 0 | Logs each self-test as it starts (to find one that hangs) |

## Online play

Online goes through OpenSpy (`OnlineDomain=openspy.net`), the community replacement for the closed GameSpy service. By
default the game uses THUG2's own identity and lobby. `OnlineGameName=thugpro` (key left empty) joins THUGPRO's lobbies
instead; joining THUGPRO players' sessions may not work, as their scripts and DLL differ. The ported levels keep online
parity with THUGPRO's ports where both players must agree (spawns, crowns, flags, gaps, geometry they collide with):
that's what the compatibility register in `tools/pipeline/compat/thugpro` records, edit by edit. Level editor edits and
edited levels are never used online. Online play is only ever tested by hand: no tool, script or automation joins,
hosts or enters a lobby.

## Tracking down a problem

1. **Read the log** (`thug2-decomp.log` in the game folder). A crash is an `EXCEPTION 0xC0000005 at ...` report with
   registers and return addresses; debug-CRT reports and run-time check failures come with a call stack. `pool <name>
   full` means one of the game's fixed pools ran out (usually a converted level that's too big; see `[memory]`).
2. **Name the functions:** `node tools/symbolize.ts --pdb-dir <build folder> <path to thug2-decomp.log>` turns the
   last crash's addresses into function names and source lines. The PDB must come from the same build.
3. **Bisect** (development builds): list suspect source files, functions or addresses under `[replacements] disable=`
   and the original code runs instead. If the problem goes away, our version is at fault; if not, it's elsewhere
   (often converted data).
4. **Report it with the screenshot key:** it saves a PNG and a `.txt` with the level, mode, skater and camera
   positions, the editor's state and the log's last lines.

A level that crashes on load in only one ported game is usually a converter problem (`tools/pipeline/<game>_levels`).

## Contributing

### Decompiling

1. Load the reference build of `THUG2.exe` into a Ghidra project (base 0x400000) and let it analyse;
   `tools\ghidra\snapshot_project.ps1` makes the read-only copy the scripts use. Your machine's paths go in a
   `local.json` at the repo root (gitignored):

   ```json
   {
     "referenceExe": "D:\\path\\to\\THUG2.exe",
     "exeNames": [],
     "ghidraProject": "THUG2",
     "ghidraProgram": "/THUG2.exe"
   }
   ```

   `referenceExe` is the default for `thug2_check.exe` and the tools that read the original code; `exeNames` lists
   other exe names a game folder may have besides `THUG2.exe`; the Ghidra scripts open `ghidraProject` /
   `ghidraProgram`.
2. Regenerate `data\derived\` (not committed): `tools\ghidra\export_functions.py` (via pyghidra) for the function list,
   `node tools\extract_cfuncs.ts <exe> data\derived\cfuncs.csv` for the script command table.
3. Rewrite functions in batches (10-40 functions, or one feature, per commit). Our code comes only from our own
   decompilation of the game: never read, reference or copy leaked Neversoft / Activision source code.

### Adding a function

1. Decompile it in Ghidra and check its calling convention in the disassembly (some functions take arguments in
   registers: those get naked assembly stubs). Never trust Ghidra's return types or parameter counts.
2. Write it in the matching `src/game/<subsystem>/` file with a `// 0xADDRESS` comment.
3. Add `REPLACE(0xADDRESS, Namespace::Function);` - or `REPLACE_SHADOW(...)` if the function is pure, so the game's
   own calls get compared with the original (`src/hook/shadow.h`). Functions too small to hook are still written and
   registered with `REPLACE_SOURCE_ONLY`. These lines stay in the standalone build as the map from our code back to
   the original's addresses.
4. Add a self-test comparing it with `Original<Fn>(0xADDRESS)`, registered with `SELFTEST(Early|Late|EngineStarted, Name)`
   for the earliest stage where what it needs exists (`src/hook/selftest.h`), and prove the test catches mistakes with
   `node tools/mutate.ts` (deliberate bugs that must fail it).
5. Build and run the checks: `thug2_check.exe` (argument sizes against the original's `RET n`, and the early
   self-tests, offline), `node tools/check_code_writes.ts`, `npm run typecheck`, `npm run lint`, `npm test`.

### Tools

Tools are TypeScript (Node 24, no build step; conventions in `docs/typescript.md`); Python only for the Ghidra
scripts. Code from Morten1337's THUGPRO repositories is used with his permission for insight only, never copied as
is, and each borrowed idea is credited where it's used.

## Layout

| Path | Contents |
|---|---|
| `src/game/` | The game's code, by subsystem (see `docs/subsystems.md`) |
| `src/hook/` | Development builds: the replacement mechanism, logging, self-tests |
| `src/proxy/` | Development builds: loading into the game process |
| `tools/` | Checks, progress report, Ghidra / pyghidra scripts |
| `tools/pipeline/` | Submodule: level converters, menus, test runs, layouts, the THUGPRO compatibility register |
| `tools/anteara/` | Submodule: Anteara Mod, stored as edits to the game's scripts |
| `docs/` | Progress, the exe's map (`subsystems.md`, `library_ranges.md`), the script runtime, tool conventions |

## Glossary

| Term | Meaning |
|---|---|
| QB / QB script | Neversoft's compiled script format (`.qb`); NeverScript compiles and decompiles it |
| CFunction | A script command implemented in C++ (1254 of them) |
| Node array | A level's list of nodes: restarts, rails, triggers, objects, paths... |
| Sector / scene | A piece of a level's drawn geometry; the scene (`.scn.xbx`) holds them all |
| Collision (`.col.xbx`) | A level's collision objects (faces with flags: skatable, vert, wallride, trigger...) |
| `.pre` / `.prx` | Neversoft's packed archives (LZSS) |
| Data overlay | `Game\thug2-decomp-data`: our menus and ported levels, read before the game's own `Data` folder |
| Edited copy | A level with a level editor layout on top, offered separately as "<LEVEL> (EDITED)"; offline only |
| REPLACE / REPLACE_SOURCE_ONLY | A rewritten function and the original address it stands for (installed / not installed in development builds) |
| Reference build | The build of `THUG2.exe` whose addresses the code and tools use |
| TP-n / TPN-&lt;game&gt;-n / PC-&lt;game&gt;-n | The THUGPRO register's entries: THUGPRO's hand edits, THUGPRO's own content, our own deviations |

## Legal and credits

You need your own copies of the games; no game files, executables or data extracted from them are in this
repository, and the level converters generate everything from your installs at conversion time. Third-party code and
assets are credited in `THIRD_PARTY_NOTICES.md`: PartyMod (MIT, PARTYMANX), Morten1337's thps-formats (MIT) and the
libraries the build downloads. Leaked Neversoft / Activision source code is never read or used.
