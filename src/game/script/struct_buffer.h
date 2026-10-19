#pragma once

// Script structures written to / read from a byte buffer (THUG's Script::WriteToBuffer / ReadFromBuffer; network
// messages, saves). 0x00479840-0x0047A8D1.
//
// The format: for each component a type byte (the component type, with 0x80 when its name is index n of the
// global array 0x35115A20 - then one byte n follows -, 0x40 when it's index n of 0x25231F42 - two bytes n -,
// else the four-byte name); then the value: integers by size (0x12 zero, 0x10 / 0x11 unsigned byte / short,
// 0x0E / 0x0F signed byte / short, else 1 and four bytes), floats (2, or 0x13 for ones smaller than 0x006484A0),
// strings with their terminator, pairs / vectors as their floats, structures recursively, arrays as their type,
// two-byte count and elements; scripts are written as the checksum 0xB9C4F664. A zero byte ends a structure.

#include <cstdint>

namespace Script {

class CStruct;
class CArray;

// 0x00479840: the type byte and the name (shortened when the name is in one of the two arrays); returns the end.
uint8_t* WriteNameAndType(uint8_t* buffer, uint8_t type, uint32_t name);
// 0x004798F0 (name in ECX, value in EBX): an integer component, in the smallest form for its value. Returns the
// bytes written - 0 when they don't fit in `size`.
uint32_t WriteInteger(uint32_t name, int value, uint8_t* buffer, uint32_t size);
void WriteIntegerStub();
// 0x00479A50 (buffer in ECX, name in EDX, size in ESI): a float component (0x13 with no value for tiny ones).
uint32_t WriteFloat(uint8_t* buffer, uint32_t name, uint32_t size, float value);
void WriteFloatStub();
// 0x00479AC0 / 0x00479B20 (buffer in EAX, string in ECX): a string / local string component.
uint32_t WriteString(uint8_t* buffer, const char* string, uint32_t name, uint32_t size);
uint32_t WriteLocalString(uint8_t* buffer, const char* string, uint32_t name, uint32_t size);
void WriteStringStub();
void WriteLocalStringStub();
// 0x00479B80: an array's type, count and elements (strings and nested values included). Returns the bytes
// written (0: they don't fit).
uint32_t WriteArrayToBuffer(CArray* array, uint8_t* buffer, uint32_t size, int assert);
// 0x00479E20: the structure (null: empty), with the terminating zero. `callback` (if any) is called before each
// component. Returns the bytes written, 0 when they don't fit (or a component can't be written).
uint32_t WriteToBuffer(CStruct* structure, uint8_t* buffer, uint32_t size, int assert, void (*callback)());
// 0x0047A070: how many bytes WriteToBuffer takes for the structure, written into a scratch block of `maxSize`
// (from the top-down heap): 0 if it doesn't fit.
uint32_t CalculateBufferSize(CStruct* structure, uint32_t maxSize);
// 0x0047A0C0: the structure emptied and filled from the buffer (structures and arrays made from the script
// heap); returns the end of what was read.
const uint8_t* ReadFromBuffer(CStruct* structure, const uint8_t* buffer);
// 0x0047A440: the array emptied and filled from the buffer; returns the end.
const uint8_t* ReadArrayFromBuffer(CArray* array, const uint8_t* buffer);

} // namespace Script
