#pragma once

#include <cstdint>
#include <span>

#include "engine/script/param_struct.h"

// Pre-compiled .qb files (magic 0xB075EF6F; the platform's extension, "xbx" on PC): an image of finished global
// symbols, loaded as it is and kept loaded. The file's own symbol table entries are linked into the table in
// place; their values (strings, pairs, vectors, scripts, structures, arrays) follow them in the file, with every
// pointer stored as an offset from the start of the file, which loading turns into an address ("relocates").
//
// The layout (all little-endian, every item 4-byte aligned):
//   +0x00 0xB075EF6F   +0x04 the file's size   +0x08..+0x1B (not used by the loader)
//   +0x1C symbols, up to the file's end: a CSymbolTableEntry (0x14 bytes) and its value after it:
//     integer, float, name, C function, member function, 0x0B (and any other type): nothing (the value is in the entry)
//     string / local string: the text (null-terminated, padded)    pair: a CPair (0x0C)   vector: a CVector (0x10)
//     script: {checksum, size, stored size, code} (as define_script_symbol makes them)
//     structure: a CStruct (8) then its components, each a CComponent (0x10) followed by its value as above (a
//       script component: its tokens up to and including '$'), structures and arrays nested the same way
//     array: a CArray (0x0C), then for more than one element their table (4 bytes each), then the elements'
//       values: strings one after another (padded once at the end), pairs, vectors, or nested structures/arrays
namespace script {

// The image's header (magic, size, and 0x14 bytes not used by the loader).
constexpr uint32_t kPrecompiledHeaderSize = 0x1C;

// Load the pre-compiled .qb image `data` (named `filename`; its bytes): relocate it and define its symbols, replacing
// symbols of the same names. `unused` is passed on as a flag no one reads.
void parse_precompiled_qb(const char* filename, std::span<uint8_t> data, int unused);
// NOT ORIGINAL - the image of the file whose name's checksum is `file_checksum` is going (unload_qb): it no longer holds
// code (tokens.h).
void forget_precompiled_image(uint32_t file_checksum);

// Called for each symbol of a pre-compiled file once it's relocated.
using PrecompiledSymbolCallback = void (*)(SymbolTableEntry* symbol, uint32_t argument, uint32_t flag);
// Relocate every symbol of the pre-compiled image `file` (its bytes; offsets from `base`), passing each to `callback`
// (if any) with `argument` and `flag`.
void relocate_precompiled_symbols(std::span<uint8_t> file, uint32_t base, PrecompiledSymbolCallback callback,
                                  uint32_t argument, uint32_t flag);
// Define the relocated symbol `symbol` of the file whose name's checksum is `file_checksum`: it replaces (and
// frees) the symbol of the same name, if there is one, and is linked in.
void define_precompiled_symbol(SymbolTableEntry* symbol, uint32_t file_checksum, uint32_t unused);

// Relocating (adding `base` to every stored offset) an item of the image `image`; each returns where the item's data
// ends in the image (an offset in it).
uint32_t relocate_symbol(std::span<uint8_t> image, SymbolTableEntry* symbol, uint32_t base);
uint32_t relocate_struct(std::span<uint8_t> image, Struct* structure, uint32_t base);
uint32_t relocate_component(std::span<uint8_t> image, Component* component, uint32_t base);
uint32_t relocate_array(std::span<uint8_t> image, Array* array, uint32_t base);
// Only the array's element pointers (the table, or the single element in its place).
void relocate_array_elements(Array* array, uint32_t base);

} // namespace script
