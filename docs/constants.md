# Named constants: where they live

Every number in the source that means something has a name (the magic-number check is on, at zero). There is no
central constants file: a constant lives with its owner.

- A value one function or one file uses is defined in that file's unnamed namespace (or in the function).
- A value a subsystem shares is defined once in the header of the subsystem whose meaning it is, and every user
  includes that header.
- Lower layers never include higher ones (wire, formats, gpu, platform and core are low; the engine is above them;
  the game, the front end and the editor are on top). When two layers share a value, the lower layer owns it.
- One meaning, one constant: two values that are merely equal are not merged. Where the same thing is needed both
  as a `std::size_t` extent and as an `int` count for loops, the two forms sit next to each other in the owner.

The index below lists each kind of shared constant and the header that holds it.

| Kind | Header |
|---|---|
| Player slots of a network game (8: the rider slots, the per-skater records) | `engine/net/player_info.h` (`online::kPlayerSlotCount`, the player flags) |
| Network message ids with a reader / writer | `wire/gamenet_messages.h`, `wire/play_messages.h`, `wire/skater_messages.h`, `wire/skate_net_messages.h` |
| Network message ids without one, and the sequenced message groups | `wire/message_ids.h` |
| The transport's message ids and stream layouts | `wire/net_wire.h` |
| The object-update stream (the skaters' states) | `wire/object_updates.h` |
| GameSpy game / product ids, the SDK threads' stack | `engine/net/online_identity.h` |
| Server list sizes, online service states | `engine/net/server_list.h`, `engine/net/service.h` |
| Park floor size and levels, the saved park map's layout | `formats/park_map_format.h` |
| Park editor limits and sizes | `game/park_editing/park.h` |
| QB script tokens and token sizes | `engine/script/qb.h` (`script::kToken*`) |
| Collision face flags and the probes' ignore / require masks | `engine/render/coll_obj.h` |
| Path buffer sizes (a disk path, an overlay path) | `engine/file/file_load.h` |
| Language numbers | `engine/core/language.h`; territory, hardware and display numbers: `engine/sys/game_config.h` |
| UTF-8 byte values | `engine/core/str.h` |
| A level's load script prefix | `frontend/start_level.h` |
| 2D layout sizes (640 x 480, the screen elements' 640 x 448) | `engine/gfx/screen.h` |
| The new front end's 1280 x 720 design canvas | `frontend/new_frontend_model.h` |
| The screen elements' float constants as the original stores them | `frontend/screen_constants.h` |
| Blend modes, Direct3D values (address modes, texture ops, lock flags, vertex format bits) | `engine/gfx/render_state.h` |
| pi and a full turn | `engine/maths/trig.h` (`maths::kPi`, `maths::kTwoPi`) |
| A flat 4 x 4 matrix's float count and element indices | `engine/maths/mat4.h` (`maths::kMatrixFloats`, `kMatXx` ... `kMatTw`) |
| Units (inches per foot), double-to-integer limits | `engine/maths/maths.h` |
| Key binding slots, ports, the draw distance setting | `platform/platform_input.h` |
| Pad button masks, control data layout | `engine/pad_input/input_hub.h`, `engine/pad_input/device.h` |
| Sound and music limits | `engine/sound/sfx.h`, `engine/audio/music_player.h` |
| The level editor's view, colour channels, box corners, mesh walk, text limits | `editor/level_editor.h` |
| The level editor's message durations | `editor/level_editor_hud.h` |
| The level editor panels' shared colours and sizes | `editor/level_editor_ui_theme.h`, `editor/level_editor_ui_layout.h` |
| The level editor's goal limits and defaults | `editor/level_editor_goals.h` |

## Two kinds that stay in each file

- **Checksums by their text**: a checksum is written as `constexpr std::uint32_t kText = 0x...u; // text`, in the
  file that uses it, the name made from the text it is the checksum of (`kTriggerScript` for `TriggerScript`). Where
  the text is not known the name says what the checksum is used for and the comment says "text unknown". The same
  text in two files gives two definitions of the same number; each states the text, so they can't drift apart.
- **Layout checks**: the offsets and sizes an original structure is checked against
  (`static_assert(offsetof(X, f) == kXFOffset)`) are `std::size_t` constants just before the structure.

Small per-file helpers (a hex base, the byte shifts 8 / 16 / 24, milliseconds per second) stay in the file that
uses them unless one subsystem shares them.
