# Architecture

## Layers

The game is one executable (`thug2-decomp.exe`, CMake target `thug2`) built from `src/`, in layers. A layer may
include the layers below it, never the ones above. Each layer but `app/` is a static library, `thug2_<folder>`,
defined in its folder's `CMakeLists.txt`; `app/` is the exe, which links them all.

```
app/                        main, WinMain, the start-up, the game's main: the composition (may include anything)
editor/  frontend/  devtools/   the level editor; the new front end and mouse menus; the developer tools
game/                       skating, the rider, goals, parks, saves, the online session (game/online)
render_d3d9/                the graphics backend: Direct3D 9 (the original's PC platform layer); only app/ includes it
audio_dsound/  media_bink/  the sound backend (DirectSound) and the movie codec (Bink); only app/ includes them
input_dinput/               the original's DirectInput side, an input source; only app/ includes it
engine/                     memory, lists, files, assets, script, objects, renderer, audio, media, network transport, pad input,
                            ui (the original's screen elements), ext (the extension points)
-------------------------   the boundary: where bytes come from outside the game
formats/  wire/             file formats; network messages
platform/                   Win32, the SDL window, the GameSpy SDK, crash reports, code addresses
-------------------------
base/                       checksums, formatting, text, checked indexing, maths, the bit stream (no OS, no SDK)
```

**The base layer** (`src/base`, static library `thug2_base`) is what every layer uses and nothing of the OS: `core`
(checksums, printf-style formatting, text, number parsing, checked indexing, the enum, cast and pointer helpers, the
compiler's few names), `maths` (vectors, matrices - their product, the vector transforms and the viewing matrices are
D3DX's in plain C++, bit for bit, checked by `thug2_maths_check` -, quaternions, shape tests), `lists` (two containers
that don't use the game's heaps) and `util` (the bit stream, BPS patches). It includes only the standard library and GSL
(and the compiler's SSE intrinsics: the reciprocal estimates D3DX's normalising uses). What it can't do itself it hands
to a sink the platform installs first thing in WinMain (`platform/output_sinks.h`): the debugger output
(`base/core/dbg.h`) and error lines into thug2-decomp.log (`base/core/log_sink.h`). The game's heaps are `engine/memory`
(they use the run log, the C runtime's heap and the game config), and what is built on them stays in the engine
(`engine/lists`' chains and maps, `engine/util`'s LZSS compressor and text store).

**The platform layer** (`src/platform`, static library `thug2_platform`) is the OS and the SDKs, and includes only
`base/` and their headers. It doesn't call up: what the game does on the platform's events, and where it keeps the
platform's devices, is wired by `app/`. The SDL event pump calls the handlers registered with it (the quit request is
the start-up's, `app/pc_setup.cpp`; the window's focus WinMain's); `sys::create_sdl_window` returns the window's
handle, which WinMain keeps where the platform's window is (`sys::game_window`); the sound output WinMain opens through
`audio::Device` and keeps in the audio's state; the base layer's sinks are installed by `app/main.cpp`. What uses the engine's state is the engine's: the frame
clock (`engine/sys/stopwatch`, `game_clock`), the input set-up and the tables the original's input read with - the
ports' controls, the key names, the registry options (`engine/pad_input/input_setup`, `port_controls`); the
original's DirectInput side itself is a backend (`src/input_dinput`, "The input seam" below).

**The composition** (`src/app`): `main.cpp`, WinMain (`winmain.cpp`: the command line, the window, the devices),
PartyMod's start-up (`pc_setup.cpp`) and the game's main (`startup.cpp`: the singletons, modules and the main loop).
It may include every layer. `src/devtools` sits beside the editor and the new front end: the screenshot key, the
test-automation channel (`automation`), the restart tester, the start level and the test view.

**The extension points** (`engine/ext/extension.h`): the engine and the game never call the editor, the front end or
the dev tools. Where something we added runs inside the original's frame, the original code calls `ext::` at that
place - the front end's input logic (`ext::menu_frame`), the 3D frame (`before_draw_world`, `place_camera`,
`near_plane` / `view_angle`, `view_drawn`, `draw_over_frame`), the main loop (`before_present`, `keeps_every_frame`,
`device_lost`), the screen elements' layout pass, the input set-up and poll (`prepare_input`, `game_owner` and its
`pass_pause_key`, `game_input_blocked`; the buttons a dev tool presses are an input source's, "The input seam"), a scene
loaded, the data overlay, the start-up's movies,
the career save and the high scores - and `ext::` calls each registered `ext::Extension` in turn. An extension
overrides only its points (the defaults do nothing or pass the value on); a point that returns a value keeps the
contract its one caller had (the first extension that owns the game takes the keyboard but for the Pause key; the
near plane and field of view are passed from one extension to the next, starting from the original's). The grain is
a few calls per frame, viewport or event. `app/extensions.cpp` is the exe's composition: it registers the extensions
in the one order that is every point's call order (the mouse menus, the restart tester, THAW's BMX, the editor, debug
mode, the start level, automation, the test view, the new front end, the screenshot key) and registers their script
commands where the start-up did. An exe of the editor alone would register only the editor (and the game features it
wants). Our NOT ORIGINAL game and engine features that run at those points (THAW's BMX, debug mode) are extensions
too; what the editor drives inside the game stays in the game (`game/skate/edited_copy_career`: the edited copy
kept out of the career save, its gaps asked of the extensions).

**Where the UI lives** (what `frontend/` held was three things): the original's screen elements, which the game's HUD
and menus use (`screen_element*`, `text_element`, `text_block`, `menu_element`, `sprite_element`, `scrolling_menu`,
`model_widget`, `theme_cfuncs`, `screen_constants.h`), are `engine/ui` (they include no game header); the original's
pause / menu module is `game/front_end`, CREATE 100% CAS's commands `game/commands`, and the data overlay (a file
system source) `engine/file/data_overlay`. `src/frontend` is now the new front end and the mouse menus only. The
namespaces are the ones the files had (`frontend::` for all of these).

**Where the network code lives**: `engine/net` is the transport only (the app, connection, dispatcher, manager and
client objects, message framing and compression: `net_*`, `shared_state.h`); it includes no game header. The
online session - the GameSpy lobby, rooms, buddies, the content vault, stats, the player roster, the game-flow
messages and handlers, the THUGPRO peer mode and guards - is `game/online` (74 files, namespaces `online::` and
`net::` as they were). The two things the transport asks of the session are hooks set by the exe before any original
code runs (`online::install_transport_hooks`, game/online/transport_hooks.h): the peer-mode question
(`net::set_peer_mode_query`) and the check on received messages (`net::set_received_message_check`). The rest of
the engine asks the session through `net::SessionQueries` (below); the text-entry flag the session sets is the
input's (`sio::text_entry_flag`), and the handler that does nothing is the dispatcher's (`net::handle_nothing`).

**Where the script commands live**: the game's script commands (CFunctions) that read the skater, the session, the goals or the
parks - `game_cfuncs`, `game_flow_cfuncs`, `game_util_cfuncs`, `trick_cfuncs`, `world_cfuncs`, `net_state_cfuncs`,
`node_object_cfuncs`, the create-a-graphic and face-texture commands, and the code half of the member-function tables
(`member_function_code`) - are `game/commands`; the commands that only use the engine stay beside it in
`engine/script`, `engine/gfx`, `engine/objects`, `engine/ui` and `engine/sound`. The interpreter and the script data
stay in `engine/script`. The command table itself - the 1,254 commands and the 250 member-function names, in the
original's order, which is a command's identity in the script symbol tables - is composed in `app/command_table.cpp`,
the one place that sees every library, and handed to the script engine before any original code runs
(`app::install_command_tables`; `script::cfunction_table()` is the engine's view of it). The start-up initialisers'
sequence (`app/startup_init.cpp`) is in `app/` for the same reason. The engine's two uses of node commands (the time of
day creating an object from a node, the colour cycles setting a node's colour) go through hooks installed by
`commands::install_node_hooks` (`engine/script/node_hooks.h`).

**Where the object components live**: `engine/objects` is the object framework (the entity, its modules and their
manager, the model, skeleton, animation blender, sound, collision, motion, rigid body, input, camera, cutscene and
node components) and the components that need nothing of the game. The components that are the game's own - the
drivable vehicle (`vehicle_*`), the peds (`ped_logic*`, `avoid`), the projectiles, the trick objects and the park
editor's camera - are `game/world`; the walk component's hanging and ladder code is `game/rider/walk_hang` beside the
rest of the walk component (its interpolation helper, which animation uses too, stays in `engine/objects/walk_lerp`).
Namespaces are as they were (`objects::`). The component registry - the 81 component types in the original's order,
which `spawn_module` searches - is `app/component_types.cpp`, called by the start-up. `engine/objects` includes nothing above the
engine: what the framework asks of the game - the skate module's players, level, movie managers and viewer, a node left
out of the game mode, the front end's controllers, the park editor's state, the skater's flip component, a
projectile's hit, the trick objects' clusters, the walk component's ladder and rail checks, THAW's BMX - goes through
`objects::GameHooks` (`engine/objects/game_hooks.h`: function pointers, each the game's own code, installed once by
`objects::install_game_hooks`, game/world/object_hooks.h, from `app/main.cpp`). What the engine asks of the online
session - whether a network game is on (on the objects' copy of the network manager's pointer, or on the manager as it
is now), the session's client apps, the client app of this machine's player of an object - is `net::SessionQueries`
(`engine/net/session_query.h`, installed by `online::install_session_queries` at start-up where the original kept the
objects' copy of the network manager's pointer): the objects, the interpreter, the animation channels and the terrain
sounds use it. The viewport count (`modules::viewport_count`) is `engine/gfx/viewport`'s, beside the screen mode that
sets it.

**The engine includes nothing of the game.** Each engine subsystem that asks the game something has its own small
set of hooks, named for the subsystem that asks - not one set for the whole engine - and each is installed once from
`app/main.cpp` before any original code runs, by an installer on the game's side that may include any game module:

| asks | hooks | installed by | what |
|---|---|---|---|
| engine/objects | `objects::GameHooks` (`engine/objects/game_hooks.h`) | `objects::install_game_hooks` (game/world/object_hooks) | above |
| engine/gfx | `render::GameHooks` (`engine/gfx/game_hooks.h`) | `render::install_game_hooks` (game/skate/render_hooks) | the career level (the screen's aspect), multiplayer, the game mode's name, the front end's pause, the park editor's shell (the time of day) |
| engine/pad_input | `sio::GameHooks` (`engine/pad_input/game_hooks.h`) | `sio::install_game_hooks` (game/front_end/pad_hooks) | the front end's menus up, its input port, the park editor editing, THAW's BMX half-stick key |
| engine/sys | `sys::GameHooks` (`engine/sys/game_hooks.h`) | `sys::install_game_hooks` (game/skate/sys_hooks) | the main loop's GameSpy service, THUGPRO's position scale and the rail debugging after the render; the front end's pause for debug mode |
| engine/render | `render::set_feeler_query_observer` (`engine/render/probe_ray.h`) | `collision_overlay::install` (game/rider/debug_collision) | `[features] debug_collision`'s log after each feeler query |
| engine/net | `net::SessionQueries`, the transport hooks | `online::install_session_queries` (at start-up), `online::install_transport_hooks` | above |
| engine/script | the node hooks (`engine/script/node_hooks.h`) | `commands::install_node_hooks` | above |

Each hook is the game's own code that the engine called at that place: one indirect call for a direct one, none added
per object, draw or poll. Some things moved down instead, because they need only the engine: the CAS option lists
(`engine/gfx/cas_options`), THUGPRO's CAS parts from the user's install (`engine/file/thugpro_cas`), the viewer's
pending script (`engine/sys/model_viewer`). `tools/layers.json` lists the engine (with base and platform) as strict:
no upward include, not even in the baseline.

**The rule is checked.** `tools/layers.json` maps each folder to a library and lists the libraries it may include; `node tools/layers.ts` (part of `npm test`) fails on any include outside the map that is not in `tools/layers_baseline.txt`, and on any baseline line that no longer occurs - the baseline can only shrink. The editor is its own layer above `game`, beside the front end: neither may include the other. A file that only needs the name of a class takes it from the owning subsystem's `fwd.h` (`engine/script`, `engine/objects`, `game/online`, `engine/memory`, `base/maths`), not from a header of a higher layer.

**The libraries link as the map says.** Each library links only the ones its layer may include (`PUBLIC`: their
headers are part of its own), and reaches every one of them through those links:

```
thug2_base  <-  thug2_formats  <-  thug2_wire
            <-  thug2_platform
thug2_platform + thug2_wire  <-  thug2_engine  <-  thug2_game  <-  thug2_frontend, thug2_editor, thug2_devtools
thug2_engine  <-  thug2_render_d3d9, thug2_audio_dsound, thug2_media_bink, thug2_input_dinput (the backends: linked by
                  the exe alone)
every library  <-  thug2 (src/app: thug2-decomp.exe)
```

The checks in `src/tests` link only what they check (`thug2_format_check`: wire and formats; `thug2_maths_check`:
base; `thug2_parse_check` / `thug2_printf_check`: formats). Third-party and system libraries are linked by the
library that calls them (SDL, Winsock, D3D9 and the GameSpy SDK by the platform; LZHAM by the engine; DirectInput
by the input backend; DirectSound by the sound backend; D3DX by the graphics backend; imgui's core by the graphics backend - the loading console's
panel -, the editor and the front end, and imgui's SDL2 and Direct3D 9 backends, `imgui_backends`, by the graphics
backend alone). `node tools/cmake_layers.ts` (part of `npm test`)
reads the `CMakeLists.txt` files and fails when a library links outside the map or doesn't reach a library the map
allows it, when a source sits in another library's folder, or when a `.cpp` is compiled by no target or by two.
The one exception is the editor / front end / dev tools' sibling edges (round 29's): they link each other exactly
where `tools/layers_baseline.txt` still has include lines between them - static libraries that need each other, which
CMake repeats on the link line for a linker that reads each library once (GNU ld) - and a sibling link with no
baseline line left fails. A sibling's own links are its own: the front end linking the editor doesn't count as
reaching what the editor links. Only the exe links the backends: the editor and the front end reach the graphics
backend through the engine's interfaces (`render::Device`, `render::UiBackend`), the engine reaches the sound output
and the movie codec through `audio::Device` and `media::Movie`, and DirectInput through its `input::Source`.

Nothing is registered by a static initialiser: the start-up runs explicit tables (app's start-up initialisers, the
command table, the component registry), so the linker drops no object the game needs (a static library leaves out an
object nothing refers to). The project's only dynamic initialisers are four list-head markers and one callback
pointer, each used in its own file. Each library keeps its sources in the order the exe listed them before the
split; the exe's symbols were compared before and after.

**The boundary layer** is where the game meets bytes it doesn't lay out itself. Only there do byte layouts matter;
everything above it uses the game's own types and may change them freely.

- `formats/` (static library `thug2_formats`): a reader - and, where the game writes the format, a writer - for each
  file the game loads or saves (and, for the files used in place - collision, animation keys - the accessors that read
  their records, e.g. `collision_format::FaceTable` for a collision object's faces, `relocate_object` /
  `object_record` / the tree's link accessors for its fix-up in place): PRE archives and their LZSS (`pre_format`, `lzss`), cutscene bundles, texture
  dictionaries and images, scenes / models / skins (`scene_format`), collision, skeletons, animations, fonts, sound
  effects, the streamed sounds' index, the park map and its rails, the save file. Each takes a byte span
  (`ByteReader` / `ByteWriter`, `byte_stream.h`: little-endian, unaligned, bounds-checked) and gives plain records.
  `thug2_format_check` (`tests/`) runs every reader over a game folder, compares it with an independent
  reading of the bytes, and round-trips the writers.
- `wire/` (static library `thug2_wire`): the packet framing (`net_wire`) and every game message
  (`gamenet_messages`, `skater_messages`, `play_messages`, `skate_net_messages`), byte-compatible with the
  original's and THUGPRO's peers. Encoders and decoders over the same byte stream; a short or oversized message is
  refused, not read past.
- **The graphics backend** (`src/render_d3d9`, static library `thug2_render_d3d9`, on the engine) is the original's PC
  platform layer: the engine's `s_plat_*` calls and what they reach (0x004B2270-0x004E0A90 in the reference build), the
  Direct3D files of our own and the GPU buffers. It implements `render::Device` (`engine/gfx/render_device.h`); the
  engine side (the engine's own graphics, the objects, scripts, UI, file loading, the game, the dev tools) asks it for
  everything through that: the frame (begun, drawn, finished), a lost device's reset in the main loop's steps, the
  resources it makes and gives back (scenes, meshes, models, geoms, sprites, primitive drawers, render targets, texture
  dictionaries and projections, viewports, particle systems, fonts), the state blocks (fog, gamma, the 2D clip, the
  primitives' states, the world's lights), the effects a batch at a time (a flash, a shatter, a splat set, a texture
  shadow, the sun's occlusion query) and what is shown (screenshots, the back buffer's copy, the disc error and loading
  screens). A call per frame, resource, state block or batch - never per vertex or object: what the engine does per
  vertex (a primitive drawer's vertices: `VertexRun`, `engine/gfx/vertex_run.h`, writes them inline; a splat
  triangle's collapse, `engine_splats.h`) or per object (the occluders and sphere tests, `engine/gfx/occlusion.cpp`:
  CPU culling) is the engine's own code. The device takes and returns the engine's own types (`WorldScene`,
  `SubMesh`, `TextureSet`, `ViewRegion`, `Shatter`, `VertexRun`...); the backend's objects derive from them. The
  loading console's panel, which the backend draws over the loading screen, is reached through four hooks
  (`gfx::loading_console::install_panel`). `app/main.cpp` installs Direct3D 9's (`render::d3d9::device()`,
  `render_d3d9/d3d9_device.cpp`: each method is the platform layer's own function) before any original code runs;
  another backend (Vulkan) would be a second implementation chosen there. Nothing below `app/` includes or links it
  (tools/layers.json: render_d3d9 is strict and only the app may include it), so the engine, the game, the editor and
  the front end build without Direct3D.
  - What our level editor asks of it (`render::Device`, "what our level editor asks"; `render_d3d9/editor_support.cpp`):
    whether the device is made, its view modes (`engine/gfx/view_modes.h`), its old HUD's lines, a PC geom's meshes,
    mesh array, instance and active flag, the buffers its ground tool makes and their upload and release, and the Add
    palette's preview (the camera, a geom drawn, the frustum put back). The editor reads and edits a sector's meshes
    through the PC geometry's records - `engine/gfx/mesh_records.h`: a vertex buffer's, an index buffer's, a mesh's and
    the first part of an instance's, the original's layouts; the backend's `VertexBuffer`, `IdxStore`, `MeshRef` and
    `Singleton` derive from them and add what they do.
  - `render::UiBackend` (`engine/gfx/ui_backend.h`): what our Dear ImGui layers - the editor's UI and the new front
    end - ask of it, a frame at a time: ImGui's platform and renderer backends started on a context, a frame begun and
    its draw data drawn, the device objects dropped before a reset, the images ImGui shows (made from texels in memory)
    and the two targets the renderer draws into for the UI (the preview, the thumbnails' atlas). Direct3D 9's
    (`render_d3d9/d3d9_ui_backend.cpp`: ImGui's SDL2 and Direct3D 9 backends) is installed by `app/main.cpp` beside the
    device; a Vulkan one would be ImGui's SDL2 and Vulkan backends behind the same calls. The editor's
    `level_editor_ui_backend` keeps what is the UI's own (whether a context is started, the log, the draw data scaled to
    the back buffer). Only `render_d3d9/` includes ImGui's backend headers (tools/d3d_headers.test.ts).
  - D3DX: only the backend calls it (`render_d3d9/d3dx.cpp`: the pixel shaders' assembly and the front buffer's JPEG).
    Its maths - the vector transforms and the viewing matrices - is plain C++ in `base/maths/view_transforms.h`, the
    same float operations in the same order as the D3DX9 the game ran with (thug2_maths_check compares each with
    d3dx9_43.dll; bit for bit with the x87 at 53 bits, the precision the game's thread runs at - the frames drawn
    with the builders worked at 24 bits differed from D3DX's in their last bits -, but the matrix inverse, whose one
    caller doesn't use the result).
  - Its vertex and index buffers (`vertex_buffers`): made, filled through their copies in memory, written again after
    a device reset, torn down. Code outside the backend reads and writes vertices by attribute through
    `gpu::VertexView` / `VertexWriter` (`engine/gfx/vertex_view.h`: a stride and offsets, never the bytes themselves).
  - The graphics handles (`engine/gfx/gpu_handles.h`): what a header outside Direct3D's own files holds of the graphics
    API's objects - `render::GpuDevice`, `GpuTexture`, `GpuSurface`, `GpuVertexBuffer`, `GpuIndexBuffer`,
    `GpuInstance` -, pointers to incomplete types whose value is the object's own pointer (no wrapper or table: a
    handle costs what the original's pointer did). The backend converts them (`render_d3d9/d3d9_handles.h`:
    `d3d9::object`, `handle`, and `out` for Direct3D's out-parameters). `<d3d9.h>` is included only by Direct3D's files
    and their own headers: `render_d3d9/d3d9_state.h` (the renderer's Direct3D objects - Direct3D itself, the device,
    the depth surfaces, the back buffer, the screen copies -, `render::d3d9::state()`; the backend-free rest of the
    renderer's state is `render::Renderer`, `engine/gfx/renderer.h`), `device_d3d.h`, `pc_d3d_resources.h`,
    `d3d9_handles.h`, `d3d_matrix.h` (Direct3D's matrices over the game's), `render_d3d9/d3dx_jpg.h` and ImGui's
    texture id over Direct3D's textures (`render_d3d9/imgui_texture_id.h`).
- **The audio backend** (`src/audio_dsound`, static library `thug2_audio_dsound`, on the engine) is DirectSound. It
  implements `audio::Device` (`engine/audio/audio_device.h`): the output made and released (WinMain; the platform
  layer's 0x005F59D0 / 0x005F5A00), its devices listed (the graphics device's start-up notes the last one), and the
  sound effects' voices (`engine/sound/sfx_voices.cpp`, the original's PC driver) - a buffer made from the wave's
  format and description, its 3D controls, filled whole, played from the start, stopped, its status read, its volume,
  pan and frequency set, its 3D range and place; the listener made, faced and committed. A call per voice or per
  listener update, never per sample; the units are the game's (hundredths of a decibel, samples a second). Each method
  is the DirectSound calls the original made at that place, in the same order. The output, the buffers, their 3D
  controls and the listener are handles (`engine/audio/audio_handles.h`: `audio::SoundOutput`, `SoundBuffer`,
  `SoundBuffer3d`, `SoundListener`, the objects' own pointers; `audio_dsound/dsound_handles.h` converts).
  `app/main.cpp` installs DirectSound's (`audio::dsound::device()`), or with `[features] mute_audio=1` the muted
  output over it (`engine/audio/muted_device.h`): the null output, which forwards every call but the three that could
  be heard - the audio session muted while the output is open (`platform/mute_audio.h`), every voice's volume silent,
  every movie's and stream's track at 0 (`audio::Device::track_volume`) - so the voices still play and end as with
  sound. Only `audio_dsound/` includes `<dsound.h>` (tools/audio_headers.test.ts).
- **The movie codec** (`src/media_bink`, static library `thug2_media_bink`, on the engine) is Bink: the player's own
  binkw32.dll, each export looked up by name on first use (`media_bink/bink.h`). It implements `media::Movie`
  (`engine/media/movie.h`): the movies (`render_d3d9/movie.cpp`) and the music and sound streams
  (`engine/audio/bink_stream*.cpp`, sound-only Bink files from a path, a file handle or memory) open, decode, step,
  seek and pause through it and set their tracks' volume and pan; an open movie is its header
  (`media::MovieHeader`, the fields of Bink's BINK the game reads). Its sound plays into the sound output
  (`use_sound_output`: the output's handle - Bink's DirectSound output on the game's DirectSound object). A call per
  movie or per frame. `app/main.cpp` installs Bink's (`media::bink::movie()`); only `media_bink/` includes the
  wrapper (tools/audio_headers.test.ts).
- **The input seam** (`engine/pad_input/input_source.h`, `input_event.h`): where the game's input comes from. An
  `input::Source` is one kind of device; `app/main.cpp` adds them in the order the original read them, which is the
  call order at every point: the original's DirectInput side (`src/input_dinput`, static library
  `thug2_input_dinput`, on the engine: `input::dinput::keyboard()`), SDL's game controllers and keyboard
  (`engine/pad_input/sdl_input.cpp`, PartyMod's input: `sio::sdl_source()`), then the test-automation channel's
  pressed buttons (`devtools/automation`, a dev tool's source: nothing pressed unless the workspace turns it on).
  - The frame: the main loop's first step (`sio::read_keyboard`, 0x005BDD60) forgets the typed characters and has
    each source read its devices (`read_frame`: DirectInput's keyboard state into its two buffers); then the SDL
    window's events are pumped (`os_layer::process_window_events`, as before).
  - A port's poll (`sio::InputPort::work_on`, once a frame for each port): the device made ready, each source's
    `begin_poll` (SDL's: the menu flags, then the pending events - pumped again here, once a port, as PartyMod did),
    the control data cleared, port 0 usable and connected, each source's `poll_port` adding its buttons, sticks and
    pressures to the port's control bytes (`input::PortPoll`; SDL's: port 0's keyboard, then the port's controller;
    then the automation channel's buttons), the bits inverted. While an extension blocks the game's input
    (`ext::game_input_blocked`, the new front end) the keyboard and the controllers give nothing; the automation
    channel's buttons still go in. A call per source per port - never per key.
  - A port's pad (`has_pad`: DirectInput's joystick read, then the port's key bindings), its motors (`rumble`: SDL's
    controllers), the start-up (`devices_made` - the original's "DirectInput made" test -, `start`: DirectInput's state
    cleared, then SDL's controllers, settings, bindings and its window handler, then `ext::prepare_input`), and the
    original pad read for `InputPort::pull_data`, which PartyMod's poll replaced (`set_pad_mode`, `set_default_keys`,
    `read_pad`, `key_down`: DirectInput's).
  - What our tools read of the devices (the level editor, the new front end, the mouse menus, the restart tester,
    debug mode, THAW's BMX): the keyboard's state (`input::keys`: one array by SDL scan code - the USB keyboard usage -,
    read by key), the mouse (`input::mouse`), the first attached controller (`input::controller`), the look's held
    mouse and the cursor (`set_relative_mouse`, `show_cursor`), the size of the window an event names
    (`window_size`) - each from the first source that has it (SDL's).
  - Their events (`input::Event`, `input::add_event_handler`): the SDL source's window handler - third in the
    platform's pump, after the quit request and the window's focus - does the game's input's own handling, then hands
    each event to the handlers added (by `ext::prepare_input`, in the extensions' order; the editor's simulation
    later): the event's kind, key, repeat and modifiers, the mouse's place, motion, button, wheel and window, a
    controller's button or axis. Dear ImGui needs SDL's own events (its SDL2 platform backend takes text typed and
    edited, the keys, the mouse and the wheel, the window's events): the event carries SDL's for as long as it is handed
    round (`Event::native`), which the editor's UI passes untouched to `render::UiBackend::process_event`; nothing else
    reads it, and a queued event keeps only ours (`Event::kept`). The scan codes are named by SDL's
    `SDL_SCANCODE_*` constants (a numbering, not a call), and SDL's window queries (its size, `sys::sdl_window`) are the
    platform's window's.
  Only `input_dinput/` includes `<dinput.h>` and only app/ picks its source; outside the platform and the SDL source
  nothing calls SDL's input (the keyboard's state, the mouse, the controllers, the cursor, the event pump), only the
  SDL source includes `platform/keyboard.h`, and only it and app/ register with the platform's pump
  (tools/input_headers.test.ts; the screenshot key's sidecar, a dev tool's note of the mouse over the window, is the
  one exception). An editor exe adds the same sources (DirectInput's, SDL's) without the automation channel's.
- `platform/`: the operating system and the SDKs - the SDL window and its event pump, the C runtime's differences from the original's, crash reports, the GameSpy SDK's glue, and code
  addresses as plain pointers (`code_address`, `function_at`, `crt::library_function`);
  Winsock with IPv4 addresses and byte buffers (`sockets.h`), the GameSpy SDK's glue (`gamespy.h`: its callbacks as typed records), the registry's values (`registry.h`), the Windows audio session API
  (`mute_audio.h`), the owners of Win32 and C runtime handles (`unique_handle.h`) and the sinks behind the base
  layer's output (`output_sinks.h`).

`formats/` depends only on the standard library and `base/` (`ints.h`, `checksums.h`, `pointer_cell.h`, `raw_view.h`);
`wire/` also on `formats/` (the byte stream). They build and are checked without the game.

**The original's layouts stay in the boundary**: the named helpers that read memory through another type or keep a
pointer in an integer - `cell::` (`base/core/pointer_cell.h`), `raw::` (`base/core/raw_view.h`),
`memory::bits_as`, `function_at` - are used only there. Above it every field has its member and every pointer its
type; data from outside is decoded where it arrives. `npm test` forbids the helpers above the boundary
(`tools/layering.ts`). The QB bytecode readers (`engine/script/qb.cpp`,
`token_reader.cpp`, `precompiled.cpp`) count as boundary until the script runtime is replaced. They read the tokens
through `script::Tokens` (`engine/script/tokens.h`: a `core::Cursor` over the block of code, every read checked
against its extent); the runtime keeps plain pointers into code (a script's place, its loops, its callers' return
points) and `tokens_at` finds a pointer's block in a table of the blocks of code, noted where each is made and freed
(outside the game's heaps).
`.clang-tidy` keeps `reinterpret_cast` and integer-to-pointer casts out of `src/` entirely.

**The script values** are `script::Cell`s (`engine/script/cell.h`): a component's, a global symbol's or an array
element's 32 bits, read and written by the accessor of the kind its holder records (an externally tagged union -
the holders are pooled records of the original's sizes). The union access is inside `Cell` only. Structures and
arrays are made and freed by `script::new_struct` / `new_array` / `free_struct` / `free_array` (the pools'
factory and deleter), held in a scope or member by `script::StructPtr` / `ArrayPtr`.

**Fatal exits** (no Direct3D device, a texture that can't be made) end the process with `crt::exit_fatal`
(std::quick_exit after the C streams are flushed), as the normal end (`sys::fast_exit`) does: the static owners of
the singletons are never destroyed under the game's live objects.

**Layouts freed above the boundary change sizes**: the game's fixed-size pools (`memory::PoolPair` / `SlabBank`:
blend channels, animation slots, script structures...) are sized by `sizeof` of what they hold, and a pool refuses
an object bigger than its items - a class that grows past the original's byte size stops the game with a report
instead of running into its neighbour. The singletons are owned by type (`std::unique_ptr`, `memory::HeapUnique`);
a loaded `.pre` archive is an index of plain records (`packs::PackedFile`) over one block of unpacked files.
