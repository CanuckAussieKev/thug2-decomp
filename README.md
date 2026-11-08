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
matter. During the decompilation each rewritten function was checked against the original's behaviour, so the
source is a trustworthy base. On top of that base:

- **Bugs get fixed.** Crashes, hangs, memory limits and other faults found in the original are fixed in the source.
- **The game gets better:** PartyMod's fixes are part of it, plus a level editor, a new front end, graphics options,
  levels from the other Tony Hawk games and more (below) - with the original behaviour kept wherever it's what you
  play against: physics, scoring, scripts, online play with other players.
- **Changes are marked.** Code that isn't the original's behaviour carries a `NOT ORIGINAL` comment saying what it
  does and why, so the original stays readable underneath, and most additions can be turned off in
  `thug2-decomp.ini`.

## What's in it

- **The game's code** as readable C++, by subsystem (`src/engine/`, `src/game/` and the backends beside them): engine,
  renderer, script runtime, skater physics, goals, park editor, networking, front end.
- **PartyMod's fixes** ([PartyMod](https://github.com/PARTYMANX/partymod-thug2) v1.0.4 by PARTYMANX, MIT) as part of the
  source: an SDL window and input (SDL 3), controller and keyboard bindings, widescreen, the OpenSpy online service.
- **Additions:** a level editor, a new front end, graphics options, a loading console, a screenshot key with the
  game's state alongside, debugging aids.
- **Level converters** (installed by the [thug2-content-pipeline](https://github.com/CanuckAussieKev/thug2-content-pipeline)
  submodule: one repository per game, on
  [thug2-level-converter-core](https://github.com/CanuckAussieKev/thug2-level-converter-core) and on the converters of
  the games their game is built on - Project 8's on THAW's, Proving Ground's on Project 8's): THPS2, THPS3, THPS4,
  THUG, THAW, Project 8 and Proving Ground levels, generated from your own copies of those games - plus THAW's BMX,
  rideable in any level. What makes them agree with THUGPRO's levels for online play is
  [thug2-thugpro-compat](https://github.com/CanuckAussieKev/thug2-thugpro-compat) (the register and its data).

## Status

The whole game builds from this source: `thug2-decomp.exe` is compiled from `src/` alone, links the libraries the
original linked (D3DX9, the GameSpy SDK, the C runtime) instead of decompiling them, and runs on your copy's data
files - no part of the original program is used. It boots to the main menu and into free skate; it is new, so expect
rough edges in the parts nobody has played through yet, and report them (Tracking down a problem, below).

Next: the QB script interpreter is planned to be replaced by JavaScript (QuickJS) with the scripts decompiled to
readable JS, and the renderer to move off Direct3D 9 one day.

## Optional QB to JavaScript prototype

The optional [`scripting/`](scripting/README.md) target starts the planned QB to
JavaScript path: an offline TypeScript translator emits generators executed in
embedded QuickJS, with typed C++ host calls and frame waits. Synthetic tests
need no game files. This is an optional standalone prototype; the game build
is unchanged. See [the implementation plan](docs/qb-js-plan.md) for the
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
| THUGPRO (optional) | THUGPRO's additions to the ported levels, and online parity with its players | `%LOCALAPPDATA%\THUG Pro` | Read only, by reference (`thug2-thugpro-compat/thugpro`) |

Every folder you give a tool (`--thug2`, `--thps3`, `--thps4`, `--thug1`, `--thaw`, `--game`, CMake's `THUG2_GAME_DIR`)
may be the install root, with the game in a `Game` subfolder, or the game folder itself; the tools find either
(`thug2-level-converter-core/lib/install.ts`).

**Software:**

| Tool | Version | Used for |
|---|---|---|
| Visual Studio | 2026 (MSVC 14.51) with "Desktop development with C++", x86 tools (x64 tools for the 64-bit build), CMake and Ninja | The game |
| Node.js | 24 or later | Every tool in `tools/` (TypeScript, run directly: `node tools/<x>.ts`) |
| Git | any | Clone with `--recurse-submodules` (`tools/pipeline`, `tools/anteara`) |
| NeverScript | downloaded and built by `tools\build_neverscript.ps1` (docs/neverscript.md) | Compiling / decompiling QB scripts for the level converters |
| Python | 3.13 or later | The crash tools in `tools/standalone` (optional) |

The build downloads its libraries itself at configure time, each pinned and checked against a SHA-256: SDL3 3.2.30,
Microsoft's D3DX9 (the Microsoft.DXSDK.D3DX package), IGN's GameSpy SDK, Dear ImGui 1.91.9b, stb, LZHAM, and the
Inter 4.1 and JetBrains Mono 2.304 fonts (the level editor's UI).

## Build

1. In Visual Studio: **File > Open > Folder...** and pick this folder.
2. Choose the **x86 Debug** configuration in the toolbar.
3. **Build > Build All**. The build makes `thug2-decomp.exe`, the level editor's plugin,
   `plugins\thug2-level-editor.dll`, and its launcher, `thug2-editor.exe`, and puts `d3dx9_43.dll`, `SDL3.dll` and `gamecontrollerdb.txt` next to the exe (a
   Release build also the Visual C++ runtime's `vcruntime140.dll`, `msvcp140.dll` - and on 64-bit
   `vcruntime140_1.dll`); when `THUG2_GAME_DIR` (in `CMakeLists.txt` or the CMake cache, default
   `C:\Games\THUG2\Game`) is a game folder, all of them and the PDBs are copied there too (the plugin into its
   `plugins` folder).

From the command line:

```
npm install                          (once: the TypeScript tools' dependencies, the level converters among them)
tools\build_neverscript.ps1          (once: out\neverscript\ns.exe)
tools\verify_build.cmd               (builds out\build\verify\thug2-decomp.exe; nothing copied)
node tools/ffmpeg/build_ffmpeg.ts    (FFmpeg's DLLs: optional on 32-bit, needed on 64-bit - see "FFmpeg's DLLs" below)
npm run typecheck && npm run lint && npm test
```

The game builds as a 32-bit or a 64-bit program from the same source (docs/design/x64-port.md): the **x64 Debug** /
**x64 Release** configurations (`tools\verify_build.cmd x64`, out\build\verify-x64) make the 64-bit
`thug2-decomp.exe` and its plugin, with the 64-bit DLLs next to it (Running, below). The 32-bit build stays
the reference the 64-bit one is compared with.

Prefer a solution file? `cmake -S . -B out/vs -G "Visual Studio 18 2026" -A Win32`, then open `out\vs\thug2_decomp.slnx`
and build **Debug | Win32**. Builds are Debug: debug-CRT reports and run-time check failures go to `thug2-decomp.log`
with a call stack. The C runtime is a DLL (one heap for the exe and its plugin): a Debug build runs on the debug
runtime Visual Studio installs, so it runs on a machine with Visual Studio; a Release build carries its runtime DLLs.

## Running

The game runs from the game folder of your THUG2 install - the folder with `THUG2.exe` and its `Data` folder:

1. Copy `thug2-decomp.exe`, `d3dx9_43.dll`, `SDL3.dll`, FFmpeg's four DLLs (below), `gamecontrollerdb.txt` and - from a
   Release build - the Visual C++ runtime's DLLs from the build folder into the game folder, and the `plugins` folder
   (`thug2-level-editor.dll`: the level editor - without it the game runs with no editor) beside them, and
   `thug2-editor.exe` (the editor's launcher, below) - the build does it when `THUG2_GAME_DIR` points there - and the
   editor's `editor-tools` folder (Import Level's tools: `node tools/editor_tools.ts` makes it). Keep `thug2-decomp.pdb` with them for readable crash
   reports. `THUG2.exe` stays as it is: nothing of yours is changed.
2. Optional: a `thug2-decomp.ini` in the same folder (Settings, below; every key has a default).
3. Start `thug2-decomp.exe`. It writes `thug2-decomp.log` next to itself - read it when something goes wrong.
4. Or start **`thug2-editor.exe`** for the level editor on its own (below): it starts `thug2-decomp.exe -editor`
   and passes on what it's given (`thug2-editor.exe -edit BO` opens Boston).

The 64-bit build goes in the same folder the same way, in place of the 32-bit one (the files have the same names, so a
folder holds one build or the other): `thug2-decomp.exe` (+ `plugins\thug2-level-editor.dll`, the PDBs, a Release build's runtime DLLs), and from its own build
folder the 64-bit `d3dx9_43.dll` and `SDL3.dll`, `gamecontrollerdb.txt`, and FFmpeg's four 64-bit DLLs (below). Direct3D 9 comes with Windows; for Vulkan (the default renderer) the 64-bit Vulkan
runtime comes with the graphics driver. Its Vulkan caches are files of their own
(`thug2-decomp-vulkan-shaders-x64.bin`, `thug2-decomp-vulkan-pipelines-x64.bin`); the ini, the editor's layouts and the
data overlays are the same files for both builds.

### FFmpeg's DLLs

The movies and the sound streams are decoded by FFmpeg's Bink decoders (LGPL 2.1 or later; `THIRD_PARTY_NOTICES.md`). The
Both builds need them (the game's `binkw32.dll` is not used); without them the game starts, with no movies and no music or
sound streams, and the log says why. Build the four DLLs
(`avutil-59.dll`, `avcodec-61.dll`, `avformat-61.dll`, `swscale-8.dll`) from FFmpeg's release source:

```
node tools/ffmpeg/build_ffmpeg.ts --arch x64     (or --arch x86, the default; output in outfmpeg\<arch>in)
```

It needs Linux, or Windows with WSL (an Ubuntu distribution; `--distro` picks another), with the mingw-w64 cross compilers:

```
sudo apt-get install gcc-mingw-w64-i686-win32 gcc-mingw-w64-x86-64-win32 gcc make xz-utils
```

The build fetches the release tarball and checks its SHA-256. The CMake build copies the DLLs next to the exe; when you
copy the game by hand, put them there with the other DLLs. They are loaded by name at run time, so any LGPL build of the
same major versions can replace them.

## Playing

Useful start-up switches:

| How | What it does |
|---|---|
| `-level <name>` or `[features] start_level=<name>` | Skips the menus and starts that level in free skate. `<name>` is the level's folder name (`TR`, `BO`, `foun`, `z_ma`...). |
| `-edit <name>` or `[features] edit_level=<name>` | Starts the level editor (as `thug2-editor.exe -edit <name>`) and opens it in that level (without its plugin, `plugins\thug2-level-editor.dll`, the level opens in free skate). |
| `-level <name> -at x,y,z[,heading]` or `[features] start_at=` | Once the level is up, the skater stands at that spot, facing `heading` (degrees, 0 = +z, 90 = +x). |
| `node tools/game/edit_level.ts` | Lists every installed level by game; `edit_level.ts <name>` starts one in the editor. |

### The level editor

`thug2-editor.exe` (or `thug2-decomp.exe -editor`, `-edit <name>`, or `[features] editor_mode=1`). The editor is an
application of its own; the game itself doesn't offer it (its pause menu has no LEVEL EDITOR), but levels edited in it
play in the game (the level select's EDITED LEVELS). It edits in free skate only (never career / story, classic, the
park editor or network games).

- **The editor on its own:** `thug2-editor.exe` starts the editor as an application, **THUG2 Editor**, with its own
  taskbar button and icon. The game's menus never show, nor its music or loading screens: it opens on a Start page -
  recent levels and every installed level with its picture, edited copies included - and **File > Open Level** /
  **File > Recent** switch levels (asking about unsaved changes first); while a level loads a **Loading** box shows
  what is being read. **Play** plays from
  the level's start, **Play from Here** (F5) from the camera, both in the Scene panel; F5 stops. In play the pause
  menu has no CHANGE LEVEL or QUIT (the data overlay from `make_menu_level.ts` leaves them out). Escape doesn't leave
  the editor; **File > Exit** or closing the window quits, asking about unsaved changes. The window is resizable, never
  full screen (the ini's resolution and full screen are for the game), and opens where it was left
  (`Game\thug2-decomp-editor-window.ini`, written as each move or resize ends). `-editor -edit <name>` opens that level straight away. Without the
  plugin, `-editor` is logged and the game starts as usual.

- **Other games' levels:** **File > Import Level...** (also on the Start page) imports levels from your own install
  of THPS2, THPS3, THPS4, THUG, THAW, Project 8 or Proving Ground: **Browse...** to the game's folder (the game is
  told from its files and the folder remembered), tick levels, **Import**. The converters' output scrolls by in the
  window and **Cancel** stops them; afterwards the levels are on the Start page, a section per game, and open like any
  other. They go where the converters put them (the game folder's `thug2-decomp-<game>` and its data overlay; `[editor]
  import_output=` in the ini for another folder); the other game's folder is only read. It runs the tools in
  `editor-tools\` beside the exe - Node.js, NeverScript's `ns`, the content pipeline and its converters, the
  retail-limits DLL (docs/retail-limits.md) - which `node tools/editor_tools.ts` makes (`out\editor-tools`, about
  104 MB; it builds the DLL with `tools\build_retail_limits.cmd`) and `tools/game/deploy_build.ps1` copies in.
  Export to Retail Install puts the imported levels into a THUG2 install of the original game; "Install the limits
  DLL" puts the DLL beside its THUG2.exe with them, for levels too big for the original game's limits.
- **Learning it:** F1 lists every key; **Ctrl+Space** (or F3) searches every action; the status bar shows what the
  mouse and keys do at that moment. Every key is rebindable (Preferences > Keys, saved in `[editor_keys]` of the ini).
  The full list of controls and the editor's modules is at the top of `src/editor/level_editor.h`.
- **The window:** while the editor is open the game is the **Scene** panel, drawn at the panel's size, with the
  Outliner, the Inspector and the other windows docked around it - drag a tab to move, split, tab or float a panel;
  drag a panel out of the window and it becomes a window of its own on the desktop (on a second monitor, say; the
  Scene panel stays in the main window). **Window > Reset Layout** puts them back. The layout, the panels' own windows
  with it, is kept in `Game\thug2-decomp-editor.ini`. The editor always runs on Vulkan (`[Graphics] renderer=d3d9`
  is the game's alone). Play from here (F5)
  plays in the Scene panel (**Play > Maximise on Play** gives it the whole window); F5 goes back to editing. With the
  editor closed the game fills the window as ever.
- **Edited copies:** edits never change a shipped level. Saving (Ctrl+S) writes the level's layout,
  `Game\thug2-decomp-layouts\<scene>.txt` (plain text, one record per line), and the level select then has a separate
  entry, **EDITED LEVELS (OFFLINE)** > "<LEVEL> (EDITED)". Layouts are never applied online. Unsaved work is autosaved
  every minute and offered back after a crash; Reset level puts a level back as shipped.
- **Nodes and scripts it makes:** nodes the editor adds (spawns, crowns, flags, lights, rails, ladders, paths,
  triggers, cameras, goals...) go into an extra node array; trigger volumes, rail speed-ups, jump boosts, breakables
  and proximity nodes get small QB scripts generated at run time. Goals use THUG2's own Create-a-Goal records.
- **Baking a layout into a level's files:** `node tools/game/apply_layout.ts --level <name> [--dry-run]` writes a
  copy of the level's files with the layout built in, to `out\baked\<name>`; `--install --game <folder>` puts it in
  that folder's overlay. It warns about every edit that would break online parity with THUGPRO.

### Other games' levels (optional)

Each converter reads your install of that game and writes a staging folder next to THUG2; the menu tool then installs
everything into the data overlay and the level select. The converters are repositories of their own, which `npm
install` puts in `node_modules` (the pipeline's dependencies); `tools\pipeline\run.ts` runs a converter's tool by its
path in its repository:

```
node tools\pipeline\run.ts thps2_levels\thps2_levels.ts convert --thug2 C:\Games\THUG2 --thps2 C:\Games\THPS2
node tools\pipeline\run.ts thps3_levels\thps3_levels.ts convert --thug2 C:\Games\THUG2 --thps3 C:\Games\THPS3
node tools\pipeline\run.ts thps4_levels\thps4_levels.ts convert --thug2 C:\Games\THUG2 --thps4 C:\Games\THPS4
node tools\pipeline\run.ts thug1_levels\thug1_levels.ts --thug2 C:\Games\THUG2 --thug1 C:\Games\THUG1 convert
node tools\pipeline\run.ts thaw_levels\thaw_levels.ts   convert --thug2 C:\Games\THUG2 --thaw  C:\Games\THAW
node tools\pipeline\run.ts thp8_levels\thp8_levels.ts   convert --thug2 C:\Games\THUG2 --thp8  C:\Games\THP8
node tools\pipeline\thaw_bmx\convert.ts         convert --thug2 C:\Games\THUG2 --ns out\neverscript\ns.exe
node tools\pipeline\menu_level\make_menu_level.ts --game C:\Games\THUG2
```

THPS4 must be converted before `make_menu_level.ts` for the playable skate shop. Each
converter's README (`thug2-<game>-level-converter`: its `<game>_levels/README.md`) lists its switches; the THUGPRO compatibility register is
explained in `thug2-thugpro-compat/thugpro/README.md`. `make_menu_level.ts` empties and rebuilds the overlay each run.
`docs/level-port-compromises.md` lists what the ports do differently from the originals. Optional: Anteara Mod
(`tools/anteara/README.md`).

The biggest ported levels need more than the original game's limits (script pools, heaps, the node and rail name
tables). thug2-decomp.exe has them raised; for the original THUG2.exe, `src/retail_limits` builds a DLL that raises
them while it runs - `docs/retail-limits.md`.

## Settings (`thug2-decomp.ini`)

The ini sits in the game folder. Every key is optional; the default applies when it's missing. 1 = on, 0 = off.

**`[features]`** - our additions

| Key | Default | Effect |
|---|---|---|
| `skip_intro_movies` | 0 | Skips the logo and intro movies at start-up |
| `mute_audio` | 0 | Silences all sound from the first frame |
| `continue_music` | 0 | The song playing carries on through level loads (the skate shop, CAS and the park editor included) and run starts instead of being stopped or skipped; the playlist goes on when it ends. Skip, music volume 0, movies and cutscenes still stop it |
| `widescreen_menus` | 1 | 0 brings back PartyMod's pillarboxed 4:3 front end |
| `new_frontend` | 0 | The new front end: the game's own menus, restyled (0 = the classic menus) |
| `mouse_menus` | 1 | Mouse control of the menus (`debug_mouse=1` logs each click) |
| `cursor_by_device` | 1 | A controller button hides the mouse cursor and a key, mouse movement or click shows it again (where there is one; the level editor keeps it); the controller doesn't rumble while you play with the keyboard, until a button on it is pressed |
| `single_player_any_pad` | 1 | Outside split screen, every connected controller plays as player 1: the one a button was pressed on last (2-player games keep the controllers' seats) |
| `hd_ui` | 0 | The menus' 4x-upscaled images and fonts (when the overlay was made with `make_menu_level.ts --hd-ui 4`) |
| `editor_ui` / `debug_editor` | 1 / 0 | The level editor: `editor_ui=0` the old on-screen HUD instead of the Dear ImGui UI; `debug_editor=1` logs its picks and moves |
| `start_level` / `edit_level` | (none) | Start straight in a level / the level editor in a level (as `-level` / `-edit`) |
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
| `automation` | 0 | The test-automation channel (`tools/game/README.md`); never online |
| `debug_rail_speed`, `debug_collision`, `debug_sprites` | 0 | Extra log lines for those systems |

**`[debug]`**

| Key | Default | Effect |
|---|---|---|
| `collision_events` | 0 | Logs a `collision-event:` line each time the skater's velocity turns round on the ground (the physics path that did it, the face, probes) and shows it on screen for 2 s - for finding broken collision in converted levels; `node tools/game/collision_events.ts <log> [--col <level>]` groups them by spot |
| `collision_event_screenshots` | 1 | With `collision_events`: a screenshot of each event's frame too |
| `thaw_bmx_steer` / `thaw_bmx_wheels` | 0 | A log line each frame for the BMX's steering / its wheels |
| `soft_bounds` | 0 | A play-test aid: an index outside an array is logged once per place (`OUT OF RANGE (soft_bounds: carried on)`, with the calls that led there) and the game carries on with a zeroed element, instead of ending with a report |

**`[Graphics]`** - from PartyMod, plus ours

| Key | Default | Effect |
|---|---|---|
| `ResolutionX` / `ResolutionY` | 0 | The game's resolution in pixels (in a window: its inside); 0 = the desktop's |
| `dpi_aware` | 1 | On a display scaled above 100%, the window is the resolution's real pixels, sharp; 0 lets Windows scale it up (blurred, as before) |
| `Windowed` / `Borderless` | 0 | Runs in a window / a borderless window |
| `AntiAliasing` | 0 | Multisample anti-aliasing |
| `HQShadows` | 0 | High-quality shadows |
| `DistanceClipping` / `ClippingDistance` | 0 / 100 | Stops drawing beyond a distance (1-100) |
| `Fog` | 0 | Distance fog |
| `DisableBlur` | 1 | Keeps the motion blur off |
| `OcclusionCulling` | 1 | The original's occlusion culling; 0 draws everything in view |
| `anisotropy` | 0 | 2-16: anisotropic filtering on every sampler |
| `msaa` | 0 | 2 / 4 / 8 samples (the best the device has) |
| `renderer` | vulkan | `vulkan` or `d3d9`: `renderer=d3d9` runs the game on Direct3D 9. Without a Vulkan 1.3 device (dynamic rendering, push descriptors) or its loader, Direct3D 9 runs anyway, and the log says why |
| `vulkan_device` | -1 | On Vulkan: the GPU, by Vulkan's numbering (-1: the best, a discrete one first) |
| `vulkan_validation` | 0 | On Vulkan: 1 uses the Khronos validation layer when a 32-bit one can be loaded (its messages in the log) |
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
| `[Keybinds]` (PartyMod's keys) | PartyMod's | Keyboard bindings (SDL 2 scan codes, as PartyMod wrote them - they still mean the same keys); plus `Screenshot=` and `ThawBmx=` (an SDL key name or scan code) |
| `[Gamepad]` (PartyMod's keys) | PartyMod's | Controller bindings by button number: 0 A, 1 B, 2 X, 3 Y, 4 Back, 5 Guide, 6 Start, 7 / 8 left / right stick, 9 / 10 left / right shoulder, 11-14 d-pad up / down / left / right, 15 Misc1 (Share, Capture, Mic), 16-19 paddles (right 1, left 1, right 2, left 2), 20 touchpad, 21 / 22 right / left trigger, 23-27 Misc2-Misc6 (extra buttons some controllers have); -1 none |
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
| `gizmos_behind` | 1 | Rails and trigger volumes behind geometry (View > Gizmos Behind Geometry): 0 X-Ray, 1 Faded, 2 Hidden |
| `rail_range` | 4000 | Distance at which rails, ladders and paths have faded out |
| `ui_scale_pct` | 0 | UI scale (%); 0 = automatic for the window size |
| `autosave_s` | 60 | Seconds between autosaves |
| `snap_move` | 25 | Grid size for snapped moves |
| `snap_turn` | 15 | Angle step for snapped turns |
| `accent` | 0 | The UI's accent colour: 0 orange, 1 blue, 2 green, 3 purple, 4 red, 5 teal |
| `wasd_flies` | 0 | 1 = WASD always flies; 0 = fly while the right mouse button is held |
| `speed_by_selection` | 0 | Fly speed scaled to the selection's size |
| `pad` | 0 | Controller mode |
| `tips_seen` | 0 | The first-run tips have been shown |

**`[memory]`** - the script pools' sizes (bigger than THUG2's so big converted levels fit - `src/engine/memory/memory_sizes.h`)

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

## Online play

Online goes through OpenSpy (`OnlineDomain=openspy.net`), the community replacement for the closed GameSpy service. By
default the game uses THUG2's own identity and lobby. `OnlineGameName=thugpro` (key left empty) joins THUGPRO's lobbies
instead; joining THUGPRO players' sessions may not work, as their scripts and DLL differ. The ported levels keep online
parity with THUGPRO's ports where both players must agree (spawns, crowns, flags, gaps, geometry they collide with):
that's what the compatibility register in `thug2-thugpro-compat/thugpro` records, edit by edit. Level editor edits and
edited levels are never used online. Online play is only ever tested by hand: no tool, script or automation joins,
hosts or enters a lobby.

## Tracking down a problem

1. **Read the log** (`thug2-decomp.log` in the game folder). A crash is an `EXCEPTION 0xC0000005 at ...` report with
   registers and return addresses; debug-CRT reports and run-time check failures come with a call stack. `pool <name>
   full` means one of the game's fixed pools ran out (usually a converted level that's too big; see `[memory]`).
2. **Name the functions:** `node tools/symbolize.ts --pdb-dir <build folder> <path to thug2-decomp.log>` turns the
   last crash's addresses into function names and source lines, for any module of the report that has its file (and PDB)
   in that folder - a 32-bit or a 64-bit build alike. The PDB must come from the same build.
3. **Without a log line:** `python tools/standalone/first_fault.py <exe> <game folder>` runs the game under the
   debugger API and reports its first fault with registers, the stack and symbol names (a 32-bit or a 64-bit exe: the
   file's machine decides; `--watch <rva> [--watch-size 1|2|4|8]` reports each write to an address).
4. **Report it with the screenshot key:** it saves a PNG and a `.txt` with the level, mode, skater and camera
   positions, the editor's state and the log's last lines.

A level that crashes on load in only one ported game is usually a converter problem (`thug2-<game>-level-converter`).

## Contributing

Code from Morten1337's THUGPRO repositories is used with his permission for insight only, never copied as is, and
each borrowed idea is credited where it's used.

- **Changing the game:** the original's behaviour is the default. A change that differs from it carries a
  `NOT ORIGINAL` comment saying what it does and why, and an ini switch where a player might want the original back.
  The `// 0x...` comment above a function is the original's address it was decompiled from - handy with a
  disassembler open on your own copy of `THUG2.exe`.
- **Checks before a commit:** the game builds (`tools\verify_build.cmd`), `npm run typecheck`, `npm run lint`,
  `npm test`.
- **Tools** are TypeScript (Node 24, no build step; conventions in `docs/typescript.md`); Python only for the crash
  tools.

## Layout

| Path | Contents |
|---|---|
| `src/` | The program, in layers (each a CMake library; `docs/architecture.md`): `base` (checksums, formatting, maths), `platform` (the window, crash reports, the GameSpy glue), `formats` and `wire` (the game's files and network messages), `engine` (memory, files, assets, scripts, objects, the renderer, audio, network transport, pad input, the original's screen elements), `game` (skating, the rider, goals, parks, saves, the online session, the game's script commands), `frontend` (the new front end), `editor` (the level editor: a plugin DLL the exe loads, `plugins\thug2-level-editor.dll`), `devtools` (screenshot key, test-automation channel) |
| `src/render_pc/`, `gpu_d3d9/`, `audio_dsound/`, `media_ffmpeg/`, `input_dinput/`, `fs_win32/`, `net_winsock/`, `script_qb/` | The backends behind the engine's interfaces: Direct3D 9, DirectSound, FFmpeg's movie decoders, DirectInput, Win32 files, Winsock, the QB script interpreter |
| `src/retail_limits/` | Not part of the program: `dinput8.dll` for the original THUG2.exe, raising its limits for large levels at run time, and its offline check (`docs/retail-limits.md`) |
| `src/app/` | The program's start-up (`main.cpp`, WinMain, the main loop, the command and component tables) and its extension list; `thug2-decomp.exe` |
| `src/tests/` | The offline checks (file formats, maths against D3DX, number parsing, printf-style formatting) |
| `tools/` | The crash tools, symbolize, NeverScript's build, the script-table extractors |
| `tools/game/` | The scripts that run and drive the game for tests, the layout baker, the level editor's offline tools |
| `tools/pipeline/` | Submodule: the data overlay and menus, HD art, THAW's BMX; its dependencies are the level converters and their core (with the THUGPRO compatibility register) |
| `tools/anteara/` | Submodule: Anteara Mod, stored as edits to the game's scripts |
| `docs/` | The script runtime, the GameSpy SDK notes, the level ports, tool conventions, the front end's design |

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
| Reference build | The build of `THUG2.exe` whose addresses the `// 0x...` comments give |
| TP-n / TPN-&lt;game&gt;-n / PC-&lt;game&gt;-n | The THUGPRO register's entries: THUGPRO's hand edits, THUGPRO's own content, our own deviations |

## Legal and credits

You need your own copies of the games; no game files, executables or data extracted from them are in this
repository, and the level converters generate everything from your installs at conversion time. Third-party code and
assets are credited in `THIRD_PARTY_NOTICES.md`: PartyMod (MIT, PARTYMANX), Morten1337's thps-formats (MIT) and the
libraries the build downloads.
