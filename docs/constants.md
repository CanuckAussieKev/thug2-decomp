# Named constants: where they live

Every number in the source that means something has a name (the magic-number check is on, at zero). There is no
central constants file: a constant lives with its owner.

- A value one function or one file uses is defined in that file's unnamed namespace (or in the function).
- A value a subsystem shares is defined once in the header of the subsystem whose meaning it is, and every user
  includes that header.
- Lower layers never include higher ones (base, platform, formats and wire are low; the engine is above them, with
  the backends beside it; then the game; the front end, the editor and the dev tools are on top - docs/architecture.md).
  When two layers share a value, the lower layer owns it.
  (a Direct3D state id the engine names, a Winsock error number the game tests) is the interface's, in the engine.
- One meaning, one constant: two values that are merely equal are not merged. Where the same thing is needed both
  as a `std::size_t` extent and as an `int` count for loops, the two forms sit next to each other in the owner.

## Enums or constants

The values of one closed thing - the kinds, states, ids and flags a field or parameter holds - are a scoped enum with
the same underlying type the value always had (`enum class QbValueKind : uint8_t`), so sizes, layouts and code stay as
they were. A value the file or the wire can carry that the enum doesn't list is still representable: the boundary
layer converts the raw byte or word once, where it is read (`static_cast<MessageId>(reader.u8())`,
`script::as_token(byte)`), and writes it back with `core::to_underlying` (`base/core/enum_ops.h`). A set of bits is
a flag enum: declared with `constexpr bool enable_flag_operators(Flags) { return true; }` beside it, it has |, &, ^, ~
and the compound forms, and `core::any(flags & Flags::kX)` tests a bit. A field the original keeps wider than the
enum stays its own width behind typed accessors (`Hub::service_state()`). Switches over an enum list every case and
then need no `default:`, so a new enumerator is reported where it isn't handled.

Everything else stays a `constexpr` constant: checksums (an open set), sizes, extents, offsets, tunings, timings,
colours, and values used as numbers (table indexes, ranges, values combined with arithmetic) - where an enum would need
a cast at most of its uses.

The index below lists each kind of shared constant or enum and the header that holds it.

| Kind | Header |
|---|---|
| Player slots of a network game (8: the rider slots, the per-skater records) | `game/online/player_info.h` (`online::kPlayerSlotCount`, the player flags) |
| Network message ids, the transport's and the game's (`online::MessageId`), and the sequenced message groups (`MessageGroup`) | `wire/message_ids.h` (THUGPRO's own ids: `game/online/thugpro_peer.h`; the model viewer link's: `engine/sys/viewer_object.h`) |
| The transport's stream layouts | `wire/net_wire.h` |
| The object-update stream (the skaters' states) | `wire/object_updates.h` |
| GameSpy game / product ids, the SDK threads' stack | `game/online/online_identity.h` |
| Server list sizes; the online service's states (`online::ServiceState`) | `game/online/server_list.h`, `game/online/service.h` |
| Join refusal reasons and join phases (`JoinRefusedReason`, `EntryPhase`) | `game/online/gamenet_manager.h`, `game/online/hub_join.h` |
| A player's flags (`online::PlayerFlag`, a flag enum) | `game/online/player_info.h` |
| Park floor size and levels, the saved park map's layout | `formats/park_map_format.h` |
| Park editor limits and sizes | `game/park_editing/park.h` |
| QB script tokens (`script::QbToken`) and token sizes | `engine/script/qb.h` |
| Script value kinds (`script::QbValueKind`) | `engine/script/param_struct.h` |
| Collision face flags and the probes' ignore / require masks | `engine/render/coll_obj.h` |
| Path buffer sizes (a disk path, an overlay path) | `engine/file/file_load.h` |
| Language numbers | `base/core/language.h`; territory, hardware and display numbers: `engine/sys/game_config.h` |
| UTF-8 byte values | `base/core/str.h` |
| A level's load script prefix | `game/front_end/start_level.h` |
| 2D layout sizes (640 x 480, the screen elements' 640 x 448) | `engine/gfx/screen.h` |
| The new front end's 1280 x 720 design canvas | `frontend/new_frontend_model.h` |
| The screen elements' float constants as the original stores them | `engine/ui/screen_constants.h` |
| The renderer's render-state ids (`render::RenderStateId`); Direct3D values (address modes, texture ops, lock flags) | `render_d3d9/render_state.h` |
| The materials' blend modes and the meshes' vertex format bits | `engine/gfx/mesh_formats.h` |
| The PC geometry's record layouts (vertex / index buffers, meshes, instances: their offsets and sizes) | `engine/gfx/mesh_records.h` |
| pi and a full turn | `base/maths/trig.h` (`maths::kPi`, `maths::kTwoPi`) |
| A flat 4 x 4 matrix's float count and element indices | `base/maths/mat4.h` (`maths::kMatrixFloats`, `kMatXx` ... `kMatTw`) |
| Units (inches per foot), double-to-integer limits | `base/maths/maths.h` |
| Key binding slots, ports, the draw distance setting | `engine/pad_input/port_controls.h` |
| Pad button masks, control data layout | `engine/pad_input/input_hub.h`, `engine/pad_input/device.h` |
| Sound and music limits | `engine/sound/sfx.h`, `engine/audio/music_player.h` |
| The level editor's view, colour channels, box corners, mesh walk, text limits | `editor/level_editor.h` |
| The level editor's message durations | `editor/level_editor_hud.h` |
| The level editor panels' shared colours and sizes | `editor/level_editor_ui_theme.h`, `editor/level_editor_ui_layout.h` |
| The level editor's goal limits and defaults | `editor/level_editor_goals.h` |
| The level editor's chain operations (`chains::ChainOp`) | `editor/level_editor_chains.h` |
| The scoped-enum helpers (`core::to_underlying`, the flag operators, `core::any`) | `base/core/enum_ops.h` |

## Two kinds that stay in each file

- **Checksums by their text**: a checksum is written as `constexpr std::uint32_t kText = 0x...U; // text`, in the
  file that uses it, the name made from the text it is the checksum of (`kTriggerScript` for `TriggerScript`). Where
  the text is not known the name says what the checksum is used for and the comment says "text unknown". The same
  text in two files gives two definitions of the same number; each states the text, so they can't drift apart.
  There are no placeholder names built from the number (`kHash` plus eight hex digits): a checksum whose text is in
  the game's strings is named after it (the comment gives the text, and says "letter case unknown" where only the
  CRC of the lower-cased text matched); one whose text isn't found is named for its use ("text unknown (...)"). A
  number that turned out not to be a checksum is what it is: a float constant (`kStickUnit`, `maths::kPi`), a
  float's bit pattern compared as bits (`kFloatOneBits`), or an ARGB colour (`kAwakeColour`).
- **Layout checks**: the offsets and sizes an original structure is checked against
  (`static_assert(offsetof(X, f) == kXFOffset)`) are `std::size_t` constants just before the structure.

Small per-file helpers (a hex base, the byte shifts 8 / 16 / 24, milliseconds per second) stay in the file that
uses them unless one subsystem shares them.
