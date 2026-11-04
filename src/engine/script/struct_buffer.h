#pragma once

// Script structures written to / read from a byte buffer (THUG's Scripts::emit_into_buffer / decode_from_bytes; network
// messages, saves). 0x00479840-0x0047A8D1.
//
// The format: for each component a type byte (the component type, with 0x80 when its name is index n of the
// global array 0x35115A20 - then one byte n follows -, 0x40 when it's index n of 0x25231F42 - two bytes n -,
// else the four-byte name); then the value: integers by size (0x12 zero, 0x10 / 0x11 unsigned byte / short,
// 0x0E / 0x0F signed byte / short, else 1 and four bytes), floats (2, or 0x13 for ones smaller than 0x006484A0),
// strings with their terminator, pairs / vectors as their floats, structures recursively, arrays as their type,
// two-byte count and elements; scripts are written as the checksum 0xB9C4F664. A zero byte ends a structure.

#include "base/core/within.h"
#include "base/core/checksums.h"
#include <cstddef>
#include <cstdint>

namespace util {
class ByteWriter;
} // namespace util

namespace script {

class Struct;
class Array;

// The writers below write at a util::ByteWriter (NOT ORIGINAL: the original's took a buffer pointer and the bytes left);
// each tests its fit as the original did - room for the longest name form (5 bytes) and the value - and returns the
// bytes written, 0 when they don't fit (the writer's bytes past its start are then not to be used).
// 0x004798F0: an integer component, in the smallest form for its value.
uint32_t emit_int(util::ByteWriter& out, checksums::Name name, int value);
// 0x00479A50: a float component (0x13 with no value for tiny ones).
uint32_t emit_real(util::ByteWriter& out, checksums::Name name, float value);
// 0x00479AC0 / 0x00479B20: a string / local string component.
uint32_t write_string(util::ByteWriter& out, const char* string, uint32_t name);
uint32_t write_local_string(util::ByteWriter& out, const char* string, uint32_t name);
// 0x00479B80: an array's type, count and elements (strings and nested values included).
uint32_t write_array_to_buffer(util::ByteWriter& out, Array* array, int assert);
// 0x00479E20: the structure (null: empty), with the terminating zero. `callback` (if any) is called before each
// component. Returns the bytes written, 0 when they don't fit (or a component can't be written).
uint32_t emit_into_buffer(Struct* structure, uint8_t* buffer, uint32_t size, bool assert, void (*callback)());
// 0x0047A070: how many bytes emit_into_buffer takes for the structure, written into a scratch block of `maxSize`
// (from the top-down heap): 0 if it doesn't fit.
uint32_t measure_buffer_bytes(Struct* structure, uint32_t max_size);
// 0x0047A0C0: the structure emptied and filled from the buffer's first `size` bytes (structures and arrays made from
// the script heap); returns the end of what was read. NOT ORIGINAL: the size (the original read on to the end
// marker); a buffer cut short ends the structure there.
const uint8_t* decode_from_bytes(Struct* structure, const uint8_t* buffer, size_t size);
// The same for a buffer the caller may change: the end as a pointer into it.
inline uint8_t* decode_from_bytes(Struct* structure, uint8_t* buffer, size_t size)
{
    return mutable_within(buffer, decode_from_bytes(structure, static_cast<const uint8_t*>(buffer), size));
}
// 0x0047A440: the array emptied and filled from the buffer; returns the end.
const uint8_t* read_array_from_buffer(Array* array, const uint8_t* buffer, size_t size);

} // namespace script
