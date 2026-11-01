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
  file the game loads or saves: PRE archives and their LZSS (`pre_format`, `lzss`), cutscene bundles, texture
  dictionaries and images, scenes / models / skins (`scene_format`), collision, skeletons, animations, fonts, sound
  effects, the streamed sounds' index, the park map and its rails, the save file. Each takes a byte span
  (`ByteReader` / `ByteWriter`, `byte_stream.h`: little-endian, unaligned, bounds-checked) and gives plain records.
  `thug2_format_check` runs every reader over a game folder and round-trips the writers.
- `wire/` (static library `thug2_wire`): the packet framing (`net_wire`) and every game message
  (`gamenet_messages`, `skater_messages`, `play_messages`, `skate_net_messages`), byte-compatible with the
  original's and THUGPRO's peers. Encoders and decoders over the same byte stream; a short or oversized message is
  refused, not read past.
- `gpu/`: the Direct3D vertex and index buffers (`vertex_buffers`): made, filled through their copies in memory,
  written again after a device reset, torn down. Game code sees the buffers' API, not the device.
- `platform/`: the operating system and the SDKs - the window and input (SDL, DirectInput), Direct3D's device,
  DirectSound, the C runtime's differences from the original's, crash reports, the GameSpy SDK's glue, and code
  addresses as plain pointers (`code_address`, `function_at`, `crt::library_function`).

`formats/` and `wire/` depend only on the standard library and `engine/core`'s leaf headers (`ints.h`,
`checksums.h`, `pointer_cell.h`, `raw_view.h`); they build and are checked without the game.

**What is left of the original's layouts above the boundary** is spelled with named helpers, never a cast:
`cell::` (`engine/core/pointer_cell.h`) where a pointer still travels in a 32-bit integer (the script values' cells,
callbacks' user words), `raw::` (`engine/core/raw_view.h`) where an in-memory structure is still read through another
type or at a byte offset. Each use is a place still to be given its real type or member; their number only goes down.
`.clang-tidy` keeps `reinterpret_cast` and integer-to-pointer casts out of `src/` entirely.
