#pragma once

#include <cstdint>
#include <span>

#include "engine/script/param_struct.h"

// Pre-compiled .qb files (magic 0xB075EF6F; the platform's extension, "xbx" on PC): an image of finished global
// symbols. Their values (strings, pairs, vectors, scripts, structures, arrays) follow each symbol in the file, with
// every pointer stored as an offset from the start of the file.
//
// The layout (all little-endian, every item 4-byte aligned, every field 32 bits - a pointer too, as an offset):
//   +0x00 0xB075EF6F   +0x04 the file's size   +0x08..+0x1B (not used by the loader)
//   +0x1C symbols, up to the file's end: a symbol table entry (0x14 bytes: flags, type, -, name, -, value, next) and
//     its value after it:
//     integer, float, name, C function, member function, 0x0B (and any other type): nothing (the value is in the entry)
//     string / local string: the text (null-terminated, padded)    pair: 0x0C bytes   vector: 0x10 bytes
//     script: {checksum, size, stored size, code} (as define_script_symbol makes them)
//     structure: 8 bytes (+4 its first component) then its components, each 0x10 bytes (kind, -, name, value, next)
//       followed by its value as above (a script component: its tokens up to and including '$'), structures and
//       arrays nested the same way
//     array: 0x0C bytes (-, type, -, size, element), then for more than one element their table (4 bytes each), then
//       the elements' values: strings one after another (padded once at the end), pairs, vectors, or nested
//       structures / arrays
//
// The original used the image as its own symbols: the records are its 32-bit in-memory ones, and loading turned each
// offset into an address in place ("relocating": 0x0046CA50 and the relocate_* functions it calls), linking the
// file's entries into the symbol table. NOT ORIGINAL - here the image is read as a file and unpacked into the script
// runtime's own pooled records (as a .qb's symbols are made), so nothing depends on the records' in-memory layout
// (a cell is a pointer wide: docs/design/x64-port.md). The symbols own their values like any other; the image itself
// stays loaded until its file is unloaded, as before, but nothing refers into it.
namespace script {

// The image's header (magic, size, and 0x14 bytes not used by the loader).
constexpr uint32_t kPrecompiledHeaderSize = 0x1C;

// Load the pre-compiled .qb image `data` (named `filename`; its bytes): define its symbols, replacing symbols of the
// same names. `unused` is passed on as a flag no one reads.
void parse_precompiled_qb(const char* filename, std::span<const uint8_t> data, int unused);

// Unpack every symbol of the pre-compiled image `file` (its bytes) into the symbol table, each marked as the file's
// whose name's checksum is `file_checksum` and replacing (freeing) the symbol of the same name, if there is one
// (0x0046CA50 relocated them, calling 0x00478DE0 for each).
void unpack_precompiled_symbols(std::span<const uint8_t> file, uint32_t file_checksum);

} // namespace script
