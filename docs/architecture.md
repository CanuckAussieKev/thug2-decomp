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
engine/                     memory, lists, files, assets, script, objects, renderer, audio, network transport, pad input,
                            ui (the original's screen elements), ext (the extension points)
-------------------------   the boundary: where bytes come from outside the game
formats/  wire/             file formats; network messages
platform/                   Win32, the SDL window, DirectSound's set-up, the GameSpy SDK, crash reports, code addresses
-------------------------
base/                       checksums, formatting, text, checked indexing, maths, the bit stream (no OS, no SDK)
```

**The base layer** (`src/base`, static library `thug2_base`) is what every layer uses and nothing of the OS: `core`
(checksums, printf-style formatting, text, number parsing, checked indexing, the enum, cast and pointer helpers, the
compiler's few names), `maths` (vectors, matrices - their product is D3DX's in plain C++, bit for bit, checked by
`thug2_maths_check` -, quaternions, shape tests), `lists` (two containers that don't use the game's heaps) and
`util` (the bit stream, BPS patches). It includes only the standard library and GSL. What it can't do itself it
hands to a sink the platform installs first thing in WinMain (`platform/output_sinks.h`): the debugger output
(`base/core/dbg.h`) and error lines into thug2-decomp.log (`base/core/log_sink.h`). The game's heaps are
`engine/memory` (they use the run log, the C runtime's heap and the game config), and what is built on them stays in
the engine (`engine/lists`' chains and maps, `engine/util`'s LZSS compressor and text store).

**The platform layer** (`src/platform`, static library `thug2_platform`) is the OS and the SDKs, and includes only
`base/` and their headers. It doesn't call up: what the game does on the platform's events, and where it keeps the
platform's devices, is wired by `app/`. The SDL event pump calls the handlers registered with it (the quit request is
the start-up's, `app/pc_setup.cpp`; the window's focus WinMain's); `sys::create_sdl_window` returns the window's
handle, which WinMain keeps where the platform's window is (`sys::game_window`), and `os_layer::init_direct_sound` the
DirectSound device, which it keeps in the audio's state; the base layer's sinks are installed by `app/main.cpp`. What uses the engine's state is the engine's: the frame
clock (`engine/sys/stopwatch`, `game_clock`), the renderer's Direct3D buffers and queries
(`engine/gfx/pc_d3d_resources`), and the original's DirectInput side - its pad read, the registry options and input
set-up (`engine/pad_input/direct_input`, `input_setup`).

**The composition** (`src/app`): `main.cpp`, WinMain (`winmain.cpp`: the command line, the window, the devices),
PartyMod's start-up (`pc_setup.cpp`) and the game's main (`startup.cpp`: the singletons, modules and the main loop).
It may include every layer. `src/devtools` sits beside the editor and the new front end: the screenshot key, the
test-automation channel (`automation`), the restart tester, the start level and the test view.

**The extension points** (`engine/ext/extension.h`): the engine and the game never call the editor, the front end or
the dev tools. Where something we added runs inside the original's frame, the original code calls `ext::` at that
place - the front end's input logic (`ext::menu_frame`), the 3D frame (`before_draw_world`, `place_camera`,
`near_plane` / `view_angle`, `view_drawn`, `draw_over_frame`), the main loop (`before_present`, `keeps_every_frame`,
`device_lost`), the screen elements' layout pass, the input set-up and poll (`prepare_input`, `game_owner` and its
`pass_pause_key`, `game_input_blocked`, `add_pad_buttons`), a scene loaded, the data overlay, the start-up's movies,
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
thug2_engine  <-  thug2_render_d3d9 (the graphics backend: linked by the exe - and the editor, below)
every library  <-  thug2 (src/app: thug2-decomp.exe)
```

The checks in `src/tests` link only what they check (`thug2_format_check`: wire and formats; `thug2_maths_check`:
base; `thug2_parse_check` / `thug2_printf_check`: formats). Third-party and system libraries are linked by the
library that calls them (SDL, DirectSound, Winsock, D3D9, D3DX and the GameSpy SDK by the platform; LZHAM and
DirectInput by the engine; imgui by the graphics backend - the loading console's panel -, the editor and the front
end). `node tools/cmake_layers.ts` (part of `npm test`)
reads the `CMakeLists.txt` files and fails when a library links outside the map or doesn't reach a library the map
allows it, when a source sits in another library's folder, or when a `.cpp` is compiled by no target or by two.
The one exception is the editor / front end / dev tools' sibling edges (round 29's): they link each other exactly
where `tools/layers_baseline.txt` still has include lines between them - static libraries that need each other, which
CMake repeats on the link line for a linker that reads each library once (GNU ld) - and a sibling link with no
baseline line left fails. A sibling's own links are its own: the front end linking the editor doesn't count as
reaching what the editor links. The editor is the one library besides the exe that links the graphics backend
(tools/layers.json allows it): its Dear ImGui backend and its preview's and ground's drawing still call Direct3D 9
until they go behind the engine's interfaces.

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
  (tools/layers.json: render_d3d9 is strict and only the app - and, for now, the editor - may include it), so the
  engine builds without Direct3D.
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
    `d3d9_handles.h`, `d3d_matrix.h` (Direct3D's matrices over the game's), `platform/d3dx_jpg.h` and the editor's ImGui
    texture id (`platform/imgui_texture_id.h`).
- `platform/`: the operating system and the SDKs - the SDL window and its event pump, DirectSound's set-up, the C runtime's differences from the original's, crash reports, the GameSpy SDK's glue, and code
  addresses as plain pointers (`code_address`, `function_at`, `crt::library_function`); the Bink SDK (`bink.h`),
  Winsock with IPv4 addresses and byte buffers (`sockets.h`), the GameSpy SDK's glue (`gamespy.h`: its callbacks as typed records), the registry's values (`registry.h`), D3DX
  behind the renderer's own calls (`d3dx.h`: the graphics backend and the occluders' transform call it), the Windows audio session API
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
