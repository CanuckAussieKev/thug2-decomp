# Architecture

## Layers

The game is one executable (`thug2-decomp.exe`, CMake target `thug2`) built from `src/`, in layers. A layer may
include the layers below it, never the ones above.

```
editor/  frontend/          the level editor; menus and screens
game/                       skating, the rider, goals, parks, saves
engine/                     core, maths, lists, files, assets, script, objects, renderer, audio, network, pad input
-------------------------   the boundary: where bytes come from outside the game
formats/  wire/  gpu/       file formats; network messages; GPU buffers
platform/                   Win32, Direct3D, DirectSound, DirectInput, SDL, the GameSpy SDK, code addresses
```

**The boundary layer** is where the game meets bytes it doesn't lay out itself. Only there do byte layouts matter;
everything above it uses the game's own types and may change them freely.

- `formats/` (static library `thug2_formats`): a reader - and, where the game writes the format, a writer - for each
  file the game loads or saves (and, for the files used in place - collision, animation keys - the accessors that read
  their records, e.g. `collision_format::FaceTable` for a collision object's faces, `relocate_object` /
  `object_record` / the tree's link accessors for its fix-up in place): PRE archives and their LZSS (`pre_format`, `lzss`), cutscene bundles, texture
  dictionaries and images, scenes / models / skins (`scene_format`), collision, skeletons, animations, fonts, sound
  effects, the streamed sounds' index, the park map and its rails, the save file. Each takes a byte span
  (`ByteReader` / `ByteWriter`, `byte_stream.h`: little-endian, unaligned, bounds-checked) and gives plain records.
  `thug2_format_check` (`formats/check/`) runs every reader over a game folder, compares it with an independent
  reading of the bytes, and round-trips the writers.
- `wire/` (static library `thug2_wire`): the packet framing (`net_wire`) and every game message
  (`gamenet_messages`, `skater_messages`, `play_messages`, `skate_net_messages`), byte-compatible with the
  original's and THUGPRO's peers. Encoders and decoders over the same byte stream; a short or oversized message is
  refused, not read past.
- `gpu/`: the Direct3D vertex and index buffers (`vertex_buffers`): made, filled through their copies in memory,
  written again after a device reset, torn down. Game code sees the buffers' API, not the device; it reads and writes
  vertices by attribute through `gpu::VertexView` / `VertexWriter` (`vertex_view.h`: a stride and offsets, never the
  bytes themselves).
- `platform/`: the operating system and the SDKs - the window and input (SDL, DirectInput), Direct3D's device,
  DirectSound, the C runtime's differences from the original's, crash reports, the GameSpy SDK's glue, and code
  addresses as plain pointers (`code_address`, `function_at`, `crt::library_function`); the Bink SDK (`bink.h`),
  Winsock with IPv4 addresses and byte buffers (`sockets.h`), Direct3D's matrices over the game's (`d3d_matrix.h`),
  the GameSpy SDK's glue (`gamespy.h`: its callbacks as typed records), the registry's values (`registry.h`), D3DX
  behind the game's own calls (`d3dx.h`), the Windows audio session API (`mute_audio.h`) and the pointer whose bits are
  all ones (`all_ones.h`: INVALID_HANDLE_VALUE, the list heads' marker).

`formats/` and `wire/` depend only on the standard library and `engine/core`'s leaf headers (`ints.h`,
`checksums.h`, `pointer_cell.h`, `raw_view.h`); they build and are checked without the game.

**The original's layouts stay in the boundary**: the named helpers that read memory through another type or keep a
pointer in an integer - `cell::` (`engine/core/pointer_cell.h`), `raw::` (`engine/core/raw_view.h`),
`memory::bits_as`, `function_at` - are used only there. Above it every field has its member and every pointer its
type; data from outside is decoded where it arrives. `npm test` forbids the helpers above the boundary
(`tools/layering.ts`). The QB bytecode readers (`engine/script/qb.cpp`,
`token_reader.cpp`, `precompiled.cpp`) count as boundary until the script runtime is replaced.
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
