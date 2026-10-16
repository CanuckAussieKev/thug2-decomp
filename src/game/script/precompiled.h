#pragma once

#include <cstdint>

#include "game/script/struct.h"

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
//     script: {checksum, size, stored size, code} (as CreateScriptSymbol makes them)
//     structure: a CStruct (8) then its components, each a CComponent (0x10) followed by its value as above (a
//       script component: its tokens up to and including '$'), structures and arrays nested the same way
//     array: a CArray (0x0C), then for more than one element their table (4 bytes each), then the elements'
//       values: strings one after another (padded once at the end), pairs, vectors, or nested structures/arrays
namespace Script {

// Load the pre-compiled .qb image `data` (named `filename`): relocate it and define its symbols, replacing
// symbols of the same names. `unused` is passed on as a flag no one reads.
void ParsePrecompiledQB(const char* filename, uint8_t* data, int unused);

// Called for each symbol of a pre-compiled file once it's relocated.
using PrecompiledSymbolCallback = void(__cdecl*)(CSymbolTableEntry* symbol, uint32_t argument, uint32_t flag);
// Relocate every symbol of the pre-compiled image `file` (offsets from `base`), passing each to `callback`
// (if any) with `argument` and `flag`.
void RelocatePrecompiledSymbols(uint8_t* file, uint32_t base, PrecompiledSymbolCallback callback, uint32_t argument,
                                uint32_t flag);
// Define the relocated symbol `symbol` of the file whose name's checksum is `fileChecksum`: it replaces (and
// frees) the symbol of the same name, if there is one, and is linked in.
void DefinePrecompiledSymbol(CSymbolTableEntry* symbol, uint32_t fileChecksum, uint32_t unused);

// Relocating (adding `base` to every stored offset); each returns the end of the item's data in the file.
uint8_t* RelocateSymbol(CSymbolTableEntry* symbol, uint32_t base);
uint8_t* RelocateStruct(CStruct* structure, uint32_t base);
uint8_t* RelocateComponent(CComponent* component, uint32_t base);
uint8_t* RelocateArray(CArray* array, uint32_t base);
// Only the array's element pointers (the table, or the single element in its place).
void RelocateArrayElements(CArray* array, uint32_t base);

} // namespace Script
